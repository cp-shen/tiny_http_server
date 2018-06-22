#include "http_server_2.hpp"
#include <cstring>
#include <fstream>
#include <sstream>
#include <ctime>
#include <iostream>
#include <cerrno>

#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netdb.h>

void HTTPServer::make_socket_non_blocking(int sfd) {  
    int flags;
    flags = fcntl(sfd, F_GETFL, 0);
    if (flags == -1) {
        throw ServerException("failed to get socket fd flags");
    }

    flags |= O_NONBLOCK;
    if (fcntl(sfd, F_SETFL, flags) == -1) {  
        throw ServerException("failed to set socket non-blocking");
    }
}  

void HTTPServer::accept_and_add_new(int epoll_fd, int server_socket)  
{
    struct epoll_event event;
    struct sockaddr in_addr;
    int infd = -1;
    socklen_t in_len = sizeof(in_addr);
    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
  
    while ((infd = accept(server_socket, &in_addr, &in_len)) != -1) {
        if (getnameinfo(&in_addr, in_len,  
                hbuf, sizeof(hbuf),  
                sbuf, sizeof(sbuf),  
                NI_NUMERICHOST | NI_NUMERICHOST) == 0) {  
            printf("Accepted connection on descriptor %d (host=%s, port=%s)\n",  
                    infd, hbuf, sbuf);  
        }  
        /* Make the incoming socket non-block 
         * and add it to list of fds to 
         * monitor*/  
        make_socket_non_blocking(infd);
  
        event.data.fd = infd;
        event.events = EPOLLIN | EPOLLET;  
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, infd, &event) == -1) {  
            throw ServerException("failed to add the new connection socket to epoll fd");
        }  
        in_len = sizeof(in_addr);  
    }  

    // if the socket is marked nonblocking and no pending connections are present on the queue
    // errno will be set tp EAGIN or EWOULDBLOCK
    // else an error has ocuured
    if (errno != EAGAIN && errno != EWOULDBLOCK)
        throw ServerException("after server_socket accept loop");
}

HTTPServer::HTTPServer(int port) : server_socket(-1), epoll_fd(-1), flag_exit(false){
    // allocate the server socket
    if( (server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        throw ServerException("failed to allocate the server socket");
    }

    // Enable address reuse
    int on = 1;
    if( setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1){
        throw ServerException("failed set server socket to reuse address");
    }

    // initialize sockaddr
    struct sockaddr_in server_addr;

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    // bind the server socket
    if( bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1){
        throw ServerException("failed to bind the server socket");
    }

    // listen to connections
    if( listen(server_socket, 10) < 0){
        throw ServerException("failed to set the server socket to listening state");
    }

    // set the socket to non blocking
    make_socket_non_blocking(server_socket);

    // allocate the epoll fd
    epoll_fd = epoll_create1(0);  
    if (epoll_fd == -1) {  
        throw ServerException("failed to allocate epoll fd");
    }  
  
    // set the server socket monitored by epoll fd
    struct epoll_event event;
    event.data.fd = server_socket;
    event.events = EPOLLIN | EPOLLET;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_socket, &event) == -1) {
        throw ServerException("failed to add server socket to epoll fd");
    }
}

void HTTPServer::send_file(int soc_fd, std::ifstream &file){
    const std::streamsize BUFF_SIZE = 1024 * 1024;
    std::streamsize nb_read = 0;
    std::streamsize nb_send = 0;
    std::streamsize send_total = 0;
    char buff[BUFF_SIZE];
    while (true) {
        if(file.eof()){
            break;
        }
        if(!file.good()) {
            // reset the ifstream
            file.clear(std::ios::goodbit);
            file.seekg(send_total, std::ios::beg);
        }
        file.read(buff, BUFF_SIZE);
        nb_read = file.gcount();
        nb_send = send(soc_fd, buff, nb_read, 0);
        if (nb_send == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
            throw ServerException("failed to send requested file, errno = " + std::to_string(errno));
        }
        if(nb_send == -1){
            nb_send = 0;
        }
        if (nb_send != nb_read) {
            file.seekg(nb_send - nb_read, std::ios::cur);
        }
        send_total += nb_send;
    }
    std::cout << "finished sending the file" << std::endl;
}

