#ifndef HTTP_SERVER_HPP
#define HTTP_SERVER_HPP

#include <string>
#include <exception>


class HTTPServer{
public:
    HTTPServer(int port);
    ~HTTPServer();
    void run();
    void server_exit(){
        flag_exit = true;
    }
private:
    int server_socket;
    int epoll_fd;
    bool flag_exit;

    // utility methods
    static void accept_and_add_new(int epoll_fd, int server_socket);
    static void process_new_soc(int soc_fd);
    static void make_socket_non_blocking(int soc_fd);
    static void recv_request(int soc_fd, std::string &recv_buffer);
    static void respond_get(std::string const &uri, int soc_fd);
    static void respond_head(std::string const &uri, int soc_fd);
    static void respond_delete(std::string const &uri, int soc_fd);
    static void send_file(int soc_fd, std::ifstream &in_file);
};

class ServerException : std::exception{
private:
    std::string msg;
public:
    const char* what() const noexcept{
        return msg.data();
    }
    ServerException(std::string const &msg){
        this->msg = msg;
    }
};

#endif
