#ifndef CLIENT_H
#define CLIENT_H

#include <iostream>
#include <string>
#include <map>
#include <stdexcept>
#include <thread>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "util.h"

class Client {
    int tcp_fd, udp_fd;
    sockaddr_in tcp_sai, udp_sai, server_sai;
    std::string username;
	void chat_rx();
	void chat_tx();
	void chat_init();
    
    public:
        Client(const in_addr_t& ip, const in_port_t listed_port, const std::string& un);
        void connect_wait();
        void connect_manual(std::string invite);
};

struct ClientException : std::runtime_error {
    ClientException();
    ClientException(std::string details);
};

#endif
