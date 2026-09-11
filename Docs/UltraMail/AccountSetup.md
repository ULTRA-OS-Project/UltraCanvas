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
4. The first time, UltraMail asks you to **choose a master password**. It
   encrypts your account passwords and sign-ins on disk and is never stored
   itself — if you forget it, you enter your account passwords again.
5. For Gmail and Outlook the browser opens the provider's sign-in page; come
   back to UltraMail when it says you are done.
6. The inbox is fetched right away. Later, every account is synced every
   five minutes, and **Reload** syncs all of them now.

The three ways in:

| Sign-in | What you type in the wizard | Providers |
|---|---|---|
| **Browser sign-in (OAuth2)** | Password left **empty** | Gmail, Outlook / Microsoft 365 |
| **App password** | A password generated in the provider's security settings | Yahoo, iCloud — and Gmail if you prefer it over the browser sign-in |
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
| Sign-in | **Browser sign-in with Google** (leave the password empty), or an **app password** |

Google rejects the normal account password in mail programs.

- **Browser sign-in:** leave the password field empty. UltraMail opens
  Google's consent page in your browser with your address preselected; allow
  UltraMail to read and send your mail. This needs a Google OAuth client
  configured on the machine — see [section 3](#3-oauth-clients-for-the-browser-sign-in).
  Until one is configured the wizard asks for an app password instead.
- **App password:** in your Google Account open *Security → 2-Step
  Verification* (it must be on), then *App passwords*
  (`myaccount.google.com/apppasswords`). Create one for "Mail", copy the
  16-character password into the wizard's password field.

### Outlook.com, Hotmail, Live, Microsoft 365 (`outlook.com`, `hotmail.com`, `live.com`, `msn.com`, `office365.com`)

| | |
|---|---|
| Incoming | `outlook.office365.com`, port 993, TLS |
| Outgoing | `smtp.office365.com`, port 587, STARTTLS |
| Sign-in | **Browser sign-in with Microsoft only** (leave the password empty) |

Microsoft has retired password sign-in ("basic authentication") for IMAP and
SMTP on personal Outlook.com accounts and in Microsoft 365, and app passwords
are gone with it. Leave the password field empty; UltraMail opens Microsoft's
sign-in page in your browser. This needs a Microsoft OAuth client configured
on the machine — see [section 3](#3-oauth-clients-for-the-browser-sign-in).

For a **Microsoft 365 work or school** mailbox the tenant administrator must
leave **IMAP** and **Authenticated SMTP** enabled for the mailbox (Exchange
admin center → the mailbox → *Email apps*); otherwise the sign-in succeeds
but the mail session is refused. Addresses on a company's own domain are not
in UltraMail's provider table yet (see [section 5](#5-other-providers)).

### Yahoo Mail (`yahoo.com`, `yahoo.de`, `ymail.com`)

| | |
|---|---|
| Incoming | `imap.mail.yahoo.com`, port 993, TLS |
| Outgoing | `smtp.mail.yahoo.com`, port 465, TLS |
| Sign-in | **App password** |

Yahoo rejects the normal account password in mail programs and offers no
browser sign-in to third-party desktop apps. In Yahoo, open *Account Info →
Account Security → Generate and manage app passwords* (the entry is
sometimes called *Generate app password*), create one for "Other app" /
UltraMail, and type the generated password into the wizard.

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

## 3. OAuth clients for the browser sign-in

The browser sign-in for Gmail and Outlook runs as an OAuth *client* that the
provider must know. Register one per provider once — on whatever machine or
deployment ships UltraMail — and give it to UltraMail in `oauth.ini` in the
data folder:

| Platform | Data folder |
|---|---|
| Linux | `~/.local/share/UltraMail/` (or `$XDG_DATA_HOME/UltraMail/`) |
| Windows | `%APPDATA%\UltraMail\` |

```ini
[google]
client_id     = 1234567890-abc.apps.googleusercontent.com
client_secret = GOCSPX-…

[microsoft]
client_id     = 00000000-1111-2222-3333-444444444444
```

The environment works too: `ULTRAMAIL_GOOGLE_CLIENT_ID`,
`ULTRAMAIL_GOOGLE_CLIENT_SECRET`, `ULTRAMAIL_MICROSOFT_CLIENT_ID` (an
optional `…_REDIRECT_URI` overrides the provider's default). Until a client
is configured the wizard says so.

### Google

1. In the [Google Cloud console](https://console.cloud.google.com/) create a
   project and open *APIs & Services → OAuth consent screen*. Choose
   *External*. While the app stays in *Testing*, add every address that will
   sign in under *Test users* — the `https://mail.google.com/` scope is
   restricted, so an unverified app serves only its test users.
2. *APIs & Services → Credentials → Create credentials → OAuth client ID*,
   application type **Desktop app**. Copy the client id and the client
   secret into `oauth.ini`.
3. No redirect URI needs registering: desktop clients accept any loopback
   port. UltraMail uses `http://127.0.0.1:<port>/callback`.

### Microsoft

1. In the [Microsoft Entra admin center](https://entra.microsoft.com/) open
   *Identity → Applications → App registrations → New registration*.
   Supported account types: **Accounts in any organizational directory and
   personal Microsoft accounts** (covers outlook.com / hotmail.com as well as
   Microsoft 365 tenants).
2. Under *Authentication* add the platform **Mobile and desktop
   applications** with the redirect URI `http://127.0.0.1`, and switch
   **Allow public client flows** on. No client secret is needed.
3. Under *API permissions* add the *delegated* permissions
   `IMAP.AccessAsUser.All` and `SMTP.Send` from *Office 365 Exchange Online*
   (found under *APIs my organization uses*), plus `offline_access`.
4. Copy the *Application (client) ID* into `oauth.ini`. UltraMail redirects
   to `http://127.0.0.1:<port>/`; Microsoft ignores the port of a loopback
   URI.

### What happens during the sign-in

UltraMail generates a one-time PKCE challenge, opens the provider's consent
page in the system browser (never in an embedded view) with your address as
the login hint, listens on an ephemeral loopback port for the redirect, and
exchanges the code for an access token and a refresh token. Both go into the
credential vault. Each IMAP or SMTP session then authenticates with XOAUTH2;
an expired access token is refreshed through the provider before the session
starts. Cancelling the "Sign in with …" dialog abandons the attempt; add the
account again to retry.

## 4. On the machine

- **Master password.** Asked for once per session, the first time a stored
  password or sign-in is needed (adding an account, Reload, sending). Nothing
  syncs in the background while the vault is still locked; UltraMail says so
  once, and Reload asks for the password.
- **Data folder.** `mail.db` (accounts, folders, message index), `mail/`
  (cached message bodies), `contacts.db`, `outbox.db`, `vault/ultramail.vault`
  (the encrypted vault) and `oauth.ini` live in the data folder named above.
- **IMAP / SMTP plug-ins.** Mail is fetched and sent through UltraNet's
  `ultranet_imap` and `ultranet_smtp` plug-ins. UltraMail looks for them in
  `Plugins/UltraNet` next to the executable (or up to two levels above it,
  then in the working directory); `ULTRAMAIL_PLUGIN_DIR` overrides. If they
  are missing, Reload and the first sync of a new account say so and name the
  folder that was searched.
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
Thunderbird provider database (`autoconfig.thunderbird.net`). Most hosting
providers and many organisations publish one of these; the first hit gives
the servers, ports, security and the username pattern, and the wizard
reports "settings found for <domain>". The lookup runs off the UI thread
behind a wait dialog and takes a few seconds at most.

**Manual settings page.** When nothing is published, the page opens with
*Incoming (IMAP)* and *Outgoing (SMTP)* rows — host, port, security
(SSL/TLS, STARTTLS, None) — and the *Username*, prefilled with
`imap.<domain>` 993 SSL/TLS, `smtp.<domain>` 587 STARTTLS and the full
address. Correct them from your provider's "mail program settings" or
"IMAP/SMTP" help page and **Save**. Save first **checks the sign-in**: UltraMail opens
one IMAP session to the incoming server with the entered servers and the
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

The settings are **stored on the account**, so later syncs and sends never
look them up again. The page also opens by itself when Reload finds an
account without known servers — for example one that was added before
this version for a domain outside the table — and adding the same address
again keeps the servers it already has.

Sign-in for these accounts is the account password, unless the provider
says otherwise in its help page (some require an app password when
two-factor authentication is on). The browser sign-in is available only for
Gmail and Outlook.

## 6. When it does not work

| UltraMail says | What to do |
|---|---|
| *New mail could not be fetched … authentication failed* | Gmail / Yahoo / iCloud: you typed the account password; use an app password (section 2). Outlook: passwords are not accepted; add the account again with the password empty. |
| *No OAuth client is configured for Google / Microsoft* | Register a client and put it into `oauth.ini` (section 3), or — Gmail only — use an app password. |
| *The IMAP plug-in was not found* | Build the UltraNet IMAP plug-in and keep it in `Plugins/UltraNet` next to the executable, or set `ULTRAMAIL_PLUGIN_DIR`. |
| *Looking up server settings* takes long, or finds nothing | The domain publishes no autoconfig document; Cancel opens the manual page, or wait for it to open by itself. Enter the servers from the provider's help page (section 5). |
| *Server settings for …* opens on Reload | The account has no known servers (added before they were stored, or for a domain outside the table). Enter them once; they are kept. |
| *The sign-in at … did not succeed* on the settings page | The host, port, security or password is wrong, or the server is unreachable. Correct the page and Save again; *Save anyway* keeps the entry for a server that is only down right now. |
| *New mail could not be fetched … host not found / connection refused* | A server name or port on the settings page is wrong. Add the account again with the same address and correct the page. |
| *The sign-in has expired; sign in again* | The refresh token was revoked or expired (Google revokes the tokens of an app in *Testing* after seven days). Add the account again to sign in anew. |
| *Signed in, but the mail session is refused* (Microsoft 365) | Ask the tenant administrator to enable IMAP and Authenticated SMTP for the mailbox. |
| *Your mail account passwords are locked* | Enter the master password — Reload, sending, or adding an account asks for it. |
