#include "client.h"

Client::Client(const in_addr_t& ip, const in_port_t listed_port, const std::string& un) : username{un}
{
    udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0) throw ClientException("opening udp socket");

    udp_sai = {AF_INET, listed_port, ip};
    if (bind(udp_fd, (sockaddr*)&udp_sai, sizeof(sockaddr)) < 0) {
        udp_sai.sin_port = 0;
        if (bind(udp_fd, (sockaddr*)&udp_sai, sizeof(sockaddr)) < 0) {
            throw ClientException("binding up socket");
        }
    }

    tcp_sai = {AF_INET, 0, ip};
    tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_fd < 0) throw ClientException("opening tcp socket");
    if (bind(tcp_fd, (sockaddr*)&tcp_sai, sizeof(sockaddr)) < 0) throw ClientException("binding tcp socket");
}

void Client::connect_wait()
{
    char buf[256];
	util::invite_fmt* ifmt = (util::invite_fmt*) buf;

    socklen_t addrlen = sizeof(sockaddr);
    auto size = recvfrom(udp_fd, buf, 256, 0, (sockaddr*)&udp_sai, &addrlen);

    server_sai = {AF_INET, ifmt->sin_port, ifmt->s_addr};
	chat_init();
}

void Client::connect_manual(std::string invite)
{

	auto delimpos = invite.find(":", 0);
	std::string addr = invite.substr(0, delimpos);
	int port = std::stoi(invite.substr(delimpos+1, std::string::npos));

	server_sai = {
		AF_INET,
		htons(port),
		inet_addr(addr.c_str())
	};
	chat_init();
}

void Client::chat_init()
{
	if (connect(tcp_fd, (sockaddr*)&server_sai, sizeof(sockaddr)) < 0)
		throw ClientException("connecting to server");
    std::cout << "Connected to server" << std::endl;
    send(tcp_fd, username.c_str(), username.length(), 0);

    std::thread t_chat_rx(&Client::chat_rx, this);
    std::thread t_chat_tx(&Client::chat_tx, this);
    t_chat_rx.join();
    t_chat_tx.join();
}

void Client::chat_rx()
{
    for (;;) {
        char buf[256];
        auto size = recv(tcp_fd, buf, 256, 0);
        if (size < 0)
			if (errno == EBADF) {
				std::cerr << "Connection closed" << std::endl;
				break;
			}
			else
				std::cerr << "RX Packet error " << errno << std::endl;
        else {
            std::string message(buf, size);
            std::cout << message << std::endl;
        }
    }
}

void Client::chat_tx()
{
    for (;;) {
        std::string message;
        getline(std::cin, message);
		if (send(tcp_fd, message.c_str(), message.length(), 0) < 0)
			std::cerr << "Error, message not sent";
		if (message == ":quit" || message == ":q")
            break;
    }
    close(tcp_fd);
}

ClientException::ClientException() : std::runtime_error("Client error") {}
ClientException::ClientException(std::string details) : std::runtime_error("Client error: "+details) {}
