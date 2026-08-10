# UltraCrypt — Cryptographic Services for ULTRA OS

**Status:** Design proposal for review. Not yet implemented.
**Version:** 0.1.0 (draft)
**Author:** UltraCanvas Framework / ULTRA OS
**Last Modified:** 2026-08-10

UltraCrypt is the cryptographic module of the ULTRA OS / UltraCanvas
framework: the single place where hashing, message authentication,
authenticated encryption, key derivation and secure random generation are
implemented, so that no application, plugin or sibling module ever has to
write its own.

It follows the same rules as UltraNet and UltraDatabase — clear structure,
names understandable from the name itself, `UltraCryptResult` from every
blocking operation, and a backing library that is completely wrapped so it
can be replaced without affecting callers.

This document specifies **what UltraCrypt must provide and why** (§1–2), the
**backend decision that must be settled before implementation** (§3), and the
**proposed public surface** (§5). It stops short of implementation so the
backend choice can be ratified first — that choice affects dependency policy
on all three host platforms.

---

## 1. Why this module exists

There is currently **no sanctioned way for UltraCanvas code to compute a hash,
an HMAC, derive a key, or encrypt a blob.** Outside vendored third-party
code, only three files in the repository touch a crypto library, and all three
are TLS backends doing TLS.

The result is that every component needing cryptography has independently
hand-rolled one, reached around the rules, or stalled at design stage:

| Consumer | Requires | State today |
|---|---|---|
| **UCD file format v2** ([spec](../../UltraCanvas/UCD-FileFormat-v2.md) §4.3–4.4) | AES-256-GCM, ChaCha20-Poly1305, Argon2id, PBKDF2-HMAC-SHA256, HKDF-SHA256, SHA-256, BLAKE3-256, CSPRNG (file UUID, salts) | Fully specified; **none of the primitives exist**, so the format cannot be implemented as written |
| **UltraCanvasDocument** (UCD v1 encryption) | AES-256, PBKDF2, password verification | `#include <openssl/aes.h>` directly inside a plugin — house-rule violation — and functionally broken (§1.1) |
| **AnchorPoint** | SHA-256 file integrity | Hand-rolled `Apps/AnchorPoint/net/Sha256.h`, whose header states it is a placeholder awaiting "a vetted crypto surface" |
| **UltraVault** ([design](../../../UltraAI/Docs/UltraVault.md)) | AEAD + password KDF for its file-backed fallback backend | Design doc only |
| **UltraDatabase** | At-rest encryption | Listed as a Stage 3 item, unstarted |
| **UltraAuthenticator** ([investigation](../../UltraAuthenticator/UltraAuthenticator-Investigation.md)) | HMAC-SHA-1/256/512, CSPRNG, AEAD, KDF, constant-time compare, secure memory | Blocked |

Six consumers, one missing service. That is the case for building it once.

### 1.1 What the absence already produced

`UltraCanvas/Plugins/Documents/UltraCanvasDocument.cpp` is the cautionary
example. Its `EncryptData` / `DecryptData` are guarded by
`#ifdef ULTRACANVAS_USE_OPENSSL`, **a macro no build file anywhere defines**.
The branch that actually compiles is:

```cpp
// Fallback - no encryption
output = input;
return true;
```

So `Save()` with a password writes a header claiming `AES256` over entirely
plaintext content, and `Load()` accepts *any* password. With the macro defined
it is worse, not better: the line appending the ciphertext is commented out
(output would contain only the IV, destroying the document) and `DecryptData`
never assigns its output — both still returning `true`. Supporting routines
are independently unsound: a single SHA-256 round used as a password hash with
a `std::hash<std::string>` fallback, `std::mt19937` for salt generation, and
PBKDF2 at 10 000 iterations with the salt taken from the IV.

**This file is not in the CMake source list, so it is dormant code and not a
shipping vulnerability** — it must not be reported as one. Its relevance here
is diagnostic: it is exactly what a missing shared API produces. Plausible,
well-intentioned security code that silently does nothing. Before it is ever
added to the build it must be rewritten against UltraCrypt, or its encryption
entry points removed so no caller can believe in them.

---

## 2. Scope

**In scope** — data-at-rest and data-integrity primitives: hashing, HMAC,
authenticated encryption (AEAD), password-based and key-derivation functions,
cryptographically secure random generation, constant-time comparison, and
secure (zeroizing) memory.

**Out of scope:**

