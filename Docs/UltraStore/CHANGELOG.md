#### 2026-11-12 *0.1.13*
- **Subscriptions.** An app priced `monthly` or `yearly` is bought through
  Quick Checkout's recurring billing: the first payment at the price after
  the code, then the list price every month or year. The payment service
  (0.5.0) sets the paid period, re-issues the licence on each renewal
  Skrill reports with a later `valid_until` (the period plus the grace
  days Skrill retries in), numbers a receipt per payment, and hands the
  renewals to the admin backend as sales of their own. The store takes the
  re-issued token when it refreshes the catalog (`StoreEngine::SyncLicences`,
  the licence file with it); `ultrastore licences --sync <catalog.json>`
  does it by hand and shows a subscription's day; `ultrastore unsubscribe
  <catalog.json> <app-id>` ends one - no more charges, the licence until
  the paid period ends. An app checks the day after verifying its file:
  `Licence::ValidOn(today)`.
- **Refunds and chargebacks.** The operator refunds a payment through the
  service (`POST /v1/orders/{order}/refund` with the admin token, whole or
  in part, through Skrill's refund API); a chargeback reaches the service
  from Skrill's status report. When everything paid is reversed the
  licence is taken back: the service hands it out no more and refuses the
  app's downloads, and the store removes it from the registry and the
  app's folder at its next catalog refresh. `GET /v1/licences/{id}` tells
  an app that is online whether its licence stands; `GET /v1/reversals`
  is the feed of refunds and chargebacks for the admin backend. The
  service's database moves to schema 2 (a `payments` table) on its own.

#### 2026-11-11 *0.1.12*
- **Categories in the person's language.** The category chips, the pills on
  Discover, a card's and a row's category and the app page's breadcrumb
  showed the words the admin typed into the catalog ("graphics",
  "office"). Categories come from one fixed list now (audio, video,
  graphics, office, communication, internet, development, education,
  science, finance, security, games, system, utilities), each shown by its
  name in the person's language ("Grafik und Fotos", "グラフィック・写真"),
  in the list's order; the search finds an app by its category's name too.
  A catalog from before the list is read case-insensitively ("Office" files
  under office); an id a later list adds is kept and not shown, so this
  store still reads a newer catalog.
- **The header fits the window at its narrowest.** At the 900 px minimum
  the header ran past the window's edge even in English: Refresh and
  Settings were cut off. In less room it now takes its compact forms one at
  a time - Refresh as its icon, then the wordmark as the app icon, then
  Settings as a gear - as far as it has to and no further. The store
  measures what the header needs after each frame that laid it out (the
  buttons as wide as their texts, the search field at its least, 120 px)
  and goes back to the fuller form when the window is wide enough again, so
  it works for every language without a width guessed per language. The
  icon-only buttons keep their tooltips, which name them for a screen
  reader.
- **The Settings page's last English labels follow the language.** Its
  title, the Catalog and Servers headings, the Channel caption and its
  Stable choice, and the Save, Revert and Proxy settings buttons were
  plain literals, so they stayed English in a translated store. They go
  through `Tr` now: Settings, Save and Proxy settings show translated in
  every language of the standard set at once; Catalog, Channel, Stable,
  Servers and Revert are UltraStore's own texts, translated with the rest
  of its catalogue.
- **UltraStore is translated into the suite's 29 languages.** Every text the
  window shows goes through `Tr` now - the navigation, the filters, the
  buttons and badges, the facts and the information column, the download
  queue, the prices ("4.90 EUR / month", "(30-day trial)"), the code
  messages and the sizes - and `media/locale/<lang>/UltraStore.json` carries
  the store's own texts for ar, ca, cs, da, de, el, es, fi, fr, he, hi, hu,
  id, it, ja, ko, nb, nl, pl, pt, ro, ru, sv, th, tr, uk, vi, zh-Hans and
  zh-Hant (machine drafts, marked for review).
- **The core's messages are whole sentences in the person's language.** A
  refused install ("UltraVideo is a paid app: buy it first, then install"),
  a missing dependency, a catalog that does not verify, a failed download
  or a registry error was glued from English pieces; each is one `TrF`
  sentence with its names as placeholders, so a translation can order them
  as its grammar needs. The command line stays English (it runs before a
  language is loaded).
- **Dates and sizes follow the language.** A release date is the standard
  set's short month in the language's order ("22. Okt. 2026",
  "2026年10月22日") instead of "22 Oct 2026" everywhere; KB, MB and GB are the
  language's units ("12.3 Mo", "1.5 ГБ").
- **Fixed: Update all counted an update to buy by its English message.** An
  update outside a purchase is now told by the licence's terms, so the "to
  buy anew" count stays right in every language.
- **Fixed: a heading in another script.** The small upper-case headings
  (CATEGORY, PRICE, VERSION HISTORY) upper-cased ASCII letters only, so
  "Catégorie" came out "CATéGORIE"; they upper-case any letter now. A
  shortened summary or release note no longer cuts a character in two.
- **Counts take the language's plural forms.** The Update all summary
  ("3 apps updated, 1 failed"), the download queue's waiting and finished
  counts, the catalog's app and component counts and every trial length
  ("14-day free trial") are plural entries now, so Russian, Polish, Czech,
  Arabic and the rest say "21 день", "3 dni", not one form for every number.
- **A discount code has a word of its own.** The code field and a code
  without a discount used the suite's "Code", which several languages
  translate as program code; the store's entry is the discount or partner
  code.
- **Fixed: a longer translation covered the last header tab.** A
  fixed-width button widened for its text, but its row kept the width it
  measured first, so "Updates" in German slid under the search field and
  "Clear finished" ran off the Downloads panel. The framework now lays such
  a row out again at the next frame (UltraCanvas changelog), and the search
  field gives way down to 120 px.

#### 2026-11-11 *0.1.11*
- **The payment service runs on PostgreSQL in production**; SQLite is for
  tests and development only. `ULTRASTORE_PAY_DB` is a database URL, one
  database layer hides the two engines, and the tests run the whole
  purchase on both, PostgreSQL through a service container in CI.

#### 2026-11-11 *0.1.10*
- **Proxy settings** on the Settings page: the catalog, the downloads and
  the purchases go through the proxy every ULTRA OS application shares, and
  the button opens the dialog that sets it.

#### 2026-11-11 *0.1.9*
- The README and `Apps/UltraStore/keys/README.md` describe what a build
  now trusts: the production server as `ULTRASTORE_SERVERS`'s default and
  `publish.pub` as the compiled-in key, instead of saying neither exists.
- **Every text the window shows goes through UltraLocalisation.** The
  pages, the app page, the settings, the jobs' progress and messages are
  `Tr`, `TrF` and `TrN` sites, and `media/locale/en/UltraStore.json` is
  their English catalogue, so a translation reaches the store by dropping
  a `<lang>/UltraStore.json` beside it.
- **The shop design.** The window follows the canvas *UltraStore Shop
  Design* (2026-11-05): a white ground, ink text, the UltraStore blue as
  the one accent, Manrope over Inter, 20 px card corners, pill buttons and
  chips. A header with the wordmark, the sections as pills (Updates with
  its count), the search pill, Refresh and Settings replaces the sidebar.
  Discover shows the featured app as a hero, the categories as pills, *New
  and updated* as a numbered list, *Free essentials* as cards, the four
  reasons for the store and the partner / discount code; a search turns it
  into the results. Categories gets the filters on the left (the categories
  with their counts, All / Free / Paid, open source only, hide installed),
  the count and the sort order (newest release, name, price), the cards in
  a grid. The app page is the shop's: the breadcrumb, the facts as pills,
  what this machine downloads, the screenshots, *About*, *What's new* with
  the version history, *What it installs* with the sizes, the information
  column with the links, *More from* the publisher. Not taken from the
  design: *Sign in* (no accounts), the share and wish-list buttons, the
  footer's links, the *Runs on* filter.
- **`featured`.** An app the admin marks as featured (the catalog's new
  `featured` flag) is the Discover hero - the newest one this machine can
  install - and the featured apps are the *Editor's picks* row; without a
  pick the hero is the newest release.

#### 2026-11-10 *0.1.8*
- **The payment pages look like the product.** The checkout hand-off,
  the thank-you and cancel pages and the receipt share one frame: the
  ULTRA OS mark and the name at the top, the content on a card in the
  store's colours, a dark look where the browser asks for one. The
  receipt opens as a page from the store's *Receipt* button and stays
  text for a script. The stylesheet and the mark are served by the
  service itself.

#### 2026-10-28 *0.1.7*
- **After the purchase.** The licence carries the app's update period as
  the catalog had it when the order was paid, and the store holds to it:
  an update outside the purchase is refused, the card shows *Buy update*,
  *Update all* counts it as "to buy anew", the *Bought* badge names the
  terms. The licence reaches the app as `apps/<id>/licence.ulic`, written
  at install and at purchase, found and verified by the app with
  `FindLicenceFile` and `ReadLicenceFile` (`Purchase.h`); `ultrastore
  licence <app-id>` shows a purchase and checks the file. An order the
  store was closed in the middle of is remembered and picked up at the
  next start: paid meanwhile, the licence is kept; still open, the checkout
  opens again.
- **Buy through the payment service.** A catalog that names one
  (`payment_url`, `licence_key`) makes *Buy* ask it for the order with the
  code in force, open the checkout in the browser, wait for the payment
  (the download panel shows it; half an hour at most), verify the licence
  against the catalog's key and keep it in the registry; a bought app
  shows *Bought*, *Install* instead of *Buy*, its receipt number and a
  *Receipt* button, and its restricted package is fetched through the URL
  the service signs for the order, on every install and update. Without
  a payment service *Buy* still goes to the publisher's page. `ultrastore
  buy` and `licences` on the command line.

#### 2026-10-27 *0.1.6*
- **The payment service** (`Server/UltraStorePay`): a Skrill Quick Checkout
  session per purchase, priced from the signed catalog with the code in
  force; Skrill's status report verified (the signature, the amount, the
  currency) before an order is paid; the licence an app checks offline
  (Ed25519-signed), the numbered receipt, the signed ten-minute URLs for
  the packages of paid apps, and the sales for the admin backend's ledger.
  The store's *Buy* through it is the next step.

#### 2026-10-27 *0.1.5*
- The buttons that open a dialog or a page say what they do without a
  trailing ellipsis: *Buy*, not *Buy…*.

#### 2026-10-27 *0.1.4*
- **Discount and affiliate codes.** The catalog carries `codes`: what a code
  takes off (a percentage or an amount), the apps it applies to, its
  validity, and the partner it credits. A code typed on Discover, or
  arriving with an `ultrastore://code/<CODE>` link from a partner's QR code
  or page (the store registers the scheme; a second start hands the link to
  the running copy), is checked against the catalog and kept in force for
  30 days: the prices show what it takes off ("23.92 EUR (was 29.90)"), the
  details page names it, Settings shows it and forgets it. `ultrastore code
  [<CODE> | clear]` does the same from the command line.
  `ultrastore://app/<id>?code=<CODE>` opens an app's details with the code.

