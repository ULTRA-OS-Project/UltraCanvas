# UltraAuthenticator — Feasibility Investigation & Security Analysis

**Status:** Investigation / pre-implementation. No code exists yet.
**Scope:** A TOTP/HOTP authenticator app for ULTRA OS (in the spirit of
Google Authenticator / FreeOTP / Aegis), built on UltraCanvas.
**Date:** 2026-08-10

This document answers two questions:

1. Which building blocks does the UltraCanvas repository already provide,
   and which are missing?
2. What are the security drawbacks and holes to design around — both in the
   app itself and in the platform underneath it?

---

## 1. What the app must do

A minimal authenticator implements:

- **TOTP** — RFC 6238 time-based one-time passwords (30 s step, HMAC-SHA-1
  by default, SHA-256/SHA-512 variants, 6–8 digits).
- **HOTP** — RFC 4226 counter-based codes (optional but cheap once TOTP
  exists; needs persistent counter handling).
- **Provisioning** via the de-facto `otpauth://` Key-URI format
  (`otpauth://totp/Issuer:account?secret=BASE32&issuer=…&algorithm=…&digits=…&period=…`),
  entered by scanning a QR code with a camera, loading a QR image file, or
  typing the Base32 secret manually.
- **Secret storage** — the shared secrets are long-lived credentials that
  must survive restarts and must not be readable by other software or by
  whoever obtains the disk.
- **Display** — a list of accounts, the current 6-digit code, a countdown
  to the next 30 s window, copy-to-clipboard.
- Optional: encrypted export/import (device migration), app lock (PIN),
  QR generation for transferring an account to another device.

---

## 2. Inventory — what the repository already provides

### 2.1 Ready to use

| Need | Provided by | Notes |
|---|---|---|
| App shell, window, event loop | `UltraCanvasApplication` (`Apps/DemoApp/`, `Apps/Texter/main.cpp` are the canonical bootstraps) | |
| All UI controls | Element catalogue, `Docs/UltraCanvas/UltraCanvasUIElements.md` — `UltraCanvasTextInput`, `UltraCanvasButton`, `UltraCanvasLabel`, `UltraCanvasBadge`, `UltraCanvasContainer`, `UltraCanvasModalDialog`, `UltraCanvasProgressBar`, … | Per AGENTS.md, the account list / code tiles must be assembled from elements, not painted by hand |
| QR **generation** | `UltraCanvas/include/Plugins/QRCode/UltraCanvasQRCode.h` — `QRCodeUtils::GenerateQRCode`, `UltraCanvasQRCode` element, SVG/PNG export | Needed only for the optional "show this account as QR" transfer feature |
| QR **decoding** (from image file) | `QRCodeUtils::ScanQRCodeFile(path)` backed by **libzbar** (`UltraCanvas/Plugins/QRCode/UltraCanvasQRCode.cpp`), `IsDecoderAvailable()` reports whether zbar was compiled in | File-path input only — see gap 2.2-c |
| Camera access + live preview frames | `UltraCanvasVideoRecorder` (`include/UltraCanvasVideoRecorder.h`): `Open()` starts a preview without recording, `GetPreviewFrame()` / `onPreviewFrame` deliver `UCVideoFramePtr`; backends for V4L2/GStreamer, AVFoundation, MediaFoundation; `onPermissionChanged` handles camera permission | Recording to disk is *not* needed — preview-only mode is exactly right for scanning |
| Local relational storage | UltraDatabase SQLite driver is implemented (`UltraCanvas/core/UltraDatabase/UltraDatabaseSqliteDriver.cpp`); parameter binding is mandatory by design | **Unencrypted** — see 2.2-b and §3.1 |
| JSON (config, export format framing) | `UltraCanvasJSON` (yyjson-backed), hardened against hostile input (strict RFC 8259, depth limit) | Do not persist raw secrets through it — see §3.2 |
| Clipboard | `UltraCanvasClipboard.h` | For copying *codes* only — see §3.7 |
| HTTPS (if ever needed) | UltraNet Stage 1 (libcurl + OpenSSL, TLS verification on by default) | The authenticator should be fully offline; no network use is required or recommended |

