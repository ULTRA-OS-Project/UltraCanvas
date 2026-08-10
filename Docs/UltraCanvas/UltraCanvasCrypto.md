# UltraCanvasCrypto — Framework-Wide Cryptographic Services

**Status:** Design proposal for review. Not yet implemented.
**Version:** 0.1.0 (draft)
**Author:** UltraCanvas Framework / ULTRA OS
**Last Modified:** 2026-08-10

`UltraCanvasCrypto` is proposed as the second module of the **DataFormats**-style
core-service tier (`UltraCanvas/{include,core}/Crypto/`): a framework-wide
cryptographic facility that core, plugins, sibling modules and applications can
all depend on, following the same wrapped-engine discipline as
[`UltraCanvasJSON`](UltraCanvasJSON.md).

This document specifies **what it must provide and why**, the **backend
decision** that must be made before implementation, and the **proposed public
API**. It deliberately stops short of implementation so the backend choice
(§3) can be settled first — that choice affects dependency policy on all three
host platforms.

---

## 1. Why this module exists

There is currently **no sanctioned way for UltraCanvas code to compute a hash,
an HMAC, derive a key, or encrypt a blob.** Outside vendored third-party code,
only three files in the repository touch a crypto library, and all three are
TLS backends doing TLS.

The result is that every component needing cryptography has independently
hand-rolled, reached around the rules, or stalled:

| Consumer | Requires | State today |
|---|---|---|
| **UCD file format v2** ([spec](UCD-FileFormat-v2.md) §4.3–4.4) | AES-256-GCM, ChaCha20-Poly1305, Argon2id, PBKDF2-HMAC-SHA256, HKDF-SHA256, SHA-256, BLAKE3-256, CSPRNG (file UUID, salts) | Fully specified; **none of the primitives exist**, so the format cannot be implemented as written |
| **UltraCanvasDocument** (UCD v1 encryption) | AES-256, PBKDF2, password verification | `#include <openssl/aes.h>` directly inside a plugin — house-rule violation — and functionally broken (§1.1) |
| **AnchorPoint** | SHA-256 file integrity | Hand-rolled `Apps/AnchorPoint/net/Sha256.h`, whose header states it is a placeholder awaiting "a vetted crypto surface" |
| **UltraVault** ([design](../../UltraAI/Docs/UltraVault.md)) | AEAD + password KDF for its file-backed fallback backend | Design doc only |
| **UltraDatabase** | At-rest encryption | Listed as a Stage 3 item, unstarted |
| **UltraAuthenticator** ([investigation](../UltraAuthenticator/UltraAuthenticator-Investigation.md)) | HMAC-SHA-1/256/512, CSPRNG, AEAD, KDF, constant-time compare, secure memory | Blocked |

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
added to the build it must be rewritten against this API, or its encryption
entry points removed so no caller can believe in them.

---

## 2. Scope

**In scope** — data-at-rest and data-integrity primitives:
hashing, HMAC, authenticated encryption (AEAD), password-based and
key-derivation functions, cryptographically secure random generation,
constant-time comparison, and secure (zeroizing) memory.

**Out of scope:**

- **Transport security.** TLS stays in UltraNet, per-platform and OS-native.
  UltraCanvasCrypto never opens a socket and never validates a certificate.
- **Credential storage and policy.** That is UltraVault's job; UltraVault
  *consumes* this module for its file-backed fallback backend.
- **Public-key cryptography** (signatures, key agreement, X.509). No consumer
  needs it yet. The API is arranged so a `Crypto::Sign/Verify` surface can be
  added later without disturbing what is specified here.
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
(`core/UltraNet/UltraNetTlsImpl.h` + one impl per OS): OpenSSL on Linux, CNG /
BCrypt on Windows, CommonCrypto / CryptoKit on macOS.

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

Vendor **libsodium** (ISC licence) under `UltraCanvas/third_party/` and wrap it
exactly as yyjson is wrapped.

*For:* one implementation, bit-identical on every platform — the property that
matters most for a file format. Covers nearly the whole requirement set
directly: AES-256-GCM (hardware-accelerated), ChaCha20-Poly1305 and
XChaCha20-Poly1305, Argon2id, HMAC-SHA-256/512, SHA-256/512, BLAKE2b, HKDF,
a CSPRNG that draws from the OS entropy source, `sodium_memcmp` for
constant-time comparison, and — valuable for §5.1 — `sodium_malloc`,
`sodium_mlock` and `sodium_memzero` for guarded, non-swappable, zeroizing
memory. Small, widely audited, packaged on every distro, and API-stable for a
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
trust store and cipher policy. For *data at rest*, the requirement is inverted —
a document encrypted on Linux must decrypt byte-for-byte on Windows and macOS,
which argues for exactly one implementation everywhere. TLS policy is
untouched by this choice; the two concerns stay cleanly separated.

