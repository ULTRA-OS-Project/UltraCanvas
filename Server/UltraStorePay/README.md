# The UltraStore payment service

The server side of a purchase in [UltraStore](../../Docs/UltraStore/README.md)
(*Payment*): a small [FastAPI](https://fastapi.tiangolo.com/) service on
PostgreSQL that holds the merchant credentials - never the store binary, never
the catalog - and does what the store cannot do for itself:

- opens a **Skrill Quick Checkout** session for an app, priced from the
  same signed catalog the stores read, with the discount of the code the
  buyer arrived with;
- receives Skrill's **status report** (server to server, signed with the
  secret word), which is the one thing it trusts - the browser's return
  proves nothing;
- issues the **licence** the app checks (an Ed25519-signed token) and the
  numbered **receipt**;
- runs a **subscription** (a monthly or yearly app) through Quick
  Checkout's recurring billing: each renewal Skrill reports extends the
  licence, a cancelled one lasts until the paid period ends;
- makes **refunds** through Skrill's API when the operator asks, and takes
  the licence back on a full refund or a **chargeback**;
- signs the short-lived **download URLs** for the packages of paid apps
  (`restricted/`, nginx `secure_link_md5`);
- hands the **sales** to the admin backend, which records them in its ledger
  with the partners' credits (two tiers, one level up and no further).

It never sees a card number: the buyer pays on Skrill's hosted page.

## The purchase, end to end

1. The store asks `POST /v1/checkout {app, code}` and gets an order: the
   amount after the code, the order's secret, and `checkout_url`.
2. The store opens `checkout_url` in the browser: `GET /pay/<order>` is a
   page with the Quick Checkout form, submitted at once to
   `https://pay.skrill.com` with `pay_to_email`, `transaction_id` (the
   order), `amount`, `currency`, `return_url`, `cancel_url` and
   `status_url`.
3. The buyer pays on Skrill's page. Skrill posts the outcome to
   `POST /v1/skrill/status` with `md5sig` =
   `MD5(merchant_id + transaction_id + MD5(secret word).upper() + mb_amount + mb_currency + status).upper()`.
   The service checks the signature, the amount and the currency against
   the order; on status 2 the order is paid, the licence issued, the
   receipt numbered. A repeated report is answered `OK` again and changes
   nothing; a forged one is a 403; another amount a 400, noted on the order.
4. The store polls `GET /v1/orders/<order>?secret=…` until `status` is
   `paid`, keeps the licence, and fetches the paid app's package through
   `POST /v1/orders/<order>/download?secret=…&file=restricted/<sha256>.<format>`,
   which answers a URL that lives ten minutes.
5. The admin backend fetches `GET /v1/sales?since=<seq>` with its bearer
   token and records each sale with the partners' credits.

## API

Requests and answers are JSON unless said; errors are
`{"error": "...", "error_description": "..."}` with a 4xx or 503 status.

