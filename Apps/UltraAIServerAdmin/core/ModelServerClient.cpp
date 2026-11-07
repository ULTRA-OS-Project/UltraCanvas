// Apps/UltraAIServerAdmin/core/ModelServerClient.cpp
// The model server's admin API over UltraNet (ModelServerClient.h).
// Version: 0.1.0
// Author: UltraAI Module

#include "ModelServerClient.h"

#include "UltraCrypt/UltraCryptCore.h"
#include "UltraNet/UltraNetHttp.h"

#include <algorithm>
#include <cctype>

namespace UltraAIServerAdmin {

using namespace UltraCanvas;

const std::vector<std::string>& Capabilities() {
    static const std::vector<std::string> list = {"textllm", "embeddings", "speechtotext", "texttospeech", "imagegen"};
    return list;
}

const std::vector<std::string>& Platforms() {
    static const std::vector<std::string> list = {"linux-x64", "linux-arm64", "macos-x64", "macos-arm64", "win-x64", "win-arm64"};
    return list;
}

std::string CapabilityLabel(const std::string& capability) {
    if (capability == "textllm")      return "Chat";
    if (capability == "embeddings")   return "Embeddings";
    if (capability == "speechtotext") return "Speech to text";
    if (capability == "texttospeech") return "Speech";
    if (capability == "imagegen")     return "Images";
    return capability;
}

bool Sha256OfFile(const std::string& pathUtf8, std::string& outHex, std::string& outError) {
    std::vector<uint8_t> digest;
    const UltraCryptResult r = UltraCrypt_HashFile(UltraCryptHashAlgorithm::SHA256, pathUtf8, digest);
    if (!r.success) {
        outError = r.message.empty() ? std::string("SHA-256 is not available in this build") : r.message;
        return false;
    }
    outHex = UltraCrypt_ToHex(digest);
    std::transform(outHex.begin(), outHex.end(), outHex.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return true;
}

ModelServerClient::ModelServerClient(Connection connection) : connection_(std::move(connection)) {
    connection_.baseUrl = NormaliseBaseUrl(connection_.baseUrl);
}

std::string ModelServerClient::NormaliseBaseUrl(const std::string& url, std::string* why) {
    std::string out = url;
    const size_t first = out.find_first_not_of(" \t\r\n");
    out = first == std::string::npos ? std::string() : out.substr(first, out.find_last_not_of(" \t\r\n") - first + 1);
    // A gallery address pasted from a client's settings is fine too.
    const std::string gallery = "/gallery/index.json";
    if (out.size() > gallery.size() && out.compare(out.size() - gallery.size(), gallery.size(), gallery) == 0) {
        out.resize(out.size() - gallery.size());
    }
    while (!out.empty() && out.back() == '/') out.pop_back();
    std::string lower = out;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    // http://localhost and 127.0.0.1 are accepted for a server under test on
    // this machine; anywhere else the token must not travel in clear.
    const bool loopback = lower.rfind("http://127.0.0.1", 0) == 0 || lower.rfind("http://localhost", 0) == 0 ||
                          lower.rfind("http://[::1]", 0) == 0;
    if (out.empty()) {
        if (why) *why = "No server address.";
        return {};
    }
    if (lower.rfind("https://", 0) != 0 && !loopback) {
        if (why) *why = "The server's address starts with https:// (the token would otherwise travel in clear).";
        return {};
    }
    return out;
}

std::string ModelServerClient::Escape(const std::string& segment) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : segment) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '+' || c == '@') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

Reply ModelServerClient::Send(const std::string& method, const std::string& path, const std::string& body,
                              const std::string& bodyFileUtf8,
                              const std::vector<std::pair<std::string, std::string>>& headers,
                              std::function<void(int64_t, int64_t)> onProgress, bool authenticated) const {
    Reply reply;
    if (connection_.baseUrl.empty()) {
        reply.error = "network";
        reply.message = "No usable server address (https:// is required).";
        return reply;
    }
    if (!UltraNet_IsInitialized()) UltraNet_Initialize();
    UltraNetHttpRequest request;
    request.url = connection_.baseUrl + path;
    request.method = method == "GET"    ? UltraNetHttpMethod::Get
                   : method == "PUT"    ? UltraNetHttpMethod::Put
                   : method == "POST"   ? UltraNetHttpMethod::Post
                   : method == "DELETE" ? UltraNetHttpMethod::Delete
                                        : UltraNetHttpMethod::Get;
    request.options.httpsOnlyRedirects = true;
    request.options.headers.Set("Accept", "application/json");
    if (authenticated) request.options.headers.Set("Authorization", "Bearer " + connection_.token);
    for (const auto& [name, value] : headers) request.options.headers.Set(name, value);
    if (!bodyFileUtf8.empty()) {
        request.bodyFilePath = bodyFileUtf8;
        request.options.headers.Set("Content-Type", "application/octet-stream");
        request.options.timeoutMs = -1;            // a model is gigabytes: no whole-transfer limit
        request.options.stallTimeoutMs = 120000;   // but a transfer that stops is given up
        if (onProgress) request.onUploadProgress = onProgress;
    } else if (method == "PUT" || method == "POST") {
        request.body.assign(body.begin(), body.end());
        request.options.headers.Set("Content-Type", "application/json");
    }
    UltraNetResponse response;
    UltraNet_HttpRequest(request, response);
    reply.status = response.statusCode;
    if (!response.IsComplete() || response.statusCode == 0) {
        reply.error = "network";
        reply.message = response.transferError.empty() ? "The server did not answer." : response.transferError;
        return reply;
    }
    const std::string text = response.GetBodyAsString();
    JSONParseResult parsed;
    reply.body = text.empty() ? JSONValue::MakeObject() : JSON::Parse(text, &parsed);
    reply.ok = response.IsSuccess();
    if (!reply.ok) {
        reply.error = reply.body["error"].GetString("http_" + std::to_string(response.statusCode));
        reply.message = reply.body["error_description"].GetString(
            "The server answered HTTP " + std::to_string(response.statusCode) + ".");
        if (response.statusCode == 401) reply.message = "The server does not accept this token. " + reply.message;
    } else if (!text.empty() && !parsed.success) {
        reply.ok = false;
        reply.error = "invalid_reply";
        reply.message = "The server's answer is not JSON: is this an UltraAI model server?";
    }
    return reply;
}

