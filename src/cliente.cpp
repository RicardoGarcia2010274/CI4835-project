#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

int main(int argc, char* argv[]) {

    //si la persona se equivoca escribiendo el comando se le indica como debe escribirlo correctamente 
    if (argc != 3) {
        std::cerr << "Uso: ./cliente <ip> <puerto>\n";
        return 1;
    }

    //creamos el socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(std::stoi(argv[2]));
    inet_pton(AF_INET, argv[1], &server_addr.sin_addr);

    //si la conexion con el servidor sale mal, me "Error de conexion" 
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Error de conexión\n";
        return 1;
    }

    //creamos un arreglo de descriptores que el cliente va a monitorear usando poll    
    struct pollfd fds[2];
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[1].fd = sock;
    fds[1].events = POLLIN;
    
    //Definimos el tamano del buffer 
    std::string buffer_red = "";
    char buf[512];

    //bucle principal
    while (true) {

        //aqui pongo a esperar el proceso del cliente, sin consumir recursos
        //el deja de esperar si el cliente escribe algo o el servidor le envia algo 
        int ret = poll(fds, 2, -1);
        if (ret < 0) break;

        //lee la linea que le manda el servidor 
        if (fds[0].revents & POLLIN) {
            std::string input;
            std::getline(std::cin, input);
            std::cout << "C: " << input << "\n";
            input += "\r\n";
            send(sock, input.c_str(), input.length(), 0);
            
        }
        //aqui leo los bytes que llegan al socket 
        if (fds[1].revents & POLLIN) {
            int bytes = recv(sock, buf, sizeof(buf), 0);
            if (bytes <= 0) break;
            
            buffer_red.append(buf, bytes);
            size_t pos;
            while ((pos = buffer_red.find("\r\n")) != std::string::npos) {
                std::cout << "S: " << buffer_red.substr(0, pos) << "\n";
                buffer_red.erase(0, pos + 2);
            }
        }
    }

    //cierro el socket y me desconecto al salir del bucle principal 
    close(sock);
    return 0;
}