### 2.2 Missing — the real gaps

**(a) No crypto API surface.** TOTP is `HMAC-SHA-1/256/512` plus dynamic
truncation. OpenSSL is already a vendored dependency (for UltraNet/libcurl),
but the house rule — *"never expose a third-party type in a public header;
never call vendored libraries directly from app code"* (AGENTS.md) — means
there is currently **no sanctioned way for an app to compute an HMAC**.
The symptom already exists in the tree: `Apps/AnchorPoint/net/Sha256.h` is a
hand-rolled SHA-256 whose own header says *"When UltraNet/UltraVault bring a
vetted crypto surface, this can be replaced by that."* An authenticator must
not repeat that pattern with hand-rolled HMAC.

→ **Prerequisite work item: a small `UltraCrypto` wrapper module** (peer of
`UltraCanvasJSON` in `DataFormats/` or a sibling module) exposing at minimum:
`HmacSha1/HmacSha256/HmacSha512`, `RandomBytes` (CSPRNG), `ConstantTimeEquals`,
`SecureZero`, and an AEAD primitive (`Aes256GcmEncrypt/Decrypt`) plus a
password KDF (Argon2id preferred; PBKDF2-HMAC-SHA256 as the OpenSSL-only
fallback) for the storage layer in §3.1. All backed by OpenSSL behind an
UltraCanvas-owned API, per the wrapped-engines convention.

This gap is **framework-wide, not specific to this app** — see §2.3.

**(b) No secret storage.** `UltraVault` — the designated credential store for
ULTRA OS — is an *architecture recommendation only*
(`UltraAI/Docs/UltraVault.md`: "UltraVault module does not yet exist").
UltraDatabase has no at-rest encryption (no SQLCipher; encryption is a
Stage 3 item in its design doc). **There is nowhere in the framework today to
put a TOTP seed safely.** This is the single biggest blocker; see §3.1 for
what to do about it.

**(c) No in-memory QR decode.** `ScanQRCodeFile` takes a file path. Live
camera scanning would otherwise mean writing every preview frame to disk —
which for an authenticator would write *the secret* to disk in image form.
→ Work item: add `QRCodeUtils::ScanQRCodeImage(const UCImage&)` (or a
`UCVideoFramePtr` overload) that feeds zbar from memory, and wire it to
`onPreviewFrame`.

**(d) No TOTP/HOTP/Base32 code.** RFC 4226/6238 engines, RFC 4648 Base32
decoding, and an `otpauth://` URI parser must be written (small, but they
must be test-vector-verified — RFC 4226 App. D and RFC 6238 App. B provide
official vectors).

**(e) No screen-capture / screenshot protection.** UltraCanvas windows have
no equivalent of Android's `FLAG_SECURE`; on X11 none is even possible
(§3.6).

### 2.3 The crypto gap is framework-wide

The authenticator is not the first consumer to need cryptography — it is the
sixth. The framework already has multiple components that require crypto
primitives, and because there is no sanctioned API, each has independently
either hand-rolled one, called OpenSSL directly in violation of the
wrapped-engines rule, or stalled at design stage.

