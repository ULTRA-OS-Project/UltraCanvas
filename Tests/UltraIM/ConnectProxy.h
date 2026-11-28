// Tests/UltraIM/ConnectProxy.h
// A web proxy for the provider tests, on 127.0.0.1: it takes
// "CONNECT host:port", writes the target down, and carries the connection to
// that port on this computer whatever the host name was (the names the tests
// ask for exist nowhere), until either side closes. For a connection that is
// to go through the system's web proxy: IRC and XMPP over TCP.
// SystemWebProxy makes it the operating system's proxy for a test's length,
// the way Linux names one: the environment (https_proxy), with UltraNet's
// setting the system's.
// Version: 0.1.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework
#pragma once

#if !defined(_WIN32)

#include "UltraNet/UltraNetCore.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

struct ConnectProxy {
    int port = 0;

    ~ConnectProxy() { Stop(); }

    bool Start() {
        listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listener_ < 0) return false;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        socklen_t len = sizeof addr;
        if (::bind(listener_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 ||
            ::getsockname(listener_, reinterpret_cast<sockaddr*>(&addr), &len) != 0 || ::listen(listener_, 16) != 0)
            return false;
        port = ntohs(addr.sin_port);
        acceptor_ = std::thread([this]() {
            for (;;) {
                const int client = ::accept(listener_, nullptr, nullptr);
                if (client < 0) return;
                std::lock_guard<std::mutex> lock(mutex_);
                sockets_.push_back(client);
                carriers_.emplace_back([this, client]() { Carry(client); });
            }
        });
        return true;
    }

    void Stop() {
        if (listener_ < 0) return;
        ::shutdown(listener_, SHUT_RDWR);
        if (acceptor_.joinable()) acceptor_.join();
        std::vector<std::thread> carriers;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const int fd : sockets_) ::shutdown(fd, SHUT_RDWR);
            carriers.swap(carriers_);
        }
        for (std::thread& t : carriers) t.join();
        ::close(listener_);
        listener_ = -1;
    }

    // "irc.example.test:6667": every target asked for, in order.
    std::vector<std::string> Targets() {
        std::lock_guard<std::mutex> lock(mutex_);
        return targets_;
    }

private:
    void Carry(int client) {
        std::string head;
        char c = 0;
        while (head.size() < 8192 && head.find("\r\n\r\n") == std::string::npos && ::recv(client, &c, 1, 0) == 1) head += c;
        const size_t space = head.find(' ', 8);
        const std::string target = head.rfind("CONNECT ", 0) == 0 && space != std::string::npos ? head.substr(8, space - 8) : "";
        const size_t colon = target.rfind(':');
        int server = -1;
        if (colon != std::string::npos) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                targets_.push_back(target);
            }
            server = ::socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = htons(static_cast<uint16_t>(std::atoi(target.c_str() + colon + 1)));
            if (server >= 0 && ::connect(server, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
                ::close(server);
                server = -1;
            }
        }
        if (server < 0) {
            const std::string refused = "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\n\r\n";
            ::send(client, refused.data(), refused.size(), MSG_NOSIGNAL);
            ::close(client);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sockets_.push_back(server);
        }
        const std::string established = "HTTP/1.1 200 Connection established\r\n\r\n";
        ::send(client, established.data(), established.size(), MSG_NOSIGNAL);
        pollfd both[2] = {{client, POLLIN, 0}, {server, POLLIN, 0}};
        char buffer[4096];
        for (bool open = true; open;) {
            if (::poll(both, 2, -1) <= 0) break;
            for (int i = 0; i < 2 && open; ++i) {
                if (!(both[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
                const ssize_t n = ::recv(both[i].fd, buffer, sizeof buffer, 0);
                if (n <= 0 || ::send(both[1 - i].fd, buffer, static_cast<size_t>(n), MSG_NOSIGNAL) != n) open = false;
            }
        }
        ::shutdown(client, SHUT_RDWR);
        ::shutdown(server, SHUT_RDWR);
        ::close(client);
        ::close(server);
    }

    int listener_ = -1;
    std::thread acceptor_;
    std::mutex mutex_;
    std::vector<int> sockets_;
    std::vector<std::thread> carriers_;
    std::vector<std::string> targets_;
};

// The system's settings naming a web proxy on this computer and nothing
// else - no SOCKS proxy, no exceptions - until it goes out of scope. Linux:
// elsewhere the system's proxy is not read from the environment.
struct SystemWebProxy {
    explicit SystemWebProxy(int port) : setting_(UltraNet_GetGlobalProxy()) {
        for (const char* name : Names()) {
            if (const char* value = std::getenv(name)) saved_.emplace_back(name, value);
            ::unsetenv(name);
        }
        ::setenv("https_proxy", ("http://127.0.0.1:" + std::to_string(port)).c_str(), 1);
        UltraNet_SetGlobalProxy(UltraNetProxyConfig::FromSystem());
    }
    ~SystemWebProxy() {
        for (const char* name : Names()) ::unsetenv(name);
        for (const auto& kv : saved_) ::setenv(kv.first.c_str(), kv.second.c_str(), 1);
        UltraNet_SetGlobalProxy(setting_);
    }
    SystemWebProxy(const SystemWebProxy&) = delete;
    SystemWebProxy& operator=(const SystemWebProxy&) = delete;

private:
    static std::vector<const char*> Names() {
        return {"http_proxy", "HTTP_PROXY", "https_proxy", "HTTPS_PROXY", "all_proxy",
                "ALL_PROXY",  "ftp_proxy",  "FTP_PROXY",   "no_proxy",    "NO_PROXY"};
    }
    UltraNetProxyConfig setting_;
    std::vector<std::pair<std::string, std::string>> saved_;
};

#endif // !_WIN32
