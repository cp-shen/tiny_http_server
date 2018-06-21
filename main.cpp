#include "http_server_2.hpp"
#include <csignal>
#include <iostream>
#include <unistd.h>

HTTPServer *server_ptr_global = nullptr;
void shutdown_by_singnal(int signal){
    if(signal == SIGINT && server_ptr_global != nullptr){
        std::cout << std::endl << "shut down the server by SIGINT..." << std::endl; 
        server_ptr_global->server_exit();
    } 
}

int main(int argc, const char* argv[]){
    int port = 0;
    if(argc == 1){
        port = 80;
    }
    else if(argc == 2){
        port = std::stoi(argv[1]); 
    }
    else{
        std::cout << "usage: ./my_http <port> or ./my_http" << std::endl;
        return -1;
    }
    try{
        HTTPServer server = HTTPServer(port);

        // setup signal shutdown method
        server_ptr_global = &server;
        signal(SIGINT, shutdown_by_singnal);

        server.run();
    }
    catch(ServerException const &exc){
        std::cout << exc.what() << std::endl;
    }
    std::cout << "process finished" << std::endl;
    return 0;
}
