// UltraAI/tests/fake_engine.cpp
// A stand-in for llama-server, so the local runtime and the `local`
// provider are tested without an engine or a model: it takes the command
// line the runtime builds (--host, --port, --api-key-file, --model,
// --alias, the rest ignored), serves /health without a key and the
// OpenAI-shaped /v1/models, /v1/chat/completions and /v1/embeddings with
// one, and ends on SIGTERM like the real one. A --model path that does not
// exist ends it with status 1, as llama-server does; an alias "crash-me"
// ends it with status 2 before listening, for the restart-marking test.
//
// One request per connection, single threaded: enough for a test, and
// what libcurl's "Connection: close" handling expects.

#include <nlohmann/json.hpp>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
static void CloseSocket(SocketHandle s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
static const SocketHandle INVALID_SOCKET = -1;
static void CloseSocket(SocketHandle s) { close(s); }
#endif

using nlohmann::json;

namespace {

struct Request {
    std::string method, path, body;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string Header(const std::string& name) const {
        for (const auto& h : headers) {
            std::string lower = h.first;
            for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower == name) return h.second;
        }
        return {};
    }
};

bool ReadRequest(SocketHandle s, Request& out) {
    std::string data;
    char buffer[4096];
    size_t headerEnd = std::string::npos;
    while (headerEnd == std::string::npos) {
        const int n = static_cast<int>(recv(s, buffer, sizeof(buffer), 0));
        if (n <= 0) return false;
        data.append(buffer, static_cast<size_t>(n));
        headerEnd = data.find("\r\n\r\n");
        if (data.size() > 1 << 20) return false;
    }
    std::istringstream head(data.substr(0, headerEnd));
    std::string line;
    std::getline(head, line);
    std::istringstream requestLine(line);
    requestLine >> out.method >> out.path;
    while (std::getline(head, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string value = line.substr(colon + 1);
        while (!value.empty() && value.front() == ' ') value.erase(value.begin());
        out.headers.push_back({line.substr(0, colon), value});
    }
    const size_t length = static_cast<size_t>(std::atoll(out.Header("content-length").c_str()));
    out.body = data.substr(headerEnd + 4);
    while (out.body.size() < length) {
        const int n = static_cast<int>(recv(s, buffer, sizeof(buffer), 0));
        if (n <= 0) return false;
        out.body.append(buffer, static_cast<size_t>(n));
    }
    out.body.resize(length);
    return true;
}

void Respond(SocketHandle s, int status, const std::string& body) {
    const char* reason = status == 200 ? "OK" : status == 401 ? "Unauthorized" : "Not Found";
    std::string response = "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: " + std::to_string(body.size()) + "\r\n"
                           "Connection: close\r\n\r\n" + body;
    size_t sent = 0;
    while (sent < response.size()) {
        const int n = static_cast<int>(send(s, response.data() + sent, response.size() - sent, 0));
        if (n <= 0) return;
        sent += static_cast<size_t>(n);
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string host = "127.0.0.1", apiKeyFile, model, alias = "fake";
    int port = 0;
    for (int i = 1; i + 1 < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--host") host = argv[++i];
        else if (a == "--port") port = std::atoi(argv[++i]);
        else if (a == "--api-key-file") apiKeyFile = argv[++i];
        else if (a == "--model") model = argv[++i];
        else if (a == "--alias") alias = argv[++i];
    }
    if (alias == "crash-me") {
        std::fprintf(stderr, "fake engine: asked to crash\n");
        return 2;
    }
    if (!model.empty()) {
        std::ifstream in(model, std::ios::binary);
        if (!in) {
            std::fprintf(stderr, "fake engine: cannot open model %s\n", model.c_str());
            return 1;
        }
    }
    std::string apiKey;
    if (!apiKeyFile.empty()) {
        std::ifstream in(apiKeyFile);
        std::getline(in, apiKey);
        while (!apiKey.empty() && (apiKey.back() == '\r' || apiKey.back() == ' ')) apiKey.pop_back();
    }

#if defined(_WIN32)
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    SocketHandle listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener == INVALID_SOCKET) return 3;
    int one = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::fprintf(stderr, "fake engine: cannot bind port %d: %s\n", port, std::strerror(errno));
        return 4;
    }
    if (listen(listener, 16) != 0) return 5;
    std::fprintf(stderr, "fake engine: listening on http://%s:%d alias=%s\n", host.c_str(), port,
                 alias.c_str());
    std::fflush(stderr);

    while (true) {
        SocketHandle client = accept(listener, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;
        Request req;
        if (ReadRequest(client, req)) {
            if (req.path == "/health" || req.path == "/v1/health") {
                Respond(client, 200, R"({"status":"ok"})");
            } else if (!apiKey.empty() && req.Header("authorization") != "Bearer " + apiKey) {
                Respond(client, 401, R"({"error":{"message":"Invalid API Key","type":"authentication_error","code":401}})");
            } else if (req.path == "/v1/models") {
                json models = {{"object", "list"}, {"data", json::array({{{"id", alias}, {"object", "model"}}})}};
                Respond(client, 200, models.dump());
            } else if (req.path == "/v1/chat/completions") {
                json body = json::parse(req.body, nullptr, false);
                std::string last = "(nothing)";
                if (body.is_object() && body.contains("messages") && body["messages"].is_array() &&
                    !body["messages"].empty()) {
                    const json& m = body["messages"].back();
                    if (m.contains("content") && m["content"].is_string()) last = m["content"].get<std::string>();
                }
                json reply = {
                    {"id", "chatcmpl-fake"},
                    {"object", "chat.completion"},
                    {"model", alias},
                    {"choices", json::array({{{"index", 0},
                                               {"message", {{"role", "assistant"}, {"content", "echo: " + last}}},
                                               {"finish_reason", "stop"}}})},
                    {"usage", {{"prompt_tokens", 3}, {"completion_tokens", 2}, {"total_tokens", 5}}}};
                Respond(client, 200, reply.dump());
            } else if (req.path == "/v1/embeddings") {
                json body = json::parse(req.body, nullptr, false);
                size_t count = 1;
                if (body.is_object() && body.contains("input") && body["input"].is_array()) count = body["input"].size();
                json data = json::array();
                for (size_t i = 0; i < count; ++i) {
                    data.push_back({{"object", "embedding"}, {"index", i},
                                    {"embedding", json::array({0.1, 0.2, 0.3})}});
                }
                json reply = {{"object", "list"}, {"model", alias}, {"data", data},
                              {"usage", {{"prompt_tokens", 2}, {"total_tokens", 2}}}};
                Respond(client, 200, reply.dump());
            } else {
                Respond(client, 404, R"({"error":{"message":"not found"}})");
            }
        }
        CloseSocket(client);
    }
}
