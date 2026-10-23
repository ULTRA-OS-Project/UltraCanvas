// Apps/UltraDBAdmin/engine/DbAdminConnections.cpp
// Version: 0.2.0 - auto-commit per connection
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminConnections.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasPathUtf8.h"
#include <UltraVault/UltraVault.h>
#include <UltraVault/UltraVaultDeviceKeyVault.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <random>
#include <system_error>

using UltraCanvas::JSONValue;
using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

namespace UltraDBAdmin {

namespace {

const char* const kFileName   = "connections.json";
const char* const kKeyPrefix  = "dbadmin.connection.";
const int         kFileFormat = 1;

const char* TlsName(UltraDbTls tls) {
    switch (tls) {
        case UltraDbTls::Disable:    return "disable";
        case UltraDbTls::Prefer:     return "prefer";
        case UltraDbTls::Require:    return "require";
        case UltraDbTls::VerifyFull: return "verify-full";
    }
    return "verify-full";
}

UltraDbTls TlsFromName(const std::string& name) {
    if (name == "disable") return UltraDbTls::Disable;
    if (name == "prefer")  return UltraDbTls::Prefer;
    if (name == "require") return UltraDbTls::Require;
    return UltraDbTls::VerifyFull;   // also what an unknown word falls back to
}

JSONValue ToJson(const ConnectionSpec& s) {
    JSONValue o = JSONValue::MakeObject();
    o.Set("id", s.id);
    o.Set("name", s.name);
    o.Set("driver", s.driver);
    o.Set("database", s.database);
    o.Set("host", s.host);
    o.Set("port", s.port);
    o.Set("user", s.user);
    o.Set("hasPassword", s.hasPassword);
    o.Set("tls", TlsName(s.tls));
    o.Set("readOnly", s.readOnly);
    o.Set("production", s.production);
    o.Set("autoCommit", s.autoCommit);
    return o;
}

ConnectionSpec FromJson(const JSONValue& o) {
    ConnectionSpec s;
    s.id          = o["id"].GetString();
    s.name        = o["name"].GetString();
    s.driver      = o["driver"].GetString("sqlite");
    s.database    = o["database"].GetString();
    s.host        = o["host"].GetString();
    s.port        = static_cast<int>(o["port"].GetInteger(0));
    s.user        = o["user"].GetString();
    s.hasPassword = o["hasPassword"].GetBoolean(false);
    s.tls         = TlsFromName(o["tls"].GetString("verify-full"));
    s.readOnly    = o["readOnly"].GetBoolean(false);
    s.production  = o["production"].GetBoolean(false);
    s.autoCommit  = o["autoCommit"].GetBoolean(false);
    return s;
}

} // namespace

std::string RegisteredName(const ConnectionSpec& spec) {
    return "ultradbadmin." + spec.id;
}

std::string ConsoleName(const ConnectionSpec& spec) {
    return RegisteredName(spec) + ".console";
}

std::string Describe(const ConnectionSpec& spec) {
    if (spec.IsSqlite()) {
        const std::string file = PathToUtf8(PathFromUtf8(spec.database).filename());
        return file.empty() ? spec.database : file;
    }
    std::string text;
    if (!spec.user.empty()) text += spec.user + "@";
    text += spec.host.empty() ? std::string("localhost") : spec.host;
    if (spec.port > 0) text += ":" + std::to_string(spec.port);
    text += "/" + spec.database;
    return text;
}

ConnectionStore::ConnectionStore() = default;
ConnectionStore::~ConnectionStore() = default;

bool ConnectionStore::Open(const std::string& directory, std::string& error) {
    namespace fs = std::filesystem;
    file_ = PathToUtf8(PathFromUtf8(directory) / kFileName);
    specs_.clear();
    std::error_code ec;
    if (fs::exists(PathFromUtf8(file_), ec)) {
        UltraCanvas::JSONParseResult parsed;
        const JSONValue root = UltraCanvas::JSON::ParseFile(file_, &parsed);
        if (!parsed.errorMessage.empty()) {
            error = "cannot read " + file_ + ": " + parsed.errorMessage;
            return false;
        }
        for (const JSONValue& item : root["connections"].GetElements()) {
            ConnectionSpec spec = FromJson(item);
            if (!spec.id.empty()) specs_.push_back(std::move(spec));
        }
    }
    // The vault: an encrypted file in a directory of its own, unlocked by a
    // device key next to it (no prompt), as UltraMail and UltraPress keep theirs.
    vault_ = std::make_unique<UltraVault::DeviceKeyVault>(
        PathToUtf8(PathFromUtf8(directory) / "vault"),
        UltraVault::DeviceKeyVaultProfile{"dbadmin.vault", kKeyPrefix});
    if (!vault_->TryAutoUnlock()) {
        error = "cannot open the password vault: " +
                UltraVault::DeviceKeyVault::DescribeUnlockStatus(vault_->GetLastUnlockStatus());
        return false;
    }
    return true;
}

void ConnectionStore::OpenInMemory() {
    file_.clear();
    specs_.clear();
    vault_.reset();
    memoryVault_ = true;
    UltraVault::Initialize();   // the memory backend, unless the process already has one
}

const ConnectionSpec* ConnectionStore::Find(const std::string& id) const {
    for (const auto& s : specs_) if (s.id == id) return &s;
    return nullptr;
}

std::string ConnectionStore::NewId() {
    std::random_device device;
    std::mt19937_64 generator((static_cast<uint64_t>(device()) << 32) ^ device());
    char text[24];
    std::snprintf(text, sizeof(text), "c%016llx",
                  static_cast<unsigned long long>(generator()));
    return text;
}

std::string ConnectionStore::Put(ConnectionSpec spec, const std::string& password,
                                 bool clearPassword, std::string& error) {
    if (spec.id.empty()) spec.id = NewId();
    const std::string key = kKeyPrefix + spec.id;
    if (!password.empty()) {
        const bool stored = vault_ ? vault_->Store(spec.id, password)
                                   : UltraVault::Put(key, UltraVault::SecretValue::FromString(password)).IsOk();
        if (!stored) {
            error = "the password could not be stored in the vault";
            return std::string();
        }
        spec.hasPassword = true;
    } else if (clearPassword) {
        if (vault_) vault_->Remove(spec.id);
        else UltraVault::Delete(key);
        spec.hasPassword = false;
    }
    auto it = std::find_if(specs_.begin(), specs_.end(),
                           [&](const ConnectionSpec& s) { return s.id == spec.id; });
    if (it != specs_.end()) *it = spec;
    else specs_.push_back(spec);
    if (!Save(error)) return std::string();
    return spec.id;
}

bool ConnectionStore::Remove(const std::string& id, std::string& error) {
    auto it = std::find_if(specs_.begin(), specs_.end(),
                           [&](const ConnectionSpec& s) { return s.id == id; });
    if (it == specs_.end()) return true;
    if (vault_) vault_->Remove(id);
    else UltraVault::Delete(kKeyPrefix + id);
    UltraDb_CloseConnection(RegisteredName(*it));
    UltraDb_CloseConnection(ConsoleName(*it));
    specs_.erase(it);
    return Save(error);
}

namespace {

// One session of `spec`, registered under `name`.
UltraDbResult RegisterSession(const ConnectionSpec& spec, const std::string& name) {
    UltraDbConnectionConfig cfg;
    cfg.name     = name;
    cfg.driver   = spec.driver;
    cfg.database = spec.database;
    cfg.host     = spec.host;
    cfg.port     = spec.port;
    cfg.user     = spec.user;
    cfg.tls      = spec.tls;
    cfg.readOnly = spec.readOnly;
    // Only the vault key travels; the driver reads the password itself.
    if (spec.hasPassword) cfg.credentials = std::string("vault:") + kKeyPrefix + spec.id;
    return UltraDb_RegisterConnection(cfg);
}

} // namespace

UltraDbResult ConnectionStore::Register(const ConnectionSpec& spec) const {
    UltraDbResult result = RegisterSession(spec, RegisteredName(spec));
    if (!result) return result;
    return RegisterSession(spec, ConsoleName(spec));
}

bool ConnectionStore::Save(std::string& error) const {
    if (file_.empty()) return true;   // in memory
    JSONValue root = JSONValue::MakeObject();
    root.Set("format", kFileFormat);
    JSONValue list = JSONValue::MakeArray();
    for (const auto& s : specs_) list.Append(ToJson(s));
    root.Set("connections", std::move(list));
    UltraCanvas::JSONSerializeOptions options;
    options.pretty = true;
    // Written beside and renamed over, so a crash mid-write leaves the old list.
    const std::string temp = file_ + ".tmp";
    if (!UltraCanvas::JSON::SerializeToFile(temp, root, options)) {
        error = "cannot write " + temp;
        return false;
    }
    std::error_code ec;
    std::filesystem::rename(PathFromUtf8(temp), PathFromUtf8(file_), ec);
    if (ec) {
        error = "cannot replace " + file_ + ": " + ec.message();
        return false;
    }
    return true;
}

} // namespace UltraDBAdmin
