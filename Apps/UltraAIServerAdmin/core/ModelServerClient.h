// Apps/UltraAIServerAdmin/core/ModelServerClient.h
// The admin API of the UltraAI model server (Server/UltraAIModelServer), as
// UltraAI ServerAdmin's window and command line use it: the models with
// their files (uploaded or hosted elsewhere) and their state, the topics and
// the models each offers, the engines of the gallery, the uploads, the
// administrator accounts and the audit log. Every call is one HTTPS request
// through UltraNet with the administrator's bearer token; nothing is cached,
// so the window always shows what the server has.
//
// Blocking: call from a worker thread in a window. JSON in and out is
// UltraCanvasJSON.
// Version: 0.1.0
// Author: UltraAI Module
#pragma once

#include "DataFormats/UltraCanvasJSON.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace UltraAIServerAdmin {

using UltraCanvas::JSONValue;

// Where the server is and who signs in: the base URL
// ("https://models.example.org") and the token the server's administrator
// gave out (ULTRAAI_MODELS_SUPER_TOKEN, or an account's).
struct Connection {
    std::string baseUrl;
    std::string token;
};

// One answer. `ok` for a 2xx; otherwise `error` is the server's error name
// ("not_found", "not_publishable", "forbidden", ...) or "network" when no
// answer came, and `message` says what went wrong in words.
struct Reply {
    bool ok = false;
    int status = 0;
    std::string error;
    std::string message;
    JSONValue body;
};

// The capabilities a model or topic is for, as the server names them.
const std::vector<std::string>& Capabilities();
// The platforms an engine bundle is built for.
const std::vector<std::string>& Platforms();
// "textllm" -> "Chat", ...
std::string CapabilityLabel(const std::string& capability);

// SHA-256 of a file as lowercase hex (UltraCrypt). False with a message
// when the file cannot be read or the build has no hashing.
bool Sha256OfFile(const std::string& pathUtf8, std::string& outHex, std::string& outError);

class ModelServerClient {
public:
    explicit ModelServerClient(Connection connection);

    const Connection& GetConnection() const { return connection_; }
    // The base URL with a trailing slash removed and https:// checked;
    // empty when it is not usable (plain http:// is refused: the token
    // would travel in clear), with the reason in `why`.
    static std::string NormaliseBaseUrl(const std::string& url, std::string* why = nullptr);

    // ----- raw -----
    Reply Get(const std::string& path) const;
    Reply Put(const std::string& path, const JSONValue& body) const;
    Reply Post(const std::string& path, const JSONValue& body = JSONValue::MakeObject()) const;
    Reply Delete(const std::string& path) const;

    // ----- who -----
    Reply WhoAmI() const;                 // {name, role, server, published, revision, baseUrl}

    // ----- models -----
    Reply ListModels() const;             // {models: [...]}
    Reply GetModel(const std::string& id) const;
    // Create a draft or change one; `model` as the catalogue has it
    // (displayName, description, capability, engine, license, requires,
    // features, engineArgs, tags). Its files are left alone.
    Reply PutModel(const std::string& id, const JSONValue& model) const;
    Reply DeleteModel(const std::string& id) const;
    Reply CheckModel(const std::string& id) const;   // {publishable, problems: [...]}
    Reply Publish(const std::string& id) const;
    Reply Withdraw(const std::string& id) const;
    Reply BackToDraft(const std::string& id) const;
    // A file hosted elsewhere (Hugging Face, a CDN): its https URL, its
    // SHA-256 (may be empty only for a Hugging Face file) and its size.
    Reply PutExternalFile(const std::string& modelId, const std::string& name, const std::string& url,
                          const std::string& sha256, uint64_t size) const;
    // A file uploaded to the server: hashed here first, sent with its hash
    // so the server checks it and skips one it already has. `onProgress`
    // (bytes sent, total) fires on the calling thread's transfer.
    Reply UploadFile(const std::string& modelId, const std::string& name, const std::string& localPathUtf8,
                     std::function<void(int64_t, int64_t)> onProgress = {}) const;
    Reply RemoveModelFile(const std::string& modelId, const std::string& name) const;

    // ----- topics -----
    Reply ListTopics() const;             // {topics: [{id, name, description, capability, position, models: [{id, default?}]}]}
    Reply PutTopic(const std::string& id, const JSONValue& topic) const;
    Reply DeleteTopic(const std::string& id) const;

    // ----- engines (super) -----
    Reply ListEngines() const;            // {engines: [...]} as the gallery index has them
    Reply PutEngine(const std::string& id, const JSONValue& engine) const;
    Reply DeleteEngine(const std::string& id) const;
    // Load documents in the formats a client reads: {index?, catalogue?, topics?, publish?}.
    Reply Import(const JSONValue& documents) const;

    // ----- uploads -----
    Reply ListUploads() const;            // {blobs: [{sha256, size, uploaded_at, uploaded_by, original_name, used}]}
    Reply PruneUploads() const;           // super: {deleted, bytes}

    // ----- accounts (super) -----
    Reply ListAdmins() const;             // {admins: [{name, role, created_at, created_by, last_used_at}]}
    // Make an account or give one a new role; the answer carries its new
    // token, which the server never shows again.
    Reply PutAdmin(const std::string& name, const std::string& role) const;
    Reply DeleteAdmin(const std::string& name) const;

    Reply Audit(int64_t since = 0) const; // {entries: [{seq, at, admin, action, target, detail}], next}

    // ----- what a client reads (no token) -----
    Reply PublicCatalogue() const;
    Reply PublicTopics() const;

private:
    Reply Send(const std::string& method, const std::string& path, const std::string& body,
               const std::string& bodyFileUtf8, const std::vector<std::pair<std::string, std::string>>& headers,
               std::function<void(int64_t, int64_t)> onProgress, bool authenticated = true) const;
    static std::string Escape(const std::string& segment);   // one path segment, percent-encoded

    Connection connection_;
};

} // namespace UltraAIServerAdmin
