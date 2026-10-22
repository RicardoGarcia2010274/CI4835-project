#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <ctime>
#include <algorithm>

struct MensajeBuzon {
    std::string contenido;
    bool borrado;
};

struct Cliente {
    int fd;
    std::string buffer;
    std::string apodo;
    bool es_http;
    std::vector<MensajeBuzon> buzon_cargado;
};

std::map<int, Cliente> clientes;
std::map<std::string, std::vector<int>> canales;
std::map<std::string, std::vector<std::string>> historial_canales;
std::vector<struct pollfd> fds;

int crear_socket(int puerto) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(puerto);
    bind(fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(fd, 200);
    return fd;
}

void cargar_buzon(Cliente& c) {
    c.buzon_cargado.clear();
    std::ifstream archivo("buzon_" + c.apodo + ".txt");
    if (!archivo.is_open()) return;
    
    std::string linea, msg_actual;
    while (std::getline(archivo, linea)) {
        if (linea.find("From:") == 0 && !msg_actual.empty()) {
            c.buzon_cargado.push_back({msg_actual, false});
            msg_actual = "";
        }
        msg_actual += linea + "\n";
    }
    if (!msg_actual.empty()) c.buzon_cargado.push_back({msg_actual, false});
}

void guardar_buzon(const Cliente& c) {
    std::ofstream archivo("buzon_" + c.apodo + ".txt", std::ios::trunc);
    for (const auto& msg : c.buzon_cargado) {
        if (!msg.borrado) archivo << msg.contenido;
    }
}