| Consumer | Needs | Current state |
|---|---|---|
| **UCD file format v2** (`Docs/UltraCanvas/UCD-FileFormat-v2.md`) | AES-256-GCM, ChaCha20-Poly1305, Argon2id / PBKDF2-HMAC-SHA256, HKDF, SHA-256, BLAKE3-256 | **Specified in detail; none of the primitives exist.** §4.3 defines the per-section compress→encrypt pipeline, §4.4 the SuperVault remote-authorization record. The format cannot be implemented as written. |
| **UltraCanvasDocument** (v1 doc encryption) | AES-256, PBKDF2, password hashing | Implemented by `#include <openssl/aes.h>` **directly inside a plugin** — a house-rule violation — and the implementation is broken (see below) |
| **AnchorPoint** | SHA-256 file integrity | Hand-rolled `Apps/AnchorPoint/net/Sha256.h`, whose header explicitly says it is a placeholder "when UltraNet/UltraVault bring a vetted crypto surface" |
| **UltraVault** (design) | KDF + AEAD for its file-backed fallback backend, per-platform keyring glue | Design doc only; module does not exist |
| **UltraDatabase** | At-rest encryption | Listed as a Stage 3 item, unstarted |
| **UltraAuthenticator** (this app) | HMAC-SHA-1/256/512, CSPRNG, AEAD, KDF, constant-time compare | Blocked |

**Correction (2026-08-10):** an earlier revision of this document stated that
OpenSSL is "already linked on every platform, so the dependency is paid for".
**That is wrong.** `UltraCanvas/CMakeLists.txt:1407` links `OpenSSL::SSL` /
`OpenSSL::Crypto` only under `ULTRACANVAS_PLATFORM STREQUAL "Linux"`; Windows
links Schannel (`secur32`/`crypt32`) and macOS links `Security.framework`
(SecureTransport). `master_dependencies.yaml` states the policy explicitly —
*"we never call OpenSSL directly … OpenSSL is only an explicit dependency on
Linux"* — and pins `min_version: "1.1.1"`, which predates OpenSSL's Argon2
support (3.2+). Choosing a crypto backend is therefore a real dependency
decision, not a free one; the options and a recommendation are in
[UltraCanvasCrypto](../UltraCanvas/UltraCanvasCrypto.md) §3.

#### Cautionary tale: what the missing API already produced

`UltraCanvas/Plugins/Documents/UltraCanvasDocument.cpp` is the clearest
argument for a shared API. It implements document password protection like
this:

- `EncryptData` / `DecryptData` are guarded by `#ifdef ULTRACANVAS_USE_OPENSSL`.
  **That macro is never defined anywhere in the build system** — no
  `CMakeLists.txt` or `.cmake` file sets it. So the compiled branch is the
  `#else` fallback: `output = input; return true;` — the data is passed
  through in the clear while the function reports success.
- The consequence in the live code paths: `Save()` with a non-empty password
  sets `encryption = UCEncryptionType::AES256`, calls `EncryptData`, gets
  back plaintext, and writes a file whose header claims AES-256
  (`UltraCanvasDocument.cpp:184-193`). `Load()` then requires a password,
  calls `DecryptData`, and succeeds with **any** password at all
  (`:112-124`).
- Even with the macro defined, the OpenSSL path is worse, not better: in
  `EncryptData` the line that appends the ciphertext is commented out, so the
  output contains only the IV and the document body is destroyed; in
  `DecryptData` the output vector is never assigned. Both still `return true`.
- Supporting routines are unsound independently: `GeneratePasswordHash` is a
  single unsalted-KDF round of SHA-256 (a fast hash, not a password KDF) with
  a `std::hash<std::string>` fallback; `GenerateSalt` uses `std::mt19937`
  rather than a CSPRNG; the PBKDF2 call uses 10 000 iterations (well below
  current guidance) and derives the salt from the first 8 bytes of the IV
  instead of an independent random salt.

**Severity note, stated precisely:** `UltraCanvasDocument.cpp` is *not
currently in the CMake source list*, so this is dormant code rather than a
shipping vulnerability, and it should not be reported as an active CVE-class
bug. Its value here is diagnostic: it is precisely the failure mode that a
missing shared crypto API produces — plausible-looking security code, written
once by a non-specialist, that silently does nothing. Before this file is
ever added to the build it must be rewritten against a real API, or its
encryption entry points removed so no caller can believe in them.

---

## 3. Security analysis — drawbacks and holes

Ordered roughly by severity.

### 3.1 Secrets at rest (critical)

