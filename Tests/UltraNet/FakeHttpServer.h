// Tests/UltraNet/FakeHttpServer.h
// A one-thread fake HTTP server on 127.0.0.1:<ephemeral> for the UltraNet
// tests: serves a fixed list of canned answers, one per connection, in
// order, and captures each request's request line and body. The fake token
// endpoint of the OAuth2 tests and the fake sign-in service of the service
// client tests are both this. Join() before reading what was captured.
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetSocket.h>

#include <cstdlib>
#include <string>
#include <thread>
#include <utility>
#include <vector>

struct FakeHttpAnswer {
    int status = 200;
    std::string body;
    std::string contentType = "application/json";
};

struct FakeHttpServer {
    UltraNetHandle listener = UltraNetInvalidHandle;
    int port = 0;
    std::thread thread;
    std::vector<FakeHttpAnswer> answers;
    std::vector<std::string> capturedRequestLines;   // "POST /v1/token HTTP/1.1"
    std::vector<std::string> capturedBodies;

    explicit FakeHttpServer(std::vector<FakeHttpAnswer> canned) : answers(std::move(canned)) {
        UltraNetSocketOptions options;
        options.bindAddress  = "127.0.0.1";
        options.reuseAddress = true;
        listener = UltraNet_TcpListen(0, options);
        if (listener == UltraNetInvalidHandle) return;
        UltraNetEndpoint local;
        if (!UltraNet_SocketLocalEndpoint(listener, local)) return;
        port = local.port;
        thread = std::thread([this] { Serve(); });
    }

    // Every answer a 200 with a JSON body.
    static std::vector<FakeHttpAnswer> Json(const std::vector<std::string>& bodies) {
        std::vector<FakeHttpAnswer> out;
        for (const auto& b : bodies) out.push_back({200, b, "application/json"});
        return out;
    }

    std::string Url(const std::string& path = "") const {
        return "http://127.0.0.1:" + std::to_string(port) + path;
    }

    void Serve() {
        for (const auto& answer : answers) {
            UltraNetEndpoint remote;
            UltraNetHandle conn = UltraNet_TcpAccept(listener, remote, 5000);
            if (conn == UltraNetInvalidHandle) return;

            std::string request;
            std::size_t bodyStart = std::string::npos;
            std::size_t contentLength = 0;
            while (request.size() < 65536) {
                if (bodyStart == std::string::npos) {
                    bodyStart = request.find("\r\n\r\n");
                    if (bodyStart != std::string::npos) {
                        bodyStart += 4;
                        std::size_t cl = request.find("Content-Length:");
                        if (cl == std::string::npos) cl = request.find("content-length:");
                        if (cl != std::string::npos)
                            contentLength = static_cast<std::size_t>(std::atoll(request.c_str() + cl + 15));
                    }
                }
                if (bodyStart != std::string::npos && request.size() >= bodyStart + contentLength) break;
                std::vector<uint8_t> chunk;
                auto r = UltraNet_TcpReceive(conn, chunk, 4096, 3000);
                if (!r || chunk.empty()) break;
                request.append(chunk.begin(), chunk.end());
            }
            const std::size_t eol = request.find("\r\n");
            capturedRequestLines.push_back(eol == std::string::npos ? request : request.substr(0, eol));
            capturedBodies.push_back(bodyStart == std::string::npos ? std::string()
                                                                     : request.substr(bodyStart));

            std::string resp = "HTTP/1.1 " + std::to_string(answer.status) + " "
                + (answer.status == 200 ? "OK" : answer.status == 404 ? "Not Found" : "Status") + "\r\n"
                "Content-Type: " + answer.contentType + "\r\n"
                "Content-Length: " + std::to_string(answer.body.size()) + "\r\n"
                "Connection: close\r\n\r\n" + answer.body;
            int sent = 0;
            UltraNet_TcpSend(conn, std::vector<uint8_t>(resp.begin(), resp.end()), sent);
            UltraNet_SocketClose(conn);
        }
    }

    void Join() {
        if (thread.joinable()) thread.join();
        if (listener != UltraNetInvalidHandle) {
            UltraNet_SocketClose(listener);
            listener = UltraNetInvalidHandle;
        }
    }

    ~FakeHttpServer() { Join(); }
};
