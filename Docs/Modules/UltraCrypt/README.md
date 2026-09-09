# UltraCrypt — Cryptographic Services for ULTRA OS

**Status:** Stages 1–2 implemented and under test; Stage 3 in progress (§8).
**Version:** 0.1.0
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
**backend decision** and how it was reached (§3), and the **public surface**
(§5).

**Implemented so far** — `UltraCanvas/{include,core}/UltraCrypt/`, backed by
libsodium, covered by `Tests/UltraCryptTests.cpp`:
`UltraCryptSecureBuffer`, `UltraCrypt_SecureZero`,
`UltraCrypt_ConstantTimeEquals`, the random family, SHA-1 (legacy-gated) and
SHA-256/512 (one-shot, streaming and file), HMAC-SHA-1/256/512, AEAD
(XChaCha20-Poly1305 plus the hardware-gated AES-256-GCM interop path),
Argon2id key derivation and HKDF-SHA-256/512. (The RFC 4648 Base32/Base64
codecs live in UltraCanvasUtils, §5.7; UltraCrypt keeps only the secure-buffer
Base32 decoder.) First consumer: the UCD document encryption envelope
(`UltraCanvas/Plugins/Documents/UCDCryptoEnvelope.{h,cpp}`, tested by
`Tests/UCDCryptoEnvelopeTests.cpp`), which replaced the broken encryption
described in §1.1.

Everything UltraAuthenticator's OTP engine needs — `UltraCrypt_Hmac` with
SHA-1/256/512, and `UltraCrypt_Base32Decode` yielding a secure buffer — is
therefore in place.

**Notes on two of these.** SHA-1 is vendored (~150 lines in
`UltraCryptCore.cpp`) because libsodium omits it; `UltraCrypt_Hash` refuses it
unless `UltraCryptHashOptions::allowLegacy` is set, while `UltraCrypt_Hmac`
accepts it freely since HMAC-SHA-1 is sound and TOTP requires it. HKDF is
built on the HMAC primitives rather than libsodium's `crypto_kdf_hkdf_*`,
which only exists from 1.0.19 — the current Ubuntu LTS ships 1.0.18.

**Not yet implemented:** the public-key surface (§2, no consumer). BLAKE3-256,
once listed as optional, has been dropped altogether (§9).

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
| **UCD file format v2** ([spec](../../UltraCanvas/UCD-FileFormat-v2.md) §4.3–4.4) | XChaCha20-Poly1305, Argon2id, HKDF-SHA256, SHA-256, CSPRNG (file UUID, salts, nonces) | Fully specified; **none of the primitives exist**, so the format cannot be implemented as written |
| **UltraCanvasDocument** (UCD v1 encryption) | AEAD + password KDF | **Fixed** — now uses UltraCrypt through `UCDCryptoEnvelope`; see §1.1 for what it was |
| **AnchorPoint** | SHA-256 file integrity | ✅ Migrated. `Apps/AnchorPoint/net/Sha256.h` has been deleted; `Protocol.cpp` hashes through `UltraCrypt_HashFile` |
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

**That file was not in the CMake source list, so this was dormant code and
never a shipping vulnerability** — it must not be reported as one. Its
relevance is diagnostic: it is exactly what a missing shared API produces.
Plausible, well-intentioned security code that silently does nothing.

**Resolved.** The cryptography now lives in
`UltraCanvas/Plugins/Documents/UCDCryptoEnvelope.{h,cpp}`, which depends only
on UltraCrypt and is therefore compiled and unit-tested on its own
(`Tests/UCDCryptoEnvelopeTests.cpp`) rather than reviewed by eye.
`UltraCanvasDocument::EncryptData` / `DecryptData` are now thin adapters over
it, `SetPassword` / `VerifyPassword` use Argon2id with a constant-time
comparison instead of a bare SHA-256 with a `std::hash` fallback, and
`GeneratePasswordHash` / `GenerateSalt` are gone. Every regression test in that
suite fails against the old implementation.

Two caveats stated plainly:

1. `UltraCanvasDocument.cpp` **still does not compile**, for reasons unrelated
   to cryptography — it references undeclared types (`UCComponentData`,
   `UltraCanvasWindow`) and defines `GetCurrentDateTime` twice. It therefore
   remains out of the build, and the crypto changes inside it are verified by
   inspection, while the logic they delegate to is verified by tests.
2. Because the old code never produced ciphertext, **no readable encrypted
   `.ucd` file can exist**, so the new envelope carries no backward-compatibility
   burden and none is implemented.

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
`Security.framework` (SecureTransport).

