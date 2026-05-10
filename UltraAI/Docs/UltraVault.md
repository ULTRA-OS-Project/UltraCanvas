# UltraVault — Credential & Secret Storage for ULTRA OS

**Status:** Architectural recommendation. UltraVault module does not yet exist.
**Author:** UltraAI Module
**Last Modified:** 2026-05-08

ULTRA OS needs a single, system-level service for storing API keys,
SSH credentials, OAuth tokens, encryption passphrases, and any other
secret. This document recommends the architecture and captures the
contract UltraAI assumes when looking up credentials.

---

## 1. Why a dedicated module

Every mature platform converges on a **dedicated system service** for
secret storage instead of asking each application to roll its own:

| Platform | Service | Backing |
|---|---|---|
| macOS / iOS | Keychain Services | Per-bundle-id ACL, biometric unlock |
| Windows | Credential Manager | DPAPI (per-user encryption) |
| Linux | Secret Service (`libsecret`) | gnome-keyring / KWallet over D-Bus |
| Android | Keystore | Hardware-backed where available |
| Browsers | Built-in password manager | Sandboxed per origin |

The lesson from 25 years of "every app re-invented this and got it
wrong" is unambiguous: **credential storage does not belong inside
the consumer module**. UltraAI, UltraNet, file managers, Git tools,
package managers, FTP / SSH plugins, OAuth-using apps — all of them
need the same thing. That thing belongs in one place.

---

## 2. Where the surface lives — three viable choices

| Option | Verdict |
|---|---|
| **(A)** Each module reads `apiKey` strings from caller / env var / file | Status quo. Insecure on multi-user systems, no audit log, no rotation, every app re-implements. |
| **(B)** Dedicated `UltraVault` module — sibling of UltraCanvas / UltraNet / UltraAI | **Recommended.** Single responsibility, no circular deps, host-OS keyrings are a backend, native UltraOS implementation can be a different backend behind the same API. |
| **(C)** Folded into `UltraNet` (`UltraNetCredentials` extended) | Too narrow — credentials are needed for non-network secrets too (disk encryption, SSH keys, app license tokens). UltraNet would absorb concerns that aren't networking. |
| **(D)** Direct ULTRA OS syscall (no separate module) | Right *eventual* shape. A separate module is a strict superset because the syscall becomes one of UltraVault's backends; the public C++ API stays stable. |

**(B)** is the recommendation: ship UltraVault now with host-OS
backends, promote the implementation to a kernel-mediated one when
ULTRA OS native builds land, without changing the public surface.

---

## 3. Proposed public surface

```cpp
namespace UltraVault {

enum class ResultCode {
    Success, NotFound, AccessDenied, Locked,
    BackendUnavailable, InvalidKey, IoError, Unknown
};

struct Result {
    ResultCode code = ResultCode::Success;
    std::string message;
    bool IsOk() const { return code == ResultCode::Success; }
};

struct SecretValue {
    std::vector<uint8_t> bytes;        // arbitrary content
    std::string mimeType;              // "text/plain", "application/x-pem", ...
    std::string AsString() const;
};

struct SecretAcl {
    std::string ownerAppId;            // empty -> current app
    std::vector<std::string> readers;  // additional app ids allowed to read
    bool requiresUserPresence = false; // bio / password prompt on every read
};

// Core operations
Result Put(const std::string& key, const SecretValue& value,
           const SecretAcl& acl = {});
Result Get(const std::string& key, SecretValue& outValue);
Result Delete(const std::string& key);
std::vector<std::string> List(const std::string& prefix = "");

// Bootstrap / migration
Result Import(const std::string& sourcePath);
Result PromptUserForSecret(const std::string& key,
                           const std::string& displayPrompt);

// Lifecycle
Result Initialize();
void   Shutdown();
bool   IsAvailable();        // false if no backend reachable

} // namespace UltraVault
```

---

## 4. Naming convention for keys

A predictable, sortable, namespaced format:

```
<vendor>.<app>.<purpose>
```

Examples:

| Key | Purpose |
|---|---|
| `ai.anthropic.api_key` | Anthropic Messages API key |
| `ai.openai.api_key` | OpenAI API key |
| `ai.elevenlabs.api_key` | ElevenLabs TTS key |
| `ai.deepgram.api_key` | Deepgram live STT key |
| `net.cdn.ftps_password` | FTPS password for ULTRA Store CDN |
| `git.github.token` | GitHub PAT |
| `ssh.id_ed25519` | SSH private key (PEM) |
| `disk.fde.recovery_passphrase` | Full-disk-encryption recovery |

