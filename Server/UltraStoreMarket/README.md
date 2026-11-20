# The UltraStore marketplace service

The operator tier of [UltraStore](../../Docs/UltraStore/README.md) (*The
operator tier*): the records that belong to no merchant, in one HTTPS
service the ULTRA OS Project runs as the operator, beside the payment
service. A small [FastAPI](https://fastapi.tiangolo.com/) service on
**MySQL** (PostgreSQL works too; SQLite for tests and development only)
through [`ultrastore-db`](../UltraStoreDb/README.md). It holds:

- the **merchants**, their keys, their profile (the website the catalog
  names as the publisher's page, a description) and where each stands
  with the terms; the **applications** for a shop, decided by the
  operator;
- the **submissions**: a merchant's signed component, its packages' hashes,
  the automatic checks, and every decision appended - never edited;
- the **reports** from the three doors (the store's app page, a merchant,
  the operator from mail), the reporter visible to the operator only;
- the **notices** (announcement, reminder, information request, warning,
  suspension), the strikes a warning counts, and the **enforcement policy**
  that turns strikes into a suspension;
- the **suspensions** of a merchant or an app, and who lifted them;
- the **terms** versions and the merchants' acceptances;
- the **audit log**: append-only, hash-chained, every operator action one
  row, so a gap or an edit shows.

What it never holds: a merchant's sales ledger and partners (the merchant's
`admin.sqlite`, fed by the payment service), a merchant's signing seed, the
payment credentials.

## Who is asking

| | |
|---|---|
| the operator | `Authorization: Bearer <session or operator token>`: the operator's window and command line, `ultrastoreop` (`Apps/UltraStoreOperator`). A **session** comes from `POST /v1/operators/signin` with an operator account's email and password: `op1.<payload>.<hmac-sha256>` over the operator's id, name, role and the end (`ULTRASTORE_MARKET_OPERATOR_SESSION_HOURS`, 12), signed with `ULTRASTORE_MARKET_OPERATOR_SESSION_SECRET` (`ultrastore_db.operator_session`); the account service verifies the same session with the same secret, so one sign-in reaches both. The static `ULTRASTORE_MARKET_OPERATOR_TOKEN` is the **bootstrap admin** ("operator"): it makes the first account. Every operator call names a permission; a role without it is refused with 403 `forbidden`. The roles: `admin` everything (the accounts included), `reviewer` `review.decide` and `publish`, `support` `reports.handle`, `notices.send` and `users.support`, `auditor` reads only. The audit log names the person. |
| a merchant | `Authorization: Merchant <id>:<timestamp>:<signature>`, the Ed25519 signature with the merchant's key over `<METHOD> <path>\n<timestamp>\n` and the body bytes (`auth.py`, `sign_request`); a timestamp outside the window (300 s) is refused, so a captured request is not replayed. The key is the one that signs the merchant's submissions, registered by the operator and rotated with `PUT /v1/merchants/{id}` |
| anyone | the store's report form, the reporter's outcome line, the terms |

## API

Requests and answers are JSON; errors are `{"error", "error_description"}`
with a 4xx status.

The operator accounts (`operators` table, schema 3): `POST /v1/operators/signin`
`{email, password}` → `{token, operator, hours}` (401 `bad_credentials`; 503
`not_configured` without the session secret); `GET /v1/operators/me` who the
bearer is, with the permissions; `GET /v1/operators` the accounts and the
roles; `POST /v1/operators` `{email, name, role, password}` (admin; a
password is 12 characters or more, kept as scrypt); `PUT /v1/operators/{id}`
`{name?, role?, password?, disabled?}` - anyone their own name and password,
an admin the rest, never their own switch or admin role (409 `own_account`).
A disabled account's session is refused from the next call on. **The
second factor** (schema 4): `POST /v1/operators/me/two-factor/setup` →
`{secret, otpauth_url}`, `.../enable {code}` → `{recovery_codes}` (eight,
shown once, kept hashed), `.../disable {code}`, `.../recovery-codes {code}`;
the sign-in then takes `totp` (the app's six digits) or `recovery_code`
(used up), else 401 `two_factor_required`. **A forgotten password**: `POST
/v1/operators/password/forgot {email}` mails a six-digit code
(`ULTRASTORE_MARKET_SMTP_URL`; the log without one) good for
`ULTRASTORE_MARKET_CODE_MINUTES` (10), five tries; `POST
/v1/operators/password/reset {email, code, new_password, totp?,
recovery_code?}`. An admin's `PUT /v1/operators/{id} {two_factor: false}`
clears another's second factor, never their own.

**The review pipeline** (`checks.py`): once a submission's packages are
all in the store the service runs, in the background, the checks behind
the ones it makes at once - `scans` (the archives walked: paths that
escape, links outside, a zip bomb, an installer inside; each file handed
to `ULTRASTORE_MARKET_SCAN_COMMAND` when set, `{file}` in its text, exit 0
clean and 1 found as clamscan does), `entry` (the file the store launches
is in the archive, the package's or one found by the admin backend's
rules), `smoke` (`ULTRASTORE_MARKET_SMOKE_COMMAND`, a sandbox runner with
`{file}`, `{entry}`, `{os}`, `{arch}`, exit 0 a pass; *not run* without
one) and `listing` (what the store's page needs). Each is `ok`, `failed`
or `not run`, with `findings` (lines per check) and `checked_at` on the
submission. A failed `scans` or `entry` makes it `failed_checks`; the
listing is also checked at submit time without a package store. `POST
/v1/submissions/{id}/checks` (`review.decide`) runs the pipeline again
and answers with the submission. 7z and tar.zst packages need the `scan`
extra (`py7zr`, `zstandard`); without it that package is *not run*, said
so. `ULTRASTORE_MARKET_CHECK_TIMEOUT` (120 s) bounds each external run.

**The package store** (`ULTRASTORE_MARKET_PACKAGE_DIR`): with one, a
submission's `packages` check is `waiting` until the merchant has sent every
package's bytes - `POST /v1/submissions/{id}/packages/{sha256}`, the body
the bytes (at most `ULTRASTORE_MARKET_MAX_PACKAGE_MB`, 512), the request
signed over them like every other; the service hashes them, checks the size
the submission named, and keeps them under `<sha256>.<format>` (400
`hash_mismatch` / `size_mismatch`, 404 `no_such_package`, 409
`wrong_state` once decided). Bytes already in the store need no upload. A
release is refused while a package is missing (409 `packages_missing`). The
operator's `GET /v1/submissions/{id}/packages/{sha256}` downloads one (the
review's *Download build*). Without a package store the check is `ok` on
the shape alone and an upload is 503 `not_configured`, as before.

**The operator**

| | |
|---|---|
| `POST /v1/merchants` | `{id?, name, contact?, key}` → the merchant (`standing`, `strikes`, `terms`) |
| `GET /v1/merchants`, `GET /v1/merchants/{id}` | the merchants; one with its suspensions, acceptances and submissions |
| `PUT /v1/merchants/{id}` | `{name?, contact?, key?}` - the key rotated |
| `GET /v1/applications[?state=]`, `GET /v1/applications/{id}` | the applications for a shop, with the applicant's email |
| `POST /v1/applications/{id}/approve` | `{id?, message?}` → the application and the merchant registered with the application's key and profile, under the id given or one made |
| `POST /v1/applications/{id}/reject` | `{message}`: the reason the applicant reads |
| `POST /v1/merchants/{id}/suspend` | `{clause, reason, days}` (0: until reinstated); `POST .../reinstate {reason}` |
| `POST /v1/apps/{app}/suspend`, `/reinstate` | the same for one app: it leaves `/v1/released` while suspended |
| `GET /v1/submissions[?state=&merchant=&app=]`, `GET /v1/submissions/{id}` | the review's queue; one with its decisions |
| `POST /v1/submissions/{id}/decide` | `{action, message, sequence?}` - `start_review`, `release` (the next catalog sequence, or the one given), `request_changes`, `reject`; a decision is appended, the state follows, a released submission stays released |
| `GET /v1/released` | what the catalog is generated from: each app's latest released component, from merchants in good standing and apps not suspended; `sequence` the last release's, `published` the last publish's |
| `POST /v1/publish` | the catalog generated from it, signed into the publish folder (*The publish*) → `{sequence, apps, components, publishers, left_out, files, public_key}` |
| `GET /v1/reports[?state=]`, `GET /v1/reports/{id}` | with the reporter |
| `POST /v1/reports/{id}/assign {assignee}`, `/resolve`, `/dismiss {findings, outcome}` | the findings for the operator, the `outcome` line for the reporter and the merchant |
| `POST /v1/notices` | `{kind, subject, text, clause?, recipients}` (`["*"]` for everyone) → the notice and the merchants the policy suspended on it |
| `GET /v1/notices` | all of them |
| `POST /v1/terms` | `{text, changes, in_force_from, version?}`; `GET /v1/terms`, `GET /v1/terms/current` are public |
| `GET /v1/policy`, `PUT /v1/policy` | `strike_expiry_days` (365), `strikes_before_suspension` (3), `suspension_days` (30), `answer_deadline_days` (14), `clauses_that_suspend` (`[]`) |
| `GET /v1/audit?since=N&limit=`, `GET /v1/audit/verify` | the log, and the chain walked: `{ok, rows}` or `{ok: false, broken_at}` |

**A merchant**

| | |
|---|---|
| `GET /v1/merchants/me` | the standing (`active`, `paused` until the terms in force are accepted, `suspended`), the strikes, the terms, the active suspensions, the acceptances, the profile (`contact`, `website`, `description`, `key`) |
| `PUT /v1/merchants/me` | `{name?, contact?, website?, description?, key?}`: the merchant's own profile; the website is the publisher's page the publish names; a key given is the rotation, the request signed with the old one, 409 `key_taken` when it signs for another merchant |
| `POST /v1/submissions` | `{component, signature, packages, answers?}`: the component object as `PutComponent` reads it back, signed with the merchant's key over its canonical JSON (sorted keys, no spaces); the packages as `{file or url, sha256, size}`; `answers` the submission a resubmission answers. The checks the service makes at once: the signature, the merchant's standing, the packages' hashes; the scans, the entry, the smoke run and the listing are *not run* until the review pipeline exists. State `checked` or `failed_checks` |
| `GET /v1/merchants/me/submissions` | with every decision |
| `POST /v1/submissions/{id}/withdraw` | |
| `GET /v1/merchants/me/notices` | the unacknowledged first; `POST /v1/notices/{id}/ack` |
| `GET /v1/merchants/me/reports` | about the merchant, its apps and its submissions - the reporter, the assignee and the findings removed |
| `GET /v1/merchants/me/history` | the audit rows about them |
| `POST /v1/terms/accept` | `{version, accepted_by}` |
| `POST /v1/reports` | signed: a merchant reporting another's app or a problem with their own |

**Anyone**

| | |
|---|---|
| `POST /v1/merchants/apply` | `{name, contact, email?, website?, description?, key, signature}`: a shop asked for; `signature` is Ed25519 by `key` over the canonical JSON of the other fields, so the key is proven; 409 `key_registered` when it signs for a merchant already, `applied_already` while one waits → `{id, secret, state}` |
| `GET /v1/applications/{id}?secret=` | what became of it: `applied`, `approved` with `merchant`, `rejected` with `message` |
| `POST /v1/reports` | `{target_kind: app\|release\|merchant, target, kind, text, attachments?, reporter?}` → `{id, secret}`; the store sends the licence or the install id as `reporter` when there is one |
| `GET /v1/reports/{id}/outcome?secret=` | the one line the reporter learns |
| `GET /v1/terms`, `GET /v1/terms/current` | |
| `GET /v1/publish/key` | `{public_key, trusted_keys}`: the operator's signing key |
| `GET /healthz` | `ok`, or what is not configured |

## The publish

`POST /v1/publish` (the operator) generates the one signed `catalog.json`
the stores read from the released submissions (`publish.py`): each app's
latest released component as the merchant sent it, from merchants in good
standing and apps not suspended, under `apps` or `components` by its
kind; the `publishers` from the components' publisher ids with the
merchants' names (and the contact as the URL when it is one); the
`trusted_keys` - the operator's signing key and `ULTRASTORE_MARKET_TRUSTED_KEYS`
(a key being rotated in); `payment_url` and `licence_key` from
`ULTRASTORE_MARKET_PAYMENT_URL` and `_LICENCE_KEY`; `market_url`, this
service's public URL (`ULTRASTORE_MARKET_BASE_URL`, ending in `/market`
behind the deploy folder's nginx location), where the store's app page reports a problem; `codes` empty (the
codes are the merchants' own stores'). The sequence counts up past the
last publish and the last release. The catalog and `catalog.json.sig`
(the Ed25519 signature over the exact bytes, hex) go into
`ULTRASTORE_MARKET_PUBLISH_DIR`, which the web server serves as the
catalog base. The operator's signing key is `ULTRASTORE_MARKET_SIGNING_SEED`
(32 bytes, hex), or one made on the first start and kept in the database;
`GET /v1/publish/key` gives its public half for `Apps/UltraStore/keys/`
and for a store's trusted keys. A package the merchant uploaded into the
package store is laid out beside the catalog - `packages/<sha256>.<format>`,
or `restricted/` for a paid app's without a trial, which the web server
serves to nobody directly - and the catalog names it under
`ULTRASTORE_MARKET_CATALOG_URL` (the public URL of the publish folder); the
report says how many. A package not uploaded keeps the URL the merchant
named, so a merchant with a server of its own loses nothing.

## Strikes, suspensions, standing

A warning names a clause and counts as a strike for each merchant it is
addressed to, for `strike_expiry_days`. When a merchant's unexpired strikes
reach `strikes_before_suspension`, or the clause is one of
`clauses_that_suspend`, the policy suspends the merchant for
`suspension_days` - a suspension row by the actor `policy`, in the audit
log with the notice it acted on. A merchant or an app under an active
suspension is out of `/v1/released`; a merchant that has not accepted the
terms version in force by its date is `paused` and out of it the same way;
`release` refuses a submission of a merchant not `active`. The operator
suspends and reinstates by hand as well.

## The audit log

Every operator action - a registration, a key rotation, a terms version,
an acceptance, a submission and each decision on it, an assignment, a
resolution, a notice, a suspension, a reinstatement, a policy change, an
acknowledgement - is one row: the actor, the action, the target, the
clause, the state before and after (JSON), the row it acted on, and the
hash of the row before it; the row's own hash is SHA-256 over those and
the previous hash. `GET /v1/audit/verify` walks the chain and names the
first row whose hash or link is wrong, so an edit or a deletion shows.

## Running

```
python3 -m venv .venv && .venv/bin/pip install -e ../UltraStoreDb -e '.[test,mysql,postgres]'
.venv/bin/python -m pytest -q                                                          # on SQLite
ULTRASTORE_MARKET_TEST_MYSQL_URL=mysql://ultrastore:ultrastore@127.0.0.1:3306/ultrastore_test \
ULTRASTORE_MARKET_TEST_PG_URL=postgresql://... .venv/bin/python -m pytest -q           # and on MySQL and PostgreSQL
ULTRASTORE_MARKET_DB=mysql://ultrastore_market:password@127.0.0.1:3306/ultrastore_market ULTRASTORE_MARKET_OPERATOR_TOKEN=... \
ULTRASTORE_MARKET_OPERATOR_SESSION_SECRET=<the same value as the account service's> \
ULTRASTORE_MARKET_PACKAGE_DIR=/var/lib/ultrastore-market/packages ULTRASTORE_MARKET_CATALOG_URL=https://store.example/v1 .venv/bin/ultrastore-market
```

The database and its user are made beforehand:

```
mysql -u root -e "CREATE DATABASE ultrastore_market CHARACTER SET utf8mb4;
  CREATE USER 'ultrastore_market'@'localhost' IDENTIFIED BY 'password';
  GRANT ALL ON ultrastore_market.* TO 'ultrastore_market'@'localhost';"
```

The service makes its tables on the first start. `deploy/`: the systemd
unit, the nginx location, the environment file to fill in.

The first operator account: with the static token, `ultrastoreop settings
set token <ULTRASTORE_MARKET_OPERATOR_TOKEN>`, then `ultrastoreop operator
add alex@example.org admin --name Alex` (asked for the password), then
`ultrastoreop signin alex@example.org`; the static token goes into a drawer.

## What comes next

The merchant's media and the web store's pages through this service, which
today publishes the catalog and the packages alone (the merchant backend's
publish still produces the pictures and the pages). The operator's window
and command line on this API are `Apps/UltraStoreOperator`
([its README](../../Docs/UltraStoreOperator/README.md)).