Reply ModelServerClient::Get(const std::string& path) const { return Send("GET", path, {}, {}, {}, {}); }
Reply ModelServerClient::Put(const std::string& path, const JSONValue& body) const {
    return Send("PUT", path, JSON::Serialize(body), {}, {}, {});
}
Reply ModelServerClient::Post(const std::string& path, const JSONValue& body) const {
    return Send("POST", path, JSON::Serialize(body), {}, {}, {});
}
Reply ModelServerClient::Delete(const std::string& path) const { return Send("DELETE", path, {}, {}, {}, {}); }

Reply ModelServerClient::WhoAmI() const { return Get("/v1/admin/whoami"); }

Reply ModelServerClient::ListModels() const { return Get("/v1/admin/models"); }
Reply ModelServerClient::GetModel(const std::string& id) const { return Get("/v1/admin/models/" + Escape(id)); }
Reply ModelServerClient::PutModel(const std::string& id, const JSONValue& model) const {
    return Put("/v1/admin/models/" + Escape(id), model);
}
Reply ModelServerClient::DeleteModel(const std::string& id) const { return Delete("/v1/admin/models/" + Escape(id)); }
Reply ModelServerClient::CheckModel(const std::string& id) const { return Get("/v1/admin/models/" + Escape(id) + "/check"); }
Reply ModelServerClient::Publish(const std::string& id) const { return Post("/v1/admin/models/" + Escape(id) + "/publish"); }
Reply ModelServerClient::Withdraw(const std::string& id) const { return Post("/v1/admin/models/" + Escape(id) + "/withdraw"); }
Reply ModelServerClient::BackToDraft(const std::string& id) const { return Post("/v1/admin/models/" + Escape(id) + "/draft"); }

Reply ModelServerClient::PutExternalFile(const std::string& modelId, const std::string& name, const std::string& url,
                                         const std::string& sha256, uint64_t size) const {
    JSONValue body = JSONValue::MakeObject();
    body.Set("url", url);
    if (!sha256.empty()) body.Set("sha256", sha256);
    body.Set("size", static_cast<int64_t>(size));
    return Put("/v1/admin/models/" + Escape(modelId) + "/files/" + Escape(name), body);
}

Reply ModelServerClient::UploadFile(const std::string& modelId, const std::string& name,
                                    const std::string& localPathUtf8,
                                    std::function<void(int64_t, int64_t)> onProgress) const {
    std::string sha, error;
    if (!Sha256OfFile(localPathUtf8, sha, error)) {
        Reply reply;
        reply.error = "local_file";
        reply.message = localPathUtf8 + ": " + error;
        return reply;
    }
    return Send("PUT", "/v1/admin/models/" + Escape(modelId) + "/files/" + Escape(name) + "/upload", {},
                localPathUtf8, {{"X-Content-SHA256", sha}}, std::move(onProgress));
}

Reply ModelServerClient::RemoveModelFile(const std::string& modelId, const std::string& name) const {
    return Delete("/v1/admin/models/" + Escape(modelId) + "/files/" + Escape(name));
}

Reply ModelServerClient::ListTopics() const { return Get("/v1/admin/topics"); }
Reply ModelServerClient::PutTopic(const std::string& id, const JSONValue& topic) const {
    return Put("/v1/admin/topics/" + Escape(id), topic);
}
Reply ModelServerClient::DeleteTopic(const std::string& id) const { return Delete("/v1/admin/topics/" + Escape(id)); }

Reply ModelServerClient::ListEngines() const { return Get("/v1/admin/engines"); }
Reply ModelServerClient::PutEngine(const std::string& id, const JSONValue& engine) const {
    return Put("/v1/admin/engines/" + Escape(id), engine);
}
Reply ModelServerClient::DeleteEngine(const std::string& id) const { return Delete("/v1/admin/engines/" + Escape(id)); }
Reply ModelServerClient::Import(const JSONValue& documents) const { return Post("/v1/admin/import", documents); }

Reply ModelServerClient::ListUploads() const { return Get("/v1/admin/blobs"); }
Reply ModelServerClient::PruneUploads() const { return Post("/v1/admin/blobs/prune"); }

Reply ModelServerClient::ListAdmins() const { return Get("/v1/admin/admins"); }
Reply ModelServerClient::PutAdmin(const std::string& name, const std::string& role) const {
    JSONValue body = JSONValue::MakeObject();
    body.Set("role", role);
    return Put("/v1/admin/admins/" + Escape(name), body);
}
Reply ModelServerClient::DeleteAdmin(const std::string& name) const { return Delete("/v1/admin/admins/" + Escape(name)); }

Reply ModelServerClient::Audit(int64_t since) const { return Get("/v1/admin/audit?since=" + std::to_string(since)); }

Reply ModelServerClient::PublicCatalogue() const { return Send("GET", "/gallery/catalogue.json", {}, {}, {}, {}, false); }
Reply ModelServerClient::PublicTopics() const { return Send("GET", "/gallery/topics.json", {}, {}, {}, {}, false); }

} // namespace UltraAIServerAdmin
