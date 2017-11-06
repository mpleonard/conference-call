#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <exception>
#include <chrono>
#include <thread>
#include <random>
#include <future>

#include <cstring>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

using namespace std;

mutex osmtx;

struct parse_db_ex : exception {
    const char* what() const noexcept {return "DB parsing error";}
};

map<string, sockaddr_in> parse_db(const char* filename, const bool verbose = false)
{
    map<string, sockaddr_in> directory;
    ifstream dbfile(filename);
    if (dbfile.is_open()) {
        string entry;
        auto nline = 0U;
        while (getline(dbfile, entry)) {
            ++nline;
            string field[3];
            const string delim {","};
            auto start = 0U;
            auto end = 0;
            try {
                for (auto& f : field) {
                    if (end == string::npos)
                        throw parse_db_ex();
                    end = entry.find(delim, start);
                    f = entry.substr(start, end-start);
                    start = end + delim.length();
                }
                if (verbose) {
                    cout << nline << " ";
                    for (auto& f : field) cout << f << " ";
                    cout << endl;
                }
                try {
                    sockaddr_in sai  {
                        AF_INET,
                        htons(stoi(field[2])),
                        inet_addr(field[1].c_str())
                    };
                    directory.emplace(field[0], sai);
                } catch (const invalid_argument& ia) {
                    cerr << ia.what() << endl;
                }
            } catch (parse_db_ex e) {
                cerr << e.what() << ", line " << nline << endl;
            }
        }
    }
    dbfile.close();
    return directory;
}

bool verify_user(const string& username, const map<string, sockaddr_in>& dir, int lag)
{
    // Anti latency-based side-channel info leak
    this_thread::sleep_for(chrono::milliseconds(lag));
    // TODO: Password hash verification
    return dir.find(username) != dir.end();
}

int query(const char* prompt, pair<int, int> range, int offset = 1)
{
    int choice;
    for (;;) {
        cout << prompt;
        cin >> choice;
        if (cin.fail()) {
            cin.clear();
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            continue;
        }
        choice-=offset;
        if (choice < range.first || range.second < choice)
            continue;
        break;
    }
}

class Server {
    public:
    struct failure : exception {
        string details;
        failure(string details) : details("Server failure: "+details) {}
        const char* what() const noexcept {return details.c_str();}
    };
    struct invite_fmt {
        uint32_t s_addr;
        uint16_t sin_port;
    };
    // make invite: address, encrypted with k-1
    // setup socket
    Server(const in_addr_t& ip, const string& un) : username{un}
    {
        tcp_sai = {AF_INET, 0, ip};
        tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (tcp_fd < 0) throw failure("opening tcp socket");
        if (bind(tcp_fd, (sockaddr*)&tcp_sai, sizeof(sockaddr)) < 0) throw failure("binding tcp socket");
        if (listen(tcp_fd, 32) < 0) throw failure("listening to tcp socket");

        // Get assigned port
        socklen_t tcp_slen = sizeof(sockaddr);
        if (getsockname(tcp_fd, (sockaddr*)&tcp_sai, &tcp_slen) < 0) throw failure("get tcp socket name");
        cout << "Address: " << inet_ntoa(tcp_sai.sin_addr) << ":" << ntohs(tcp_sai.sin_port) << endl;

        udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (udp_fd < 0) throw failure("opening udp socket");

        udp_sai = {AF_INET, 0, ip};
        if (bind(udp_fd, (sockaddr*)&udp_sai, sizeof(sockaddr)) < 0) throw failure("binding udp socket");
    }
    void client_handler() {
        map<int, string> userbyfd;
        sockaddr_in client_sai;
        fd_set active_fds, read_fds;

        socklen_t slen = sizeof(sockaddr_in);
        char buf[256];

        FD_ZERO(&active_fds);
        FD_SET(tcp_fd, &active_fds);
        for (;;) {
            read_fds = active_fds;
            if (select(FD_SETSIZE, &read_fds, nullptr, nullptr, nullptr) < 0) throw failure("socket select");
            for (int fd = 0; fd < FD_SETSIZE; ++fd) {
                if (FD_ISSET(fd, &read_fds)) {
                    if (fd == tcp_fd) {
                        auto new_fd = accept(tcp_fd, (sockaddr*)&client_sai, &slen);
                        cout << "Connection from " << inet_ntoa(client_sai.sin_addr) << ":" << ntohs(client_sai.sin_port) << endl;
                        auto size = recv(new_fd, buf, 256, 0);
                        userbyfd.emplace(new_fd, string(buf,size));
                        FD_SET(new_fd, &active_fds);
                    }
                    else {
                        auto size = recv(fd, buf, 256, 0);
                        string msg(buf, size);
                        if (size < 0)
                            cerr << "Recv fail" << endl;
                        else {
                            if (msg == ":quit") {
                                cout << "Disconnecting " << userbyfd.at(fd) << endl;
                                close(fd);
                                FD_CLR(fd, &active_fds);
                                userbyfd.erase(fd);
                            } else if (msg == ":who") {
                                string list_msg("Participating: ");
                                for (auto& u : userbyfd)
                                    list_msg += u.second + " ";
                                broadcast(read_fds, list_msg, true);
                            } else {
                                string titled_msg(userbyfd.at(fd) +":"+ msg);
                                broadcast(read_fds, titled_msg, true);
                            }
                        }
                    }
                }
            }
        }
    }
    void broadcast(const fd_set& fds, const string& msg, bool log = false)
    {
        if (log) cout << msg << endl;
        for (int other_fd = 0; other_fd < FD_SETSIZE; ++other_fd)
            if (FD_ISSET(other_fd, &fds))
                if (send(other_fd, msg.c_str(), msg.length(), 0) < 0)
                    cerr << "Send fail" << endl;
    }