| | |
|---|---|
| `POST /v1/checkout` | `{app, code?, language?}` → the order (`order_id`, `secret`, `amount`, `currency`, `list_price`, `code`, `code_applied`, `checkout_url`, `environment`) |
| `GET /pay/{order}` | the page that sends the buyer to Skrill (HTML) |
| `GET /pay/static/style.css`, `/pay/static/logo-light.png`, `/pay/static/logo-dark.png` | the look of the pages: the stylesheet and the ULTRA OS mark, from the package (`ultrastore_pay/static/`), cached for a day |
| `POST /v1/skrill/status` | Skrill's status report (form fields) → `OK` |
| `GET /pay/{order}/return`, `/cancel` | where Skrill sends the buyer back (HTML) |
| `POST /v1/skrill/recurring` | Skrill's report of a subscription's later payment (`rec_status_url`) → `OK` |
| `GET /v1/orders/{order}?secret=` | the order's state; when paid, `licence`, `licence_id`, `receipt_no`, `paid_at`, the `payments`; a subscription's `cycle`, `paid_until`, `valid_until`, `subscription_status`; `revoked` with `revoked_reason` after a refund or a chargeback (no `licence` then), `refunded` |
| `GET /v1/orders/{order}/receipt?secret=[&no=]` | the receipt of the sale, or of the payment with that receipt number: a page for a browser (`Accept: text/html` first, as the store's *Receipt* button opens it), text for anything else |
| `POST /v1/orders/{order}/download?secret=&file=` | `{url, expires}` for one of the app's restricted packages; 409 once the licence is taken back or the subscription ended |
| `POST /v1/orders/{order}/subscription/cancel?secret=` | the buyer ends the subscription (Skrill is told); the order, `subscription_status` cancelled |
| `POST /v1/orders/{order}/refund` | `Authorization: Bearer <admin token>`, `{amount?, transaction?, note?, revoke?}` → the order after the refund through Skrill |
| `GET /v1/licences/{licence}` | `{licence_id, app, status, cycle, valid_until, revoked_reason}` - `status` is `valid`, `expired` or `revoked`; no secret, the id is random |
| `GET /v1/sales?since=N` | `Authorization: Bearer <admin token>` → `{sales: [{seq, order_id, reference, kind, at, code, app, amount, currency, receipt_no, skrill_transaction, cycle}], next}` - the sales and the renewals |
| `GET /v1/reversals?since=N` | the same for the refunds and the chargebacks, each with `reverses` (the reference of the sale or renewal it takes back) and `revoked` (whether it took the licence back) |
| `GET /v1/keys` | `{licence_public_key, environment}` |
| `GET /healthz` | `ok`, or what is not configured |

## The pages

Everything a buyer reads in a browser - the checkout hand-off, the
thank-you and cancel pages, the receipt - is one frame (`pages.py`): the
ULTRA OS mark and the name at the top, the content on a card, the store's
colours (the window's `StoreTheme.h`), a dark look where the browser asks
for one, and no script or font from anywhere else. The stylesheet and the
mark are served by the service itself under `/pay/static/`. The admin
backend's partner landing pages (`go/<CODE>/`) carry the same stylesheet,
copied in `Affiliates.cpp`: change both together.

## The licence

`ULIC1.<base64url payload>.<base64url signature>`: the payload names the
licence, the app, the order, the day it was issued and what the purchase
includes (`updates`: the app's update period as the catalog has it when
the order is paid - `always`, `never` for the bought version only, or
`until` with `updates_until`; the store holds to it on updates), and for
a subscription the day it is valid until (`valid_until`); the
signature is Ed25519 over the payload bytes with
the service's licence key (`ULTRASTORE_PAY_LICENCE_SEED`, or one made on
the first start and kept in the database). The public half is
`GET /v1/keys`; an app verifies a licence offline with it, and an app that
is online may ask `GET /v1/licences/{id}` whether it still stands.

## Subscriptions, refunds and chargebacks

**A subscription** is an app priced `monthly` or `yearly` in the catalog.
The checkout carries Quick Checkout's recurring billing: the first payment
is the price after the code (a code takes its discount off the first
payment only), then `rec_amount` (the list price) every `rec_cycle`
(`month` or `year`), with `rec_status_url` pointing at
`POST /v1/skrill/recurring`. The first status report pays the order like
any other and sets the paid period: `paid_until` is one cycle from the day
it is paid, `valid_until` is that plus the grace days
(`ULTRASTORE_PAY_GRACE_DAYS`, 7), the days Skrill retries a failed renewal
- the licence carries `valid_until`, so the buyer is not cut off while a
retry is on its way. Each renewal Skrill reports (status 2, signed the
same way, for the list price) starts the next period where the paid one
ends (or today, when that is past), re-issues the licence with the same
id and the later `valid_until`, numbers a receipt and goes into the sales
feed as a `renewal` under its own reference (`<order>/<payment>`), so the
admin backend credits the partner for it. A repeated report changes
nothing. A failed renewal (status -2) sets `subscription_status` to
`failing` and leaves the licence to run out; a cancelled recurring payment
(-1) sets it to `cancelled`. The buyer cancels from the store
(`POST .../subscription/cancel`): the service asks Skrill to end the
recurring payment (`query.pl`, `cancel_rec_payment`, with the API
password), and the licence lasts until `valid_until`. The store fetches
the re-issued token, and learns of a licence taken back, when it refreshes
the catalog (`SyncLicences`), and an app checks `valid_until` against
today after verifying the file (`Licence::ValidOn`).

**A refund** is the operator's: `POST /v1/orders/{order}/refund` with the
admin token refunds the latest sale or renewal (or `transaction`), whole
or `amount` of it, through Skrill's refund API (`refund.pl`: prepared,
then executed with the session id; the API password MD5-hashed, as the
guide says). The refund is a payment of its own with a receipt number,
`refunded` on the order adds up, and the licence is taken back
(`revoked`, `status` `refunded`, no `licence` in the order, the downloads
refused) when everything paid is refunded or when `revoke` says so; a
partial refund leaves it. Skrill refusing (a transaction already refunded,
a wrong password) is a 502 with Skrill's words, and nothing changes.

