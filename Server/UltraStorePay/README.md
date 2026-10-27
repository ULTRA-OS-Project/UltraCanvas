# The UltraStore payment service

The server side of a purchase in [UltraStore](../../Docs/UltraStore/README.md)
(*Payment*): a small [FastAPI](https://fastapi.tiangolo.com/) service on
SQLite that holds the merchant credentials - never the store binary, never
the catalog - and does what the store cannot do for itself:

- opens a **Skrill Quick Checkout** session for an app, priced from the
  same signed catalog the stores read, with the discount of the code the
  buyer arrived with;
- receives Skrill's **status report** (server to server, signed with the
  secret word), which is the one thing it trusts - the browser's return
  proves nothing;
- issues the **licence** the app checks (an Ed25519-signed token) and the
  numbered **receipt**;
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
| `POST /v1/skrill/status` | Skrill's status report (form fields) → `OK` |
| `GET /pay/{order}/return`, `/cancel` | where Skrill sends the buyer back (HTML) |
| `GET /v1/orders/{order}?secret=` | the order's state; when paid, `licence`, `licence_id`, `receipt_no`, `paid_at` |
| `GET /v1/orders/{order}/receipt?secret=` | the receipt (text) |
| `POST /v1/orders/{order}/download?secret=&file=` | `{url, expires}` for one of the app's restricted packages |
| `GET /v1/sales?since=N` | `Authorization: Bearer <admin token>` → `{sales: [{seq, order_id, at, code, app, amount, currency, receipt_no, skrill_transaction}], next}` |
| `GET /v1/keys` | `{licence_public_key, environment}` |
| `GET /healthz` | `ok`, or what is not configured |

## The licence

`ULIC1.<base64url payload>.<base64url signature>`: the payload names the
licence, the app, the order, the day it was issued and what the purchase
includes (`updates`); the signature is Ed25519 over the payload bytes with
the service's licence key (`ULTRASTORE_PAY_LICENCE_SEED`, or one made on
the first start and kept in the database). The public half is
`GET /v1/keys`; an app verifies a licence offline with it.

## Settings

From the environment (`deploy/ultrastore-pay.env.example`): the Skrill
merchant account (`ULTRASTORE_SKRILL_PAY_TO_EMAIL`, `_SECRET_WORD`,
`_MERCHANT_ID`, `_API_PASSWORD`), the public base URL
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
python3 -m venv .venv && .venv/bin/pip install -e '.[test]'
.venv/bin/python -m pytest -q
ULTRASTORE_PAYMENT_BASE_URL=https://store.example ... .venv/bin/ultrastore-pay
```

`deploy/`: the systemd unit, the nginx site (the publish folder as the
document root, the service behind `/pay` and `/v1`, `restricted/` behind
`secure_link_md5`), the environment file to fill in.
