# UltraStore

The ULTRA OS app store: one place that lists the UltraCanvas applications,
installs the right build for the machine with the shared libraries it needs,
keeps everything up to date and takes it away again. One program,
`ultrastore`: the window when started plainly, the commands below when given
one.

This page is the design and the record of the decisions behind it. 0.1.0
delivered the core and the command line, 0.1.1 UltraInstaller, the next
release the window; the sections marked *later* name what the following
ones add, in order.

## Decisions

The brief's open questions, answered on 2026-10-24:

| Question | Decision |
|---|---|
| What is the unit the store installs? | **Layers.** A *runtime* (the general shared libraries every app runs on: libUltraCanvas and what it loads), *libraries* (specialised shared sets some apps need: VideoFX, the PDF plug-in, OCR) and the *app* itself. The installer checks which layers are already there and installs only the missing ones, then the app. Least storage on the server, least transfer to the machine. |
| Where do packages come from? | **Our own server.** The admin backend keeps the apps and their settings in a database; the packages are zip / 7z files on a server only the store reaches; the catalog is generated from the database and signed. |
| Where does the code live? | `Apps/UltraStore`, with `ultrastore_core` as a static library in v1. |
| Signature and hash primitives | Ed25519 added to UltraCrypt 0.2.0 (framework 0.9.272); the store target **requires** libsodium and SQLite and stops the configure without them. |
| Management | **UltraStore admin backend**, a local application (`Apps/UltraStoreAdmin`, its own README), adding and removing apps with every setting the catalog carries: see *The admin backend*. |
| Translations | Creators supply at least one language; the admin backend translates with the server's local UltraAI into every official language and marks what it translated (`source_language`, `machine_translated`); ULTRA OS apps' own localisation folders get the same. |
| Signing key | Generated and kept **in the admin backend's database**, the seed sealed with a passphrase (Argon2id, XChaCha20-Poly1305); the public key reaches the store through the repository and the signed catalog: see *Keys*. |
| UltraInstaller | **Created** (0.2.0): `Downloader`, `SqliteRegistry`, `ArchiveInstaller`, `StoreEngine` behind the seam `PackageInstaller.h` fixed in 0.1.0; see *UltraInstaller* below. |
| The services' databases | **PostgreSQL in production, SQLite for tests and development only** (decided 2026-11-10). The payment service and the operator tier's marketplace service run on PostgreSQL; SQLite is what the tests and a developer's machine use, behind the same database layer, and never a production store. The files on a user's or a merchant's own machine (`registry.sqlite`, `admin.sqlite`) are UltraDatabase files, not servers, and stay as they are. |
| Who checks for updates? | **One App manager for the whole machine, never the apps** (decided 2026-11-11). ULTRA OS keeps network traffic down, so one program asks the server: it fetches the one signed catalog and compares it with the registry for every installed app, runtime and library at once. An application has no update check, no "check for updates" menu item and no update server of its own, and contacts no server to learn its latest version. **The scheme:** a separate **App manager** application (to be built, on this page's core: `CatalogClient`, `SqliteRegistry`, `StoreEngine`) owns the update check, the updates and the list of what is installed; it is later integrated into the OS manager / settings (UOS-Settings). Until it exists, UltraStore's Updates page and `ultrastore update` do the job with the same engine; the store itself stays the place to discover, buy and install. See *The App manager (later)*. |
| How does the store reach a machine, and how is it updated? | **As one of its own apps** (2026-11-11). UltraStore is installed into the tree it manages (`apps/org.ultraos.ultrastore/<version>/`, its runtime under `components/`), so the first install and every update are the engine's ordinary Install and Update. A small **setup program**, `ultrastore-setup`, does the first install; the store updates itself from then on. Both read the **store's own catalog**, `ultrastore.json`: the store's entry and what it runs on, signed with the same keys, at a schema that stays put - so a store too old to read `catalog.json` still finds its update. See *The store installs and updates itself*. |
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
    PackageInstaller.h          the UltraInstaller seam: IPackageInstaller, IInstallRegistry, InstalledRecord
    Downloader.h/.cpp           a package onto the disk through UltraNet, resumed, hash-checked
    Registry.h/.cpp             SqliteRegistry: what is installed, on UltraDatabase
    Installer.h/.cpp            ArchiveInstaller: unpack through VirtualFS, the tree, the collector
    StoreEngine.h/.cpp          install / update / remove / gc for the command line and the window
    SelfUpdate.h/.cpp           the store as one of its own apps: its own catalog, the version it runs from, install-or-update
    Launcher.h/.cpp             the entry in the system's application menu for an installed program
    StoreQuery.h/.cpp           what the pages list: the catalog joined with what is installed, filtered
    AssetCache.h/.cpp           icons and screenshots, fetched once into the cache
    StoreSettings.h/.cpp        channel, language, servers, trusted keys, the window's size (UltraCanvasAppSettings)
    UltraStoreBuiltin.h.in      -> the compiled-in keys (keys/*.pub) and servers (ULTRASTORE_SERVERS)
  cli/Commands.h/.cpp           ultrastore <command>
  setup/main.cpp                ultrastore-setup, the first installer: the core and no window
  ui/                           the window: StoreWindow (shell, pages, details), StoreJobs (the
                                worker thread and the Downloads panel), StoreSettingsPage, StoreTheme, main
  keys/                         the public keys a build trusts out of the box (README.md there)
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
  "schema": 2,
  "sequence": 42,
  "generated": "2026-10-24T12:00:00Z",
  "trusted_keys": ["0415eff8…fba970e", "9c2b…"],
  "payment_url": "https://store.example",
  "licence_key": "e52d4fca…40c1ab",
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

`trusted_keys` (schema 2) lists the public keys that may sign the catalogs
after this one: see *Keys*. A schema 1 catalog has none and is read as
before.

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
| Featured | `featured` | the admin's pick: the store's Discover shows the newest featured app as its hero and the featured apps as *Editor's picks* |
| Links | `homepage`, `support_url`, `privacy_url` | |
| Search | `categories`, `tags` | categories from the fixed list the store's sidebar shows; tags free-form |
| Texts | `summary`, `description`, `releases[].notes` | per language, `en` the fallback; `source_language` names what the creator wrote, `machine_translated` the languages the server's UltraAI added (see *Translations*) |
| Media | `icon`, `screenshots` | relative to the catalog URL, served from the same place |
| Codes | `codes[]` (catalog-wide) | the discount and affiliate codes in force: see *Affiliates and codes* |

Pricing is **shown** by the store from 0.1.0 on; the purchase itself is
milestone 7, with its own design (*Payment*), and until then the store
installs the free apps and the trial of a paid one, and sends *Buy* to the
publisher's page.

### Payment

Decided: the provider is **Paysafe, with Skrill Quick Checkout** as the
checkout. The buyer pays on Skrill's hosted page (cards, Skrill wallet,
the local methods Quick Checkout offers), the store never sees a card
number, and Skrill's server-to-server status report is what is trusted,
not the browser's return.

**The payment service** (`Server/UltraStorePay`, its own README) holds the
merchant credentials - never the store binary, never the catalog - and
does the purchase:

| Step | Who | What |
|---|---|---|
| 1 | the store | `POST /v1/checkout {app, code}`: an order priced from the same signed catalog the stores read, with the code in force applied the way the store showed it (*Affiliates and codes*) |
| 2 | the store | opens the order's `checkout_url` in the browser: the Quick Checkout form, submitted to `pay.skrill.com` with the order as `transaction_id` |
| 3 | Skrill | posts the status report to the service, signed with the secret word (`md5sig`); the service checks the signature, the amount and the currency; status 2 pays the order, issues the licence and numbers the receipt - a repeat changes nothing, a forgery is refused |
| 4 | the store | polls the order with its secret until it is paid, keeps the licence, and fetches the paid app's package through a signed URL that lives ten minutes (`restricted/`, *Where the server is reached*) |
| 5 | the admin backend | fetches the sales (`GET /v1/sales?since=`) and records each in its ledger with the partners' credits, one level up and no further |

**The licence** is `ULIC1.<payload>.<signature>`: the licence id, the app,
the order, the day and what the purchase includes, signed Ed25519 with the
service's licence key, whose public half is `GET /v1/keys`; an app
verifies it offline. **The receipt** is numbered per paid order.

**What the purchase includes** is the app's update period as the catalog
had it when the order was paid (`updates` in the payload: `always`,
`never` for the bought version only, `until` with `updates_until`). The
store holds to it: an update to a release dated after the purchase's
terms is refused by the engine ("not in your purchase"), the card and the
Updates page show *Buy update* instead of *Update*, and *Update all*
counts it as "to buy anew"; the new purchase replaces the licence, with
the terms the catalog has then. The details page's *Bought* badge names
the terms.

**The licence reaches the app** as a file, `<root>/apps/<id>/licence.ulic`
(the token, one line), written when the app is installed and when it is
bought while its trial is installed, removed with the app's folder. The
app finds it from its own executable and checks it with the licence key it
compiles in (`Purchase.h`):

```cpp
#include "Purchase.h"   // Apps/UltraStore/core
const std::string file = UltraStore::FindLicenceFile(UltraCanvas::PathToUtf8(executable));
const auto licence = UltraStore::ReadLicenceFile(file, kLicenceKeyHex);
const bool unlocked = licence && licence.Value().app == "org.example.app";
```

`FindLicenceFile` looks in the executable's folder and up to three above
it (the version folder, then `apps/<id>/`); `ReadLicenceFile` fails
`NotFound` without a file and `SignatureInvalid` for a token another key
signed. `ultrastore licence <app-id> [--key <hex>]` shows one purchase and,
with the key, verifies the file the way the app does.

**The catalog names the service**: `payment_url` (the service's base,
`https://store.example`) and `licence_key` (the public half of its licence
key, hex) at the top level, from the admin backend's `payment.url` and
`payment.licence_key` settings; both are optional within schema 2, and a
catalog without `payment_url` sends *Buy* to the publisher's page as
before. With them, the store's *Buy* is the five steps above: the order
asked for with the code in force, the browser opened on the checkout, the
order polled every five seconds for up to half an hour (the download panel
shows the wait; *Cancel* leaves the order open on the service and nothing
is installed), the licence verified against `licence_key` and kept in the
registry (`licences` in `registry.sqlite`: the app, the licence, the
order and its secret, the amount, the receipt number), and the app's
restricted package fetched through the signed URL whenever it is
installed or updated. The order is remembered in the settings until it is
over: a store closed during the payment asks the service about it at the
next start (within two days) and, if it is paid meanwhile, keeps the
licence; if it is still open, the browser opens on the checkout again and
the wait resumes. A bought app shows a *Bought* badge, *Install*
instead of *Buy*, and on its details page the receipt number, the day and
a *Receipt* button that opens the receipt in the browser, as a page in
the look of the service's other pages (the service's README, *The
pages*).
`ultrastore buy <catalog.json> <app-id> [--code <CODE>]` does the same from
the command line, and `ultrastore licences` lists what was bought.

A paid app's **trial** installs the same package, so an app with a trial
keeps its package public (the licence gates what the trial lacks) and only
an app without one puts its package under `restricted/` (`package add
--access`); a restricted package with a trial would make *Try* fail with
"buy it first".

On the admin's side, `sale sync` pulls the paid orders from the service
(`GET /v1/sales?since=`, the `payment.token` setting or
`ULTRASTORE_PAY_ADMIN_TOKEN` as the bearer token) and records each once in
the ledger with the partners' credits; the sequence it reached is kept in
`payment.sales_seq`, so a repeat records nothing twice.

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

### Affiliates and codes

A partner brings buyers with a **code** - typed into the store's Discover
page, or carried by the partner's **QR code** and link - and earns a share
of each sale made with it; a **discount code** without a partner takes
something off the price. The scheme is decided as follows.

- **Two tiers and no more.** An affiliate recorded as recruited by another
  credits that recruiter a *recruit share* of each sale made with the
  affiliate's codes, once, one level up. The recruiter's own recruiter earns
  nothing from those sales: a referral bonus for bringing a partner in, not
  a pyramid. `CreditsFor` in the admin backend is the one place the rule
  lives, and its test shows the third level getting nothing.
- **What is public.** The catalog carries `codes`: the code, its kind
  (`discount` or `affiliate`), what it takes off (`discount_percent` or
  `discount_amount` with `currency`), the apps it applies to (none listed:
  every app), its validity (`valid_from`, `valid_until`) and, for an
  affiliate code, the partner's id - an opaque handle. A code is made to be
  passed around, so listing it costs nothing; what a partner earns, their
  contact and who recruited them stay in the admin database and never
  reach the catalog.
- **In the store.** `CheckCode` tells whether a code is known and in force
  today and what it is for; `DiscountedPrice` and `PriceLabel` show the
  price with it ("23.92 EUR (was 29.90)"), to the cent, never below 0.00,
  only in the app's currency. The code entered is kept in the settings
  (`code.value`, `code.enteredAt`) and stays **in force for 30 days**
  (`StoreSettings::kCodeDays`): a buyer who arrived through a partner's
  link and buys within that time credits the partner. `ultrastore code
  <CODE>` does the same from the command line; `code clear` forgets it.
- **Links.** `ultrastore://code/<CODE>` opens the store with the code and
  `ultrastore://app/<id>?code=<CODE>` an app's details with it; the store
  registers the scheme (`MimeType=x-scheme-handler/ultrastore`), and a
  second start hands the link to the running copy. The partner's QR code
  encodes `https://<catalog base>/go/<CODE>`, a page the publish folder
  carries (`go/<CODE>/index.html`, so the extension-less link is served by
  any static web server: what the code gives and the `ultrastore://`
  link), so a phone that scans it lands somewhere useful; a future partner
  scans `https://<catalog base>/join/<affiliate>` (`join/<affiliate>/index.html`:
  who invites, what a partner earns, the id to name) and names the
  recruiter when the admin signs them up.
- **A partner may use their own code.** Nothing distinguishes a partner's
  purchase from any other, and the share they earn on it is theirs by the
  same rule; the admin backend does not try to tell them apart.
- **The redemption** - the purchase, a limit on uses, the payout - is the
  payment service's (milestone 7, *Payment*). Until it reports sales, the admin
  backend records them by hand (`sale add`) and its report says what each
  partner is owed, so the scheme works from the first sale.

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
  key with nothing fetched. And the catalog carries `trusted_keys` (schema
  2): the public keys that may sign the catalogs after it, signed along
  with everything else. `CatalogClient` keeps the list of the last accepted
  catalog in its cache (`state.json`, `announcedKeys`) and verifies the
  next one against the compiled-in keys, the ones the person added in the
  settings, and that list (`KeysTrustedNow()`). So a key is rotated by
  publishing once with the old key and the new one listed - the admin
  backend lists every active key on every publish - and with the new key
  from then on: a store that has seen that catalog accepts the new key
  before the binary that carries it ships. A key that no accepted catalog
  announced is refused whatever the catalog it signs claims: an
  announcement only counts from a catalog already trusted, and each
  accepted catalog's list replaces the previous one (a schema 1 catalog
  announces nothing).
- **A compromised key** is retired by a release of the store without it;
  the `trusted_keys` list cannot remove a compiled-in key, by design, since
  the catalog is what an attacker with the key controls. The announced
  keys live in the cache folder beside the catalog they came from, with the
  same trust as the settings file next to it.
- **Two signing roles once the operator tier is built** (*The operator
  tier*). Today one key does everything, because one publisher is the
  operator. In the marketplace the roles split, and the store must not
  confuse them:
  - **The operator's key signs the catalog.** It is the only kind of key
    the store trusts for `catalog.json.sig`: compiled in, announced in
    `trusted_keys`, rotated and retired as above. A merchant's key never
    appears in `trusted_keys` and never signs a public catalog; a catalog
    signed with one is refused like any unknown key.
  - **A merchant's key signs submissions.** The pair the admin backend
    generates (*Who generates the production pair*) stays on the
    merchant's machine and signs what `submit` sends to the marketplace
    service: the component object, the package hashes, the media. The
    service checks the signature against the public half the merchant
    registered when the shop was verified, keeps it on the submission, and
    the review reads it as proof of who sent the build. The merchant's
    `publish` still signs that merchant's own private or test catalog with
    the same key; such a catalog is trusted only by a store whose settings
    name the key, never by the public store.
  - **The packages carry no signature of their own**, in either role: a
    package is what its SHA-256 in the signed catalog says it is, and the
    catalog's signature covers the hash. A merchant's signature on a
    submission proves origin to the operator; the operator's signature on
    the catalog proves the result to the store.
  - A merchant's key is retired at the service (the operator marks it,
    the merchant registers a new public half); nothing in the store
    changes, since the store never trusted it.

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
the cached bytes again before trusting them. The keys the accepted catalog
announced are kept in `state.json` and trusted for the next one (*Keys*). The fetch is a function
injected at construction: the default is UltraNet with HTTPS-only redirects
and a 64 MB cap, the tests hand in a map.

Cache: `<data root>/UltraStore/cache/` (`UltraCanvasLocalDataFolderRoot()`,
UTF-8 paths through `PathToUtf8` / `PathFromUtf8`).

### Where the server is reached

Decided with the admin backend (its README, *Decisions*): the catalog, the
media and the packages of free apps are plain public files - what a signed
catalog needs and no more - under `packages/`; the packages of paid apps go
to `restricted/`, which the web server serves to nobody directly. A buyer's
store gets a short-lived signed URL for a restricted file from the payment
service (milestone 6), the way the other stores do it; no bearer token in
the binary, so there is nothing to leak, and a leaked URL dies in minutes.
The resolver and the client do not care: `Package::url` is whatever the
catalog says, and a signed URL is a URL.

## UltraInstaller

The part that turns a plan into installed software, behind the interfaces of
`PackageInstaller.h`:

- **`Downloader`** - one package at a time into `downloads/<sha256>.<format>`:
  UltraNet streams it (`outputFilePath`, no whole-transfer timeout, the
  stall timeout, HTTPS-only redirects), a `.part` left by an interrupted run
  is continued with a Range request, the finished file is checked against
  the catalog's SHA-256 and renamed into place, a mismatch deletes it. `Pause`,
  `Resume` and `Cancel` act on the running transfer from any thread; a
  complete file from an earlier run is reused without a request.
- **`SqliteRegistry`** - `registry.sqlite` on UltraDatabase, the table below;
  refuses a database a newer store wrote (`SchemaTooNew`).
- **`ArchiveInstaller`** - zip, 7z and tar.* through the framework's
  VirtualFS bridge (libarchive), an AppImage copied; into
  `<kind>/<id>/<version>.installing` beside the destination, renamed into
  place when complete, so a failure leaves nothing half installed; the
  entry made executable (a zip made on Windows records no execute bit);
  `apps/<id>/current` a relative symlink to the version, or a file naming it
  where no symlink can be made. `Remove` takes a version away and moves
  `current` to the newest remaining one; `CollectGarbage` removes every app
  version that is not current and every component no current app needs,
  following the exact dependencies each row was installed with.
- **`StoreEngine`** - `Install` resolves, plans against the registry, fetches
  and unpacks each missing step and deletes the download once unpacked;
  `Update` installs the newer release, removes the older app versions and
  collects; `UpdateAll` does that for every installed app (the only update
  check on the machine: the apps make none of their own, and the App manager
  will be its caller, see *Decisions*); `Remove` takes
  every version of an app and collects. Callbacks report each step, the
  bytes of its download and the files of its unpacking.

## The store installs and updates itself

UltraStore is one of the apps in its own catalog, `org.ultraos.ultrastore`,
and lives in its own tree like the rest:

```
<root>/apps/org.ultraos.ultrastore/<version>/...   the store, one folder per version
<root>/apps/org.ultraos.ultrastore/current         -> the version in use
<root>/components/<runtime id>/<version>/...       what it runs on, shared with the other apps
<root>/cache/self/                                 its own catalog, kept apart from the catalog's cache
```

So putting it on a machine is `StoreEngine::Install` and moving it on is
`StoreEngine::Update`; `SelfUpdate.h` adds the three things that make that
work for the program doing it.

**The store's own catalog.** `ultrastore.json` with `ultrastore.json.sig`
sits beside `catalog.json` on every server. It is a catalog - the same
format, the same parser, signed with the same keys, with its own sequence
and cache - cut down to the store's app entry and the components its
releases depend on (`ExtractSelfCatalog`); the admin backend's `publish`
writes it whenever the catalog lists the store, and `ultrastore
self-catalog` cuts it by hand. **Its schema stays at 2**
(`kSelfCatalogSchema`), whatever `catalog.json` has moved to: members are
added, never changed, so every store from this one on reads it. That is
what it is for. A `catalog.json` with a newer schema cannot be read by an
older store (`SchemaTooNew`), so it cannot tell that store where its update
is; this file can. Should schema 2 ever become impossible to keep, the new
format gets a new file name and `ultrastore.json` goes on serving the
store that can read the new one.

**The first install: `ultrastore-setup`.** A console program built from the
core and nothing of the window. It fetches the store's own catalog from the
servers the build names (`ULTRASTORE_SERVERS`), checks it against the keys
the build trusts (`keys/*.pub`), installs the store and its runtime into
the per-user store folder, writes the application-menu entry and starts
the store. Run again, it updates an installed store or does nothing.

```
ultrastore-setup [--check] [--channel beta] [--root <dir>] [--server <url>] [--key <hex>]
                 [--catalog <ultrastore.json>] [--no-menu] [--no-start]
```

It links what the core links. With the shared UltraCanvas library of the
shipping configuration that library travels beside it; a static
configuration (`-DULTRACANVAS_BUILD_SHARED=OFF`) gives one file of about
3 MB on Linux x86_64 (the store itself is about 63 MB there) that needs
only the system's network, archive, crypto and database libraries, not the
UI's: on Linux it is linked `--as-needed`.

**The version that is running.** A store started from the tree names its
own version to the installer (`ProtectRunningInstall`,
`ArchiveInstaller::SetInUse`). That version is never removed and the
collector keeps it and what it was installed with, so an update unpacks the
new version beside it, moves `current`, and the running copy keeps its
files; removing the store from inside itself answers `InUse` and removes
nothing. The next start, from the new version, takes the old one away
(`RemoveOldStoreVersions`). An update done by another program - the setup
program run again, `ultrastore self-update` from elsewhere - leaves the
version that was current in the same way, since a window may be running
from it.

**In the window.** A refresh that fails with `SchemaTooNew` starts the
*Update UltraStore* job by itself; Settings has the same job as a button;
and the store is also an ordinary row of the Updates page whenever the
catalog lists a newer release of it. After any of them, when the current
version is no longer the one the window runs from, the window offers to
restart: it closes, releases its single-instance lock and starts the new
version. When the servers offer no store newer than the running one and
the catalog is still too new, it says so. None of this is a second update
check: the store's own catalog is fetched only when the catalog could not
be read or the person asks, in keeping with *Who checks for updates?*.

**A store that is not in the tree** - from a distribution package, an MSIX,
a build folder - is not managed: Settings says so, *Update UltraStore* only
reports what the servers have, and it is updated the way it was installed
(or replaced by running `ultrastore-setup`).

**The menu entry** (`Launcher.h`), per user: a desktop entry under
`~/.local/share/applications` on Linux, a shortcut in the Start menu's
Programs folder on Windows, a link in `~/Applications` to the bundle on
macOS. It names the version folder (`current` is a plain file where no
symlink can be made), so it is rewritten after an update - only when it is
there: someone who ran the setup with `--no-menu` or deleted the entry gets
none back.

Not done yet, in the order they matter:

- **The store's package and the runtime's are not built by anything in the
  repository.** The admin has to import a build of the store as
  `org.ultraos.ultrastore` with its `entry`, depending on the runtime
  component, before `publish` has an `ultrastore.json` to write. Until then
  the servers have none, and the setup program and the self-update report
  that the file could not be fetched.
- **The Windows shortcut and the macOS link are written but have not been
  run**: the tests run the Linux entry. The engine and the store's own
  catalog are platform-neutral and tested.
- **The other installed apps still get no menu entry.** `WriteLauncher`
  takes any program; filling the menus for every app is the App manager's.
- **One file for the download.** A static build gives one; the shipping
  shared configuration needs the library beside the setup program, so the
  download there is a small archive or a platform installer around it.

## On disk

The brief's `~/.local/share/ultrastore` predates the framework's rule; the
store follows the rule (`UltraCanvasDataFolder.h`, `UltraCanvasSettingsFolder.h`)
like every other app, with its own folder named `UltraStore` beside
`UltraMail` and `UltraPassword`, not the shared `UltraCanvas` folder (that
one is for settings several apps read together):

| What | Root | Linux | macOS | Windows |
|---|---|---|---|---|
| installed apps and components, the registry, the catalog cache, downloads | `UltraCanvasLocalDataFolderRoot() / "UltraStore"` | `~/.local/share/UltraStore` | `~/Library/Application Support/UltraStore` | `%LOCALAPPDATA%\UltraStore` |
| settings: channel, language, servers, trusted keys, the window's size | `UltraCanvasAppSettings("UltraStore")`: the framework's one place for an application's preferences, `<settings root>/UltraCanvas/UltraStore/settings.json` | `~/.config/UltraCanvas/UltraStore/settings.json` | `~/Library/Application Support/UltraCanvas/UltraStore/settings.json` | `%APPDATA%\UltraCanvas\UltraStore\settings.json` |

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
| `depends` | the exact versions it was installed with, `id@version` separated by spaces: what the garbage collector follows |
| `current` | the version the launcher starts |

An install is: resolve, plan, download each missing package to
`downloads/` (UltraNet, `outputFilePath`, `resumeOutputFile`, the stall
timeout, paused and resumed by the user), check its SHA-256, unpack it into
a staging folder beside its destination and rename it into place, add the
registry row, move `current`, delete the download. An update is an install
of the new version followed by a `current` move, the older app versions
removed and the components only they needed collected. Nothing is ever
written where a failure would leave a half-installed folder in place.

## The admin backend (`Apps/UltraStoreAdmin`)

The program the people who publish run; its own page is
[`Docs/UltraStoreAdmin/README.md`](../UltraStoreAdmin/README.md). It keeps
the apps and components with every setting of the table above (each as the
catalog's own component object, so what it keeps is what the store reads),
the publishers, the files behind the packages under their hash, and the
signing key with its seed sealed by a passphrase; it translates the texts
with UltraAI into every official language; and `publish` builds the
catalog with the next sequence, lays the packages and the media out beside
it, signs the exact bytes and verifies them with this store's own
`ParseCatalog` and `VerifyCatalogSignature` before recording the sequence.
`ultrastore sign`, `sha256`, `keygen` and `pubkey` remain as the hand
tools. Its upload to the host is a later step; the publish folder is the
server's document root until then.

## The window

How the window and the web shop look is drawn in
[`design/README.md`](design/README.md): the Discover page, an app's page,
the category browser and the phone views, with every colour, size and font.

`ultrastore` started plainly (or with `--catalog`, `--root`, `--channel`)
opens the window; `Apps/UltraStore/ui/`. It follows the UltraStore shop
design (the canvas *UltraStore Shop Design*, 2026-11-05): a white ground,
ink text, the UltraStore blue of the app icon as the one accent, Manrope
for display text over Inter for body text (both in `media/fonts`), 20 px
card corners, pill buttons and chips (`StoreTheme.h`). A header carries
the wordmark (the way home), the sections as pills - Discover, Categories,
Installed, Updates with its count - the search pill, Refresh and Settings
(where the design has *Sign in*: the store has no accounts). The download
queue sits in a collapsible panel along the bottom
(`UltraCanvasCollapsiblePanel`), a status line under it. Every operation
goes through the core - the same `StoreEngine`, `CatalogClient`,
`PackageResolver` the command line uses - so the window and the command
line never disagree; the pages are `StoreQuery::ListApps` with its filters.

| Page | Shows | Does |
|---|---|---|
| Discover | the featured app as a hero (the admin's newest `featured` pick this machine can install, else the newest release), the categories as pills, *Editor's picks* as cards (the `featured` apps, when there are any), *New and updated* as a numbered list in two columns, *Free essentials* as cards, the four reasons for the store, the partner / discount code; text in the header's search pill turns the page into the results, as cards | Install / Try free / Buy, Open, Update, Remove on each card or row; a card, a row or the hero opens the app page; a category pill opens Categories on it |
| Categories | the filters on the left - the catalog's categories as chips (`UltraCanvasChipGroup`), All / Free / Paid, open source only, hide installed - the heading with the count and the sort order (newest release, name, price), and the cards in a wrapping grid | the same actions; the design's *Runs on* filter is not there: a native store resolves every app for this machine |
| Installed | the installed apps as cards, then the runtimes and libraries under them | Open, Remove, Reload, Clean up (the garbage collector) |
| Updates | the installed apps a newer release has a package for; the count in the header pill | Update one, Update all |
| App page | the shop's app page: the breadcrumb (the section it came from, the category, the name), the icon, the name, the summary, the publisher (a link when the catalog has their page), the facts as pills (the trial, open source and the licence, the systems, the age rating, the account, the update period, a purchase, a code in force), the actions, what this machine downloads (the plan: the layers already there, the ones added, the bytes); the screenshots side by side; *About this app*; *What's new* with the release notes and the version history; *What it installs* step by step with the sizes; the information column (publisher, licence, price, trial, updates, languages, age rating, the links); *More from* the publisher | the same actions; the breadcrumb is the way back |
| Settings | channel, the texts' language, the servers, the keys trusted beside the compiled-in ones, fetch at start; this machine, the store folder, the settings file, the catalog's state, what is installed | Save, Revert, Refresh catalog now, Clean up, Reload installed |

Not in the window, of the design: *Sign in* (no accounts), the share and
wish-list buttons of the app page, and the footer's links (for publishers,
support, privacy, terms: the catalog carries no store-wide pages). The
phone artboards are a web shop's; the admin and operator artboards belong
to UltraStoreAdmin and to a proposal.

What is shown follows the admin backend's settings (`StoreQuery`): a
*listed* app always; an *unlisted* one when it is installed or searched for
by its exact id; a *hidden* one only when it is installed (it is served for
updates); a *withdrawn* one only when it is installed. An installed app is
always shown, whatever the catalog says now, so it can be updated and
removed; one the catalog no longer knows is listed by its id under
Installed, with Open and Remove.

**Jobs.** Install, Update, Update all, Remove, Clean up, Refresh and Reload
line up and run one at a time on a worker thread; the engine and the client
are touched by the worker only while a job runs and by the UI thread only
while none does. Each visible job is a row of the Downloads panel with its
progress bar (`UltraCanvasGaugeDiagramElement`, LinearBar; indeterminate
while the size is unknown), Pause / Resume (the downloader's, from any
thread) and Cancel (a waiting job is dropped; a running one stops at the
next chunk or file, and nothing is left half installed). Progress reaches
the window through `UltraCanvasApplication::PostToUIThread`; the worker
never touches an element. Closing the window while a job runs asks first.

**Pictures.** An app's icon and screenshots are URLs relative to the
catalog's (`ResolveCatalogUrl`); `AssetCache` fetches each once into
`cache/assets/<sha256 of the url><extension>` on a second thread and the
cards get them when they arrive; offline, from the cache.

**A local catalog.** `ultrastore --catalog <catalog.json>` shows that file
instead of the servers' (no signature check, Refresh does nothing), with
`--root <dir>` for a store folder of its own: how the window is tried
against a catalog served from `python3 -m http.server`, and how a creator
previews an entry before it is published.

**What a build trusts.** The production server is the default of
`ULTRASTORE_SERVERS` (`https://store.ultraos.eu/v1`) and its signing key's
public half is `Apps/UltraStore/keys/publish.pub`, so a store built from
the repository finds the catalog with nothing entered. A build with both
emptied (`-DULTRASTORE_SERVERS=""`, no `.pub`) has Discover say so and
point at Settings, where a server and a key are entered by hand.

## The command line

```
ultrastore [--catalog <catalog.json>] [--root <dir>] [--channel beta]   the window
ultrastore platform                                      what this machine is
ultrastore list <catalog.json> [--channel beta] [--lang de]
ultrastore resolve <catalog.json> <app-id> [--installed <id>@<version> ...]
ultrastore verify <catalog.json> <catalog.json.sig> --key <hex> [--key <hex>]
ultrastore fetch <base-url> --key <hex> [--cache <dir>]
ultrastore keygen | pubkey <seed-hex> | sign <catalog.json> <seed-hex> [-o <sig>] | sha256 <file>
ultrastore install <catalog.json> <app-id> [--channel beta] [--root <dir>]
ultrastore update <catalog.json> [<app-id>] [--root <dir>]
ultrastore remove <app-id> [--root <dir>]
ultrastore installed [--root <dir>]
ultrastore gc [--root <dir>]
ultrastore self-update [--catalog <ultrastore.json>] [--server <url>] [--key <hex>] [--channel beta] [--root <dir>] [--check]
                                                         UltraStore itself, from its own catalog
ultrastore self-catalog <catalog.json> [-o <ultrastore.json>]   the store's own catalog cut out of a catalog
ultrastore buy <catalog.json> <app-id> [--code <CODE>] [--root <dir>]   through the payment service
ultrastore licences [--root <dir>]                        the apps bought here
ultrastore licence <app-id> [--root <dir>] [--key <hex>]    one purchase's terms; with the key, the licence file verified
ultrastore code [<CODE> | clear] [--cache <dir>]          the code in force
ultrastore --version | --help
```

## Building and testing

```
cmake -S . -B build -DBUILD_ULTRASTORE=ON -DULTRACANVAS_BUILD_ULTRASTORE_TESTS=ON
cmake --build build --target ultrastore ultrastore-setup UltraStoreCoreTests
(cd build && ctest -R '^UltraStoreCore$' --output-on-failure)
```

The target requires libsodium (UltraCrypt's Ed25519) and SQLite
(UltraDatabase, the registry); without either the configure stops and names
the package to install, or `-DBUILD_ULTRASTORE=OFF`. `-DULTRASTORE_SERVERS=
"https://a.example/store;https://b.example/store"` compiles the catalog
servers in; every `Apps/UltraStore/keys/*.pub` is compiled in as a trusted
key (the generated `UltraStoreBuiltin.h`).

The window is tried end to end against a catalog of one's own: zip a small
program, `ultrastore sha256` it, write a catalog.json with
`http://127.0.0.1:8765/...` URLs, serve the folder with
`python3 -m http.server 8765`, and start
`ultrastore --catalog catalog.json --root /tmp/store`.

## The App manager (later)

The update scheme decided on 2026-11-11: updating is not each application's
business and, in the end, not the shop's either. A separate **App manager**
does it, and that App manager is later integrated into the OS manager /
settings (UOS-Settings, `Apps/UOSSettings`).

| | Now | With the App manager | In the end |
|---|---|---|---|
| Checks for updates, updates, lists and removes what is installed, cleans up | UltraStore (the Updates and Installed pages, `ultrastore update / installed / remove / gc`) | the App manager, a separate application | a page of the OS manager / settings |
| Discover, app pages, buy, install | UltraStore | UltraStore | UltraStore |
| An application itself | nothing: no update check | nothing; at most it opens the App manager | the same |

What follows from it, so the step is small when it comes:

- **One engine, several fronts.** The App manager links `ultrastore_core`
  as the store does and calls the same `CatalogClient`, `SqliteRegistry`
  and `StoreEngine` on the same store folder (*On disk*), so the store, the
  App manager and the command line never disagree about what is installed.
- **Still one check for the machine.** Whichever front runs it, the update
  check is one fetch of the signed catalog for everything installed; two
  fronts do not mean two checks (the catalog client's cache serves the
  second).
- **Nothing new goes into the store's window for updates.** Its Updates and
  Installed pages stay until the App manager replaces them; new work on
  updating (a schedule, notifications, menu entries for every app) belongs
  to the App manager. The store's own update is already in the core
  (`SelfUpdate.h`, *The store installs and updates itself*), so the App
  manager calls it as the store does.

Open, to be decided when it is built: its name and folder, whether it
checks on a schedule or only when opened, and whether the store keeps a
reduced Updates page beside it.

## The operator tier (later)

Everything above is one publisher running one store: the admin backend
generates and signs the catalog, and `admin.sqlite` is the whole truth. A
**marketplace** - other publishers (merchants) opening shops in UltraStore,
the ULTRA OS Project running it as the operator - needs records that belong
to no merchant: who the merchants are and where they stand with the terms,
what they submitted and what the review found, what users and merchants
report, the notices sent and the audit of every operator action. None of
that can live in a merchant's `admin.sqlite`, and the operator's own copy of
it would be one more merchant store. The design for that tier is drawn in
[`design/README.md`](design/README.md) (the `Super*` pages); this section
settles where its data lives, so the pages are not drawn against nothing.

| Question | Decision |
|---|---|
| Where do the operator's records live? | **In a marketplace service, `Server/UltraStoreMarket`, beside the payment service**: one HTTPS service the operator runs, its own database on **PostgreSQL in production, SQLite for tests and development only** (decided 2026-11-10; the payment service works that way, with one database layer hiding the two engines). It holds the merchants, the submissions, the reports, the notices and the audit log, and nothing a merchant's `admin.sqlite` already holds. The operator's window (the `Super*` pages) is a client of this service, as the merchant's window is a client of `UltraStoreAdminCore`: the window and a command line never disagree. |
| Who generates and signs the catalog? | **The operator, from the approved submissions.** The public catalog stays one signed `catalog.json` with the store's `trusted_keys`, generated by the service's publish from what the review released, signed with the operator's key (*Keys*). A merchant's `UltraStoreAdmin` keeps `admin.sqlite` as its working copy and its own `publish` for a private or test catalog, and gains `submit`: it sends a release - the component object `PutComponent` reads back, the packages into the operator's package store under their hash, the media - signed with the merchant's key, so the submission proves who sent it. The merchant's key is the one *Keys* already describes; it signs submissions now, not the public catalog. |
| What is a submission? | **A row the review works on**: the signed component object as sent, the package hashes, the automatic checks' results (signature, SHA-256, the scans, the entry found, the smoke run, the listing's pictures and texts, the merchant's standing), the reviewer's checklist, the decision with its message, and the catalog sequence that released it. A resubmission is a new row that names the one it answers, so the review picks up where it left off. Nothing is edited in place: a decision is appended. |
| Where do reports come from, and who sees the reporter? | **Three doors into one table**: the store's app page (`POST /v1/reports`, the licence or the install id as the reporter when there is one, else anonymous), the merchant backend (a merchant reporting another's app or a problem with their own), and the operator entering one from mail. A report names its target (an app, a release, a merchant), its kind, the text and attachments, and gets a state, an assignee and the operator's findings. **The reporter is visible to the operator only**: the merchant sees the report, never who filed it, and the reporter learns the outcome in one line. |
| How do notices reach a merchant, and how is a warning counted? | **Written once, delivered twice**: a notice (announcement, reminder, information request, warning, suspension) is a row with its recipients; it goes out by mail, and the merchant backend fetches it (`GET /v1/notices` with the merchant's key) and shows it as a banner until acknowledged. A warning is a notice that names a clause and counts as a strike; strikes expire and add up to a suspension by the **enforcement policy**, which is a settings record of the service (expiry, strikes before a suspension, its length, the answer deadlines, the clauses that suspend at once), not a constant. A suspension is a state on the merchant or the app with its scope, clause, start, end and the row that lifted it; the publish reads it and leaves the listing out. |
| What is the audit log? | **Append-only, hash-chained, never edited**: every operator action - verify, release, request changes, reject, warn, suspend, reinstate, withdraw, dismiss, resolve, a policy or terms change - is one row with the actor, the target, the clause, what was before and after, the row it acted on, and the hash of the row before it, so a gap or an edit shows. The dashboard's *Operator actions* and a merchant's *History* are views of it. |
| Which terms did a merchant accept? | **A versions table and an acceptances table.** A terms version has its text, what changed, and the date it comes into force; an acceptance names the merchant, the version, the person who accepted and when. A merchant not on the version in force by its date is paused by the publish, the way a suspension is, until an acceptance arrives. The merchant backend's *Terms* page reads both through the service. |
| Does the store binary change? | **Little.** It reads the same signed catalog; it gains the report form on the app page and, with an account, the reporter's identity; a withdrawn or suspended app is simply absent from the next catalog, and an installed copy keeps updating or not as the catalog says (`visibility: hidden` already covers "served for updates only"). |

What the operator never holds: a merchant's sales ledger and partners (those
stay in the merchant's `admin.sqlite`, fed by the payment service as today),
a merchant's signing seed, and the payment credentials (the payment service's).
What the operator holds and the merchant can always read back through the
service: their own submissions with every decision, every notice sent to
them, every report about them with the reporter removed, and their standing.

## Milestones

1. **0.1.0** core and command line (this page).
2. **0.2.0** UltraInstaller: download manager on UltraNet, unpacking through
   VirtualFS / libarchive, the registry on UltraDatabase, garbage collection;
   `ultrastore install / update / remove / installed / gc`.
3. **The framework's missing widgets** (card with button child, sidebar
   navigation, collapsible bottom panel, single-select chip group), with
   component docs (framework 0.9.409).
4. **The window** (this page's *The window*).
5. **The admin backend** (`Apps/UltraStoreAdmin`: the core, the command
   line and the window), the packages server's access scheme
   (decided: *Where the server is reached*); key rotation through the
   catalog (schema 2, `trusted_keys`, see *Keys*).
6. **Affiliates and codes** (*Affiliates and codes*): discount and
   affiliate codes in the catalog, QR codes and links, two-tier partners in
   the admin backend with the sales ledger and the report.
7. **Payment** (*Payment*): Paysafe with Skrill Quick Checkout. The
   payment service (`Server/UltraStorePay`): the checkout, the status
   report, the licence, the receipt, the signed URLs for `restricted/`, the
   sales for the admin backend; the store's *Buy* through it, the licence
   in the registry and on the details page, the restricted package through
   the signed URL, `ultrastore buy` and `licences`; the admin's `sale
   sync`; the catalog naming the service (`payment_url`, `licence_key`).
   After the purchase: the update period in the licence and held to by the
   store (*Buy update*), the licence file for the app (`FindLicenceFile`,
   `ReadLicenceFile`, `ultrastore licence`), the open order resumed at
   the next start. Next: a run against the Skrill sandbox once the
   credentials are in the environment.
8. **The operator tier** (*The operator tier*, later): the marketplace
   service `Server/UltraStoreMarket` with merchants, submissions, reports,
   notices, terms and the audit log; the merchant backend's `submit` and
   its notices banner; the operator's window on the service; the report
   form on the store's app page.
9. **The store installs and updates itself** (*The store installs and
   updates itself*): the store's own catalog `ultrastore.json`, the setup
   program `ultrastore-setup`, the running version kept through an update,
   the restart, the menu entry; the admin backend's publish writes the
   store's own catalog.
10. **The App manager** (*The App manager*, later): a separate application
   on the store's core that owns the update check, the updates and the
   list of what is installed; then its integration into the OS manager /
   settings (UOS-Settings).
