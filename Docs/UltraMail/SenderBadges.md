# UltraMail — sender badges, the icon cache and the phishing scan

Every row of UltraMail's message list carries a small square immediately left
of the subject, and the reading pane shows the same square beside the sender.
It answers the question a reader asks before anything else — *who is this
from, and can I trust it?* — with three things at once: the service's own icon
where the sender belongs to one, the sender's initial where it does not, and a
frame whose colour is the verdict.

Companion documents: [`Concept.md`](Concept.md) (the design),
[`AccountSetup.md`](AccountSetup.md), [`CHANGELOG.md`](CHANGELOG.md), and the
developer notes in [`Apps/UltraMail/README.md`](../../Apps/UltraMail/README.md).

## 1. What the colours mean

| Badge | Meaning | Where it comes from |
|---|---|---|
| **Filled green** | Known contact | The address is in the address book under Family, Friends or Leisure |
| **Filled blue** | Business contact | The address is in the address book under Work, Services or Other — **or** it belongs to a service in the known-sender registry |
| **Black outline** | New sender | Never seen in the address book |
| **Dark blue outline** | Likely advertisement | Bulk/marketing markers (`List-Unsubscribe`, `Precedence: bulk`, `Auto-Submitted`) |
| **Orange outline** | Likely spam | Server-side spam markers, the junk folder, or a content scan that did not add up |
| **Red outline** | Likely scam | Phishing markers — above all, links that lie about where they go |

Two rules decide which one wins:

* **Known beats guessed.** An address in the address book is a contact even
  when the message is bulk mail.
* **Danger beats known.** A message whose links lie is called a scam even when
  the sender is in the address book — an address book entry says who an
  address belongs to, not that *this* message really came from them.
* **Bulk mail is still bulk mail.** A campaign newsletter from Kickstarter is
  an advertisement (dark blue), not a business contact: the dark-blue badge
  exists to say "this is marketing", and a known sender does not change that.
  A *transactional* message from the same service — a pledge receipt, an order
  confirmation — carries no bulk markers and reads blue.

The badge is never the only place a verdict is said: hovering it shows the
class, the service, the reason and the scan's findings in words, and a
suspicious or scam message also carries a warning strip above its body in the
reading pane. When the scan's verdict is suspicious or worse and a button (or,
failing that, a bare link) leads off the sender's domain, the strip is titled
*"Warning: This is likely a phishing² email!"*, quotes both domains —
`Mismatch of domains` / `Sender domain: …` / `Button domain: …` — and explains
the footnote: phishing emails try to get your credentials to hack your
accounts on other websites (`FindDomainMismatch` in the scan). Nothing is ever hidden, moved or deleted — UltraMail labels, the
reader decides.

## 2. The known-sender registry

`Apps/UltraMail/engine/UltraMailSenderBrands.{h,cpp}` holds the lookups and
`UltraMailSenderBrandTable.cpp` the curated table: about 400 services, with 600
of their own domains. They are the services whose mail an inbox actually
carries **and** the brands phishing mail most often dresses up as, because the
content scan (section 5) asks the same table "this mail says it is Coinbase -
is it from Coinbase?". Each entry has an id, a display name, the site's own
favicon URL, a brand colour and a category:

