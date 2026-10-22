// Plugins/UltraIM/matrix/MatrixOlm.cpp
// Matrix provider - C++ owners of vodozemac's Olm and Megolm objects
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "MatrixOlm.h"

#include "DataFormats/UltraCanvasJSON.h"

#include <utility>

#ifdef ULTRAIM_HAVE_OLM
#include "ultraim_olm.h"
#else
// Built without the Rust shim: the same calls, every one failing, so the
// code below is the same either way.
namespace {
const char* uim_olm_version() { return ""; }
const char* uim_olm_last_error() { return "built without vodozemac (no Rust toolchain)"; }
void uim_olm_string_free(char*) {}
UimOlmAccount* uim_olm_account_new() { return nullptr; }
UimOlmAccount* uim_olm_account_unpickle(const char*, const uint8_t*) { return nullptr; }
char* uim_olm_account_pickle(const UimOlmAccount*, const uint8_t*) { return nullptr; }
void uim_olm_account_free(UimOlmAccount*) {}
char* uim_olm_account_curve25519(const UimOlmAccount*) { return nullptr; }
char* uim_olm_account_ed25519(const UimOlmAccount*) { return nullptr; }
char* uim_olm_account_sign(const UimOlmAccount*, const uint8_t*, size_t) { return nullptr; }
size_t uim_olm_account_max_one_time_keys(const UimOlmAccount*) { return 0; }
void uim_olm_account_generate_one_time_keys(UimOlmAccount*, size_t) {}
char* uim_olm_account_one_time_keys(const UimOlmAccount*) { return nullptr; }
void uim_olm_account_generate_fallback_key(UimOlmAccount*) {}
char* uim_olm_account_fallback_key(const UimOlmAccount*) { return nullptr; }
void uim_olm_account_mark_keys_as_published(UimOlmAccount*) {}
UimOlmSession* uim_olm_account_outbound_session(const UimOlmAccount*, const char*, const char*) { return nullptr; }
UimOlmSession* uim_olm_account_inbound_session(UimOlmAccount*, const char*, const char*, char**) { return nullptr; }
UimOlmSession* uim_olm_session_unpickle(const char*, const uint8_t*) { return nullptr; }
char* uim_olm_session_pickle(const UimOlmSession*, const uint8_t*) { return nullptr; }
void uim_olm_session_free(UimOlmSession*) {}
char* uim_olm_session_id(const UimOlmSession*) { return nullptr; }
int uim_olm_session_matches(const UimOlmSession*, const char*) { return -1; }
int uim_olm_session_has_received_message(const UimOlmSession*) { return 0; }
int uim_olm_session_encrypt(UimOlmSession*, const uint8_t*, size_t, char**) { return -1; }
char* uim_olm_session_decrypt(UimOlmSession*, int, const char*) { return nullptr; }
UimMegolmOutbound* uim_megolm_outbound_new() { return nullptr; }
UimMegolmOutbound* uim_megolm_outbound_unpickle(const char*, const uint8_t*) { return nullptr; }
char* uim_megolm_outbound_pickle(const UimMegolmOutbound*, const uint8_t*) { return nullptr; }
void uim_megolm_outbound_free(UimMegolmOutbound*) {}
char* uim_megolm_outbound_id(const UimMegolmOutbound*) { return nullptr; }
char* uim_megolm_outbound_session_key(const UimMegolmOutbound*) { return nullptr; }
uint32_t uim_megolm_outbound_message_index(const UimMegolmOutbound*) { return 0; }
char* uim_megolm_outbound_encrypt(UimMegolmOutbound*, const uint8_t*, size_t) { return nullptr; }
UimMegolmInbound* uim_megolm_inbound_new(const char*) { return nullptr; }
UimMegolmInbound* uim_megolm_inbound_import(const char*) { return nullptr; }
UimMegolmInbound* uim_megolm_inbound_unpickle(const char*, const uint8_t*) { return nullptr; }
char* uim_megolm_inbound_pickle(const UimMegolmInbound*, const uint8_t*) { return nullptr; }
void uim_megolm_inbound_free(UimMegolmInbound*) {}
char* uim_megolm_inbound_id(const UimMegolmInbound*) { return nullptr; }
uint32_t uim_megolm_inbound_first_known_index(const UimMegolmInbound*) { return 0; }
char* uim_megolm_inbound_export_at(UimMegolmInbound*, uint32_t) { return nullptr; }
char* uim_megolm_inbound_decrypt(UimMegolmInbound*, const char*, uint32_t*) { return nullptr; }
int uim_olm_ed25519_verify(const char*, const uint8_t*, size_t, const char*) { return 0; }
} // namespace
#endif