So there is no free ride. §3.1 lists what has to be covered; §3.2 and §3.3
compare the candidates against it; §3.4 explains what actually separates them.

> **Ruling (2026-08-10):** UCD v2 is our own format, so it does not need to
> offer a menu of algorithms — it should use the single best one. That
> decision has been taken (§3.5) and it removes two of the three things that
> made this a close call. The analysis in §3.1–3.4 is kept as the record of
> *why*; §3.7 records why the OS-native route was rejected.

### 3.1 What the backend must supply

Derived from the consumer table in §1 — nothing here is speculative:

| # | Primitive | Required by |
|---|---|---|
| 1 | SHA-256 | AnchorPoint, UCD `SVLT` creation hash, UCD section checksums |
| 2 | SHA-512 | TOTP SHA-512 variant |
| 3 | SHA-1 (HMAC use only) | TOTP issuer compatibility — the default TOTP algorithm |
| 4 | HMAC-SHA-1 / -256 / -512 | UltraAuthenticator (RFC 4226/6238) |
| 5 | AES-256-GCM | UCD v2 encryption type 1 |
| 6 | ChaCha20-Poly1305 | UCD v2 encryption type 2 |
| 7 | Argon2id | UCD v2 recommended KDF, UltraVault, authenticator store |
| 8 | PBKDF2-HMAC-SHA256 | UCD v2 fallback KDF |
| 9 | HKDF-SHA256 | UCD v2 §4.4 SuperVault subkey derivation |
| 10 | ~~BLAKE3-256~~ | **Dropped** (§9): the UCD v2 `SVLT` hash algorithm field is SHA-256 only |
| 11 | CSPRNG | Salts, nonces, file UUIDs |
| 12 | Constant-time compare | Every MAC/tag check |
| 13 | Secure memory (zeroize, page-lock) | Authenticator seeds, vault master keys |

### 3.2 Algorithm coverage

Assessed against the versions **actually packaged on the current Ubuntu LTS**
(24.04), since that is what a build will link by default:
OpenSSL 3.0.13, libsodium 1.0.18, mbedTLS 2.28.8.

| # | Primitive | OpenSSL 3.0 | libsodium 1.0.18 | mbedTLS 2.28 |
|---|---|---|---|---|
| 1 | SHA-256 | ✅ | ✅ | ✅ |
| 2 | SHA-512 | ✅ | ✅ | ✅ |
| 3 | SHA-1 | ✅ | ❌ deliberately omitted — **vendored, ~150 lines** | ✅ |
| 4 | HMAC-SHA-1/256/512 | ✅ | ⚠️ SHA-256/512 only | ✅ |
| 5 | AES-256-GCM | ✅ **portable software impl** | ⚠️ **AES-NI hardware only** | ✅ **portable software impl** |
| 6 | ChaCha20-Poly1305 | ✅ | ✅ (+ XChaCha20) | ✅ |
| 7 | Argon2id | ❌ **needs OpenSSL 3.2+** | ✅ native | ❌ |
| 8 | PBKDF2-HMAC-SHA256 | ✅ | ❌ (offers scrypt instead) | ✅ |
| 9 | HKDF-SHA256 | ✅ | ⚠️ dedicated API is 1.0.19+ — **built on HMAC** | ✅ |
| 10 | ~~BLAKE3-256~~ (dropped, §9) | ❌ | ❌ | ❌ |
| 11 | CSPRNG | ✅ `RAND_bytes` | ✅ `randombytes_buf` | ✅ CTR-DRBG |
| 12 | Constant-time compare | ✅ `CRYPTO_memcmp` | ✅ `sodium_memcmp` | ✅ |
| 13 | Secure memory | ⚠️ `OPENSSL_cleanse`; secure heap is opt-in and awkward | ✅ **best in class** — `sodium_malloc` (guard pages), `sodium_mlock`, `sodium_memzero` | ❌ none |

### 3.3 Practical and operational comparison

| Dimension | OpenSSL | libsodium | mbedTLS |
|---|---|---|---|
| Licence | Apache-2.0 | ISC | Apache-2.0 |
| Approx. added size (static) | ~4–5 MB | **~300 KB** | ~1 MB |
| Present on Linux today | ✅ already linked for UltraNet | ❌ new | ❌ new |
| Present on Windows today | ❌ **none** — Schannel is used | ❌ new | ❌ new |
| Present on macOS today | ❌ **none** — SecureTransport is used | ❌ new | ❌ new |
| Effect on documented TLS policy | ⚠️ contradicts "OS-native on Win/macOS" | ✅ none — different concern | ✅ none |
| Cross-platform byte-identical results | ✅ | ✅ | ✅ |
| API surface to wrap | Very large, many legacy traps | Small, misuse-resistant by design | Moderate |
| Hardware assumptions | none | **requires AES-NI for AES-GCM** | none |
| Distro packaging | universal | universal | universal |

