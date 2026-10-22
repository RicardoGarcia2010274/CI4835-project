//Bibliotecas usadas a lo largo del servidor :(

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cctype>
#include <poll.h>
#include <ctime>
#include <algorithm>
#include <sys/stat.h>
#include <sys/types.h>
#include <cerrno>
const size_t MAX_LINEA = 512; // Límite máximo antes de exigirse CRLF

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
    bool descartando; // Estado para descartar bytes de líneas sobredimensionadas
};

std::map<int, Cliente> clientes;
std::map<std::string, std::vector<int>> canales;
std::map<std::string, std::vector<std::string>> historial_canales;
std::vector<struct pollfd> fds;
std::string dir_buzones;

//aqui validaremos las longitudes de los apodos: de 1 a 16 caracteres

bool es_apodo_valido(const std::string& apodo){
    if (apodo.empty() || apodo.length() > 16) {
        return false;
    }; 

    for (char c:apodo) {
        
        bool es_mayuscula = (c >= 'A' && c <= 'Z');
        bool es_minuscula = (c >= 'a' && c <= 'z');
        bool es_digito    = (c >= '0' && c <= '9');
        bool es_permitido = (c == ' ' || c == '-');
            
        if (!es_mayuscula && !es_minuscula && !es_digito && !es_permitido){
            return false;

        }
        
    }; 

    return true;
}


//valida que el nombre de los canales no sea mayor a 32
bool es_canal_valido(const std::string& canal){

    //validacion
    if(canal.empty() || canal[0] != '#' || canal.length() > 32){
        return false; 
    };

    for (char c: canal){
        bool es_minuscula = (c >= 'a' && c <= 'z');
        bool es_digito = (c >= '0' && c <= '9');
        bool es_permit = (c == ' ' && c == '-');

        if (!es_minuscula && !es_digito && !es_permit){
            return false; 
        }
    }

    return true; 
}


//aqui creamos el socket 
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

