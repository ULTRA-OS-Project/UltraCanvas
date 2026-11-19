# The UltraStore account service

The account tier of [UltraStore](../../Docs/UltraStore/README.md) (*The
account tier*): what follows a person from machine to machine, in one HTTPS
service the ULTRA OS Project runs beside the payment service and the
marketplace service. A small [FastAPI](https://fastapi.tiangolo.com/) service
on **MySQL** (PostgreSQL works too; SQLite for tests and development only)
through [`ultrastore-db`](../UltraStoreDb/README.md). It holds:

- the **users**: the email (verified by a mailed code), the password as an
  scrypt hash, the display name and handle, the language, the country (VAT
  and currency) and the year of birth (age ratings; never shown), the
  settings;
- the **sessions**: one per machine signed in - the *devices* of the design -
  with the machine's name, system and store version, when it was last seen,
  ended by the user from any machine or by the operator;
- the **second factor**: TOTP as UltraAuthenticator and every other
  authenticator app speaks it, with recovery codes kept hashed, each good
  once; the sign-in history;
- the **licences** attached to the account: a payment-service licence whose
  signature the service checks, and the machines it is installed on, counted
  against its seats;
- the **notices** sent to a person - by the service (a new sign-in, a
  password change) or by the operator - kept on the account and sent by mail;
- the **data archive** a person asks for, built at once and released by the
  operator, downloaded once; the **deletion** a person asks for, carried out
  after a grace;
- the **audit log**: append-only, hash-chained, every operator action one
  row, the marketplace service's chain.

What it never holds: the password itself, a payment method (the payment
service's, and that keeps only Skrill's reference), the listening and watch
history (each machine's own), a merchant's records (the marketplace
service's). What the operator never sees: the password hash, the year of
birth, the email beyond its first two letters.

## Who is asking

| | |
|---|---|
| anyone | sign up, verify, sign in, a forgotten password: the store on a machine, before it is signed in |
| a signed-in store | `Authorization: Bearer <session token>`, the `us_…` token the sign-in answered with; a token not used for `ULTRASTORE_ACCOUNT_SESSION_IDLE_DAYS` (90) is signed out |
| the operator | `Authorization: Bearer <operator token>` (`ULTRASTORE_ACCOUNT_OPERATOR_TOKEN`): the operator's window and command line, `ultrastoreop users` (`Apps/UltraStoreOperator`) |

## API

Requests and answers are JSON; errors are `{"error", "error_description"}`
with a 4xx status. A `device` is `{name, os, client}`: what the store knows
of the machine it runs on.

**Anyone**

| | |
|---|---|
| `POST /v1/signup` | `{email, password, country?, year_of_birth?, language?, marketing?}` → `{user: {id, email}, verify: "email_code"}`; a six-digit code goes to the address. An address that has an account gets the same answer and a mail to its owner, so the service does not tell a stranger whether an address is registered. A password is at least 12 characters |
| `POST /v1/signup/verify` | `{email, code, device}` → `{token, device, user}`: the address verified, this machine signed in |
| `POST /v1/signup/resend` | `{email}`: the code again, at most once a minute |
| `POST /v1/signin` | `{email, password, device, totp?, recovery_code?}` → `{token, device, user}`. With two-factor on, 401 `two_factor_required` until the app's code or a recovery code comes with it; a used recovery code is spent and leaves a notice. More than ten failed attempts in a quarter of an hour: 429 |
| `POST /v1/password/forgot` | `{email}`: a code by mail |
| `POST /v1/password/reset` | `{email, code, new_password, totp?, recovery_code?}`: the second factor is asked for when on; every machine is signed out |
| `GET /healthz` | `ok`, or what is not configured |

**A signed-in store**

| | |
|---|---|
| `GET /v1/me` | the profile, `two_factor`, `recovery_codes_left`, this `device`, the `devices`, the `licences` (with their tokens and installs), the `settings`, `unread_notices`, the open `archive` |
| `PUT /v1/me` | `{display_name?, handle?, language?, country?, year_of_birth?, about?}`; a handle is 3 to 32 letters, digits, dots or underscores, unique |
| `PUT /v1/me/settings` | `{notifications?, history_on_account?, public_profile?, marketing?}`; `notifications.security` stays `email` whatever is sent |
| `POST /v1/me/email` | `{email, password}`: a code to the new address; `POST /v1/me/email/verify {code}` moves the account, the old address is told |
| `POST /v1/me/password` | `{current, new}` → `{changed, signed_out}`: the other machines are signed out |
| `POST /v1/me/two-factor/setup` | → `{secret, otpauth_url}` for the authenticator app; `POST /v1/me/two-factor/enable {code}` → `{recovery_codes}` (eight, shown once); `POST /v1/me/two-factor/disable {code}`; `POST /v1/me/two-factor/recovery-codes {code}` → a new set |
| `GET /v1/me/devices` | the machines signed in, `current` marking this one; `PUT /v1/me/devices/{id} {name}`; `DELETE /v1/me/devices/{id}` signs it out and frees the licence seats it holds |
| `POST /v1/me/signout`, `POST /v1/me/signout-all` | this machine; every other machine |
| `GET /v1/me/sign-ins` | the sign-in history, refused attempts included |
| `GET /v1/me/licences` | the licences on the account; `POST /v1/me/licences {licence, seats?}` attaches a payment-service licence (`ULIC1.…`, verified with `ULTRASTORE_ACCOUNT_LICENCE_KEY`; a renewal of one already attached replaces its token; one attached to another account is 409 `licence_taken`); `DELETE /v1/me/licences/{id}` releases it |
| `POST /v1/me/licences/{id}/install`, `/uninstall` | a seat taken or freed for this machine; 409 `no_seat` when every seat (`seats`, else `ULTRASTORE_ACCOUNT_DEFAULT_SEATS`) is taken |
| `GET /v1/me/notices` | the unread first; `POST /v1/me/notices/{id}/read` |
| `POST /v1/me/archive` | the data archive requested and built at once → `{archive: {id, state, due}}`; `GET /v1/me/archive` the archives; `GET /v1/me/archive/{id}/download` the JSON, once, after the operator released it |
| `POST /v1/me/delete` | `{password}`: the account marked `deleting`, carried out after `ULTRASTORE_ACCOUNT_DELETION_GRACE_DAYS` (30); `POST /v1/me/delete/cancel` before then |

**The operator**

| | |
|---|---|
| `GET /v1/users[?state=active\|suspended\|deleting\|requests&q=]` | the users (the email masked, no year of birth, no password) with the machines and licences counted and the open `request`, and the `stats` |
| `GET /v1/users/{id}` | the file: the devices, the licences (without their tokens), the notices, the archives, the last sign-ins, the history from the audit log |
| `POST /v1/users/{id}/notice` | `{kind: notice\|security\|warning\|suspension\|information_request, subject, text}`: written to the account and sent by mail |
| `POST /v1/users/{id}/signout-all` | `{reason}`: every machine signed out, the seats freed |
| `POST /v1/users/{id}/two-factor/reset` | `{reason}`: after checking the person's identity; the second factor off, every machine signed out, the person told |
| `POST /v1/users/{id}/suspend` | `{reason, days}` (0: until reinstated): signed out everywhere, sign-in refused; `POST .../reinstate {reason}` |
| `POST /v1/users/{id}/delete` | `{reason}`: a deletion the person asked for carried out now; 409 `not_requested` otherwise - the operator never deletes an account nobody asked to delete |
| `POST /v1/archives/{id}/release` | the archive released to the person, who is told by notice and mail |
| `POST /v1/maintenance` | deletions past their grace carried out, suspensions past their end lifted → the counts; a timer runs it daily (`deploy/`); the users list runs it too |
| `GET /v1/stats` | `accounts`, `new_in_30_days`, `two_factor_percent`, `archive_requests`, `archive_requests_due`, `deletion_requests`, `suspended` |
| `GET /v1/audit?since=N&limit=`, `GET /v1/audit/verify` | the log, and the chain walked: `{ok, rows}` or `{ok: false, broken_at}` |

## What a deletion does

The person asks, with the password; the account is `deleting` for the grace
days, signed in still, and a cancel brings it back. Then - by the timer, or
by the operator on the person's request - every machine is signed out, every
licence released (the apps keep working on the machines they are on, without
updates: the licence file is the machine's), the codes, notices, archives,
sign-ins and sessions removed, and the row kept under its id with the email
replaced, so the address is free again and the audit rows still name a
target. The invoices are the payment service's and stay as the law asks.

## Mail

Codes, security notices and the operator's notices go out by mail:
`ULTRASTORE_ACCOUNT_SMTP_URL=smtp://user:password@host:587` (STARTTLS) or
`smtps://…:465`, from `ULTRASTORE_ACCOUNT_MAIL_FROM`. Left empty, the
service writes them to its log instead - a development machine, never a
production one. The tests hand `create_app` a mailer of their own.

## Running

```
pip install -e ../UltraStoreDb[mysql] -e '.[mysql]'      # or [postgres]
ULTRASTORE_ACCOUNT_DB=mysql://ultrastore_account:password@127.0.0.1:3306/ultrastore_account \
ULTRASTORE_ACCOUNT_OPERATOR_TOKEN=... ULTRASTORE_ACCOUNT_LICENCE_KEY=<the payment service's GET /v1/keys> \
ULTRASTORE_ACCOUNT_SMTP_URL=smtp://... .venv/bin/ultrastore-account
```

The database and its user are made beforehand:

```
mysql -u root -e "CREATE DATABASE ultrastore_account CHARACTER SET utf8mb4;
  CREATE USER 'ultrastore_account'@'localhost' IDENTIFIED BY 'password';
  GRANT ALL ON ultrastore_account.* TO 'ultrastore_account'@'localhost';"
```

The service makes its tables on the first start. `deploy/`: the systemd
unit, the maintenance timer, the nginx location, the environment file to
fill in. The tests: `pip install -e '../UltraStoreDb[mysql,postgres]' -e
'.[test]'`, then `python -m pytest -q`, on SQLite and on whatever
`ULTRASTORE_ACCOUNT_TEST_PG_URL` and `ULTRASTORE_ACCOUNT_TEST_MYSQL_URL`
name (`.github/workflows/ultrastore-account.yml` runs all three).

## What comes next

Passkeys (WebAuthn needs the platform's authenticator on each machine; the
design draws them); the payment service told the account at checkout, so an
order lands on the account without the store attaching the licence; an
avatar; the merchant-to-buyer message relayed through the marketplace
service with the order as the key (the store's README, *The account tier*).
The store's side is `Apps/UltraStore` (*Sign in*, the account page,
`ultrastore account`); the operator's is `Apps/UltraStoreOperator`
(*Users*, `ultrastoreop users`).