void HTTPServer::recv_request(int soc_fd, std::string &recv_buffer){
    size_t buffer_capacity = 1024;
    char buffer[buffer_capacity];
    ssize_t recv_length = 0;

    // non blocking socket receiving
    recv_length = recv(soc_fd, buffer, buffer_capacity, 0);
    if(recv_length == -1 && errno != EAGAIN && errno != EWOULDBLOCK){
        // an error occured when receiving
        throw ServerException("failed to receive the request");
    }
    if(recv_length == 0){
        // the peer socket is closed on the client side
        throw ServerException("close connection: the client has closed the socket");
    }
    recv_buffer.append(buffer, recv_length);
}

void HTTPServer::process_new_soc(int soc_fd){
    std::string recv_buffer ("");

    size_t sp1_pos = recv_buffer.find(" ");
    size_t sp2_pos = recv_buffer.find("HTTP");
    while(sp1_pos == std::string::npos || sp2_pos == std::string::npos){
        if(recv_buffer.size() > 1024 * 1024){
            // received request is larger than 1MB
            // and can not find uri and method
            throw ServerException("recv buffer overflow");
        }
        recv_request(soc_fd, recv_buffer);
        sp1_pos = recv_buffer.find(" ");
        sp2_pos = recv_buffer.find("HTTP");
    }

    std::string method (recv_buffer, 0, sp1_pos);
    std::string uri (recv_buffer, sp1_pos + 1, sp2_pos - sp1_pos - 2); 
    if(uri == "/"){
        uri = "./resources/index.html";
    }
    else{
        uri = "./resources" + uri;
    }

    std::cout << "method: " << method << std::endl;
    std::cout << "uri: " << uri << std::endl;

    if(method == "GET"){
        respond_get(uri, soc_fd);
    }
    else if(method == "HEAD"){
        respond_head(uri, soc_fd);
    }
    else if(method == "DELETE"){
        respond_delete(uri, soc_fd);
    }
    close(soc_fd);
}

void HTTPServer::respond_head(const std::string &uri, int soc_fd) {
    std::stringstream response ("");

    // build the status line
    response << "HTTP/1.1 "; 
    std::ifstream file (uri, std::ifstream::binary | std::ifstream::ate);
    if(file.is_open()){
        response << "200 OK\r\n";
    }
    else{
        response << "404 Not Found\r\n"; 
    }

    // build the Content-Length field
    response << "Content-Length: ";
    if(file.is_open()){
        response << file.tellg() << "\r\n";
    }
    else{
        response << std::string ("404 Not Found").length() << "\r\n";
    }

    // build the Content-Type field
    std::string file_type;
    if(file.is_open()){
        std::string suffix (uri, uri.rfind('.') + 1);
        if(suffix == "asc" || suffix == "txt" || suffix == "text"
           || suffix == "pot" || suffix == "brf" || suffix == "srt"){
            file_type = "text/plain";
        }
        else if(suffix == "jpeg" || suffix == "jpg"){
            file_type = "image/jpeg";
        }
        else if(suffix == "html" || suffix == "htm" || suffix == "shtml"){
            file_type = "text/html";
        }
        else{
            // binary file
            file_type = "application/octet-stream";
        }
    }
    else{
        // file not found
        file_type = "text/plain";
    }
    response << "Content-Type: " << file_type << "\r\n";
    file.close();

    // build the Date field
    char buf[100];
    time_t now = time(0);
    struct tm tm = *gmtime(&now);
    size_t time_length = strftime(buf, sizeof buf, "%a, %d %b %Y %H:%M:%S %Z", &tm); 
    std::string date (buf, time_length);
    response << "Date: " << date << "\r\n";
    
    // add Connection: close
    response << "Connection: close\r\n";

    // add an empty line
    response << "\r\n";

    // send the response header
    if( send(soc_fd, response.str().data(), response.str().size(), 0) != (ssize_t)response.str().size() ){
        throw ServerException("failed to send response head");
    }
}