### 3.1 Handling the two gaps

- **SHA-1** is needed only for `HMAC-SHA-1` in TOTP (where HMAC does not
  inherit SHA-1's collision weakness, and issuer compatibility requires it).
  It must *not* be offered as a general-purpose digest. Proposal: vendor a
  ~150-line SHA-1 compression function used solely by the HMAC path, and mark
  `HashAlgorithm::SHA1` as legacy in the header, rejected by
  `Crypto::Hash()` unless `HashOptions::allowLegacy` is set.
- **BLAKE3-256** is `hash algorithm = 1` in UCD v2's `SVLT` record, and
  optional — `0` = SHA-256 is the default. Proposal: ship SHA-256 only in
  Stage 1, have writers emit algorithm `0`, and treat BLAKE3 as a later
  addition (vendoring the reference implementation, CC0/Apache-2.0) if a real
  need appears. `IsAlgorithmAvailable()` lets readers degrade honestly in the
  meantime.

---

## 4. Placement and build target

Sources at `UltraCanvas/{include,core}/Crypto/UltraCanvasCrypto.{h,cpp}`,
namespace `UltraCanvas`, following the `DataFormats/` precedent — UCD is a
core file format, so core must be able to reach the primitives without
depending on a sibling module.

Because UltraVault and UltraDatabase also need this and must **not** pull in
the UI layer, the crypto sources are deliberately free of any UI dependency
(they include no UltraCanvas headers beyond fixed-width integer types) and
should be compiled into a small standalone static target — `UltraCanvasCrypto`
— that both the framework and the sibling modules link. This keeps one
implementation without forcing a UI dependency on a headless consumer.

```cpp
#include "Crypto/UltraCanvasCrypto.h"
using namespace UltraCanvas;
```

---

## 5. Proposed public API

House conventions followed throughout: PascalCase; `namespace UltraCanvas`;
no third-party type in any public header; a structured result type with
`operator bool()` for blocking operations, matching `UltraNetResult` /
`UltraDbResult`; options carried in structs so calls stay readable and
extensible; nothing throws.

### 5.0 Result type and lifecycle

```cpp
enum class CryptoResultCode : uint8_t {
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

struct CryptoResult {
    CryptoResultCode code = CryptoResultCode::InternalError;
    bool success = false;
    std::string message;

    operator bool() const { return success; }

    static CryptoResult Ok();
    static CryptoResult Error(CryptoResultCode code, const std::string& message);
};

namespace Crypto {
    // Idempotent; thread-safe; called automatically on first use, but callers
    // that care about deterministic init order may call it explicitly.
    CryptoResult Initialize();
    void        Shutdown();
    bool        IsAvailable();
    std::string GetBackendName();          // e.g. "libsodium 1.0.19"
}
```

**Error-reporting rule:** `message` is for developers and logs. It must never
contain key material, plaintext, or password-derived data, and it must not
distinguish *why* an AEAD open failed beyond `AuthenticationFailed` — the
distinction is an oracle.

### 5.1 SecureBuffer — the type secrets travel in

Every API that touches key material takes or returns a `SecureBuffer` rather
than `std::string` / `std::vector<uint8_t>`, so secrets cannot be copied by
accident, are wiped on destruction, and are kept out of swap where the OS
allows it.

```cpp
class SecureBuffer {
public:
    SecureBuffer();
    explicit SecureBuffer(size_t size);              // zero-filled
    SecureBuffer(const void* data, size_t size);
    ~SecureBuffer();                                  // zeroizes, then frees

    // Move-only: copying a secret must be deliberate, never implicit.
    SecureBuffer(SecureBuffer&&) noexcept;
    SecureBuffer& operator=(SecureBuffer&&) noexcept;
    SecureBuffer(const SecureBuffer&) = delete;
    SecureBuffer& operator=(const SecureBuffer&) = delete;
    SecureBuffer Clone() const;                       // explicit copy

    uint8_t*       Data();
    const uint8_t* Data() const;
    size_t         GetSize() const;
    bool           IsEmpty() const;

    void Resize(size_t size);       // grow/shrink, wiping any discarded bytes
    void Clear();                   // zeroize and release

    // Best-effort mlock()/VirtualLock(): keeps pages out of swap. Returns
    // false when the OS refuses (RLIMIT_MEMLOCK) — a hint, never a guarantee.
    bool LockPages();

    // Adopts the bytes of a std::string and wipes the source in place. For
    // bridging from UI text fields, which cannot hand over a SecureBuffer.
    static SecureBuffer AdoptString(std::string& text);
};

namespace Crypto {
    // Compiler-barrier-protected wipe; not elided by optimizers.
    void SecureZero(void* data, size_t size);
    // Length-independent comparison. Use for every MAC/tag/digest check.
    bool ConstantTimeEquals(const void* a, const void* b, size_t size);
}
```

### 5.2 Hashing

```cpp
enum class HashAlgorithm : uint8_t {
    SHA1,        // LEGACY: HMAC-SHA-1 (TOTP) only; see HashOptions::allowLegacy
    SHA256,
    SHA384,
    SHA512,
    BLAKE2b256,
    BLAKE3_256   // UCD v2 SVLT alg 1; may report NotSupported (see §3.1)
};

struct HashOptions {
    bool allowLegacy = false;    // required to use SHA1 as a plain digest
};

class Hasher {                    // streaming; for files and large payloads
public:
    explicit Hasher(HashAlgorithm algorithm,
                    const HashOptions& options = HashOptions());
    ~Hasher();
    Hasher(Hasher&&) noexcept;
    Hasher(const Hasher&) = delete;

    void Update(const void* data, size_t size);
    // Finalizes and returns the digest; the object must be Reset() to reuse.
    std::vector<uint8_t> Final();
    void Reset();

    size_t GetDigestSize() const;
    bool   IsValid() const;       // false when the algorithm is unavailable
};

namespace Crypto {
    size_t GetDigestSize(HashAlgorithm algorithm);
    bool   IsAlgorithmAvailable(HashAlgorithm algorithm);

    CryptoResult Hash(HashAlgorithm algorithm,
                      const void* data, size_t size,
                      std::vector<uint8_t>& outDigest,
                      const HashOptions& options = HashOptions());

    CryptoResult HashFile(HashAlgorithm algorithm,
                          const std::string& filePath,
                          std::vector<uint8_t>& outDigest);

    std::string ToHex(const std::vector<uint8_t>& bytes);
    bool        FromHex(const std::string& hex, std::vector<uint8_t>& outBytes);
}
```

*Serves:* AnchorPoint integrity, UCD `SVLT` creation hash, UCD section
checksums.

### 5.3 HMAC

```cpp
class HmacHasher {                // streaming, same shape as Hasher
public:
    HmacHasher(HashAlgorithm algorithm, const SecureBuffer& key);
    ~HmacHasher();
    void Update(const void* data, size_t size);
    std::vector<uint8_t> Final();
    void Reset();                 // keeps the key
    size_t GetDigestSize() const;
    bool   IsValid() const;
};

namespace Crypto {
    // SHA1 is permitted here without allowLegacy: HMAC-SHA-1 remains sound
    // and TOTP interoperability requires it.
    CryptoResult Hmac(HashAlgorithm algorithm,
                      const SecureBuffer& key,
                      const void* data, size_t size,
                      std::vector<uint8_t>& outMac);
}
```

*Serves:* UltraAuthenticator (RFC 4226/6238), any future signed-manifest work.

### 5.4 Authenticated encryption (AEAD)

Only AEAD is offered. There is **no raw block-cipher or unauthenticated-mode
surface** — that omission is deliberate and is what prevents the next
`UltraCanvasDocument` from appearing.

```cpp
enum class AeadAlgorithm : uint8_t {
    Aes256Gcm,           // UCD v2 encryption type 1
    ChaCha20Poly1305,    // UCD v2 encryption type 2
    XChaCha20Poly1305    // 192-bit nonce: safe for random nonces at volume
};

struct AeadParams {
    AeadAlgorithm algorithm = AeadAlgorithm::Aes256Gcm;

    // Seal(): if empty, a fresh random nonce is generated and written back
    // here — the caller must store it. Supplying a nonce is allowed but is
    // the caller's responsibility to keep unique per key.
    // Open(): must be set to the nonce used at Seal time.
    std::vector<uint8_t> nonce;

    // Authenticated but not encrypted. UCD writers put the section header
    // here so a header edit invalidates the tag.
    std::vector<uint8_t> associatedData;
};

namespace Crypto {
    size_t GetKeySize(AeadAlgorithm algorithm);      // 32 for all three
    size_t GetNonceSize(AeadAlgorithm algorithm);    // 12 / 12 / 24
    size_t GetTagSize(AeadAlgorithm algorithm);      // 16
    bool   IsAlgorithmAvailable(AeadAlgorithm algorithm);

    // Ciphertext is plaintext-length + tag size; the tag is appended.
    CryptoResult AeadSeal(const SecureBuffer& key,
                          AeadParams& params,
                          const void* plaintext, size_t size,
                          std::vector<uint8_t>& outCiphertext);

    // Returns AuthenticationFailed — and leaves outPlaintext empty — if the
    // ciphertext, the AAD or the nonce was altered. Never returns partially
    // verified plaintext.
    CryptoResult AeadOpen(const SecureBuffer& key,
                          const AeadParams& params,
                          const void* ciphertext, size_t size,
                          SecureBuffer& outPlaintext);
}
```

Taking `AeadParams&` by non-const reference in `Seal` is the deliberate shape:
generating the nonce by default makes nonce reuse — the classic
catastrophic-failure mode of both GCM and Poly1305 — something a caller has to
opt *into* rather than something they forget to avoid.

*Serves:* UCD v2 §4.3 section pipeline, UltraVault file backend,
UltraAuthenticator's store, UltraDatabase at-rest encryption.

### 5.5 Key derivation

```cpp
enum class KdfAlgorithm : uint8_t {
    Argon2id,           // memory-hard; the default and the UCD v2 recommendation
    Pbkdf2HmacSha256    // fallback for interoperability with existing files
};

struct KdfParams {
    KdfAlgorithm algorithm = KdfAlgorithm::Argon2id;

    // If empty, DeriveKeyFromPassword generates 16 random bytes and writes
    // them back — the caller stores them (UCD keeps them in SECU).
    std::vector<uint8_t> salt;

    uint32_t iterations  = 0;   // Argon2 passes / PBKDF2 iterations
    uint32_t memoryKiB   = 0;   // Argon2 only
    uint32_t parallelism = 0;   // Argon2 only
    size_t   outputLength = 32;
};

namespace Crypto {
    // Current-guidance cost parameters; revisited as hardware moves, so
    // stored files must always record the parameters they were written with
    // rather than assuming today's defaults.
    //   Argon2id          -> 3 passes, 64 MiB, parallelism 1
    //   Pbkdf2HmacSha256  -> 600 000 iterations
    KdfParams RecommendedKdfParams(KdfAlgorithm algorithm);

    CryptoResult DeriveKeyFromPassword(const SecureBuffer& password,
                                       KdfParams& params,
                                       SecureBuffer& outKey);

    // HKDF (RFC 5869) — for deriving subkeys from existing high-entropy key
    // material. Not for passwords: it is deliberately fast.
    // UCD v2 §4.4 uses exactly this:
    //   HKDF-SHA256(keyMaterial, salt = SECU salt, info = "UCD-SVLT-v1" + UUID)
    CryptoResult DeriveKeyHkdf(HashAlgorithm algorithm,
                               const SecureBuffer& inputKeyMaterial,
                               const std::vector<uint8_t>& salt,
                               const std::vector<uint8_t>& info,
                               size_t outputLength,
                               SecureBuffer& outKey);
}
```

### 5.6 Random generation

```cpp
namespace Crypto {
    // All draw from the OS CSPRNG. On failure they return EntropyFailure and
    // leave the output untouched — there is deliberately no PRNG fallback.
    CryptoResult RandomBytes(void* out, size_t size);
    CryptoResult RandomBytes(std::vector<uint8_t>& out, size_t size);
    CryptoResult RandomSecureBuffer(size_t size, SecureBuffer& out);

    // Uniform in [0, bound) without modulo bias.
    CryptoResult RandomUInt32(uint32_t bound, uint32_t& out);

    // RFC 4122 version 4, lowercase hyphenated. UCD v2 needs one per file.
    CryptoResult GenerateUuidV4(std::string& out);
}
```

### 5.7 Companion encodings — placement to be decided

TOTP provisioning needs **Base32** (RFC 4648) and several consumers want
**Base64**. These are encodings, not cryptography, so they arguably belong in
`DataFormats/` rather than here. They are listed for completeness because no
home exists today and secrets pass through them, which means the
implementations need the same care about intermediate copies:

```cpp
namespace Crypto {
    std::string  Base64Encode(const void* data, size_t size);
    CryptoResult Base64Decode(const std::string& text, std::vector<uint8_t>& out);

    std::string  Base32Encode(const void* data, size_t size, bool pad = true);
    // Strict RFC 4648: rejects non-alphabet characters. Case-insensitive.
    CryptoResult Base32Decode(const std::string& text, SecureBuffer& out);
}
```

Note the asymmetry: `Base32Decode` yields a `SecureBuffer` because its
dominant use is decoding a TOTP seed.

---

## 6. Usage sketches

**UCD v2 section encryption (§4.3 pipeline):**

```cpp
KdfParams kdf = Crypto::RecommendedKdfParams(KdfAlgorithm::Argon2id);
SecureBuffer key;
if (!Crypto::DeriveKeyFromPassword(password, kdf, key)) return false;
// kdf.salt / iterations / memoryKiB now hold what must be written to SECU.

AeadParams aead;
aead.algorithm      = AeadAlgorithm::Aes256Gcm;
aead.associatedData = sectionHeaderBytes;      // header tampering breaks the tag
std::vector<uint8_t> ciphertext;
if (!Crypto::AeadSeal(key, aead, payload.data(), payload.size(), ciphertext))
    return false;
// aead.nonce holds the generated nonce — store it with the section.
```

**UCD v2 SuperVault subkey (§4.4 step 6):**

```cpp
std::vector<uint8_t> info(kSvltInfoPrefix.begin(), kSvltInfoPrefix.end());
info.insert(info.end(), fileUuid.begin(), fileUuid.end());

SecureBuffer sectionKey;
Crypto::DeriveKeyHkdf(HashAlgorithm::SHA256, serviceKeyMaterial,
                      kdfSalt, info, 32, sectionKey);
```

**TOTP code (RFC 6238):**

```cpp
uint64_t counter = static_cast<uint64_t>(unixTimeUtc) / period;
uint8_t  message[8];
for (int i = 0; i < 8; ++i) message[7 - i] = static_cast<uint8_t>(counter >> (i * 8));

std::vector<uint8_t> mac;
Crypto::Hmac(HashAlgorithm::SHA1, seed, message, sizeof(message), mac);

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
- **Round-trip and cross-platform:** a fixture file sealed on one platform
  must open on the other two in CI — this is the guarantee Option B exists to
  provide, so it must be tested, not assumed.
- **Hygiene:** `SecureBuffer` wipe-on-destroy verified; a test asserting no
  public header includes a backend header (the wrapped-engine rule, mechanically
  checked); ASan/UBSan on the crypto target.

---

## 8. Suggested rollout

| Stage | Contents |
|---|---|
| **1** | Backend decision ratified; `SecureBuffer`, `SecureZero`, `ConstantTimeEquals`, random, SHA-2 hashing, HMAC (incl. SHA-1 for TOTP), full test vectors |
| **2** | AEAD (AES-256-GCM, ChaCha20-Poly1305), Argon2id + PBKDF2, HKDF, Base32/64 — this completes what UCD v2, UltraVault and the authenticator need |
| **3** | Consumer migration: rewrite or remove `UltraCanvasDocument`'s encryption; retire `AnchorPoint/net/Sha256.h`; UCD v2 writer/reader; UltraVault file backend |
| **4** | Optional additions as needed: BLAKE3-256, XChaCha20-Poly1305 at volume, public-key surface |

Stage 1 alone unblocks UltraAuthenticator's OTP engine; Stage 2 unblocks its
storage layer and the UCD v2 format.

---

## 9. Open questions

1. **Backend: A, B or C (§3)?** Everything else follows from this. The
   recommendation is B (vendored libsodium).
2. Module name — `UltraCanvasCrypto` (core service, as proposed here) or
   `UltraCrypto` (sibling module)? This document assumes the former because
   UCD is a core format. *Note: the request that prompted this document used
   the name "UltraCrypt"; renaming is a trivial search-and-replace if that is
   preferred.*
3. Is a standalone `UltraCanvasCrypto` static target (§4) acceptable, so
   UltraVault and UltraDatabase can link crypto without the UI layer?
4. Do the companion encodings (§5.7) belong here or in `DataFormats/`?
5. Should `HashAlgorithm::SHA1` be exposed at all outside the HMAC path, even
   behind `allowLegacy`?
6. Is BLAKE3-256 (UCD v2 `SVLT` algorithm `1`) needed in the first release, or
   may writers emit algorithm `0` (SHA-256) until a consumer asks?
7. Confirm the disposition of `UltraCanvasDocument`'s dormant encryption code
   (§1.1): rewrite against this API, or delete the entry points?

---

## See also

- [UltraCanvasJSON](UltraCanvasJSON.md) — the wrapped-engine precedent
- [UCD File Format v2](UCD-FileFormat-v2.md) — the largest consumer
- [UltraAuthenticator investigation](../UltraAuthenticator/UltraAuthenticator-Investigation.md)
- [UltraVault design](../../UltraAI/Docs/UltraVault.md) — consumes this module
