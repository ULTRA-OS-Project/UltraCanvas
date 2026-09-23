#### 2026-09-23 *0.10.3*
- **`OAuthApps` is a profile of UltraNet's shared OAuth2 app registry**
  (framework 0.9.31). Same API, same order - `Set()`, `ULTRAMAIL_*` in the
  environment, `oauth.ini`, the baked-in client - and the same behaviour for
  the wizard; what changes is where the registration lives: the Google and
  Microsoft clients UltraMail ships or reads from `oauth.ini` now also serve
  UltraCloud's Google Drive and OneDrive sign-ins in the composer's "Attach
  cloud link…" picker, and the shared `ULTRANET_OAUTH_GOOGLE_CLIENT_ID` /
  `ULTRANET_OAUTH_MICROSOFT_CLIENT_ID` names work beside the `ULTRAMAIL_`
  ones. `Docs/UltraMail/AccountSetup.md` §3 says what to add to the consent
  screen for that.

#### 2026-09-20 *0.10.2*
- **The credential vault is the framework's now.** `UltraMailCredentialVault.cpp`
  was the only implementation of the device-key vault, and UltraSocial had
  copied it; the implementation moved to `UltraVault::DeviceKeyVault`
  (framework 0.9.23) and `UltraMailCredentialVault.h` only names UltraMail's
  profile — `ultramail.vault` and `device.key` in the account folder, keys
  `mail.ultramail.<account>`. Nothing on disk changes and no existing vault
  needs migrating; `CredentialVault`, `OAuthTokens`, `SignInMethod` and
  `VaultStatus` keep their names in `namespace UltraMail` (the last three are
  aliases of the framework's types), so the app, EmailCleaner and the tests
  compile unchanged. `KeyFor` is a member now rather than static.
- **Cloud account credentials live in the mail vault too.** "Attach cloud
  link" kept its Nextcloud / Dropbox / Drive tokens in obfuscated files under
  `cloud-vault/` (UltraCloud's file store, which framework 0.9.23 removes).
  They are in `ultramail.vault` now, under `cloud.<accountId>.*`, and the old
  files are carried across and removed the first time the vault is open — at
  start-up with the device key, or after the one master-password prompt an
  older vault still needs.

#### 2026-09-19 *0.10.1*
- **Crowdfunding and creator-support platforms are known senders.** Kickstarter,
  Indiegogo, GoFundMe, Startnext, Crowd Supply, Patreon, Buy Me a Coffee, Ko-fi,
  Liberapay, Open Collective, Gumroad and Substack join the registry, along with
  Pinterest, Tumblr, Mastodon, Twitch, Vimeo and Etsy. Each carries its own
  site's favicon URL, which is exactly what the sender-icon cache fetches — one
  request per service, once, on the sync worker — so their mail arrives wearing
  the platform's own mark.
- **Registry entries now carry a category** (`BrandCategory`: social, messaging,
  crowdfunding, creator support, shopping, payment, technology, media, travel,
  delivery). The badge tooltip names it — "Kickstarter (Crowdfunding platform)" —
  and a collected contact's note records it.
- **The registry is a source of new business contacts, not only of icons.**
  A sender that belongs to a known service now reads as a **business contact**
  (blue) even before the address book has caught up, and the auto-collector
  (`ContactCollector::CollectSender`) files it under **Services** with the
  service's name as the contact's organization and a note saying what kind of
  service it is. Ordinary addresses still land in Other, a personal mailbox is
  never a service however large its provider, and **an existing contact is never
  modified or reclassified** — an address the user filed under Friends stays
  there. Bulk mail keeps its own badge: a campaign newsletter from a known
  platform is still an advertisement (dark blue), because that is what the
  dark-blue badge is for; a pledge receipt or order confirmation from the same
  service carries no bulk markers and reads blue.

#### 2026-09-19 *0.10.0*
- **A sender badge left of every subject line.** The message list gained a
  narrow column between From and Subject, and the reading pane's avatar became
  the same square: the service's own icon where the address belongs to one, the
  sender's initial where it does not, inside a frame that says who the sender
  is — filled green for an address book contact under Family/Friends/Leisure,
  filled blue for one under Work/Services, a black outline for a sender never
  seen before, dark blue for bulk mail, orange for likely spam and red for a
  likely scam. Two rules decide: known beats guessed (a contact stays a contact
  even when the mail is bulk), and danger beats known (a message whose links
  lie is a scam even from a known address — an address book entry says who an
  address belongs to, not that *this* message came from them). The colour is
  never the only place a verdict is said: the badge's tooltip spells out the
  class, the service and every reason, and the reading pane adds a warning
  strip above the body of anything suspicious. Nothing is hidden, moved or
  deleted. New: `UltraMailSenderTrust`, `UltraMailSenderBadge`, the `kTrust*`
  colours in `UltraMailTheme.h`.
- **A cache folder of known senders' icons.** `<dataDir>/cache/sender-icons`
  now holds one icon per entry of a curated registry of ~30 services —
  Facebook, Instagram, WhatsApp, LinkedIn, X, Claude, OpenAI, the Google and
  Apple services, Microsoft, GitHub, Amazon, PayPal, Stripe, eBay, Netflix,
  Spotify, Dropbox, Slack, Discord, Telegram, Reddit, TikTok, Zoom, Booking,
  Airbnb, DHL, UPS, FedEx — fetched once each, on the sync worker, over a
  TLS-verified HTTPS GET, with the file extension sniffed from the bytes and a
  failure remembered for a week rather than retried every sync. Two boundaries
  are deliberate: **only the registry is ever fetched** (UltraMail never asks
  the internet about a stranger's domain, which would tell a third party who
  writes to the user), and **the fetch is injected**, so the engine keeps no
  network dependency and the suite drives the cache with a fake. A new
  Settings checkbox, *Download icons of known senders*
  (`fetch_sender_icons` in `preferences.ini`), turns downloading off; icons
  already cached keep being shown, and a missing icon is a normal state — the
  badge falls back to the initial in the brand's own colour.
- **A brand is matched on the registrable domain, never on a name.**
  `amazon.secure-login.ru` is not Amazon and is not handed Amazon's icon, and —
  as asked — a Google *service* domain is Google while an ordinary `gmail.com`
  address is just a person (the same holds for `icloud.com`, `gmx.net` and the
  other mailbox providers). `UltraMailSenderBrands` carries the table, the
  registrable-domain reduction and the "does this domain really belong to the
  brand it claims?" test the phishing scan is built on.
- **UltraMail now says when a message's links do not go where they say.**
  `UltraMailThreatScan` reads every link and button out of an HTML or plain
  text body and weighs seventeen signals: anchor text naming one site while the
  href goes to another, `http://paypal.com@203.0.113.9/` userinfo hiding the
  real host, numeric-address and punycode targets, a brand's name worn in front
  of a foreign domain, a sender claiming a brand its address does not own,
  credential language plus a link off the sender's domain, `spf`/`dkim`/`dmarc`
  failures, a Reply-To pointing elsewhere, sign-in links over plain HTTP, URL
  shorteners, and an attachment that is a program — `Invoice_2026.pdf.exe`
  weighs more than a plainly named `setup.exe`, because the disguise *is* the
  attack. 45 points is a scam, 22 is suspicious, bulk markers alone are an
  advertisement. It is just as deliberately quiet about ordinary mail: a
  personal message, a tracking link on the sender's own domain and a genuine
  brand newsletter all come out clean.
- **Each body is scanned once, where it is downloaded.** The verdict (level,
  score, reasons) is stored by `LocalStore` in a new `message_security` table —
  schema 4 — keyed by account/folder/UID. It is a separate table rather than
  columns on `messages` because an envelope upsert runs on every header sync,
  long before a body exists to scan, and would reset the verdict each time. So
  the list colours a whole folder's badges from one query, a phishing mail is
  marked before it is ever opened, and a message cached by an older build is
  scanned the first time it is read.
- Docs: [`Docs/UltraMail/SenderBadges.md`](SenderBadges.md) — the colours, the
  registry, the cache and the full rule table. Tests:
  `Tests/UltraMail/test_senderidentity.cpp` and `test_threatscan.cpp`, plus the
  stored-verdict and scan-on-download cases in `test_localstore.cpp` and
  `test_syncengine.cpp`.

#### 2026-09-15 *0.9.5*
- **"Add email account" accepts a name like `Fröhling`.** Typing one into the
  wizard's Your name field (or a passphrase with an umlaut into Password) used
  to leave invalid UTF-8 in the field the moment the caret moved over the
  character, backspaced across it or a click landed on it — the text stopped
  drawing and the debug log filled with Pango's `Invalid UTF-8 string passed to
  pango_layout_set_text()`. The fault was in the framework's text field, not in
  UltraMail: see framework 0.8.61 for what changed. That entry also covers the
  other half of the same name's journey — the display name now goes out as an
  RFC 2047 encoded-word in `From:` instead of as a raw 8-bit header byte.

#### 2026-09-14 *0.9.4*
- **One icon for UltraMail, everywhere it is shown.** `media/appicon/UltraMail.png`
  — the coloured ring around an `@` — replaces the envelope placeholder
  (`UltraMail.svg`, now removed) as the app's single mark, and every place that
  shows one now reads that one file:
  - the **window icon**, published as `_NET_WM_ICON` from `main.cpp`, which is
    also where a Linux / ULTRA OS **taskbar** takes the button's image from;
  - the **taskbar and Explorer icon on Windows**, which read the binary rather
    than the window: `ultracanvas_embed_app_icon(UltraMail …)` converts the PNG
    to a multi-resolution `.ico` and links it into `UltraMail.exe`;
  - the **filer's app icon**, via a new `Apps/UltraMail/UltraMail.desktop`
    (`Icon=UltraMail`) installed to `share/applications` with the PNG installed
    to `share/icons/hicolor/256x256/apps` — UltraFiler resolves an application's
    icon by reading its desktop entry and looking the name up in the installed
    themes (`UltraCanvasNativeFileIcons.cpp`), so the entry is what it needs to
    find anything but a generic executable glyph;
  - the **start-page logo** (`UltraMailStartPage`);
  - and `UCAPP_ICON_PATH`, the core's build-time fallback, so a window cannot
    come up unbranded even if an icon is never set explicitly.
  The desktop entry deliberately declares no `MimeType=` and no `%U`:
  UltraMail's `main()` takes no arguments yet, so claiming the `mailto:`
  association would route mail links to a program that drops them.

#### 2026-09-14 *0.9.3*
- Implemented OAuth for Google. Fixes in layout and HTML renderer.

#### 2026-09-11 *0.9.2*
- **Type scale matched to UltraFiler.** UltraMail's text was far larger than
  the rest of the desktop (13pt body, 15pt headings, 18pt titles next to
  UltraFiler's 9pt UI font). `UltraMailTheme.h` now uses 9pt body text,
  8.5pt secondary, 8pt small, 11pt headings and 13pt titles; controls are
  24px high in a 28px toolbar, the avatar square 28px, and the paddings,
  gaps and radii shrank with them. Every view followed: the inbox list
  (22px rows and header, narrower From / Date columns), the message header
  (13pt subject, 26px avatar, HTML bodies at 12px), the account cards and
  their counters, the contact rows, the composer, the attachment chips, the
  start page, and every dialog (wizard, master password, wait, server
  settings, contact) with its fixed heights. Dropdowns get the body size
  through the new `Theme::StyleDropdown`.

#### 2026-09-10 *0.9.1*
- **The settings page checks the sign-in before it saves.** Save opens one
  IMAP session to the incoming server with the entered host, port and
  security and the account's credentials — the typed password in the
  wizard, or the stored password / OAuth2 token of an existing account
  (resolved on the worker like a sync) — and only closes on success. A
  failed check shows the reason in place and offers **Save anyway**, for a
  server that is down right now or when the IMAP plug-in is not loaded. The
  engine side is `LoginCheck::Imap` / `OptionsFor` in
  `UltraMailLoginCheck.{h,cpp}` (tested with a recording mailbox); the page
  takes a `Verifier` and runs it off the UI thread via
  `UltraMailApp::LoginVerifier`. SMTP has no sign-in-only operation in the
  plug-in interface, so the outgoing server is not checked.

#### 2026-09-10 *0.9.0*
- **Any provider: autoconfig lookup and a manual settings page.** An address
  outside the provider table no longer ends as an account that cannot fetch.
  The wizard now asks the domain's own autoconfig document, its
  `.well-known` copy and the Thunderbird ISPDB (`AutoDiscovery::Discover`,
  on a worker thread behind a cancellable "Looking up server settings"
  dialog) and, when nothing is published, opens the **server settings page**
  (`UltraMailServerSettingsDialog`: incoming / outgoing host · port ·
  security dropdown, username; prefilled with `imap.<domain>` 993 SSL/TLS
  and `smtp.<domain>` 587 STARTTLS via `AutoDiscovery::GuessForDomain`;
  validates in place). The account-ready dialog says where the settings
  came from.
- **Server settings are stored on the account.** `Account` carries
  `imap` / `smtp` (`MailServerSettings`: host, port, security, username,
  oauth flag) and `providerName`; `LocalStore` schema 2 adds the columns.
  Every sync and send resolves the servers through
  `AutoDiscovery::ForAccount` — the stored ones, else the provider table for
  accounts created before — and applies the stored security (SSL/TLS,
  STARTTLS, plain) and username instead of assuming implicit TLS and the
  address. `Outbox::Flush` takes an options resolver that sets the SMTP
  session's username and TLS mode along with the credentials.
- **Reload opens the settings page** for an account whose servers are not
  known, instead of only saying so; saving stores them and syncs at once.
  Adding an address again keeps the servers it already has.
- `MailSecurity` and `MailServerSettings` moved from `UltraMailDiscovery.h`
  to `UltraMailTypes.h`; the plaintext value is `MailSecurity::Plain` (the
  old `None` collides with the X11 macro once a UI translation unit includes
  the type). `OAuthWaitDialog` became the generic `WaitDialog`.

#### 2026-09-10 *0.8.3*
- **Account setup guide.** `Docs/UltraMail/AccountSetup.md`: how to sign in
  to each provider in the table (Gmail, Outlook / Microsoft 365, Yahoo,
  iCloud, GMX, WEB.DE, mailbox.org, Posteo) — servers, the sign-in each one
  expects, where its app password is generated, the OAuth client registration
  for the browser sign-in, what lives on the machine, and what the error
  messages mean.
- **Outlook is browser sign-in only.** Microsoft retired password
  ("basic") authentication for IMAP/SMTP on Outlook.com and in Microsoft 365,
  app passwords included. The wizard hint no longer offers an app password
  for Outlook addresses, and the account-ready dialog says a typed password
  will be refused. `ProviderAcceptsPassword` in `UltraMailOAuth` carries the
  rule.

#### 2026-09-10 *0.8.2*
- **App-password hint for Yahoo and iCloud.** The wizard's live hint under
  the password field now also covers providers that offer no OAuth2 to mail
  apps but reject the normal account password: Yahoo and iCloud get "enter an
  app password generated in your account's security settings" as the address
  is typed (placeholder "App password"), the same advice the account-ready
  dialog gives. The rule is `ProviderNeedsAppPassword` in `UltraMailOAuth`,
  shared by the wizard and the dialog.

#### 2026-09-10 *0.8.1*
- **Outlook / Microsoft 365 sign in with Microsoft.** The second entry in the
  OAuth2 provider table: `microsoft` — the Microsoft identity platform's
  `common` tenant endpoints, the `IMAP.AccessAsUser.All` + `SMTP.Send` +
  `offline_access` scopes, `prompt=select_account`, a public client (no
  secret). Outlook, Hotmail, Live and Microsoft 365 addresses get the same
  browser sign-in, wait dialog, vault token set and XOAUTH2 sessions as Gmail.
  Registration: `[microsoft]` in `oauth.ini` or `ULTRAMAIL_MICROSOFT_CLIENT_ID`
  (README, "OAuth2 sign-in").
- **Per-provider redirect default.** Microsoft matches loopback redirects on
  host and path with the port ignored, so its default is
  `http://127.0.0.1:0/` (register `http://127.0.0.1`); Google keeps
  `/callback`. `OAuthApps::Get` fills an empty `redirectUri` with
  `DefaultRedirectUri(provider)`.
- **Login hint.** The typed address goes to the consent page as `login_hint`
  for both providers, so the user is not asked to pick the account again.

#### 2026-09-10 *0.8.0*
- **Gmail signs in with Google.** Leave the password empty in the account
  wizard for a Gmail / Googlemail address and UltraMail opens Google's consent
  page in the browser (OAuth2 authorization code + PKCE over UltraNet's OAuth2
  client, redirect caught on an ephemeral loopback port). A "Sign in with
  Google" dialog waits — with Cancel — until the redirect arrives, the tokens
  go into the credential vault, and the inbox is fetched right away. Every
  IMAP and SMTP session of such an account then authenticates with XOAUTH2
  and a fresh bearer token: an expired one is refreshed through Google on the
  worker thread before the fetch, and stored again. A typed password still
  works the classic way (an app password).
- **Engine: `UltraMailOAuth.{h,cpp}`** — the provider table (`google`:
  endpoints, the `https://mail.google.com/` scope, `access_type=offline`,
  `prompt=consent`), the app registration (`OAuthApps`: `Set()`, the
  environment `ULTRAMAIL_GOOGLE_CLIENT_ID` / `_CLIENT_SECRET` /
  `_REDIRECT_URI`, or `oauth.ini` in the data folder), and `MailOAuth`
  (`SignIn`, `EnsureFresh`, `CredentialsFor`) with test seams for the
  interactive authorization and the refresh. `CredentialVault` stores an
  OAuth2 token set (access + refresh + expiry) beside the password slot — an
  account has exactly one sign-in method (`MethodFor`). `Outbox::Flush` takes
  a credentials resolver; `SyncService::SyncInBackground` takes a prepare step
  that runs on the worker. The SMTP plug-in now honours XOAUTH2 bearer
  credentials like the IMAP plug-in already did.
- **Wizard hint.** As the address is typed, the wizard says whether to leave
  the password empty for the browser sign-in, or — when no Google OAuth client
  is configured — to use an app password.

#### 2026-09-10 *0.7.1*
- **A new account fetches its inbox right away.** Adding an account only
  wrote it to the store; the first sync waited for the five-minute timer —
  which had been started before the account existed, so it never covered it —
  or for a manual Reload. Once the password is in the vault the account is
  put on the schedule and synced at once, and the timer starts if it was not
  running yet (the plug-in used to be checked only at start-up).
- **The IMAP plug-in is found wherever the app is started from.** The UltraNet
  registry looks for plug-ins in `Plugins/UltraNet` relative to the *working
  directory*, which matches the build tree only when UltraMail is run from
  there. The app now resolves the directory against the executable (up to two
  levels above it, then the working directory), `ULTRAMAIL_PLUGIN_DIR` still
  overriding.
- **Nothing fails silently any more when mail cannot be fetched.** Reload and
  the first sync of a new account used to return without a word when the IMAP
  plug-in was not loaded, when no server was known for the address, or when no
  password was stored. Each case now says what is missing and where (the
  plug-in message names the directory that was searched). Errors from a sync
  the user asked for are always shown; timer syncs still report once.
- **Passwords of the second and later accounts are saved on Windows.**
  UltraVault replaced the vault file with C's `rename()`, which on Windows
  refuses to overwrite an existing file — so the first account's password was
  stored and every later one failed with "could not be saved to the credential
  vault". It uses `std::filesystem::rename` now.
- **The data folder is `%APPDATA%\UltraMail` on Windows.** `HOME` is normally
  unset there, so the mailbox database and the vault were created in whatever
  folder the app was started from.
- **App-password hint.** The account-ready dialog tells Gmail, Outlook and
  Yahoo users that the normal sign-in password is rejected over IMAP and an app
  password from the provider's security settings is needed; the earlier
  "Sign-in: OAuth2 (browser)" line described a flow the app does not have.

#### 2026-09-09 *0.7.0*
- **Every window restyled on one theme.** `Apps/UltraMail/ui/UltraMailTheme.h`
  now holds the app's colours (near-white page, white cards with hairline
  borders, one blue accent, a primary / secondary / muted text scale), type
  sizes, metrics, and the styling helpers (`StylePrimary`, `StyleSecondary`,
  `StyleInput`, `CardGroupBox`, `MakeAvatar`, `ClickSurface`). The windows use
  it instead of styling in place, so they cannot drift apart.
- **Main window.** The 150px button column is gone; a single toolbar row
  carries **New email** (the one filled button), **Reload**, **Contacts** and,
  on the right, **Add account**. The account summary is a compact card:
  provider initial in a tinted square, the local part with the domain under it
  (full address on hover), and the three counters as tinted *count · caption*
  pills (blue = new today, green = unread before, orange = waiting for reply).
  With several accounts the tiles are the same cards, the selected one with an
  accent frame; their height follows their content instead of a fixed square.
- **Inbox and message panes** are white cards separated by an invisible
  splitter gap. The list has 30px rows, a quiet header, soft hover / selection
  tints and no expander column; the date column shortens the way mail clients
  do (time today, "Sep 09" this year, "Jan 14, 2025" older; the full date is in
  the row tooltip). The message header is one row — sender avatar, name over
  address · recipients, date, **Reply** — above a rule and the body; with no
  selection the pane shows a muted hint and no header chrome.
- **Composer** is a flex layout that follows the window (760×620 by default):
  To / Cc / Subject rows with captions, a rule, a borderless body that takes
  the remaining height, an attachment row shown only while something is
  attached, and a bottom toolbar with **Send** (primary), **Attach file…**,
  **Attach cloud link…** and, apart on the right, **Cancel** — which now closes
  the window.
- **Contacts** is a sidebar + list layout that follows the window: sections
  are selectable entries with counts in a tinted sidebar, the list is a titled
  column of contact cards (initial, name, email · phone, organisation). The
  contact dialog, the account wizard and the master-password dialog share the
  same rows, inputs and button order (Cancel, then the primary action).
- **UltraCloud dialogs** (`UltraCloud/ui/UltraCloudUiStyle.h`): the account
  dialog and the link picker use the same palette — captions, styled inputs,
  a quiet list header, secondary buttons and one primary action.

#### 2026-09-04 *0.6.0*
- **Account tiles grow with their counters.** The tile was a fixed 176×176 box,
  so a four- or five-digit unread count pushed the counter row past the rounded
  frame and the container clipped it. The tile's width is now **auto** with 176
  as a *minimum* (the height stays fixed), so the frame widens with the numbers
  while the letter, the address and the counter row stay centred on the tile's
  centre line. The counter row also centres explicitly
  (`JustifyContent::Center`) instead of relying on shrink-wrap.
- **Counters are rounded boxes, not pills.** `BadgeStyle::cornerRadius` (new,
  `-1` keeps the pill default) lets `UltraCanvasBadge` draw a rounded box; the
  account counters use an 8px radius so they echo the tile's rounded frame, as
  in the design.

#### 2026-09-03 *0.3.0*
- **Attachments in the composer.** "Attach file…" opens the file dialog and
  adds the file to the draft (media type guessed from the extension); the
  attachment strip under the body lists what is attached, and forwards carry
  the original's attachments there too. Attached files go out through the
  existing MIME builder.
- **"Attach cloud link…"** through the new **UltraCloud** module
  (`Docs/Modules/UltraCloud/README.md`): the picker lists the cloud accounts
  (default preselected), browses the chosen account, uploads a local file into
  the current folder, and puts a share link for the selected file into the
  body as "<name>: <url>". With no account yet it offers the add-account
  dialog (Nextcloud / ownCloud with password- and expiry-capable links, generic
  WebDAV, and an in-memory demo). Accounts live in `cloud.db` next to the mail
  store, secrets in `cloud-vault/`. `ULTRAMAIL_DEMO_CLOUD=1` seeds a demo
  account and opens the composer.
#### 2026-09-03 *0.5.0*
- **Mail account passwords now live in UltraVault.** The 0.1 credential vault
  XOR-ed each secret against a 32-byte key it wrote to `vault.key` **in the same
  directory as the ciphertext** — anyone who could read the vault folder could
  recover every mail password, and the file permissions were the only real
  control. Secrets now go through `UltraVault`
  (`UltraCanvas/include/UltraVault`), the framework's credential module, whose
  file backend derives its key from a passphrase with Argon2id and seals the
  store with XChaCha20-Poly1305 via UltraCrypt, authenticating the header so
  tampering with the stored cost parameters is detected rather than obeyed.
  UltraMail no longer implements a secret format of its own — the same module
  UltraNet, UltraDatabase and UltraAI resolve credentials through.
- **A master password guards the vault.** It is the passphrase the key is
  derived from and is never written to disk, so the stored secrets genuinely
  cannot be read without the user. UltraMail asks for it once per session —
  with confirmation the first time, when there is no vault yet — through the
  new `ui/UltraMailPassphraseDialog`. Cancelling leaves the vault locked rather
  than falling back to something weaker.
- Secrets under the 0.1 format are migrated on the first successful unlock and
  the old `creds.dat` / `vault.key` are deleted — but only once every secret is
  safely in the new vault, so a partial migration loses nothing.
- `CredentialVault` reports *why* an unlock failed (`VaultStatus`): a wrong
  master password re-prompts with the reason shown in the dialog, while a build
  without libsodium says so and stops instead of appearing to work. A wrong
  passphrase and a tampered vault are deliberately indistinguishable — that is
  UltraVault's no-oracle rule, and the message covers both.
- The vault is now a session-lifetime member of `UltraMailApp` (it was
  constructed per call site), and its derived key is wiped on shutdown. Sending
  and adding an account unlock in the foreground; the background sync timer
  never raises a password prompt over what the user is doing — it skips the
  round and says once that mail is not being fetched while the vault is locked.
- Alert helpers take an optional completion callback, so the add-account flow
  shows the discovery result and *then* asks for the master password instead of
  stacking one dialog under the other.
- Linking UltraVault pulls in UltraCrypt, which exposed a latent link-time
  collision in the framework's text utilities that broke the Windows build of
  both UltraMail and EmailCleaner. The fix is a framework change and is recorded
  in [`Docs/UltraCanvas/CHANGELOG.md`](../UltraCanvas/CHANGELOG.md) *0.3.95*;
  this release depends on it.

#### 2026-09-03 *0.4.0*
- **"Save As…" on an attachment now works.** The reading view's attachment strip
  raised its `onSaveAs` callback into nothing — the menu entry was inert — and
  `SaveAttachment()` ignored where the user wanted the file, writing blindly
  into the attachment cache instead. The strip's callback is now wired, and
  saving goes through `UltraCanvasFileLoader::SaveFileDialog`: the user picks
  the destination, the file is written there via the `AttachmentCache::SaveAs()`
  the engine already provided, the path is registered with the platform's
  recent-documents list, and the result is reported. The dialog opens on
  Downloads (falling back to home), pre-fills the sanitised attachment name, and
  offers the attachment's own media type as a filter.
- UltraMail now uses the framework's file loader at all: it previously called
  `UltraCanvasFileLoader` nowhere, while every other application in the tree
  uses it. `ULTRAMAIL_DEMO_SAVE=1` exercises the save dialog, matching the
  existing `ULTRAMAIL_DEMO_OPEN` demo path.
- Cached message bodies are read through `UltraCanvasFileLoader::LoadFile()`
  rather than a bare `ifstream`. A body that exists but cannot be read now says
  so and shows the reason, instead of being indistinguishable from one that was
  never downloaded — the "not downloaded" text now reads "not downloaded yet".
- Note on modules: plain local file and directory work continues to use
  `std::filesystem`, matching every other application in the tree. VirtualFS is
  the transparent-archive module (ZIP/7z/TAR as folders), not a filesystem
  wrapper, so it is not the right tool for writing the mail store; the file
  loader already routes through it for transparent decompression.

#### 2026-09-03 *0.3.0*
- **Failures are now reported instead of swallowed.** UltraMail used to fail
  silently almost everywhere: a rejected password, an untrusted certificate, an
  unreachable server, a database that would not open, an attachment that could
  not be written — each ended in a bare `return`, so the app simply appeared to
  do nothing. The diagnosis already existed in `UltraNetResult::message`,
  `UltraDbResult::message` and `SyncOutcome::message`; it was being dropped at
  the UI boundary. Every one of those paths now raises an alert that names the
  cause. This delivers the error-handling contract in `Concept.md`.
- Alerts are `UltraCanvasAlert` (`UltraCanvas/include/UltraCanvasAlert.h`), so a
  failure carries an Error severity and icon rather than the Information dialog
  a failed send used to show. The one-line summary goes in the alert's message
  and the underlying diagnostic in its `details` line.
- New `ui/UltraMailAlerts.{h,cpp}`: `FriendlyMessage()` maps the UltraNet result
  codes a mail client actually hits — `AuthenticationFailed`,
  `TlsCertificateInvalid`, `TlsCertificateExpired`, `HostNotFound`,
  `ConnectionRefused`, `PluginNotFound` and the rest — to text a user can act
  on, and `IsRetryable()` decides when a Retry button is offered.
- A failed send now offers **Retry**, which re-flushes the outbox, rather than
  only stating that the message was queued.
- Input is validated where it is entered: sending with no recipient, adding an
  account with an empty or malformed address, and saving a nameless contact each
  explain what is wrong instead of discarding what was typed. New pure helper
  `LooksLikeEmailAddress()` beside `EmailDomain()` / `EmailLocalPart()` in the
  discovery engine.
- Background sync failures surface. `RunDueSyncs()` discarded its `SyncOutcome`
  entirely, so a wrong password or an expired certificate meant mail silently
  never arrived. The outcome is now reported once per run of failures — not on
  every timer tick — and re-arms when a sync succeeds again.
- A startup failure shows an alert naming the data folder and the database error
  before exiting, instead of terminating with no window and no message.
- `Outbox::FlushStats` carries `lastFailure` (the `UltraNetResult` of the most
  recent failed send) so the UI can say *why* a message stayed in the queue; the
  reason was already persisted as the outbox row's `last_error` but was never
  read back. `OutboxStore::IsOpen()` added to mirror `LocalStore` /
  `ContactStore`, so "the queue is unavailable" is distinguishable from "the
  queue is empty".
- `UltraMailApp::Initialize()` takes an optional `outError` and records why the
  contacts / outbox stores failed to open, so the Contacts button reports the
  problem rather than doing nothing.

#### 2026-09-03 *0.2.0*
- **First run shows a start page, nothing else.** Until the first email account
  exists the main window holds only the UltraMail logo, the app title and an
  "Add email account" button (`UltraMailStartPage`). The old "Welcome to
  UltraMail. Add an account to begin." hint is gone. The button is a
  primary-style `UltraCanvasButton` with the envelope icon; the page is a
  centred flex column that follows the window size.
- **The main window is one screen: actions · account bar · inbox | message.**
  The Toolbox grid, the info-tile bar and the separate three-pane reading
  window are replaced by a single account view (`UltraMailApp::BuildAccountView`,
  a flex column sized to the window):
  - an **actions column** — New email, Reload email, Contacts, Add account;
  - the **account bar** (`UltraMailAccountBar`): with one account a summary
    strip showing the provider's initial (first letter of the address's domain,
    upper-case, bold), the account name (local part; full address in the
    tooltip) and three `UltraCanvasBadge` counters — New today (blue), Unread
    before today (lime), Waiting for reply (orange); with several accounts a
    row of square tiles carrying the same information, the clicked tile
    (selection-blue frame) driving the mail view;
  - the **mail view** (`UltraMailMailView`): an `UltraCanvasSplitPane` with an
    "Inbox" group box holding the selected account's inbox as an
    `UltraCanvasColumnsTreeView` (From · Subject · Date, `●` unread, `↩`
    waiting for reply, counts in the caption) and a "Message" group box holding
    the `UltraMailMessagePreview` (subject, from, to, date, Reply, the body —
    HTML through HTMLReader / CSSLayout, plain text in a read-only text area —
    and the attachment strip). The preview is the reading view's pane, moved
    into its own class; `UltraMailReadingView`, `UltraMailToolbox` and
    `UltraMailInfoTileBar` are removed.
