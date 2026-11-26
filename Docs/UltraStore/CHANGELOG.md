#### 2026-11-26 *0.1.32*
- **Every text in 29 languages.** The application had no translation, so
  it showed English whatever the language; its whole catalogue is now
  translated into all 29 (machine drafts, reviewed by -; Brazilian
  Portuguese uses the Portuguese).

#### 2026-11-25 *0.1.31*
- **Advertising: the console around a campaign** (`Server/UltraStorePortal` 0.6.0, its README's *Advertising*, *The console*; the first half of phase 2 of `Docs/Research/UltraAdsDesignProposal.md`).
  - **Search words of the publisher's choosing**, each matching every word in any order, the words together, or exactly the search - and words a campaign is *never* shown for. A campaign with its own words is shown for those searches and no others. The relevance floor still holds: the listing itself must hold half of what was searched, so a word bought for a listing that has nothing to do with it shows nothing at any price.
  - **What people searched** when a campaign was shown, with the click rate. A search is named only once it was shown often enough to be many people's; a rare one is counted under *other searches*. Kept a month.
  - **An offer per place**, suggestions read from a campaign's numbers, and **the report as a CSV file** (a search that a spreadsheet would run as a formula is written as plain text).
  - **Roles**: of a publisher's people Marketing runs the campaigns, Billing pays into the balance, Analyst reads; the apps, listings and releases stay an owner's and a developer's.
  - **The advertising policy** as a document with versions, accepted where a campaign is started or changed; a new version makes every campaign wait until its publisher accepts it. None is seeded: the operator writes it.
  - **The library** (`/promote/library/`, `GET /v1/promote/library`): every advertisement that runs or ran in the last twelve months, for anyone - what, who pays, when, where, for which searches.
  - ***Not this one again*** on every advertisement, remembered in the browser alone: the store is not told who hid what.
  - The operator's page gains revenue by day and the policy in force. Not built yet: several listings in one campaign, banners with their review queue, fixed slots, invoices.

#### 2026-11-24 *0.1.30*
- **Advertising in the store: sponsored listings** (`Server/UltraStorePortal` 0.5.0, its README's *Advertising*; UltraAds, phase 1 of `Docs/Research/UltraAdsDesignProposal.md`). Off unless switched on (`ULTRASTORE_PORTAL_PROMOTE=1`).
  - **A publisher promotes an app** from its console page: *where* - every shop and marketplace with its places, this shop's ticked and the others listed (the shops that are not built yet as *not open yet*) - *how much* a day, *how long*. The app's own card then appears, marked *Sponsored*, in a search it answers, at the top of its category, in a row of its own on the front page and beside apps of its category.
  - **Chosen from what is on the page and never from who is looking**: the search words, the category, the app beside it, the system. The choosing code is handed no request, session or account. *Why this ad?* on every advertisement says who paid and what matched, and takes a report.
  - **Money moves no unpaid result.** The store's search and its order stay as they are; the editor's picks are not for sale. An advertisement that does not answer the search is not shown at any price, and a short list does not repeat above itself an app it already shows.
  - **Paid per click, from a prepaid balance**: the winner pays what it took to stay ahead of the next publisher's offer, never less than the placement's floor, which the operator sets. A click is charged once; the publisher's own click, a burst from one address and what the operator credits back are not charged. At zero the campaigns wait, and the publisher is mailed before. Paying money in is a second switch (`ULTRASTORE_PORTAL_PROMOTE_TOPUPS`), off until the store writes tax invoices: until then a balance is the operator's voucher.
  - **Counted without following anyone**: shown, clicked, spent, installed and ordered are numbers per campaign, place, country and day. A click row holds no account and no address and goes after a month. An order counts for a campaign through the token the buyer's own store kept and sent with the checkout (`POST /v1/checkout`, `promote`); a country that rests on a handful of showings is folded into "other".
  - **The operator's page** (`/operator/promote/`): the placements with their floors, every campaign with *Stop* and its reason, vouchers, clicks to credit back, revenue by place, the settings.
  - **For a store's client** (not used by the native store or the web store yet): `POST /v1/promote/decide` - refused when it carries an `Authorization` header - `POST /v1/promote/events`, `GET /v1/promote/why/<token>`.
- **One sign-in for every service: the portal is an OpenID Connect provider, and the account can have an address of its own** (`Server/UltraStorePortal` 0.4.0, its README's *One sign-in for every service*). A shop, a service or a merchant's own site sends a person to the account and learns from it who signed in - the way "Sign in with Google" works, by the same protocol - without ever seeing the password.
  - **The provider** (`identity/`): the authorization code flow with PKCE (S256) and no other; the discovery document, the keys (`/oauth/jwks`), `/oauth/authorize`, `/oauth/token`, `/oauth/userinfo`, introspection for a service that is handed other services' tokens, revocation, and sign-out from a service. Identity tokens are signed RS256 with a key from a PEM file or one made on first use, rotated with `oidc rotate-key`. Written on the `cryptography` package the portal already had: no new dependency.
  - **The account at its own address**: `ULTRASTORE_PORTAL_ACCOUNT_URL` (in production `https://account.ultraos.eu`) is a second host the same application answers - the account's pages at its root and the provider's endpoints, nothing of the store. The store's host then has no password page: it signs people in through the account like any other service, each host keeps its own session cookie, signing out at the account signs the store out, and the store's *Sign out* signs both out. Unset, everything stays under the store's root as it was. The address is the issuer inside every token: it is chosen before the first service is registered and not changed.
  - **Services**: registered in the admin site or with `ultrastore-portal oidc client`; a server's secret is shown once and kept as a hash. One of ours (`--trusted`) needs no consent page and sees the same subject for a person as our other services; every other service is asked for on a consent page and gets a subject made for it alone, so two of them cannot compare. An application on the person's own machine registers `http://127.0.0.1/...` and is answered on whatever port it got.
  - **What is taken back**: a code shown twice takes back what the first showing got, a refresh token shown twice ends its grant, a new password ends every grant and every other sign-in, and a person sees on their account's page which services they signed in to and takes the access back.
  - **After an independent review of the code**, each finding fixed with a test: `prompt=login` is satisfied only by a password typed after the account asked; an application on the person's own machine is asked for every time, ours or not; a refused code or token no longer counts towards the slow-down that guards client secrets (it let anyone shut a service's sign-in); the store's sign-in no longer loops once the account's session is older than two weeks; the password given again, or someone else signing in, no longer leaves the store signed in after a sign-out; a refresh that asks for less keeps the whole grant; a client switched off holds no good tokens; the mailed confirmation link signs in once and never replaces whoever is signed in; a password set in the admin site or on the command line ends every grant too; malformed tokens, credentials and addresses are refused instead of answered with an error page; with two hosts the cookies are `__Host-` cookies and neither host trusts the other's forms.
  - Not yet run against the OpenID Foundation's conformance suite and not reviewed from outside the project; both come before a service that is not ours is registered. No second factor at the sign-in yet.

#### 2026-11-23 *0.1.29*
- **The portal's developer console keeps what was typed, and the portal is on Bootstrap** (`Server/UltraStorePortal` 0.3.0, its README's *The pages*).
  - **No release before the listing is filled in.** An app's page offered *New release* from the moment the app existed, and the release then could not go to review. Until the listing has what a review needs - a summary, and for an app a description, a category, an icon and a screenshot - the page offers *Edit the store listing* in its place, and `/console/apps/<id>/releases/new/` sends back to the listing.
  - **The icon and the screenshots are saved where they are chosen.** *Add screenshot* and the icon's file field each reloaded the listing page and lost whatever had been typed into it. The pictures are now dropped on, or picked through, an upload area and uploaded at once, each by a request of its own (`POST .../icon/`, `.../screenshots/`); the page draws the new picture and what the listing still lacks without loading again. The icon is no longer a field of the listing form.
  - **A field shows only while it applies**: price, currency, trial days and free updates only for an app that is not free, *Updates until* only for free updates until a date, *Source url* only for an open-source app, *Successor* only for a deprecated one, *Category* only when creating an app (not a runtime or a library); a package's processor offers *universal* for macOS and *wasm32* for WebAssembly only. The server agrees with the page: a hidden field's value is not kept and its errors keep nothing from saving (`DependentFieldsMixin`).
  - **A package is edited after its upload.** While its release is a draft or has changes requested, a package's system, processor, needs and entry are corrected in a dialog (*Edit* in the packages table; `.../packages/<n>/edit/`) - a slip no longer means removing the package and uploading the archive again. The archive and its SHA-256 stay as uploaded, the entry is looked up in the stored archive again, and every edit is in the audit log (`package.edit`). In review and once published the packages are closed, as the release is.
  - **Packages are added and removed in place**, with the upload's progress shown; an error is named at its field and the chosen archive stays chosen, and release details typed on the same page are not lost.
  - **Bootstrap 5.3** for every page - the store, the accounts, buying, the console, the operator's - in place of the portal's own classes: its grid, forms, buttons, cards, tables, alerts, navigation (collapsing on a phone) and dialog, themed by `portal.css` to the design's colours, radii and pill buttons. Bootstrap and Dropzone 6.3 (the upload areas) are copied into `static/portal/vendor/` as published; nothing is fetched from another host and there is still no front-end build.
  - Every in-place address answers a browser without the script as before, with a redirect and a message.
- **A portal password is 8 to 64 characters** (`Server/UltraStorePortal`): the shortest stays at 8 and there is now a longest, 64 - counted in characters of Unicode text, not in bytes, so "ä" or "€" is one. The rule is checked wherever a password is chosen (sign-up, a reset, a change, `createsuperuser`) and never at a sign-in: an account whose password is older than the rule and longer keeps it until it is changed. The two numbers are named once (`PASSWORD_MIN_LENGTH`, `PASSWORD_MAX_LENGTH` in `settings.py`); the sign-up page said "twelve characters or more" after the minimum had become 8 and now says what is checked.
- **The B2B trade variant of the design** (`Docs/UltraStore/design/Trade*.dc.html`, on the design canvas): a sourcing marketplace in the manner of Alibaba - the home with the products / suppliers search, the request-for-quotation box, categories, ready-to-ship products priced by quantity tier, verified suppliers and the trade-assurance steps; a product whose quantity picks the price tier and computes the total and the deposit; a supplier's profile with what the operator verified; the buyer desk with a request's quotes compared and awarded into an order in escrow; the phone's source screen. The design README says what of it exists (merchant verification, the payment service, submissions and review) and what does not (buyer companies, price tiers, quotations, escrow, disputes).

#### 2026-11-20 *0.1.28*
- **The portal sells** (`Server/UltraStorePortal` 0.2.0, its README's *Buying*): the checkout, the licence and the signed download link for paid apps, with the protocol the store's client already speaks (`Apps/UltraStore/core/Purchase.cpp`; the catalog's `payment_url` is the store's root, `licence_key` the licence key's public half, both written once the store is configured to sell).
  - **An order** from the store's app (`POST /v1/checkout`) or from an app's page (*Buy on the web*, with an email for the receipt), priced from the listing; `/pay/<order>/` hands the buyer to **Skrill Quick Checkout**, and only Skrill's signed server-to-server report (`POST /v1/skrill/status`: signature, amount and currency checked against the order) pays it - a repeat changes nothing, a forgery is refused, the browser's return proves nothing.
  - **The licence** `ULIC1.<payload>.<signature>` (Ed25519, what the purchase includes, a subscription's `valid_until`), the numbered receipt, the buyer's mail, and the buyer's page of a purchase (`/orders/<order>/?secret=`: licence, `licence.ulic`, receipt, a download per system).
  - **The signed link**: a paid app's packages without a trial under `v1/restricted/`, served by nginx's `secure_link_md5` to a link the portal signs for the order's buyer for ten minutes (`POST /v1/orders/<order>/download`).
  - **Subscriptions** (monthly, yearly) through recurring billing: renewals extend the paid period and re-issue the licence under the same id, a failed one is `failing` through the grace days, the buyer cancels from the store or the order's page.
  - **Refunds and chargebacks**: the operator's refund from `/operator/orders/` through Skrill's API, whole or in part; a chargeback from Skrill's report; the licence taken back when everything paid is reversed.
  - **Sales for a publisher** (`/console/sales/`: every payment and reversal, the net per currency, never who bought), orders for the operator.
  - A **test provider** for a developer's machine (`ULTRASTORE_PORTAL_PAYMENT_PROVIDER=test`, with `DEBUG` only): a button in Skrill's place that sends the same signed report through the same code.
  - Not exercised against Skrill itself yet: a sandbox run is the next step. Tax invoices, VAT and payouts to publishers are not built.
- **MySQL is the portal's production database** (decided 2026-11-20): the tests run on MySQL 8.0, PostgreSQL and SQLite in CI.
- **The UltraStore portal** (`Server/UltraStorePortal`, its own README): one Django application for the whole store under one root, `https://store.ultraos.eu`, built to replace the small services (`Server/UltraStoreMarket`, `UltraStoreAccount`, `UltraStorePay`) and the two desktop tools on them (`ultrastoreadmin`, `ultrastoreop`). 0.1.0 is the essential path:
  - **accounts** - sign up with a mailed confirmation link, sign in, password reset;
  - **the developer console** (`/console/`) - apply for a publisher account with the terms in force accepted, create an app, its store listing with icon and screenshots, a release with a package per system and processor (hashed on upload, checked to be the archive its name says and to hold its entry, private until published), *Send for review*, withdraw, unpublish;
  - **the operator's pages** (`/operator/`) - publisher applications approved or rejected, releases reviewed (*Download build*, approve and publish, request changes, reject), apps suspended and picked for the front page, reports closed with the line the reporter reads, every step in the audit log; Django's admin site for the records;
  - **the store's web pages** - Discover, categories, search, an app's page with screenshots, what's new and direct downloads, a publisher's page, *Report a problem*;
  - **the JSON service** (`/v1/`) - the one signed `catalog.json` (schema 2, as `Catalog.cpp` reads it, with its Ed25519 signature, an `ETag` and a sequence that only moves when the content does), the apps as a filtered, paged list, one app, the categories, a publisher, the signing key, and the native store's report door.
- **Decided with it** (2026-11-20): one store and one catalog, no private or per-publisher catalogs and no publisher keys; every URL under the one root; no compatibility with the earlier services, which were not in use - only the catalog's format, the buyer client's, is kept.
- **The review pipeline** (the README's *The operator tier*, *What do the automatic checks do?*): the marketplace service runs the checks behind the ones it makes at once, in the background once a submission's packages are in the store - `scans` (the archives walked for paths that escape, links outside, a zip bomb, an installer; a virus scanner through `ULTRASTORE_MARKET_SCAN_COMMAND`), `entry` (the file the store launches is in the archive), `smoke` (a sandbox runner through `ULTRASTORE_MARKET_SMOKE_COMMAND`, not run without one) and `listing` (what the store's page needs) - and keeps each check's findings with the submission (schema 4); a failed scan or entry makes it `failed_checks`; `POST /v1/submissions/{id}/checks` runs the pipeline again. The `scan` extra opens 7z and tar.zst packages.

#### 2026-11-20 *0.1.27*
- **The package store** (the README's *The operator tier*, *Where do a merchant's packages go?*): the marketplace service keeps the packages a merchant uploads with a submission (`POST /v1/submissions/{id}/packages/{sha256}`, the bytes signed like every request, hashed and kept under `<sha256>.<format>` in `ULTRASTORE_MARKET_PACKAGE_DIR`); the `packages` check waits for them and a release is refused while one is missing; the operator downloads a build (`GET .../packages/{sha256}`); the publish lays the uploaded packages out beside the catalog (`packages/`, `restricted/`) under `ULTRASTORE_MARKET_CATALOG_URL`. A package not uploaded keeps the merchant's URL.
- **The operators' second factor and the forgotten password**: two-factor sign-in with an authenticator app (`/v1/operators/me/two-factor/setup | enable | disable | recovery-codes`, the sign-in's `totp` or `recovery_code`, 401 `two_factor_required`), a reset code by mail (`/v1/operators/password/forgot | reset`, `ULTRASTORE_MARKET_SMTP_URL`), an admin's reset of another's second factor; `ultrastore-db` 0.4 shares the TOTP and code helpers with the account service. Both services now log their own lines (a mailed code on a machine without a mail server).

#### 2026-11-20 *0.1.26*
- **Operator accounts with roles** (the README's *The operator tier*, *Who sits at the operator's window?*): the marketplace service keeps the operator accounts (`operators`, schema 3: email, scrypt password, a role - admin, reviewer, support, auditor) and `POST /v1/operators/signin` answers a signed session (`ultrastore-db` 0.3, `operator_session`: `op1.<payload>.<hmac>`) that the account service verifies with the shared secret (`ULTRASTORE_MARKET_OPERATOR_SESSION_SECRET`, `ULTRASTORE_ACCOUNT_OPERATOR_SESSION_SECRET`), so one sign-in reaches both with the same name and role; every operator call of both services names a permission and refuses the rest (403 `forbidden`); the audit logs name the person; `GET /v1/operators/me`, `GET|POST /v1/operators`, `PUT /v1/operators/{id}`. The static operator tokens stay the bootstrap admin. The design's `SuperOperators` artboard.

#### 2026-11-19 *0.1.25*
- **The account tier** (the README's *The account tier*; the design's `Account*` pages): the account service `Server/UltraStoreAccount` - sign up with the mailed code, sign in with the password and the second factor (TOTP with recovery codes), the sessions as devices, the profile and the settings, the email moved after a code, the password changed with the other machines signed out, the licences attached to the account and counted in seats, the notices (the operator's customer messaging, and the service's own security notices), the data archive released by the operator and downloaded once, the deletion with its 30-day grace, the operator's users API and its hash-chained audit log; `POST /v1/maintenance` for the daily timer. The store's side (`Account.h`): the session in `<root>/account.json`, readable by the owner alone; the licences kept in step both ways at a sign-in (`SyncLicencesWithAccount`, the `AccountSync` job); the header's *Sign in* pill, the person's initials and name once signed in; the sign-in page with its steps and the account page with its rail - profile, security and sign-in, devices, purchases and licences, notifications, privacy and data (`ui/StoreAccountPage.cpp`); the Settings page's account service of one's own; `ultrastore account signup | verify | signin | signout | me | devices | licences | notices | sync`. The catalog names the service (`account_url`, schema unchanged). Tests: `Tests/UltraStore/test_account.cpp`, the service's own under `Server/UltraStoreAccount/tests`.
- **Customer messaging decided**: a notice on the account, written by the operator or the service, kept under Notifications and sent by mail; a merchant never reaches a buyer directly - its message is relayed through the marketplace service with the order as the key, later (the README's *The account tier*).
- **Merchant self-service** (the README's *The operator tier*, *How does a merchant join?*): the marketplace service's applications - `POST /v1/merchants/apply` with the shop's name, contact, email, website and description and the key that will sign its submissions, the application signed by that key; `GET /v1/applications/{id}?secret=` for the applicant; the operator's queue, approve (the merchant registered with that key and profile) and reject with a reason - and the merchant's own profile, `PUT /v1/merchants/me` (name, contact, website, description; a key given is the rotation, signed with the old one). The merchants gain a website and a description (schema 2, the columns added to a 0.1 database); the publish names the publisher's page from the website. The design's `AdminMarketplace` artboard and the applications section of `SuperMerchants`.

#### 2026-11-19 *0.1.24*
- **The e-book and audiobook shop variant of the design** (`Docs/UltraStore/design/Books*.dc.html`, on the design canvas): the shop's chrome as a book shop in the manner of Audible - the home with the book of the month in both editions, continue listening and reading with the place kept between the audiobook and the e-book, the genres, the editor's picks, Books Plus (one credit a month through the store's subscription, the book kept), a series with the next part on pre-order; a title's page with the editions, the narrator, the chapters both editions share, reviews and the author's and narrator's share of the price; browse with working format, genre, language and Plus filters; the library with progress, the device the place was last kept on, bookmarks, highlights and the files on the machine; the phone's listening screen with chapters, skips, speed, the sleep timer and *read here*. A design only: UltraViewer's e-book viewer, UltraCanvas Audio's M4B playback and the store's subscriptions exist, a book catalogue does not; every book, author, narrator and figure is sample data.

#### 2026-11-19 *0.1.23*
- **The music streaming variant of the design** (`Docs/UltraStore/design/Music*.dc.html`, on the design canvas): the shop's chrome as a streaming service - the home with the library rail, mixes, new releases, the charts and podcasts; an album page with the track table and the artist's share of the subscription; an artist page with working discography filters; search with working type and genre filters; the phone's Now playing with lyrics; the now-playing bar on every desktop page. A design only: UltraCanvas Audio and the store's subscriptions exist, a streaming service does not; every artist, album and figure is sample data.
- **The account pages of the design** (`Docs/UltraStore/design/Account*.dc.html`): sign up, sign in and the email code with working steps, the profile, security (two-factor, passkeys, sign-in history), devices with licence seats, notifications, privacy and data, payment methods, subscriptions, purchases and licences, orders and invoices, the phone's account and sign-in pages, the password reset and two-factor setup flows with the delete confirmation, and the operator's users page - shared by every shop on the canvas. A proposal: the store has no accounts today; the README says what an account service would tie together.

#### 2026-11-19 *0.1.22*
- **The ULTRA OS Project's own apps go through the marketplace** (decided
  2026-11-18, the README's *The operator tier*): the project is the merchant
  `ultra-os-project`, its admin backend submits each release, and the
  public catalog has one writer, the marketplace's publish; the merchant
  backend's `publish` stays for a store of one's own. The README names what
  the handover needs - the operator's key committed as `keys/market.pub`
  and announced by a last catalog signed with `publish.pub` (which the
  admin backend cannot do yet), a first sequence above the public
  catalog's, the project's codes leaving the public catalog - and a new
  section, *Packaging an application for the store*, on
  `scripts/package-store-app.sh`.
- **Open starts an app on its runtime layer.** The store installed an app's
  runtime and libraries beside it and then started the app with nothing on
  its library path, so an app whose package left the runtime out installed
  and did not start - and every package had to carry the whole runtime, a
  hundred megabytes each. `PlanLaunch` (`StoreQuery.h`) now puts the
  components the app was installed with - the exact versions of its
  `depends`, and theirs in turn - first on `LD_LIBRARY_PATH` (a component's
  `lib/`) or `PATH` (its folder, on Windows), and names the runtime's folder
  in `ULTRACANVAS_RUNTIME_DIR`, where the framework finds media and
  UltraNet's plug-ins after the app's own. A self-contained app (no
  `depends`) starts as before; on macOS nothing is added, an app there being
  a bundle with its own `Frameworks/`. The README's *Packaging an
  application for the store* describes the three package shapes of
  `scripts/package-store-app.sh`.

#### 2026-11-17 *0.1.21*
- **The video portal variant of the design** (`Docs/UltraStore/design/Video*.dc.html`, on the design canvas): the shop's chrome as a portal with channels - the home with the sections rail, topic chips, the video grid, Shorts and live; the watch page on the UltraCanvas video element with chapters, captions, the channel row, comments, up next and the transcript; a channel page with working sort and kind filters; the creator's Studio with the upload, the details form and the videos table; the phone's watch page. A design only: the video element and UltraVideo's export exist, a portal service does not; every channel, video and figure is sample data.
- The marketplace variant's write-up names the store's own payment service and Skrill Quick Checkout's hosted page for payment, as the store README decides it, in place of a reference to the UltraPress proposal; the variants are UltraStore's own business.

#### 2026-11-17 *0.1.20*
- **The marketplace variant of the design** (`Docs/UltraStore/design/Market*.dc.html`, on the design canvas): the shop as a general marketplace with many sellers - the home with deals, departments, best sellers and top sellers, a product page with the buy box, other offers, reviews and questions, search and browse with working department, price, rating, delivery and condition filters, the basket grouped by seller with the checkout steps, and the phone's home. The words are the standard set's `Shopping.json`; payment is the store's own payment service with Skrill Quick Checkout's hosted page, as the store README decides it. A design only: every product, seller, price and rating is sample data.

#### 2026-11-17 *0.1.19*
- **The games shop variant of the design** (`Docs/UltraStore/design/Games*.dc.html`, on the design canvas): Discover, a game's page, the genre browser with working genre, price, age-rating and players filters, and the phone's Discover - the shop's chrome with a violet accent, covers where the apps have icons, the genres Action, 3D, Simulation, Tactics, Adventure, Learning (by age band), Retro, Board games, Puzzle, Racing, Role-playing and Sports, and on the game page the PEGI rating with its descriptors, players and co-op, controller support, a trailer, system requirements and a demo. A design only: the catalog knows the category `games` and none of those fields yet; every game on the pages is sample data.

#### 2026-11-16 *0.1.18*
- **The web store's `site/` never carries a test key.** A build configured
  with `-DULTRASTORE_WEB_TEST_KEYS` - every CI build, since the browser
  test's fixture catalog is signed with a key made for it - compiled that
  key into the one module there was, so no run left a `site/` a store could
  be published with. Under emcmake the option now builds a second module
  from the same sources, `ultrastore-web-test.wasm`, that trusts the store's
  keys and the test keys; it stays beside `site/`, and `site/`'s
  `ultrastore-web.wasm` trusts `Apps/UltraStore/keys/*.pub` and nothing
  else, whatever the build was configured with. No new keys were needed:
  the production key is the one already there, the native store's.
  Natively the option still adds the keys to the one program, which is a
  tool to look at a publish folder.
  - The browser test holds the build to it: the store's module's bytes hold
    the store's keys and not the fixture's (the test module, which holds
    both, is the control), it is the module the 150 KB Brotli budget is
    measured on, and served in the test module's place it runs and refuses
    the fixture's catalog.
  - `test/stage-store.mjs` stages the store the test opens - the fixture
    with the test module, the loader, the catalogues and the runtime laid
    over it as the admin backend's publish lays a `site/` - and
    `wasm-build.yml` uploads it as `preview/` beside `site/` in the run's
    `ultrastore-web-dynamic` artifact (the framework's changelog has the
    workflow's side of it).

#### 2026-11-14 *0.1.17*
- **The web store** (`Apps/UltraStore/web`, `Docs/UltraStore/WebstoreBrief.md`): the shop's Discover, categories and app pages as an UltraCanvas application a browser runs - a side module on the UltraCanvas runtime, from the UltraCanvas extension when it is installed and from the site's `runtime/<hash>/` otherwise - served from the catalog base by the admin backend's publish. The catalog comes from the site's own origin and is verified in the browser (UltraCrypt through the runtime, WebCrypto's Ed25519 as the fallback) against the keys built in and the ones an accepted catalog announced, with the sequence check; a catalog that does not verify is not shown. *Install*, *Try* and *Buy* open the native store's `ultrastore://app/<id>` link; the page offers the store's download where the link finds none. Every page has an address (`?page=app/<id>`, the published `app/<id>/`), with an HTML shadow for search engines and the moment before the module paints. The site carries the translation catalogues and the module fetches the visitor's language's before it draws. Builds natively as `ultrastore-web <publish folder>` with the demos.
- **The catalog's picture variants**: `icon_variants` and `screenshot_variants`, entries `{format, width, url}`, read leniently and written when present - the pictures at the web store's sizes and browser formats, which it picks by the format the browser decodes and the device pixel ratio, never resizing in the browser. A store that does not know the keys is unaffected.
- The catalog's writer keeps a price or an update date set on an entry before its model or period is: the admin's `app set` fields no longer vanish between two commands.

#### 2026-11-12 *0.1.16*
- **The operator's window and command line.** `Apps/UltraStoreOperator`,
  `ultrastoreop` (its own README and changelog): the marketplace service's
  operator side - the merchants, the review and the publish, the reports,
  the notices, the terms and the policy, the audit log - on the service's
  API with the operator token. The service (0.2.1): `GET /v1/released`
  says the last publish's sequence as `published` beside the last
  release's `sequence`.

#### 2026-11-12 *0.1.15*
- **Report a problem.** The app page gets *Report a problem* when the
  catalog names the operator's marketplace service (`market_url`): the
  kind and the text go to the service's reports, the licence names the
  reporter when the app was bought here, and the outcome the operator
  gives shows on the page after a refresh. `ultrastore report
  <catalog.json> <app-id> <kind> <text>` and `ultrastore reports` do the
  same from the command line; the receipts are kept in the settings.
- **The operator publishes the catalog.** The marketplace service (0.2.0)
  generates the signed `catalog.json` from the released submissions
  (`POST /v1/publish`): the merchants' components as sent, the
  publishers, the trusted keys, the payment service, `market_url`; the
  operator's signing key is kept by the service and `GET /v1/publish/key`
  gives its public half for the store's keys.

#### 2026-11-12 *0.1.14*
- **The marketplace service** (`Server/UltraStoreMarket`, milestone 8):
  the operator tier's records in one service - the merchants with their
  keys, signing every request; the submissions with the service's own
  checks and every decision appended; the reports through the three doors
  (the store, a merchant, the operator), the reporter kept from the
  merchant; the notices, the strikes and the enforcement policy that
  turns them into a suspension; suspensions by hand and by the policy;
  the terms versions and acceptances, a merchant paused without the one in
  force; the hash-chained audit log with `GET /v1/audit/verify`;
  `GET /v1/released` for the catalog. Tested on SQLite, MySQL and
  PostgreSQL.
- **One database layer for the services** (`Server/UltraStoreDb`,
  `ultrastore-db`): MySQL joins PostgreSQL as a production engine, SQLite
  stays for tests and development. The marketplace service runs on MySQL
  (decided 2026-11-11); the payment service takes the layer too and runs
  its tests on all three engines (`ULTRASTORE_PAY_TEST_MYSQL_URL`).

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