### 3.4 Reading the tables — what actually separates them

The tables look close because each candidate covers most of the list. The
decision turns on **two specific cells**, not on the totals.

**First: nobody gives you Argon2id from a system library.** Ubuntu 24.04 LTS
ships OpenSSL **3.0.13**, and OpenSSL only gained an Argon2 KDF in **3.2**.
Debian 12 is likewise on 3.0. So "just use OpenSSL" does *not* deliver the KDF
that UCD v2 names as its recommendation — it would still need a vendored
Argon2 (the reference implementation is packaged as `libargon2-dev`, ~2k
lines, CC0/Apache-2.0), or a bump to a vendored OpenSSL 3.2+, which is a very
large thing to vendor. libsodium is the only candidate with Argon2id built in,
because libsodium is where the reference implementation is best maintained.

**Second: libsodium's AES-256-GCM is hardware-gated.** libsodium ships no
software fallback for AES-GCM — `crypto_aead_aes256gcm_is_available()` returns
false on any CPU without AES-NI (or the ARMv8 crypto extensions). OpenSSL and
mbedTLS both include portable constant-time software implementations.

That second point lands squarely on ULTRA OS's stated hardware target —
*"affordable hardware, from ARM and RISC-V to x86."* ARMv8 crypto extensions
are **optional**, and a number of popular SBCs omit them; RISC-V's scalar
crypto extension is newer still and rare in shipping silicon. On such a
machine, a UCD file written elsewhere with encryption type 1 (AES-256-GCM)
would simply **not open** — which destroys the "one format, opens everywhere"
property that motivated picking a single portable library in the first place.
*(The exact SBC list should be confirmed on the actual target hardware before
this is treated as settled; the general point — that the extension is optional
and frequently absent — is not in doubt.)*

**Everything else on the list is cheap to repair, and these two are not
symmetric in cost:**

| Gap | Repair | Risk |
|---|---|---|
| OpenSSL: no Argon2id | link/vendor the reference `libargon2` | Low — well-trodden, RFC 9106 vectors |
| OpenSSL: weak secure memory | implement `UltraCryptSecureBuffer` over `mlock`/`VirtualLock` + explicit wipe (~80 lines) | Low |
| libsodium: no SHA-1 | vendor a SHA-1 compression function (~150 lines), HMAC path only | Low — FIPS 180-4 vectors |
| libsodium: no PBKDF2 | implement over its HMAC (~40 lines) | Low — RFC 6070 vectors |
| libsodium: no SHA-384 | **drop it** — no consumer needs it (it was speculative in §5.2) | None |
| libsodium: HKDF in 1.0.18 | implement over HMAC-SHA256 (~30 lines) | Low — RFC 5869 vectors |
| **libsodium: no software AES-GCM** | **write or vendor a constant-time AES-GCM** | **High — precisely the thing that must never be hand-rolled; vendoring one means shipping a second crypto library** |
| any: no BLAKE3 | **dropped from the spec** — `SVLT` hash algorithm is SHA-256 only | None |

PBKDF2, HKDF and SHA-1 are all safe to implement given a correct HMAC — they
are constructions *over* a primitive, with official test vectors, not new
primitives. AES-GCM is not in that category.

### 3.5 Decision

**Cipher: XChaCha20-Poly1305, alone. KDF: Argon2id, alone. Backend: libsodium.**

UCD v2 previously listed AES-256-GCM, ChaCha20-Poly1305 and a SuperVault mode
as three "encryption types", with Argon2id and PBKDF2 as two KDFs. Since the
format is ours and unshipped, that menu buys nothing and costs every reader a
code path. It is replaced by one cipher and one KDF; the header field at offset
16 now selects the **key source** (none / password / SuperVault) rather than an
algorithm, and algorithm agility lives in the format version number.

**Why XChaCha20-Poly1305 is the best single choice:**

- **It is constant-time in portable software.** No hardware dependency, so it
  behaves identically on x86, on ARM without the optional crypto extensions,
  and on RISC-V — the full range ULTRA OS targets. AES in software is either
  slow or timing-vulnerable; that is the whole reason the ChaCha family exists.
