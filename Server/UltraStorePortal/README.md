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
- the **JSON service** a buyer's client reads - the whole signed
  `catalog.json` the native store already parses, and a browsable list of
  apps.

It is built to replace the store's small services (`Server/UltraStoreMarket`,
`UltraStoreAccount`, `UltraStorePay`) and the two desktop tools on them
(`ultrastoreadmin`, `ultrastoreop`). 0.1.0 is the essential path - register a
publisher, publish an app, show and serve the catalog; *What comes next*
names the rest.

## Decisions

| Question | Decision |
|---|---|
| How many stores, how many catalogs? | **One.** One store, one public catalog, generated from what the review published. There is no private or per-publisher catalog, and a publisher holds no signing key: a publisher is an account, the operator's key signs the catalog (decided 2026-11-20). |
| Which addresses? | **Everything under one root**, `https://store.ultraos.eu` (`ULTRASTORE_PORTAL_BASE_URL`): the pages, the console, the JSON service under `/v1`, the pictures, the packages. No URL the portal writes names a second host (decided 2026-11-20). |
| Compatibility with the earlier services? | **None kept** - they were not in use (decided 2026-11-20). The portal names no payment, account or marketplace service in the catalog. What is kept is the **catalog's format** (`catalog.json`, schema 2, the Ed25519 signature beside it), because it is the buyer client's, not a service's: the native UltraStore reads the portal's catalog with the parser and the verifier it has. |
| Which stack? | **Django 5.2 LTS**, with its own accounts, forms, admin site and migrations, and three dependencies in all: Django, `cryptography` (the catalog's Ed25519 signature) and Pillow (checking uploaded pictures). No front-end build: server-rendered pages and one stylesheet. The JSON service is plain Django views - ten read-only endpoints do not need a REST framework. |
| Which database? | **PostgreSQL in production**, SQLite for tests and development; the tests run on both. `mysql://` is accepted by the settings but not tested. |
| Where do packages live? | **Private until published.** An upload goes into the package store under its SHA-256, which no web server serves; the publish lays the packages of *published* releases out beside the catalog. A build in review is reachable by the reviewer alone. |
| What is reviewed? | **Every publisher once, every release each time.** A listing edit of an app that is already public is live at once - see *What comes next*. |

## The addresses

| | |
|---|---|
| `/` | the store: Discover |
| `/apps/`, `/category/<id>/` | browse, search (`?q=`, `?price=free\|paid`, `?sort=`) |
| `/apps/<app id>/` | an app's page; `.../download/<n>/` a package by the browser, `.../report/` a problem reported |
| `/publisher/<id>/` | a publisher's page |
| `/account/…` | sign up, the mailed confirmation link, sign in, password reset, the profile |
| `/console/…` | the developer console |
| `/operator/…` | the operator's pages (staff only) |
| `/admin/` | Django's admin site: every record as it is (staff only) |
| `/v1/…` | the JSON service (below) |
| `/v1/packages/<sha256>.<format>` | the packages of published releases, served by nginx |
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

### The catalog

`catalog.json` is the document `Apps/UltraStore/core/Catalog.cpp` reads
([the store's README](../../Docs/UltraStore/README.md), *Catalog*): `schema`
2, `sequence`, `generated`, `trusted_keys`, `market_url` (the root),
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

1. **Sign up** (`/account/signup/`): name, email, a password of twelve
   characters or more. A link mailed to the address proves it (two days);
   there is no sign-in before. An address that already has an account gets
   the same answer on the page and a mail to its owner.
2. **Apply for a publisher** (`/console/`): the name buyers see, the
   publisher id (`acme-software`, fixed from then on), a website, a support
   address, a few lines, the terms in force accepted. One publisher per
   account.
3. **The operator decides** (`/operator/`): approve, or reject with the
   reason; the publisher is mailed either way. Later: suspend and reinstate.
4. **Create an app**: its name, its reverse-DNS id (`org.acme.myapp`, fixed),
   its kind (an app, or a runtime / library other apps depend on), a
   category.
5. **The store listing**: summary, description, category, tags, the icon
   (square, 128 px or more), screenshots (up to eight), licence, links, age
   rating, price (free, one-time, monthly, yearly; a trial), visibility,
   lifecycle.
6. **A release**: a version, the channel (stable, beta), what's new, the
   exact components it depends on (`org.ultraos.runtime@0.9.410`), and **a
   package per system and processor** - a zip, 7z, tar.* archive or an
   AppImage, with what the store starts inside it (`entry`), the minimum
   system version and the variants (`glibc-2.31`). The upload is hashed as
   it is written, kept under its SHA-256, checked to be the archive its name
   says, and the entry looked up in it.
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

## What is where on disk

Under `ULTRASTORE_PORTAL_DATA_DIR` (`./var` on a developer's machine):

| | |
|---|---|
| `media/apps/<app id>/` | icons and screenshots; public |
| `packages/<sha256>.<format>` | every uploaded package; **never served** |
| `public/v1/` | the publish folder: `catalog.json`, `catalog.json.sig`, `packages/` (hard links into the package store), `restricted/` (a paid app's packages without a trial - served to nobody until the portal sells) |
| `static/` | `collectstatic`'s output |

## Running

On a developer's machine, with SQLite and the mail printed to the terminal:

```
python3 -m venv .venv && .venv/bin/pip install -e .
export ULTRASTORE_PORTAL_DEBUG=1
.venv/bin/python manage.py migrate
.venv/bin/python manage.py seed_demo        # an operator, a publisher, eight apps, a review queue
.venv/bin/python manage.py runserver        # http://127.0.0.1:8000
.venv/bin/python manage.py test             # the tests, on SQLite
ULTRASTORE_PORTAL_DB=postgresql://user:password@127.0.0.1:5432/db .venv/bin/python manage.py test
```

`seed_demo` prints its three accounts (operator, publisher, an applicant);
it refuses to run without `ULTRASTORE_PORTAL_DEBUG=1`.

In production (`deploy/`: the environment file, the systemd unit, the nginx
server):

```
python3 -m venv /opt/ultrastore-portal/.venv
/opt/ultrastore-portal/.venv/bin/pip install '.[server,postgres]'
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
| `DB` | `postgresql://…`, `mysql://…`, `sqlite:///path` |
| `DATA_DIR` | the folder of *What is where on disk* |
| `MAX_PACKAGE_MB` (512), `MAX_IMAGE_MB` (8), `MAX_SCREENSHOTS` (8) | |
| `SMTP_URL`, `MAIL_FROM` | the mail; without `SMTP_URL` it goes to the log |
| `SIGNING_SEED`, `TRUSTED_KEYS`, `SEQUENCE_FLOOR` | the catalog's key, keys being rotated in, the sequence to count on from |
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
  web/             the pages: storefront.py, account.py, console.py, operator.py, forms.py
  api/views.py     the JSON service
  templates/, static/portal/portal.css   the look of Docs/UltraStore/design/README.md
tests/             the portal end to end, through its pages and its JSON service
deploy/            the environment file, the systemd unit, the nginx server
```

## What comes next

In the order the store needs it:

1. **Buying.** Prices are carried and shown, and a paid app's packages
   without a trial are kept out of the public folder, but nothing sells yet:
   the checkout, the licence and the signed download link for a buyer.
2. **Buyer accounts** - sign-in from the native store, licences that follow
   a person, devices. The portal's accounts are the base.
3. **Review of listing changes.** Today an edit to a public app's listing is
   live at once; only releases are reviewed.
4. **Two-factor sign-in** for operators and publishers, and operator
   **roles** (reviewer, support, auditor) - Django's groups and permissions
   are there, the pages check only *staff* today.
5. **More people per publisher** - the membership table has the roles, the
   console has no page to invite anyone.
6. **Automatic checks on a build**: a malware scan and a smoke run; today
   the reviewer downloads the build and checks by hand.
7. **Translations** of listings (the catalog carries a text per language;
   the portal stores the one the publisher wrote) and of the portal's own
   pages; picture variants (`icon_variants`, `screenshot_variants`).
8. Strikes and notices to publishers, a hash chain over the audit log,
   discount and affiliate codes, statistics for publishers.