- **Transport security.** TLS stays in UltraNet, per-platform and OS-native.
  UltraCrypt never opens a socket and never validates a certificate.
- **Credential storage and policy.** That is UltraVault's job; UltraVault
  *consumes* UltraCrypt for its file-backed fallback backend.
- **Public-key cryptography** (signatures, key agreement, X.509). No consumer
  needs it yet. The surface is arranged so `UltraCrypt_Sign` /
  `UltraCrypt_Verify` can be added later without disturbing anything here.
- **Protocol-level constructions** (TOTP, the UCD envelope, SuperVault's
  request flow). Those live in their consumers and are built *from* these
  primitives.

---

## 3. The backend decision — this needs a ruling before implementation

**Correction to an earlier assumption:** it is *not* true that OpenSSL is
already linked on every platform. `master_dependencies.yaml` states the policy
explicitly — *"UltraNet uses whichever TLS backend libcurl was built against;
we never call OpenSSL directly … OpenSSL is only an explicit dependency on
Linux"* — and `UltraCanvas/CMakeLists.txt:1407` confirms it: `OpenSSL::SSL` /
`OpenSSL::Crypto` are linked under `ULTRACANVAS_PLATFORM STREQUAL "Linux"`
only. Windows links `secur32`/`crypt32` (Schannel); macOS links
`Security.framework` (SecureTransport). The manifest pins OpenSSL at
`min_version: "1.1.1"` — which predates OpenSSL's Argon2 support (3.2+).

So there is no free ride. Three viable options:

### Option A — Per-platform native backends

Mirror the existing `ultranet_tls_platform::` pattern
(`core/UltraNet/UltraNetTlsImpl.h` + one implementation per OS): OpenSSL on
Linux, CNG / BCrypt on Windows, CommonCrypto / CryptoKit on macOS.

*For:* zero new dependencies; consistent with the "OS-native wherever the OS
ships it" philosophy; smallest binaries.

*Against:* **the algorithm matrix does not line up.** Argon2id is absent from
CNG, from CommonCrypto, and from OpenSSL 1.1.1; ChaCha20-Poly1305 is absent
from CommonCrypto. Worse, this is a *correctness* hazard rather than merely an
effort one: UCD v2 files must open on every platform, so three independent
implementations of the same envelope become three chances for a
cross-platform-incompatible file. Three separate crypto code paths also means
three times the review burden on the code least tolerant of subtle bugs.

### Option B — One vendored portable library (recommended)

Vendor **libsodium** (ISC licence) under `UltraCanvas/third_party/` and wrap
it exactly as yyjson is wrapped for UltraCanvasJSON.

*For:* one implementation, bit-identical on every platform — the property that
matters most for a file format. Covers nearly the whole requirement set
directly: AES-256-GCM (hardware-accelerated), ChaCha20-Poly1305 and
XChaCha20-Poly1305, Argon2id, HMAC-SHA-256/512, SHA-256/512, BLAKE2b, HKDF, a
CSPRNG that draws from the OS entropy source, `sodium_memcmp` for
constant-time comparison, and — valuable for §5.1 — `sodium_malloc`,
`sodium_mlock` and `sodium_memzero` for guarded, non-swappable, zeroizing
memory. Small, widely audited, packaged on every distro, API-stable for a
decade.

*Against:* a new third-party dependency (requires updating
`Docs/Dependencies.md`, `master_dependencies.yaml` and
`THIRD_PARTY_LICENSES.md`). Two bounded gaps: libsodium deliberately omits
**SHA-1**, which TOTP needs for issuer compatibility, and has no **BLAKE3**
(it offers BLAKE2b). Both are addressable — see §3.1.

### Option C — OpenSSL 3.2+ everywhere

*For:* one implementation; already a known quantity on Linux; covers
everything except BLAKE3.

*Against:* directly contradicts the documented Windows/macOS policy, adds a
large runtime to ship and sign on two platforms that currently need none, and
raises the Linux floor from 1.1.1 to 3.2 (newer than several supported LTS
distros ship).

### Recommendation

**Option B.** The decisive argument is not convenience but compatibility: for
transport security, OS-native backends are right, because the OS owns the
trust store and the cipher policy. For *data at rest* the requirement is
inverted — a document encrypted on Linux must decrypt byte-for-byte on Windows
and macOS, which argues for exactly one implementation everywhere. UltraNet's
TLS policy is untouched by this choice; the two concerns stay cleanly
separated.

