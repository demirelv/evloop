#include "evloop.h"
#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <string.h>

EvLoop* g_ev_ = nullptr;

void signalHandler(int sig) {
    if (g_ev_ && sig == SIGINT) {
        std::cout << "\nReceived SIGINT, stopping event loop..." << std::endl;
        g_ev_->stop();
    }
}

class TcpServer {
private:
    int server_fd_;
    EvLoop* ev_;
    int stats_timer_id_;
    int connection_count_;
    struct ClientInfo {
        int fd;
        int timer_id;
        uint64_t rbytes;
        ClientInfo(int fd) : fd(fd), timer_id(-1), rbytes(0) { }
    };
    std::unordered_map<int, std::unique_ptr<ClientInfo>> client_map_;

    bool add_client(int fd) {
        if (fd < 0) {
            return false;
        }

        if (client_map_.find(fd) != client_map_.end()) {
            std::cerr << "Warning: Client " << fd << " is already being watched" << std::endl;
            return false;
        }

        client_map_[fd] = std::make_unique<ClientInfo>(fd);
        int timer_id = ev_->add_timer(3000,
                        [this, fd](int timer_id) {
                            auto it = client_map_.find(fd);
                            if (it == client_map_.end()) {
                                std::cerr << "Warning: Client " << fd << " could not found" << std::endl;
                                return;
                            }
                            std::cout << "=== Client Stats (Timer ID: " << timer_id << ") Client "<< fd << "===" << std::endl;
                            std::cout << "Receive Bytes: " << it->second->rbytes << std::endl;
                            std::cout << "=========================================" << std::endl;
                        }, true);
        client_map_[fd]->timer_id = timer_id;
        return true;
    }

    bool remove_client(int fd) {
        auto it = client_map_.find(fd);
        if (it == client_map_.end()) {
            return false;
        }
        ev_->remove_timer(it->second->timer_id);
        client_map_.erase(it);
        return true;
    }

public:
    TcpServer(EvLoop* em) : server_fd_(-1), ev_(em),
        stats_timer_id_(-1), connection_count_(0) {}
    ~TcpServer() {
        stop();
    }

    bool start(int port) {
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ < 0) {
            perror("socket");
            return false;
        }

        int opt = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(server_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("bind");
            return false;
        }

        if (listen(server_fd_, 10) < 0) {
            perror("listen");
            return false;
        }

        bool success = ev_->add_fd(server_fd_, POLLIN,
                                   [this](int fd, short events, short revents) {
                                   this->handle_server_event(fd, events, revents);
                                   });

        if (!success) {
            return false;
        }

        stats_timer_id_ = ev_->add_timer(10000,
                                         [this](int timer_id) {
                                         this->print_stats(timer_id);
                                         }, true);

        return true;
    }

    void stop() {
        if (stats_timer_id_ >= 0) {
            ev_->remove_timer(stats_timer_id_);
            stats_timer_id_ = -1;
        }

        if (server_fd_ >= 0) {
            ev_->remove_fd(server_fd_);
            close(server_fd_);
            server_fd_ = -1;
        }
    }

