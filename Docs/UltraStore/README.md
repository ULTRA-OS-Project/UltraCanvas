# UltraStore

The ULTRA OS app store: one place that lists the UltraCanvas applications,
installs the right build for the machine with the shared libraries it needs,
keeps everything up to date and takes it away again. One program,
`ultrastore`: the window when started plainly, the commands below when given
one.

This page is the design and the record of the decisions behind it. The first
pull request (0.1.0) delivers the core and the command line; the sections
marked *later* name what the following ones add, in order.

## Decisions

The brief's open questions, answered on 2026-10-24:

| Question | Decision |
|---|---|
| What is the unit the store installs? | **Layers.** A *runtime* (the general shared libraries every app runs on: libUltraCanvas and what it loads), *libraries* (specialised shared sets some apps need: VideoFX, the PDF plug-in, OCR) and the *app* itself. The installer checks which layers are already there and installs only the missing ones, then the app. Least storage on the server, least transfer to the machine. |
| Where do packages come from? | **Our own server.** The admin backend keeps the apps and their settings in a database; the packages are zip / 7z files on a server only the store reaches; the catalog is generated from the database and signed. |
| Where does the code live? | `Apps/UltraStore`, with `ultrastore_core` as a static library in v1. |
| Signature and hash primitives | Ed25519 added to UltraCrypt 0.2.0 (framework 0.9.272); the store target **requires** libsodium and SQLite and stops the configure without them. |
| Management | **UltraStore admin backend**, a local application (next pull requests, `Apps/UltraStoreAdmin`), adding and removing apps with every setting the catalog carries: see *The admin backend*. |
| Translations | Creators supply at least one language; the admin backend translates with the server's local UltraAI into every official language and marks what it translated (`source_language`, `machine_translated`); ULTRA OS apps' own localisation folders get the same. |
| Signing key | Generated and kept **in the admin backend's database**, encrypted; the public key reaches the store through the repository and the signed catalog: see *Keys*. |
| UltraInstaller | **To be created.** v1 fixes the seam (`PackageInstaller.h`: `IPackageInstaller`, `IInstallRegistry`, the registry row) and the on-disk layout; the implementation is the second pull request. |
| UltraNet download features | Already there: UltraNet 0.3.0 added Range resume, pause / resume, HTTPS-only redirects, an idle (stall) timeout instead of the whole-transfer one, and headers before the body. The store's download manager uses them and adds nothing of its own. |

What the brief listed as the first catalog's apps, the final name, the
reverse-DNS prefix and the minimum OS versions are **data the admin backend
enters**, not constants of the store: the catalog carries them per app and
per package. The defaults the admin backend proposes are `org.ultraos.<app>`
for ids, and for minimum versions what the framework builds against today
(macOS 14, Windows 10 1809 / 10.0.17763, glibc 2.31 on Linux). The name
stays **UltraStore**.

## The pieces

```
Apps/UltraStore/
  core/                         UltraStoreCore (static, ultrastore_core)
    UltraStoreTypes.h/.cpp      OS, Arch, Target, versions, LocalizedText, Result<T>
    PlatformInfo.h/.cpp         this machine: os / arch / version / glibc / distro / Rosetta
    Catalog.h/.cpp              catalog.json in and out, strict where the store relies on it
    Verifier.h/.cpp             Ed25519 over the catalog bytes, SHA-256 over packages, signing
    PackageResolver.h/.cpp      the package for the machine, the install plan
    CatalogClient.h/.cpp        fetch (UltraNet), verify, sequence, cache, mirrors
    PackageInstaller.h          the UltraInstaller seam (interface only in v1)
  cli/main.cpp                  ultrastore <command>
Tests/UltraStore/               the core's tests (ctest: UltraStoreCore)
Docs/UltraStore/                this page, CHANGELOG.md, changelog.d/
```

The core opens no window and links only the UltraCanvas core library (for
UltraCanvasJSON), UltraNet, UltraCrypt and UltraDatabase, so the command
line, the tests and a headless CI leg use it as it is.

### Platform (`PlatformInfo`)

What the machine is, learned once at start:

| | Linux | Windows | macOS |
|---|---|---|---|
| os / arch | `uname` | `RtlGetVersion`, `PROCESSOR_ARCHITECTURE` | `uname`, `sysctl` |
| version | kernel release | `major.minor.build` | `SystemVersion.plist` |
| variants | `glibc-<version>` (`gnu_get_libc_version`), `rpi` when the device tree says Raspberry Pi, the `ID` of `/etc/os-release` | | |
| canRunX86_64 | x86_64 only | x86_64 only | x86_64, or arm64 with Rosetta installed |

`Satisfies(variant)` answers a package's variant (`glibc-2.31` when the
machine's glibc is at least that, `min-os=11.0`, `rpi`, a distribution id);
`MeetsMinOs(minOs)` answers a package's `min_os`. The targets the catalog can
name go further than the store runs today: `ios`, `android` and `wasm` /
`wasm32` are accepted so the admin backend can fill them in now and those
platforms' stores read them later.

### Catalog (`Catalog`)

One JSON document, `catalog.json`, signed as bytes (`catalog.json.sig`,
Ed25519, hex or raw). `schema` says which reader it needs; a newer one fails
with `SchemaTooNew` and the store says it needs an update. `sequence` is a
counter the admin backend raises on every publish; a client refuses a catalog
whose sequence is below the one it has already accepted (`SequenceRollback`),
so a mirror or a proxy serving yesterday's file cannot roll anyone back.

```json
{
  "schema": 1,
  "sequence": 42,
  "generated": "2026-10-24T12:00:00Z",
  "publishers": [{ "id": "ultra-os-project", "name": "ULTRA OS Project", "url": "https://ultraos.eu" }],
  "components": [
    { "id": "org.ultraos.runtime", "kind": "runtime", "name": "UltraCanvas runtime",
      "releases": [{ "version": "0.9.410", "channel": "stable", "date": "2026-10-20",
        "packages": [
          { "os": "linux",   "arch": "x86_64", "variant": ["glibc-2.31"], "format": "7z",
            "url": "https://pkg.example/runtime-0.9.410-linux-x86_64.7z", "size": 110000000, "sha256": "…" },
          { "os": "macos",   "arch": "arm64", "min_os": "14.0", "format": "zip", "url": "…", "size": 95000000, "sha256": "…" }
        ] }] },
    { "id": "org.ultraos.lib.videofx", "kind": "library", "name": "VideoFX",
      "releases": [{ "version": "0.12.0", "depends": [{ "id": "org.ultraos.runtime", "version": "0.9.410" }],
        "packages": [ … ] }] }
  ],
  "apps": [
    { "id": "org.ultraos.ultravideo", "publisher": "ultra-os-project", "name": "UltraVideo",
      "summary": { "en": "The video editor", "de": "Der Videoeditor" },
      "categories": ["video"], "license": "MIT", "icon": "icons/ultravideo.svg",
      "pricing": { "model": "one-time", "price": "29.90", "currency": "EUR", "trial_days": 30 },
      "updates": { "period": "until", "until": "2027-12-31" },
      "visibility": "listed", "lifecycle": "active", "age_rating": "3",
      "releases": [{ "version": "0.2.1", "channel": "stable", "date": "2026-10-22",
        "depends": [{ "id": "org.ultraos.runtime", "version": "0.9.410" },
                    { "id": "org.ultraos.lib.videofx", "version": "0.12.0" }],
        "packages": [
          { "os": "linux", "arch": "x86_64", "format": "7z", "url": "…", "size": 3000000, "sha256": "…", "entry": "UltraVideo" },
          { "os": "macos", "arch": "universal", "min_os": "14.0", "format": "zip", "url": "…", "size": 6000000, "sha256": "…", "entry": "UltraVideo.app" }
        ] }] }
  ]
}
```

Apps and components share one shape; `kind` tells them apart (`app`,
`runtime`, `library`). A release's `depends` names the **exact** component
versions the build was made against: there is no ABI promise between two
framework versions, so an app built on runtime 0.9.410 gets 0.9.410, and two
runtimes live side by side until the garbage collector finds one unused.
Releases are sorted newest first on read; `channel` is `stable` or `beta`
(the beta channel sees both).