- **Its 192-bit nonce makes random nonces safe outright.** UCD gives every
  encrypted section a fresh random nonce. At 96 bits (what AES-GCM and IETF
  ChaCha20-Poly1305 use) random generation carries a birthday bound that a
  long-lived key would eventually meet, which is exactly the kind of subtle
  accounting that goes wrong years later. At 192 bits the concern disappears,
  and with it the need for any counter or collision bookkeeping in the writer.
- **It is fast without acceleration** — typically faster than software AES on
  the low-cost boards in question.
- Nonce-reuse catastrophe is the top practical failure mode of both GCM and
  Poly1305 constructions; the widest nonce available is the cheapest possible
  insurance against it.

AES-256-GCM is retained in the *module* (hardware-gated, for reading foreign
data), but nothing UltraCanvas writes will use it.

**Why the ruling settles the backend question.** The two objections to
libsodium in §3.4 were its lack of software AES-GCM and its lack of PBKDF2.
With one cipher and one KDF, neither is required any more:

| Earlier gap | Status after the ruling |
|---|---|
| No software AES-GCM (rows 5) | **Moot** — UltraCanvas never writes AES-GCM |
| No PBKDF2 (row 8) | **Moot** — Argon2id is the only KDF |
| No SHA-384 | Dropped — no consumer needs it |
| No SHA-1 | Still needed for TOTP; ~150 lines, HMAC path only, FIPS 180-4 vectors |
| HKDF only in 1.0.19+ | ~30 lines over HMAC-SHA-256, RFC 5869 vectors |

What remains is two small, well-vectored constructions over a correct HMAC.
Against that, libsodium supplies the only native Argon2id, XChaCha20-Poly1305
as a first-class primitive, the best secure-memory support of any candidate
(`sodium_malloc` guard pages, `sodium_mlock`, `sodium_memzero` — which the
authenticator and UltraVault genuinely need), a ~300 KB footprint against
OpenSSL's ~4–5 MB, and no disturbance to the Windows/macOS TLS policy.

mbedTLS and OpenSSL were the right answers only under the assumption that
portable AES-GCM was mandatory. It is not, so libsodium wins on every
remaining axis.

### 3.6 Consequent changes

Recorded here because they follow from §3.5 rather than being open questions:

1. **UCD v2 spec updated** — offset 16 becomes *key source* (`0` none, `1`
   password/Argon2id, `2` SuperVault); §4.3 fixes the cipher at
   XChaCha20-Poly1305 with the section header as associated data; §4.4's
   SuperVault path uses the same cipher; `SECU` now stores Argon2id cost
   parameters rather than a bare iteration count.
2. **`UltraCryptKdfAlgorithm` collapses to `Argon2id`** (§5.5). PBKDF2 is gone
   from the surface; nothing needs it.
3. **`UltraCryptAeadAlgorithm` keeps two entries** (§5.4):
   `XChaCha20Poly1305` (default, everything we write) and `Aes256Gcm`
   (hardware-gated, for reading foreign data only).
4. **`UltraCryptHashAlgorithm::SHA384` dropped** (§5.2); no consumer needs it.
5. Cost parameters are always stored with the ciphertext, never assumed, so
   raising the recommended Argon2id costs later cannot orphan existing files.

### 3.7 libsodium versus the native Windows and macOS crypto APIs

Option A (§3) proposed using each OS's own crypto stack rather than a
portable library, mirroring how UltraNet handles TLS. It was rejected in
prose; this subsection records the comparison properly, because the ruling in
§3.5 makes the answer much sharper than it was.

The native stacks are:

- **Windows — CNG** (Cryptography Next Generation, `bcrypt.dll`), the modern
  replacement for CryptoAPI. Handle-based C API.
- **macOS — CommonCrypto** (in libSystem, C API) and **CryptoKit**
  (macOS 10.15+, **Swift only**). Randomness comes from Security.framework.

#### Algorithm coverage against the settled requirement set

