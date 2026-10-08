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
| **Red outline** | Likely scam | Phishing markers — above all, links that lie about where they go — or the pattern of a known scam: a romance scam, an advance-fee letter, a cryptocurrency scam |

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
accounts on other websites (`FindDomainMismatch` in the scan). A message the
scan reads as a **romance scam** is titled *"Warning: This is likely a romance
scam² email!"* instead ("may be" when the verdict is only suspicious), even
when it also links elsewhere, and its footnote says what a romance scam is and
that a reverse image search often finds the photo under another name; a
**cryptocurrency scam** (a recovery phrase asked for, a wallet address to pay
into, promised profit) gets its own title and footnote the same way. **Any
message about cryptocurrency** that is not otherwise flagged — a newsletter, an
exchange's own mail — still gets an orange *"Caution: this message is about
cryptocurrency"* strip: a crypto payment cannot be called back, no genuine
service asks for a recovery phrase, no genuine investment guarantees a
profit. The strip knows which scam it is from the finding codes stored with
the verdict (`MessageSecurity::findings`). Nothing is ever hidden, moved or
deleted — UltraMail labels, the reader decides.

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
  (`lidl.de`, `amazon.co.uk`, `dhl.de`, …). No entry is trusted
  under "any `amazon.*`": that would hand a squatted `amazon.xyz` the badge
  too, and the consistency test refuses it.
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
holding two kinds of icon, each under a key:

* **A registry service's icon**, from the URL in the brand table, named after
  the brand id with the extension sniffed from the downloaded bytes
  (`facebook.png`, `apple.ico`, …). Key: the brand id.
* **A website's icon**, for a sender that is no known service: the home page
  of the registrable domain it writes from (`https://example.com/`, then
  `https://www.example.com/`) is read for its `<link rel="icon">` — a size a
  badge can use first, then the home-screen icon, an icon of no stated size,
  SVG, tiny ones — and `/favicon.ico` is the fallback (`FindSiteIconUrls`).
  Filed under `sites/example.com.png`. Key: `site:example.com` (`SiteIconKey`).

How it is filled:

* **Lazily, in the background.** The message list's item delegate asks for an
  icon (`SenderBadge::iconKey`, `SenderIconCache::Request`) when it paints a
  row whose badge has none cached, and the reading pane when it shows such a
  sender; up to three loader threads of the cache's own fetch them and the
  app is told of each icon stored (`SetReadyHandler`), whereupon the waiting
  rows and the pane show it. Neither the sync nor the window waits for a
  download, and only senders actually on screen are fetched. (Until 0.10.32
  the sync worker fetched a registry icon as each new header arrived, holding
  the sync for the round trip.)
* **Website icons only for mail that passed the scan.** A website icon is
  asked for only when the message has been scanned and is clean or an
  advertisement, is not in the junk folder, and its domain is not a mailbox
  provider (a friend's Gmail address is not Google's mail). Reading a site's
  home page tells the sender's web server that someone looked — not which
  message — which is why it is a setting of its own and never done for spam,
  scams or the junk folder.
* **No icon on a spam or scam badge.** An icon is drawn without the badge's
  frame, and on a dangerous message the frame is the warning: a forged
  `paypal.com` address must not wear PayPal's logo.
* **The fetch is injected.** The engine has no network dependency of its own:
  `SetFetcher()` takes the HTTPS GET for an icon (the app supplies one built
  on `UltraNet_HttpGet`, TLS verified, 10 s timeout, 512 KB cap),
  `SetPageFetcher()` the one for a home page (256 KB, and a page cut off there
  is still read: its head comes first); the test suite supplies fakes, and a
  build that sets none downloads nothing.
* **A miss is remembered** in a `.missing` marker and not retried for a week,
  nor a second time in one session, so an offline machine does not spend its
  time on the same failures.
* **Bytes that are not an image are rejected** (a captive portal's HTML login
  page, an error page), and a file the image loader cannot decode falls back to
  the monogram rather than leaving an empty square.

