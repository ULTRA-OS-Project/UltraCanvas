// Apps/UltraDBAdmin/engine/DbAdminConnections.cpp
// Version: 0.5.0 - relay connections (UltraDatabaseRelay.h); without a vault, secrets in memory
// Version: 0.4.0 - SSH tunnels: the bastion in the list, its secrets in the vault
// Version: 0.3.0 - EngineOf
// Version: 0.2.0 - auto-commit per connection
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminConnections.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasPathUtf8.h"
#if !defined(ULTRADBADMIN_NO_VAULT)
#include <UltraVault/UltraVault.h>
#include <UltraVault/UltraVaultDeviceKeyVault.h>
#endif

#include <algorithm>
#include <map>
#include <mutex>
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
    if (s.IsRelay()) o.Set("relayEngine", s.relayEngine);
    if (s.UsesTunnel()) {
        o.Set("sshHost", s.sshHost);
        o.Set("sshPort", s.sshPort);
        o.Set("sshUser", s.sshUser);
        o.Set("sshAuth", s.sshAuth);
        o.Set("hasSshSecret", s.hasSshSecret);
        o.Set("hasSshPassphrase", s.hasSshPassphrase);
        o.Set("sshHostKey", s.sshHostKey);
    }
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
    s.sshHost          = o["sshHost"].GetString();
    s.sshPort          = static_cast<int>(o["sshPort"].GetInteger(0));
    s.sshUser          = o["sshUser"].GetString();
    s.sshAuth          = o["sshAuth"].GetString("agent");
    s.hasSshSecret     = o["hasSshSecret"].GetBoolean(false);
    s.hasSshPassphrase = o["hasSshPassphrase"].GetBoolean(false);
    s.sshHostKey       = o["sshHostKey"].GetString();
    s.relayEngine      = o["relayEngine"].GetString();
    return s;
}

// The vault entries of one connection, after kKeyPrefix: the database
// password under its id, the SSH secrets beside it.
std::string SshSecretId(const std::string& id) { return id + ".ssh"; }
std::string SshPassphraseId(const std::string& id) { return id + ".ssh-passphrase"; }

// The secrets, by their full key (kKeyPrefix + entry): in UltraVault, or -
// built without one - in memory for the session.
#if defined(ULTRADBADMIN_NO_VAULT)
std::mutex& SecretsMutex() { static std::mutex m; return m; }
std::map<std::string, std::string>& Secrets() { static std::map<std::string, std::string> s; return s; }
bool SecretPut(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(SecretsMutex());
    Secrets()[key] = value;
    return true;
}
void SecretDelete(const std::string& key) {
    std::lock_guard<std::mutex> lock(SecretsMutex());
    Secrets().erase(key);
}
bool SecretGet(const std::string& key, std::string& out) {
    std::lock_guard<std::mutex> lock(SecretsMutex());
    const auto it = Secrets().find(key);
    if (it == Secrets().end()) return false;
    out = it->second;
    return true;
}
#else
bool SecretGet(const std::string& key, std::string& out) {
    UltraVault::SecretValue secret;
    if (!UltraVault::Get(key, secret)) return false;
    out.assign(secret.bytes.begin(), secret.bytes.end());
    volatile uint8_t* b = secret.bytes.data();
    for (size_t i = 0; i < secret.bytes.size(); ++i) b[i] = 0;
    return true;
}
#endif

} // namespace

std::string RegisteredName(const ConnectionSpec& spec) {
    return "ultradbadmin." + spec.id;
}

std::string ConsoleName(const ConnectionSpec& spec) {
    return RegisteredName(spec) + ".console";
}

std::string ConnectionIdOf(const std::string& registeredName) {
    static const std::string prefix = "ultradbadmin.";
    static const std::string console = ".console";
    if (registeredName.rfind(prefix, 0) != 0) return std::string();
    std::string id = registeredName.substr(prefix.size());
    if (id.size() > console.size() && id.compare(id.size() - console.size(), console.size(), console) == 0)
        id.resize(id.size() - console.size());
    return id;
}

SqlEngine EngineOf(const ConnectionSpec& spec) {
    return spec.IsSqlite() ? SqlEngine::Sqlite : spec.IsMysql() ? SqlEngine::MySql : SqlEngine::Postgres;
}

std::string Describe(const ConnectionSpec& spec) {
    if (spec.IsRelay()) return spec.database + " on " + spec.host;
    if (spec.IsSqlite()) {
        const std::string file = PathToUtf8(PathFromUtf8(spec.database).filename());
        return file.empty() ? spec.database : file;
    }
    std::string text;
    if (!spec.user.empty()) text += spec.user + "@";
    text += spec.host.empty() ? std::string("localhost") : spec.host;
    if (spec.port > 0) text += ":" + std::to_string(spec.port);
    text += "/" + spec.database;
    if (spec.UsesTunnel())
        text += " via " + (spec.sshUser.empty() ? std::string() : spec.sshUser + "@") + spec.sshHost +
                (spec.sshPort > 0 && spec.sshPort != 22 ? ":" + std::to_string(spec.sshPort) : std::string());
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
#if defined(ULTRADBADMIN_NO_VAULT)
    memoryVault_ = true;
#else
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
#endif
    return true;
}