The threat: TOTP seeds are *equivalent to the second factor itself*. Anyone
who reads them once can generate valid codes forever. Google Authenticator's
own history is instructive: its Android database was long stored plaintext
(readable by root/backup tooling), and its 2023 cloud-sync feature launched
*without* end-to-end encryption and was widely criticized for it.

Repository reality: UltraVault doesn't exist; UltraDatabase/SQLite is
plaintext; a naive implementation would end up with Base32 secrets in a
world-readable SQLite file or JSON config.

**Required design:**

- Never store seeds in plaintext — not in SQLite, not in JSON, not even
  temporarily. Until UltraVault ships, use an **app-level encrypted store**:
  a single file containing an AES-256-GCM blob, key derived from a user
  master password via Argon2id (memory-hard; PBKDF2-HMAC-SHA256 ≥ 600k
  iterations only as fallback), random salt + nonce per write, version field
  for future migration. (This is the Aegis/andOTP model, both well audited.)
- File permissions `0600`, stored under the per-user data dir.
- When UltraVault lands, keep the same vault file but move the *master key*
  into UltraVault/OS keyring (`libsecret` on Linux hosts, Keychain on macOS,
  DPAPI on Windows, kernel-mediated store on native ULTRA OS) so unlock can
  become transparent or biometric. Designing the storage behind a small
  `ISecretStore` interface now makes that swap trivial and gives ULTRA OS a
  concrete first consumer to drive UltraVault Stage 1.
- Decide explicitly what happens with **no master password**: either refuse
  (recommended default) or store the key OS-keyring-only with a clear
  warning. Silent plaintext fallback is the classic hole — do not ship one.

### 3.2 Secrets in memory (high)

- `std::string`/`std::vector` copies of secrets survive in freed heap pages,
  swap, and core dumps. Add a `SecureBuffer` type (zeroize on destruction via
  `OPENSSL_cleanse`-style memory barrier; `mlock` best-effort) and keep raw
  seeds only inside it. Decode Base32 → bytes once; drop the Base32 string.
- Disable core dumps for the process (`prctl(PR_SET_DUMPABLE, 0)` /
  `setrlimit(RLIMIT_CORE, 0)` on Linux).
- Watch the *framework* copies: `UltraCanvasQRCode` keeps the full content
  string (`content_`) — an `otpauth://` URI **contains the secret**, so a QR
  element used for the export feature retains a plaintext copy for its
  lifetime; clear it as soon as the dialog closes. `QRScanResult::data` and
  any `UltraCanvasTextInput` used for manual entry likewise hold the secret
  in plain strings — clear the input field and discard results promptly.
  `JSONValue` is value-semantic and copies freely — never route seeds
  through it; serialize only the *encrypted* blob.
- Never log secrets or `otpauth://` URIs. Grep-able rule: the string
  `secret=` must never reach any log call.

### 3.3 Untrusted input: QR codes and images (high)

Scanning is parsing attacker-controlled data through a C library:

- **zbar attack surface.** libzbar has had real memory-safety CVEs
  (e.g. CVE-2023-40889 heap overflow, CVE-2023-40890 stack overflow in the
  decoder). It is a system dependency, so track distro patches; on native
  ULTRA OS builds, pin and update it deliberately. The image *loaders* used
  before zbar (`ScanQRCodeFile` → FileLoader → libvips et al.) are additional
  attack surface when the user imports a QR from an arbitrary image file.
- **Strict `otpauth://` validation** after decode: scheme and type
  whitelist (`totp`/`hotp` only), Base32 charset check, secret length bounds
  (≥ 128-bit per RFC 4226 §4, sane upper bound), `digits` ∈ {6,7,8},
  `algorithm` ∈ {SHA1,SHA256,SHA512}, `period` bounds (15–120 s), reject
  URL-embedded control characters in issuer/account labels before they reach
  the UI (a hostile label should not be able to spoof list rows or overflow
  layouts). Reject `otpauth-migration://` payloads unless explicitly
  implementing Google's protobuf migration format.
