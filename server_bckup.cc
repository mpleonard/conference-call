#include "server.h"

Server::Server(const in_addr_t& ip, const std::string& un) : username{un}
{
    tcp_sai = {AF_INET, 0, ip};
    tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_fd < 0) throw ServerException("opening tcp socket");
    if (bind(tcp_fd, (sockaddr*)&tcp_sai, sizeof(sockaddr)) < 0) throw ServerException("binding tcp socket");
    if (listen(tcp_fd, 32) < 0) throw ServerException("listening to tcp socket");

    // Get assigned port
    socklen_t tcp_slen = sizeof(sockaddr);
    if (getsockname(tcp_fd, (sockaddr*)&tcp_sai, &tcp_slen) < 0) throw ServerException("get tcp socket name");
    std::cout << "Address: " << inet_ntoa(tcp_sai.sin_addr) << ":" << ntohs(tcp_sai.sin_port) << std::endl;

    udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0) throw ServerException("opening udp socket");

    udp_sai = {AF_INET, 0, ip};
    if (bind(udp_fd, (sockaddr*)&udp_sai, sizeof(sockaddr)) < 0) throw ServerException("binding udp socket");
}

void Server::client_handler() {
    std::map<int, std::string> userbyfd;
    sockaddr_in client_sai;
    fd_set active_fds, read_fds;

    socklen_t slen = sizeof(sockaddr_in);
    char buf[256];

    FD_ZERO(&active_fds);
    FD_SET(tcp_fd, &active_fds);
    for (;;) {
        read_fds = active_fds;
        if (select(FD_SETSIZE, &read_fds, nullptr, nullptr, nullptr) < 0) throw ServerException("socket select");
        for (int fd = 0; fd < FD_SETSIZE; ++fd) {
            if (FD_ISSET(fd, &read_fds)) {
                if (fd == tcp_fd) {
                    auto new_fd = accept(tcp_fd, (sockaddr*)&client_sai, &slen);
                    std::cout << "Connection from " << inet_ntoa(client_sai.sin_addr) << ":" << ntohs(client_sai.sin_port) << std::endl;
                    auto size = recv(new_fd, buf, 256, 0);
                    userbyfd.emplace(new_fd, std::string(buf,size));
                    FD_SET(new_fd, &active_fds);
                }
                else {
                    auto size = recv(fd, buf, 256, 0);
                    std::string msg(buf, size);
                    if (size < 0)
                        std::cerr << "Recv fail" << std::endl;
                    else {
                        if (msg == ":quit") {
                            std::cout << "Disconnecting " << userbyfd.at(fd) << std::endl;
                            close(fd);
                            FD_CLR(fd, &active_fds);
                            userbyfd.erase(fd);
                        } else if (msg == ":who") {
                            std::string list_msg("Participating: ");
                            for (auto& u : userbyfd)
                                list_msg += u.second + " ";
                            broadcast(active_fds, list_msg, true);
                        } else {
                            std::string titled_msg(userbyfd.at(fd) +":"+ msg);
                            broadcast(active_fds, titled_msg, true);
                        }
                    }
                }
            }
        }
    }
}
void Server::broadcast(const fd_set& fds, const std::string& msg, bool log)
{
    if (log) std::cout << msg << " -> " << FD_SETSIZE << std::endl;
    for (int other_fd = 0; other_fd < FD_SETSIZE; ++other_fd)
        if (FD_ISSET(other_fd, &fds))
            if (send(other_fd, msg.c_str(), msg.length(), 0) < 0)
                std::cerr << "Send fail" << std::endl;
}

void Server::invite(std::map<std::string, sockaddr_in>& dir)
{
    char invite[6];
    util::invite_fmt* ifmt = (util::invite_fmt*) invite;
    ifmt->s_addr = tcp_sai.sin_addr.s_addr;
    ifmt->sin_port = tcp_sai.sin_port;

    std::cout << "Invited: ";
    for (auto& callee : dir) {
        if (sendto(udp_fd, invite, 6, 0,
                (sockaddr*)&callee.second, sizeof(sockaddr)) < 0) throw ServerException("send invite failed");
        std::cout << callee.first << " ";
    }
    std::cout << std::endl;
}

ServerException::ServerException() : std::runtime_error("Server error") {}
ServerException::ServerException(std::string details) : std::runtime_error("Server error: "+details) {}