| Category | Examples (see the table for all) |
|---|---|
| Bank or broker (81) | Chase, Bank of America, Wells Fargo, Citi, American Express, Barclays, HSBC, Lloyds, NatWest, Santander, Revolut, Monzo, Deutsche Bank, Commerzbank, Sparkasse, ING, DKB, N26, comdirect, Postbank, UBS, PostFinance, ABN AMRO, Rabobank, BNP Paribas, BBVA, Nordea, RBC, Commonwealth Bank, HDFC, Itaú, Nubank, Robinhood, eToro … |
| Payment service (26) | PayPal, Stripe, Venmo, Zelle, Cash App, Wise, Western Union, MoneyGram, Klarna, Afterpay, Visa, Mastercard, Interac, TWINT, Alipay, paysafecard … |
| Crypto exchange or wallet (26) | Coinbase, Binance, Kraken, Crypto.com, Gemini, Bitstamp, KuCoin, OKX, Bybit, Bitpanda, Bitvavo, Blockchain.com, MetaMask, Ledger, Trezor, Trust Wallet, Exodus, Phantom, OpenSea … |
| Online shop (42) | Amazon, eBay, Etsy, Walmart, Target, Best Buy, Costco, AliExpress, Temu, SHEIN, Zalando, OTTO, Vinted, Kleinanzeigen, MediaMarkt, Lidl, Aldi, Tesco, IKEA, Shopify, DoorDash, Deliveroo, Lieferando … |
| Parcel carrier (26) | DHL, UPS, FedEx, USPS, Royal Mail, Evri/Hermes, DPD, GLS, Deutsche Post, InPost, PostNL, Colissimo, Swiss Post, Österreichische Post, PostNord, Poste Italiane, Correos, Canada Post, Australia Post … |
| Social network (24) | Facebook, Instagram, LinkedIn, X, Reddit, TikTok, Snapchat, Pinterest, Tumblr, Mastodon, Bluesky, XING, Tinder, Bumble, Hinge, Match, Grindr … |
| Messaging service (7) | WhatsApp, Telegram, Signal, Slack, Discord, WeChat, Viber |
| Cloud, hosting or file service (26) | Dropbox, WeTransfer, Box, MEGA, Cloudflare, DigitalOcean, Hetzner, IONOS, STRATO, OVHcloud, Hostinger, Bluehost, WordPress.com, Wix, Squarespace … |
| Domain registrar (14) | GoDaddy, Namecheap, Network Solutions, Name.com, Porkbun, Gandi, INWX, united-domains, DENIC, Nominet, ICANN, Verisign … |
| Online service (21) | Google, Apple, Microsoft, GitHub, Claude, OpenAI, Zoom, DocuSign, Adobe, Atlassian, Salesforce, Okta, Webex, Intuit, Xero, Canva … |
| Government agency (18) | IRS, SSA, USCIS, FBI, HMRC, DVLA, TV Licensing, Canada Revenue Agency, ATO, myGov, ELSTER, BZSt, Rundfunkbeitrag, impots.gouv.fr, ANTAI, Ameli … |
| Telecom provider (21) | Verizon, AT&T, T-Mobile, Xfinity, Telekom, Vodafone, 1&1, O2, BT, Swisscom, Telstra, KPN, Proximus … |
| Gaming service (10) | Steam, Epic Games, PlayStation, Nintendo, Roblox, Riot Games, Blizzard, EA, Ubisoft, Minecraft |
| Security software (13) | Norton, McAfee, Avast, Kaspersky, Bitdefender, Malwarebytes, ESET, LastPass, 1Password, Bitwarden, NordVPN, ExpressVPN … |
| Media, travel, crowdfunding, creator support | Netflix, Spotify, Disney+, Max, DAZN; Booking.com, Airbnb, Expedia, Uber, Ryanair, Lufthansa, Deutsche Bahn; Kickstarter, GoFundMe; Patreon, Ko-fi … |

The category is what the badge tooltip names ("Kickstarter (Crowdfunding
platform)") and what a collected contact's note records. The **Payments**
filter takes banks and crypto exchanges as well as payment services.

### What may go into the table

A domain in the table is *trusted*: its mail gets the brand's name and icon
and the blue business-contact badge. So the entries follow rules that are
stricter than "the brand probably owns this":

* **Only the brand's own domains**, and country domains listed one by one
  (`lidl.de`, `lidl.co.uk`, …). The older `labels` form ("any `ebay.*`") is
  kept for the handful of entries that had it, but trusts a squatted
  `ebay.xyz` too, so new entries do not use it.
* **A name that is an ordinary word is claimed through keywords only**
  (`NameClaim::KeywordsOnly`). "Chase", "Target", "Visa", "Discover",
  "Steam", "Signal", "Booking", "UPS" would otherwise turn every visa
  application, hotel confirmation or "follow-ups" into an impersonation
  finding; they are claimed by "chase bank", "target.com", "verified by
  visa", "steam support", "booking.com", "ups.com" instead.
* **No keyword is a mailbox provider's name**, and an address at a mailbox
  provider inside a display name claims nothing: a friend whose display name
  is just `jane@outlook.com` is not posing as Microsoft. An address at any
  other domain still counts — `"service@paypal.com" <x@evil.example>` is
  exactly the claim to catch.
* **Brands whose genuine mail comes from hundreds of regional domains are
  recognised but not claimed.** Each Sparkasse mails from its own
  `sparkasse-<region>.de`; claiming "Sparkasse" would flag the real ones.
