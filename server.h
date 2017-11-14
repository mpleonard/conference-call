#ifndef SERVER_H
#define SERVER_H

#include <iostream>
#include <string>
#include <map>
#include <stdexcept>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "util.h"

class Server {
    int tcp_fd, udp_fd;
    sockaddr_in tcp_sai, udp_sai;
    std::string username;

    public:
        Server(const in_addr_t& ip, const std::string& un);
        void client_handler();
        void broadcast(const fd_set& fds, const std::string& msg, bool log = true);
        void invite(std::map<std::string, sockaddr_in>& dir);
};

struct ServerException : std::runtime_error {
    ServerException();
    ServerException(std::string details);
};

#endif