void HTTPServer::respond_get(const std::string &uri, int soc_fd){
    respond_head(uri, soc_fd);

    // send the file content
    std::ifstream file (uri, std::ifstream::binary);
    if(file.is_open()){
        send_file(soc_fd, file);
        file.close();
    }
    else{
        std::string response ("404 Not Found");
        if( send(soc_fd, response.data(), response.size(), 0) != (ssize_t)response.size() ){
            throw ServerException("failed to send not found msg");
        }
    }
}

void HTTPServer::respond_delete(const std::string &uri, int soc_fd){
    std::ifstream file (uri, std::ifstream::binary | std::ifstream::ate);
    bool file_exists = file.is_open();
    bool file_deleted = false;
    if(file_exists && remove(uri.data()) == 0){
        file_deleted = true;
    }
    file.close();

    std::stringstream response ("");

    // build the status line
    response << "HTTP/1.1 "; 
    if(file_exists){
        if(file_deleted){
            response << "200 OK";
        }
        else{
            response << "202 Accepted";
        }
    }
    else{
        response << "404 Not Found";
    }
    response << "\r\n";

    // build the Content-Length field
    response << "Content-Length: ";
    if(file_exists){
        if(file_deleted){
            response << std::string("200 OK").length();
        }
        else{
            response << std::string("202 Accepted").length();
        }
    }
    else{
        response << std::string("404 Not Found").length();
    }
    response << "\r\n";

    // build the Content-Type field
    std::string file_type;
    file_type = "text/plain";
    response << "Content-Type: " << file_type << "\r\n";

    // build the Date field
    char buf[100];
    time_t now = time(0);
    struct tm tm = *gmtime(&now);
    size_t time_length = strftime(buf, sizeof buf, "%a, %d %b %Y %H:%M:%S %Z", &tm); 
    std::string date (buf, time_length);
    response << "Date: " << date << "\r\n";
    
    // add Connection: close
    response << "Connection: close\r\n";

    // add an empty line
    response << "\r\n";

    // add the response message entity
    if(file_exists){
        if(file_deleted){
            response << "200 OK";
        }
        else{
            response << "202 Accepted";
        }
    }
    else{
        response << "404 Not Found";
    }

    // send the full response
    if( send(soc_fd, response.str().data(), response.str().size(), 0) != (ssize_t)response.str().size() ){
        throw ServerException("failed to send response");
    }
}

void HTTPServer::run() {
    const int MAXEVENTS = 64;
    struct epoll_event events[MAXEVENTS];
  
    while(!flag_exit) {  
        int n = 0, i = 0;  
        n = epoll_wait(epoll_fd, events, MAXEVENTS, -1);  
        for (i = 0; i < n; i++) {  
            if (events[i].events & EPOLLERR || events[i].events & EPOLLHUP ||  
                !(events[i].events & EPOLLIN)) {
                /* An error on this fd or socket not ready */  
                close(events[i].data.fd);  
            }
            else if (events[i].data.fd == server_socket) {
                /* New incoming connection */  
                try{
                    accept_and_add_new(epoll_fd, server_socket); 
                }
                catch(ServerException const &exc){
                    std::cout << exc.what() << std::endl;
                    close(events[i].data.fd);
                }
            }
            else {
                /* Data incoming on fd */  
                try{
                    process_new_soc(events[i].data.fd); 
                }
                catch(ServerException const &exc){
                    std::cout << exc.what() << std::endl;
                    close(events[i].data.fd);
                }
            }  
        }  
    } 
}

HTTPServer::~HTTPServer(){
    close(epoll_fd);
    close(server_socket);  
    std::cout << "server shutdown" << std::endl;
}
