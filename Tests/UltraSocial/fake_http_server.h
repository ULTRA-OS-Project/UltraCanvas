// Tests/UltraSocial/fake_http_server.h
// In-process fake HTTP server for connector tests: listens on
// 127.0.0.1:<ephemeral>, serves a scripted list of responses in order, and
// captures every request (start line, headers, body) for assertions.
// Built on UltraNet sockets — no external network, CI-safe.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <UltraNet/UltraNetSocket.h>

#include <cstdlib>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace ultrasocial_test {

struct CapturedRequest {
    std::string startLine;   // "POST /api/v1/statuses HTTP/1.1"
    std::string head;        // full header block
    std::string body;

    std::string Path() const {
        std::size_t sp1 = startLine.find(' ');
        std::size_t sp2 = startLine.find(' ', sp1 + 1);
        if (sp1 == std::string::npos || sp2 == std::string::npos) return {};
        return startLine.substr(sp1 + 1, sp2 - sp1 - 1);
    }
    bool HeadHas(const std::string& needle) const {
        return head.find(needle) != std::string::npos;
    }
    bool BodyHas(const std::string& needle) const {
        return body.find(needle) != std::string::npos;
    }
};

struct ScriptedResponse {
    int status = 200;
    std::string body;
    std::string contentType = "application/json";
};

class FakeHttpServer {
public:
    explicit FakeHttpServer(std::vector<ScriptedResponse> responses)
        : responses_(std::move(responses)) {
        UltraNetSocketOptions options;
        options.bindAddress  = "127.0.0.1";
        options.reuseAddress = true;
        listener_ = UltraNet_TcpListen(0, options);
        if (listener_ == UltraNetInvalidHandle) return;
        UltraNetEndpoint local;
        if (!UltraNet_SocketLocalEndpoint(listener_, local)) return;
        port_ = local.port;
        thread_ = std::thread([this] { Serve(); });
    }

    ~FakeHttpServer() { Join(); }

    int Port() const { return port_; }
    std::string BaseUrl() const {
        return "http://127.0.0.1:" + std::to_string(port_);
    }

    // Wait for all scripted exchanges to finish (or time out); call before
    // inspecting requests.
    void Join() {
        if (thread_.joinable()) thread_.join();
        if (listener_ != UltraNetInvalidHandle) {
            UltraNet_SocketClose(listener_);
            listener_ = UltraNetInvalidHandle;
        }
    }

    const std::vector<CapturedRequest>& Requests() const { return requests_; }

private:
    void Serve() {
        for (const auto& response : responses_) {
            UltraNetEndpoint remote;
            UltraNetHandle conn = UltraNet_TcpAccept(listener_, remote, 8000);
            if (conn == UltraNetInvalidHandle) return;

            std::string raw;
            std::size_t bodyStart = std::string::npos;
            std::size_t contentLength = 0;
            while (raw.size() < (16u << 20)) {
                if (bodyStart == std::string::npos) {
                    std::size_t sep = raw.find("\r\n\r\n");
                    if (sep != std::string::npos) {
                        bodyStart = sep + 4;
                        std::size_t cl = raw.find("Content-Length:");
                        if (cl == std::string::npos) cl = raw.find("content-length:");
                        if (cl != std::string::npos && cl < sep) {
                            contentLength = static_cast<std::size_t>(
                                std::atoll(raw.c_str() + cl + 15));
                        }
                    }
                }
                if (bodyStart != std::string::npos &&
                    raw.size() >= bodyStart + contentLength) {
                    break;
                }
                std::vector<uint8_t> chunk;
                auto r = UltraNet_TcpReceive(conn, chunk, 65536, 5000);
                if (!r || chunk.empty()) break;
                raw.append(chunk.begin(), chunk.end());
            }

            CapturedRequest request;
            if (bodyStart != std::string::npos) {
                request.head = raw.substr(0, bodyStart);
                request.body = raw.substr(bodyStart);
            } else {
                request.head = raw;
            }
            std::size_t eol = request.head.find("\r\n");
            request.startLine = eol == std::string::npos
                                    ? request.head : request.head.substr(0, eol);
            requests_.push_back(std::move(request));

            std::string statusText = response.status == 200 ? "OK" : "Status";
            std::string wire = "HTTP/1.1 " + std::to_string(response.status) +
                " " + statusText + "\r\n"
                "Content-Type: " + response.contentType + "\r\n"
                "Content-Length: " + std::to_string(response.body.size()) + "\r\n"
                "Connection: close\r\n\r\n" + response.body;
            int sent = 0;
            UltraNet_TcpSend(conn, std::vector<uint8_t>(wire.begin(), wire.end()),
                             sent);
            UltraNet_SocketClose(conn);
        }
    }

    UltraNetHandle listener_ = UltraNetInvalidHandle;
    int port_ = 0;
    std::thread thread_;
    std::vector<ScriptedResponse> responses_;
    std::vector<CapturedRequest> requests_;
};

} // namespace ultrasocial_test