- **Multi-symbol frames**: zbar reports every code in view. A malicious
  "setup sheet" can put a second QR next to the real one. Take exactly one
  symbol, and always show a **confirmation screen** (issuer + account, never
  the secret) before adding.

### 3.4 Weak or wrong crypto (high if done wrong)

- HMAC-SHA-1 remains cryptographically fine for TOTP (HMAC does not inherit
  SHA-1's collision break) and is required for ecosystem compatibility — but
  implement it via OpenSSL through the wrapper, not hand-rolled (§2.2-a).
  Support SHA-256/512 since issuers increasingly offer them.
- Dynamic truncation (RFC 4226 §5.3) has classic off-by-one/sign bugs —
  ship the RFC test vectors as unit tests.
- Time handling: compute the counter as `floor(unix_utc / period)` in UTC
  only. Clock skew is a *usability* issue (wrong codes), not a reason to
  widen windows — the app only displays codes, it never verifies them, so no
  look-ahead window and no need for constant-time compare in v1. If a
  verify-mode is ever added (e.g. ULTRA OS login MFA), use
  `ConstantTimeEquals` and enforce single-use per time-step (RFC 6238 §5.2).
- If the app ever *generates* seeds (acting as enroller), use the CSPRNG
  (`RandomBytes`), never `std::rand`/`std::mt19937`.
- HOTP: persist the counter atomically *before* showing the code; a crash
  that replays a counter yields reused codes.

### 3.5 The export / backup feature (medium-high)

- "Show account as QR" renders the seed on screen; `ExportToImage`/
  `ExportToSVG` write it to disk as an innocuous-looking picture that will
  outlive the app in `~/Pictures`, sync folders, thumbnails caches. Either
  don't offer file export of provisioning QRs at all, or watermark the flow
  with explicit warnings and point exports at the encrypted format instead.
- Encrypted export file: same AEAD + Argon2id envelope as the store, with
  its own passphrase (not the app master password), so a backup found later
  doesn't fall to the device password.

### 3.6 Platform exposure the app cannot fix (medium, must be documented)

- **X11:** any client of the same X server can capture window contents and
  sniff keyboard input. There is no `FLAG_SECURE` equivalent; screenshot
  tools, screen recorders, and malware all see the codes (30 s exposure) and
  — worse — the provisioning QR during enrolment (permanent secret
  exposure). Prefer the Wayland path for ULTRA OS; on X11, keep provisioning
  QRs on screen as briefly as possible. This is a strong argument for ULTRA
  OS native builds to give the authenticator a "no capture" window hint in
  the compositor.
- **Same-user process isolation:** on a stock Linux host, any process of the
  same user can read the app's files and (via ptrace, unless YAMA restricts
  it) its memory. App-level encryption raises the bar (offline/disk theft,
  backups) but cannot defeat a live same-user attacker — that requires the
  ULTRA OS per-app entitlement model that UltraVault's native backend
  anticipates. State this honestly in the threat model instead of implying
  the vault defeats local malware.
- **Phishing:** TOTP does not resist real-time relay phishing. Out of scope,
  but user-facing docs should not oversell.

### 3.7 Clipboard and UI leakage (medium)

- Copying a *code* is acceptable (30 s lifetime) but: X11 clipboard is
  readable by every app, and clipboard managers persist history. Auto-clear
  the clipboard ~30 s after copy (only if it still holds our value), and
  never offer copy for the *secret*.
- App lock (PIN or the master password) with exponential back-off, and
  auto-lock on minimize/idle. Codes should not be visible on a lock screen
  or in a window-switcher preview.
- Tap-to-reveal (codes hidden by default) is worth offering for
  shoulder-surfing resistance.

### 3.8 Process and dependency hygiene (medium)

- New dependencies (zbar is already in; Argon2 would be new unless the
  OpenSSL 3 `argon2id` KDF provider is available) must go through
  `Docs/Dependencies.md`, `master_dependencies.yaml`,
  `THIRD_PARTY_LICENSES.md` per house rules.
