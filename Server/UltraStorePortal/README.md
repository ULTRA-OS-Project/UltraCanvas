# The UltraStore portal

One server application for the whole store, at one address:
**<https://store.ultraos.eu>**. A [Django](https://www.djangoproject.com/)
project that is, in one place, what a store like Google Play is in two:

- the **developer console** - a person signs up, applies for a publisher
  account, creates an app, fills in its store listing, uploads a release's
  packages and sends it for review;
- the **operator's pages** - the queue of publisher applications and of
  releases in review, the decision on each, the reports, suspensions, the
  editor's picks, the audit log;
- the **store's web pages** - the front page, categories and search, an
  app's page with its screenshots, what's new and its downloads;
- **buying** - an order opened by the store's client or from an app's
  page, paid at the payment provider, the licence an app checks, a
  subscription's renewals, refunds, and the short-lived link to a paid app's
  package;
- the **JSON service** a buyer's client reads - the whole signed
  `catalog.json` the native store already parses, a browsable list of apps,
  and the checkout;
- the **account** every ULTRA OS shop and service signs in with - sign-up,
  sign-in and the profile, and an **OpenID Connect provider** through which
  another service (the store itself, cloud storage, the chat, advertising,
  a merchant's own site) signs a person in without ever seeing the password
  (*One sign-in for every service*). It can have an address of its own,
  `https://account.ultraos.eu`, served by this same application;
- **advertising** (UltraAds, its first phase) - a publisher promotes an app,
  whose own card then appears marked *Sponsored* in the store's search, its
  lists and beside similar apps; paid per click from a prepaid balance,
  chosen from what is on the page and never from who is looking
  (*Advertising*). Off until it is switched on.

It is built to replace the store's small services (`Server/UltraStoreMarket`,
`UltraStoreAccount`, `UltraStorePay`) and the two desktop tools on them
(`ultrastoreadmin`, `ultrastoreop`). 0.1.0 was the essential path - register
a publisher, publish an app, show and serve the catalog; 0.2.0 sells; 0.4.0
is the account for every service; 0.5.0 is advertising's first phase and
0.6.0 the first half of its second; 0.7.0 splits a sale between the store
and the publisher; 0.8.0 lets a person close or delete the account; 0.9.0
reckons VAT, writes invoices and makes the publishers' payouts; 0.10.0 is
the affiliate programme's first phase and 0.11.0 its second, the customer's
binding; *What comes next* names the rest.

## Decisions

| Question | Decision |
|---|---|
| How many stores, how many catalogs? | **One.** One store, one public catalog, generated from what the review published. There is no private or per-publisher catalog, and a publisher holds no signing key: a publisher is an account, the operator's key signs the catalog (decided 2026-11-20). |
| Which addresses? | **Everything of the store under one root**, `https://store.ultraos.eu` (`ULTRASTORE_PORTAL_BASE_URL`): the pages, the console, the JSON service under `/v1`, the pictures, the packages. No URL the portal writes for the store names a second host (decided 2026-11-20). **The account may have a root of its own** (decided 2026-11-24): `ULTRASTORE_PORTAL_ACCOUNT_URL`, in production `https://account.ultraos.eu` - the same application answering a second host, because the account is every service's and not the store's, and because its address is written into every token for good. |
| Who signs people in for other services? | **The portal, as an OpenID Connect provider** (decided 2026-11-24): the authorization code flow with PKCE and nothing else, written on the `cryptography` package the portal already has rather than on a fourth dependency - one flow, and every refusal of it tested (*One sign-in for every service*). |
| Compatibility with the earlier services? | **None kept** - they were not in use (decided 2026-11-20). The portal names no payment, account or marketplace service in the catalog. What is kept is the **catalog's format** (`catalog.json`, schema 2, the Ed25519 signature beside it), because it is the buyer client's, not a service's: the native UltraStore reads the portal's catalog with the parser and the verifier it has. |
| Which stack? | **Django 5.2 LTS**, with its own accounts, forms, admin site and migrations, and three dependencies in all: Django, `cryptography` (the catalog's Ed25519 signature) and Pillow (checking uploaded pictures). No front-end build: server-rendered pages on **Bootstrap 5.3**, with Dropzone for the upload areas - both copied into `static/portal/vendor/` as published, nothing fetched from another host - a theme stylesheet and one script of the portal's own (*The pages*). The JSON service is plain Django views - ten read-only endpoints do not need a REST framework. |
| Which database? | **MySQL in production** (decided 2026-11-20; MySQL 8.0 is what the tests run on, MariaDB speaks the same driver), SQLite for tests and development only. PostgreSQL works as well; the tests run on all three in CI. |
| Who sells? | **The store, through Skrill Quick Checkout** (Paysafe) - the provider the store's design had settled on. The buyer pays on Skrill's page, the store never sees a card, and only Skrill's signed server-to-server report pays an order. The buyer client's protocol is kept like the catalog's format: `POST /v1/checkout`, the order polled with its secret, the `ULIC1` licence, the signed link (`Apps/UltraStore/core/Purchase.cpp`). |
| Where do packages live? | **Private until published.** An upload goes into the package store under its SHA-256, which no web server serves; the publish lays the packages of *published* releases out beside the catalog. A build in review is reachable by the reviewer alone. |
| What is reviewed? | **Every publisher once, every release each time.** A listing edit of an app that is already public is live at once - see *What comes next*. |

## The addresses

| | |
|---|---|
| `/` | the store: Discover |
| `/apps/`, `/category/<id>/` | browse, search (`?q=`, `?price=free\|paid`, `?sort=`) |
| `/apps/<app id>/` | an app's page; `.../download/<n>/` a package by the browser, `.../report/` a problem reported, `.../buy/` buying on the web |
| `/pay/<order>/` | the hand-off to the payment provider; `.../return/`, `.../cancel/` where it sends the buyer back |
| `/orders/<order>/?secret=` | a buyer's purchase: its state, the licence, the receipt, the downloads |
| `/publisher/<id>/` | a publisher's page |
| `/account/…` | sign up, the mailed confirmation link, sign in, password reset, the profile, closing or deleting the account (`leave/`) and coming back (`reactivate/`). With the account at its own address these are that host's root (`https://account.ultraos.eu/signin/`), and the store's `/account/…` leads there |
| `/.well-known/openid-configuration`, `/oauth/…` | the OpenID Connect provider, at the account's host (*One sign-in for every service*) |
| `/sso/callback` | where the account sends a person back to the store, with two hosts |
| `/promote/…`, `/console/apps/<app id>/promote/`, `/operator/promote/` | advertising: a publisher's campaigns and balance, the *Promote* page of a listing, the operator's prices and stops; `/promote/go/<token>/` a click on an advertisement, `/promote/why/<token>/` who paid for it and why it was shown (*Advertising*) |
| `/console/…` | the developer console |
| `/operator/…` | the operator's pages (staff only) |
| `/admin/` | Django's admin site: every record as it is (staff only) |
| `/v1/…` | the JSON service (below) |
| `/v1/packages/<sha256>.<format>` | the packages of published releases, served by nginx |
| `/v1/restricted/<sha256>.<format>?md5=&expires=` | a paid app's package, served by nginx to a signed link only |
| `/media/…`, `/static/…` | pictures and the stylesheet, served by nginx |
| `/healthz`, `/robots.txt`, `/sitemap.xml` | |

## The JSON service

Everything a `GET` answers is public and may be read from any origin.
Errors are `{"error", "error_description"}` with a 4xx status.

| | |
|---|---|
| `GET /v1/catalog.json` | the whole catalog, exactly the bytes that were signed; `ETag`, 304 on `If-None-Match` |
| `GET /v1/catalog.json.sig` | the Ed25519 signature over those bytes, hex |
| `GET /v1/apps` | the listed apps as cards: `?q=` (words in the name, summary, tags, id, publisher), `category=`, `publisher=`, `price=free\|paid`, `os=`, `sort=updated\|new\|name\|downloads`, `page=`, `per_page=` (24, at most 100) → `{count, page, pages, results: [{id, name, publisher: {id, name}, summary, category, icon, pricing, featured, version, updated, systems, url, web_url}]}` |
| `GET /v1/apps/<app id>` | one app exactly as the catalog carries it, plus `publisher_name` and `web_url`; an unlisted app answers by its id |
| `GET /v1/categories` | the store's fixed list in its order: `[{id, name, apps}]` |
| `GET /v1/publishers/<id>` | `{id, name, url, description, web_url, apps: [card]}` |
| `GET /v1/publish/key` | `{public_key, trusted_keys}`: the key the catalog is signed with |
| `GET /v1/terms/current` | the publisher terms in force |
| `POST /v1/reports` | `{target_kind: app\|release\|merchant, target, kind, text, reporter?}` → 201 `{id, secret, state}`: the native store's *Report a problem* (`Apps/UltraStore/core/Report.cpp`; the catalog's `market_url` is the root) |
| `GET /v1/reports/<id>/outcome?secret=` | `{id, state, outcome}`: the one line the reporter learns |

