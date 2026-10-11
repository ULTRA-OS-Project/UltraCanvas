# Privacy declaration — UltraMail

**Version 1.0 (draft for legal review) · 10 October 2026**

This declaration explains what personal data the **UltraMail** application
processes, where that processing happens, which third parties it contacts and
why, and what rights you have under the General Data Protection Regulation
(GDPR). It is published at www.ultraos.eu and covers UltraMail on Linux,
Windows, macOS and ULTRA OS. It does not cover the website itself, other
ULTRA OS applications, or any ULTRA OS online service; each of those has a
declaration of its own.

> **Review status.** This text was drafted from UltraMail's source code and
> documentation. The items in square brackets (`[…]`) must be filled in, and
> the whole text must be reviewed by a lawyer before it is published.

## 1. Who is responsible

ULTRA OS Development GmbH
An den Klippen 23
57462 Olpe
Germany

E-mail: [privacy@ultraos.eu]
Managing director(s): […]
Commercial register: […]

The company has not appointed a data protection officer, because it is not
required to [confirm with legal review].

## 2. The short version

- UltraMail is a mail program that runs **on your own computer**. Your
  accounts, passwords, messages, attachments and contacts are stored on your
  device and nowhere else.
- UltraMail talks to **your e-mail provider's servers** to fetch and send your
  mail. That is what a mail program is for.
- UltraMail has **no account with us**, sends us **no usage statistics, crash
  reports or telemetry**, contains **no advertising** and performs **no update
  check** that would contact our servers. ULTRA OS Development GmbH does not
  receive, store or see any of your data through UltraMail.
- UltraMail contacts a small number of **other servers** for specific features
  (finding your provider's server settings, signing in to Gmail, Outlook or
  Yahoo, downloading sender icons, loading pictures in HTML mail, attaching a
  cloud link). Each is listed in section 5, together with the setting that
  turns it off where one exists.
- UltraMail is open-source software. Anyone can verify these statements
  against the source code.

## 3. What UltraMail stores on your device

All of the following is stored in UltraMail's data folder on your computer:
`~/.local/share/UltraMail/` on Linux and macOS (or `$XDG_DATA_HOME/UltraMail/`
when that variable is set), `%APPDATA%\UltraMail\` on Windows.

| Data | What it is | Where |
|---|---|---|
| Account settings | Your name, e-mail address, the server names, ports and encryption settings of your provider, your signature, the sync interval | `mail.db` |
| Credentials | Your account password, app password or the OAuth2 tokens obtained when you sign in through the browser | `vault/ultramail.vault`, encrypted (see 3.1) |
| Messages | The headers of the messages in your folders (the index), and the full content (text, HTML, attachments) of those UltraMail has fetched, as `.eml` files | `mail.db`, `mail/<account>/` |
| Attachments | Attachments you open are extracted into a folder; at every start those not opened for a week are removed, then the oldest until the folder is under 256 MB | `attachments/` |
| Outbox | Messages you have written and sent, until the server has accepted them; drafts | `outbox.db` |
| Contacts | Your address book: names, e-mail addresses, phone numbers, organisations and notes you enter, plus the "needs an answer" and "written to" information derived from your own mail | `contacts.db` |
| Sender icons | Icons of the services and websites that write to you (see 5.4) | `cache/sender-icons/` |
| Preferences | Your settings, including which senders may load remote pictures and which websites you trust | `preferences.ini` |
| Cloud storage accounts | The cloud storage accounts you add for "Attach cloud link" (see 5.6) | `cloud.db` |
| OAuth client registration | If you register your own OAuth client with Google or Microsoft: its client id and secret | `oauth.ini` |
| Timing trace | A log of what UltraMail is doing and how long each step takes, for performance diagnosis. It names accounts and folders and timings, not message contents. It is emptied at every start and never leaves your device. `ULTRAMAIL_TRACE=0` turns it off | `trace.log` |

### 3.1 How credentials are protected

Passwords and sign-in tokens are never written into the account settings.
They are kept in an encrypted vault file (key derived with Argon2id, content
encrypted with XChaCha20-Poly1305). The vault is unlocked either by a master
password you choose, which is never stored, or by a device key file that only
your user account on the computer can read. If you forget the master password,
no one, including us, can recover the stored passwords; you enter them again.

### 3.2 Who can read this data

Only you, and anyone who has access to your user account on the computer or to
its backups. UltraMail does not upload, synchronise or copy the data folder
anywhere. Deleting an account in UltraMail deletes its messages and
credentials; deleting the data folder removes everything UltraMail knows.

### 3.3 Our role

Because this processing happens on your device, under your control and for
your own purposes, ULTRA OS Development GmbH is **not the controller** of this
data within the meaning of Art. 4(7) GDPR. We describe it here for
transparency, and because the platforms and stores through which UltraMail is
distributed require a privacy declaration.

## 4. Data UltraMail sends to your e-mail provider

To do its job, UltraMail connects to the servers of the e-mail provider you
configure and transmits:

- your credentials (password, app password or OAuth2 token) to sign in;
- the messages you send, with their recipients, subject, text and attachments;
- requests for your folders, message headers, message bodies and flags
  (read, answered, deleted), and the changes you make to them.

**Recipient:** your e-mail provider, as its own controller. Its privacy policy
applies to what it does with your mail. **Encryption:** UltraMail uses TLS for
every connection by default and verifies the server's certificate.
**Legal basis:** this is the service you ask for when you add the account
(Art. 6(1)(b) GDPR). **Setting:** none; without it UltraMail cannot work.

## 5. Other servers UltraMail contacts, and why

### 5.1 Finding your provider's server settings (account setup)

When you add an account, UltraMail works out the server settings from your
address. For the most common providers (Gmail, Outlook, Yahoo, iCloud, GMX,
web.de, mailbox.org, Posteo and others) it uses a built-in table and contacts
nobody. For any other domain it asks, in this order:

1. `https://autoconfig.<your domain>/mail/config-v1.1.xml` and
   `https://<your domain>/.well-known/autoconfig/mail/config-v1.1.xml` —
   your own provider's servers.