namespace UltraIMMatrix {

namespace {

// Takes a string the shim returned ("" for NULL) and frees it.
std::string Take(char* text) {
    if (!text) return std::string();
    std::string out(text);
    uim_olm_string_free(text);
    return out;
}

const uint8_t* Bytes(const std::string& s) {
    return reinterpret_cast<const uint8_t*>(s.data());
}

std::map<std::string, std::string> KeyMap(char* json) {
    std::map<std::string, std::string> out;
    const std::string text = Take(json);
    if (text.empty()) return out;
    UltraCanvas::JSONParseResult parsed;
    const UltraCanvas::JSONValue keys = UltraCanvas::JSON::Parse(text, &parsed);
    for (const auto& member : keys.GetMembers()) out[member.first] = member.second.GetString();
    return out;
}

} // namespace

bool OlmAvailable() {
#ifdef ULTRAIM_HAVE_OLM
    return true;
#else
    return false;
#endif
}

std::string OlmVersion() {
    return uim_olm_version();
}

std::string OlmLastError() {
    return uim_olm_last_error();
}

bool OlmVerifyEd25519(const std::string& key, const std::string& message, const std::string& signature) {
    return uim_olm_ed25519_verify(key.c_str(), Bytes(message), message.size(), signature.c_str()) == 1;
}

// ---- account -------------------------------------------------------------------

OlmAccount::OlmAccount() = default;

OlmAccount::~OlmAccount() {
    uim_olm_account_free(account_);
}

OlmAccount::OlmAccount(OlmAccount&& other) noexcept : account_(std::exchange(other.account_, nullptr)) {}

OlmAccount& OlmAccount::operator=(OlmAccount&& other) noexcept {
    if (this != &other) {
        uim_olm_account_free(account_);
        account_ = std::exchange(other.account_, nullptr);
    }
    return *this;
}

bool OlmAccount::Create() {
    uim_olm_account_free(account_);
    account_ = uim_olm_account_new();
    return account_ != nullptr;
}

bool OlmAccount::Unpickle(const std::string& pickle, const PickleKey& key) {
    uim_olm_account_free(account_);
    account_ = uim_olm_account_unpickle(pickle.c_str(), key.data());
    return account_ != nullptr;
}

std::string OlmAccount::Pickle(const PickleKey& key) const {
    return account_ ? Take(uim_olm_account_pickle(account_, key.data())) : std::string();
}

std::string OlmAccount::Curve25519() const {
    return account_ ? Take(uim_olm_account_curve25519(account_)) : std::string();
}

std::string OlmAccount::Ed25519() const {
    return account_ ? Take(uim_olm_account_ed25519(account_)) : std::string();
}

std::string OlmAccount::Sign(const std::string& message) const {
    return account_ ? Take(uim_olm_account_sign(account_, Bytes(message), message.size())) : std::string();
}

size_t OlmAccount::MaxOneTimeKeys() const {
    return account_ ? uim_olm_account_max_one_time_keys(account_) : 0;
}

void OlmAccount::GenerateOneTimeKeys(size_t count) {
    if (account_) uim_olm_account_generate_one_time_keys(account_, count);
}

std::map<std::string, std::string> OlmAccount::OneTimeKeys() const {
    return account_ ? KeyMap(uim_olm_account_one_time_keys(account_)) : std::map<std::string, std::string>();
}

void OlmAccount::GenerateFallbackKey() {
    if (account_) uim_olm_account_generate_fallback_key(account_);
}

std::map<std::string, std::string> OlmAccount::FallbackKey() const {
    return account_ ? KeyMap(uim_olm_account_fallback_key(account_)) : std::map<std::string, std::string>();
}

void OlmAccount::MarkKeysAsPublished() {
    if (account_) uim_olm_account_mark_keys_as_published(account_);
}

OlmSession OlmAccount::OutboundSession(const std::string& theirIdentityKey, const std::string& theirOneTimeKey) const {
    if (!account_) return OlmSession();
    return OlmSession(uim_olm_account_outbound_session(account_, theirIdentityKey.c_str(), theirOneTimeKey.c_str()));
}

OlmSession OlmAccount::InboundSession(const std::string& theirIdentityKey, const std::string& preKeyMessage,
                                      std::string& plaintext) {
    plaintext.clear();
    if (!account_) return OlmSession();
    char* text = nullptr;
    UimOlmSession* session =
        uim_olm_account_inbound_session(account_, theirIdentityKey.c_str(), preKeyMessage.c_str(), &text);
    plaintext = Take(text);
    return OlmSession(session);
}

// ---- olm session -----------------------------------------------------------------

OlmSession::~OlmSession() {
    uim_olm_session_free(session_);
}

OlmSession::OlmSession(OlmSession&& other) noexcept : session_(std::exchange(other.session_, nullptr)) {}

OlmSession& OlmSession::operator=(OlmSession&& other) noexcept {
    if (this != &other) {
        uim_olm_session_free(session_);
        session_ = std::exchange(other.session_, nullptr);
    }
    return *this;
}

bool OlmSession::Unpickle(const std::string& pickle, const PickleKey& key) {
    uim_olm_session_free(session_);
    session_ = uim_olm_session_unpickle(pickle.c_str(), key.data());
    return session_ != nullptr;
}

std::string OlmSession::Pickle(const PickleKey& key) const {
    return session_ ? Take(uim_olm_session_pickle(session_, key.data())) : std::string();
}

std::string OlmSession::Id() const {
    return session_ ? Take(uim_olm_session_id(session_)) : std::string();
}

bool OlmSession::Matches(const std::string& preKeyMessage) const {
    return session_ && uim_olm_session_matches(session_, preKeyMessage.c_str()) == 1;
}

bool OlmSession::HasReceivedMessage() const {
    return session_ && uim_olm_session_has_received_message(session_) == 1;
}

int OlmSession::Encrypt(const std::string& plaintext, std::string& body) {
    body.clear();
    if (!session_) return -1;
    char* text = nullptr;
    const int type = uim_olm_session_encrypt(session_, Bytes(plaintext), plaintext.size(), &text);
    body = Take(text);
    return body.empty() ? -1 : type;
}

bool OlmSession::Decrypt(int messageType, const std::string& body, std::string& plaintext) {
    plaintext.clear();
    if (!session_) return false;
    char* text = uim_olm_session_decrypt(session_, messageType, body.c_str());
    if (!text) return false;
    plaintext = Take(text);
    return true;
}

// ---- megolm outbound ---------------------------------------------------------------

MegolmOutbound::~MegolmOutbound() {
    uim_megolm_outbound_free(session_);
}

MegolmOutbound::MegolmOutbound(MegolmOutbound&& other) noexcept : session_(std::exchange(other.session_, nullptr)) {}

MegolmOutbound& MegolmOutbound::operator=(MegolmOutbound&& other) noexcept {
    if (this != &other) {
        uim_megolm_outbound_free(session_);
        session_ = std::exchange(other.session_, nullptr);
    }
    return *this;
}

bool MegolmOutbound::Create() {
    uim_megolm_outbound_free(session_);
    session_ = uim_megolm_outbound_new();
    return session_ != nullptr;
}

bool MegolmOutbound::Unpickle(const std::string& pickle, const PickleKey& key) {
    uim_megolm_outbound_free(session_);
    session_ = uim_megolm_outbound_unpickle(pickle.c_str(), key.data());
    return session_ != nullptr;
}

std::string MegolmOutbound::Pickle(const PickleKey& key) const {
    return session_ ? Take(uim_megolm_outbound_pickle(session_, key.data())) : std::string();
}

std::string MegolmOutbound::Id() const {
    return session_ ? Take(uim_megolm_outbound_id(session_)) : std::string();
}

std::string MegolmOutbound::SessionKey() const {
    return session_ ? Take(uim_megolm_outbound_session_key(session_)) : std::string();
}

uint32_t MegolmOutbound::MessageIndex() const {
    return session_ ? uim_megolm_outbound_message_index(session_) : 0;
}

std::string MegolmOutbound::Encrypt(const std::string& plaintext) {
    return session_ ? Take(uim_megolm_outbound_encrypt(session_, Bytes(plaintext), plaintext.size())) : std::string();
}

// ---- megolm inbound ----------------------------------------------------------------

MegolmInbound::~MegolmInbound() {
    uim_megolm_inbound_free(session_);
}

MegolmInbound::MegolmInbound(MegolmInbound&& other) noexcept : session_(std::exchange(other.session_, nullptr)) {}

MegolmInbound& MegolmInbound::operator=(MegolmInbound&& other) noexcept {
    if (this != &other) {
        uim_megolm_inbound_free(session_);
        session_ = std::exchange(other.session_, nullptr);
    }
    return *this;
}

bool MegolmInbound::Create(const std::string& sessionKey) {
    uim_megolm_inbound_free(session_);
    session_ = uim_megolm_inbound_new(sessionKey.c_str());
    return session_ != nullptr;
}

bool MegolmInbound::Import(const std::string& exportedKey) {
    uim_megolm_inbound_free(session_);
    session_ = uim_megolm_inbound_import(exportedKey.c_str());
    return session_ != nullptr;
}

bool MegolmInbound::Unpickle(const std::string& pickle, const PickleKey& key) {
    uim_megolm_inbound_free(session_);
    session_ = uim_megolm_inbound_unpickle(pickle.c_str(), key.data());
    return session_ != nullptr;
}

std::string MegolmInbound::Pickle(const PickleKey& key) const {
    return session_ ? Take(uim_megolm_inbound_pickle(session_, key.data())) : std::string();
}

std::string MegolmInbound::Id() const {
    return session_ ? Take(uim_megolm_inbound_id(session_)) : std::string();
}

uint32_t MegolmInbound::FirstKnownIndex() const {
    return session_ ? uim_megolm_inbound_first_known_index(session_) : 0;
}

std::string MegolmInbound::ExportAt(uint32_t index) {
    return session_ ? Take(uim_megolm_inbound_export_at(session_, index)) : std::string();
}

bool MegolmInbound::Decrypt(const std::string& ciphertext, std::string& plaintext, uint32_t& messageIndex) {
    plaintext.clear();
    if (!session_) return false;
    char* text = uim_megolm_inbound_decrypt(session_, ciphertext.c_str(), &messageIndex);
    if (!text) return false;
    plaintext = Take(text);
    return true;
}

} // namespace UltraIMMatrix
