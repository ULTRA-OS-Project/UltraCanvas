// Apps/UltraStore/core/UltraStoreTypes.h
// The words the store core is written in: the target a package is built for
// (os / arch / variants), versions and how they compare, localized text, and
// Result<T>, which every fallible call returns - an error code and a message
// a person can read, never an exception across the core boundary.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace UltraStore {

// ===== TARGETS =====
// The systems the admin backend offers per app: ULTRA OS, Windows, macOS,
// Linux, iOS, Android and the browser (WASM). The store runs on the first
// four today; the other three are catalog targets the admin can fill in
// now, served to those platforms' stores later.
enum class OS   { Linux, Windows, MacOS, UltraOS, IOS, Android, Wasm, Unknown };
// The processors: x86, ARM (64-bit and the 32-bit Raspberry Pi line) and
// RISC-V, plus universal (a macOS fat binary), any (no native code) and
// wasm32 (the browser). The admin backend picks from these per package.
enum class Arch { X86_64, Arm64, Armv7, Riscv64, Wasm32, Universal, Any, Unknown };

// "linux" / "windows" / "macos" / "ultraos" / "ios" / "android" / "wasm";
// Unknown for anything else.
OS          ParseOS(std::string_view text);
std::string OSName(OS os);
// "x86_64" / "arm64" / "armv7" / "riscv64" / "wasm32" / "universal" / "any".
Arch        ParseArch(std::string_view text);
std::string ArchName(Arch arch);

struct Target {
    OS os = OS::Unknown;
    Arch arch = Arch::Unknown;
    std::vector<std::string> variants;   // "glibc-2.31", "rpi", "min-os=11.0", ...

    // "linux/x86_64", "macos/arm64 (min-os=11.0)".
    std::string Describe() const;
};

// ===== VERSIONS =====
// Dotted numbers, compared number by number ("0.9.400" > "0.9.41"); a part
// that is not a number compares as text after the numbers; a "-pre" suffix
// makes a version lower than the same version without it, as SemVer does.
int  CompareVersions(std::string_view a, std::string_view b);
bool VersionLess(std::string_view a, std::string_view b);
bool VersionEqual(std::string_view a, std::string_view b);

// ===== LOCALIZED TEXT =====
// {"en": "...", "de": "..."}: the text in a language, falling back to English,
// then to any entry, then to "".
struct LocalizedText {
    std::map<std::string, std::string> byLanguage;

    std::string Text(const std::string& language) const;
    bool Empty() const { return byLanguage.empty(); }
};

// ===== RESULTS =====
enum class ErrorCode {
    Ok,              // no error (not "None": an X11 macro)
    InvalidArgument,
    ParseError,          // the catalog is not the JSON the schema describes
    SchemaTooNew,        // the catalog needs a newer UltraStore
    SignatureInvalid,    // the catalog's signature does not check out
    SequenceRollback,    // the catalog is older than the one already seen
    NotFound,            // no such app, component or release
    NotAvailable,        // no package for this machine
    DependencyError,     // a dependency is missing, has no package, or cycles
    NetworkError,
    IoError,
    CryptoUnavailable,   // UltraCrypt has no backend here
    Internal
};
std::string ErrorCodeName(ErrorCode code);

struct Error {
    ErrorCode code = ErrorCode::Ok;
    std::string message;       // for a person: what went wrong, in words
    std::string detail;        // for a log: the technical reason, may be empty
};

template <typename T>
struct Result {
    std::optional<T> value;
    Error error;

    bool Ok() const { return !value ? false : error.code == ErrorCode::Ok; }
    explicit operator bool() const { return Ok(); }
    const T& Value() const { return *value; }
    T& Value() { return *value; }

    static Result Succeed(T v) { Result r; r.value = std::move(v); return r; }
    static Result Failure(ErrorCode code, std::string message, std::string detail = {}) {
        Result r; r.error = Error{code, std::move(message), std::move(detail)}; return r;
    }
};

template <>
struct Result<void> {
    Error error;
    bool ok = false;

    bool Ok() const { return ok; }
    explicit operator bool() const { return ok; }

    static Result Succeed() { Result r; r.ok = true; return r; }
    static Result Failure(ErrorCode code, std::string message, std::string detail = {}) {
        Result r; r.error = Error{code, std::move(message), std::move(detail)}; return r;
    }
};

// Lower-case hex of bytes, and back; the second returns false for a string
// that is not hex or has an odd length.
std::string ToHex(const std::vector<uint8_t>& bytes);
bool        FromHex(std::string_view hex, std::vector<uint8_t>& out);

} // namespace UltraStore