### 3.1 Handling the two gaps

- **SHA-1** is needed only for `HMAC-SHA-1` in TOTP (where HMAC does not
  inherit SHA-1's collision weakness, and issuer compatibility requires it).
  It must *not* be offered as a general-purpose digest. Proposal: vendor a
  ~150-line SHA-1 compression function used solely by the HMAC path, and mark
  `UltraCryptHashAlgorithm::SHA1` as legacy in the header, rejected by
  `UltraCrypt_Hash` unless `UltraCryptHashOptions::allowLegacy` is set.
- **BLAKE3-256** is `hash algorithm = 1` in UCD v2's `SVLT` record, and
  optional — `0` = SHA-256 is the default. Proposal: ship SHA-256 only in
  Stage 1, have writers emit algorithm `0`, and treat BLAKE3 as a later
  addition (vendoring the reference implementation, CC0/Apache-2.0) if a real
  need appears. `UltraCrypt_IsHashAvailable` lets readers degrade honestly in
  the meantime.

---

## 4. Placement, naming and build target

UltraCrypt follows the **UltraNet / UltraDatabase layout** exactly, being a
sibling module of the same generation:

| Aspect | Convention followed |
|---|---|
| Sources | `UltraCanvas/{include,core}/UltraCrypt/` |
| Public header | `UltraCrypt/UltraCryptCore.h` |
| Documentation | `Docs/Modules/UltraCrypt/README.md` (this file) |
| Registry entry | `Masterfile_modules.md` §7 |
| Namespace | **None** — matching UltraNet and UltraDatabase, which place their surface in the global namespace behind a module prefix |
| Functions | `UltraCrypt_<Verb><Noun>` free functions |
| Types | `UltraCrypt`-prefixed (`UltraCryptResult`, `UltraCryptSecureBuffer`, …) |
| Blocking ops | Return `UltraCryptResult` with `operator bool()` |

Because UltraVault, UltraDatabase and headless tools also need UltraCrypt and
must **not** pull in the UI layer, its sources are deliberately free of any UI
dependency — they include nothing from UltraCanvas beyond fixed-width integer
types — and are compiled into a small standalone static target, `UltraCrypt`,
that the framework, the sibling modules and applications all link. UltraCanvas
core links it for the UCD v2 format.

```cpp
#include "UltraCrypt/UltraCryptCore.h"
```

---

## 5. Proposed public surface

Conventions: PascalCase identifiers; module-prefixed free functions and types;
no backend type in any public header; a structured result with `operator
bool()` matching `UltraNetResult` / `UltraDbResult`; options carried in structs
so calls stay readable and extensible; **nothing throws**.

### 5.0 Result type and lifecycle

```cpp
enum class UltraCryptResultCode : uint8_t {
    Success,
    NotSupported,          // algorithm not available in this build
    InvalidArgument,
    InvalidKeySize,
    InvalidNonceSize,
    AuthenticationFailed,  // AEAD tag mismatch — ciphertext or AAD was altered
    EntropyFailure,        // CSPRNG unavailable: never fall back to a PRNG
    BackendUnavailable,
    InternalError
};

struct UltraCryptResult {
    UltraCryptResultCode code = UltraCryptResultCode::InternalError;
    bool success = false;
    std::string message;

    operator bool() const { return success; }

    static UltraCryptResult Ok();
    static UltraCryptResult Error(UltraCryptResultCode code,
                                  const std::string& message);
};

// Idempotent and thread-safe; called automatically on first use, but callers
// that care about deterministic initialisation order may call it explicitly.
UltraCryptResult UltraCrypt_Initialize();
void             UltraCrypt_Shutdown();
bool             UltraCrypt_IsAvailable();
std::string      UltraCrypt_GetBackendName();   // e.g. "libsodium 1.0.19"
```

**Error-reporting rule:** `message` is for developers and logs. It must never
contain key material, plaintext, or password-derived data, and it must not
distinguish *why* an AEAD open failed beyond `AuthenticationFailed` — that
distinction is an oracle.

### 5.1 UltraCryptSecureBuffer — the type secrets travel in

Every entry point that touches key material takes or returns an
`UltraCryptSecureBuffer` rather than `std::string` / `std::vector<uint8_t>`,
so secrets cannot be copied by accident, are wiped on destruction, and are
kept out of swap where the OS allows it.

```cpp
class UltraCryptSecureBuffer {
public:
    UltraCryptSecureBuffer();
    explicit UltraCryptSecureBuffer(size_t size);          // zero-filled
    UltraCryptSecureBuffer(const void* data, size_t size);
    ~UltraCryptSecureBuffer();                              // zeroizes, then frees

    // Move-only: copying a secret must be deliberate, never implicit.
    UltraCryptSecureBuffer(UltraCryptSecureBuffer&&) noexcept;
    UltraCryptSecureBuffer& operator=(UltraCryptSecureBuffer&&) noexcept;
    UltraCryptSecureBuffer(const UltraCryptSecureBuffer&) = delete;
    UltraCryptSecureBuffer& operator=(const UltraCryptSecureBuffer&) = delete;
    UltraCryptSecureBuffer Clone() const;                   // explicit copy

    uint8_t*       Data();
    const uint8_t* Data() const;
    size_t         GetSize() const;
    bool           IsEmpty() const;

    void Resize(size_t size);     // grow/shrink, wiping any discarded bytes
    void Clear();                 // zeroize and release

    // Best-effort mlock()/VirtualLock(): keeps pages out of swap. Returns
    // false when the OS refuses (RLIMIT_MEMLOCK) — a hint, never a guarantee.
    bool LockPages();

    // Adopts the bytes of a std::string and wipes the source in place. For
    // bridging from UI text fields, which cannot hand over a secure buffer.
    static UltraCryptSecureBuffer AdoptString(std::string& text);
};

// Compiler-barrier-protected wipe; not elided by optimizers.
void UltraCrypt_SecureZero(void* data, size_t size);

// Length-independent comparison. Use for every MAC / tag / digest check.
bool UltraCrypt_ConstantTimeEquals(const void* a, const void* b, size_t size);
```

### 5.2 Hashing

```cpp
enum class UltraCryptHashAlgorithm : uint8_t {
    SHA1,        // LEGACY: HMAC-SHA-1 (TOTP) only; see allowLegacy below
    SHA256,
    SHA384,
    SHA512,
    BLAKE2b256,
    BLAKE3_256   // UCD v2 SVLT alg 1; may report NotSupported (see §3.1)
};

struct UltraCryptHashOptions {
    bool allowLegacy = false;    // required to use SHA1 as a plain digest
};

class UltraCryptHasher {          // streaming; for files and large payloads
public:
    explicit UltraCryptHasher(UltraCryptHashAlgorithm algorithm,
                              const UltraCryptHashOptions& options =
                                  UltraCryptHashOptions());
    ~UltraCryptHasher();
    UltraCryptHasher(UltraCryptHasher&&) noexcept;
    UltraCryptHasher(const UltraCryptHasher&) = delete;

    void Update(const void* data, size_t size);
    // Finalises and returns the digest; the object must be Reset() to reuse.
    std::vector<uint8_t> Final();
    void Reset();

    size_t GetDigestSize() const;
    bool   IsValid() const;       // false when the algorithm is unavailable
};

size_t UltraCrypt_GetDigestSize(UltraCryptHashAlgorithm algorithm);
bool   UltraCrypt_IsHashAvailable(UltraCryptHashAlgorithm algorithm);

UltraCryptResult UltraCrypt_Hash(
        UltraCryptHashAlgorithm algorithm,
        const void* data, size_t size,
        std::vector<uint8_t>& outDigest,
        const UltraCryptHashOptions& options = UltraCryptHashOptions());

UltraCryptResult UltraCrypt_HashFile(
        UltraCryptHashAlgorithm algorithm,
        const std::string& filePath,
        std::vector<uint8_t>& outDigest);

std::string UltraCrypt_ToHex(const std::vector<uint8_t>& bytes);
bool        UltraCrypt_FromHex(const std::string& hex,
                               std::vector<uint8_t>& outBytes);
```

*Serves:* AnchorPoint integrity, UCD `SVLT` creation hash, UCD section
checksums.

### 5.3 HMAC

```cpp
class UltraCryptHmacHasher {      // streaming, same shape as UltraCryptHasher
public:
    UltraCryptHmacHasher(UltraCryptHashAlgorithm algorithm,
                         const UltraCryptSecureBuffer& key);
    ~UltraCryptHmacHasher();
    void Update(const void* data, size_t size);
    std::vector<uint8_t> Final();
    void Reset();                 // keeps the key
    size_t GetDigestSize() const;
    bool   IsValid() const;
};

// SHA1 is permitted here without allowLegacy: HMAC-SHA-1 remains sound and
// TOTP interoperability requires it.
UltraCryptResult UltraCrypt_Hmac(UltraCryptHashAlgorithm algorithm,
                                 const UltraCryptSecureBuffer& key,
                                 const void* data, size_t size,
                                 std::vector<uint8_t>& outMac);
```

*Serves:* UltraAuthenticator (RFC 4226 / 6238), any future signed-manifest work.

### 5.4 Authenticated encryption (AEAD)

Only AEAD is offered. There is **no raw block-cipher or unauthenticated-mode
surface** — that omission is deliberate and is what prevents the next
`UltraCanvasDocument` from appearing.

```cpp
enum class UltraCryptAeadAlgorithm : uint8_t {
    Aes256Gcm,           // UCD v2 encryption type 1
    ChaCha20Poly1305,    // UCD v2 encryption type 2
    XChaCha20Poly1305    // 192-bit nonce: safe for random nonces at volume
};

struct UltraCryptAeadParams {
    UltraCryptAeadAlgorithm algorithm = UltraCryptAeadAlgorithm::Aes256Gcm;

    // Seal: if empty, a fresh random nonce is generated and written back here
    // — the caller must store it. Supplying a nonce is allowed but then
    // keeping it unique per key is the caller's responsibility.
    // Open: must be set to the nonce used at Seal time.
    std::vector<uint8_t> nonce;

    // Authenticated but not encrypted. UCD writers put the section header
    // here, so a header edit invalidates the tag.
    std::vector<uint8_t> associatedData;
};

size_t UltraCrypt_GetKeySize(UltraCryptAeadAlgorithm algorithm);    // 32 all
size_t UltraCrypt_GetNonceSize(UltraCryptAeadAlgorithm algorithm);  // 12/12/24
size_t UltraCrypt_GetTagSize(UltraCryptAeadAlgorithm algorithm);    // 16
bool   UltraCrypt_IsAeadAvailable(UltraCryptAeadAlgorithm algorithm);

// Ciphertext is plaintext-length + tag size; the tag is appended.
UltraCryptResult UltraCrypt_AeadSeal(const UltraCryptSecureBuffer& key,
                                     UltraCryptAeadParams& params,
                                     const void* plaintext, size_t size,
                                     std::vector<uint8_t>& outCiphertext);

// Returns AuthenticationFailed — and leaves outPlaintext empty — if the
// ciphertext, the AAD or the nonce was altered. Never returns partially
// verified plaintext.
UltraCryptResult UltraCrypt_AeadOpen(const UltraCryptSecureBuffer& key,
                                     const UltraCryptAeadParams& params,
                                     const void* ciphertext, size_t size,
                                     UltraCryptSecureBuffer& outPlaintext);
```

Taking `UltraCryptAeadParams&` by non-const reference in Seal is the deliberate
shape: generating the nonce by default makes nonce reuse — the catastrophic
failure mode of both GCM and Poly1305 — something a caller has to opt *into*
rather than something they forget to avoid.

*Serves:* UCD v2 §4.3 section pipeline, UltraVault file backend,
UltraAuthenticator's store, UltraDatabase at-rest encryption.

### 5.5 Key derivation

```cpp
enum class UltraCryptKdfAlgorithm : uint8_t {
    Argon2id,           // memory-hard; the default and the UCD v2 recommendation
    Pbkdf2HmacSha256    // fallback for interoperability with existing files
};

struct UltraCryptKdfParams {
    UltraCryptKdfAlgorithm algorithm = UltraCryptKdfAlgorithm::Argon2id;

    // If empty, UltraCrypt_DeriveKeyFromPassword generates 16 random bytes
    // and writes them back — the caller stores them (UCD keeps them in SECU).
    std::vector<uint8_t> salt;

    uint32_t iterations   = 0;   // Argon2 passes / PBKDF2 iterations
    uint32_t memoryKiB    = 0;   // Argon2 only
    uint32_t parallelism  = 0;   // Argon2 only
    size_t   outputLength = 32;
};

// Current-guidance cost parameters; these are revisited as hardware moves, so
// stored files must always record the parameters they were written with
// rather than assuming today's defaults.
//   Argon2id         -> 3 passes, 64 MiB, parallelism 1
//   Pbkdf2HmacSha256 -> 600 000 iterations
UltraCryptKdfParams UltraCrypt_RecommendedKdfParams(
        UltraCryptKdfAlgorithm algorithm);

UltraCryptResult UltraCrypt_DeriveKeyFromPassword(
        const UltraCryptSecureBuffer& password,
        UltraCryptKdfParams& params,
        UltraCryptSecureBuffer& outKey);

// HKDF (RFC 5869) — derives subkeys from existing high-entropy key material.
// Not for passwords: it is deliberately fast. UCD v2 §4.4 uses exactly this:
//   HKDF-SHA256(keyMaterial, salt = SECU salt, info = "UCD-SVLT-v1" + UUID)
UltraCryptResult UltraCrypt_DeriveKeyHkdf(
        UltraCryptHashAlgorithm algorithm,
        const UltraCryptSecureBuffer& inputKeyMaterial,
        const std::vector<uint8_t>& salt,
        const std::vector<uint8_t>& info,
        size_t outputLength,
        UltraCryptSecureBuffer& outKey);
```

### 5.6 Random generation

```cpp
// All draw from the OS CSPRNG. On failure they return EntropyFailure and
// leave the output untouched — there is deliberately no PRNG fallback.
UltraCryptResult UltraCrypt_RandomBytes(void* out, size_t size);
UltraCryptResult UltraCrypt_RandomBytes(std::vector<uint8_t>& out, size_t size);
UltraCryptResult UltraCrypt_RandomSecureBuffer(size_t size,
                                               UltraCryptSecureBuffer& out);

// Uniform in [0, bound) without modulo bias.
UltraCryptResult UltraCrypt_RandomUInt32(uint32_t bound, uint32_t& out);

// RFC 4122 version 4, lowercase hyphenated. UCD v2 needs one per file.
UltraCryptResult UltraCrypt_GenerateUuidV4(std::string& out);
```

### 5.7 Companion encodings — placement to be decided

TOTP provisioning needs **Base32** (RFC 4648) and several consumers want
**Base64**. These are encodings, not cryptography, so they arguably belong in
`DataFormats/` alongside UltraCanvasJSON rather than here. They are listed for
completeness because no home exists today and secrets pass through them, which
means the implementations need the same care about intermediate copies:

```cpp
std::string      UltraCrypt_Base64Encode(const void* data, size_t size);
UltraCryptResult UltraCrypt_Base64Decode(const std::string& text,
                                         std::vector<uint8_t>& out);

std::string      UltraCrypt_Base32Encode(const void* data, size_t size,
                                         bool pad = true);
// Strict RFC 4648: rejects non-alphabet characters. Case-insensitive.
UltraCryptResult UltraCrypt_Base32Decode(const std::string& text,
                                         UltraCryptSecureBuffer& out);
```

Note the asymmetry: `UltraCrypt_Base32Decode` yields a secure buffer because
its dominant use is decoding a TOTP seed.

---

## 6. Usage sketches

**UCD v2 section encryption (§4.3 pipeline):**

```cpp
UltraCryptKdfParams kdf =
    UltraCrypt_RecommendedKdfParams(UltraCryptKdfAlgorithm::Argon2id);
UltraCryptSecureBuffer key;
if (!UltraCrypt_DeriveKeyFromPassword(password, kdf, key)) return false;
// kdf.salt / iterations / memoryKiB now hold what must be written to SECU.

UltraCryptAeadParams aead;
aead.algorithm      = UltraCryptAeadAlgorithm::Aes256Gcm;
aead.associatedData = sectionHeaderBytes;   // header tampering breaks the tag
std::vector<uint8_t> ciphertext;
if (!UltraCrypt_AeadSeal(key, aead, payload.data(), payload.size(), ciphertext))
    return false;
// aead.nonce holds the generated nonce — store it with the section.
```

**UCD v2 SuperVault subkey (§4.4 step 6):**

```cpp
std::vector<uint8_t> info(kSvltInfoPrefix.begin(), kSvltInfoPrefix.end());
info.insert(info.end(), fileUuid.begin(), fileUuid.end());

UltraCryptSecureBuffer sectionKey;
UltraCrypt_DeriveKeyHkdf(UltraCryptHashAlgorithm::SHA256, serviceKeyMaterial,
                         kdfSalt, info, 32, sectionKey);
```

**TOTP code (RFC 6238):**

```cpp
uint64_t counter = static_cast<uint64_t>(unixTimeUtc) / period;
uint8_t  message[8];
for (int i = 0; i < 8; ++i)
    message[7 - i] = static_cast<uint8_t>(counter >> (i * 8));

std::vector<uint8_t> mac;
UltraCrypt_Hmac(UltraCryptHashAlgorithm::SHA1, seed, message, sizeof(message), mac);

const size_t offset = mac[mac.size() - 1] & 0x0F;          // RFC 4226 §5.3
const uint32_t binary = ((mac[offset]     & 0x7F) << 24)
                      | ((mac[offset + 1] & 0xFF) << 16)
                      | ((mac[offset + 2] & 0xFF) <<  8)
                      |  (mac[offset + 3] & 0xFF);
```

---

## 7. Testing requirements

Cryptographic code is the least tolerant of "looks right", so the test suite
is part of the deliverable, not a follow-up:

- **Published test vectors, all mandatory:** FIPS 180-4 (SHA-2), RFC 2202 /
  4231 (HMAC-SHA-1 / SHA-2), RFC 6070 (PBKDF2), RFC 5869 (HKDF), RFC 9106
  (Argon2id), NIST CAVP GCM, RFC 8439 (ChaCha20-Poly1305), RFC 4648
  (Base32/64), plus RFC 4226 App. D and RFC 6238 App. B once the OTP engine
  lands.
- **Negative tests:** every AEAD open must be exercised with a flipped
  ciphertext bit, a flipped AAD bit, a wrong nonce and a truncated tag, each
  asserting `AuthenticationFailed` **and** an empty output.
- **Round-trip and cross-platform:** a fixture file sealed on one platform must
  open on the other two in CI — this is the guarantee Option B exists to
  provide, so it must be tested, not assumed.
- **Hygiene:** `UltraCryptSecureBuffer` wipe-on-destroy verified; a test
  asserting no public header includes a backend header (the wrapped-engine
  rule, mechanically checked); ASan/UBSan on the `UltraCrypt` target.

---

## 8. Suggested rollout

| Stage | Contents |
|---|---|
| **1** | Backend decision ratified; `UltraCryptSecureBuffer`, `UltraCrypt_SecureZero`, `UltraCrypt_ConstantTimeEquals`, random, SHA-2 hashing, HMAC (incl. SHA-1 for TOTP), full test vectors |
| **2** | AEAD (AES-256-GCM, ChaCha20-Poly1305), Argon2id + PBKDF2, HKDF, Base32/64 — completes what UCD v2, UltraVault and the authenticator need |
| **3** | Consumer migration: rewrite or remove `UltraCanvasDocument`'s encryption; retire `AnchorPoint/net/Sha256.h`; UCD v2 writer/reader; UltraVault file backend |
| **4** | Optional additions as needed: BLAKE3-256, XChaCha20-Poly1305 at volume, public-key surface |

Stage 1 alone unblocks UltraAuthenticator's OTP engine; Stage 2 unblocks its
storage layer and the UCD v2 format.

---

## 9. Open questions

1. **Backend: A, B or C (§3)?** Everything else follows from this. The
   recommendation is B (vendored libsodium).
2. Do the companion encodings (§5.7) belong in UltraCrypt or in
   `DataFormats/` alongside UltraCanvasJSON?
3. Should `UltraCryptHashAlgorithm::SHA1` be exposed at all outside the HMAC
   path, even behind `allowLegacy`?
4. Is BLAKE3-256 (UCD v2 `SVLT` algorithm `1`) needed in the first release, or
   may writers emit algorithm `0` (SHA-256) until a consumer asks?
5. Confirm the disposition of `UltraCanvasDocument`'s dormant encryption code
   (§1.1): rewrite against UltraCrypt, or delete the entry points?
6. Confirm the global-namespace + `UltraCrypt_` prefix convention (§4), chosen
   to match UltraNet and UltraDatabase. UltraAI and the UltraVault design use
   a named namespace instead, so the framework currently has both patterns.

---

## See also

- [Masterfile_modules.md](../../../Masterfile_modules.md) §7 — registry entry
- [UltraCanvasJSON](../../UltraCanvas/UltraCanvasJSON.md) — the wrapped-engine precedent
- [UCD File Format v2](../../UltraCanvas/UCD-FileFormat-v2.md) — the largest consumer
- [UltraAuthenticator investigation](../../UltraAuthenticator/UltraAuthenticator-Investigation.md)
- [UltraVault design](../../../UltraAI/Docs/UltraVault.md) — consumes UltraCrypt
