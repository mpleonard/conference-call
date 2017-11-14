#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
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

#include "server.h"
#include "client.h"
#include "util.h"

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

int query(std::initializer_list<std::string> options, int offset = 1)
{
    int choice = 0, n = offset;
    std::stringstream prompt_ss;
    for (auto& opt : options)
        prompt_ss << n++ << '\t' << opt << std::endl;
    std::string prompt = prompt_ss.str();
    for (;;) {
        std::cout << prompt;
        std::cin >> choice;
        if (std::cin.fail()) {
            std::cin.clear();
            std::cin.ignore(numeric_limits<streamsize>::max(), '\n');
            continue;
        }
        if (choice < offset || choice >= n)
            continue;
        break;
    }
    return choice-offset;
}

int main(int argc, char* argv[])
{
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
        enum {opt_server, opt_client, opt_quit};
        int choice_sc = query({"Create conference", "Connect to conference", "Quit"});
        if (choice_sc == opt_server) {
            try {
                Server s(dbentry.sin_addr.s_addr, username);
                s.invite(db);
                s.client_handler();
            } catch (ServerException e) {
                cerr << e.what() << endl;
                return -1;
            }
        } else if (choice_sc == opt_client) {
            try {
                Client c(dbentry.sin_addr.s_addr, dbentry.sin_port, username);
                enum {opt_wait, opt_manual, opt_back};
                int choice_wm = query({"Wait for invite", "Connect manually", "Back"});
                if (choice_wm == opt_wait) {
                    c.connect_wait();
                } else if (choice_wm == opt_manual) {
                    std::string invite;
                    std::cout << "Paste invite: ";
                    std::cin >> invite;
                    c.connect_manual(invite);
                }
            } catch (ClientException e) {
                cerr << e.what() << endl;
                return -1;
            }
        } else if (choice_sc == opt_quit) break;
    }
}