**A chargeback** comes from Skrill as status -3 on the status URL (the
sale) or the recurring one (a renewal): the licence is taken back at once
(`revoked_reason` `chargeback`, `status` `chargeback`), the subscription
ends, and a renewal Skrill still reports afterwards is noted and not
applied.

The refunds and the chargebacks are `GET /v1/reversals?since=N` for the
admin backend, each naming the sale or renewal it reverses; taking the
partners' credits back in the ledger is the admin's side, to come.

The API calls are what Skrill's Automated Payments Interface guide
documents; the sandbox run (the store's README, milestone 7) is where
they are first exercised against Skrill itself.

## The database

**PostgreSQL in production; SQLite for tests and development only.**
`ULTRASTORE_PAY_DB` is a URL: `postgresql://user:password@host:5432/db`
(psycopg 3, installed with the `postgres` extra) or `sqlite:///path` (a
bare path means the same). `db.py` hides the two engines behind one small
interface - the placeholder style, the serial type, how a row is read -
and `store.py` writes one SQL dialect against it; nothing else in the
service knows which engine runs. The tests run on SQLite always and on
PostgreSQL too when `ULTRASTORE_PAY_TEST_PG_URL` names one, which CI does
with a PostgreSQL service container, so the production engine runs the
whole purchase on every change. The service makes its tables on the first
start; the database and its user are made beforehand:

```
sudo -u postgres createuser --pwprompt ultrastore_pay
sudo -u postgres createdb --owner ultrastore_pay ultrastore_pay
```

## Settings

From the environment (`deploy/ultrastore-pay.env.example`): the database
URL (`ULTRASTORE_PAY_DB`, *The database*), the Skrill
merchant account (`ULTRASTORE_SKRILL_PAY_TO_EMAIL`, `_SECRET_WORD`,
`_MERCHANT_ID`, `_API_PASSWORD` for refunds and cancelling a recurring
payment, `_API_URL` to point the API elsewhere than
`https://www.skrill.com/app`), the grace days of a subscription
(`ULTRASTORE_PAY_GRACE_DAYS`), the public base URL
(`ULTRASTORE_PAYMENT_BASE_URL`, where Skrill posts the status report),
`ULTRASTORE_PAYMENT_ENV` (`sandbox` or `production`), the catalog
(`ULTRASTORE_PAY_CATALOG_URL`, `_CATALOG_KEYS`), the licence seed, the
admin token, and the restricted-URL secret and base. `GET /healthz` names
what is missing.

Skrill's sandbox is a merchant account the Skrill team puts into test
mode, on the same `pay.skrill.com`; `ULTRASTORE_SKRILL_PAY_URL` overrides
the host when a test endpoint is given.

## Running

```
python3 -m venv .venv && .venv/bin/pip install -e '.[test,postgres]'
.venv/bin/python -m pytest -q                                   # on SQLite
ULTRASTORE_PAY_TEST_PG_URL=postgresql://... .venv/bin/python -m pytest -q   # and on PostgreSQL
ULTRASTORE_PAY_DB=postgresql://... ULTRASTORE_PAYMENT_BASE_URL=https://store.example ... .venv/bin/ultrastore-pay
```

`deploy/`: the systemd unit, the nginx site (the publish folder as the
document root, the service behind `/pay` and `/v1`, `restricted/` behind
`secure_link_md5`), the environment file to fill in.
