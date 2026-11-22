# UltraAccount

## Overview

UltraAccount is the one account a person has across ULTRA OS: the store's
purchases, UltraAuthenticator's recovery from a forgotten master password,
and next cloud storage (UltraCloud) and the paid AI services (UltraAI). It
ships as `Apps/UltraAccount`, a small window built from UltraCanvas
elements, and as `UltraAccountCore`, the client every application that
needs the account links.

- Version: its own, from the first line of
  [`Docs/UltraAccount/CHANGELOG.md`](CHANGELOG.md). `--version` prints it.
- Service: [`Server/UltraStoreAccount`](../../Server/UltraStoreAccount/README.md),
  the account service UltraStore already signs in to - one account, one
  database. `--service URL` uses another (an `http://` address only on
  `127.0.0.1` / `localhost`, for development); the default is
  `https://store.ultraos.eu/account`.
- Files: `session.json` in UltraAccount's data folder
  (`%APPDATA%\UltraAccount\`, `~/Library/Application Support/UltraAccount/`,
  `$XDG_DATA_HOME/UltraAccount/` or `~/.local/share/UltraAccount/`;
  `UltraCanvasDataFolder.h`), written owner-only: the service, the session
  token, the account's id and email, this machine's device id.

## The window

Before signing in, four small pages:

- **Sign in** - the email and the password; when the account has
  two-factor sign-in on, a field for the authenticator's code appears.
- **Create an account** - the email, the password twice (at least 12
  characters), the country. A six-digit code goes to the address.
- **Verify your address** - the code; *Send the code again* at most once a
  minute.
- **Forgotten password** - a code to the address, then a new password (and
  the authenticator code when two-factor sign-in is on). Every machine
  signed in is signed out.

Signed in, the account:

- **The email** and whether it is verified.
- **The plan** - *free*, or *plus* until a date. *Subscription* takes a
  licence for **UltraAccount Plus** (the payment service's `ULIC1.…`
  licence for the subscription app, `UltraAccountPlus`) and activates it; a
  renewed licence moves the end date.
- **The phone number** - typed with its country code, confirmed with the
  account password, verified by a texted six-digit code. A text message
  costs money to send, so a phone number needs UltraAccount Plus; *Remove
  number* takes it off again.
- **Recovery keys** - the applications that left a recovery key with the
  account (UltraAuthenticator's, below), with *Remove*: a removed key can no
  longer be released by a code.
- **Sign out** - this machine.

## One sign-in for every application

The session is the machine's, not the window's: UltraAuthenticator (and
next UltraCloud and UltraAI) read `session.json` and use the account
without asking for the password again. Signing out here signs them out
too. A session the service no longer knows - 90 days unused, signed out from
another machine - is dropped the next time it is used.

## Recovery keys

An application whose data only the person can open - UltraAuthenticator's
vault is encrypted with a key derived from the master password - cannot
have a "forgot password" link of its own: nobody else holds the key. What
it can do is split a **recovery key** in two:

1. half of it stays in the application's own file;
2. half of it is left with the account (`POST /v1/me/recovery-keys`), kept
   there sealed with the service's escrow key.

On the day the password is forgotten, the application asks the service to
send a code to the account's verified email - or, on UltraAccount Plus, by
text to its verified phone - and the service releases its half for that
code. Neither half opens anything alone: the service never sees the file,
and the file never holds the service's half. Every release is told to the
person by mail and as a notice on the account, is limited to a few codes a
day, and spends the key: the application turns recovery on again with a new
one. UltraAuthenticator also shows its half once as a printed **recovery
code**, which works without the account and without a network.

## For an application: `UltraAccountCore`

```cpp
#include "UltraAccountClient.h"

UltraAccount::Session session = UltraAccount::Session::Load();
if (session.SignedIn()) {
    UltraAccount::Client client(session.url);
    auto me = client.GetMe(session.token);
    if (me && me.Value().plan.paid) { /* the paid features */ }
}
```

Every call returns `Result<T>`: the value, or an `Error` whose `code` is the
service's (`two_factor_required`, `subscription_required`, `bad_code`,
`code_sent`, `signed_out`, ...) or `network`, and whose `message` is for the
person. `UltraAccount::Describe(error)` gives one either way. Link
`UltraAccountCore` (`Apps/UltraAccount/CMakeLists.txt`); it brings the
framework and UltraNet.

## What comes next

- Cloud storage and the AI services reading the plan (`Me::plan`,
  `GET /v1/me/subscription`) to decide what a person may use.
- The payment service telling the account at checkout, so a bought
  subscription is active without pasting the licence.
- The service calls off the window's thread, with a progress line.
- The two-factor setup, the devices and the notices, which UltraStore's
  account page has, here too.