Strict where the store relies on it (ids reverse-DNS and unique, versions,
channel, each package's os / arch / format / absolute http(s) url / 64-hex
sha256, `min_os` a version, pricing with price and currency when not free,
`until` a date), lenient about the rest (a plain string is accepted where a
language map is expected; unknown members are ignored).

### The admin backend's settings

Per app, what the admin backend offers and the catalog carries:

| Setting | Catalog | Values |
|---|---|---|
| Minimum host OS version | `packages[].min_os` | per package, since it differs by OS: `14.0` (macOS), `10.0.17763` (Windows), the kernel on Linux where the `glibc-x.y` variant is the usual bar |
| Processor target | `packages[].arch` | `x86_64`, `arm64`, `armv7`, `riscv64`, `wasm32`, `universal` (a macOS fat binary), `any` (no native code) |
| Operating system | `packages[].os` | `ultraos`, `windows`, `macos`, `linux`, `ios`, `android`, `wasm` |
| Monetary | `pricing.model` | `free`, `one-time`, `monthly`, `yearly`, with `price` (decimal string), `currency` (ISO 4217) and an optional `trial_days` |
| Free update period | `updates.period` | `always`, `never` (the bought version only), `until` with `until: YYYY-MM-DD` |
| Visibility | `visibility` | `listed`, `unlisted` (reachable by id or link: a beta programme, a customer's app), `hidden` (served only to machines that have it, for updates) |
| Lifecycle | `lifecycle`, `successor` | `active`, `deprecated` (installs with a notice and names the successor), `withdrawn` (no new installs; existing ones keep updating) |
| Age rating | `age_rating` | PEGI-like `3`, `7`, `12`, `16`, `18`; empty for everyone |
| Account | `requires_account` | the app is useless without its own user account (UltraMail with no mailbox is not that; a service client is) |
| Source | `open_source`, `source_url` | shown as a badge with the link |
| Links | `homepage`, `support_url`, `privacy_url` | |
| Search | `categories`, `tags` | categories from the fixed list the store's sidebar shows; tags free-form |
| Texts | `summary`, `description`, `releases[].notes` | per language, `en` the fallback; `source_language` names what the creator wrote, `machine_translated` the languages the server's UltraAI added (see *Translations*) |
| Media | `icon`, `screenshots` | relative to the catalog URL, served from the same place |

Pricing is **shown** by the store from 0.1.0 on; the purchase itself (a
payment provider, the licence that the app checks, the receipt) is a later
milestone with its own design, and until then the store installs the free
apps and the trial of a paid one, and sends *Buy* to the publisher's page.

### Translations

The creator writes the texts in at least one language, which the catalog
records as `source_language`. The admin backend translates them into every
official language of the countries the store serves with the **local UltraAI
on the server** at publish time, lists those under `machine_translated`,
and the store shows such a text with an *automatically translated* note and
the original a click away; a translation the creator later supplies replaces
the machine's and leaves the list. For ULTRA OS apps the same service reads
the app's own localisation folder, so the app's strings get the same
treatment as its description. The store's own interface strings are not in
the catalog; they follow the framework's translation facility when it
exists (none does today) and a local table in the app until then.

### Verifier (`Verifier`)

`VerifyCatalogSignature(bytes, signature, trustedKeys)` tries each trusted
key (the active one and the spare) with `UltraCrypt_Verify`; `Sha256HexOfFile`
and the constant-time `PackageHashMatches` check a downloaded package against
the catalog. The admin side is here too, so the command line can stand in for
the admin backend until it exists: `GenerateSigningKey`, `PublicKeyOfSeed`,
`SignCatalog`.

### Keys

- **Who generates the production pair.** The admin backend, on first run:
  `UltraCrypt_GenerateSignatureKeyPair`, the 32-byte seed stored in its own
  database encrypted with a passphrase (Argon2id + XChaCha20-Poly1305, the
  UltraPassword vault's scheme), unlocked for the moment of signing. The
  seed never leaves that machine; a second machine means a second key, which
  the store trusts beside the first. `ultrastore keygen` does the same for a
  development pair.
- **How the public keys reach the store.** Two ways, both needed. The public
  key is committed to the repository (`Apps/UltraStore/keys/<name>.pub`,
  hex) and compiled into `ultrastore`, so a fresh install trusts the right
  key with nothing fetched. And the catalog carries `trusted_keys` (*later*,
  schema 2): the public keys that will sign future catalogs, signed by the
  current one, so a key can be rotated by publishing with the old key once
  and the new key from then on, and a store that has seen that catalog
  accepts the new key before the binary that carries it ships. A key that
  was never announced that way is refused whatever the catalog says.
- **A compromised key** is retired by a release of the store without it;
  the `trusted_keys` list cannot remove a compiled-in key, by design, since
  the catalog is what an attacker with the key controls.

### Resolver (`PackageResolver`)

For a release and a machine, the best package: `exact` (same os and arch,
variants and `min_os` satisfied), then `universal`, then `any`, then
`emulated` (an x86_64 package on an Apple-silicon Mac with Rosetta; Linux
never emulates). `Resolve(app, platform, channel)` walks the releases newest
first and returns the first with a package for the machine, so an older
release still serves a system the newest build dropped, and names the newest
release anyway when none fits, so the store can say *not available for your
system; available for linux/x86_64, macos/universal*.

`Plan(catalog, app, release, platform, installed)` is the install plan:
dependencies first, depth first, each at its exact version, with the ones
already installed marked and not downloaded, the bytes to fetch summed. A
missing component, a missing version, a component with no package for the
machine (unless it is installed already) or a cycle fails with
`DependencyError`; an app with no package for the machine, with
`NotAvailable`.

### Catalog client (`CatalogClient`)

`Refresh()` tries the configured base URLs in order (`<base>/catalog.json`
and `.sig`, `If-None-Match` with the ETag only against the same base),
verifies the signature, parses, checks the sequence, then replaces the
cache atomically (`catalog.json`, `catalog.json.sig`, `state.json`, written
beside and renamed over). A 304 answers from the cache; a failed refresh
leaves the cache untouched, so `LoadCached()` works offline, and verifies
the cached bytes again before trusting them. The fetch is a function
injected at construction: the default is UltraNet with HTTPS-only redirects
and a 64 MB cap, the tests hand in a map.

Cache: `<data root>/UltraStore/cache/` (`UltraCanvasLocalDataFolderRoot()`,
UTF-8 paths through `PathToUtf8` / `PathFromUtf8`).

### Where the server is reached

The packages' server is reachable only by the store. Open design question,
to settle with the admin backend: a bearer token the store carries and the
server checks (simple, rotatable, but in every binary) or unlisted URLs
whose names are only in the signed catalog (nothing to leak from the
binary, and the catalog itself is public). The resolver and the client do
not care: `Package::url` is whatever the catalog says, and
`UltraNetHttpOptions::headers` takes the token if there is one.

## On disk (*later*, UltraInstaller)

The brief's `~/.local/share/ultrastore` predates the framework's rule; the
store follows the rule (`UltraCanvasDataFolder.h`, `UltraCanvasSettingsFolder.h`)
like every other app, with its own folder named `UltraStore` beside
`UltraMail` and `UltraPassword`, not the shared `UltraCanvas` folder (that
one is for settings several apps read together):

| What | Root | Linux | macOS | Windows |
|---|---|---|---|---|
| installed apps and components, the registry, the catalog cache, downloads | `UltraCanvasLocalDataFolderRoot() / "UltraStore"` | `~/.local/share/UltraStore` | `~/Library/Application Support/UltraStore` | `%LOCALAPPDATA%\UltraStore` |
| settings: channel, mirrors, trusted keys | `UltraCanvasSettingsFolderRoot() / "UltraStore"` | `~/.config/UltraStore` | `~/Library/Application Support/UltraStore` | `%APPDATA%\UltraStore` |

The *local* data root because installed binaries are machine-specific and
large: on Windows that is `%LOCALAPPDATA%`, which does not roam with the
profile; elsewhere the local and the roaming root are one folder.
`$XDG_DATA_HOME` wins on every platform, so a test or a portable setup points
the store elsewhere. Per user, never elevated:

```
<root>/                              UltraCanvasLocalDataFolderRoot()/UltraStore
  cache/                             the catalog client's files
  downloads/                         partial and complete packages, resumed by Range
  apps/<id>/<version>/               one folder per installed app version
  apps/<id>/current -> <version>     what the launcher starts (a junction on Windows)
  components/<id>/<version>/         runtimes and libraries, side by side
  registry.sqlite                    IInstallRegistry on UltraDatabase
```

The registry's one table, `installed`:

| column | |
|---|---|
| `kind` | app / runtime / library |
| `id`, `version` | the primary key |
| `folder`, `entry` | where, and what to launch |
| `sha256` | of the package it came from |
| `installed_at`, `channel` | |
| `depends` | JSON, the exact versions it was installed with: what the garbage collector follows |
| `current` | the version the launcher starts |

An install is: resolve, plan, download each missing package to
`downloads/` (UltraNet, `outputFilePath`, `resumeOutputFile`, the stall
timeout, paused and resumed by the user), check its SHA-256, unpack it into
a temporary folder beside its destination and rename it into place, add the
registry row, move `current`. An update is an install of the new version
followed by a `current` move; the old version stays until the garbage
collector removes versions no `current` and no app depends on. Nothing is
ever written where a failure would leave a half-installed folder in place.

## The admin backend (*later*, `Apps/UltraStoreAdmin`)

A local application for the people who publish: adds and removes apps and
components with every setting in the table above, per release the packages
(the admin picks the files, the backend computes size and SHA-256 and
uploads them to the packages server), raises the sequence, generates the
catalog from its database, signs it with the key it keeps, and publishes
catalog and signature. Its database is UltraDatabase (SQLite); its upload
is UltraNet (SFTP or HTTPS PUT, to decide with the server). `ultrastore
sign`, `sha256`, `keygen` and `pubkey` are its stand-ins until it exists,
and its tests will run the same `ParseCatalog` the store runs, so what it
writes is what the store reads.

## The window (*later*)

Pages: Discover, Categories, Installed, Updates, Settings (channel, mirrors,
the keys it trusts, the platform it detected). Every operation goes through
the core, so the window and the command line never disagree.

The widgets the pages need and the framework lacks - a card element with a
real button child, a sidebar navigation, a collapsible bottom panel (the
download queue) and a single-select chip group (the categories) - are
**added to the framework as proper elements**, each with its component doc
under `Docs/UltraCanvas/` and its row in `Masterfile_modules.md`, in a
framework pull request before the window's. Composing them from containers
in the app, as UltraPassword does, was the alternative and is not taken: the
next application would compose them again.

## The command line

```
ultrastore platform                                      what this machine is
ultrastore list <catalog.json> [--channel beta] [--lang de]
ultrastore resolve <catalog.json> <app-id> [--installed <id>@<version> ...]
ultrastore verify <catalog.json> <catalog.json.sig> --key <hex> [--key <hex>]
ultrastore fetch <base-url> --key <hex> [--cache <dir>]
ultrastore keygen | pubkey <seed-hex> | sign <catalog.json> <seed-hex> [-o <sig>] | sha256 <file>
ultrastore --version | --help
```

## Building and testing

```
cmake -S . -B build -DBUILD_ULTRASTORE=ON -DULTRACANVAS_BUILD_ULTRASTORE_TESTS=ON
cmake --build build --target ultrastore UltraStoreCoreTests
(cd build && ctest -R '^UltraStoreCore$' --output-on-failure)
```

The target requires libsodium (UltraCrypt's Ed25519) and SQLite
(UltraDatabase, the registry); without either the configure stops and names
the package to install, or `-DBUILD_ULTRASTORE=OFF`.

## Milestones

1. **0.1.0** core and command line (this page).
2. **UltraInstaller**: download manager on UltraNet, unpacking through
   VirtualFS / libarchive, the registry on UltraDatabase, garbage collection;
   `ultrastore install / remove / update / gc`.
3. **The framework's missing widgets** (card with button child, sidebar
   navigation, collapsible bottom panel, single-select chip group), with
   component docs.
4. **The window.**
5. **The admin backend** (`Apps/UltraStoreAdmin`), the packages server's
   access scheme, key rotation through the catalog (schema 2).
6. **Payment.**
