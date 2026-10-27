# UltraMail — signing in to your email provider

How to add an account for each email provider UltraMail knows, what each
provider expects (a password, an *app password*, or a browser sign-in), and
what has to be set up once on the provider's side and on the machine.

Companion documents: [`Concept.md`](Concept.md) (the design),
[`CHANGELOG.md`](CHANGELOG.md), and the developer notes in
[`Apps/UltraMail/README.md`](../../Apps/UltraMail/README.md).

## 1. Adding an account, in general

1. Click **Add account** (or **Add email account** on the start page).
2. Enter your name, your address and — depending on the provider — a password,
   an app password, or nothing. The hint under the password field tells you
   which, as soon as the address is typed.
3. **Continue.** UltraMail looks the address up in its provider table
   (section 2). For any other domain it asks the provider — the domain's own
   autoconfig document, then the Thunderbird provider database — while a
   "Looking up server settings" dialog waits (Cancel gives up). If nothing
   is published, the **server settings page** opens, prefilled with the
   conventional host names, and you enter the servers from your provider's
   help page (section 5). Either way UltraMail shows the incoming (IMAP)
   and outgoing (SMTP) servers it will use, and stores them with the account.
4. Your account passwords and sign-ins are stored encrypted in UltraMail's
   vault; nothing is asked for. To have UltraMail ask for a password at every
   start, set a **Primary Password** (*Settings > Privacy > Primary Password*).
5. For Gmail, Outlook and Yahoo the browser opens the provider's sign-in page;
   when it says *Signed in - you can close this tab*, come back to UltraMail.
6. The inbox is fetched right away. Later, every account is synced every
   five minutes - or as often as *Settings > Mail > New mail* says, from every
   20 seconds to every 10 minutes - and **Update** (the download icon) fetches
   the account on screen now.

The three ways in:

| Sign-in | What you type in the wizard | Providers |
|---|---|---|
| **Browser sign-in (OAuth2)** | Password left **empty** | Gmail, Outlook / Microsoft 365, Yahoo |
| **App password** | A password generated in the provider's security settings | Yahoo, iCloud |
| **Account password** | Your normal password | GMX, WEB.DE, mailbox.org, Posteo |

Whatever you type or obtain goes into UltraMail's **credential vault**, never
into a configuration file.

## 2. Provider by provider

The servers below are what UltraMail configures automatically; you do not
type them. Every connection uses TLS.

### Gmail (`gmail.com`, `googlemail.com`)

| | |
|---|---|
| Incoming | `imap.gmail.com`, port 993, TLS |
| Outgoing | `smtp.gmail.com`, port 465, TLS |
| Sign-in | **Browser sign-in with Google only** (no password field) |