No icon is a normal state, not an error: the badge then shows the sender's
initial in the brand's own colour. **Settings → Privacy → Sender icons** has
both switches: *Download the icons of known senders* (`fetch_sender_icons` in
`preferences.ini`; off, nothing at all is downloaded) and *Show other senders'
website icons* (`fetch_site_icons`); icons already in the folder keep being
shown.

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
exists) can never reset a scan that has already run. A verdict made by older
rules (`kThreatRulesRevision`) is made again: by the sync, a batch of up to
300 cached bodies per folder sync (`SyncEngine::RescanStaleVerdicts`), and
when the message is opened.

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
| `auth-failure` | 30 | The receiving server's `Authentication-Results` reports `dmarc=fail` — or, with no DMARC result, an SPF or DKIM failure while nothing passed for the From domain |
| `sender-domain-lookalike` | 45 / 50 | The From domain is dressed up as a brand's — see *Look-alike sender domains* below. 45 for the name padded with phishing words, 50 for a misspelt name or a brand's own domain in front of a foreign one |
| `link-domain-lookalike` | 40 / 50 | A link goes to a domain dressed up as a brand's — the same check as `sender-domain-lookalike`, once per host, not for the sender's own domain |
| `government-impersonation` | 45 / 30 | A government agency or international organisation (FBI, Interpol, IMF, UN …) claimed from an address that is not a government one: 45 in the sender's name or domain, 30 in the subject or text when it addresses the reader about money, a case or an arrest |
| `romance-scam` | 22 / 35 / 50 | A stranger's love letter — see *Romance scams* below. 50 with five signs or more, 35 with four, 22 with three |
| `crypto-wallet-secret` | 50 | A sentence asking for a wallet's recovery (seed) phrase or private key — "verify your 12-word recovery phrase" — and not one warning never to give it |
| `advance-fee-fraud` | 25 / 50 | A sum in the millions ("US$ 15,500,000", "10.5 million dollars") plus a dead relative / estate / money nobody claimed ("abandoned baggage"), taxes or fees to pay first, or the 419 setting (barrister, Nigeria, "strictly confidential", "your own share", "God fearing", an "ATM card" from the FBI) — 50 when two of those appear |
| `crypto-payment-demand` | 40 | A crypto wallet address in the text (Bitcoin `bc1…`/`1…`/`3…`, Ethereum `0x…`, TRON `T…`) — blackmail and fake invoices; not from a proven exchange, not a token inside a link |
| `crypto-investment-lure` | 35 | Crypto plus promised profit: "guaranteed returns", "30% daily", a "trading platform", "withdraw your balance"; not from a proven exchange |
| `credential-request` | 30 | "Your account will be suspended" + a link off the sender's domain |
| `attachment-double-extension` | 25 | `invoice.pdf.zip` |
| `link-punycode`, `link-nonascii-host` | 25 | Hosts drawn to look like familiar names |
| `insecure-login-link` | 20 | A sign-in link over plain `http://` |
| `link-brand-mismatch` | 20 | A button labelled with a brand that goes somewhere else |
| `reply-to-mismatch` | 15 | Replies would go to a different domain than the sender's |
| `reply-elsewhere` | 15 | The text asks for answers at another free mailbox than the one it came from ("my contact email for us to proceed: x@yahoo.com") |
| `crypto-content` | 10 (0 from a proven exchange) | Any mention of cryptocurrency (bitcoin, BTC, USDT, a wallet, a seed phrase …): alone it changes no verdict, but the reading pane shows its caution strip |
| `link-shortener` | 12 | The destination cannot be seen |
| `many-foreign-domains` | 8 | Five or more link domains, none the sender's |
| `spam-flag` | 40 | The receiving server already said so |
| *(verified sender)* | −10 | DMARC passed for the From domain, or a DKIM signature of it verified: the From address is genuinely theirs |

45 and above is **Scam**, 22 and above is **Suspicious**; below that, a message
with bulk markers is an **Advertisement** and everything else is **Clean**.

