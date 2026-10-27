// Apps/UltraStore/core/CatalogClient.cpp
// Fetching, verifying and caching the catalog (CatalogClient.h).
// Version: 0.2.0 - the announced keys
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "CatalogClient.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetHttp.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace UltraStore {

namespace {

constexpr const char* kCatalogFile   = "catalog.json";
constexpr const char* kSignatureFile = "catalog.json.sig";
constexpr const char* kStateFile     = "state.json";

std::filesystem::path CachePath(const std::string& cacheDir, const char* name) {
    return UltraCanvas::PathFromUtf8(cacheDir) / name;
}

bool ReadFile(const std::filesystem::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

// Written beside the target and renamed over it, so a reader never sees half
// a file.
bool WriteFileAtomically(const std::filesystem::path& path, const std::string& bytes, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const std::filesystem::path temp = UltraCanvas::PathFromUtf8(UltraCanvas::PathToUtf8(path) + ".tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) { error = "cannot write " + UltraCanvas::PathToUtf8(temp); return false; }
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) { error = "cannot write " + UltraCanvas::PathToUtf8(temp); return false; }
    }
    std::filesystem::remove(path, ec);   // Windows will not rename over a file
    ec.clear();
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        error = "cannot replace " + UltraCanvas::PathToUtf8(path) + ": " + ec.message();
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

std::string JoinUrl(const std::string& base, const char* name) {
    if (base.empty()) return name;
    return base.back() == '/' ? base + name : base + "/" + name;
}

} // namespace

std::string NowIso8601Utc() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

Fetcher UltraNetFetcher() {
    return [](const std::string& url, const std::string& etag) {
        FetchResponse out;
        if (!UltraNet_IsInitialized()) UltraNet_Initialize();
        UltraNetHttpOptions options;
        options.httpsOnlyRedirects = true;
        options.maxReceiveSize = int64_t(64) << 20;   // a catalog is kilobytes; 64 MB is a fault
        if (!etag.empty()) options.headers.Set("If-None-Match", etag);
        UltraNetResponse response;
        const UltraNetResult r = UltraNet_HttpGet(url, response, options);
        out.status = response.statusCode;
        out.etag = response.headers.Get("ETag");
        if (!r && r.code != UltraNetResultCode::HttpError) {
            out.error = r.message.empty() ? "no answer" : r.message;
            return out;
        }
        if (!response.IsComplete()) {
            out.error = response.transferError;
            return out;
        }
        out.body = response.GetBodyAsString();
        return out;
    };
}

CatalogClient::CatalogClient(CatalogClientConfig config, Fetcher fetcher)
    : config_(std::move(config)), fetcher_(std::move(fetcher)) {}

CatalogState CatalogClient::State() const {
    CatalogState state;
    std::string text;
    if (!ReadFile(CachePath(config_.cacheDir, kStateFile), text)) return state;
    UltraCanvas::JSONParseResult parse;
    const UltraCanvas::JSONValue v = UltraCanvas::JSON::Parse(text, &parse);
    if (!parse.success || !v.IsObject()) return state;
    state.sequence = static_cast<uint64_t>(std::max<int64_t>(0, v.Get("sequence").GetInteger(0)));
    state.etag = v.Get("etag").GetString();
    state.fetchedFrom = v.Get("fetchedFrom").GetString();
    state.fetchedAt = v.Get("fetchedAt").GetString();
    for (const UltraCanvas::JSONValue& k : v.Get("announcedKeys").GetElements()) {
        std::vector<uint8_t> bytes;
        if (FromHex(k.GetString(), bytes) && bytes.size() == 32) state.announcedKeys.push_back(ToHex(bytes));
    }
    return state;
}

PublicKeys CatalogClient::KeysTrustedNow() const {
    PublicKeys keys = config_.trustedKeys;
    for (const std::string& hex : State().announcedKeys) {
        std::vector<uint8_t> bytes;
        if (!FromHex(hex, bytes) || bytes.size() != 32) continue;
        if (std::find(keys.begin(), keys.end(), bytes) == keys.end()) keys.push_back(std::move(bytes));
    }
    return keys;
}

bool CatalogClient::WriteCache(const std::string& catalogBytes, const std::vector<uint8_t>& signature,
                               const CatalogState& state, std::string& error) const {
    if (config_.cacheDir.empty()) { error = "no cache folder configured"; return false; }
    if (!WriteFileAtomically(CachePath(config_.cacheDir, kCatalogFile), catalogBytes, error)) return false;
    if (!WriteFileAtomically(CachePath(config_.cacheDir, kSignatureFile), ToHex(signature), error)) return false;
    UltraCanvas::JSONValue v = UltraCanvas::JSONValue::MakeObject();
    v.Set("sequence", static_cast<int64_t>(state.sequence));
    v.Set("etag", state.etag);
    v.Set("fetchedFrom", state.fetchedFrom);
    v.Set("fetchedAt", state.fetchedAt);
    UltraCanvas::JSONValue announced = UltraCanvas::JSONValue::MakeArray();
    for (const std::string& k : state.announcedKeys) announced.Append(k);
    v.Set("announcedKeys", std::move(announced));
    return WriteFileAtomically(CachePath(config_.cacheDir, kStateFile), UltraCanvas::JSON::Serialize(v), error);
}

Result<Catalog> CatalogClient::Accept(const std::string& catalogBytes, const std::vector<uint8_t>& signature,
                                      const std::string& etag, const std::string& baseUrl) {
    // The keys trusted now: the configured ones and what the catalog
    // accepted before this one announced. A key only this catalog names
    // cannot sign it - an announcement comes from a catalog already trusted.
    const Result<void> verified = VerifyCatalogSignature(catalogBytes, signature, KeysTrustedNow());
    if (!verified) return Result<Catalog>::Failure(verified.error.code, verified.error.message, verified.error.detail);
    Result<Catalog> parsed = ParseCatalog(catalogBytes);
    if (!parsed) return parsed;
    const CatalogState previous = State();
    if (parsed.Value().sequence < previous.sequence) {
        return Result<Catalog>::Failure(ErrorCode::SequenceRollback,
                                        "The server offered an older catalog than the one already seen",
                                        "sequence " + std::to_string(parsed.Value().sequence) + " < " +
                                            std::to_string(previous.sequence));
    }
    CatalogState state;
    state.sequence = parsed.Value().sequence;
    state.etag = etag;
    state.fetchedFrom = baseUrl;
    state.fetchedAt = NowIso8601Utc();
    state.announcedKeys = parsed.Value().trustedKeys;   // replaces the last list; empty after a schema 1 catalog
    std::string error;
    if (!WriteCache(catalogBytes, signature, state, error)) {
        return Result<Catalog>::Failure(ErrorCode::IoError, "The catalog could not be kept on disk", error);
    }
    return parsed;
}

Result<Catalog> CatalogClient::Refresh() {
    if (config_.baseUrls.empty()) {
        return Result<Catalog>::Failure(ErrorCode::InvalidArgument, "No catalog server is configured");
    }
    if (!fetcher_) return Result<Catalog>::Failure(ErrorCode::Internal, "No fetcher");
    const CatalogState previous = State();
    std::string lastError;
    for (const std::string& base : config_.baseUrls) {
        // The ETag belongs to the server it came from.
        const std::string etag = previous.fetchedFrom == base ? previous.etag : std::string();
        const FetchResponse catalogResponse = fetcher_(JoinUrl(base, kCatalogFile), etag);
        if (!catalogResponse.error.empty() || catalogResponse.status == 0) {
            lastError = base + ": " + (catalogResponse.error.empty() ? "no answer" : catalogResponse.error);
            continue;
        }
        if (catalogResponse.status == 304 && !etag.empty()) {
            Result<Catalog> cached = LoadCached();
            if (cached) return cached;
            // The cache is gone or bad: fetch afresh.
            const FetchResponse again = fetcher_(JoinUrl(base, kCatalogFile), std::string());
            if (!again.error.empty() || again.status != 200) {
                lastError = base + ": " + (again.error.empty() ? "HTTP " + std::to_string(again.status) : again.error);
                continue;
            }
            const FetchResponse sig = fetcher_(JoinUrl(base, kSignatureFile), std::string());
            std::vector<uint8_t> signature;
            if (!sig.error.empty() || sig.status != 200 || !ParseSignatureFile(sig.body, signature)) {
                lastError = base + ": the catalog's signature could not be fetched";
                continue;
            }
            return Accept(again.body, signature, again.etag, base);
        }
        if (catalogResponse.status != 200) {
            lastError = base + ": HTTP " + std::to_string(catalogResponse.status);
            continue;
        }
        const FetchResponse sig = fetcher_(JoinUrl(base, kSignatureFile), std::string());
        std::vector<uint8_t> signature;
        if (!sig.error.empty() || sig.status != 200) {
            lastError = base + ": the catalog's signature could not be fetched (" +
                        (sig.error.empty() ? "HTTP " + std::to_string(sig.status) : sig.error) + ")";
            continue;
        }
        if (!ParseSignatureFile(sig.body, signature)) {
            lastError = base + ": the catalog's signature file is not a signature";
            continue;
        }
        // A signature or sequence failure is final for this server: another
        // server may hold a good catalog, so go on, but keep the reason.
        Result<Catalog> accepted = Accept(catalogResponse.body, signature, catalogResponse.etag, base);
        if (accepted) return accepted;
        if (accepted.error.code == ErrorCode::SchemaTooNew || accepted.error.code == ErrorCode::IoError) return accepted;
        lastError = base + ": " + accepted.error.message +
                    (accepted.error.detail.empty() ? "" : " (" + accepted.error.detail + ")");
        if (accepted.error.code == ErrorCode::SignatureInvalid || accepted.error.code == ErrorCode::SequenceRollback) {
            // Keep the code: a tampered or rolled-back catalog is worth more
            // than "could not reach" in the message a person sees.
            if (&base == &config_.baseUrls.back()) return accepted;
        }
    }
    return Result<Catalog>::Failure(ErrorCode::NetworkError, "The catalog could not be fetched", lastError);
}

Result<Catalog> CatalogClient::LoadCached() const {
    std::string catalogBytes, signatureText;
    if (!ReadFile(CachePath(config_.cacheDir, kCatalogFile), catalogBytes) ||
        !ReadFile(CachePath(config_.cacheDir, kSignatureFile), signatureText)) {
        return Result<Catalog>::Failure(ErrorCode::NotFound, "No catalog has been fetched yet");
    }
    std::vector<uint8_t> signature;
    if (!ParseSignatureFile(signatureText, signature)) {
        return Result<Catalog>::Failure(ErrorCode::SignatureInvalid, "The cached catalog's signature file is not a signature");
    }
    const Result<void> verified = VerifyCatalogSignature(catalogBytes, signature, KeysTrustedNow());
    if (!verified) return Result<Catalog>::Failure(verified.error.code, verified.error.message, verified.error.detail);
    return ParseCatalog(catalogBytes);
}

} // namespace UltraStore