| Requirement (post-§3.5) | libsodium | Windows CNG | macOS native |
|---|---|---|---|
| **XChaCha20-Poly1305** (the cipher) | ✅ first-class | ❌ **absent** | ❌ **absent** (neither CommonCrypto nor CryptoKit) |
| **Argon2id** (the KDF) | ✅ native | ❌ **absent** (PBKDF2 only) | ❌ **absent** (PBKDF2 only) |
| AES-256-GCM (interop reads) | ⚠️ AES-NI only | ✅ | ⚠️ CommonCrypto's GCM is a narrow, late-added surface; CryptoKit has it but is Swift-only |
| ChaCha20-Poly1305 (IETF, 96-bit nonce) | ✅ | ⚠️ recent Windows only — version-gated | ⚠️ CryptoKit only (Swift), absent from CommonCrypto |
| SHA-256 / SHA-512 | ✅ | ✅ | ✅ |
| SHA-1 (HMAC path, TOTP) | ❌ ~150 lines to add | ✅ | ✅ |
| HMAC-SHA-1/256/512 | ⚠️ SHA-256/512 only | ✅ | ✅ |
| HKDF-SHA256 | ⚠️ dedicated API in 1.0.19+ | ✅ (Windows 10 1903+) | ❌ CommonCrypto; ✅ CryptoKit (Swift) |
| PBKDF2 | ❌ (not needed post-ruling) | ✅ | ✅ |
| CSPRNG | ✅ `randombytes_buf` | ✅ `BCryptGenRandom` | ✅ `SecRandomCopyBytes` / `arc4random_buf` |
| Constant-time compare | ✅ `sodium_memcmp` | ❌ no API (trivial to write) | ⚠️ `timingsafe_bcmp` from BSD libc |
| Zeroize | ✅ `sodium_memzero` | ✅ `SecureZeroMemory` | ✅ `memset_s` |
| Page-lock / guarded allocation | ✅ `sodium_mlock`, `sodium_malloc` (guard pages) | ⚠️ `VirtualLock` only, no guarded allocator | ⚠️ `mlock` only, no guarded allocator |

*Version-gated cells should be confirmed against the minimum Windows and
macOS versions ULTRA OS intends to support; the two ❌ rows at the top do not
depend on version and are the decisive ones.*

#### Structural and operational comparison

| Dimension | libsodium | Native (CNG + CommonCrypto/CryptoKit) |
|---|---|---|
| Number of crypto implementations to maintain | **1** | **3** (Linux still needs one — the OS stacks cover only Windows and macOS) |
| Cross-platform byte-identical output | ✅ by construction | ⚠️ must be established by test, per algorithm |
| Language fit for a C++20 framework | ✅ plain C | ⚠️ CNG is verbose C; **CryptoKit is Swift-only** and needs a bridging layer on macOS |
| Added binary size | ~300 KB | **0** |
| New third-party dependency | yes | **none** |
| Security updates | our vendoring/packaging responsibility | ✅ shipped by the OS vendor |
| FIPS-validated modules available | ❌ | ✅ (CNG) — may matter for some procurement |
| Hardware acceleration | ✅ built in | ✅ handled by OS |
| Code signing / notarization impact | one more binary to sign | ✅ nothing extra |
| Supply-chain exposure | one vendored library | ✅ none added |
| API misuse resistance | ✅ designed for it | ⚠️ general-purpose, easy to misconfigure |
| Test burden | one implementation against published vectors | three implementations, plus mandatory cross-platform fixture tests |

#### What the tables say

**The two algorithms just standardised on are precisely the two that no OS
crypto stack provides.** XChaCha20-Poly1305 is a CFRG draft that OS vendors
have not adopted, and Argon2id (RFC 9106) has not reached CNG or CommonCrypto
either — both platforms still offer PBKDF2 as their password KDF. That is not
a version-gating problem that will age out soon; it is a difference in what
these APIs are for.

So a native-backend build cannot implement §3.5 at all. It leaves two choices,
and both are worse than simply vendoring libsodium:

1. **Revert the cipher and KDF** to AES-256-GCM plus PBKDF2 — the lowest
   common denominator of OS crypto APIs — giving up portable constant-time
   encryption on unaccelerated ARM/RISC-V hardware, giving up the 192-bit
   nonce that removes nonce-reuse accounting, and giving up memory-hard
   password hashing. The format would be designed around API availability
   rather than around what is best, which is the opposite of the principle
   behind the ruling.
2. **Vendor XChaCha20-Poly1305 and Argon2id anyway** and use the OS only for
   SHA-2, HMAC and randomness. But those are the easy, uncontroversial parts;
   the vendored pieces would be the hard ones. The result is a third-party
   crypto dependency *plus* three platform backends — strictly more work and
   more risk than one library, for no benefit.

The honest case *for* native remains real and is worth stating: zero added
binary size, no supply-chain exposure, OS-vendor security updates, nothing
extra to sign or notarize, and FIPS-validated modules on Windows if
procurement ever demands them. If a FIPS requirement appears, this decision
should be revisited — but it would be a decision to change the algorithms
too, not merely the backend.

