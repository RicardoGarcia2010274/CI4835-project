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
    std::vector<MensajeBuzon> buzon_cargado; // Para manejar STAT, LIST, RETR, DELE
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

// Carga mensajes de disco separándolos por "From:" para simular POP3
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
        c.apodo = comando.substr(5);
        cargar_buzon(c);
        resp = "201 Bienvenido, " + c.apodo + "!" +"\r\n";
        if (!c.buzon_cargado.empty()) {
            resp += "303 " + std::to_string(c.buzon_cargado.size()) + "\r\n";
        }
    } else if (comando.find("JOIN ") == 0) {
        std::string canal = comando.substr(5);
        
        // Elimina espacios accidentales al final del texto enviado
        canal.erase(canal.find_last_not_of(" \n\r\t") + 1);

        if (c.apodo.empty()) {
            resp = "530 No registrado\r\n";
        } else if (canal.empty() || canal[0] != '#') {
            resp = "501 Sintaxis inválida\r\n";
        } else {
            // Evita registrar al cliente varias veces si hace JOIN de nuevo al mismo canal
            if (std::find(canales[canal].begin(), canales[canal].end(), fd) == canales[canal].end()) {
                canales[canal].push_back(fd);
            }
            resp = "210 Unida a " + canal + "\r\n";
        }
    }else if (comando.find("MSG ") == 0) {
        size_t espacio = comando.find(' ', 4);
        if (espacio != std::string::npos) {
            std::string canal = comando.substr(4, espacio - 4);
            std::string texto = comando.substr(espacio + 1);
            
            // Verificar si el usuario es miembro activo del canal
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
            resp = "501 Sintaxis inválida\r\n";
        }
    }
    

    else if (comando.find("WHO ") == 0) {
        std::string canal = comando.substr(4);
        
        // Verificamos si el canal existe en nuestro mapa
        if (canales.count(canal) > 0) {
            int miembros = 0;
            // Listamos a todos los usuarios conectados a ese canal
            for (int miembro_fd : canales[canal]) {
                resp += "213-" + clientes[miembro_fd].apodo + "\r\n";
                miembros++;
            }
            resp += "213 " + std::to_string(miembros) + " miembros\r\n";
        } else {
            resp = "551 Canal inexistente\r\n";
        }
    }else if (comando.find("PRIV ") == 0) {
        size_t espacio = comando.find(' ', 5);
        std::string dest = comando.substr(5, espacio - 5);
        std::string texto = comando.substr(espacio + 1);
        
        int dest_fd = -1;
        for (const auto& par : clientes) {
            if (par.second.apodo == dest && !par.second.es_http) { dest_fd = par.first; break; }
        }

        if (dest_fd != -1) {
            std::string msg = "301 " + c.apodo + " " + texto + "\r\n";
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
    }
    else if (comando.find("PART ") == 0) {
        std::string canal = comando.substr(5);
        canal.erase(canal.find_last_not_of(" \n\r\t") + 1); // Limpia espacios finales

        if (canales.count(canal) == 0) {
            resp = "551 Canal inexistente\r\n";
        } else {
            // Buscamos si el usuario (su fd) está en la lista del canal
            auto it = std::find(canales[canal].begin(), canales[canal].end(), fd);
            
            if (it != canales[canal].end()) {
                canales[canal].erase(it); // Lo sacamos del canal
                resp = "211 Has salido del canal\r\n";
                
                // Si el canal se queda sin miembros, lo borramos de la memoria
                if (canales[canal].empty()) {
                    canales.erase(canal);
                }
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
    else if (comando == "QUIT") {
        guardar_buzon(c); // Aplica borrados[cite: 4]
        resp = "299 Adiós\r\n";
        send(fd, resp.c_str(), resp.length(), 0);
        close(fd);
        clientes.erase(fd);
        return;
    }
    else {
        resp = "500 Comando desconocido\r\n";
    }

    send(fd, resp.c_str(), resp.length(), 0);
}

void procesar_http(int fd, const std::string& peticion) {
    std::string cuerpo;
    if (peticion.find("GET /usuarios HTTP/1.1") != std::string::npos) {
        for (const auto& par : clientes) {
            if (!par.second.es_http && !par.second.apodo.empty()) cuerpo += par.second.apodo + "\n";
        }
    } else if (peticion.find("GET /historial/") != std::string::npos) {
        size_t inicio = peticion.find("/historial/") + 11;
        size_t fin = peticion.find(" HTTP/1.1");
        std::string canal = peticion.substr(inicio, fin - inicio);
        if (historial_canales.count(canal)) {
            for (const auto& linea : historial_canales[canal]) cuerpo += linea + "\n";
        } else {
            cuerpo = "404 No encontrado\n";
        }
    } else {
        cuerpo = "Ruta no soportada\n";
    }

    std::string respuesta = "HTTP/1.1 200 OK\r\n"
                            "Content-Type: text/plain; charset=utf-8\r\n"
                            "Content-Length: " + std::to_string(cuerpo.length()) + "\r\n"
                            "Connection: close\r\n\r\n" + cuerpo;
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
                        std::string bienv = "200 servidor listo\r\n";
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