2. `https://autoconfig.thunderbird.net/v1.1/<your domain>` — the Thunderbird
   provider database operated by MZLA Technologies Corporation (Mozilla).
   Mozilla's privacy policy applies to the request.

   **All of these requests carry the domain of your address only**
   (`example.com`), never the address itself. The autoconfig format allows a
   client to send the full address; UltraMail does not.
3. DNS lookups (SRV and MX records of your domain) through your system's
   resolver, and connection attempts to the usual server names and ports.

**Data transmitted:** the domain of your e-mail address and your IP
address. **Purpose:** to configure the account without asking you
for server settings. **Legal basis:** Art. 6(1)(b) and (f) GDPR; the lookup
is a one-off during setup. **Setting:** none, but you can cancel the lookup
and enter server settings by hand; the lookup is then not made.

### 5.2 Signing in with Google, Microsoft or Yahoo (OAuth2)

Gmail, Outlook / Microsoft 365 and Yahoo accounts sign in through the
provider's own consent page in your web browser (OAuth2 authorization code
with PKCE). UltraMail opens the page, you sign in with the provider, and the
provider hands UltraMail a token that it stores in the vault and uses for IMAP
and SMTP. Your provider password is never seen by UltraMail.

**Data transmitted:** the e-mail address you typed (as a sign-in hint), and
whatever you enter on the provider's page, to the provider only.
**Recipients:** Google LLC, Microsoft Corporation or Yahoo (Oath Holdings
Inc.), as their own controllers under their own privacy policies.
**Legal basis:** Art. 6(1)(b) GDPR; the provider requires this method.
**Setting:** you can use an app password instead where the provider offers
one; then no browser sign-in takes place.

### 5.3 Pictures in HTML mail ("remote images")

A message may reference pictures hosted on the sender's web server. Loading
such a picture tells that server that, and when, the message was opened, and
reveals your IP address; marketing mail uses this to track opens. UltraMail
therefore does **not load remote pictures by default** from unknown senders.
The default policy (*Settings → Privacy → Images*) is *trusted senders only*:
pictures load automatically only from people in your address book, from
senders you have allowed with "Always from …", and from websites you have
listed as trusted. For anyone else a bar in the reading pane asks each time.
You can switch the policy to *never* or to *always*; even *always* skips junk
and suspicious mail.

**Data transmitted:** your IP address and the picture's URL to the server
hosting the picture, at the moment you (or the policy) load it.
**Recipient:** the sender or its hosting provider. **Legal basis:** your
choice (Art. 6(1)(a) GDPR). **Setting:** *Settings → Privacy → Images*.

### 5.4 Sender icons

Next to each sender UltraMail shows a badge: for about 400 well-known services
(banks, shops, parcel carriers, social networks …) their icon, for everyone
else a monogram. Two optional downloads are involved:

- **Icons of known services.** The first time a known service appears in your
  inbox, UltraMail downloads its icon (favicon) once from that service's own
  website — for example `https://www.paypal.com/favicon.ico`. The service's
  web server learns your IP address and that a copy of UltraMail requested its
  icon. It does **not** learn which message you received or your e-mail
  address. The icon is then kept in the cache.
- **Website icons of other senders.** For a sender that is not a known
  service, UltraMail may read the home page of the domain it writes from to
  find the site's icon. The sender's web server learns that someone at your IP
  address looked at its home page, though not which message or who you are.
  This is done at most once a week per domain, only for mail that passed
  UltraMail's spam and phishing scan, and never for the junk folder.

Both downloads are **off by default**: a fresh installation contacts nobody
but your mail provider until you turn them on in *Settings → Privacy → Sender
icons* (*Download the icons of known senders*, *Show other senders' website
icons*), separately. With the first switch off, nothing at all is downloaded
for badges. Icons already downloaded keep being shown until you delete the
cache folder.

**Data transmitted:** your IP address and a request for the icon or home
page. **Recipients:** the operators of the respective websites.
**Legal basis:** your consent, given by turning the switch on and withdrawn
by turning it off (Art. 6(1)(a) GDPR). **Setting:** *Settings → Privacy → Sender icons*.

### 5.5 Spam and phishing scan

UltraMail scores every incoming message for spam and phishing signs (forged
sender domains, requests for credentials, failed DKIM / SPF / DMARC results
reported by your own mail server, and others) and shows a warning badge.
**This scan runs entirely on your device.** No message content, address or
link is sent to us or to any reputation service.

### 5.6 Attaching a cloud link

The composer's "Attach cloud link…" uploads a file to a cloud storage account
of yours (Nextcloud / ownCloud, a WebDAV server, Dropbox, OneDrive or Google
Drive) and inserts the share link into the message. This happens only when you
use the feature. The file and your credentials for that service go to the
service you chose, under its own privacy policy. Dropbox, OneDrive and Google
Drive sign in through the browser as described in 5.2.

### 5.7 Links you click

A link in a message opens in your web browser. From then on the browser's and
the linked site's privacy policies apply. UltraMail does not check links
against any online service.

### 5.8 Notifications and the desktop feed

On ULTRA OS, UltraMail reports new mail to the desktop's notification server
and message centre over a local interface (UltraMessage). This stays on your
device.

### 5.9 Spell checking

Spell checking in the composer uses dictionaries installed on your computer
(Hunspell or the system spell checker). Nothing you type is sent anywhere.

## 6. What we do not do

UltraMail does not:

- create or require an account with ULTRA OS Development GmbH;
- send usage statistics, analytics, crash reports or diagnostic data to us or
  anyone else;
- check for updates by contacting our servers;
- show advertising or contain third-party advertising or tracking SDKs;
- read your messages for any purpose other than showing them to you,
  indexing them for search and the local spam scan, and the local "needs an
  answer" feature;
- share, sell or otherwise disclose any of your data.

Because no data reaches us, we hold no personal data about UltraMail users and
cannot identify you from your use of the application.

## 7. Your rights

Under the GDPR you have the rights of access (Art. 15), rectification
(Art. 16), erasure (Art. 17), restriction of processing (Art. 18), data
portability (Art. 20) and objection (Art. 21), and the right to lodge a
complaint with a supervisory authority (Art. 77). The authority responsible
for us is the Landesbeauftragte für Datenschutz und Informationsfreiheit
Nordrhein-Westfalen, Kavalleriestraße 2–4, 40213 Düsseldorf, Germany.

Because UltraMail stores your data only on your own device, you exercise these
rights directly in the application: you can read, correct, export (your
messages are standard `.eml` files) and delete everything yourself. For data
held by your e-mail provider, Google, Microsoft, Yahoo, Mozilla or the
operators of the websites named in section 5, please contact them; they are
the controllers for that processing.

For any question about this declaration, write to [privacy@ultraos.eu].

## 8. Children

UltraMail is a general-purpose mail program and is not directed at children.
Since it collects no data for us, no age verification takes place.

## 9. Changes to this declaration

We update this declaration whenever UltraMail's data flows change, for example
when a feature that contacts a server is added. The version and date at the
top identify the current text; earlier versions are available in the project's
source repository.