#### 2026-10-27 *0.1.3*
- **`ParseComponent` / `SerializeComponent`** (Catalog.h): one component as
  the catalog writes it, for the admin backend's import and export - what
  `ultrastoreadmin` keeps is what the store reads.
- **Key rotation through the catalog (schema 2).** The catalog carries
  `trusted_keys`, the public keys that may sign the catalogs after it;
  `CatalogClient` keeps the last accepted catalog's list in its cache and
  verifies the next one against the compiled-in keys, the ones added in the
  settings and that list (`KeysTrustedNow()`). A new key is announced by a
  catalog the old key signed and accepted from then on, before the store
  binary that carries it ships; a key no accepted catalog announced is
  refused whatever the catalog it signs claims, a catalog cannot drop a
  compiled-in key, and a schema 1 catalog announces nothing. The Settings
  page says how many keys the catalog announces.

#### 2026-10-26 *0.1.2*
- **The window.** `ultrastore` started plainly opens it: a sidebar with
  Discover, Categories, Installed, Updates and Settings, the download queue
  in a collapsible panel along the bottom, a status line. Discover searches
  the catalog and shows every app as a card with its icon, summary and state
  (installed version, update available, not for this system, price) and the
  action that fits: Install (or Try for a paid app with a trial, and Buy…
  to the publisher's page until payment lands), Open, Update, Remove,
  Details. Categories filters the same cards with a chip per category.
  Installed lists the apps, the runtimes and libraries under them, and the
  clean-up. Updates lists what has a newer release, one by one or all at
  once, with the count on the sidebar. Details shows one app whole: the
  badges (price, age rating, open source, needs an account, update period,
  deprecated or withdrawn, translated automatically), the description, the
  screenshots, the release's notes and what it needs, the links. Settings
  holds the channel, the texts' language, the servers, the keys trusted
  beside the compiled-in ones, fetch-at-start, and what the store knows
  about the machine, its folder and the catalog. Every operation runs
  through the core on one worker thread at a time; each is a row of the
  Downloads panel with a progress bar, Pause / Resume and Cancel.
- **Shown or hidden as the admin backend says** (`StoreQuery`): a listed app
  always, an unlisted one by its exact id, a hidden or withdrawn one only
  when installed; an installed app always, for its update and removal.
- **Settings in the framework's place** (`StoreSettings`,
  `UltraCanvasAppSettings("UltraStore")`): channel, language, servers,
  additional trusted keys, the window's size and the panel's fold; the
  command line and the window read the same file.