One further practical obstacle on macOS: the modern API (CryptoKit) is
**Swift-only**, so a C++20 framework would have to carry a Swift bridging
layer to reach it, while the C API that is directly callable (CommonCrypto) is
the older and considerably weaker one — no ChaCha, no HKDF, and a GCM surface
that arrived late and narrow. UltraNet tolerates this for TLS because
SecureTransport is a complete TLS implementation behind a C API; there is no
equivalent for the primitives UltraCrypt needs.

**Conclusion: native backends are the right call for TLS and the wrong one
here.** TLS wants the OS because the OS owns the trust store and cipher
policy; data-at-rest wants one implementation because the requirement is that
a file written anywhere opens everywhere. §3.5 stands.

## 4. Placement, naming and build target

UltraCrypt follows the **UltraNet / UltraDatabase layout** exactly, being a
sibling module of the same generation:

| Aspect | Convention followed |
|---|---|
| Sources | `UltraCanvas/{include,core}/UltraCrypt/` |
| Public header | `UltraCrypt/UltraCryptCore.h` |
| Documentation | `Docs/Modules/UltraCrypt/README.md` (this file) |
| Registry entry | `Masterfile_modules.md` §8 |
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
    SHA512,      // SHA-384 deliberately absent: no consumer needs it (§3.6)
    // No BLAKE variants. BLAKE3 was dropped with the SVLT hash ruling (§9);
    // BLAKE2b-256 is available from libsodium should diversity from SHA-2
    // ever be wanted, but nothing asks for it.
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
    // The default, and the only cipher UltraCanvas ever writes. 192-bit
    // nonce, so randomly generated nonces are safe without bookkeeping;
    // constant-time in portable software on hardware without AES support.
    XChaCha20Poly1305,

    // Interop only — for reading data produced elsewhere. Hardware-gated:
    // UltraCrypt_IsAeadAvailable returns false without AES-NI / ARMv8 crypto.
    // Never select this for new output.
    Aes256Gcm
};

struct UltraCryptAeadParams {
    UltraCryptAeadAlgorithm algorithm =
        UltraCryptAeadAlgorithm::XChaCha20Poly1305;

    // Seal: if empty, a fresh random nonce is generated and written back here
    // — the caller must store it. Supplying a nonce is allowed but then
    // keeping it unique per key is the caller's responsibility.
    // Open: must be set to the nonce used at Seal time.
    std::vector<uint8_t> nonce;

    // Authenticated but not encrypted. UCD writers put the section header
    // here, so a header edit invalidates the tag.
    std::vector<uint8_t> associatedData;
};

size_t UltraCrypt_GetKeySize(UltraCryptAeadAlgorithm algorithm);    // 32 both
size_t UltraCrypt_GetNonceSize(UltraCryptAeadAlgorithm algorithm);  // 24 / 12
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
UltraAuthenticator's store, UltraDatabase at-rest encryption — all of which
use `XChaCha20Poly1305` and none of which need to choose.

### 5.5 Key derivation