Rationale: `List("ai.")` enumerates every AI provider key; ACLs can
be declared per prefix; UI panels can group by leading segment.

---

## 5. Backends per platform

| Build target | Backend |
|---|---|
| ULTRA OS (native) | Kernel-mediated encrypted store with per-app entitlements |
| Linux host | `libsecret` (Secret Service over D-Bus); fallback to file encrypted with PBKDF2-derived key when no daemon is running |
| macOS host | Security framework / Keychain Services |
| Windows host | Credential Manager + DPAPI |
| WASM | `IndexedDB` + Web Crypto (for browser-based UltraCanvas builds) |
| CI / headless tests | In-memory dummy backend |

A single CMake option `ULTRAVAULT_BACKEND={auto, native, libsecret,
keychain, credman, file, memory}` selects per build.

---

## 6. UltraAI integration

UltraAI's `ProviderConfig` already carries both `apiKey` and
`apiKeyVaultRef` (added 2026-05-08). Network adapters implement the
following resolution order:

```
1. ProviderConfig::apiKey            // literal, used verbatim
2. ProviderConfig::apiKeyVaultRef    // UltraVault::Get(...)
3. fail with Error{AuthenticationFailed, ...}
```

A shared helper lives in `UltraAI/adapters/_shared/`:

```cpp
// UltraAI/adapters/_shared/ResolveCredentials.h  (proposed)
namespace UltraAI {
Error ResolveApiKey(const ProviderConfig& cfg, std::string& outKey);
}
```

When `ULTRAAI_USE_ULTRAVAULT=OFF` (default until UltraVault ships),
the helper uses only step 1; step 2 returns `AuthenticationFailed`.
This keeps the test-suite hermetic and lets adapters be written
against the final API today.

---

## 7. Bootstrap UX (out of scope for UltraAI, in scope for ULTRA OS)

The first-time experience is the make-or-break factor for adoption:

* A **Keys & Passwords** settings panel listing every stored secret
  by prefix, with add / edit / delete / rotate.
* A CLI tool — `ultrakeys put ai.anthropic.api_key`, with the value
  read from stdin so it never appears in shell history.
* A **provider-side onboarding flow** in apps that need a key:
  detect missing secret → call `UltraVault::PromptUserForSecret` →
  vault shows a system-modal dialog → app retries.
* Migration helpers from common formats (.env, plist, shell rc).

---

## 8. Why not extend UltraNet's `UltraNetCredentials`

`UltraNetCredentials` is scoped to *network auth schemes* — Basic /
Digest / Bearer / NTLM / Negotiate / OAuth2. Real ULTRA OS use cases
need to store:

* AI provider keys — used as request headers, but never fit cleanly
  into `UltraNetAuthType::Bearer` because each provider has its own
  header name (`x-api-key`, `Authorization: Bearer`, etc.).
* Local-only secrets — disk encryption, BIOS passwords, app license
  tokens. UltraNet has no business with these.
* Long-lived OAuth refresh tokens, with separate access tokens kept
  short-lived.
* SSH private keys — multiple modules consume them (UltraNet's
  SSH plugin, file managers, Git tooling).

UltraNet should *consume* UltraVault when a request specifies
"use named credential X", not own the storage.

---

## 9. Open questions for ULTRA OS leadership

1. Confirm UltraVault as a separate sibling module
   (peer of UltraCanvas / UltraNet / UltraAI).
2. CMake target name: `UltraVault::UltraVault` vs `UltraVault`.
3. Header include style: `<UltraVault/UltraVault.h>`?
4. ACL granularity — per-app, per-user, per-app-and-purpose?
5. GUI ownership — which team builds the "Keys & Passwords" panel?
6. Final form of the canonical secret-name namespace
   (the `ai.anthropic.api_key` example in §4).
7. Threat model — what attacks does UltraVault defend against?
   (Stolen disk, malicious local app, malicious remote, root user.)
8. Disaster recovery — vault export / import format and key
   derivation for lost-device scenarios.

---

## 10. Migration path

Once UltraVault ships:

1. UltraAI adds `ULTRAAI_USE_ULTRAVAULT=ON` and the
   `_shared/ResolveCredentials` helper starts honoring step 2.
2. Real network adapters land — they call the helper and never see a
   raw key.
3. Apps that previously read `OPENAI_API_KEY` from the environment are
   updated to call `UltraVault::Get("ai.openai.api_key")` (or set the
   `apiKeyVaultRef` in `ProviderConfig`).
4. UltraNet adapters in turn migrate `UltraNetCredentials` callers to
   look up by vault key instead of carrying literal passwords.