    void invite(map<string, sockaddr_in>& dir)
    {
        char invite[6];
        invite_fmt* ifmt = (invite_fmt*) invite;
        ifmt->s_addr = tcp_sai.sin_addr.s_addr;
        ifmt->sin_port = tcp_sai.sin_port;

        cout << "Invited: ";
        for (auto& callee : dir) {
            if (sendto(udp_fd, invite, 6, 0,
                    (sockaddr*)&callee.second, sizeof(sockaddr)) < 0) throw failure("send invite failed");
            cout << callee.first << " ";
        }
        cout << endl;
    }
    private:
    int tcp_fd, udp_fd;
    sockaddr_in tcp_sai, udp_sai;
    string username;
};

class Client {
    public:
    struct failure : exception {
        string details;
        failure(string details) : details("Client failure: "+details) {}
        const char* what() const noexcept {return details.c_str();}
    };
    Client(const in_addr_t& ip, const in_port_t listed_port, const string& un) : username{un}
    {
        udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (udp_fd < 0) throw failure("opening udp socket");

        udp_sai = {AF_INET, listed_port, ip};
        if (bind(udp_fd, (sockaddr*)&udp_sai, sizeof(sockaddr)) < 0) {
            udp_sai.sin_port = 0;
            if (bind(udp_fd, (sockaddr*)&udp_sai, sizeof(sockaddr)) < 0) {
                throw failure("binding up socket");
            }
        }

        tcp_sai = {AF_INET, 0, ip};
        tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (tcp_fd < 0) throw failure("opening tcp socket");
        if (bind(tcp_fd, (sockaddr*)&tcp_sai, sizeof(sockaddr)) < 0) throw failure("binding tcp socket");
    }
    void connect_wait()
    {
        char buf[256];
        Server::invite_fmt* ifmt = (Server::invite_fmt*) buf;

        socklen_t addrlen = sizeof(sockaddr);
        auto size = recvfrom(udp_fd, buf, 256, 0, (sockaddr*)&udp_sai, &addrlen);

        server_sai = {AF_INET, ifmt->sin_port, ifmt->s_addr};
        if (connect(tcp_fd, (sockaddr*)&server_sai, sizeof(sockaddr)) < 0) throw failure("connecting to server");
        cout << "Connected to server" << endl;
        send(tcp_fd, username.c_str(), username.length(), 0);
        /*
        thread t_chat_rx(chat_rx, tcp_fd);
        thread t_chat_tx(chat_tx, tcp_fd);
        t_chat_rx.join();
        t_chat_tx.join();
        */
    }

    /*
     * Broken, blocking on tcp_fd. Use select() like in Server
    static void chat_rx(int tcp_fd) {
        for (;;) {
            char buf[256];
            auto size = recv(tcp_fd, buf, 256, 0);
            cout << "RECV" << endl;
            osmtx.lock();
            if (size < 0)
                cerr << "RX Packet error" << endl;
            else {
                string message(buf, size);
                cout << message;
            }
            osmtx.unlock();
        }
    }
    static void chat_tx(int tcp_fd) {
        for (;;) {
            string message;
            osmtx.lock();
            getline(cin, message);
            if (message.front() != ':')
                osmtx.unlock();
                if (send(tcp_fd, message.c_str(), message.length(), 0) < 0)
                    cerr << "Error, message not sent";
            else if (message == ":quit" || message == ":q")
                break;
        }
        close(tcp_fd);
    }
     */
    private:
    int tcp_fd, udp_fd;
    sockaddr_in tcp_sai, udp_sai, server_sai;
    string username;
};

int main(int argc, char* argv[])
{
    /*
     * Load directory
     * Authenticate user: username pass - no crypto
     * Server or Client session
     * Create session
     */
    default_random_engine rng;
    uniform_int_distribution<int> udist(300,500);

    auto db = parse_db("db", true);
    string username;
    for (;;) {
        cout << "Username: ";
        cin >> username;
        if (!verify_user(username, db, udist(rng)))
            cout << "Invalid login" << endl;
        else
            break;
    }
    cout << "Logged in as " << username << endl;
    auto dbentry = db.at(username);

    for (;;) {
        enum {opt_server, opt_client};
        int choice_sc = query("1\tCreate conference\n2\tConnect to conference\n", {opt_server,opt_client});
        if (choice_sc == opt_server) {
            try {
                Server s(dbentry.sin_addr.s_addr, username);
                s.invite(db);
                s.client_handler();
            } catch (Server::failure f) {
                cerr << f.what() << endl;
                return -1;
            }
        } else if (choice_sc == opt_client) {
            try {
                Client c(dbentry.sin_addr.s_addr, dbentry.sin_port, username);
                enum {opt_wait, opt_manual};
                int choice_wm = query("1\tWait for invite\n2\tConnect manually\n", {opt_wait,opt_manual});
                if (choice_wm == opt_wait) {
                    c.connect_wait();
                } else {
                    //c.connect_manual();
                }

            } catch (Client::failure f) {
                cerr << f.what() << endl;
                return -1;
            }
        }
    }
}