### Romance scams

A romance scam starts as a love letter from a stranger: a pet name, talk of
fate, a short self-introduction (name, age, divorced, a nurse in Russia), how
they came to write ("I saw your profile", "it is destiny"), a photo or two,
and a push to answer — often at a private address, or on a site that "only
verifies" with a bank card. Later mails of the same thread add assurances ("I
am for real", a "scan passport") and the request: a ticket, a visa, the rent,
a laptop for the webcam, Western Union, crypto. No one phrase gives it away,
so `romance-scam` counts **kinds of signs**, each kind once:

| Kind | For example |
|---|---|
| Pet names | "my dear", "honey", "kisses", "truly yours" |
| Love or attraction | "true love", "future husband", "I am attracted", "drawn to you" |
| An offer of sex or a meeting | "meeting up", "escorts", "your desires" |
| A self-introduction | "my name is", "I'm 31 years old", "divorced", "I live in Russia", "Im lawyer", "I have blue eyes" |
| How they came to write | "your profile", "dating site", "destiny", "are you real?", "among the millions", "are you still looking for friend?" |
| A site to sign up on or be "verified" at | "get my number … login there", "they never charge", "criminal history" |
| Guilt or pressure | "don't upset Shui98 or make her bored" (a screen name speaking of herself), "no playing fool" |
| A pretended acquaintance | "so good to see you again", "we emailed each other a long time ago", "did you get my …" |
| Assurances of being real | "I am for real", "honest to you", "scan passport" |
| Photos, a push to write back | "two pictures", "hope you like them"; "write me at", "await your earliest response" |
| A request for money | "send me the money", "Western Union", "pay my rent", "a cheap laptop", any crypto |
| A hardship or far-away story | "my old mother", "Luhansk", "oil rig", "deployed" |

Two more signs count: **a photo** — an attached picture, or one shown in the
body — which these letters nearly always carry, and a sender at a **free
mailbox** (gmail.com, hotmail.com, i.ua, ukr.net, mail.ru …).

The rule fires only when the letter has both **something romantic** (pet
names, love, sex, or a guilt trip) and **something only a stranger writes**
(an introduction, how they "found" you, a site to sign up on, assurances of
being real, or a guilt trip). A partner's "my dear, here are the photos,
write back" has the first and not the second, and stays clean. It never
fires for a proven brand's mail (a dating service writing about matches), a
newsletter from a domain of its own, a job application ("job posting", "my
CV") or a mail over 60 000 characters. Three signs make the message
suspicious, five a scam. The tests are real letters one reader received
between 2011 and 2019 (`Tests/UltraMail/test_threatscan.cpp`).

### Cryptocurrency

Any message that mentions cryptocurrency gets `crypto-content`: 10 points
(none from a proven registry exchange), which alone changes no verdict but
puts the caution strip above the body. Three patterns are scams outright and
weigh accordingly — a request for a wallet's recovery phrase
(`crypto-wallet-secret`), a wallet address to pay into
(`crypto-payment-demand`: the "I recorded you through your camera" blackmail,
fake invoices) and promised profit (`crypto-investment-lure`: the
"investment platform" a new online acquaintance recommends). A romance letter
that turns to crypto counts it as its request for money.

### Look-alike sender domains

`BrandImitatedByDomain` (`UltraMailSenderBrands`) reads a domain the way a
hurried eye does — the From domain (`sender-domain-lookalike`) and every
link's host (`link-domain-lookalike`). It flags a domain that is none of a
brand's own in four ways:

* **The name padded with phishing words** — the brand's name or keyword
  plus only words such as secure, login, verify, account, support, inbox,
  billing, update, service, id: `paypal-secure-login.com`,
  `appleidverify.com`. A name beside any other word is a business of its
  own (`applewood-estates.com`, `amazonas-reisen.de`,
  `paypal-community.com`) or the brand's own second domain (`redditmail.com`,
  `cdn.discordapp.com`, `zoomcare.com` is a clinic), and the bare name under
  another suffix (`paypal.xyz`) is left unclaimed, as the brand table leaves
  it.