- **Compiled-in keys and servers.** Every `Apps/UltraStore/keys/*.pub` and
  the CMake variable `ULTRASTORE_SERVERS` reach the program through the
  generated `UltraStoreBuiltin.h`; both are empty until the admin backend
  publishes the first catalog, and Discover says so.
- **Pictures from the catalog** (`AssetCache`): icons and screenshots,
  relative to the catalog's URL, fetched once into `cache/assets/` and shown
  from there offline.
- **`--catalog <file>`, `--root <dir>`, `--channel beta`** open the window
  on a local catalog (unverified), a store folder of one's own, a channel.
- One copy per user (a second start brings the window forward); a GUI
  program on Windows with `ultrastore.com` beside it for the prompt; the
  freedesktop entry and the icon on Linux. `cli/main.cpp` became
  `cli/Commands.cpp` (`UltraStore::Cli::Run`), `ui/main.cpp` decides between
  the window and a command. `FormatBytes` moved to the core.

#### 2026-10-25 *0.1.1*
- **UltraInstaller: install, update, remove.** The store now does what it
  planned. `Downloader` streams a package through UltraNet into
  `downloads/<sha256>.<format>`, resumes a partial file with a Range request,
  checks the catalog's SHA-256 and deletes what does not match; `SqliteRegistry`
  remembers every installed version on UltraDatabase in `registry.sqlite`
  (one row per app or component version, the app's current one marked, the
  exact dependencies it was installed with); `ArchiveInstaller` unpacks zip,
  7z and tar.* through VirtualFS - and copies an AppImage - into
  `apps/<id>/<version>` or `components/<id>/<version>`, staged beside the
  destination and renamed into place so a failure leaves nothing half
  installed, makes the entry executable and points `apps/<id>/current` at
  it; its garbage collector removes the app versions and components no
  current app needs. `StoreEngine` ties them together for the command line
  and the window alike. New commands: `ultrastore install <catalog> <app>`,
  `update <catalog> [<app>]`, `remove <app>`, `installed`, `gc`, each with
  `--root <dir>`. Tests run the whole path against a local HTTP server; the
  Linux CI legs install libarchive so they run rather than skip.