- **Reload email** syncs every account immediately when the IMAP plug-in is
  loaded (the button reads "Reloading…" until the last sync returns) and
  re-reads the store either way.
- **Engine: `GetAccountStatus` splits unread into today / before today** and
  carries the account's email (`AccountStatus::unreadToday`, `unreadOlder`,
  `email`); an optional `todayStart` argument pins local midnight for tests.
- **UltraMail has an app icon** (`media/appicon/UltraMail.svg`): the envelope on
  the selection blue. It is the start-page logo and the window icon.
- Not used: `UltraCanvasTableView` does not compile in this tree (it is unused
  by every other target); the inbox list is a `UltraCanvasColumnsTreeView`.

#### 2026-08-31 *0.1.0*
- **UltraMail keeps its own changelog from here.** Everything up to and
  including this version shipped as part of a framework release and is recorded
  in [`Docs/UltraCanvas/CHANGELOG.md`](../UltraCanvas/CHANGELOG.md) — nothing
  was rewritten or moved, so that history stays where it was published. From
  now on a change to the mail client (`Apps/UltraMail`) is described here and
  carries this file's version, and UltraMail no longer moves when the framework
  releases.
- A framework change UltraMail needs still belongs in the framework changelog.
  Cross-reference it from here when a release depends on it; never describe one
  change in two files under two version numbers.

<!--
Version source of truth: the first line of this file, format
`#### YYYY-MM-DD *x.y.z*`, read by cmake/UltraCanvasVersion.cmake.
-->