* **The name misspelt** — look-alike characters (`0` for o, `1` or `i` for
  l, `3` e, `4` a, `5` s, `7` t, `rn` m, `vv` w: `amaz0n`, `paypa1`,
  `rnicrosoft`), a doubled letter (`paypall`, `faceebook`), or — for names of
  eight letters or more only, so that `telecom` is not Telekom and
  `interact` not Interac — one letter added, dropped, changed or swapped
  (`facebok`). Padded or bare: `faceebookinbox.biz`, `faceboook.com`.
* **A brand's own domain in front of a foreign one** —
  `paypal.com.account-check.ru`.
* **The name in letters of another script** — `pаypal.com` with a Cyrillic
  "а", which travels as `xn--pypal-4ve.com`. Punycode labels are decoded
  (`DomainToUnicode`, RFC 3492) and every Cyrillic, Greek, accented or
  full-width letter that looks like a Latin one is read as that letter; a
  domain whose reading is a brand's own domain, or its name bare or padded
  with any word, was written to deceive. A domain with a letter that has no
  Latin look-alike (`москва.рф`, `東京.jp`) is no imitation, nor is one that
  reads as an ordinary word (`münchen.de`) or as a mailbox provider.

A misspelt or foreign-lettered name may be padded with ordinary words too
(mail, app, online, my, shop …: `faceboookmail.com`), since no brand
misspells itself.

Each label is read on its own, so `uncollatednessi.faceebookinbox.biz`
finds Facebook in its second; a bare name as a host label is no claim
(`hermes.uni-example.de` is a server called Hermes). Personal mailbox domains
and every brand's own domains are never flagged.

### Letters in an agency's name

The "compensation for scam victims", "your ATM card" and "warrant for your
arrest" letters write in the name of an agency or an international
organisation. `government-impersonation` knows about twenty of them — the
FBI, Interpol, the IMF, the United Nations, the World Bank, Europol, the CIA,
Homeland Security, the Department of Justice, the US Treasury, the Federal
Reserve, the Secret Service, the IRS, the European Central Bank and
Commission, the BKA, the Bundespolizei, Scotland Yard, the National Crime
Agency, the Central Bank of Nigeria, ECOWAS — and flags a message that
claims one but was sent from an address that is not a government one: in
the sender's name or domain it is a claim on its own; in the subject or the
text it counts when it addresses the reader about money, a case or an
arrest ("beneficiary", "your payment", "compensation", "ATM card", "arrest
warrant", "this office", "we the …"), so a news item about the FBI does not.
A government address — `.gov`, `.mil`, `.int`, a country's `gov.xx` /
`gob.xx` / `gouv.xx` / `govt.xx` / `go.xx`, `bund.de`, `admin.ch`, `gv.at`,
`gc.ca`, `europa.eu`, `police.uk` — or an agency's own domain (`imf.org`,
`un.org`, `worldbank.org`, `bka.de`) never is, and neither is a newsletter
from a domain of its own. Where the brand table already reported the claim
(the IRS, HMRC) it is not said twice.

### Switching warnings off

*Settings > Warnings > Spam/scam warnings* has one switch per kind
(`ThreatScanOptions`, `warn_*` in `preferences.ini`), all on:

| Switch | Findings |
|---|---|
| Phishing | `link-*`, `brand-impersonation`, `sender-domain-lookalike`, `borrowed-brand-pictures`, `credential-request`, `insecure-login-link`, `auth-failure`, `reply-to-mismatch`, `many-foreign-domains` |
| Romance scams | `romance-scam` |
| Advance-fee letters | `advance-fee-fraud`, `reply-elsewhere` |
| Letters in an agency's name | `government-impersonation` |
| Cryptocurrency scams | `crypto-wallet-secret`, `crypto-payment-demand`, `crypto-investment-lure` |
| Any mail about cryptocurrency | `crypto-content` (the caution strip) |
| Dangerous attachments | `attachment-*` |
| Spam, as the server marked it | `spam-flag` |