```cpp
// One KDF. Argon2id is memory-hard, is the current standard (RFC 9106) and is
// what every UltraCanvas format and store uses. No alternative is offered:
// a second KDF would be a second code path in every reader for no benefit.
enum class UltraCryptKdfAlgorithm : uint8_t {
    Argon2id
};

struct UltraCryptKdfParams {
    UltraCryptKdfAlgorithm algorithm = UltraCryptKdfAlgorithm::Argon2id;

    // If empty, UltraCrypt_DeriveKeyFromPassword generates 16 random bytes
    // and writes them back — the caller stores them (UCD keeps them in SECU).
    std::vector<uint8_t> salt;

    uint32_t iterations   = 0;   // Argon2id passes
    uint32_t memoryKiB    = 0;   // Argon2id memory cost
    uint32_t parallelism  = 0;   // Argon2id lanes
    size_t   outputLength = 32;
};

// Current-guidance cost parameters: 3 passes, 64 MiB, parallelism 1. These are
// revisited as hardware moves, so stored data must always record the
// parameters it was written with rather than assuming today's defaults —
// otherwise raising the defaults orphans every existing file.
UltraCryptKdfParams UltraCrypt_RecommendedKdfParams(
        UltraCryptKdfAlgorithm algorithm = UltraCryptKdfAlgorithm::Argon2id);

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

### 5.7 Companion encodings — resolved: they live in UltraCanvasUtils

TOTP provisioning needs **Base32** (RFC 4648) and several consumers want
**Base64**. These are encodings, not cryptography, and the ruling was that they
belong with the framework's other utilities: `UltraCanvas::Base64Encode` /
`Base64Decode` were already declared in `UltraCanvasUtils.h`, and
`UltraCanvas::Base32Encode` / `Base32Decode` now sit beside them. The copies
UltraCrypt used to carry (`UltraCrypt_Base64Encode`, `UltraCrypt_Base64Decode`,
`UltraCrypt_Base32Encode`) are gone.

Two details follow from UltraCrypt being headless:

- The codecs are declared in `UltraCanvasTextUtils.h` (which
  `UltraCanvasUtils.h` includes) and compiled into the small
  `UltraCanvasTextUtils` static library, together with the framework's other
  platform-free string helpers (`Trim`, `Split`, `ToLowerCase`, `StartsWith`,
  the whitespace trims). `UltraCanvasUtils.cpp` itself is platform glue —
  process launching, `<windows.h>` — that a headless module cannot compile.
  Both the UltraCanvas library and UltraCrypt link that library, so each symbol
  exists exactly once even in an application that links both; UltraCrypt
  includes only `UltraCanvasTextUtils.h`, never `UltraCanvasUtils.h`.
- UltraCrypt keeps one function in this area, `UltraCrypt_Base32Decode`, which
  decodes into an `UltraCryptSecureBuffer` and wipes the intermediate on every
  path. It *calls* `UltraCanvas::Base32Decode`; it is a destination, not a
  second implementation. It stays in UltraCrypt because its dominant use is
  decoding a TOTP seed, and `Base32Decode`'s failure contract — leaving the
  partial output in place rather than clearing it — exists precisely so this
  wrapper can zero it with `UltraCrypt_SecureZero`, which Utils cannot call
  without inverting the dependency.

```cpp
// UltraCanvasTextUtils.h (also reachable via UltraCanvasUtils.h)
std::vector<uint8_t> Base64Decode(const std::string& input);           // lenient
std::string          Base64Encode(const std::vector<uint8_t>& in, bool wrap = true);
std::string          Base32Encode(const std::vector<uint8_t>& in, bool pad = true);
std::string          Base32Encode(const uint8_t* data, size_t size, bool pad = true);
bool                 Base32Decode(const std::string& input, std::vector<uint8_t>& out); // strict

// UltraCryptCore.h — the one crypto-relevant variant
UltraCryptResult UltraCrypt_Base32Decode(const std::string& text,
                                         UltraCryptSecureBuffer& out);
```

`Base32Decode` is strict (any character outside the alphabet, or data after
padding, fails) while `Base64Decode` keeps its long-standing lenient contract;
the asymmetry is deliberate, because the Base32 consumer is the `otpauth://`
parser, which must refuse a malformed seed rather than silently produce a
different one.

---

## 6. Usage sketches

**UCD v2 section encryption (§4.3 pipeline):**