void procesar_comando_chat(int fd, const std::string& comando) {
    std::string resp;
    Cliente& c = clientes[fd];

    if (comando.find("NICK ") == 0) {
        std::string nuevo_apodo = comando.substr(5);
        nuevo_apodo.erase(nuevo_apodo.find_last_not_of(" \n\r\t") + 1);

        c.apodo = nuevo_apodo;
        cargar_buzon(c);
        resp = "201 Bienvenido, " + c.apodo + " !" +"\r\n";

        //Esto le indica al cliente los mensajes que tiene en el buzon que no estan 
        //leidos 
        int no_leidos = 0;
        for (const auto& msg : c.buzon_cargado) {
            if (!msg.borrado) no_leidos++;
        }
        if (no_leidos > 0) {
            resp += "303 " + std::to_string(no_leidos) + "\r\n";
        }
    } 
    else if (comando.find("JOIN ") == 0) {
        std::string canal = comando.substr(5);
        canal.erase(canal.find_last_not_of(" \n\r\t") + 1);

        if (c.apodo.empty()) {
            resp = "530 No registrado\r\n";
        } else if (canal.empty() || canal[0] != '#') {
            resp = "501 Sintaxis invalida\r\n";
        } else {
            if (std::find(canales[canal].begin(), canales[canal].end(), fd) == canales[canal].end()) {
                canales[canal].push_back(fd);
            }
            resp = "210 Unida a " + canal + "\r\n";
        }
    }
    else if (comando.find("MSG ") == 0) {
        size_t espacio = comando.find(' ', 4);
        if (espacio != std::string::npos) {
            std::string canal = comando.substr(4, espacio - 4);
            std::string texto = comando.substr(espacio + 1);
            
            bool es_miembro = false;
            if (canales.count(canal) > 0) {
                auto it = std::find(canales[canal].begin(), canales[canal].end(), fd);
                if (it != canales[canal].end()) {
                    es_miembro = true;
                }
            }

            if (!es_miembro) {
                resp = "442 No eres miembro del canal\r\n";
            } else {
                std::string evento = "300 " + canal + " " + c.apodo + ": " + texto + "\r\n";
                historial_canales[canal].push_back(c.apodo + ": " + texto);
                if (historial_canales[canal].size() > 50) historial_canales[canal].erase(historial_canales[canal].begin());

                for (int miembro_fd : canales[canal]) {
                    if (miembro_fd != fd) send(miembro_fd, evento.c_str(), evento.length(), 0);
                }
                resp = "250 OK\r\n";
            }
        } else {
            resp = "501 Sintaxis invalida\r\n";
        }
    }
    else if (comando.find("WHO ") == 0) {
        std::string canal = comando.substr(4);
        canal.erase(canal.find_last_not_of(" \n\r\t") + 1);
        
        if (canales.count(canal) > 0) {
            int miembros = 0;
            for (int miembro_fd : canales[canal]) {
                resp += "213-" + clientes[miembro_fd].apodo + "\r\n";
                miembros++;
            }
            resp += "213 " + std::to_string(miembros) + " miembros\r\n";
        } else {
            resp = "551 Canal inexistente\r\n";
        }
    }

    else if (comando == "LIST") {
        int count = 0;
        for (size_t i = 0; i < c.buzon_cargado.size(); ++i) {
            if (!c.buzon_cargado[i].borrado) {
                resp += "221-" + std::to_string(i + 1) + " " + std::to_string(c.buzon_cargado[i].contenido.length()) + "\r\n";
                count++;
            }
        }
        resp += "221 " + std::to_string(count) + " mensajes listados\r\n";
    }
    else if (comando.find("PRIV ") == 0) {
        size_t espacio = comando.find(' ', 5);
        if (espacio != std::string::npos) {
            std::string dest = comando.substr(5, espacio - 5);
            std::string texto = comando.substr(espacio + 1);
            
            int dest_fd = -1;
            for (const auto& par : clientes) {
                if (par.second.apodo == dest && !par.second.es_http) { dest_fd = par.first; break; }
            }

            if (dest_fd != -1) {
                std::string msg = "301 " + c.apodo + " : " + texto + "\r\n";
                send(dest_fd, msg.c_str(), msg.length(), 0);
                resp = "250 OK\r\n";
            } else {
                std::ofstream archivo("buzon_" + dest + ".txt", std::ios::app);
                archivo << "From: " << c.apodo << "@chat.ci4835\r\n"
                        << "To: " << dest << "@chat.ci4835\r\n"
                        << "Date: Thu, 15 Oct 2026 10:00:00 -0400\r\n"
                        << "Subject: Mensaje privado\r\n"
                        << "Message-ID: <" << time(nullptr) << "@chat.ci4835>\r\n\r\n"
                        << texto << "\r\n";
                resp = "251 " + dest + " está desconectado; mensaje guardado en su buzón\r\n";
            }
        } else {
            resp = "501 Sintaxis invalida\r\n";
        }
    }
    else if (comando.find("PART ") == 0) {
        std::string canal = comando.substr(5);
        canal.erase(canal.find_last_not_of(" \n\r\t") + 1);

        if (canales.count(canal) == 0) {
            resp = "551 Canal inexistente\r\n";
        } else {
            auto it = std::find(canales[canal].begin(), canales[canal].end(), fd);
            if (it != canales[canal].end()) {
                canales[canal].erase(it);
                resp = "211 Has salido del canal\r\n";
                if (canales[canal].empty()) canales.erase(canal);
            } else {
                resp = "442 No eres miembro del canal\r\n";
            }
        }
    }
    else if (comando == "STAT") {
        int bytes = 0, count = 0;
        for (const auto& msg : c.buzon_cargado) {
            if (!msg.borrado) { count++; bytes += msg.contenido.length(); }
        }
        resp = "220 " + std::to_string(count) + " " + std::to_string(bytes) + "\r\n";
    }
    else if (comando.find("RETR ") == 0) {
        int n = std::stoi(comando.substr(5)) - 1;
        if (n >= 0 && n < (int)c.buzon_cargado.size() && !c.buzon_cargado[n].borrado) {
            std::stringstream ss(c.buzon_cargado[n].contenido);
            std::string linea;
            while (std::getline(ss, linea)) {
                if (!linea.empty() && linea.back() == '\r') linea.pop_back();
                resp += "222-" + linea + "\r\n";
            }
            resp += "222 fin del mensaje\r\n";
        } else {
            resp = "552 Mensaje no existe\r\n";
        }
    }
    else if (comando.find("DELE ") == 0) {
        int n = std::stoi(comando.substr(5)) - 1;
        if (n >= 0 && n < (int)c.buzon_cargado.size()) {
            c.buzon_cargado[n].borrado = true;
            resp = "223 Mensaje " + std::to_string(n+1) + " marcado para borrar\r\n";
        } else {
            resp = "552 Mensaje no existe\r\n";
        }
    }
    else if (comando.find("QUIT") == 0) {
        guardar_buzon(c);
        resp = "299 Adiós\r\n";
        send(fd, resp.c_str(), resp.length(), 0);
        close(fd);
        clientes.erase(fd);
        return;
    }

    else if (comando == "CHANS") {
        int total_canales = 0;
        for (const auto& par : canales) {
            // par.first es el nombre del canal, par.second es el vector de miembros
            resp += "212-" + par.first + " " + std::to_string(par.second.size()) + "\r\n";
            total_canales++;
        }
        resp += "212 " + std::to_string(total_canales) + " canales listados\r\n";
    }
    else {
        resp = "500 Comando desconocido\r\n";
    }

    send(fd, resp.c_str(), resp.length(), 0);
}