Google rejects the normal account password in mail programs, and UltraMail
signs in to Gmail through the browser only: the wizard shows no password
field for a Gmail address. UltraMail opens Google's consent page in your
browser with your address preselected; allow UltraMail to read and send your
mail. The sign-in runs through the ULTRA OS sign-in service, so there is
nothing to set up. A build with the service turned off says so in the
wizard's hint and cannot add a Gmail account until a service or an OAuth
client is configured — see [section 3](#3-the-sign-in-service-and-oauth-clients--for-people-building-or-packaging-ultramail)
for packagers.

### Outlook.com, Hotmail, Live, Microsoft 365 (`outlook.com`, `hotmail.com`, `live.com`, `msn.com`, `office365.com`)

| | |
|---|---|
| Incoming | `outlook.office365.com`, port 993, TLS |
| Outgoing | `smtp.office365.com`, port 587, STARTTLS |
| Sign-in | **Browser sign-in with Microsoft only** (leave the password empty) |

Microsoft has retired password sign-in ("basic authentication") for IMAP and
SMTP on personal Outlook.com accounts and in Microsoft 365, and app passwords
are gone with it. Leave the password field empty; UltraMail opens Microsoft's
sign-in page in your browser. The sign-in runs through the ULTRA OS sign-in
service, so there is nothing to set up (packagers: see
[section 3](#3-the-sign-in-service-and-oauth-clients--for-people-building-or-packaging-ultramail)).

For a **Microsoft 365 work or school** mailbox the tenant administrator must
leave **IMAP** and **Authenticated SMTP** enabled for the mailbox (Exchange
admin center → the mailbox → *Email apps*); otherwise the sign-in succeeds
but the mail session is refused. Addresses on a company's own domain are not
in UltraMail's provider table yet (see [section 5](#5-other-providers)).

### Yahoo Mail (`yahoo.com`, `yahoo.de`, `ymail.com`, `rocketmail.com`, Yahoo's country domains)

| | |
|---|---|
| Incoming | `imap.mail.yahoo.com`, port 993, TLS |
| Outgoing | `smtp.mail.yahoo.com`, port 465, TLS |
| Sign-in | **App password**, or the browser sign-in with Yahoo |

**App password (recommended for now).** In your Yahoo account open
**Account security > Generate app password**, name it "UltraMail", and type the
password Yahoo shows into the wizard's password field. Your normal Yahoo
password does not work in mail programs. Some Yahoo accounts no longer offer
app passwords; those can only use the browser sign-in.

**Browser sign-in.** It only works once Yahoo has granted UltraMail's OAuth
client access to Mail, which Yahoo does per app (see
[section 3](#3-the-sign-in-service-and-oauth-clients--for-people-building-or-packaging-ultramail)); until then Yahoo
signs you in but its mail servers reject the login. Leave the password field
empty; UltraMail opens Yahoo's consent page in your browser, and after you
approve, Yahoo sends the browser to the ULTRA OS sign-in service, which says
*Signed in with Yahoo* - come back to UltraMail. Nothing to copy or paste:
Yahoo insists on an https redirect, which the service is and a desktop
program cannot be. (An installation that signs in to Yahoo with an OAuth
client of its own, section 3, still gets the paste-the-code dialog instead:
copy the code Yahoo shows, or the whole `https://127.0.0.1…` address the
browser lands on, into it.)

### iCloud Mail (`icloud.com`, `me.com`, `mac.com`)

| | |
|---|---|
| Incoming | `imap.mail.me.com`, port 993, TLS |
| Outgoing | `smtp.mail.me.com`, port 587, STARTTLS |
| Sign-in | **App-specific password** |

Apple requires an app-specific password for third-party mail programs, and
the Apple ID must have two-factor authentication on. Sign in at
`account.apple.com` (formerly `appleid.apple.com`), open *Sign-In and
Security → App-Specific Passwords*, generate one named UltraMail, and type it
into the wizard. iCloud Mail itself must be enabled for the Apple ID.

The wizard carries these steps itself: under the sign-in fields, *How to set
up an iCloud mail account* has a **Show info** button that opens them in an
info area (offered for iCloud addresses and for addresses at a domain UltraMail
does not know, which is what an own domain on iCloud+ looks like).

**An own domain on iCloud+.** Type your own address in the wizard. UltraMail
finds no settings published for the domain and opens the server settings page;
enter `imap.mail.me.com` (993, SSL/TLS) and `smtp.mail.me.com` (587,
STARTTLS), and as the **username your @icloud.com address**, not the address
on your own domain. The password is the app-specific password.

**If the sign-in still fails:** changing the Apple Account password revokes
every app-specific password, so create a new one; check that iCloud Mail is
turned on; with an own domain, check that the username is the @icloud.com
address. Apple also accepts just the part before the @ as the incoming
username, but its outgoing server wants the full address, and UltraMail uses
one username for both - so keep the full address.

### GMX (`gmx.net`, `gmx.de`, `gmx.com`)

| | |
|---|---|
| Incoming | `imap.gmx.net`, port 993, TLS |
| Outgoing | `mail.gmx.net`, port 587, STARTTLS |
| Sign-in | **Account password** |

GMX has to be told that a mail program may fetch mail: in the GMX web
mailer open *E-Mail → Einstellungen → POP3/IMAP Abruf* and enable
*POP3 und IMAP Zugriff erlauben*. Then the normal account password works.
(If you use GMX's two-factor authentication, generate an *Anwendungspasswort*
under *Sicherheitsoptionen* and use that instead.)

### WEB.DE (`web.de`)

| | |
|---|---|
| Incoming | `imap.web.de`, port 993, TLS |
| Outgoing | `smtp.web.de`, port 587, STARTTLS |
| Sign-in | **Account password** |

Same as GMX (same operator): enable *POP3/IMAP* under *E-Mail →
Einstellungen → POP3/IMAP Abruf* first; with two-factor authentication on,
use an application password from the security settings.

### mailbox.org (`mailbox.org`)

| | |
|---|---|
| Incoming | `imap.mailbox.org`, port 993, TLS |
| Outgoing | `smtp.mailbox.org`, port 465, TLS |
| Sign-in | **Account password** |

The normal password works. If you have enabled two-factor authentication,
mailbox.org lets you set a separate password for mail programs under
*Settings → Security*; use that one in UltraMail.

### Posteo (`posteo.de`, `posteo.net`)

| | |
|---|---|
| Incoming | `posteo.de`, port 993, TLS |
| Outgoing | `posteo.de`, port 465, TLS |
| Sign-in | **Account password** |

The normal password works. Posteo's optional two-factor authentication
protects the webmail sign-in only; mail programs keep using the account
password.

## 3. The sign-in service and OAuth clients — for people building or packaging UltraMail

**If you are just using UltraMail, skip this section.** The browser sign-in
for Gmail, Outlook and Yahoo works with nothing to configure: UltraMail signs
in through the **ULTRA OS sign-in service** at
`https://oauth-callback.ultraos.eu`. This section is for whoever builds or
packages UltraMail, or runs a service of their own.

The browser sign-in runs as an OAuth *client* that the provider must know - a
client id and, for Google and Yahoo, a client secret. UltraMail ships
**neither**. The registrations live on the sign-in service
([`Server/OAuthCallback`](../../Server/OAuthCallback/README.md) in the
repository, a small FastAPI application behind nginx), which is also the
public https address the providers redirect the browser to:

1. UltraMail makes a PKCE verifier and asks the service to start a sign-in,
   sending only the PKCE challenge and your address; the service answers
   with the provider's consent URL, which UltraMail opens in your browser.
2. You sign in with the provider; the provider sends the browser to
   `https://oauth-callback.ultraos.eu/callback/<provider>`, which shows
   *Signed in* and keeps the authorization code for UltraMail.
3. UltraMail collects the code and exchanges it - through the service, which
   adds the client id and the secret - for the access and refresh tokens.
   Both go into the credential vault. The refresh, later, goes the same way.

PKCE stays end to end: the verifier never leaves UltraMail, so the code is
useless to anyone else - the browser, the service, a log. The service keeps
nothing: a sign-in in flight lives in its memory for ten minutes at most,
tokens pass through and are never stored. Yahoo, which refuses loopback
redirect URIs, redirects to the service like every other provider, so its
paste-the-code dialog is only needed by an installation that signs in to
Yahoo with a client of its own (below).

### Where the service is

The address is built into the framework's UltraNet module, one setting for
every application and module in a process - `ULTRANET_OAUTH_SERVICE_URL` in
[`UltraCanvas/CMakeLists.txt`](../../UltraCanvas/CMakeLists.txt), default
`https://oauth-callback.ultraos.eu`; a gitignored `cmake/oauth.local.cmake`
(see [`cmake/oauth.local.cmake.example`](../../cmake/oauth.local.cmake.example))
may set it for a private build, and `-DULTRANET_OAUTH_SERVICE_URL=""` builds
without one. It is the **lowest-priority** source, so a user or admin can
point an existing build elsewhere without a rebuild, in decreasing priority
(`UltraNet/UltraNetOAuth2Apps.h`):

- `OAuthApps::SetServiceUrl()` in code (`UltraNet_OAuth2SetSignInService`);
- `ULTRAMAIL_OAUTH_SERVICE_URL` or `ULTRANET_OAUTH_SERVICE_URL` in the
  environment;
- `[service]` / `url = …` in `oauth.ini` in the data folder (the table
  below);
- the built-in address.

The value `off` (or `none`) at any of these turns the service off. The
wizard's hint says when a provider cannot be signed in to at all. The same
setting serves the composer's *Cloud link…* sign-ins: Google Drive,
OneDrive and Dropbox go through the service too (UltraCloud, under the
service's `google`, `microsoft` and `dropbox`), unless a client of their own
is configured.

### Signing in with an OAuth client of your own

An installation that would rather not go through the service - a private
registration, a build without a service, a developer testing a provider -
configures a client per provider at run time. A provider with a client of its
own signs in **directly**, as earlier versions did (UltraNet's OAuth2 client:
authorization code + PKCE, the redirect caught on an ephemeral loopback port,
or Yahoo's pasted code), and bypasses the service for that provider only. In
decreasing priority: `OAuthApps::Set()` (in code) > the environment >
`oauth.ini` in the data folder:

| Platform | Data folder |
|---|---|
| Linux | `~/.local/share/UltraMail/` (or `$XDG_DATA_HOME/UltraMail/`) |
| Windows | `%APPDATA%\UltraMail\` |
| macOS | `~/Library/Application Support/UltraMail/` |

`$XDG_DATA_HOME`, when set, wins on every platform (`Apps/UltraMail/engine/UltraMailPaths.h`).

```ini
# Another sign-in service (a private deployment of Server/OAuthCallback),
# or "off" to use clients of your own only.
[service]
url = https://oauth-callback.example.org

# A client of your own: this provider then signs in directly, not through
# the service. A desktop client's credentials are not confidential - the
# flow's safety is PKCE + the loopback redirect + your consent.
[google]
client_id     = 1234567890-abc.apps.googleusercontent.com
client_secret = GOCSPX-…

[microsoft]
client_id     = 00000000-1111-2222-3333-444444444444

[yahoo]
client_id     = dj0yJmk9…
```

The environment works too: `ULTRAMAIL_GOOGLE_CLIENT_ID`,
`ULTRAMAIL_GOOGLE_CLIENT_SECRET`, `ULTRAMAIL_MICROSOFT_CLIENT_ID`,
`ULTRAMAIL_YAHOO_CLIENT_ID` (an optional `…_REDIRECT_URI` overrides the
provider's default), as do the shared names `ULTRANET_OAUTH_GOOGLE_CLIENT_ID` /
`ULTRANET_OAUTH_MICROSOFT_CLIENT_ID`. When neither the service nor a client is
configured, the wizard says so.

The lookup is the framework's — UltraNet's OAuth2 app registry
(`UltraNet/UltraNetOAuth2Apps.h`), one per process — and UltraMail's
`OAuthApps` is its profile. The same registry serves UltraCloud's "Attach
cloud link…" sign-ins: a Google client configured for Gmail by any of the
means above is also the client Google Drive signs in with, and the Microsoft
one serves OneDrive, as long as the registration's consent screen carries
those scopes as well.

Registering a client for the direct flow:

- **Google:** [Google Cloud console](https://console.cloud.google.com/) →
  *APIs & Services → Credentials → Create credentials → OAuth client ID*,
  application type **Desktop app**. No redirect URI needs registering:
  desktop clients accept any loopback port, and UltraMail uses
  `http://127.0.0.1:<port>/callback`.
- **Microsoft:** [Microsoft Entra admin center](https://entra.microsoft.com/)
  → *App registrations → New registration*, supported account types
  *Accounts in any organizational directory and personal Microsoft accounts*;
  under *Authentication* the platform **Mobile and desktop applications**
  with the redirect URI `http://127.0.0.1` and **Allow public client flows**
  on (no secret); under *API permissions* the delegated
  `IMAP.AccessAsUser.All` and `SMTP.Send` from *Office 365 Exchange Online*,
  plus `offline_access`. UltraMail redirects to `http://127.0.0.1:<port>/`;
  Microsoft ignores the port of a loopback URI.
- **Yahoo:** [Yahoo developer console](https://developer.yahoo.com/apps/) →
  create an app with the **Mail** API permission; the *Client ID (Consumer
  Key)* only. Yahoo forces https redirect URIs, which a loopback listener
  cannot serve, so UltraMail uses the out-of-band flow with such a client:
  `redirect_uri=oob`, and the consent page shows a code the user pastes in.
  A Yahoo app registered Thunderbird's way, with the redirect
  `https://127.0.0.1`, works too with `redirect_uri = https://127.0.0.1` in
  `oauth.ini`: the user pastes the address the browser lands on.

### Registering the clients for the service

How the service is deployed, and how each provider's client is registered
with the redirect URI `https://oauth-callback.ultraos.eu/callback/<provider>`,
is in the service's own [README](../../Server/OAuthCallback/README.md). In
short: Google needs a **Web application** client with that redirect URI and
`ultraos.eu` among the consent screen's authorised domains; Microsoft a
**Web** platform with that redirect URI and a client secret; Yahoo an app
with that redirect URI and the **Mail** permission.

Yahoo does not offer the Mail permission in its developer console any more:
apply for commercial IMAP/SMTP access with the Developer Access form at
<https://senders.yahooinc.com/developer/developer-access/> (questions:
mail-api@yahooinc.com). Until it is granted, Yahoo issues a token that IMAP
and SMTP reject, and Yahoo users sign in with an app password.

### ⚠️ Google verification is required before Gmail works for the public

Registering a Google client is **not enough** to let arbitrary users sign in
to Gmail. The `https://mail.google.com/` scope is a Google *restricted*
scope. While the OAuth consent screen is in **Testing** it serves only the
addresses added under *Test users* (max 100), and Google revokes those
users' refresh tokens after **7 days** (the "*The sign-in has expired*" row
in [section 6](#6-when-it-does-not-work)). To lift both limits the consent
screen must be **published** and pass Google's **security assessment
(CASA)** — an annual, paid review through a Google-approved assessor. Until
then, Gmail browser sign-in is usable for testing only; the app-password
path (section 2) has no such limit. Microsoft has no equivalent
restricted-scope review — publisher verification only removes the
"unverified app" banner.

### What happens during the sign-in

UltraMail generates a one-time PKCE challenge, asks the sign-in service to
start the sign-in, opens the provider's consent page in the system browser
(never in an embedded view) with your address as the login hint, waits for
the provider's redirect to arrive at the service, collects the code from it
and exchanges the code - through the service - for an access token and a
refresh token. Both go into the credential vault. With a client of its own
(above), UltraMail listens on an ephemeral loopback port for the redirect
instead and exchanges the code itself; Yahoo cannot redirect to a loopback
port, so with a Yahoo client of its own UltraMail shows a dialog and you
paste the code Yahoo displays after consent. Each IMAP or SMTP session then
authenticates with XOAUTH2; an expired access token is refreshed through the
provider - via the service, or directly - before the session starts.
Cancelling the "Sign in with …" dialog abandons the attempt; add the account
again to retry.

## 4. On the machine

- **Primary Password.** Optional (*Settings > Privacy > Primary Password*:
  set, change, remove). Once set, UltraMail asks for it when it starts, before
  it reads anything stored - the vault, and the key the stored mail is
  encrypted with, open only with it; a wrong one is asked for again, Cancel
  ends the start. It is not stored anywhere: forgotten, the mail kept on the
  computer is downloaded again and the account passwords entered again.
  Without one the vault opens with a key kept beside it (`vault/device.key`),
  so UltraMail never asks.
- **The data folder is private.** UltraMail makes it owner-only at every start
  (on Linux and macOS; Windows' `%APPDATA%` already is).
- **The stored mail is encrypted.** `mail.db`, `contacts.db`, `outbox.db`,
  `cloud.db` and every downloaded message under `mail/` are ciphertext on
  disk, encrypted with a key kept in the vault: copied elsewhere, or opened
  with another program (the sqlite3 shell, UltraDBAdmin), they show nothing.
  A mailbox from an earlier version is converted at the first start - the
  databases before the window opens, the messages in the background. With a
  Primary Password nobody without it can read them; without one, programs
  running as you can open the vault the way UltraMail does. Attachments you
  open are handed to their viewer as plain copies in `cache/attachments/`,
  which are deleted at the next start.
- **Signed and encrypted mail: S/MIME and OpenPGP.** A message that came
  signed or encrypted is shown as it was meant to be read: decrypted where a
  key here opens it, with pills that say what was checked - *Encrypted*, and
  *S/MIME* or *OpenPGP* in green when the signature holds and the signer is
  vouched for (a certificate from an authority this computer trusts, a key
  your keyring trusts), amber when it holds but nobody vouches for the signer,
  red when the message was changed after it was signed. On disk it stays
  exactly as it arrived - encrypted mail is decrypted only for the reading
  pane, never written down decrypted.
  - **S/MIME.** Import your certificate and its private key in *Account
    Settings > S/MIME > Import…* from the `.p12` / `.pfx` file your
    certificate authority gave you (its password is asked for). It is kept in
    the vault, and only a certificate for the account's own address is
    accepted. Your correspondents' certificates are learnt from the signed
    mail they send you - only from a signature that holds, is vouched for and
    is from the From address.
  - **OpenPGP.** UltraMail uses your own GnuPG keyring and `gpg-agent`, which
    asks for a key's passphrase itself; keys are found by the address in
    their user id. A key that arrives as an attachment (`application/pgp-keys`)
    is offered for import when you open it.
  - **Sending.** The compose window's *Security* row: *Off*, or *S/MIME* /
    *OpenPGP* (each offered when this build has it, and S/MIME once the
    account has a certificate), with *Sign* and *Encrypt*. An encrypted
    message is encrypted to every recipient and to you, so the copies in
    Drafts and Sent are the same ciphertext and you can still read them;
    without a certificate or key for a recipient UltraMail says so and does
    not send. A message that cannot be secured is never sent in plain text
    instead. Replies and forwards of encrypted mail start encrypted.
  - S/MIME needs OpenSSL 3 at build time and OpenPGP GPGME (with GnuPG
    installed to use it); a build without one shows such mail as one it
    cannot check.
  - **OpenPGP on macOS**: the app carries GPGME, not GnuPG. Install it with
    Homebrew - `brew install gnupg pinentry-mac`, then put
    `pinentry-program /opt/homebrew/bin/pinentry-mac` (`/usr/local/bin/...` on
    Intel) in `~/.gnupg/gpg-agent.conf` and run `gpgconf --kill gpg-agent`, so
    the passphrase is asked for in a window rather than a terminal - or with
    GPG Suite, which sets that up itself. UltraMail finds either without
    changing `PATH`.
- **Data folder.** `mail.db` (accounts, folders, message index), `mail/`
  (cached message bodies), `contacts.db`, `outbox.db`, `vault/ultramail.vault`
  (the encrypted vault) and `oauth.ini` live in the data folder named above.
- **IMAP / JMAP / SMTP plug-ins.** Mail is fetched through UltraNet's
  `ultranet_imap` plug-in - or `ultranet_jmap` for an account whose incoming
  server is set to JMAP - and sent through `ultranet_smtp`. UltraMail looks
  for them in `Plugins/UltraNet` next to the executable (or up to two levels
  above it, then in the working directory); `ULTRAMAIL_PLUGIN_DIR` overrides.
  If the one an account needs is missing, Update and the first sync of a new
  account say so and name the folder that was searched.
- **Changing the sign-in of an account.** Add the account again with the
  same address: the existing entry is updated, and whatever you provide this
  time (a password, an app password, or the browser sign-in) replaces what
  the vault held. An account has exactly one sign-in method.

## 5. Other providers: autoconfig and manual settings

Addresses outside the table — a company domain, a hosting provider, a
university — go through two more steps.

**Autoconfig lookup.** UltraMail fetches, in this order, the domain's own
`https://autoconfig.<domain>/mail/config-v1.1.xml`, its
`https://<domain>/.well-known/autoconfig/mail/config-v1.1.xml`, and the
Thunderbird provider database (`autoconfig.thunderbird.net`). All three
requests name the domain only - the address itself is never sent; a
per-user value in the published settings is a placeholder (`%EMAILADDRESS%`)
that UltraMail fills in on the machine. Most hosting
providers and many organisations publish one of these; the first hit gives
the servers, ports, security and the username pattern, and the wizard
reports "settings found for <domain>". The lookup runs off the UI thread
behind a wait dialog and takes a few seconds at most.

**Manual settings page.** When nothing is published, the page opens with
a *Protocol* row (IMAP or JMAP, see below), *Incoming (IMAP)* and
*Outgoing (SMTP)* rows — host, port, security
(SSL/TLS, STARTTLS, None) — and the *Username*, prefilled with
`imap.<domain>` 993 SSL/TLS, `smtp.<domain>` 587 STARTTLS and the full
address. Correct them from your provider's "mail program settings" or
"IMAP/SMTP" help page and **Save**. Save first **checks the sign-in**: UltraMail opens
one session to the incoming server (IMAP, or JMAP's session resource) with
the entered servers and the
password (or the stored sign-in of an existing account), which proves the
host, the port, the security mode and the credentials in one go. While it
runs the page says "Checking the sign-in at …"; on success the page closes,
the account is added with those servers, the password goes into the vault,
and the first sync starts. A failed check shows the reason in place (wrong
password, host not found, connection refused, certificate problem) and
offers **Save anyway** for a server that is down right now. The outgoing
(SMTP) server is not checked — the plug-in has no sign-in-only operation —
so a wrong SMTP entry shows up on the first send. The page also validates
in place (a host must be given, ports are 1–65535).

**JMAP instead of IMAP.** The *Protocol* row at the top of the page chooses
what the incoming server speaks: **IMAP** (every account from before the
choice existed, and most providers) or **JMAP**, the JSON-over-HTTPS mail
protocol (RFC 8620/8621) of Fastmail, Stalwart, Cyrus and Apache James.
Choosing JMAP renames the incoming rows to *Incoming (JMAP)* and *Sign-in
(JMAP)*, sets the port to **443** (a port that was IMAP's usual 993 or 143;
an unusual one you typed stays), fixes the security to SSL/TLS - a JMAP
server is HTTPS - and leaves *Automatic*, *Normal password* and *OAuth2* as
the sign-in methods, the ones JMAP has. Enter the server as its host name
only - `api.fastmail.com`, or the mail host of a Stalwart or Cyrus server;
UltraMail asks it for the JMAP session at `/.well-known/jmap`. The sign-in
check, every later sync, marking read, moving, deleting (to the Trash
mailbox) and the copies in Drafts and Sent then go through the JMAP plug-in.
The *Outgoing (SMTP)* server is **optional** for a JMAP account: left
empty, mail is sent through the JMAP server too (JMAP submission, as the
account's own identity; the server files the sent message in Sent); an
SMTP server entered next to it is used to send instead. Switching an
existing account between the protocols drops the mail held for it and
fetches it anew: the same mailbox read over the other protocol numbers its
messages differently. Switching a password account also asks for its
password again: the page warns as soon as the other protocol is chosen, the
password field turns *Required*, and *Save* (and *Save anyway*) wait for it -
the stored password belonged to the old server, and a JMAP server often wants
its own app password or API token. An account signed in with OAuth2 keeps its
tokens and is not asked. The choice is stored with the other server settings
of the account and shown again on its Settings page.

**JMAP is found by itself.** Before the autoconfig documents (which only
describe IMAP), the lookup asks the domain for its JMAP server (RFC 8620
§2.2): the DNS SRV record `_jmap._tcp.<domain>` names it, else the domain
itself is asked at `https://<domain>/.well-known/jmap`; a server that
answers with its session resource, or asks for a sign-in first (401, as
Fastmail and Stalwart do), is taken - as the host the answer came from,
after redirects (`fastmail.com` leads to `api.fastmail.com`). The wizard
then reports *a JMAP server was found for <domain>*, and the account is
added with that JMAP server as its incoming server, the full address as
its username and no SMTP server: it sends through JMAP. The provider table
comes first, so Gmail, Outlook and the other known providers are never
probed.

**New mail arrives at once.** A JMAP account is watched: UltraMail holds
the server's event source open and checks the account the moment the
server reports a change to its mail or folders, so new mail shows without
waiting for the interval in *Settings > Mail > Connection* - which still
runs, as the safety net for a change the stream missed. The watch opens
once the account's sign-in is usable, is reopened a minute after the
server or the network ended it, and `trace.log` in the data folder says
when it opened, ended and fired.

**Authentication method.** Under the server rows, *Sign-in (IMAP)* and
*Sign-in (SMTP)* choose how each server is signed in to — the same choice
Thunderbird calls the authentication method:

| Choice | What UltraMail sends |
|---|---|
| Automatic | Whatever the server offers, strongest first (the default, and what every account created before this setting keeps) |
| Normal password | IMAP `LOGIN`; SMTP `AUTH PLAIN` / `AUTH LOGIN` |
| Encrypted password | `AUTH CRAM-MD5` / `DIGEST-MD5` (POP3: `APOP`); the password never crosses the wire |
| OAuth2 | `AUTH XOAUTH2` / `OAUTHBEARER` with the browser sign-in's token |
| Kerberos / GSSAPI | `AUTH GSSAPI` with the system's Kerberos ticket |
| NTLM | `AUTH NTLM` |
| No authentication | Nothing (outgoing server only: a relay that trusts your network) |

Gmail, Outlook/Hotmail/Live and Yahoo are set to **OAuth2** by the provider
table; an autoconfig document sets the method it lists first (OAuth2 only
when nothing else is listed, since the browser sign-in exists for those three
providers alone); everything else starts on **Automatic**. Pick a specific
method when Automatic tries one the account cannot use — a server that
advertises Kerberos or NTLM next to the password, for example. OAuth2 needs
the browser sign-in: set on an account that signs in with a password, the
check fails with "OAuth2 is set as the authentication method, but there is no
OAuth2 sign-in for this account".

The settings are **stored on the account**, so later syncs and sends never
look them up again. The page also opens by itself when Update finds an
account without known servers — for example one that was added before
this version for a domain outside the table — and adding the same address
again keeps the servers it already has.

Sign-in for these accounts is the account password, unless the provider
says otherwise in its help page (some require an app password when
two-factor authentication is on). The browser sign-in is available only for
Gmail, Outlook and Yahoo.

## 6. When it does not work

| UltraMail says | What to do |
|---|---|
| *New mail could not be fetched … authentication failed* | iCloud: you typed the account password; use an app-specific password (section 2). Gmail, Outlook and Yahoo take no password: open the account's Settings and use *Sign in with …*. |
| *The sign-in service is turned off in this build and no OAuth client is configured* | The build was configured with `-DULTRAMAIL_OAUTH_SERVICE_URL=""`, or `off` was set. Name a service (`[service] url =` in `oauth.ini`, or `ULTRAMAIL_OAUTH_SERVICE_URL`) or a client of your own (section 3). |
| *The sign-in service did not answer … The sign-in service at … could not be reached* (and the browser never opened) | `oauth-callback.ultraos.eu` could not be reached from this computer: the name does not resolve, a proxy blocks it, or the service is down. Open `https://oauth-callback.ultraos.eu/healthz` in a browser; `trace.log` in the data folder says which way the sign-in went (*through the sign-in service …* or *directly, with this installation's own client*). An installation behind a firewall can run its own service (section 3). |
| *The IMAP plug-in was not found* / *The JMAP plug-in was not found* | Build the UltraNet IMAP (or JMAP) plug-in and keep it in `Plugins/UltraNet` next to the executable, or set `ULTRAMAIL_PLUGIN_DIR`. An account whose *Protocol* is JMAP needs `ultranet_jmap`, every other one `ultranet_imap`. |
| *The JMAP plug-in is not loaded, so UltraMail cannot reach a mail server* on Send | The account sends through its JMAP server (no SMTP server is entered) and `ultranet_jmap` is missing: see the row above, or enter an outgoing (SMTP) server on the account's Settings page. |
| *The server refused the submission: forbiddenFrom* on Send from a JMAP account | The JMAP server has no sending identity for the From address. Use the address the account signs in with, or add the identity (an alias) at the provider. |
| *Looking up server settings* takes long, or finds nothing | The domain publishes no autoconfig document; Cancel opens the manual page, or wait for it to open by itself. Enter the servers from the provider's help page (section 5). |
| *Server settings for …* opens on Update | The account has no known servers (added before they were stored, or for a domain outside the table). Enter them once; they are kept. |
| *The sign-in at … did not succeed* on the settings page | The host, port, security or password is wrong, or the server is unreachable. Correct the page and Save again; *Save anyway* keeps the entry for a server that is only down right now. |
| The pill at the right of the status line says *Offline* or *Failed* | The selected account's last contact with its mail server did not succeed. Hover the pill: it names the server, when it was last reached, the reason, and what to do. *Offline* (amber) means the server could not be reached at all — usually the network; *Failed* (red) means it answered but refused, usually the sign-in. |
| Any failure | The alert shows **Connection details** under the reason (on the settings page: the **Details** button): UltraMail and UltraNet versions, server and address, TLS mode, sign-in method, libcurl and TLS library versions, trusted roots, system. Copy that block into a bug report. |
| *… security certificate is not valid …* with a reason in brackets | The reason is the TLS library's own: *unable to get local issuer certificate* (the server sends an incomplete chain, or a program such as a virus scanner intercepts the connection with its own certificate), *certificate has expired*, *self-signed certificate*, or *no alternative certificate subject name matches* (the certificate is for another host name — use the host it names). On Windows the reasons start with *schannel:* — *the certificate or certificate chain is based on an untrusted root* means Windows itself does not trust the server's root (a self-signed or company certificate: install its root in Windows' *Trusted Root Certification Authorities*); builds from before 2026-09-28 also said it for Let's Encrypt servers, because they checked against the bundled `cacert.pem` instead of Windows' own certificate store. |
| *… sign-in was rejected* with the right password | The server may not accept the method *Automatic* picked. Set *Sign-in (IMAP)* / *Sign-in (SMTP)* to *Normal password* (or what the provider's help page names) and Save again. |
| *New mail could not be fetched … host not found / connection refused* | A server name or port on the settings page is wrong. Add the account again with the same address and correct the page. A background sync, or opening a folder, raises this only after the account has been unreachable for ten minutes, so a computer that has just started, or a laptop between two networks, does not: the status line shows the reason meanwhile and the account is retried every minute. Update reports it at once. |
| No alert right after the computer was switched on, though the pill says *Offline* or *Failed* | When UltraMail starts, or the computer wakes, more than ten minutes after it last reached a mail server, the computer was most likely switched off and its network may not be up yet. For two minutes then (*Settings > Mail > Connection*: not at all to 10 minutes) a background check that fails raises no alert, whatever the reason; the account is checked again when the time is up, and an alert follows if it still fails. Update reports at once. |
| *The sign-in has expired; sign in again* | The refresh token was revoked or expired (Google revokes the tokens of an app in *Testing* after seven days), or it was issued to a different OAuth client than the one this installation now signs in as - an account signed in with a build before the sign-in service cannot be refreshed through it. Add the account again, or use *Sign in with …* on its settings page, to sign in anew. |
| *Signed in, but the mail session is refused* (Microsoft 365) | Ask the tenant administrator to enable IMAP and Authenticated SMTP for the mailbox. |
| *Your mail account passwords are locked* | Enter the Primary Password — Update, sending, or adding an account asks for it. |
| *UltraMail is locked* when it starts | A Primary Password is set (*Settings > Privacy > Primary Password*). Enter it; Cancel ends the start. |
