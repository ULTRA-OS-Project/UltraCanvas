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
  and the checkout.

It is built to replace the store's small services (`Server/UltraStoreMarket`,
`UltraStoreAccount`, `UltraStorePay`) and the two desktop tools on them
(`ultrastoreadmin`, `ultrastoreop`). 0.1.0 was the essential path - register
a publisher, publish an app, show and serve the catalog; 0.2.0 sells;
*What comes next* names the rest.

## Decisions

| Question | Decision |
|---|---|
| How many stores, how many catalogs? | **One.** One store, one public catalog, generated from what the review published. There is no private or per-publisher catalog, and a publisher holds no signing key: a publisher is an account, the operator's key signs the catalog (decided 2026-11-20). |
| Which addresses? | **Everything under one root**, `https://store.ultraos.eu` (`ULTRASTORE_PORTAL_BASE_URL`): the pages, the console, the JSON service under `/v1`, the pictures, the packages. No URL the portal writes names a second host (decided 2026-11-20). |
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
| `/account/…` | sign up, the mailed confirmation link, sign in, password reset, the profile |
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
| `POST /v1/checkout` | `{app, code?, language?}` → the order: `order_id`, `secret`, `amount`, `currency`, `list_price`, `status`, `cycle`, `checkout_url` (`<root>/pay/<order>/`, opened in the browser). 404 `no_such_app`, 400 `free_app`, 503 `not_configured` |
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
   provider, its secret is the buyer's.
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

**Who sees what.** The operator sees the orders with the buyer's address;
a publisher sees what its apps sold (`/console/sales/`: every payment and
reversal, the net per currency), never who bought.

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
| `RESTRICTED_SECRET`, `RESTRICTED_TTL` (600) | a buyer's download link: nginx's secret and its seconds |
| `STORE_DOWNLOAD_PATH` | a path under the root where the UltraStore app is downloaded |
| `ALLOWED_HOSTS`, `LOG_LEVEL`, `DEBUG` | |

## The code

```
ultrastore_portal/
  settings.py, urls.py, wsgi.py
  accounts/        the person: email as the sign-in name, the mailed proof
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
    models.py      Order, Payment
    services.py    an order opened, the provider's report believed or not, the licence, renewals, refunds, the signed link
    licence.py     the ULIC1 token
    skrill.py      Quick Checkout's form and signature, the refund and recurring API
  web/             the pages: storefront.py, account.py, console.py, operator.py, pay.py, forms.py,
                   ui.py (a form's field with Bootstrap's classes)
  api/             the JSON service: views.py (the catalog, the apps), billing.py (the checkout)
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
2. **Tax and payouts.** A receipt is not a tax invoice: VAT by the buyer's
   country, invoices, and the split of a sale between the publisher, the
   store and the provider's fee are not built. `/console/sales/` shows what
   buyers paid, nothing about what a publisher is owed.
3. **Buyer accounts** - sign-in from the native store, licences that follow
   a person across machines, devices. Today a purchase is reached by its
   order's secret (kept by the store's app, mailed to a web buyer).
4. **Review of listing changes.** Today an edit to a public app's listing is
   live at once; only releases are reviewed.
5. **Two-factor sign-in** for operators and publishers, and operator
   **roles** (reviewer, support, auditor) - Django's groups and permissions
   are there, the pages check only *staff* today.
6. **More people per publisher** - the membership table has the roles, the
   console has no page to invite anyone.
7. **Automatic checks on a build**: a malware scan and a smoke run; today
   the reviewer downloads the build and checks by hand.
8. **Translations** of listings (the catalog carries a text per language;
   the portal stores the one the publisher wrote) and of the portal's own
   pages; picture variants (`icon_variants`, `screenshot_variants`).
9. Discount and affiliate codes (a `code` sent with a checkout is kept on
   the order and takes nothing off), strikes and notices to publishers, a
   hash chain over the audit log, statistics for publishers.