void procesar_http(int fd, const std::string& peticion) {
    std::string cuerpo;
    std::string codigo_estado = "200 OK";
    std::string cabeceras_extra = "";

    // 1. Extraer la primera línea de la petición (Request Line)
    size_t fin_linea = peticion.find("\r\n");
    if (fin_linea == std::string::npos) {
        codigo_estado = "400 Bad Request";
        cuerpo = "400 Petición mal formada\n";
    } else {
        std::string request_line = peticion.substr(0, fin_linea);
        
        // 2. Extraer las tres partes: Método, Ruta y Versión
        std::istringstream iss(request_line);
        std::string metodo, ruta, version;
        iss >> metodo >> ruta >> version;

        // Validaciones estrictas según los requerimientos
        if (metodo.empty() || ruta.empty() || version.empty()) {
            codigo_estado = "400 Bad Request";
            cuerpo = "400 Petición mal formada\n";
        } 
        else if (metodo != "GET") {
            codigo_estado = "405 Method Not Allowed";
            cabeceras_extra = "Allow: GET\r\n"; // Cabecera obligatoria para 405
            cuerpo = "405 Método no permitido\n";
        } 
        else {
            // Es un GET válido, procesamos las rutas exactas
            if (ruta == "/usuarios") {
                for (const auto& par : clientes) {
                    if (!par.second.es_http && !par.second.apodo.empty()) {
                        cuerpo += par.second.apodo + "\n";
                    }
                }
            } 
            else if (ruta == "/canales") {
                for (const auto& par : canales) {
                    cuerpo += par.first + " " + std::to_string(par.second.size()) + "\n";
                }
            } 
            else if (ruta.find("/historial/") == 0) {
                // Extraemos el nombre del canal de la ruta y le agregamos '#'
                std::string canal_buscado = "#" + ruta.substr(11);
                
                if (historial_canales.count(canal_buscado)) {
                    for (const auto& linea : historial_canales[canal_buscado]) {
                        cuerpo += linea + "\n";
                    }
                } else {
                    codigo_estado = "404 Not Found";
                    cuerpo = "404 Canal inexistente\n";
                }
            } 
            else {
                codigo_estado = "404 Not Found";
                cuerpo = "404 Ruta inexistente\n";
            }
        }
    }

    // 3. Construir la respuesta garantizando las cabeceras obligatorias
    std::string respuesta = "HTTP/1.1 " + codigo_estado + "\r\n"
                            "Content-Type: text/plain; charset=utf-8\r\n"
                            "Content-Length: " + std::to_string(cuerpo.length()) + "\r\n"
                            "Connection: close\r\n"
                            + cabeceras_extra + 
                            "\r\n" + cuerpo;
                            
    send(fd, respuesta.c_str(), respuesta.length(), 0);
    close(fd);
    clientes.erase(fd);
}
int main() {
    int chat_fd = crear_socket(5000);
    int http_fd = crear_socket(8080);
    fds.push_back({chat_fd, POLLIN, 0});
    fds.push_back({http_fd, POLLIN, 0});

    std::cout << "Servidor iniciado...\n";

    while (true) {
        poll(fds.data(), fds.size(), -1);
        for (size_t i = 0; i < fds.size(); ++i) {
            if (fds[i].revents & POLLIN) {
                int fd_actual = fds[i].fd;
                if (fd_actual == chat_fd || fd_actual == http_fd) {
                    int nuevo_fd = accept(fd_actual, nullptr, nullptr);
                    fds.push_back({nuevo_fd, POLLIN, 0});
                    clientes[nuevo_fd] = {nuevo_fd, "", "", (fd_actual == http_fd), {}};
                    if (fd_actual == chat_fd) {
                        std::string bienv = "200 server listo\r\n";
                        send(nuevo_fd, bienv.c_str(), bienv.length(), 0);
                    }
                } else {
                    char buf[512];
                    int bytes = recv(fd_actual, buf, sizeof(buf), 0);
                    if (bytes <= 0) {
                        close(fd_actual);
                        clientes.erase(fd_actual);
                        fds.erase(fds.begin() + i);
                        i--; 
                        continue;
                    }

                    clientes[fd_actual].buffer.append(buf, bytes);

                    if (clientes[fd_actual].es_http) {
                        if (clientes[fd_actual].buffer.find("\r\n\r\n") != std::string::npos) {
                            procesar_http(fd_actual, clientes[fd_actual].buffer);
                            fds.erase(fds.begin() + i);
                            i--;
                        }
                    } else {
                        size_t pos;
                        while ((pos = clientes[fd_actual].buffer.find("\r\n")) != std::string::npos) {
                            std::string comando = clientes[fd_actual].buffer.substr(0, pos);
                            clientes[fd_actual].buffer.erase(0, pos + 2);
                            procesar_comando_chat(fd_actual, comando);
                        }
                    }
                }
            }
        }
    }
    return 0;
}