//cargamos el buzon de los usuarios conectados al servidor 
void cargar_buzon(Cliente& c) {
    c.buzon_cargado.clear();
    std::ifstream archivo(dir_buzones + "/buzon_" + c.apodo + ".txt");
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

//valida que ningun canal tenga letras mayusculas :)
bool tiene_mayusculas(const std::string& str) {
    for (char c : str) {
        if (std::isupper(static_cast<unsigned char>(c))) {
            return true;
        }
    }
    return false;
}

//guarda en el buzon los mensajes privados que le lleguen a un cliente 
void guardar_buzon(const Cliente& c) {
    if (c.apodo.empty()) return;
    std::ofstream archivo(dir_buzones + "/buzon_" + c.apodo + ".txt", std::ios::trunc);
    for (const auto& msg : c.buzon_cargado) {
        if (!msg.borrado) archivo << msg.contenido;
    }
}

// Cierre limpio de clientes, de manera que el servidor no estalle si alguien se 
// desconecta sin usar QUIT
void desconectar_cliente(int fd) {
    if (clientes.count(fd) == 0) return;

    Cliente c = clientes[fd];
    if (!c.es_http && !c.apodo.empty()) {
        guardar_buzon(c);
        std::string aviso = "302 " + c.apodo + " desconecta\r\n";
        
        // Notificar y remover del mapa de canales
        for (auto it_canal = canales.begin(); it_canal != canales.end(); ) {
            auto& miembros = it_canal->second;
            auto it_m = std::find(miembros.begin(), miembros.end(), fd);
            if (it_m != miembros.end()) {
                miembros.erase(it_m);
                for (int m_fd : miembros) {
                    send(m_fd, aviso.c_str(), aviso.length(), 0);
                }
            }
            if (miembros.empty()) {
                it_canal = canales.erase(it_canal);
            } else {
                ++it_canal;
            }
        }
    }

    close(fd);
    clientes.erase(fd);

    for (auto it = fds.begin(); it != fds.end(); ++it) {
        if (it->fd == fd) {
            fds.erase(it);
            break;
        }
    }
}

void procesar_comando_chat(int fd, const std::string& comando) {
    std::string resp;
    Cliente& c = clientes[fd];

    if (comando.find("NICK ") == 0) {
        std::string nuevo_apodo = comando.substr(5);
        nuevo_apodo.erase(nuevo_apodo.find_last_not_of(" \n\r\t") + 1);

        if (nuevo_apodo.empty() || !es_apodo_valido(nuevo_apodo)) {
            resp = "501 Sintaxis invalida\r\n";
        } else {
            c.apodo = nuevo_apodo;
            cargar_buzon(c);
            resp = "201 Bienvenido, " + c.apodo + " !\r\n";

            int no_leidos = 0;
            for (const auto& msg : c.buzon_cargado) {
                if (!msg.borrado) no_leidos++;
            }
            if (no_leidos > 0) {
                resp += "303 " + std::to_string(no_leidos) + "\r\n";
            }
        }
    } 
    else if (comando.find("JOIN ") == 0) {
        std::string canal = comando.substr(5);
        canal.erase(canal.find_last_not_of(" \n\r\t") + 1);

        if (c.apodo.empty()) {
            resp = "530 No registrado\r\n";
        } else if (canal.empty() || canal[0] != '#' || tiene_mayusculas(canal)) {
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
            if (canal.empty() || canal[0] != '#' || tiene_mayusculas(canal)){
                resp = "501 Sintaxis Invalida";
            }

            if (canales.count(canal) > 0) {
                auto it = std::find(canales[canal].begin(), canales[canal].end(), fd);
                if (it != canales[canal].end()) es_miembro = true;
            }

            if (!es_miembro) {
                resp = "442 No eres miembro del canal\r\n";
            } else {
                std::string evento = "300 " + canal + " " + c.apodo + ": " + texto + "\r\n";
                
                time_t ahora = time(nullptr);
                char buffer_hora[10];
                strftime(buffer_hora, sizeof(buffer_hora), "%H:%M:%S", localtime(&ahora));
                std::string registro_historial = std::string(buffer_hora) + " " + c.apodo + ": " + texto;
                
                historial_canales[canal].push_back(registro_historial);
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
                std::ofstream archivo(dir_buzones + "/buzon_" + dest + ".txt", std::ios::app);
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
        try {
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
        } catch (...) {
            resp = "501 Sintaxis invalida\r\n";
        }
    }
    else if (comando.find("DELE ") == 0) {
        try {
            int n = std::stoi(comando.substr(5)) - 1;
            if (n >= 0 && n < (int)c.buzon_cargado.size()) {
                c.buzon_cargado[n].borrado = true;
                resp = "223 Mensaje " + std::to_string(n+1) + " marcado para borrar\r\n";
            } else {
                resp = "552 Mensaje no existe\r\n";
            }
        } catch (...) {
            resp = "501 Sintaxis invalida\r\n";
        }
    }
    else if (comando.find("QUIT") == 0) {
        resp = "299 Adiós\r\n";
        send(fd, resp.c_str(), resp.length(), 0);
        desconectar_cliente(fd);
        return;
    }
    else if (comando == "CHANS") {
        int total_canales = 0;
        for (const auto& par : canales) {
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

    size_t fin_linea = peticion.find("\r\n");
    if (fin_linea == std::string::npos) {
        codigo_estado = "400 Bad Request";
        cuerpo = "400 Petición mal formada\n";
    } else {
        std::string request_line = peticion.substr(0, fin_linea);
        
        std::istringstream iss(request_line);
        std::string metodo, ruta, version;
        iss >> metodo >> ruta >> version;

        if (metodo.empty() || ruta.empty() || version.empty()) {
            codigo_estado = "400 Bad Request";
            cuerpo = "400 Petición mal formada\n";
        } 
        else if (metodo != "GET") {
            codigo_estado = "405 Method Not Allowed";
            cabeceras_extra = "Allow: GET\r\n"; 
            cuerpo = "405 Método no permitido\n";
        } 
        else {
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

    std::string respuesta = "HTTP/1.1 " + codigo_estado + "\r\n"
                            "Content-Type: text/plain; charset=utf-8\r\n"
                            "Content-Length: " + std::to_string(cuerpo.length()) + "\r\n"
                            "Connection: close\r\n"
                            + cabeceras_extra + 
                            "\r\n" + cuerpo;
                            
    send(fd, respuesta.c_str(), respuesta.length(), 0);
    desconectar_cliente(fd);
}

int main(int argc, char* argv[]) {

    int puerto_chat = 0;
    int puerto_http = 0;
    dir_buzones = "";
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--chat" && i + 1 < argc) puerto_chat = std::stoi(argv[++i]);
        else if (arg == "--http" && i + 1 < argc) puerto_http = std::stoi(argv[++i]);
        else if (arg == "--buzones" && i + 1 < argc) dir_buzones = argv[++i];
    }
    
    if (puerto_chat == 0 || puerto_http == 0 || dir_buzones.empty()) {
        std::cerr << "Uso: ./servidor --chat <puerto> --http <puerto> --buzones <carpeta>\n";
        return 1;
    }
    
    //esta funcion crea la carpeta de buzon si es que no existe en la raiz del proyecto

    //NOTE: 0755 es para asignar permisos r, w, x 
    if (mkdir(dir_buzones.c_str(), 0755) == 0){
        std:: cout << "Se creo la carpeta '" +dir_buzones+ "' exitosamente >:) \r\n" ;
    } else if (errno != EEXIST){
        std::cout << "No se pudo crear la carpeta 'buzones'";
        return 1;
    }

    int chat_fd = crear_socket(puerto_chat);
    int http_fd = crear_socket(puerto_http);
    fds.push_back({chat_fd, POLLIN, 0});
    fds.push_back({http_fd, POLLIN, 0});

    std::cout << "Servidor iniciado...\n";
    std::string nombre = "Servidor de CI4835";
    while (true) {
        poll(fds.data(), fds.size(), -1);
        for (size_t i = 0; i < fds.size(); ++i) {
            if (fds[i].revents & POLLIN) {
                int fd_actual = fds[i].fd;
                
                if (fd_actual == chat_fd || fd_actual == http_fd) {
                    int nuevo_fd = accept(fd_actual, nullptr, nullptr);
                    if (nuevo_fd >= 0) {
                        fds.push_back({nuevo_fd, POLLIN, 0});
                        clientes[nuevo_fd] = {nuevo_fd, "", "", (fd_actual == http_fd), {}, false};
                        
                        if (fd_actual == chat_fd) {
                            std::string bienv = "200 "+ nombre +" listo\r\n";
                            send(nuevo_fd, bienv.c_str(), bienv.length(), 0);
                        }
                    }
                } else {
                    char buf[512];
                    int bytes = recv(fd_actual, buf, sizeof(buf), 0);
                    
                    if (bytes <= 0) {
                        desconectar_cliente(fd_actual);
                        i--; 
                        continue;
                    }

                    Cliente& cliente_ref = clientes[fd_actual];

                    // Manejo del estado de descarte para líneas demasiado largas
                    if (cliente_ref.descartando) {
                        cliente_ref.buffer.append(buf, bytes);
                        size_t pos = cliente_ref.buffer.find("\r\n");
                        if (pos != std::string::npos) {
                            cliente_ref.buffer.erase(0, pos + 2);
                            cliente_ref.descartando = false;
                        } else {
                            cliente_ref.buffer.clear();
                        }
                        continue;
                    }

                    cliente_ref.buffer.append(buf, bytes);

                    // Verificación preventiva de tamaño de línea antes de CRLF
                    if (cliente_ref.buffer.length() > MAX_LINEA && cliente_ref.buffer.find("\r\n") == std::string::npos) {
                        std::string err = "502 Linea demasiado larga\r\n";
                        send(fd_actual, err.c_str(), err.length(), 0);
                        cliente_ref.buffer.clear();
                        cliente_ref.descartando = true;
                        continue;
                    }

                    if (cliente_ref.es_http) {
                        if (cliente_ref.buffer.find("\r\n\r\n") != std::string::npos) {
                            procesar_http(fd_actual, cliente_ref.buffer);
                            i--;
                        }
                    } else {
                        size_t pos;
                        while ((pos = clientes[fd_actual].buffer.find("\r\n")) != std::string::npos) {
                            std::string comando = clientes[fd_actual].buffer.substr(0, pos);
                            clientes[fd_actual].buffer.erase(0, pos + 2);
                            procesar_comando_chat(fd_actual, comando);
                            
                            if (clientes.count(fd_actual) == 0) {
                                i--;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    return 0;
}