#### 2026-10-24 *0.1.0*
- **The store's core and command line.** `Apps/UltraStore` begins with
  `UltraStoreCore`, a static library (`ultrastore_core`) that knows the
  machine (`PlatformInfo`: os, processor, OS version, glibc, distribution,
  Rosetta), reads and writes the signed catalog (`Catalog`: apps and the
  runtime / library components they run on, releases with a package per
  target, exact-version dependencies, the admin backend's settings - minimum
  OS version per package, pricing, update period, visibility, lifecycle and
  flags), checks the catalog's Ed25519 signature and the packages' SHA-256
  (`Verifier`, UltraCrypt with libsodium), picks the package for the machine
  and lays out the install plan with the shared components first and the
  installed ones skipped (`PackageResolver`), and fetches the catalog from the
  first server that answers, verified, refused when older than the one seen,
  cached for offline use (`CatalogClient`, UltraNet). The `ultrastore`
  program drives it from the terminal: `platform`, `list`, `resolve`,
  `verify`, `fetch`, `keygen`, `pubkey`, `sign`, `sha256`. The target
  requires libsodium and SQLite and stops the configure without them
  (`-DBUILD_ULTRASTORE=OFF` leaves it out). Tests under `Tests/UltraStore`
  (`ULTRACANVAS_BUILD_ULTRASTORE_TESTS`), run by CI. The design is
  `Docs/UltraStore/README.md`.