- Keep OpenSSL current (it's already a Tier-1 UltraNet dependency).
- CI: run the RFC vectors and a round-trip encrypt/decrypt fuzz-lite test;
  `scripts/check_ui_reuse.py` will enforce that the account list is built
  from real elements.

---

## 4. Proposed architecture

```
Apps/UltraAuthenticator/
  main.cpp                     — UltraCanvasApplication bootstrap
  AccountListView.*            — container of per-account tiles (elements only)
  AddAccountFlow.*             — camera scan / image file / manual entry + confirm dialog
  otp/
    Totp.*  Hotp.*             — RFC 6238 / RFC 4226 (uses UltraCrypto HMAC)
    Base32.*                   — RFC 4648 decode/encode, strict
    OtpAuthUri.*               — otpauth:// parse + validate (§3.3)
  store/
    ISecretStore.h             — interface (swap point for UltraVault later)
    EncryptedFileStore.*       — AES-256-GCM + Argon2id envelope (§3.1)
    SecureBuffer.h             — zeroizing secret container (§3.2)

UltraCanvas/{include,core}/DataFormats/UltraCrypto.h/.cpp   (new, shared)
  — HMAC-SHA1/256/512, RandomBytes, ConstantTimeEquals, SecureZero,
    Aes256GcmSeal/Open, DeriveKeyArgon2id — OpenSSL behind the API.

UltraCanvas/Plugins/QRCode/
  + QRCodeUtils::ScanQRCodeImage(const UCImage&)             (new overload)
```

Camera scan pipeline: `UltraCanvasVideoRecorder::Open()` (preview only,
`captureAudio=false`, no `Start()`, so nothing touches disk) →
`onPreviewFrame` → downscale/grayscale → `ScanQRCodeImage` → on first valid
`otpauth://` hit: stop preview, validate, confirm, store.

### Suggested build order

1. `UltraCrypto` wrapper (unblocks everything; also retires AnchorPoint's
   ad-hoc SHA-256 eventually).
2. OTP engine + Base32 + URI parser, with RFC test vectors in `Tests/`.
3. `EncryptedFileStore` + `SecureBuffer`.
4. App shell: list + manual entry (usable v0 without any camera work).
5. `ScanQRCodeImage` overload + camera scan flow.
6. Optional: encrypted export/import, app lock, per-account QR display.
7. Later, when UltraVault ships: `UltraVaultSecretStore` backend; on native
   ULTRA OS, secure-window hint for the compositor.

---

## 5. Verdict

**Feasible.** The UI layer, QR generation/decoding, and camera preview are
already in the tree, and the missing OTP logic is small and testable. The two
genuine gaps are **(1) no app-usable crypto API** and **(2) no secure secret
storage**.

Neither is really an authenticator problem — both are **framework
prerequisites that ULTRA OS needs regardless of whether this app is ever
built** (§2.3). The UCD v2 file format already specifies AES-256-GCM,
ChaCha20-Poly1305 and Argon2id and cannot be implemented without them;
UltraVault's fallback backend needs the same primitives; UltraDatabase's
at-rest encryption needs them; AnchorPoint is running on a hand-rolled
placeholder that says so in its own header; and the one component that did
try to ship encryption without a shared API produced code that silently
encrypts nothing.

The recommendation is therefore to treat crypto as a **core service in its own
right**, scheduled ahead of the authenticator rather than as part of it, with
the authenticator as its first proving consumer. That design is now specified
in [UltraCanvasCrypto](../UltraCanvas/UltraCanvasCrypto.md), which also
settles the backend question the correction above raises. Likewise
`ISecretStore` should be defined so UltraVault can slot in behind it later.
The hardest *unfixable-in-app*
issues are X11 screen/clipboard/ptrace exposure — they need to be documented
honestly and ultimately solved by ULTRA OS's per-app isolation and a
secure-window compositor hint, for which this app is an ideal first customer.