```cpp
UltraCryptKdfParams kdf =
    UltraCrypt_RecommendedKdfParams(UltraCryptKdfAlgorithm::Argon2id);
UltraCryptSecureBuffer key;
if (!UltraCrypt_DeriveKeyFromPassword(password, kdf, key)) return false;
// kdf.salt / iterations / memoryKiB now hold what must be written to SECU.

UltraCryptAeadParams aead;                  // defaults to XChaCha20Poly1305
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

- **Published test vectors, all mandatory** (implemented in
  `Tests/UltraCryptTests.cpp`): FIPS 180-4 (SHA-1 and SHA-2), RFC 2202 / 4231
  (HMAC-SHA-1 / SHA-2), RFC 5869 (HKDF), RFC 4648 (Base32/64), RFC 8439
  (ChaCha20-Poly1305) plus the XChaCha20-Poly1305 vectors from
  draft-irtf-cfrg-xchacha and libsodium's own suite, NIST CAVP GCM for the
  interop-only AES path, and RFC 4226 App. D / RFC 6238 App. B once the OTP
  engine lands.

  **One gap, stated plainly:** RFC 9106's Argon2id vectors use parallelism 4,
  which libsodium's `crypto_pwhash` does not expose (it fixes lanes at 1), so
  they cannot be run against this API. Argon2id is instead covered by
  determinism, salt sensitivity, password sensitivity and cost-parameter
  sensitivity tests, and `parallelism != 1` is rejected rather than silently
  ignored so a stored parameter set never misdescribes how a key was derived.
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
| **1** | ✅ **Done** — backend ratified (libsodium); `UltraCryptSecureBuffer`, `UltraCrypt_SecureZero`, `UltraCrypt_ConstantTimeEquals`, random, SHA-1/SHA-2 hashing, HMAC-SHA-1/SHA-2, published test vectors |
| **2** | ✅ **Done** — AEAD (XChaCha20-Poly1305; AES-256-GCM interop path), Argon2id, HKDF. The Base32/64 codecs have since moved to UltraCanvasUtils (§5.7) |
| **3** | ◐ **Started** — `UltraCanvasDocument`'s encryption is rewritten against UltraCrypt (§1.1), and `AnchorPoint/net/Sha256.h` is retired. Still to do: UCD v2 writer/reader. (UltraVault's file backend has since shipped on its own.) |
| **4** | Optional additions as needed: XChaCha20-Poly1305 at volume, public-key surface |

Stage 1 alone unblocks UltraAuthenticator's OTP engine; Stage 2 unblocks its
storage layer and the UCD v2 format.

---

## 9. Open questions

**Resolved:**

- ~~Backend choice~~ — **libsodium** (§3.5), settled by the single-cipher ruling.
- ~~UCD default cipher~~ — **XChaCha20-Poly1305 only**, with Argon2id as the
  only KDF (§3.5–3.6). The spec has been updated accordingly.
- ~~Companion encodings~~ — **UltraCanvasUtils.** Base32 joins the existing
  Base64 there; UltraCrypt's copies are removed and it keeps only the
  secure-buffer Base32 decoder, which delegates (§5.7).
- ~~SHA-1 exposure~~ — **yes, exposed.** `UltraCryptHashAlgorithm::SHA1` stays
  available for hashing behind `allowLegacy`, and HMAC-SHA-1 is ungated, as
  RFC 6238 TOTP requires.
- ~~`UltraCanvasDocument`'s dormant encryption~~ — **leave it.** The file is
  out of the build and likely to be deleted; its encryption path was already
  superseded by `UCDCryptoEnvelope` (§1.1). Nothing further is to be done to it.
- ~~Naming convention~~ — **keep the `UltraCrypt_` prefix** and the global
  namespace, matching UltraNet and UltraDatabase. The framework accepts having
  both patterns.
- ~~libsodium linkage~~ — **keep linking the distro package.** Consequence,
  stated so nobody rediscovers it: Ubuntu's 1.0.18 lacks `crypto_kdf_hkdf_*`,
  so HKDF remains this module's own RFC 5869 construction over
  `UltraCrypt_Hmac`, tested against the RFC vectors.

- ~~BLAKE3-256 as UCD v2 `SVLT` hash algorithm `1`~~ — **collapsed to
  SHA-256 only.** Code point `1` has been removed from `UCD-FileFormat-v2.md`;
  `0` = SHA-256 is the sole assigned value and readers reject anything else. A
  future algorithm would arrive with a record-version bump, consistent with the
  one-algorithm principle already applied to the cipher and KDF. The evidence
  the ruling rested on, kept so nobody has to redo the comparison:

  | | SHA-256 only | BLAKE3-256 as code point `1` |
  |---|---|---|
  | Availability | Already in libsodium (`crypto_hash_sha256`), already used by this module | In neither libsodium (which has BLAKE2b) nor OpenSSL 3.0; would mean vendoring the reference C implementation and registering a new third-party dependency |
  | Standardisation | FIPS 180-4 | None — a 2020 design with a single reference implementation |
  | Speed | Hardware-accelerated on every current x86 (SHA-NI) and ARMv8 CPU; the creation hash is computed once per document, so throughput is irrelevant | Fastest *software* hash available, but that headline compares against software SHA-256, not accelerated SHA-256 |
  | SuperVault service | One algorithm to implement and verify server-side | Two, plus a negotiation surface; every reader must handle a code point no writer may ever emit |
  | Diversity from SHA-2, if ever wanted | — | BLAKE2b-256 gives the same property for free from libsodium |

**Still open:** nothing. New questions go here.

## See also

- [Masterfile_modules.md](../../../Masterfile_modules.md) §8 — registry entry
- [UltraCanvasJSON](../../UltraCanvas/UltraCanvasJSON.md) — the wrapped-engine precedent
- [UCD File Format v2](../../UltraCanvas/UCD-FileFormat-v2.md) — the largest consumer
- [UltraAuthenticator investigation](../../UltraAuthenticator/UltraAuthenticator-Investigation.md)
- [UltraVault design](../../../UltraAI/Docs/UltraVault.md) — consumes UltraCrypt