* **Providers that are also mailbox domains are left out** (Orange, SFR, Sky,
  Virgin Media, Rogers): their addresses are people.
* **Government service suffixes are not one party.** `RegistrableDomain()`
  knows `gov.in`, `gouv.fr`, `gc.ca` and the three-level `service.gov.uk`, so
  `tax.service.gov.uk` is HMRC and `vehicle-tax.service.gov.uk` is not.

`registry_is_consistent` in `Tests/UltraMail/test_senderidentity.cpp` checks
the table: unique ids, every domain owned by one brand and resolving back to
it, no public suffix as a domain, lowercase keywords, no mailbox-provider
keyword.

Two rules hold it together, and both exist because the table is also what the
phishing scan reasons about:

* **A brand is matched on the registrable domain only** — never on a display
  name, never on a label anywhere in the host. `amazon.secure-login.ru` is not
  Amazon and must not be handed Amazon's icon. `RegistrableDomain()` reduces a
  host to `example.co.uk` / `example.com` using a small two-level public-suffix
  list.
* **A mailbox provider is not a brand.** Mail from `gmail.com`, `icloud.com` or
  `gmx.net` is personal mail that happens to be carried by Google, Apple or
  GMX, so those domains resolve to no brand at all — only a Google *service*
  domain (`google.com`, `youtube.com`, `accounts.google.com`) is Google. This
  is what keeps a friend's Gmail out of the "known service" category.

## 3. The sender-icon cache

`UltraMailSenderIconCache` owns one folder — `<dataDir>/cache/sender-icons` —
holding the icon of each registry entry, named after the brand id with the
extension sniffed from the downloaded bytes (`facebook.png`, `apple.ico`, …).

* **Only the curated registry is ever fetched.** UltraMail does not ask the
  internet about a stranger's domain; that would tell a third party who writes
  to the user. The set of possible requests is the brand table, and each is
  made at most once.
* **The fetch is injected.** The engine has no network dependency of its own:
  `SetFetcher()` takes the HTTPS GET (the app supplies one built on
  `UltraNet_HttpGet`, TLS verified, 10 s timeout, 512 KB cap), the test suite
  supplies a fake, and a build that sets none downloads nothing.
* **Fetching happens on the sync worker**, as each new message's header
  arrives — never on the UI thread, which only ever reads the folder.
* **A miss is remembered** in a `<brand>.missing` marker and not retried for a
  week, so an offline machine does not spend every sync on the same failures.
* **Bytes that are not an image are rejected** (a captive portal's HTML login
  page, an error page), and a file the image loader cannot decode falls back to
  the monogram rather than leaving an empty square.

No icon is a normal state, not an error: the badge then shows the sender's
initial in the brand's own colour. The whole feature can be turned off in
**Settings → Download icons of known senders** (stored as
`fetch_sender_icons` in `preferences.ini`); icons already in the folder keep
being shown.

## 4. The registry as a source of business contacts

A service the user hears from is a business relationship: the crowdfunding
platform a project was backed on, the shop an order came from, the
creator-support service a membership runs through. The registry is therefore
used twice:

* **In the badge.** A registry sender that is not (yet) in the address book
  reads as a **business contact** — blue — rather than as an unknown sender,
  with the tooltip naming the service and what kind of service it is.
* **In the address book.** `ContactCollector::CollectSender()` — which the app
  runs over each synced folder — files a registry sender under **Services**,
  carrying the service's name as the contact's `organization` and a note
  saying what it is and that UltraMail added it. A robot display name is
  replaced by the service's own name only when the header carries none
  ("Kickstarter" beats an empty From name). Everything else still lands in
  Other, exactly as before, and a personal mailbox address never counts as a
  service however large its provider.

One rule guards it: **an existing contact is never modified or reclassified.**
An address the user has filed under Friends themselves stays there, whatever
the registry says.

## 5. The content scan

`UltraMailThreatScan.{h,cpp}` reads a message the way a suspicious reader
would and returns a level (`Clean`, `Advertisement`, `Suspicious`, `Scam`), a
score and the reasons, in the words the tooltip and the warning strip show.
It runs **once per message, where the body is cached during sync**, and the
verdict is stored in the `message_security` table — its own table, so that an
envelope upsert (which happens on every header sync, long before a body
exists) can never reset a scan that has already run. A message whose body was
cached by an older build is scanned the first time it is opened.