private:
    void handle_server_event(int fd, short events, short revents) {
        (void)events;
        if (revents & POLLIN) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);

            int client_fd = accept(fd, (sockaddr*)&client_addr, &client_len);
            if (client_fd >= 0) {
                connection_count_++;
                std::cout << "New client connected: " << client_fd
                    << " (total: " << connection_count_ << ")" << std::endl;

                ev_->add_fd(client_fd, POLLIN,
                            [this](int cfd, short ev, short rev) {
                            this->handle_client_event(cfd, ev, rev);
                            });
                ev_->add_timer(30000,
                               [this, client_fd](int timer_id) {
                               (void) timer_id;
                               std::cout << "Auto-disconnecting client " << client_fd << std::endl;
                               this->disconnect_client(client_fd);
                               }, false);
                add_client(client_fd);
            }
        }

        if (revents & (POLLERR | POLLHUP)) {
            std::cerr << "Server socket error!" << std::endl;
        }
    }

    void handle_client_event(int client_fd, short events, short revents) {
        (void)events;
        if (revents & POLLIN) {
            char buffer[1024];
            ssize_t bytes = read(client_fd, buffer, sizeof(buffer) - 1);

            if (bytes > 0) {
                buffer[bytes] = '\0';
                std::cout << "Received from client " << client_fd << ": " << buffer;
                int rc = write(client_fd, buffer, bytes);
                if (rc != bytes) {
                    disconnect_client(client_fd);
                }
                client_map_[client_fd]->rbytes += bytes;
            } else if (bytes == 0) {
                disconnect_client(client_fd);
            }
        }

        if (revents & (POLLERR | POLLHUP)) {
            std::cout << "Client " << client_fd << " error, closing" << std::endl;
            disconnect_client(client_fd);
        }
    }

    void disconnect_client(int client_fd) {
        connection_count_--;
        std::cout << "Client " << client_fd << " disconnected (remaining: "
            << connection_count_ << ")" << std::endl;
        remove_client(client_fd);
        ev_->remove_fd(client_fd);
        close(client_fd);
    }

    void print_stats(int timer_id) {
        std::cout << "=== Server Stats (Timer ID: " << timer_id << ") ===" << std::endl;
        std::cout << "Active connections: " << connection_count_ << std::endl;
        std::cout << "Total FDs monitored: " << ev_->get_fd_count() << std::endl;
        std::cout << "=========================================" << std::endl;
    }
};

class HeartbeatService {
private:
    EvLoop* ev_;
    int heartbeat_timer_id_;
    int counter_;

public:
    HeartbeatService(EvLoop* em) : ev_(em),
        heartbeat_timer_id_(-1), counter_(0) {}

    ~HeartbeatService() {
        stop();
    }

    bool start(int interval_ms) {
        heartbeat_timer_id_ = ev_->add_timer(interval_ms,
                                             [this](int timer_id) {
                                             this->send_heartbeat(timer_id);
                                             }, true);

        return heartbeat_timer_id_ >= 0;
    }

    void stop() {
        if (heartbeat_timer_id_ >= 0) {
            ev_->remove_timer(heartbeat_timer_id_);
            heartbeat_timer_id_ = -1;
        }
    }

    void change_interval(int new_interval_ms) {
        if (heartbeat_timer_id_ >= 0) {
            ev_->update_timer_interval(heartbeat_timer_id_, new_interval_ms);
            std::cout << "Heartbeat interval changed to " << new_interval_ms << "ms" << std::endl;
        }
    }

private:
    void send_heartbeat(int timer_id) {
        counter_++;
        std::cout << "❤️  Heartbeat #" << counter_ << " (Timer: " << timer_id << ")" << std::endl;
        if (counter_ == 20) {
            std::cout << "Changing heartbeat interval from 2s to 5s..." << std::endl;
            change_interval(5000);
        }
    }
};

int main() {

    EvLoop ev;
    g_ev_ = &ev;

    signal(SIGINT, signalHandler);

    TcpServer server(&ev);

    if (!server.start(9000)) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }
    std::cout << "Server started on port 9000" << std::endl;
    HeartbeatService heartbeat(&ev);
    heartbeat.start(2000);

    ev.add_timer(5000,
                 [](int timer_id) {
                 std::cout << "🎯 One-shot timer triggered! (ID: " << timer_id << ")" << std::endl;
                 std::cout << "💡 You can connect with: telnet localhost 8080" << std::endl;
                 }, false);

    std::cout << "Starting event loop..." << std::endl;
    std::cout << "Heartbeat service running every 2 seconds" << std::endl;

    ev.run(10000);
    std::cout << "Event loop stopped." << std::endl;

    return 0;
}
