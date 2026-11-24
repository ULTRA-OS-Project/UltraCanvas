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
| What is the unit the store installs? | **Layers.** A *runtime* (the general shared libraries every app runs on: libUltraCanvas and what it loads), *libraries* (specialised shared sets some apps need: VideoFX, the PDF plug-in, OCR) and the *app* itself. The installer checks which layers are already there and installs only the missing ones, then the app, and starts the app with its layers on the library path (*Packaging an application for the store*). Least storage on the server, least transfer to the machine. |
| Where do packages come from? | **Our own server.** The admin backend keeps the apps and their settings in a database; the packages are zip / 7z files on a server only the store reaches; the catalog is generated from the database and signed. |
| Where does the code live? | `Apps/UltraStore`, with `ultrastore_core` as a static library in v1. |
| Signature and hash primitives | Ed25519 added to UltraCrypt 0.2.0 (framework 0.9.272); the store target **requires** libsodium and SQLite and stops the configure without them. |
| Management | **UltraStore admin backend**, a local application (`Apps/UltraStoreAdmin`, its own README), adding and removing apps with every setting the catalog carries: see *The admin backend*. |
| Translations | Creators supply at least one language; the admin backend translates with the server's local UltraAI into every official language and marks what it translated (`source_language`, `machine_translated`); ULTRA OS apps' own localisation folders get the same. |
| Signing key | Generated and kept **in the admin backend's database**, the seed sealed with a passphrase (Argon2id, XChaCha20-Poly1305); the public key reaches the store through the repository and the signed catalog: see *Keys*. |
| UltraInstaller | **Created** (0.2.0): `Downloader`, `SqliteRegistry`, `ArchiveInstaller`, `StoreEngine` behind the seam `PackageInstaller.h` fixed in 0.1.0; see *UltraInstaller* below. |
| The services' databases | **A database server in production, SQLite for tests and development only** (decided 2026-11-10, the engine 2026-11-11). The marketplace service runs on **MySQL**; the payment service on MySQL or PostgreSQL, the operator's choice - both go through one database layer (`Server/UltraStoreDb`, `ultrastore-db`) that hides the engine, and the tests of both run on all three. SQLite is what the tests and a developer's machine use, and never a production store. The files on a user's or a merchant's own machine (`registry.sqlite`, `admin.sqlite`) are UltraDatabase files, not servers, and stay as they are. |
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
    StoreQuery.h/.cpp           what the pages list: the catalog joined with what is installed, filtered
    AssetCache.h/.cpp           icons and screenshots, fetched once into the cache
    StoreSettings.h/.cpp        channel, language, servers, trusted keys, the window's size (UltraCanvasAppSettings)
    UltraStoreBuiltin.h.in      -> the compiled-in keys (keys/*.pub) and servers (ULTRASTORE_SERVERS)
  cli/Commands.h/.cpp           ultrastore <command>
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
  "market_url": "https://store.example/market",
  "account_url": "https://store.example/account",
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
| Search | `categories`, `tags` | categories from the store's fixed list (below); tags free-form |
| Texts | `summary`, `description`, `releases[].notes` | per language, `en` the fallback; `source_language` names what the creator wrote, `machine_translated` the languages the server's UltraAI added (see *Translations*) |
| Media | `icon`, `screenshots`, `icon_variants`, `screenshot_variants` | relative to the catalog URL, served from the same place. The variants are the pictures at the web store's sizes and browser formats, written by the admin backend at import: `icon_variants` a list of `{"format": "avif"\|"webp"\|"png"\|"jpeg", "width": <px>, "url": "media/<id>/icon-64.avif"}`, `screenshot_variants` one such list per screenshot in the screenshots' order. Icons at 64, 128 and 256 px, screenshots at 560 and 1120 px wide, never above the original. A store that does not know the keys ignores them; the web store picks the format the browser decodes and the smallest width at or above its slot times the device pixel ratio, falling back to `icon` / `screenshots` |
| Codes | `codes[]` (catalog-wide) | the discount and affiliate codes in force: see *Affiliates and codes* |

**Categories** come from one fixed list, so the store can name each in the
buyer's language; the catalog writes the ids, and the store's browser shows
them in this order (`StoreCategories()`, `Catalog.h`; the names are
`CategoryName()`, `StoreQuery.h`):

| Id | Name (English) | | Id | Name (English) |
|---|---|---|---|---|
| `audio` | Music and audio | | `education` | Education |
| `video` | Video | | `science` | Science |
| `graphics` | Graphics and photos | | `finance` | Finance |
| `office` | Office | | `security` | Security |
| `communication` | Communication | | `games` | Games |
| `internet` | Internet | | `system` | System |
| `development` | Developer tools | | `utilities` | Utilities |

The store reads an id in any case and trims it, so a catalog from before
the list (`"Office"`) still files its apps; an id it does not know - a
later list's - is kept and not shown, so an older store reads a newer
catalog. The admin backend writes only the list's: its form offers the
list, and `PutComponent` and `app set <id> categories` refuse any other
word (`CheckCategories`).

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
| 5 | the admin backend | fetches the sales (`GET /v1/sales?since=`) and records each in its ledger with the partners' credits, one level up and no further; fetches the refunds and chargebacks (`GET /v1/reversals?since=`) and takes the credits back in proportion, so its Finance page's net and what a partner is owed are right |

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
the command line, and `ultrastore licences` lists what was bought (with a
subscription's day; `--sync <catalog.json>` asks the service about each
first).

**Subscriptions.** An app priced `monthly` or `yearly` is bought the same
way, through Quick Checkout's recurring billing: the first payment is the
price after the code, then the list price every month or year, until the
buyer cancels. The licence carries the day it is valid until
(`valid_until`: the paid period plus the service's grace days, during
which Skrill retries a failed renewal), each renewal re-issues it with a
later day, and the store takes the new token when it refreshes the
catalog (the engine's `SyncLicences`, after every catalog fetch; `ultrastore
licences --sync <catalog.json>` by hand). An app checks the day after
verifying its file, `Licence::ValidOn` with today's date. `ultrastore
unsubscribe <catalog.json> <app-id>` ends a subscription (the service
tells Skrill); the licence lasts until the paid period ends. The window's
side of a subscription - the day on the details page, a *Cancel
subscription* button - is still to come.

**Refunds and chargebacks.** The operator refunds a payment through the
service (`POST /v1/orders/{order}/refund`, the admin token; Skrill's
refund API), whole or in part; a chargeback reaches the service from
Skrill. Either takes the licence back when everything paid is reversed: the
service no longer hands the licence out or signs the app's downloads, and
the store, at its next catalog refresh, removes the licence from the
registry and the licence file from the app's folder (`SyncLicences`).
The reversals are a feed of their own for the admin backend
(`GET /v1/reversals`); taking the partners' credits back in the ledger is
to come. The service's README, *Subscriptions, refunds and chargebacks*.

A paid app's **trial** installs the same package, so an app with a trial
keeps its package public (the licence gates what the trial lacks) and only
an app without one puts its package under `restricted/` (`package add
--access`); a restricted package with a trial would make *Try* fail with
"buy it first".

On the admin's side, `sale sync` pulls the paid orders from the service
(`GET /v1/sales?since=`, the `payment.token` setting or
`ULTRASTORE_PAY_ADMIN_TOKEN` as the bearer token) and records each once in
the ledger with the partners' credits - a subscription's renewal as a sale
of its own, under the service's `reference` (`<order>/<payment>`); the
sequence it reached is kept in `payment.sales_seq`, so a repeat records
nothing twice.

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
the catalog: they go through the framework's `Tr` (UltraLocalisation), from
`media/locale/<lang>/UltraStore.json` over the standard set, in the
language the desktop or `Language.conf` picks - every page, button, fact,
price, date (the language's month names and order), size unit and the
messages of the core (a refused install, a catalog that does not verify, a
failed download) the window shows. The command line (`ultrastore list`,
`install` ...) stays English: its output is read by scripts and logs, and it
runs before any language is loaded.

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
  collects; `UpdateAll` does that for every installed app; `Remove` takes
  every version of an app and collects. Callbacks report each step, the
  bytes of its download and the files of its unpacking.

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

**The web store as HTML + JavaScript** is the other output: the same shop
as complete, styled pages written by the admin backend's publish through
UltraSite (`web.output` `html`, the default for a store without
`web.site_dir`; `Apps/UltraStoreAdmin/core/WebStoreHtml.h`). The pages
come from the catalog the same publish has just signed, so the browser
fetches no catalog and verifies nothing; *Install*, *Try* and *Buy* open
the native store, which verifies everything before it installs. Discover
is about 11 KB compressed on a first visit, its pictures aside.

**The web store** (`Apps/UltraStore/web/`, its [README](../../Apps/UltraStore/web/README.md))
is the same shop in a browser: Discover, the categories and an app's page
as an UltraCanvas application compiled to WebAssembly, a side module on
the UltraCanvas runtime, served from the catalog base by the admin
backend's publish (the settings `web.output` `wasm` and `web.site_dir`), with an HTML shadow of
every page for search engines and the moment before the module paints.
It fetches `catalog.json` and `catalog.json.sig` from its own origin and
verifies the signature in the browser - through the runtime's UltraCrypt
on libsodium, the native store's own verifier, with WebCrypto's Ed25519 as
the fallback - against the keys built in and the ones an accepted catalog
announced, with the same sequence check as the native store; a catalog
that does not verify is not shown. Its texts follow the visitor's
language: the site carries the catalogues, the module fetches the
browser's language's before it draws. It does not install, buy or verify a package: *Install*, *Try* and
*Buy* open the native store's `ultrastore://app/<id>` link, because a
licence and an install have to land in the store that keeps them, and
where the store is missing the page offers its download
(`web.store_download_url`). The brief it was built from, with what the
build found different, is [`WebstoreBrief.md`](WebstoreBrief.md).

`ultrastore` started plainly (or with `--catalog`, `--root`, `--channel`)
opens the window; `Apps/UltraStore/ui/`. It follows the UltraStore shop
design (the canvas *UltraStore Shop Design*, 2026-11-05): a white ground,
ink text, the UltraStore blue of the app icon as the one accent, Manrope
for display text over Inter for body text (both in `media/fonts`), 20 px
card corners, pill buttons and chips (`StoreTheme.h`). A header carries
the wordmark (the way home), the sections as pills - Discover, Categories,
Installed, Updates with its count - the search pill, Refresh, *Sign in*
(the person's initials and name once signed in; the pill is there when the
catalog names an account service, *The account tier*) and Settings. The download
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
| Settings | channel, the texts' language, the servers, the keys trusted beside the compiled-in ones, fetch at start; the account service of one's own; this machine, the store folder, the settings file, the catalog's state, what is installed | Save, Revert, Refresh catalog now, Clean up, Reload installed |
| Sign in | the design's `AccountSignUp`: sign in (email, password; the second factor asked for when the account has one), create an account (email, password, country, year of birth, consent), the mailed code, a forgotten password, the welcome | Sign in, Create an account, Verify, Send it again, Set the password; a sign-in attaches this machine's licences to the account and fetches the account's (`StoreAccountPage.cpp`, Account.h) |
| Account | the design's `AccountProfile` with its rail: profile (display name, handle, language, country, year of birth, about; the email moved after a code), security and sign-in (the password, two-factor with the app's key and the recovery codes shown once), devices (the machines signed in with the seats they hold), purchases and licences (seats taken or freed for this machine), notifications (the settings, the notices with the operator's), privacy and data (the history switch, the public profile, the data archive, the deletion with its grace) | Save, Change, Turn on / off, Rename, Sign out, Take a seat here, Request the archive, Download, Delete my account, Keep my account |

Not in the window, of the design: the account's payment methods,
subscriptions and orders pages (the payment service's; the licences page
shows what of them the account holds), passkeys and the avatar (*The
account tier*, what comes next), the share and
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
ultrastore buy <catalog.json> <app-id> [--code <CODE>] [--root <dir>]   through the payment service
ultrastore licences [--sync <catalog.json>] [--root <dir>]  the apps bought here; --sync asks the payment service about each
ultrastore unsubscribe <catalog.json> <app-id> [--root <dir>]   ends a subscription
ultrastore report <catalog.json> <app-id> <kind> <text>   a problem with an app to the catalog's marketplace service
ultrastore reports <catalog.json>                         what became of the reports sent from this machine
ultrastore licence <app-id> [--root <dir>] [--key <hex>]    one purchase's terms; with the key, the licence file verified
ultrastore code [<CODE> | clear] [--cache <dir>]          the code in force
ultrastore account signup <email> [--country DE] [--year 1990] [--marketing]   an account at the catalog's account service; the password from ULTRASTORE_PASSWORD or stdin
ultrastore account verify <email> <code>                  the mailed code; this machine signed in, its licences attached
ultrastore account signin <email> [--totp <code> | --recovery <code>]
ultrastore account signout [--everywhere] | me | devices | licences | notices | sync
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
the package to install, or `-DBUILD_ULTRASTORE=OFF`. `-DULTRASTORE_SERVERS=
"https://a.example/store;https://b.example/store"` compiles the catalog
servers in; every `Apps/UltraStore/keys/*.pub` is compiled in as a trusted
key (the generated `UltraStoreBuiltin.h`).

The window is tried end to end against a catalog of one's own: zip a small
program, `ultrastore sha256` it, write a catalog.json with
`http://127.0.0.1:8765/...` URLs, serve the folder with
`python3 -m http.server 8765`, and start
`ultrastore --catalog catalog.json --root /tmp/store`.

The web store builds natively with the demos, `-DBUILD_WASM_DEMOS=ON`, as
`ultrastore-web <publish folder> [page]`, over a folder the admin backend
published (`Apps/UltraStore/web/test/make-fixture.sh` makes one); under
emcmake with `-DULTRACANVAS_WASM_DYNAMIC=ON` it is the side module
`ultrastore-web.wasm` and the `site/` folder, which `wasm-build.yml`'s
dynamic variant builds, measures and opens in Chromium
(`Apps/UltraStore/web/test/webstore-test.mjs`) - and uploads as the run's
`ultrastore-web-dynamic` artifact: `site/`, to publish a store with, and
`preview/`, the test's fixture store with the build laid over it, a folder
any static host serves (`Apps/UltraStore/web/README.md`, *The store a CI
run built*). Both trust the keys of `keys/*.pub`
(`cmake/UltraStoreKeys.cmake`) and need no others for a release;
`-DULTRASTORE_WEB_TEST_KEYS=<hex>` names a test key, as CI does for the
fixture - natively added to the program, under emcmake built into a second
module, `ultrastore-web-test.wasm`, so that `site/`'s never carries one.

## The portal

The store's server side is being gathered into **one application, the
UltraStore portal** (`Server/UltraStorePortal`,
[its README](../../Server/UltraStorePortal/README.md); started 2026-11-20):
a Django project under the one root `https://store.ultraos.eu` with the
developer console (register a publisher, publish an app), the operator's
review, the store's web pages, buying and the catalog as a JSON service. It is to
replace the services and tools the two sections below describe - the
marketplace, account and payment services, `ultrastoreadmin` and
`ultrastoreop` - and it changes three of their decisions: there is **one
store and one catalog**, with no private or per-merchant catalog and no
merchant key (a publisher is an account, the operator's key signs); **every
URL of the store sits under the one root**; and nothing of the earlier
services' APIs is kept, since none was in use. Since its 0.4.0 (2026-11-24)
the portal is also **the account every ULTRA OS service signs in with**: an
OpenID Connect provider, at an address of its own when one is set
(`https://account.ultraos.eu`), through which the store itself, and next
cloud storage, the chat and advertising, sign a person in (its README, *One
sign-in for every service*). What stays is this page's *Catalog*: the
portal publishes the same signed `catalog.json`, so the store's core reads
it unchanged - and *Payment*: the portal sells with the protocol and the
licence that section describes (its 0.2.0), on MySQL. Until the portal has
taken over buyer accounts, the sections below remain the record of how
those were designed.

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
| Where do the operator's records live? | **In a marketplace service, `Server/UltraStoreMarket`, beside the payment service** (built 2026-11-11, its own README): one HTTPS service the operator runs, its own database on **MySQL in production, SQLite for tests and development only** (decided 2026-11-11; the payment service shares the database layer, `Server/UltraStoreDb`, which hides the engine). It holds the merchants, the submissions, the reports, the notices and the audit log, and nothing a merchant's `admin.sqlite` already holds. The operator's window (the `Super*` pages) is a client of this service, as the merchant's window is a client of `UltraStoreAdminCore`: the window and a command line never disagree. |
| Who generates and signs the catalog? | **The operator, from the approved submissions.** The public catalog stays one signed `catalog.json` with the store's `trusted_keys`, generated by the service's publish from what the review released, signed with the operator's key (*Keys*). A merchant's `UltraStoreAdmin` keeps `admin.sqlite` as its working copy and its own `publish` for a private or test catalog, and gains `submit`: it sends a release - the component object `PutComponent` reads back, the packages into the operator's package store under their hash, the media - signed with the merchant's key, so the submission proves who sent it. The merchant's key is the one *Keys* already describes; it signs submissions now, not the public catalog. |
| Where do a merchant's packages go? | **Into the operator's package store, with the submission** (built 2026-11-20). The submission names each package by its hash, size, format and access; when the service keeps a package store (`ULTRASTORE_MARKET_PACKAGE_DIR`) the `packages` check waits until the merchant has sent the bytes - `POST /v1/submissions/{id}/packages/{sha256}`, the request signed over them like every other, the service hashing them before it keeps them under `<sha256>.<format>` - and a release is refused while one is missing. `market submit` uploads them from the admin backend's own package store right after the submission (`market upload` again); the review's *Download build* fetches one; the publish lays the uploaded packages out beside the catalog (`packages/`, or `restricted/` for a paid app's without a trial) and the catalog names them under `ULTRASTORE_MARKET_CATALOG_URL`, so a merchant needs no public server of its own. A package the merchant did not upload keeps the URL it named - the service without a package store behaves as before. The same bytes submitted again need no second upload. |
| What do the automatic checks do? | **The service checks what it can at once, the pipeline the rest once the packages are in** (built 2026-11-20, `checks.py` in the marketplace service). At once: the signature over the component, the merchant's standing, the packages named by hash and size. After the last upload (in the background; `POST /v1/submissions/{id}/checks` runs it again on the operator's word): **scans** - every archive walked without unpacking for a path that escapes the install folder, a link that points outside, an unpacked size out of proportion to the download, an installer inside, and each file handed to a virus scanner when `ULTRASTORE_MARKET_SCAN_COMMAND` names one (clamscan's convention), the archive scan alone otherwise, said so; **entry** - the file the store launches is in the archive, the one the package names or one found by the admin backend's own rules; **smoke** - the build started by a sandbox runner the operator provides (`ULTRASTORE_MARKET_SMOKE_COMMAND`), *not run* without one, since the service never runs a merchant's binary itself; **listing** - what the store's page needs (a name, a summary in English or the source language, categories, an icon, a price and currency when not free, an age rating the store knows, URLs that are http(s), the packages' access against a trial). Each check keeps its findings, which the review shows under it. A failed scan or entry makes the submission `failed_checks`, which cannot be released; the listing's failures and the smoke run are the reviewer's to weigh. |
| What is a submission? | **A row the review works on**: the signed component object as sent, the package hashes, the automatic checks' results (signature, SHA-256, the scans, the entry found, the smoke run, the listing's pictures and texts, the merchant's standing), the reviewer's checklist, the decision with its message, and the catalog sequence that released it. A resubmission is a new row that names the one it answers, so the review picks up where it left off. Nothing is edited in place: a decision is appended. |
| Where do reports come from, and who sees the reporter? | **Three doors into one table**: the store's app page (`POST /v1/reports`, the licence or the install id as the reporter when there is one, else anonymous), the merchant backend (a merchant reporting another's app or a problem with their own), and the operator entering one from mail. A report names its target (an app, a release, a merchant), its kind, the text and attachments, and gets a state, an assignee and the operator's findings. **The reporter is visible to the operator only**: the merchant sees the report, never who filed it, and the reporter learns the outcome in one line. |
| How do notices reach a merchant, and how is a warning counted? | **Written once, delivered twice**: a notice (announcement, reminder, information request, warning, suspension) is a row with its recipients; it goes out by mail, and the merchant backend fetches it (`GET /v1/notices` with the merchant's key) and shows it as a banner until acknowledged. A warning is a notice that names a clause and counts as a strike; strikes expire and add up to a suspension by the **enforcement policy**, which is a settings record of the service (expiry, strikes before a suspension, its length, the answer deadlines, the clauses that suspend at once), not a constant. A suspension is a state on the merchant or the app with its scope, clause, start, end and the row that lifted it; the publish reads it and leaves the listing out. |
| What is the audit log? | **Append-only, hash-chained, never edited**: every operator action - verify, release, request changes, reject, warn, suspend, reinstate, withdraw, dismiss, resolve, a policy or terms change - is one row with the actor, the target, the clause, what was before and after, the row it acted on, and the hash of the row before it, so a gap or an edit shows. The dashboard's *Operator actions* and a merchant's *History* are views of it. |
| Which terms did a merchant accept? | **A versions table and an acceptances table.** A terms version has its text, what changed, and the date it comes into force; an acceptance names the merchant, the version, the person who accepted and when. A merchant not on the version in force by its date is paused by the publish, the way a suspension is, until an acceptance arrives. The merchant backend's *Terms* page reads both through the service. |
| Does the store binary change? | **Little.** It reads the same signed catalog; it gains the report form on the app page and, with an account, the reporter's identity; a withdrawn or suspended app is simply absent from the next catalog, and an installed copy keeps updating or not as the catalog says (`visibility: hidden` already covers "served for updates only"). |
| How does a merchant join? | **It applies from its admin backend, and the operator decides** (decided 2026-11-19). `ultrastoreadmin market apply`, or the window's *Marketplace* page, sends the shop's name, how to reach it, an email for the operator, the website and a line about the publisher, with the public half of the key that will sign its submissions - the application signed by that key, so the key is proven, not typed in by the operator. The applicant keeps the application's id and secret in its settings and asks what became of it (`market application`); the operator sees the queue on *Merchants* (`ultrastoreop merchant applications`) and approves - the merchant registered with that key and profile, under an id the operator gives or one made - or rejects with a reason the applicant reads. Approved, the admin backend sets `market.merchant_id` itself. The merchant then edits its own profile (`PUT /v1/merchants/me`: name, contact, website, description - the catalog's publisher page comes from the website) and rotates its key signed with the old one (`market profile --rotate-key`); the operator's register and set stay for the hand-made cases. A key is the merchant's identity: a second machine is a second key, not a staff account. |
| Who sits at the operator's window? | **Operator accounts with roles, kept by the marketplace service** (decided 2026-11-19). An account is an email, a password (scrypt, as the account service keeps a person's) and a role: `admin` (everything, the accounts included), `reviewer` (the review and the publish), `support` (the reports, the notices, acting for a user), `auditor` (reads everything, changes nothing). `POST /v1/operators/signin` answers a signed session (`ultrastore_db.operator_session`: `op1.<payload>.<hmac>`, the operator's id, name and role, an end after the service's hours) that the window keeps as its token; the account service verifies the same session with the secret the two share (`ULTRASTORE_MARKET_OPERATOR_SESSION_SECRET` = `ULTRASTORE_ACCOUNT_OPERATOR_SESSION_SECRET`), so one sign-in reaches both with the same name and role and the *Users* page needs no token of its own. Every operator call names the permission it needs and the service refuses the rest (403 `forbidden`, naming the role); the audit log names the person. The static operator token of each service stays the **bootstrap admin**: it makes the first admin account, then goes into a drawer. An admin changes another's role and disables an account (not their own); anyone changes their own name and password. An operator's **two-factor sign-in** (2026-11-20): an authenticator app set up from the *Operators* page or `ultrastoreop twofactor setup`, turned on with its first code, eight recovery codes shown once; the sign-in then wants the six digits or a recovery code (401 `two_factor_required`). A **forgotten password** (`password forgot`, *Forgot the password* on Settings) mails a code the marketplace service sends (`ULTRASTORE_MARKET_SMTP_URL`); the reset takes it with the second factor. An admin clears another's second factor after the identity was checked another way (`operator reset-2fa`, *Reset 2FA*), never their own. Not built: staff accounts per merchant. |
| How do the ULTRA OS Project's own apps reach the store? | **Through the marketplace, like every merchant's** (decided 2026-11-18). The project is a merchant (`ultra-os-project`, the publisher id its components already carry), registered by the operator with the public half of its admin backend's key (or applied for like any other merchant, *How does a merchant join?*); its `UltraStoreAdmin` keeps the apps and submits each release (`market submit`); the review releases it and `POST /v1/publish` writes it into the one public catalog. So the public catalog has **one writer**, the marketplace's publish, and two publishes never overwrite each other at one address. The merchant backend's `publish` and `upload` stay for a private or test catalog only. The packages come from `scripts/package-store-app.sh` (*Packaging an application for the store*). |

What follows from the last decision, for the handover from today's public
catalog (signed by the admin backend with `Apps/UltraStore/keys/publish.pub`)
to the marketplace's:

- **The operator's key must be trusted before its first catalog.** Its
  public half (`ultrastoreop key`, `GET /v1/publish/key`) is committed as
  `Apps/UltraStore/keys/market.pub`, so every store built afterwards trusts
  it. A store already installed trusts only the compiled-in keys and what an
  accepted catalog announced, so the last catalog signed with
  `publish.pub` must list the operator's key in `trusted_keys` (*Keys*). The
  admin backend lists only its own active keys there today: announcing a
  public key it holds no seed for is still to be built. Until then the
  marketplace does not publish to the public address.
- **The sequence goes on from the public catalog's.** A store refuses a
  catalog below the sequence it accepted, and the marketplace counts from
  its own last publish: the first release is given a sequence above the
  public catalog's (`ultrastoreop review release <submission> --sequence <n>`).
- **The project's codes leave the public catalog.** The marketplace's
  catalog carries no `codes` (they are a merchant's own store's); the
  project's partners keep their links only through a store of its own.

What the operator never holds: a merchant's sales ledger and partners (those
stay in the merchant's `admin.sqlite`, fed by the payment service as today),
a merchant's signing seed, and the payment credentials (the payment service's).
What the operator holds and the merchant can always read back through the
service: their own submissions with every decision, every notice sent to
them, every report about them with the reporter removed, and their standing.

**Built so far** (`Server/UltraStoreMarket`, its README): the service with
all of the above - the merchants registered by the operator with their
keys and signing every request (`Authorization: Merchant <id>:<timestamp>:
<signature>`), the submissions with the checks the service can make at
once (the signature, the standing, the packages' hashes) and the review
pipeline's (the scans, the entry, the smoke run, the listing, with their
findings; *What do the automatic checks do?*) and the
decisions appended, the three doors for reports with the reporter kept
from the merchant and an outcome line for the reporter, the notices and
the strikes, the enforcement policy as a settings record, the suspensions
by hand and by the policy, the terms versions and acceptances with the
*paused* standing, and the hash-chained audit log with its verification;
`GET /v1/released` is what the catalog is generated from. The merchant
backend's side (its README, *The command line*): `market submit`, the
notices banner, the terms accepted from the *Marketplace* page, the
standing and the reports. The store's door for reports: the app page's *Report a problem* (the
kind, the text; the licence as the reporter when the app was bought here)
when the catalog names the service (`market_url`), the outcome line on
the page once the operator answers, `ultrastore report` and `reports`
from the command line. The publish: `POST /v1/publish` generates and
signs the catalog from the released submissions (the service's README,
*The publish*). The operator's window and command line
(`Apps/UltraStoreOperator`, [its README](../UltraStoreOperator/README.md)):
the `Super*` pages on the service - the merchants and their files, the
review with its decisions and the publish, the reports with the reporter
the operator alone sees, the notices, the terms and the policy, the audit
log verified. Still to come: the packages uploaded with a submission, the
review pipeline behind the checks marked *not run*.

## The account tier

Everything above a person does without an account: a free app installs,
a purchase is a licence in `registry.sqlite` on the machine it was bought
on, the payment service sells without one. The design's `Account*` pages
(`design/README.md`) draw what follows a person from machine to machine;
this section settles where it lives, and what of it is built.

| Question | Decision |
|---|---|
| Where does a person's identity live? | **In an account service, `Server/UltraStoreAccount`, beside the payment service and the marketplace service** (built 2026-11-19, its own README): one HTTPS service the ULTRA OS Project runs, its own database on MySQL (the shared layer `Server/UltraStoreDb`; SQLite for tests and development). It holds the email, the password as an scrypt hash, the display name and handle, the language, the country (VAT, currency) and the year of birth (age ratings, never shown), the second factor, the sessions, the licences attached, the notices, the archives, the audit log. The store's window and command line are its clients (`Account.h`); the catalog names it (`account_url`), the way it names the payment and marketplace services. |
| What is a device? | **A session**: one per machine signed in, with the machine's name, system and store version, ended by the person from any machine, by the operator, or by 90 days unused. The store keeps the session token in `<root>/account.json`, readable by the owner alone, and sends it as a bearer token; a token the service no longer knows signs the store out. |
| How does a licence follow a person? | **Attached to the account, verified, counted in seats.** A licence the payment service issued (`ULIC1.…`, its signature checked with the licence key) is attached to the account the store is signed in to, and each machine it is installed on takes a seat (the licence's number, else the service's default of 3). At a sign-in the store sends every licence of its registry up and takes a seat, and fetches every licence the account holds that the registry lacks, verified against the catalog's `licence_key`, into the registry and its licence file; signing a machine out frees its seats. The order secret stays on the machine that bought: the receipt and the cancel go through it, the app does not need it. |
| Who signs up, and how is an address proven? | **Anyone, with a mailed six-digit code**, good for ten minutes; the address is the sign-in name. An address with an account gets the same answer as a new one and a mail to its owner, so the service tells a stranger nothing. A password is twelve characters or more. |
| What is the second factor? | **TOTP** - UltraAuthenticator or any authenticator app - **with recovery codes** (eight, hashed, each good once, shown once; a new set from Security). The service asks for it at sign-in and at a password reset; the operator can reset it after checking the person's identity another way, and the person is told. Passkeys are drawn and come later: WebAuthn needs the platform's authenticator on each machine. |
| What does the operator see, and do? | **Accounts as far as the store needs them**: the email masked, the name and handle, the country, the state, two-factor on or off, the devices and licences counted, the requests; one user's file with the devices, the licences without their tokens, the notices, the archives, the sign-ins and the history. Never the password hash, the year of birth, a payment method, the listening and watch history. The actions - a notice, sign out everywhere, reset two-factor, release the archive, suspend with a reason and days, reinstate, delete - are rows in the account service's own hash-chained audit log, and a notice to the person where one is due. A deletion the person did not ask for is refused. |
| How does the operator reach a buyer, and a merchant? | **Customer messaging is a notice on the account** (decided 2026-11-19): written once by the operator (`POST /v1/users/{id}/notice`: notice, security, warning, suspension, information request), kept on the account under Notifications until read, and sent by mail. The service itself writes the security notices (a new sign-in, a password change, a recovery code used). **A merchant never reaches a buyer directly**: the merchant has no buyer identity (the marketplace service hides the reporter, the payment service names no account). A merchant's message to a buyer goes through the marketplace service with the order as the key, relayed by the operator's services into a notice on the account the order belongs to, the buyer's identity never given to the merchant - decided, built later, once the payment service records the account at checkout. Until then the report's outcome line is the merchant-to-buyer channel, as today. |
| What does the person take with them, and how does it end? | **A data archive and a grace.** The archive is everything the account holds of the person as one JSON file, built at once when asked for, released by the operator within a day, downloaded once. The deletion is asked for with the password, carried out after 30 days unless cancelled - by the daily maintenance, or by the operator on the person's request - and leaves the row under its id with the email replaced, so the audit rows still name a target and the address is free again; the invoices are the payment service's and stay as the law asks. |
| Does the store need an account to install? | **No.** A free app installs without one, as before; an account is for what follows the person. The store's window shows *Sign in* only when the catalog names the service. |

**Built so far** (`Server/UltraStoreAccount`, its README): the service with
all of the above - sign up with the mailed code, sign in with the password
and the second factor, the sessions as devices, the profile and the
settings, the email moved after a code, the password changed with the other
machines signed out, two-factor with the recovery codes, the licences
attached and their seats, the notices, the archive released by the operator
and downloaded once, the deletion with its grace, the operator's users list
with the counts and the states, the file, the actions and the audit log
verified; `POST /v1/maintenance` for the daily timer. The store's side
(`Apps/UltraStore/core/Account.h`): the session file, the client, the
licences kept in step both ways (`SyncLicencesWithAccount`); the window's
*Sign in* pill, the sign-in page with its steps and the account page with
its rail (`ui/StoreAccountPage.cpp`); `ultrastore account`. The operator's
side (`Apps/UltraStoreOperator`, [its README](../UltraStoreOperator/README.md)):
the *Users* page after the design's `SuperUsers` on the account service
with its own token, and `ultrastoreop users`. The admin backend's publish
writes `account_url` from the setting `account.url`. What comes next: the
payment service told the account at checkout (an order on the account
without the store attaching the licence), passkeys, the avatar, the
merchant's message relayed to a buyer.

## Packaging an application for the store

A store package is one archive per operating system and processor. The
store unpacks an app into `<root>/apps/<id>/<version>/` and a runtime or
library into `<root>/components/<id>/<version>/`, and starts an app's
`entry` **on its runtime layer** (`PlanLaunch`, `StoreQuery.h`): the
components the app was installed with - the exact versions of its release's
`depends`, and theirs in turn - go first on the library path (a component's
`lib/` on Linux, its folder on Windows, as the suite package lays them out;
`LD_LIBRARY_PATH`, `PATH`), and the runtime's folder is named in
`ULTRACANVAS_RUNTIME_DIR`. The framework looks there for `media/` and for
UltraNet's plug-ins after the app's own folders (`kRuntimeDirVariable`,
`UltraCanvasConfig.h`), so an app whose package carries none takes the
runtime's. That is the *layers* of *Decisions*, *What is the unit the store
installs?*, at the moment the app starts. On macOS nothing is added: an app
there is a bundle with its own `Frameworks/`, and `open` passes no
environment.

`scripts/package-store-app.sh` makes the archives out of the suite package
`package-linux.sh` or `package-win.sh` made, the way
`package-ultracanvasstart.sh` cuts UltraCanvasStart out of it, in three
shapes:

| Shape | What it carries | Release |
|---|---|---|
| `<App>` | the app on its own: the executable and its launcher, the plug-in families it loads (every one but UltraIM, which goes with ChatCube alone), the closure of libraries the loader resolves inside the suite with libvips', libheif's and ImageMagick's run-time modules, the media the framework loads (fonts, icons, app icons, `media/lib`, the translations, the emoji fonts), the app's own data folder | no `depends`; runs in every store |
| `--runtime` | `org.ultraos.runtime` at the framework's version: the core library and its closure, the plug-in families every app may load with theirs, the run-time modules, the media - at the top of the archive (`lib/`, `bin/Plugins/`, `share/media/` on Linux; the DLLs, `Plugins/`, `Resources/media` on Windows), as the component's folder | a component of kind `runtime` |
| `<App> --on-runtime` | the app without what that runtime carries: the executable, its launcher, the libraries and plug-in families the runtime does not have, the app's own data | `--depends org.ultraos.runtime@<framework version>`; needs a store that starts an app on its runtime layer |

An app on the runtime is a few megabytes where the app on its own is a
hundred, and every app built on one framework version shares one runtime.
A store from before the runtime layer starts the entry with nothing added,
and an app on the runtime does not find `libUltraCanvas`: the self-contained
shape stays the one to publish until the stores in use start apps on their
runtime.

Each run checks the package from inside - every library of the executable
and the plug-ins resolves within it (and the runtime, for `--on-runtime`),
and an app that answers `--version` runs from it (on the staged runtime,
with the environment the store would give it) - and ends with the
`ultrastoreadmin` lines: the release (with its `depends`) and the package,
with the entry and the glibc variant (the newest the files ask for) or the
macOS minimum. The admin backend's own suggestion takes the launcher too
(`SuggestEntry`: on Linux `<dir>/<name>` when `<dir>/bin/<name>` is in the
archive, before `bin/<name>`, the executable that alone does not find
`lib/`).

```
./package-linux.sh
scripts/package-store-app.sh --runtime          # once per framework version
scripts/package-store-app.sh UltraMail --on-runtime
# dist-store/org.ultraos.runtime-<fw>-linux-x86_64.tar.xz and
# dist-store/org.ultraos.ultramail-<version>-linux-x86_64.tar.xz, then:
ultrastoreadmin release add org.ultraos.runtime <fw>
ultrastoreadmin package add org.ultraos.runtime <fw> dist-store/org.ultraos.runtime-<fw>-linux-x86_64.tar.xz \
    --os linux --arch x86_64 --variant glibc-<x.y>
ultrastoreadmin release add org.ultraos.ultramail <version> --depends org.ultraos.runtime@<fw>
ultrastoreadmin package add org.ultraos.ultramail <version> dist-store/org.ultraos.ultramail-<version>-linux-x86_64.tar.xz \
    --os linux --arch x86_64 --entry UltraMail/UltraMail --variant glibc-<x.y>
ultrastoreadmin market submit org.ultraos.runtime
ultrastoreadmin market submit org.ultraos.ultramail
```

On macOS, `./package-macos.sh --store-app UltraMail` cuts the bundle out of
the signed suite with a `Contents/Frameworks/` of its own (the rewrite
`--start-app` makes for UltraCanvasStart), into `dist-store-macos/`, and
`scripts/package-store-app.sh UltraMail` zips it, checks its signature and
prints its `--min-os`. UltraMail is in the macOS suite since 2026-11-18, with
UltraNet's plug-ins in its `Contents/PlugIns/UltraNet/`.

The version is line 1 of the application's changelog (the framework's for
the runtime) and the id `org.ultraos.<app in lower case>`; `--version`,
`--id`, `--plugins` and `--media` say otherwise. CI makes UltraMail's
package on every Linux, Windows and macOS build and uploads it
(`UltraMail-store-<os>-<arch>`), and on Linux and Windows the runtime and
UltraMail on it (`UltraCanvas-runtime-store-<os>-<arch>`).

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
   the next start. Subscriptions (monthly and yearly apps through
   recurring billing, the licence valid until a day, renewed and synced),
   refunds through Skrill's API and chargebacks, both taking the licence
   back; `ultrastore licences --sync`, `unsubscribe`. Next: a run against
   the Skrill sandbox once the credentials are in the environment; the
   window's side of a subscription; the reversals in the admin's ledger.
8. **The operator tier** (*The operator tier*): the marketplace service
   `Server/UltraStoreMarket` with merchants, submissions, reports,
   notices, terms, the enforcement policy, the suspensions and the audit
   log, on MySQL through the shared database layer `Server/UltraStoreDb`
   (built; *Built so far* in that section); the merchant backend's
   `submit` and its notices banner; the report form on the store's app
   page; the catalog published from the released submissions; the
   operator's window and command line (`Apps/UltraStoreOperator`). Next:
   the packages uploaded with a submission into the operator's package
   store; the review pipeline behind the checks marked *not run*.
   Decided 2026-11-18: the ULTRA OS Project's own apps go through the
   marketplace too (*The operator tier*), packaged by
   `scripts/package-store-app.sh` (*Packaging an application for the
   store*). Next for that: the handover of the public catalog - the
   operator's key announced by a last catalog signed with `publish.pub`,
   which the admin backend cannot do yet. The store starts an app on its
   runtime layer after 0.1.21 (*Packaging an application for the store*),
   so an app's package can leave the runtime out.

9. **The account tier** (*The account tier*): the account service
   (`Server/UltraStoreAccount`) - sign up with the mailed code, sign in
   with the password and the second factor, the devices, the licences
   attached with their seats, the notices, the archive and the deletion,
   the operator's users API with its audit log; the store's *Sign in* and
   account page with the licences kept in step both ways, `ultrastore
   account`; the operator's *Users* page and `ultrastoreop users`; the
   catalog's `account_url`. Next: the payment service told the account at
   checkout, passkeys, the avatar, the merchant's message relayed to a
   buyer through the marketplace service.
10. **Merchant self-service** (*The operator tier*, *How does a merchant
    join?*): the application for a shop from the admin backend, the
    operator's approval or rejection, the merchant's own profile and key
    rotation, the publisher's page in the catalog from the website;
    `market apply | application | profile`, `ultrastoreop merchant
    applications | approve | reject`, the admin's *Marketplace* page with
    its three states and the operator's *Applications* section.
11. **Operator accounts with roles** (*The operator tier*, *Who sits at
    the operator's window?*): the marketplace service's operators and
    sign-in, the signed session both services verify, a permission on
    every operator call, the audit log naming the person; the operator's
    sign-in on *Settings*, the role in the sidebar, the *Operators* page,
    `ultrastoreop signin | whoami | operator | signout`; the design's
    `SuperOperators`.
12. **The package store, and the operators' second factor** (*The operator
    tier*, *Where do a merchant's packages go?*, 2026-11-20): a package
    uploaded with the submission, the release gated on it, the review's
    *Download build*, the publish laying the packages out beside the
    catalog; an operator's two-factor sign-in (an authenticator app, the
    recovery codes) and the password reset by mail, `ultrastoreop
    twofactor | password`, an admin's *Reset 2FA*.
13. **The review pipeline** (*The operator tier*, *What do the automatic
    checks do?*, 2026-11-20): the scans, the entry, the smoke run and the
    listing on the packages in the store, their findings kept with the
    submission and shown under each check in the operator's review, the
    pipeline run again on the operator's word; `ultrastoreop review
    checks`. Next: the merchant's media and the web store's pages through
    the marketplace path, which today publishes the catalog and the
    packages alone (the system map of 2026-11-20).