Buying - what the store's client asks (`Apps/UltraStore/core/Purchase.cpp`;
the catalog's `payment_url` is the root):

| | |
|---|---|
| `POST /v1/checkout` | `{app, code?, language?, country?, ref?, via?}` → the order (with `Authorization: Bearer <access token>` of the account's sign-in it is that account's; without, nobody's): `order_id`, `secret`, `amount`, `currency`, `list_price`, `status`, `cycle`, `checkout_url` (`<root>/pay/<order>/`, opened in the browser). 404 `no_such_app`, 400 `free_app`, 503 `not_configured` |
| `GET /v1/orders/<order>?secret=` | the order's state; when paid `licence`, `licence_id`, `receipt_no`, `paid_at`, the `payments`; a subscription's `paid_until`, `valid_until`, `subscription_status`; `revoked` with `revoked_reason` after a refund or a chargeback (no `licence` then) |
| `GET /v1/orders/<order>/receipt?secret=[&no=]` | the receipt of the sale, or of the payment with that number: a page for a browser, text for anything else |
| `POST /v1/orders/<order>/download?secret=&file=restricted/<sha256>.<format>` | `{url, expires}`: a link to one of the app's restricted packages; 409 once the licence is taken back or the subscription ended |
| `POST /v1/orders/<order>/subscription/cancel?secret=` | the buyer ends a subscription |
| `GET /v1/licences/<licence id>` | `{licence_id, app, status, cycle, valid_until, revoked_reason}`: `valid`, `expired` or `revoked` |
| `GET /v1/keys` | `{licence_public_key, environment}` |
| `POST /v1/skrill/status`, `/v1/skrill/recurring` | Skrill's signed reports (form fields) → `OK` |

### The catalog

`catalog.json` is the document `Apps/UltraStore/core/Catalog.cpp` reads
([the store's README](../../Docs/UltraStore/README.md), *Catalog*): `schema`
2, `sequence`, `generated`, `trusted_keys`, `market_url` (the root),
`payment_url` (the root) and `licence_key` once the store can sell,
`publishers`, `components` (runtimes and libraries) and `apps`, each with its
texts, category, pictures, pricing, visibility and its published `releases`
with their `packages` (`os`, `arch`, `variant`, `min_os`, `format`, `url`,
`size`, `sha256`, `entry`). Pictures and packages are absolute URLs under the
root.

- **Published by itself, and only on a change.** Every decision that can
  change what is public asks for the catalog; the content without the
  sequence and the date is hashed, and a publish that would say the same as
  the last one makes no new catalog. A new one takes the next `sequence`
  (a store refuses a catalog below the one it accepted).
- **Kept as signed.** Each catalog is a row with the exact bytes and the
  signature (`CatalogSnapshot`), served from there, and written to
  `<data>/public/v1/catalog.json` and `.sig` as well.
- **The key** is an Ed25519 seed: `ULTRASTORE_PORTAL_SIGNING_SEED`, or one
  made on the first publish and kept in the database. Its public half
  (`GET /v1/publish/key`, the operator's page) goes into
  `Apps/UltraStore/keys/` so the native store trusts it;
  `ULTRASTORE_PORTAL_TRUSTED_KEYS` announces a key being rotated in.
- **What is in it**: the apps of approved publishers that are not suspended
  and have a published release. A suspension, a withdrawal or a publisher
  suspended takes the app out with the next catalog, and its packages out of
  the public folder.

## From an account to the store

1. **Sign up** (`/account/signup/`): name, email, a password of 8 to 64
   characters (characters of Unicode text, not bytes; `PASSWORD_MIN_LENGTH`
   and `PASSWORD_MAX_LENGTH` in `settings.py`, checked wherever a password is
   chosen, never at a sign-in). A link mailed to the address proves it (two days);
   there is no sign-in before. An address that already has an account gets
   the same answer on the page and a mail to its owner.
2. **Apply for a publisher** (`/console/`): the name buyers see, the
   publisher id (`acme-software`, fixed from then on), a website, a support
   address, a few lines, the terms in force accepted. One publisher per
   account.
3. **The operator decides** (`/operator/`): approve, or reject with the
   reason; the publisher is mailed either way. Later: suspend and reinstate.
4. **Create an app**: its name, its reverse-DNS id (`org.acme.myapp`, fixed),
   its kind (an app, or a runtime / library other apps depend on), and for
   an app a category.
5. **The store listing**: summary, description, category, tags, the icon
   (square, 128 px or more), screenshots (up to eight), licence, links, age
   rating, price (free, one-time, monthly, yearly; a trial), visibility,
   lifecycle. The icon and the screenshots are **saved as soon as they are
   chosen or dropped** on their areas, each by a request of its own, so a
   picture never costs what was typed into the rest of the form. A field
   shows only while it applies (*The pages*). **No release before the
   listing has what a review needs** - a summary, and for an app a
   description, a category, an icon and a screenshot: until then the app's
   page offers *Edit the store listing* where *New release* will be.
6. **A release**: a version, the channel (stable, beta), what's new, the
   exact components it depends on (`org.ultraos.runtime@0.9.410`), and **a
   package per system and processor** - a zip, 7z, tar.* archive or an
   AppImage, with what the store starts inside it (`entry`), the minimum
   system version and the variants (`glibc-2.31`). The upload is hashed as
   it is written, kept under its SHA-256, checked to be the archive its name
   says, and the entry looked up in it. While the release is a draft or has
   changes requested, a package's system, processor, needs and entry are
   **edited in place** (*Edit* in the packages table): the archive stays as
   uploaded, the entry is looked up in it again. Only another file means
   removing the package and uploading again.
7. **Send for review.** Refused while the listing or the release lacks
   something (the page lists it), or new terms are not accepted. The release
   is closed to edits while the review runs; *Withdraw from review* reopens
   it.
8. **The review** (`/operator/releases/<n>/`): the checks the portal makes,
   *Download build* for each package, the listing as buyers will see it, and
   the decision - *Approve and publish*, *Request changes* or *Reject*, the
   last two with the message the publisher reads. An approval is refused
   while a dependency is not published.
9. **Published**: the app is on the store's pages and in the next catalog. A
   published release is never changed - a fix is a new version; *Unpublish*
   takes one out.

Every step is a row in the audit log (`/operator/audit/`), with who did it.

## The pages

Server-rendered Django templates on **Bootstrap 5.3** - its grid, forms,
buttons, cards, tables, alerts, navigation and dialog - with
`static/portal/portal.css` giving them the values of
[the design](../../Docs/UltraStore/design/README.md) (*The look*) and adding
the few things Bootstrap has no component for (an app's icon, the hero, the
screenshot strip, the upload areas). A form's fields get Bootstrap's classes
from one template filter (`{{ field|bs }}`, `web/ui.py`), so no form knows how
the pages look. Bootstrap and Dropzone are in `static/portal/vendor/` as
published (its `README.md` says which versions and how to update them).

`static/portal/portal.js` is the portal's own script, and everything it does
is an addition to a page that works without it:

- **Fields that depend on another field.** A form lists them
  (`DependentFieldsMixin`, `web/forms.py`), the page hides them, and the
  server does not trust the page: a hidden field's value is replaced by its
  value when hidden, and its errors keep nothing from saving.

  | Shown | Only while |
  |---|---|
  | Category (creating an app) | the kind is *Application* |
  | Price, currency, trial days, free updates | the pricing model is not *Free* |
  | Updates until | free updates are *Until a date* |
  | Source url | *Open source* is ticked |
  | Successor | the lifecycle is *Deprecated* |
  | Processor: *universal* | the system is macOS |
  | Processor: *wasm32* | the system is WebAssembly, which takes only *wasm32* and *any* |

- **Forms sent in place.** A picture uploaded or removed, a package added,
  edited or removed: the script sends the form with
  `X-Requested-With: XMLHttpRequest` and the view answers
  `{"ok": true, "fragments": {selector: html}}` - the parts of the page to
  draw again - or `{"ok": false, "error", "errors": {field: [...]}}`, shown at
  the fields while the chosen file stays chosen. A package's upload shows its
  progress. The same addresses answer a browser without the script with a
  redirect and a message.
- **The upload areas** (Dropzone): the icon and the screenshots, dropped or
  picked, each uploaded at once.

| Console address | |
|---|---|
| `POST /console/apps/<id>/icon/` | the icon alone |
| `POST /console/apps/<id>/screenshots/`, `.../screenshots/<n>/delete/` | a screenshot added, removed |
| `POST /console/apps/<id>/releases/<n>/packages/` | a package uploaded |
| `GET, POST .../packages/<n>/edit/` | a package's fields: the dialog's form for the script, a page of its own without it |
| `POST .../packages/<n>/delete/` | a package and its archive removed |

## Buying

Off until it is configured: without `ULTRASTORE_PORTAL_PAYMENT_PROVIDER` the
store shows the prices, sells nothing and names no payment service in the
catalog (the native store then sends *Buy* to the publisher's page).
`/operator/` and `/operator/orders/` say what is missing.

**A purchase, end to end.**

1. An order is opened - by the UltraStore app (`POST /v1/checkout`), or on
   the app's page (*Buy on the web*, which asks for an email for the
   receipt). It is priced from the listing; its id names it to the
   provider, its secret is the buyer's. **An order knows the account that
   bought when it is told** (`Order.buyer`, 0.11.0): whoever is signed in
   on the buy page; the person of the access token a client sends as
   `Authorization: Bearer` with the checkout (a token that is not good is
   no error - the order is nobody's, as before); and a guest's order
   becomes the account's when its owner opens it signed in with the
   address it was bought under. The order is still reached by its secret,
   and its JSON says nothing of who bought.
2. The buyer's browser opens `/pay/<order>/`, a form posted at once to
   Skrill (`pay_to_email`, the order as `transaction_id`, the amount, the
   return and cancel pages, `status_url`).
3. Skrill posts its report to `POST /v1/skrill/status`, signed with the
   secret word (`md5sig`). The signature, the amount and the currency are
   checked against the order; status 2 pays it: the licence is issued, the
   receipt numbered, the buyer mailed. A repeat changes nothing, a forgery
   is a 403, another amount a 400 noted on the order. **The browser coming
   back proves nothing and pays nothing.**
4. The UltraStore app polls the order with its secret, keeps the licence and
   fetches the package through a signed link. A web buyer lands on
   `/orders/<order>/?secret=` - the licence (also as `licence.ulic`), the
   receipt, a download per system - and gets that link by mail.

**The licence** is `ULIC1.<payload>.<signature>` (`billing/licence.py`): the
licence id, the app, the order, the day, what the purchase includes
(`updates`: the listing's update period when the order is paid) and for a
subscription the day it is valid until, signed Ed25519 with the store's
licence key (`ULTRASTORE_PORTAL_LICENCE_SEED`, or one made on the first sale
and kept in the database). The catalog carries the public half
(`licence_key`); the store and an app verify the token offline.

**The download link.** A paid app's packages without a trial are laid out
under `v1/restricted/`, which nginx serves only to a link the portal signed
(`secure_link_md5`, `ULTRASTORE_PORTAL_RESTRICTED_SECRET` - the same value
in `deploy/nginx.conf`), for ten minutes. A paid app *with* a trial keeps
its package public: the trial installs the same build, and the licence
gates what the trial lacks.

**A subscription** is an app priced monthly or yearly: Quick Checkout's
recurring billing. The first report pays the order and sets the paid
period; the licence is valid until that plus the grace days
(`ULTRASTORE_PORTAL_GRACE_DAYS`, 7), in which Skrill retries a failed
renewal. Each renewal Skrill reports (`POST /v1/skrill/recurring`) starts
the next period where the paid one ends and re-issues the licence under the
same id; a failed one marks the subscription `failing`, a cancelled one
`cancelled`. The buyer cancels from the store's app or the order's page
(Skrill is told through its API); the licence lasts until the paid period
ends.

**Refunds and chargebacks.** The operator refunds from the order's page
(`/operator/orders/<n>/`): the latest payment, whole or in part, through
Skrill's refund API (`ULTRASTORE_PORTAL_SKRILL_API_PASSWORD`). The licence
is taken back when everything paid is refunded, or when the operator says
so; a partial refund leaves it. A chargeback comes as Skrill's report
(status -3) and takes the licence back at once. A licence taken back is no
longer handed out, its downloads are refused, and `GET /v1/licences/<id>`
answers `revoked`.

**The store's fee and the publisher's share** (0.7.0). The store keeps a
fee of every payment - **15 %** of what the buyer paid
(`ULTRASTORE_PORTAL_STORE_FEE_PERCENT`), to the cent, half up - and the rest
is the publisher's share. The split is written on the payment when the money
moves (`Payment.fee_percent`, `store_fee`; `billing/split.py`) and never
reckoned again: a fee changed in the settings changes later sales only. A
refund or a chargeback gives the fee back in the proportion of the money
given back - half of a payment refunded returns half of its fee, and the
last cent of a payment returns whatever of its fee is left, so a payment
refunded in parts nets to nothing for the store and for the publisher. A
renewal is split like a sale. `/console/sales/` shows each payment with the
fee and the share and, per currency, *Your share*; the operator's order page
shows the same with the percentage each payment was sold under. Payments
recorded before 0.7.0 get the split when the database is migrated, at the
fee configured then. Since 0.9.0 the tax comes out first and the fee is
reckoned on the net (below). **Not in the split:** the payment provider's
fee.

**Tax and invoices** (0.9.0, `billing/tax.py`). The store is the seller, so
it owes the VAT and writes the invoice. **Off until the seller's details are
configured** - `ULTRASTORE_PORTAL_SELLER_NAME`, `SELLER_ADDRESS` (lines
separated by `|`), `SELLER_VAT_ID` and `SELLER_COUNTRY`: without all four a
payment carries no tax and no invoice, as before, and `/operator/tax/` says
what is missing.

- **The price includes the tax.** A buyer pays the listed price wherever
  they are; the tax is taken out of it. Of 19.90 at 19 %, 3.18 is tax
  (19.90 x 19 / 119) and 16.72 is net; the store's fee is 15 % of the net,
  2.51, and the publisher's share 14.21.
- **The rate is the buyer's country's on the day the money moves** - the
  rule for digital services sold to consumers in the EU. The country comes
  with the order: the buy page asks for it, a client sends `country` with
  `POST /v1/checkout` (the native store does: its Settings' country, else
  the signed-in account's, else the region of the system's language); with
  none, it is the seller's. A country outside the
  EU is charged no EU tax and the payment says so.
- **The rates are the operator's data** (`VatRate`: a country, a
  percentage, the day it counts from), kept on `/operator/tax/`, where a
  rate for the same country and day replaces the one there and a rate for a
  later day is a change to come. `ultrastore-portal money seed-rates` loads
  a starting table of the 27 standard rates, each marked *to be checked* -
  **check them against the European Commission's table before selling**;
  the sources consulted when it was written disagreed on two countries. An
  EU country with no rate is charged the seller's and the payment says so;
  the tax page counts such payments and lists the countries without a rate.
- **An invoice for every payment, a credit note for every refund and
  chargeback**, in one series without gaps: `<INVOICE_PREFIX><year>-000001`,
  counted per year under a lock. Each is a copy of what it says as it stood -
  the seller, the buyer's address and country, the net, the rate, the tax,
  the total - so what the seller is called next year changes no invoice. A
  credit note names the invoice it takes back and returns the tax in the
  proportion of the money. The buyer's *Receipt* link shows the invoice
  where there is one (`GET /v1/orders/<id>/receipt`, as a page or as text);
  the order's JSON names it (`payments[].invoice_no`).
- **A quarter's figures** by the buyer's country and the rate - net, tax and
  total, from the invoices and credit notes written in it - are on
  `/operator/tax/` and from `ultrastore-portal money quarter <year> <1-4>`:
  what the one-stop-shop return asks for.

**For the tax adviser before the first sale, and not built:** a business
buyer's VAT id and the reverse charge; the two pieces of evidence the
one-stop shop asks for a buyer's country (the portal keeps the one the buyer
gave); the taxes of countries outside the EU; reduced rates; and whether the
invoice's wording is enough where the store is established.

**Payouts** (0.9.0, `billing/payouts.py`). A payout gathers what the store
owes a publisher in one currency:

- a sale's or a renewal's share is due `ULTRASTORE_PORTAL_PAYOUT_HOLD_DAYS`
  (30) after it was paid - the time in which it is most likely given back;
  a refund's or a chargeback's comes off at once;
- a sum under `ULTRASTORE_PORTAL_PAYOUT_MINIMUM` (0.01), or under nothing,
  makes no payout and is carried: what is given back after a payout comes
  off the next one, and nobody is asked for money;
- a payment is in one payout and no more.

**The portal moves no money.** *Make the payouts* on `/operator/payouts/`
(or `ultrastore-portal money payouts [--until YYYY-MM-DD]`, by default for
the last day of last month) writes them; each says what to pay and where -
the publisher's payout details as they stood. The operator pays it at the
bank or the provider and marks it paid with the transfer's reference, which
is refused empty. The publisher's owner says where the share goes on the
publisher profile (the account holder and IBAN, or a Skrill address, with
the legal name and a VAT id) - the owner alone: another member neither sees
nor changes it. `/console/sales/` shows what is not paid out yet and every
payout; `/console/payouts/<n>/` is its statement, payment by payment.
**Not built:** paying through the provider's own interface, and the tax
document a payout is to the publisher (a self-billed credit note) - the
statement says it is not one.

**Who sees what.** The operator sees the orders with the buyer's address;
a publisher sees what its apps sold (`/console/sales/`: every payment and
reversal with its split, the net and its share per currency), never who
bought.

**The test provider.** `ULTRASTORE_PORTAL_PAYMENT_PROVIDER=test` (refused
without `ULTRASTORE_PORTAL_DEBUG=1`) puts a page with *Pay*, *Let the
payment fail* and *Cancel* where Skrill's is; the button sends the same
signed report through the same code, so a developer's machine sells end to
end with no money moving.

**Not exercised against Skrill itself.** The integration is written to
Skrill's Quick Checkout and Automated Payments Interface guides and tested
against reports signed the documented way and a faked API; its first
meeting with Skrill is a sandbox run with a merchant account in test mode.
Until that run, treat the field names and the refund calls as unproven.

## One sign-in for every service

A person registers once. Every shop and service then sends them to the
account to sign in, and learns from the account who they are - the way
"Sign in with Google" works, by the same protocol, **OpenID Connect**. The
service never sees the password; what it gets is a short signed statement
(the identity token) and, if it asked, an access token.

**The address.** `ULTRASTORE_PORTAL_ACCOUNT_URL` is the account's root and
the **issuer** every token names.

| | |
|---|---|
| unset | the account lives under the store's root as before (`<root>/account/…`), the provider's endpoints under `<root>/` - a developer's machine, and any installation that does not set it |
| `https://account.ultraos.eu` | a second host this same application answers. It has the account's pages at its root and the provider's endpoints, and nothing of the store; the store's host has no password page any more and signs people in through the account like any other service (`identity/sso.py`: its client is `ultrastore`, registered by itself) |

Set it **before the first service is registered and never change it**: the
issuer is inside every token and in every client's configuration. Each host
keeps its own session cookie; none is shared across `ultraos.eu`, so no
other host under that name is ever sent a session - and under https they
are `__Host-` cookies, which no other host under that name can set either.
Neither host is told to trust the other's forms.

**What a service does** (any OpenID Connect client library does it from the
first address alone):

| | |
|---|---|
| `GET <issuer>/.well-known/openid-configuration` | what the account can do, and where |
| `GET <issuer>/oauth/jwks` | the public keys the identity tokens verify with |
| `GET <issuer>/oauth/authorize?response_type=code&client_id=&redirect_uri=&scope=openid+profile+email&state=&nonce=&code_challenge=&code_challenge_method=S256` | the person is sent here; they sign in if they are not, agree if the service is not one of ours, and come back to `redirect_uri` with `code`, `state` and `iss`. Also read: `prompt` (`none`, `login`, `consent`), `max_age`, `login_hint` |
| `POST <issuer>/oauth/token` | `grant_type=authorization_code`, `code`, `redirect_uri`, `code_verifier` → `access_token`, `id_token`, `expires_in`, `scope`, and `refresh_token` when `offline_access` was granted; `grant_type=refresh_token` for the next set. A server names itself with its secret (HTTP Basic, or in the form); an application or a page has no secret and proves itself with the code verifier |
| `GET <issuer>/oauth/userinfo` | the person, for `Authorization: Bearer <access token>` |
| `POST <issuer>/oauth/introspect` | `token=` → `{active, sub, scope, client_id, exp, …}`: for a service that is handed other services' tokens - the chat's relay asked by the chat's application - and is registered as one |
| `POST <issuer>/oauth/revoke` | a token given back |
| `GET <issuer>/oauth/logout?id_token_hint=&post_logout_redirect_uri=&state=` | the service signs the person out at the account. With the identity token of that sign-in it happens at once; without it the person is asked first |

**Scopes and claims.** `openid` (required) gives `sub`; `profile` adds
`name`; `email` adds `email` and `email_verified`; `offline_access` adds a
refresh token. The identity token carries them too, with `iss`, `aud`,
`exp`, `iat`, `auth_time`, `nonce`, `sid` (the sign-in) and `at_hash`, signed
RS256.

**Who a person is to a service** (`sub`). Our own services (`trusted`) all
see the same subject for a person, so cloud storage and the chat know they
have the same customer. Every other service gets a subject made for it
alone, so two of them cannot compare notes. It is never the account's own
number, and it never changes.

**Registering a service.** In the admin site (*Clients*), or:

```
ultrastore-portal oidc client ultracloud --name UltraCloud --trusted \
    --redirect-uri https://cloud.ultraos.eu/callback --post-logout-redirect-uri https://cloud.ultraos.eu/
ultrastore-portal oidc client chatcube --name ChatCube --trusted --public \
    --redirect-uri http://127.0.0.1/callback --scopes "openid profile offline_access"   # asked for each time: it is on the person's machine
ultrastore-portal oidc client chat-relay --name "Chat relay" --resource-server --redirect-uri https://chat.ultraos.eu/unused
ultrastore-portal oidc clients | issuer | rotate-key | retire-key <kid> | cleanup
```

A server's secret is printed once, when it is made (`--new-secret` makes
another); only its hash is kept. `--public` is an application or a page
that cannot keep one. A redirect address is `https`, matched exactly - or
`http://127.0.0.1/…` for an application on the person's own machine, which
stands for whatever port it got (UltraNet's `UltraNetOAuth2.h` signs in
that way). `--trusted` is one of ours: no consent page - **except for an
application on the person's own machine, which is asked for every time**,
ours or not: anything running there can claim to be that application, and
only the person can say it is the one they started. Every registration is a
row in the audit log.

**What is refused, and what is taken back.**

- Only the code flow; PKCE with S256, always for a client without a secret.
  No implicit flow, no password grant, no request objects.
- An unknown client or return address is told to the person and sent
  nowhere; every other mistake is told to the service at its own address.
- A code is good once and for two minutes. Shown twice, it takes back what
  the first showing got. A refresh token is spent by its use; shown twice,
  its whole grant ends. A refresh may ask for less than was granted; the
  next one has the whole grant again.
- `prompt=login` means the password, typed after the account asked: the way
  back from the sign-in page carries the account's signed note of when it
  asked, and only a sign-in newer than the note goes on.
- A thing named twice in a request (two return addresses, say) is refused.
- Guessing at a client's secret is slowed down, by the client that is
  named. A refused code or token is not counted: nobody guesses 256 random
  bits, and counting them would let anyone shut a service's sign-in.
- Codes, tokens and secrets are stored as their SHA-256, never as handed out.
- A new password ends every grant and every other sign-in, wherever it is
  set - the account's pages, the admin site, the command line. An account
  switched off has no good tokens, and neither has a client switched off.
- On their account's page a person sees the services they signed in to, with
  what each was told - ours that hold a refresh token included - and takes
  the access back.
- The mailed confirmation link proves the address once and signs in only
  then, and never replaces whoever is signed in.
- Signing out at the account signs the store out; the store's *Sign out*
  signs both out.

**The keys.** The identity tokens are signed with an RSA key of 2048 bits:
a PEM file (`ULTRASTORE_PORTAL_OIDC_KEY_FILE`), or one made on first use and
kept in the database. `oidc rotate-key` makes a new stored key the signer
and keeps the old one published until `oidc retire-key`. The secret the
subjects are derived with (`ULTRASTORE_PORTAL_OIDC_SUBJECT_SECRET`, or made
once and kept in the database) must never change, and belongs in every
backup: without it every service sees every person as a stranger.

**Not yet.** Written to the OpenID Connect Core, Discovery and RP-Initiated
Logout texts and RFC 6749, 7636, 7009, 7662, 8252 and 9207, and tested
through its own endpoints. One independent review of the code was made
and what it found is fixed, each finding with a test; it is **not yet run
against the OpenID Foundation's conformance suite and not reviewed from
outside the project** - both before a service that is not ours is let in.
In two-host mode the admin site's own sign-in (`/admin/login/` on the
store's host) still takes a password directly. Also not built: a second
factor at the sign-in, passkeys, telling a service's server that a person
signed out (back-channel logout), a service registering itself.

## Leaving an account

Two ways to leave (0.8.0, `accounts/closure.py`), because they mean
different things, and one end:

| | What the person does | What happens | Coming back |
|---|---|---|---|
| **Closed** | *Close the account* on `/account/leave/`, with the password | Signed out everywhere, every service's access ended, nothing signs in to it. Kept whole for `ULTRASTORE_PORTAL_ACCOUNT_CLOSED_DAYS` (**365**) | Signing in with the right password in that time shows *This account is closed* with **Reactivate my account**; it is then as it was left, and the services need signing in again. A forgotten password can still be reset |
| **Deleting** | *Delete the account* on the same page, with the password | It stays usable for `ULTRASTORE_PORTAL_ACCOUNT_DELETION_GRACE_DAYS` (**90**); the account's page says the day it goes | **Keep my account** on the account's page takes the request back |
| **Erased** | nothing: the end of both, when the days are over | The row stays under its id with the address replaced (`erased-<id>@erased.invalid`, so the address is free again), the name and the password gone, every grant, token, sign-in and consent removed, the memberships removed, and the audit rows it acted in naming `account <id>` in place of the address | None. The same person signing up again is somebody new |

A mail goes out when an account is closed and when a deletion is asked for,
with the day and the way back - which is also how the owner learns of it if
it was not their doing. A wrong password on a closed account says what it
always says: only the owner, with the password, is told it is closed.

**Who cannot leave this way:** an operator (another operator removes the
account) and the owner of a publisher (the publisher is handed over first:
an app must not be left with nobody behind it). The page says which.

**What is not the account's:** orders and receipts. A purchase is reached by
its order's secret, not by the account, and stays as the law asks.

**The daily maintenance** erases what is due and is safe to run again:

```
ultrastore-portal accounts maintain    # e.g. from a systemd timer, once a day
ultrastore-portal accounts leaving     # who is closed or being deleted, by id, with the day
```

Without it nothing is ever erased: the accounts stay closed, or usable with
a date that has passed. The affiliate programme hangs a customer's binding
on these states (*Affiliates*): suspended while closed or deleting, restored
on the way back, gone with the erasure.

## Affiliates

UltraAffiliates, phases 1 and 2 of
[its design](../../Docs/Research/UltraAffiliatesDesignProposal.md)
(`affiliates/`): **product links** (0.10.0) and **the customer's binding**
(0.11.0). A person the store knows advertises what the store sells; when
their link to a product brings its sale, they earn - and a new customer they
brought stays theirs, earning them the base on whatever that customer buys
later.

**Switched off unless switched on.** `ULTRASTORE_PORTAL_AFFILIATES=1`.
Without it none of its pages exists, `/r/...` is a 404, and no order earns
anyone anything.

**The rules, which the code holds to and the tests check:**

- **Free for the publisher, and every paid app is in it.** There is
  nothing to join, switch on or opt out of. Any active affiliate may take a
  link to any app.
- **A base and a boost.** The store pays an affiliate
  `AFFILIATE_BASE_PERCENT` (**5**) of the net price out of its own fee -
  the publisher's share is untouched. A publisher's **owner** may add a
  **boost of 1 to 45 %** of the net on an app, a category or everything
  (`/console/affiliates/`); the most specific one in force counts, and it
  comes out of the publisher's proceeds of that sale: the payment's
  `boost_amount`, which *Your share* and the payout leave out. A boost
  changed later does not change a sale already made.
- **A link to a product wins that product** - `/r/<handle>/<app>/` - for
  `AFFILIATE_LINK_DAYS` (**90**): the last such link the order carries. The
  web buy page keeps the link's token in the session and sends it with the
  order; a client sends it as `ref` with `POST /v1/checkout`. `/r/<handle>/`
  leads to the shop: it wins no product, and it is what binds a new
  customer (below). `?s=<name>` tells an affiliate's campaigns apart.
- **A new customer is bound to the affiliate who brought them** (0.11.0).
  The last link a person followed, to a product or to the shop, is kept
  with the session and sent with an order (`via`). Somebody who has never
  paid for anything and followed one in the last `AFFILIATE_LINK_DAYS` is
  bound to its affiliate - at **sign-up**, or at their **first paid order**
  when they had the account already or the sign-up did not see the link.
  From then on **every purchase and every renewal of theirs earns that
  affiliate the base** - the 5 %, out of the store's fee - **and no
  boost**: a boost is for the link that sold the product. One customer has
  one affiliate; one sale has one commission.
- **A product link by another affiliate wins that product, and moves
  nothing.** The other affiliate earns base and boost on that sale; the
  customer stays where they are bound, and the next purchase without a
  link is their affiliate's again.
- **Somebody who is buying cannot be claimed.** A link moves a binding in
  one case only, the **win-back**: a customer who paid for nothing - no
  order, no renewal - in `AFFILIATE_WINBACK_MONTHS` (**6**) and buys within
  `AFFILIATE_WINBACK_DAYS` (**14**) of following an affiliate's link is
  that affiliate's from this order on, whether they were another's or
  nobody's. "Paid" counts the account's orders and those under its address
  from before orders knew accounts.
- **The binding follows the customer's account** (*Leaving an account*):
  suspended while the account is closed or its deletion is asked for -
  nothing is earned on it then, and nobody new is bound to an account on
  its way out - in force again, to the same affiliate, when the person
  comes back, and removed with the erasure. What it earned stays, with no
  customer behind it. A renewal follows the customer: it earns whoever
  they are bound to on its day.
- **It ends with the affiliate's ban or erasure**; a block or a pause
  leaves the customers theirs and earns nothing meanwhile. Nobody is their
  own customer, and a purchase from a publisher the affiliate belongs to
  earns nothing.
- **A guest's purchase binds nobody**: a binding is between two accounts.
  It counts once the order is the account's (*Buying*).
- **Who does not earn:** the buyer themselves (the order's address is the
  affiliate's), a member of the publisher selling the app, and an affiliate
  who is not active.
- **Only a person following a link counts.** A page fetched as a picture, a
  script or a frame (`Sec-Fetch-Dest`) records no click.
- **The superaffiliate.** Whoever's recruiting link
  (`/affiliate/join/<handle>/`) brought an affiliate in - or, before any
  such link, **whoever the new affiliate is bound to as a customer** -
  earns
  `AFFILIATE_SUPER_PERCENT` (**10**) of each of that affiliate's
  commissions, **on top** and at the store's cost - the affiliate's is not
  reduced, and the publisher pays none of it. One level up and no further.
  The operator may set another share, or none, for one affiliate.
- **Pending, then approved** after `AFFILIATE_APPROVAL_DAYS` (**30**). A
  refund or a chargeback takes the commission back in the proportion of the
  money, as a line below zero - the affiliate's, the superaffiliate's and
  the publisher's boost alike - so a sale refunded in parts nets to nothing
  for everybody. Given back after a payout, it leaves the affiliate owing,
  and the next sale makes it up.
- **Identified, not anonymous.** A profile is active once its phone number
  is proven - a six-digit code by text, good for ten minutes and five
  tries - and the operator has looked at an identity document and written
  down what it says: the name, the date of birth, the document and its
  country. **No picture of a document is kept.** One person has one
  profile, and an affiliate is eighteen or older.
- **Blocked or banned.** A blocked affiliate's links count nothing and what
  they earned is held, until a day or until lifted; a banned one's
  relations to other affiliates end, for good. Each has a **record** that
  is added to and never edited: the rule, what happened, the evidence, the
  money, and what the affiliate said to it. The affiliate sees the rule and
  what happened at once, can put a statement on the record, and is given
  the whole of it on request.
- **Nobody sees who bought.** An affiliate sees the product, the day and
  the commission, and of a bound customer a number ("your customer no. 12")
  and how many there are; a publisher sees which affiliate sold how often
  and what its boosts cost. The customer sees who referred them, on
  `/account/`.
- **The profile follows the account** (*Leaving an account*): closed with
  it, back with it, emptied when it is erased - the row stays, so what it
  earned and a ban's record still name something.

**The pages.** `/affiliate/` - apply, prove the phone number, then the
links, what was earned and paid, the affiliates brought in;
`/affiliate/products/` - every paid app with what a link to it pays.
`/console/affiliates/` - what a link to each of the publisher's apps earns
today, the boosts, what affiliates sold. `/operator/affiliates/` - the
applications, the review, block and ban with their record, an affiliate's
share and the payout.

**The portal moves no money.** *Mark as paid* on an affiliate's page writes
the payout down with the transfer's reference; the operator pays at the bank
or the provider.

**A text message** is one POST to `ULTRASTORE_PORTAL_TEXT_GATEWAY_URL`
(`to`, `text`; the bearer token `TEXT_GATEWAY_TOKEN`). Without it the store
cannot send one: the affiliate's page says so, and the operator confirms the
number at the review.

**The daily maintenance:** `ultrastore-portal affiliates maintain` approves
what is due, lifts blocks whose time is over, and counts clicks older than
the link's days by day before removing them. `affiliates owed` and
`affiliates cost` say what is approved and unpaid, and what the programme
cost the store's fee and the publishers' boosts.

**What the binding does not reach yet.** The native store neither signs in
at the portal nor sends a link's token, so a purchase made in the app is
nobody's and binds nobody (*What comes next*, 3). With the account on a
host of its own (`ACCOUNT_HOST`), a sign-up there does not see the link the
store's session holds; the binding is then made at the first order.

**Not built** (the design's phases 3 and 4): the 5 % holdback for six
months; statements and self-billed credit notes; fixed boosts for hardware;
affiliate codes; the native store's `ultrastore://r/` links; sales made
outside the portal.

## Advertising

UltraAds, phase 1 of
[its design](../../Docs/Research/UltraAdsDesignProposal.md) - **sponsored
listings in the app store** - and the first half of phase 2, **the
console** around them (*The console*, below). A publisher promotes one of
its apps; the app's own card then appears, marked *Sponsored*, where the
page calls for it; the publisher pays when someone clicks.

**Switched off unless switched on.** `ULTRASTORE_PORTAL_PROMOTE=1` turns
advertising on. Without it there is no *Promote* button, none of its pages
exists, the JSON service answers an empty list, and no page shows an
advertisement. `ULTRASTORE_PORTAL_PROMOTE_TOPUPS=1` is a second switch for
paying money into a balance - **leave it off until the store writes tax
invoices** (*What comes next*, 2); until then a balance comes from the
operator's vouchers.

**The rules, which the code holds to and the tests check:**

- **Chosen from the page, never from the person** (decided 2026-11-24).
  The search words, the category, the app being looked at, the system: that
  is all `promote/decide.py` is ever handed. It takes no request, no session
  and no account, so it could not use one. There is no profile and no
  history, and the *Why this ad?* page says so with what matched.
- **Always marked.** Every advertisement carries *Sponsored*, dark on
  light, above the card, with *Why this ad?* beside it: who paid, what it
  matched, and *Report this advertisement*.
- **Money moves no unpaid result.** The store's search and its order are
  made first (`store/queries.py`) and left as they are; advertisements are
  placed before them. Editor's picks, *New and updated* and the rest are
  never sold.
- **Relevant or not shown.** In a search an advertisement must answer the
  search by the store's own rule; in a category be of the category; beside
  an app be of its category. Below that it is not shown at any price. A
  short list does not repeat above itself an app it already shows.
- **An auction on top of a price list.** Rank is the offer times how often
  the listing is clicked in that place. The winner pays what it took to stay
  ahead of the next publisher's - never more than it offered, never less
  than the placement's floor, which the operator sets. With one advertiser
  that is the floor. A publisher's own campaigns never bid against each
  other.
- **Prepaid, charged once.** A click draws the balance down; at zero the
  campaigns wait and the publisher was mailed before. A click is charged
  once however often it is told. Not charged: the publisher's own click, a
  burst from one address, a click on a campaign that ended meanwhile, and
  what the operator credits back.
- **Counted, not followed.** Shown, clicked, spent, installed and ordered
  are numbers per campaign, place, country and day. A click row holds no
  account and no address and is removed after a month
  (`ultrastore-portal promote cleanup`, a daily timer). An order counts for
  a campaign only through the token the buyer's own store kept from the
  click and sent with the checkout (`POST /v1/checkout`, `promote`); a
  publisher reads "3 orders, 59.70 EUR", never who.

**The publisher's pages.** *Promote* on an app's console page
(`/console/apps/<id>/promote/`) is the shop's door: **where** - every shop
and marketplace with its places (search, lists, the front page, beside
similar apps), this shop's ticked, every other one listed to tick (decided
2026-11-24); a shop that is not built yet is listed as *not open yet* -
**how much** a day and, if wanted, the most for a click, and **how long**.
`/promote/` lists the campaigns with what each cost and brought;
`/promote/campaigns/<n>/` is one campaign's report by place, day and
country, with *Change*, *Pause* and *End*; `/promote/balance/` the balance,
what went in and what was spent by day. A publisher new to advertising has
a ceiling per day for its first weeks.

**The console** (0.6.0) is what a campaign's own page adds to the three
fields of *Promote*:

- **Search words.** Without any, a campaign is shown for every search its
  listing answers. With its own - each matching *every word in any order*,
  *the words together in this order*, or *exactly this search* - it is shown
  for the searches that match one of them, and no others. **The relevance
  floor still holds**: the listing itself must hold half of what was
  searched (a setting), so a photo editor that bids on "spreadsheet" is not
  shown for it at any price. A word marked *never for this* keeps the
  campaign out of every search it matches.
- **What people searched** when it was shown, with how often and the click
  rate. A search is named only once it was shown often enough to be many
  people's (the same threshold a country has in a report); a rare one could
  be one person's and is counted under *other searches*. The rows are kept
  a month, like the click rows.
- **An offer per place**: the most to pay for a click in the search, in a
  category list, on the front page, beside similar apps - each its own
  figure, or empty for the campaign's.
- **What the numbers suggest**: the budget ran out on several days; it is
  seen and seldom clicked; people click and do not install; the balance
  covers less than three days; its own words were never searched. A
  sentence each; nothing is changed for the publisher.
- **The report as a file** (CSV: by day, by place, what was searched). A
  search is whatever someone typed: one that a spreadsheet would run as a
  formula is written as plain text.
- **Who may do what.** Of a publisher's people an *Owner* does everything;
  **Marketing** runs the campaigns; **Billing** pays into the balance;
  **Analyst** reads, the file included, and changes nothing; a *Developer*
  reads advertising and keeps the apps. Marketing, Billing and Analyst do
  not reach the apps, their listings or releases. (There is still no page
  to invite a person: a membership with a role is made in the admin site.)
- **The advertising policy.** A document with versions, written by the
  operator in the admin site (*Policy versions*) and shown at
  `/promote/policy/`. Once one is in force a publisher accepts it where it
  starts or changes a campaign, and a new version makes every campaign wait
  until its publisher accepts that one. **None is seeded**: what the store
  refuses to advertise is the owner's to write, not the code's.
- **The library** (`/promote/library/`, `GET /v1/promote/library`, linked
  from every page's foot): every advertisement that runs or ran in the last
  twelve months, for anyone - what is promoted, who pays, from when to when,
  where, and for which searches. What a campaign excludes is its own
  business and is not listed.
- **Not this one again** on every advertisement: remembered in the browser
  (`localStorage`) and nowhere else. The store is not told, so it cannot
  know who hid what; the page leaves the card out.

**The operator's page** (`/operator/promote/`): the placements with how
many advertisements each holds, the least a click costs there and open or
switched off; every campaign with *Stop* (the reason is the publisher's to
read) and *Let it run*; a voucher into a publisher's balance; the recent
clicks with *Did not count - credit*; revenue by place and by day; the
advertising policy in force; and the settings
(one advertisement per so many results, the days an order counts back, the
burst rule, the ceiling for a new publisher, the threshold under which a
country is folded into "other" in a report). Every action is a row in the
audit log. **The floors in the table are starting values, not decided
prices.**

**For a store's client that draws its own pages** (the native store, the
web store - neither uses it yet):

| | |
|---|---|
| `POST /v1/promote/decide` | `{placement, count?, results?, context: {q, category, listing, os, country}, seen: [campaign ids]}` → `{ads: [{token, kind, id, name, campaign, label, payer: {id, name}, why: [...], why_url}]}`. Asked **without saying who asks**: a request with an `Authorization` header is refused, and nothing reads a session. `seen` is how a device keeps one advertisement from being shown forty times, without the server learning who saw what |
| `POST /v1/promote/events` | `{events: [{token, type: shown \| clicked \| installed}]}` → `{accepted}` |
| `GET /v1/promote/why/<token>` | who paid, and what it matched |
| `POST /v1/checkout` | takes `promote`, the token of the advertisement last clicked, beside `code` |

Placements: `store-search`, `store-category`, `store-home`, `store-detail`.
The pages the portal draws itself ask the same function in the same request.

**Not built yet.** The native store and the web store showing
advertisements (the catalog names no `promote_url` until one of them reads
it). Of the console: several listings in one campaign (ad groups), brand
and display banners with their review queue, fixed slots booked from a
calendar, invoices (they wait for the store's tax invoices), a monthly
summary by mail, how full each placement is. Every shop but the app store - their placements are listed
and cannot be opened; an order refunded later is not taken out of a report;
the page a browser is on is not kept from showing the same advertisement
often (only a client that sends `seen` is).

## What is where on disk

Under `ULTRASTORE_PORTAL_DATA_DIR` (`./var` on a developer's machine):

| | |
|---|---|
| `media/apps/<app id>/` | icons and screenshots; public |
| `packages/<sha256>.<format>` | every uploaded package; **never served** |
| `public/v1/` | the publish folder: `catalog.json`, `catalog.json.sig`, `packages/` (hard links into the package store), `restricted/` (a paid app's packages without a trial - served to a signed link only) |
| `static/` | `collectstatic`'s output |

## Running

On a developer's machine, with SQLite and the mail printed to the terminal:

```
python3 -m venv .venv && .venv/bin/pip install -e .
export ULTRASTORE_PORTAL_DEBUG=1
export ULTRASTORE_PORTAL_PAYMENT_PROVIDER=test   # buying, with a button in the provider's place
.venv/bin/python manage.py migrate
.venv/bin/python manage.py seed_demo        # an operator, a publisher, eight apps, a review queue
.venv/bin/python manage.py runserver        # http://127.0.0.1:8000
.venv/bin/python manage.py test             # the tests, on SQLite
ULTRASTORE_PORTAL_DB=mysql://user:password@127.0.0.1:3306/db .venv/bin/python manage.py test   # needs '.[mysql]'; the user makes and drops test_db
```

`seed_demo` prints its three accounts (operator, publisher, an applicant);
it refuses to run without `ULTRASTORE_PORTAL_DEBUG=1`.

In production (`deploy/`: the environment file, the systemd unit, the nginx
server):

```
python3 -m venv /opt/ultrastore-portal/.venv
/opt/ultrastore-portal/.venv/bin/pip install '.[server,mysql]'       # mysqlclient builds against libmysqlclient-dev
mysql -u root -e "CREATE DATABASE ultrastore_portal CHARACTER SET utf8mb4;
  CREATE USER 'ultrastore_portal'@'localhost' IDENTIFIED BY 'password';
  GRANT ALL ON ultrastore_portal.* TO 'ultrastore_portal'@'localhost';"
# /etc/ultrastore-portal.env from deploy/ultrastore-portal.env.example, then:
set -a; . /etc/ultrastore-portal.env; set +a
ultrastore-portal migrate
ultrastore-portal createsuperuser           # the first operator
systemctl enable --now ultrastore-portal
```

The first operator makes the others in the admin site (`/admin/`, *Users*:
*Operator* ticked), writes the first publisher terms there (*Terms
versions*), and commits the catalog's public key to `Apps/UltraStore/keys/`.
`ultrastore-portal publish_catalog` publishes from the command line.

### Settings

All `ULTRASTORE_PORTAL_…`, from the environment:

| | |
|---|---|
| `BASE_URL` | the one root; `https://store.ultraos.eu` unless set |
| `ACCOUNT_URL`, `ACCOUNT_NAME` | the account's own root and the issuer (`https://account.ultraos.eu`); unset, the account lives under the store's root. What the account calls itself there (`ULTRA OS account`) |
| `ACCOUNT_CLOSED_DAYS` (365), `ACCOUNT_DELETION_GRACE_DAYS` (90) | the days a closed account is kept to be reactivated, and the days a deletion that was asked for waits; then the account is erased (*Leaving an account*) |
| `OIDC_KEY_FILE`, `OIDC_SUBJECT_SECRET` | the identity tokens' key as a PEM file, and the secret the subjects are derived with; unset, each is made once and kept in the database |
| `PROMOTE`, `PROMOTE_TOPUPS`, `PROMOTE_CURRENCY` (EUR) | advertising on (`1`); paying into a balance on (`1` - not before tax invoices); the one currency of balances |
| `OIDC_CODE_TTL` (120), `OIDC_ACCESS_TOKEN_TTL` (3600), `OIDC_ID_TOKEN_TTL` (3600), `OIDC_REFRESH_TOKEN_TTL` (90 days) | how long each lasts, in seconds |
| `SECRET_KEY` | Django's secret; required outside `DEBUG` |
| `DB` | `mysql://…` (production), `postgresql://…`, `sqlite:///path` |
| `DATA_DIR` | the folder of *What is where on disk* |
| `MAX_PACKAGE_MB` (512), `MAX_IMAGE_MB` (8), `MAX_SCREENSHOTS` (8) | |
| `SMTP_URL`, `MAIL_FROM` | the mail; without `SMTP_URL` it goes to the log |
| `SIGNING_SEED`, `TRUSTED_KEYS`, `SEQUENCE_FLOOR` | the catalog's key, keys being rotated in, the sequence to count on from |
| `PAYMENT_PROVIDER` | `skrill`, `test` (with `DEBUG` only), or empty: nothing is sold |
| `SKRILL_PAY_TO_EMAIL`, `SKRILL_SECRET_WORD`, `SKRILL_MERCHANT_ID`, `SKRILL_API_PASSWORD` | the Skrill merchant account; the API password for refunds and ending a recurring payment |
| `SKRILL_PAY_URL`, `SKRILL_API_URL` | `https://pay.skrill.com`, `https://www.skrill.com/app`; another host when Skrill gives a test endpoint |
| `LICENCE_SEED`, `GRACE_DAYS` (7) | the licence key, a subscription's grace |
| `SELLER_NAME`, `SELLER_ADDRESS`, `SELLER_VAT_ID`, `SELLER_COUNTRY` | the store as the seller on every invoice: its name, its address (lines separated by `\|`), its VAT id, its country (two letters). Until all four are set no tax is reckoned and no invoice written |
| `INVOICE_PREFIX` | what stands before an invoice's year and number (`UO-` gives `UO-2026-000001`) |
| `AFFILIATES`, `AFFILIATE_BASE_PERCENT` (5), `AFFILIATE_SUPER_PERCENT` (10), `AFFILIATE_LINK_DAYS` (90), `AFFILIATE_APPROVAL_DAYS` (30), `AFFILIATE_WINBACK_MONTHS` (6), `AFFILIATE_WINBACK_DAYS` (14) | the affiliate programme on (`1`); the base commission in percent of the net price, out of the store's fee (so not above it); the superaffiliate's share of a commission; the days a link counts, for its product and for binding a new customer; the days a commission waits; the months without a payment after which a customer can be won back, and the days after a link in which they must buy |
| `TEXT_GATEWAY_URL`, `TEXT_GATEWAY_TOKEN` | where a text message is sent (an https address taking `to` and `text`) and its bearer token; unset, the operator confirms an affiliate's phone number |
| `PAYOUT_HOLD_DAYS` (30), `PAYOUT_MINIMUM` (0.01) | the days a sale waits before its share can be paid out, and the least a payout is made for |
| `STORE_FEE_PERCENT` (15) | the store's fee: the percentage of what a buyer pays that the store keeps, from 0 to under 100 with at most two decimals; the rest is the publisher's share. Written on each payment, so a change applies to later sales |
| `RESTRICTED_SECRET`, `RESTRICTED_TTL` (600) | a buyer's download link: nginx's secret and its seconds |
| `STORE_DOWNLOAD_PATH` | a path under the root where the UltraStore app is downloaded |
| `ALLOWED_HOSTS`, `LOG_LEVEL`, `DEBUG` | |

## The code

```
ultrastore_portal/
  settings.py, urls.py, wsgi.py
  urls_account.py  the addresses of the account's own host
  accounts/        the person: email as the sign-in name, the mailed proof; closure.py: closed, deleting, erased
  identity/        signing in for other services: OpenID Connect
    models.py      Client, SignIn, Consent, Grant, Code, Token, SigningKey
    provider.py    what a request may ask, what a code and a token are worth
    keys.py        the RSA keys, the tokens' signatures, the subject secret
    views.py       the endpoints
    hosts.py       two hosts, one service: which addresses exist on which
    sessions.py    a sign-in as a record; the store's session ends with it
    sso.py         the store signing in through the account
    management/commands/oidc.py   clients, keys, cleanup
  affiliates/      UltraAffiliates, phases 1 and 2: models.py (AffiliateProfile, Boost, Click, OrderVisit, Binding,
                   Attribution, Commission, AffiliatePayout, Sanction), services.py (the rules), texts.py (a text message)
  promote/         advertising: UltraAds, phase 1
    models.py      Placement, Campaign, Balance, LedgerEntry, TopUp, DayCount, Click, Attribution,
                   Keyword, PlacementBid, TermCount, PolicyVersion, PolicyAcceptance
    decide.py      which advertisement goes where: from the page, never from the person
    services.py    campaigns, the balance, counting and charging a click, an order counted back, reports
    management/commands/promote.py   cleanup, voucher, placements
  store/           the records and the rules
    models.py      Publisher, PublisherMember, TermsVersion, App, Screenshot, Release, Package,
                   Report, AuditEntry, CatalogSnapshot
    services.py    every change of state, each one audited: apply, decide, submit, review, suspend
    catalog.py     the catalog built, signed, counted and laid out
    packages.py    the package store: hashed on upload, checked, private
    queries.py     the one search the pages and the JSON service share
    constants.py   the fixed lists the catalog is written from (categories, systems, formats)
    admin.py       the admin site
  billing/         buying
    models.py      Order, Payment, VatRate, Invoice, Payout
    services.py    an order opened, the provider's report believed or not, the licence, renewals, refunds, the signed link
    split.py       the store's fee and the publisher's share of a payment, and of what a reversal gives back
    tax.py         VAT by the buyer's country, the rates, invoices and credit notes, a quarter's figures
    payouts.py     what a publisher is owed, the payouts, marking one paid
    licence.py     the ULIC1 token
    skrill.py      Quick Checkout's form and signature, the refund and recurring API
  web/             the pages: storefront.py, account.py, console.py, operator.py, pay.py, promote.py, forms.py,
                   ui.py (a form's field with Bootstrap's classes)
  api/             the JSON service: views.py (the catalog, the apps), billing.py (the checkout), promote.py (advertising)
  templates/       the pages; console/_*.html are the parts a page draws again in place
  static/portal/   portal.css (the look of Docs/UltraStore/design/README.md, on Bootstrap),
                   portal.js (dependent fields, forms sent in place, uploads), vendor/ (Bootstrap, Dropzone)
tests/             the portal end to end, through its pages and its JSON service
deploy/            the environment file, the systemd unit, the nginx server
```

## What comes next

In the order the store needs it:

1. **The Skrill sandbox run** - a merchant account in test mode, one
   purchase, one renewal, one refund (*Buying*, the last paragraph).
2. **Tax and payouts, what is left of them.** VAT by the buyer's country,
   invoices and credit notes, a quarter's figures and the publishers'
   payouts are built (*Buying*, 0.9.0). Left: a business buyer's VAT id and
   the reverse charge, the evidence of a buyer's country beyond what they
   said, the taxes of countries outside the EU, the payment provider's fee
   in the split, paying a payout through the provider's interface, and the
   self-billed document a payout is to a publisher.
3. **Buyer accounts** - sign-in from the native store, licences that follow
   a person across machines, devices. An order knows its buyer's account
   since 0.11.0 (*Buying*), and the sign-in is there (*One sign-in for
   every service*); what is missing is the native store and
   `UltraAccountCore` using it - sending the access token and an
   affiliate's link with the checkout - a page of a person's purchases,
   and the licences hung on the person. Until then a purchase made in the
   app is reached by its order's secret alone.
4. **Review of listing changes.** Today an edit to a public app's listing is
   live at once; only releases are reviewed.
5. **Advertising, its next steps**: the native store and the web store
   showing advertisements (and `promote_url` in the catalog); a daily timer
   for `promote cleanup`; the rest of the console - ad groups, banners with
   their review, fixed slots. Paying into a balance and invoices wait for
   step 2.
6. **The provider, proven**: the OpenID Foundation's conformance suite and
   an outside review before a service that is not ours is registered;
   back-channel logout; a daily timer for `oidc cleanup`.
7. **Two-factor sign-in** for operators and publishers, and operator
   **roles** (reviewer, support, auditor) - Django's groups and permissions
   are there, the pages check only *staff* today.
8. **More people per publisher** - the membership table has the roles
   (Owner, Developer, and advertising's Marketing, Billing, Analyst, which
   the pages hold to), the console has no page to invite anyone.
9. **Automatic checks on a build**: a malware scan and a smoke run; today
   the reviewer downloads the build and checks by hand.
10. **Translations** of listings (the catalog carries a text per language;
   the portal stores the one the publisher wrote) and of the portal's own
   pages; picture variants (`icon_variants`, `screenshot_variants`).
11. Discount and affiliate codes (a `code` sent with a checkout is kept on
   the order and takes nothing off), strikes and notices to publishers, a
   hash chain over the audit log, statistics for publishers.