void ConnectionStore::OpenInMemory() {
    file_.clear();
    specs_.clear();
    memoryVault_ = true;
#if !defined(ULTRADBADMIN_NO_VAULT)
    vault_.reset();
    UltraVault::Initialize();   // the memory backend, unless the process already has one
#endif
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
    SecretChanges secrets;
    secrets.password = password;
    secrets.clearPassword = clearPassword;
    return Put(std::move(spec), secrets, error);
}

std::string ConnectionStore::Put(ConnectionSpec spec, const SecretChanges& secrets, std::string& error) {
    if (spec.id.empty()) spec.id = NewId();
    // One vault entry: stored, removed or left as it is; `has` follows.
    auto change = [&](const std::string& id, const std::string& value, bool clear, bool& has,
                      const char* what) {
        if (!value.empty()) {
#if defined(ULTRADBADMIN_NO_VAULT)
            const bool stored = SecretPut(kKeyPrefix + id, value);
#else
            const bool stored = vault_ ? vault_->Store(id, value)
                                       : UltraVault::Put(kKeyPrefix + id, UltraVault::SecretValue::FromString(value)).IsOk();
#endif
            if (!stored) {
                error = std::string("the ") + what + " could not be stored in the vault";
                return false;
            }
            has = true;
        } else if (clear) {
#if defined(ULTRADBADMIN_NO_VAULT)
            SecretDelete(kKeyPrefix + id);
#else
            if (vault_) vault_->Remove(id);
            else UltraVault::Delete(kKeyPrefix + id);
#endif
            has = false;
        }
        return true;
    };
    // No tunnel: no SSH secrets kept for one.
    const bool tunnel = spec.UsesTunnel();
    if (!change(spec.id, secrets.password, secrets.clearPassword, spec.hasPassword, "password") ||
        !change(SshSecretId(spec.id), tunnel ? secrets.sshSecret : std::string(),
                tunnel ? secrets.clearSshSecret : spec.hasSshSecret, spec.hasSshSecret, "SSH password or key") ||
        !change(SshPassphraseId(spec.id), tunnel ? secrets.sshPassphrase : std::string(),
                tunnel ? secrets.clearSshPassphrase : spec.hasSshPassphrase, spec.hasSshPassphrase,
                "SSH key's passphrase"))
        return std::string();
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
    for (const std::string& entry : {id, SshSecretId(id), SshPassphraseId(id)}) {
#if defined(ULTRADBADMIN_NO_VAULT)
        SecretDelete(kKeyPrefix + entry);
#else
        if (vault_) vault_->Remove(entry);
        else UltraVault::Delete(kKeyPrefix + entry);
#endif
    }
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
    if (spec.IsRelay()) {
        // The relay holds the database's password; the app holds only the
        // token, which goes with the registration (the relay driver reads
        // no vault).
        cfg.driver = "relay";
        cfg.host = spec.host;
        cfg.database = spec.database;
        std::string token;
        if (spec.hasPassword && SecretGet(kKeyPrefix + spec.id, token)) cfg.options["relay_token"] = token;
        return UltraDb_RegisterConnection(cfg);
    }
    cfg.driver   = spec.driver;
    cfg.database = spec.database;
    cfg.host     = spec.host;
    cfg.port     = spec.port;
    cfg.user     = spec.user;
    cfg.tls      = spec.tls;
    cfg.readOnly = spec.readOnly;
    // Only the vault key travels; the driver reads the password itself.
    if (spec.hasPassword) cfg.credentials = std::string("vault:") + kKeyPrefix + spec.id;
    if (spec.UsesTunnel()) {
        cfg.options["ssh_host"] = spec.sshHost;
        if (spec.sshPort > 0) cfg.options["ssh_port"] = std::to_string(spec.sshPort);
        if (!spec.sshUser.empty()) cfg.options["ssh_user"] = spec.sshUser;
        if (spec.hasSshSecret) cfg.options["ssh_credentials"] = std::string("vault:") + kKeyPrefix + SshSecretId(spec.id);
        if (spec.hasSshPassphrase)
            cfg.options["ssh_key_passphrase"] = std::string("vault:") + kKeyPrefix + SshPassphraseId(spec.id);
        if (!spec.sshHostKey.empty()) cfg.options["ssh_host_key"] = spec.sshHostKey;
    }
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
