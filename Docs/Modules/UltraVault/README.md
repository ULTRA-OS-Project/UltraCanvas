# UltraVault

**Credential and secret storage for every ULTRA OS app.**
Sibling of `UltraCanvas` (UI), `UltraNet` (networking), `UltraCrypt`
(cryptography), `UltraDatabase` (storage) and `UltraAI` (AI capabilities).

UltraVault is the single system-level home for API keys, tokens, passphrases
and any other secret, so that no application, plugin or sibling module rolls
its own storage. A secret is stored once under a namespaced key and every
consumer — UltraAI resolving a provider key, UltraCloud holding an OAuth
refresh token, a mail or FTP client keeping a password — reads it back through
the same call.

> **Status (v0.1):** the memory backend (CI / ephemeral) and the encrypted-file
> backend are implemented and tested — Argon2id key derivation with the cost
> parameters stored in the file header, XChaCha20-Poly1305 AEAD with that
> header authenticated as associated data, all via **UltraCrypt**. Sources
> under `UltraCanvas/{include,core}/UltraVault/`, target `UltraVault`, header
> `<UltraVault/UltraVault.h>`, `namespace UltraVault`, unit tests in
> `Tests/UltraVaultTests.cpp`. Platform-native backends (libsecret / Keychain /
> Credential Manager), `Import` and `PromptUserForSecret` are planned.
>
> The full design document — why a dedicated module, the backend comparison
> and the key-naming contract — is
> [`UltraAI/Docs/UltraVault.md`](../../../UltraAI/Docs/UltraVault.md).

---

## Why a module

Every mature platform converges on one system service for secrets — Keychain
on macOS, Credential Manager on Windows, the Secret Service (`libsecret`) on
Linux, Keystore on Android — instead of asking each application to invent its
own. Credential storage does not belong inside the consumer module: UltraAI,
UltraNet, file managers, Git tools, package managers and OAuth-using apps all
need the same thing, and each in-app re-implementation is another place to get
key derivation, wiping and file tampering wrong.

Keeping it separate also keeps the migration path open: when ULTRA OS gains a
kernel-mediated secret store, it becomes one more UltraVault backend and the
public C++ surface does not change.

## Backends

| Backend | State | Notes |
|---|---|---|
| `Memory` | implemented | Process-lifetime store, nothing persisted. Used by CI and tests, and the `Auto` fallback when nothing else is configured. |
| `File` | implemented | One encrypted vault file. Argon2id-derived key (parameters in the header), XChaCha20-Poly1305 AEAD through UltraCrypt. |
| `Libsecret` / `Keychain` / `CredMan` | planned | Host keyrings behind the same surface. |
| `Native` | planned | Kernel-mediated ULTRA OS store. |

`Backend::Auto` picks `File` when a vault path is configured (`Config::filePath`
or the `ULTRAVAULT_FILE` environment variable) and `Memory` otherwise.

## Design rules

- **Nothing throws.** Every fallible operation returns `UltraVault::Result`
  (`Success`, `NotFound`, `AccessDenied`, `Locked`, `BackendUnavailable`,
  `InvalidKey`, `IoError`, `Unknown`).
- **No oracle.** A wrong passphrase and a tampered vault file report the same
  `AccessDenied` with the same message, so the file cannot be used to test
  guesses.
- **Secrets do not linger.** `Initialize()` wipes the passphrase in the
  caller's `Config` after adopting it; `Shutdown()` wipes the decrypted store
  and the derived key.
- **Namespaced keys.** `<vendor>.<app>.<purpose>` — e.g. `ai.anthropic.api_key`.

## Public surface

```cpp
#include <UltraVault/UltraVault.h>

UltraVault::Config config;
config.backend    = UltraVault::Backend::File;
config.filePath   = "/home/user/.config/ultraos/secrets.vault";
config.passphrase = passphraseFromUser;      // wiped in place by Initialize

if (!UltraVault::Initialize(config).IsOk()) return;

UltraVault::Put("ai.anthropic.api_key",
                UltraVault::SecretValue::FromString(key));

UltraVault::SecretValue value;
if (UltraVault::Get("ai.anthropic.api_key", value).IsOk()) {
    UseApiKey(value.AsString());
}

for (const std::string& key : UltraVault::List("ai.")) { /* … */ }

UltraVault::Delete("ai.anthropic.api_key");
UltraVault::Shutdown();
```

| Group | Functions |
|---|---|
| Lifecycle | `Initialize()`, `Initialize(Config&)`, `Shutdown()`, `IsAvailable()`, `GetBackendName()` |
| Secrets | `Put(key, SecretValue, SecretAcl = {})`, `Get(key, out)`, `Delete(key)`, `List(prefix = "")` |
| Bootstrap (planned) | `Import(sourcePath)`, `PromptUserForSecret(key, prompt)` |

`SecretValue` carries arbitrary bytes plus a MIME type (`text/plain`,
`application/x-pem`, …). `SecretAcl` records the owning app id, additional
readers and a "requires user presence" flag; the memory and file backends
store it, OS-native backends will enforce it.

## Consumers

- **UltraAI** — `ProviderConfig::apiKeyVaultRef` resolves through
  `UltraVault::Get` when built with `ULTRAAI_USE_ULTRAVAULT` (on by default
  in-tree); see `Docs/Modules/UltraAI/README.md`.
- **UltraCloud** — `VaultSecretStore` keeps cloud account tokens here, falling
  back to a per-app obfuscated file store when UltraVault is not built.

## Third-party dependencies

None of its own: the cryptography comes from the **UltraCrypt** sibling
(libsodium). See [`Docs/Dependencies.md`](../../Dependencies.md).