Links are pulled out of both HTML (`<a href>`, `<area href>`, `<form action>`,
with their anchor text) and plain-text bodies (bare URLs). The rules, with
their weights:

| Finding | Weight | What it catches |
|---|---|---|
| `link-target-mismatch` | 50 | A link reads `www.paypal.com` and goes to `203.0.113.9` |
| `link-userinfo` | 45 | `http://paypal.com@203.0.113.9/` — everything before the `@` is decoration |
| `attachment-disguised-executable` | 55 | `Invoice_2026.pdf.exe` |
| `brand-impersonation` | 40 / 55 | The display name or subject claims a brand the From domain does not own (55 when sent from a free mailbox) |
| `link-brand-lookalike` | 40 | `apple-id-verify.delivery-update.example` |
| `attachment-executable` | 40 | A plain `.exe` / `.jar` / `.js` attachment |
| `link-ip-host` | 35 | A link straight to a numeric address |
| `auth-failure` | 30 | `Authentication-Results` reports `spf=fail` / `dkim=fail` / `dmarc=fail` |
| `advance-fee-fraud` | 25 / 50 | A sum in the millions ("US$ 15,500,000", "10.5 million dollars") plus a dead relative / estate, taxes or fees to pay first, or the 419 setting (barrister, Nigeria, "strictly confidential") — 50 when two of those appear |
| `credential-request` | 30 | "Your account will be suspended" + a link off the sender's domain |
| `attachment-double-extension` | 25 | `invoice.pdf.zip` |
| `link-punycode`, `link-nonascii-host` | 25 | Hosts drawn to look like familiar names |
| `insecure-login-link` | 20 | A sign-in link over plain `http://` |
| `link-brand-mismatch` | 20 | A button labelled with a brand that goes somewhere else |
| `reply-to-mismatch` | 15 | Replies would go to a different domain than the sender's |
| `link-shortener` | 12 | The destination cannot be seen |
| `many-foreign-domains` | 8 | Five or more link domains, none the sender's |
| `spam-flag` | 40 | The receiving server already said so |
| *(`dmarc=pass`)* | −10 | The From address is at least genuinely theirs |

45 and above is **Scam**, 22 and above is **Suspicious**; below that, a message
with bulk markers is an **Advertisement** and everything else is **Clean**.

The scan is deliberately asymmetric — a false "suspicious" costs the reader a
second look, a missed phishing mail can cost them their account — but it is
also deliberately quiet about ordinary mail: a personal message, a tracking
link on the sender's own domain and a genuine brand newsletter all come out
clean, and each of those is a test in
`Tests/UltraMail/test_threatscan.cpp`.

## 6. Where the code lives

| Piece | File |
|---|---|
| Brand registry, domain helpers | `Apps/UltraMail/engine/UltraMailSenderBrands.{h,cpp}`; the table in `UltraMailSenderBrandTable.{h,cpp}` |
| Icon cache | `Apps/UltraMail/engine/UltraMailSenderIconCache.{h,cpp}` |
| Content scan | `Apps/UltraMail/engine/UltraMailThreatScan.{h,cpp}` |
| Classification (address book + brand + verdict) | `Apps/UltraMail/engine/UltraMailSenderTrust.{h,cpp}` |
| Collecting a sender into the address book | `Apps/UltraMail/engine/UltraMailContactCollector.{h,cpp}` |
| Stored verdicts (`message_security`, schema 4) | `Apps/UltraMail/engine/UltraMailLocalStore.{h,cpp}` |
| Scan at download time | `Apps/UltraMail/engine/UltraMailSyncEngine.cpp` (`WriteBody`) |
| The badge itself (painting + element) | `Apps/UltraMail/ui/UltraMailSenderBadge.{h,cpp}` |
| Badge column in the message list | `Apps/UltraMail/ui/UltraMailMailView.cpp` |
| Badge + warning strip in the reading pane | `Apps/UltraMail/ui/UltraMailMessagePreview.cpp` |
| Colours | `Apps/UltraMail/ui/UltraMailTheme.h` (`kTrust*`) |
| Tests | `Tests/UltraMail/test_senderidentity.cpp`, `test_threatscan.cpp`, `test_contacts.cpp` |