A kind switched off is not reported: its findings are dropped and add
nothing to the score, so a message is labelled as if those rules did not
exist. Changing a switch marks every stored verdict stale
(`LocalStore::MarkVerdictsStale`): the open message is scanned again at once,
and the rest are re-scanned by the mail check a batch at a time, the first
check starting straight away.

### Mail authentication

The receiving server checks the sending domain's own records when a message
arrives and writes what it found into an `Authentication-Results` header
(RFC 8601): **DKIM** (a signature by the domain over the message), **SPF**
(whether the delivering server is one the envelope sender's domain allows) and
**DMARC** (whether the From domain's own policy was met by an aligned DKIM or
SPF pass). The scan reads the **topmost** such header only — the one the
user's own server prepended; one further down may have been written by anyone,
the sender included (`TopHeaderValue`, `ParseAuthenticationResults`).

A sender is **verified** when DMARC passed for the From domain, or a DKIM
signature by the From address's registrable domain verified — a mail
service's own signature (`sendgrid.net`) proves nothing about the From
address (`VerifiedSenderDomain`). Verified is not the same as harmless: a
fraudster can sign for a domain of their own. So a verified sender keeps
every rule that catches a lie, and loses only the ones that misfire on
genuine mail:

* `link-target-mismatch` when the text names the sender's own, verified site
  and the link goes through its mail service's click tracker;
* `reply-to-mismatch` (replies to a help desk on another domain) and
  `many-foreign-domains`;
* for a verified **registry brand** (the From domain is the brand's own, e.g.
  `paypal.com` proven by DKIM), also `credential-request` and
  `link-brand-mismatch` — the bank asking to update account details is the
  bank.

A look-alike domain that signs its own mail (`paypa1-alerts.example`) still
trips `link-target-mismatch`, `brand-impersonation` and `credential-request`.
The verified domain and how it was proven are stored with the verdict
(`message_security.verified_domain` / `verified_by`, schema 10) and named in
the badge's and the sender's tooltips ("✓ Verified sender: paypal.com (DKIM
signature and DMARC)").

The reading pane shows the checks beside the sender as small bordered labels
— **[DMARC] [DKIM] [SPF]**, green when passed, red when failed, grey with no
verdict — each with a tooltip saying what was checked, for which domain, what
that proves and which server checked it (`DescribeAuthentication`). A message
the server recorded no checks for shows a grey **[Not checked]**; that is not
a warning. A message signed by its author shows **[S/MIME]** or **[OpenPGP]**
in grey: the signature is detected (`MessageSignatureKind`) but not yet
verified, so it counts for nothing.

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
| Stored verdicts (`message_security`, schema 4; verified sender, schema 10; finding codes, schema 11) | `Apps/UltraMail/engine/UltraMailLocalStore.{h,cpp}` |
| Scan at download time | `Apps/UltraMail/engine/UltraMailSyncEngine.cpp` (`WriteBody`) |
| The badge itself (painting + element) | `Apps/UltraMail/ui/UltraMailSenderBadge.{h,cpp}` |
| Badge column in the message list | `Apps/UltraMail/ui/UltraMailMailView.cpp` |
| Badge + warning strip in the reading pane | `Apps/UltraMail/ui/UltraMailMessagePreview.cpp` |
| Colours | `Apps/UltraMail/ui/UltraMailTheme.h` (`kTrust*`) |
| The warning switches | `Apps/UltraMail/ui/UltraMailSettingsDialog.cpp` (Warnings > Spam/scam warnings), `UltraMailPreferences.{h,cpp}` (`scamWarnings`), applied in `UltraMailApp.cpp` |
| Tests | `Tests/UltraMail/test_senderidentity.cpp`, `test_threatscan.cpp`, `test_contacts.cpp`, `test_localstore.cpp`, `test_preferences.cpp` |
