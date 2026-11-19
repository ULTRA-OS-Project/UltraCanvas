# UltraStore and UltraStoreAdmin: the visual design

The screens the store window, the web shop and the admin backend are built
to look like. They were drawn on a Design canvas
(<https://claude.ai/artifact/EUPtgSBTfSLY3QYWhaVTSg>) and these are that
canvas's own source files, copied here so every checkout carries them. The
canvas is the editable original: when it changes, copy its `project/` files
over these.

## What is here

| File | Screen | Size |
|---|---|---|
| `canvas.json` | the index: every artboard's frame on the canvas, the notes | |
| `Main.dc.html` | the shop's Discover (home) page: featured app, categories, Editor's picks, New and updated, Free essentials, Why UltraStore | page, 1440 wide |
| `AppDetail.dc.html` | one app's page (UltraVideo): badges, Try / Buy, screenshots, About, What's new, What it installs, information, more from the publisher | page |
| `Categories.dc.html` | Browse by category: the category chips and the price filter work (state in the script) | page |
| `PhoneHome.dc.html`, `PhoneDetail.dc.html` | the same two screens on a phone, with a bottom tab bar; the reference for the web store (`Apps/UltraStore/web`) at phone width, where the header wraps and the cards go to one column | 390 × 844 |
| `AdminDashboard.dc.html` | the admin backend's Overview: stat tiles, revenue by week, top apps, latest sales, last publish, translations, keys | page |
| `AdminApps.dc.html` | Apps and components: the *Add an app or a component* panel and the table of every record | page |
| `AdminAppEditor.dc.html` | the form of one app: identity, store settings, texts per language, pictures, releases and packages | page |
| `AdminSales.dc.html` | the sales ledger with its filters, totals and *Record a sale by hand* | page |
| `AdminFinance.dc.html` | the monthly statement, net by month, revenue by app, the affiliate report, VAT by country, the archive | page |
| `AdminPublish.dc.html` | Publish: the changes since the last sequence, the check's warnings, sign and publish, upload, history | page |
| `SuperDashboard.dc.html` | the operator's (super admin's) dashboard: merchants, review queue, reports, compliance, operator actions | page |
| `SuperMerchants.dc.html` | the merchants: verification requests, the table with terms, warnings, reports, payouts and status | page |
| `SuperMerchantDetail.dc.html` | one merchant's file: apps, history, compliance, money, open reports, with the Suspend dialog open | page |
| `SuperReview.dc.html` | app review and release: the queue, the automatic checks, the reviewer's checklist, the store card, the decision | page |
| `SuperReports.dc.html` | reports from users and merchants about apps and merchants, one opened with findings and actions | page |
| `SuperTerms.dc.html` | the merchant terms by version and who accepted, the enforcement policy, warnings by clause, notices sent, send a notice | page |
| `GamesHome.dc.html` | the games shop variant's Discover: game of the week, the genre tiles, Editor's picks with covers, New releases, Learning games by age, Retro corner and Board games, Free to play, why play from UltraStore | page |
| `GamesDetail.dc.html` | one game's page (Skyline Siege): genre and PEGI badges, players and controller, Play the demo / Buy, trailer and screenshots, About, the rating explained, What's new, System requirements, What it installs, information, more tactics games | page |
| `GamesGenres.dc.html` | Browse games by genre: the genre chips, the price, the age rating, the players filter work (state in the script); covers carry the PEGI mark | page |
| `GamesPhoneHome.dc.html` | the games shop's Discover on a phone: game of the week, genre chips, Editor's picks, Learning games, the bottom tab bar | 390 × 844 |
| `MarketHome.dc.html` | the marketplace variant's home: the search-first header with the department select, the delivery address and the basket, the department strip, the sale banner, Today's deals with the countdown and the claimed bar, Shop by department, Best sellers with ratings, Top sellers, New arrivals, why buy here | page |
| `MarketProduct.dc.html` | one product's page (Nordlicht Aura 2): the gallery, the rating, the price with the regular price, colour variants, the buy box with delivery and the seller, other offers, product details, Compare with similar items, customer reviews with the distribution and a seller's reply, questions and answers, Customers also bought | page |
| `MarketSearch.dc.html` | search and browse: the department chips with counts, the price, the customer rating, the delivery and the condition filters work and show as removable active filters (state in the script); listings with the seller, the delivery and a sponsored mark | page |
| `MarketBasket.dc.html` | the basket grouped by seller with the quantity, Save for later and the delivery choice per seller, the free-delivery threshold, Saved for later, the order summary with the voucher, the payment methods and Proceed to checkout; the checkout steps in the header | page |
| `MarketPhoneHome.dc.html` | the marketplace's home on a phone: delivery address and basket, search, department chips, the sale, Today's deals, Best sellers, Buy again, the bottom tab bar | 390 × 844 |
| `VideoHome.dc.html` | the video portal variant's home: the sections rail with subscriptions and Explore, topic chips, the video grid with durations, the watched bar and a live viewer count, Shorts, From your subscriptions, why watch here | page |
| `VideoWatch.dc.html` | the watch page: the player with chapters on the seek bar, captions, quality, speed, picture in picture and theatre, the channel row with Subscribe and Support, like, share, download and save, the description with chapters and the licence, comments with a pinned one and the creator's reply, up next with autoplay, the transcript | page |
| `VideoChannel.dc.html` | a channel (ULTRA OS Project): banner, avatar, handle and counts, Subscribe and Support, the tabs, search in the channel; the sort (Latest, Popular, Oldest) and the kind (All, Videos, Live, Shorts) filters work (state in the script); an upcoming stream and a short in the grid | page |
| `VideoStudio.dc.html` | the creator's Studio: the channel figures, an upload in progress with the format check, chapters from UltraVideo's markers and captions from the subtitle track, the details form (title, description, thumbnail, playlist, language, visibility, audience, licence), the videos table with visibility, views, comments, likes and a copyright claim | page |
| `VideoPhoneWatch.dc.html` | the watch page on a phone: the player on top, title, channel and Subscribe, the action chips, the current chapter, comments, up next, the bottom tab bar with Upload | 390 × 844 |
| `MusicHome.dc.html` | the music streaming variant's home: the library rail with playlists, artists, podcasts and downloads, the jump-back-in tiles, mixes made for you, new releases, the charts with movement, podcasts with the position, why listen here; the now-playing bar | page |
| `MusicAlbum.dc.html` | an album (Low Tide): the cover and facts, play, shuffle, saved, downloaded, the artist's share of the subscription, the track table with plays, likes and durations, the credits and the quality, more by the artist | page |
| `MusicArtist.dc.html` | an artist (Kesselhaus Quartet): the header with monthly listeners, follow and support, popular songs; the discography's kind (albums, singles and EPs, live, appears on) and sort (newest, oldest) filters work (state in the script); about, on tour, fans also like | page |
| `MusicSearch.dc.html` | search: the type (songs, artists, albums, playlists, podcasts) and genre filters work over a sample result set (state in the script), the top result with songs, the result grid, browse all by genre | page |
| `MusicPhoneNowPlaying.dc.html` | Now playing on a phone: the cover, title, like, the position, shuffle, previous, play, next, repeat, the playing device and quality, share and queue, lyrics | 390 × 844 |
| `AccountSignUp.dc.html` | sign in or create an account: the brand panel, the steps (account, verify, profile, done) work in the script - email and password with the strength check, country and year of birth, consent; the email code; the profile basics and what the account keeps; the welcome with two-factor as the next step; sign in with a passkey | page |
| `AccountProfile.dc.html` | the account page: the account rail; profile (avatar, display name, handle, the private email, language, country, year of birth, about); security (password, two-factor with recovery codes, passkeys, sign-in history); devices with the licence seats they hold; notifications; privacy and data (history on the account, public profile, the data archive, delete the account) | page |
| `AccountBilling.dc.html` | payment methods (a reference to the method, the billing address, VAT); subscriptions (switch, cancel, resume); purchases and licences with seats and *Install on a machine*; orders and invoices (PDF, XRechnung, returns, credit notes) | page |
| `AccountPhoneProfile.dc.html` | the account on a phone: the profile head, the Premium card, the account sections, this device, sign out, the bottom tab bar with Account | 390 × 844 |
| `AccountRecovery.dc.html` | reset the password (email, the email code and the two-factor code, the new password, done) and set up two-factor (the app, the QR code or the key, the recovery codes, on) - both step flows work in the script; the delete-account confirmation with what happens | page |
| `SuperUsers.dc.html` | the operator's users: the counts with the data-archive and deletion requests, the table with a working status filter (active, suspended, deleting, with a request), one user's file with history and the actions (notice, sign out everywhere, reset two-factor, archive, suspend or reinstate, delete on request) and what the operator cannot see; a line under the title says what of it the account service backs | page |
| `AccountPhoneSignIn.dc.html` | sign in on a phone: email and password, a passkey, or confirming on a signed-in machine nearby | 390 × 844 |
| `BooksHome.dc.html` | the e-book and audiobook shop variant's home: the book of the month in both editions with the sample, continue listening and reading with the place kept between the editions, the genres, the editor's picks with the length badge, Books Plus (one credit a month, the book kept), a series with the next part on pre-order, short listens for tonight, why read and listen here | page |
| `BooksDetail.dc.html` | a title (The Salt Garden): the cover, the sample and the first chapter, the narrator, ratings for narration and story, the chapters both editions share, reviews that say listened or read to the end; the editions (audiobook for a credit, e-book, both kept in step), buy, the return rule, and what of the price goes to the author, the narrator, the publisher and the store | page |
| `BooksBrowse.dc.html` | browse: the format (audiobooks, e-books, both editions), genre, language and *Included with Plus* filters work over a sample catalogue (state in the script), the cover grid with length and price | page |
| `BooksLibrary.dc.html` | your library: the sections rail (in progress, finished, on this machine, borrowed from Plus) with collections and the download folder, the titles in progress with the device the place was last kept on, bookmarks, highlights and the files, the table of all titles, this year's hours, pages and the authors' share | page |
| `BooksPhoneListen.dc.html` | listening on a phone: the cover, the chapter picker, the position in the chapter and what is left in the book, 30-second skips, previous and next chapter, speed, the sleep timer, bookmark, *read here* in the e-book, downloaded and the place kept with the laptop | 390 × 844 |

Each `.dc.html` is one self-contained HTML page. Every colour, size,
radius and font is inline on the element, so read the file next to the
widget you are building and take the values from there. The
`<script type="text/x-dc">` block at the end holds the page's data and
behaviour; `{{accent}}` is the one tweak, the accent colour.

The files are the canvas's format, not plain web pages: they need the
canvas's `support.js` to render, so open the canvas to see them drawn.
Share › Export on the canvas gives a PNG or PDF of any artboard.

## The look

- **Ground** white `#FFFFFF`; the admin pages sit on `#F8FAFC` with white
  cards. Ink `#0F172A`, body text `#334155`, muted `#64748B`, lines
  `#E2E8F0`, soft surface `#F1F5F9`.
- **Accent** UltraStore blue `#2563EB`, taken from `media/appicon/UltraStore.svg`;
  pressed `#1E40AF`; soft tint `#DBEAFE` with `#1D4ED8` text for secondary
  buttons and the active navigation item.
- **State colours** installed / published green `#15803D` on `#DCFCE7`;
  warning `#92400E` on `#FEF3C7`; refunded / delete red `#991B1B` on `#FEE2E2`;
  beta purple `#5B21B6` on `#EDE9FE`.
- **Type** Manrope 800 for titles and app names (Google Fonts), Inter for
  everything else; 15 px body in the shop, 14 px in the admin backend,
  13 px in tables.
- **Shapes** cards 20 px radius with a 1 px `#E2E8F0` border, admin cards
  16 px; app icons at 22 % radius (64 px icon → 16 px, 160 px → 36 px);
  buttons are pills, 40 px tall in lists, 44 px for chips and inputs,
  52 px for the page's main action; 44 px is the smallest touch target.
- **Layout** a 1280 px centred column with 32 px side padding on the shop;
  the admin backend is a 240 px sidebar beside the content, both wrapping
  under 800 px. Sections are 48 to 56 px apart on the shop, 20 px in the
  admin backend.

## The pictures

The app icons in the files are the canvas's uploaded copies of
`media/appicon/*.png`, named by blob id:

| Blob id in the files | Icon |
|---|---|
| `9b931f0bda5f6ee1d30304d2295e0e4a` | UltraStore |
| `023cd138f38912c764ae7c221c087db6` | UltraVideo |
| `451050dafeeb42579ba048b1a08f78fa` | UltraPaint |
| `0d8f1d55bd369843755da72b7a7fbd33` | ArtCreator |
| `05ae627f527f163025af07e9a4228fc4` | UltraViewer |
| `693816119a6f2ec456eb26c1c05fd97a` | Texter |
| `0339854b95aaa1785eff13f10f2fce1c` | UltraFiler |
| `eb53611555a40a4f89409ce13e6ce00c` | AnchorPoint |
| `9339098138c28559209f05450b47c180` | UltraWeb |
| `9f46fbb5f61e6ff5f3160c323225c3d3` | UltraPress |
| `bafbc6e21d0bfb6f2d3d60d6d272ad66` | UltraMail |
| `d919f5092ad77a08c45dde5ff8c48797` | UltraSocial |
| `a59673ce6edcb56f9629a3733d04bc3c` | UltraPassword |
| `ee8d25a781cc2a803bc6dce46ffb3a8e` | UltraAuthenticator |
| `0ff3e14cb1fc27dfb3ebc15b0315f22f` | UltraClaude |
| `c5402e04d2949ef467a6e80deabd4144` | UltraFIBU |
| `90eb60090b672bec83f5ce0aa977c5a0` | UltraDesktop |
| `af5fce512947c6e87522bb7e0ef04080` | UltraClipboard |
| `51239c2aff5d2daea6499e95b26434cd` | UltraCleaner |
| `94e5f60f5056aac33b2e136d329737b3` | UltraNetMonitor |
| `2e6a1f53164c10cfa6769251b9f13925` | DeviceExplorer |
| `fdf2f8dafee1b10effdd57f30c2e4037` | EmailCleaner |
| `f4ba4727fc2023735981031b16e08964` | UltraAI |
| `2a23005da8e1e896df09b451fcedae79` | UltraSnip |
| `b4d3985fdfe86bd3ea2fcdd94dcd3425` | ChatCube |

The screenshots are drawn placeholders (dark window mock-ups built from
`div`s); real screenshots replace them.

## What is real and what is sample

The apps, their versions, categories and summaries come from
[`UltraOSApplicationOverview.md`](../../UltraOSApplicationOverview.md);
UltraVideo's price, trial, update period, dependencies and package sizes
are the example catalog in [the store's README](../README.md). Everything
else that looks like a number — other apps' prices (`[PRICE]`), release
dates, order counts, revenue, partner credits, VAT, fees — is sample data
for the layout. The store shows no star ratings because the catalog has no
rating field. VAT by country and the statements archive on the Finance
page are proposals beyond the admin backend's README; the rest of the admin
pages follow its *The window* table.

The `Games*` pages are the **games shop variant** of the same design: the
shop's chrome, type and shapes with the accent violet `#7C3AED` (the
`accent` prop; UltraStore blue is one of its options), covers in the
genre's gradient with a two-letter mark where the apps have icons, and the
genres Action, 3D, Simulation, Tactics, Adventure, Learning (education, by
age band), Retro, Board games, Puzzle, Racing, Role-playing and Sports.
The game page adds what a game needs beyond an app: the PEGI rating with
its content descriptors, players and local co-op, controller support, a
trailer, system requirements (minimum and recommended), a demo as the
free action, and the campaign content as a layer bought after the demo.
The repository holds no games-shop proposal: the catalog's fixed category
list has `games` (`Docs/UltraStore/README.md`) and nothing more, so
genre, age rating and descriptors, players, input and requirements are
proposed catalog fields. Every game, studio, count, date and `[PRICE]` on
those pages is invented sample data; none of them exists.

The `Market*` pages are the **marketplace variant**: the shop as a general
marketplace in the manner of Amazon, eBay or Temu, with many verified
sellers (the merchants of the store README's marketplace tier) in one
basket, physical goods with delivery and returns, star ratings from
delivered orders, deals with a countdown, departments instead of
categories, and a basket grouped by seller with the checkout steps. The
accent is orange `#EA580C` (the `accent` prop). The words are the standard
string set's `media/locale/en/base/Shopping.json` (Basket, Add to basket,
Proceed to checkout, Free shipping, Estimated delivery, Deal, Best
sellers, Guest checkout, Voucher code, Compare, In stock, Low stock);
payment is the store's own, as [the store's README](../README.md)
*Payment* decides it: the payment service `Server/UltraStorePay` and
Skrill Quick Checkout's hosted page (cards, the Skrill wallet, local
methods), card data never in the marketplace, with the seller's invoice
on every order. These variants are UltraStore's own business, not
UltraPress's shop module. Nothing of
this exists in the catalog or the services yet: products, sellers,
prices, ratings, stock, delivery and every figure are invented sample
data, and the pictures are drawn `[PHOTO]` placeholders.

The `Video*` pages are the **video portal variant**: the shop's chrome as
a portal in the manner of YouTube or Vimeo, with channels, subscriptions,
uploads, live streams, Shorts and comments, the accent red `#DC2626` (the
`accent` prop). What exists in the repository: the UltraCanvas video
element plays a file or a URL through GStreamer, Media Foundation or
AVFoundation, with poster frames and the codec registry that says what a
build decodes ([`UltraCanvasVideo.md`](../../UltraCanvas/UltraCanvasVideo.md),
[`UltraCanvasMediaCodecRegistry.md`](../../UltraCanvas/UltraCanvasMediaCodecRegistry.md)),
and UltraVideo exports MP4 and WebM, which the Studio's upload builds on
(chapters from the timeline's markers, captions from the subtitle track).
What does not exist: a portal service with channels, uploads, renditions,
comments, subscriptions and support payments, which would go through the
store's own payment service. The portal is UltraStore's own business, not
UltraPress's. Every channel, video, count and figure is invented sample
data; the ULTRA OS Project channel is the team's own, its videos invented;
the pictures are drawn `[THUMBNAIL]` placeholders.

The `Music*` pages are the **music streaming variant**: the shop's chrome
as a streaming service in the manner of Spotify, with the library rail,
mixes, albums, artists, playlists, podcasts, downloads and the now-playing
bar on every desktop page, the accent green `#16A34A` (the `accent`
prop). What exists in the repository: UltraCanvas Audio plays WAV, MP3
and FLAC always and OGG, Opus and AAC with the system codecs, gapless
through miniaudio, with the audio player element and the analysis for
visualisation ([`UltraCanvasAudio.md`](../../UltraCanvas/UltraCanvasAudio.md)),
and the store sells subscriptions (`monthly`, `yearly`) through its own
payment service, which is what Premium would be. What does not exist: a
streaming service with a catalogue, licences, renditions, mixes computed
on the machine, downloads and the artists' share. The service is
UltraStore's own business, not UltraPress's. Every artist, album, song,
count and figure is invented sample data; the covers are drawn `[COVER]`
placeholders.

The `Account*` pages are the **account** every shop on the canvas shares:
sign up, sign in and the profile, security, devices, notifications,
privacy, payment methods, subscriptions, licences and orders. **Built**
(2026-11-19, the store's README, *The account tier*): the account service
`Server/UltraStoreAccount` holds the person, the sessions as devices, the
second factor, the licences with their seats, the notices, the archive and
the deletion; the store's window has the *Sign in* pill, the sign-in page
with its steps and the account page with its rail
(`Apps/UltraStore/ui/StoreAccountPage.cpp`); the operator's window has the
*Users* page (`Apps/UltraStoreOperator/ui/OperatorUsersPage.cpp`). What the
pages draw that is not built: passkeys (WebAuthn needs the platform's
authenticator), the avatar, the sign-in location, and the payment methods,
subscriptions and orders pages - those are the payment service's, which
does not yet record the account at checkout; the account page shows the
licences it holds instead. An account service ties licences and their
seats, subscriptions and the library (wish list, playlists, watch later,
saved games) to a person across machines; installing a free app still
needs none. The operator's `SuperUsers` page is the other side: accounts as
far as the store needs them, suspend and reinstate with a reason in the
audit log, the data-archive and deletion requests with their deadlines, and
never the password, the payment method or the listening history; the
artboard says on its face what of it the service backs. Two-factor sign-in
is UltraAuthenticator or any TOTP app, recovery codes in UltraPassword if
wanted; payment stays on Skrill Quick Checkout's page through the store's
payment service, the account keeping only a reference to the method.
Customer messaging is a notice on the account, written by the operator or
the service and sent by mail; a merchant's message to a buyer is relayed
through the marketplace service later. Every person, device, order and
figure is invented sample data.

The `Books*` pages are the **e-book and audiobook shop variant**: the
shop's chrome as a book shop in the manner of Audible, with both editions
of a title, the place kept between listening and reading, a library of
files the buyer keeps, and Books Plus, the accent amber `#D97706` (the
`accent` prop). What exists in the repository: UltraViewer reads EPUB,
FB2, MOBI, PRC, AZW and AZW3 in `UltraCanvasEBookViewer` (the chapter
toolbar, the table of contents, reflowing content) and PDF in
`UltraCanvasPDFView`
([`UltraCanvasMediaViewer.md`](../../UltraCanvas/UltraCanvasMediaViewer.md)),
UltraCanvas Audio plays MP3, WAV and FLAC always and M4A/M4B (AAC) with
FAAD2, fdk-aac or the GStreamer plugins
([`UltraCanvasAudio.md`](../../UltraCanvas/UltraCanvasAudio.md)), and the
store sells subscriptions (`monthly`, `yearly`) through its own payment
service and Skrill Quick Checkout, which is what Books Plus would be. What
does not exist: a book catalogue with editions, narrators, chapters shared
by the audiobook and the e-book, the place kept across machines, credits,
pre-orders and the publishers' share. The files are the buyer's: EPUB and
M4B without DRM, so UltraViewer and any other reader open them. The shop
is UltraStore's own business, not UltraPress's. Every book, author,
narrator, review, count and figure is invented sample data; the covers are
drawn `[COVER]` placeholders.

The `Super*` pages are a **proposal** for a third tier, later: the operator
of UltraStore running a marketplace in which other publishers (merchants)
open shops. Where its records live is settled in the store's README,
*The operator tier*. The operator verifies merchants,
reviews and releases their submissions with the catalog publish, handles
reports, warns, suspends and reinstates merchants and apps, and keeps every
merchant on the current terms. The dark sidebar marks that tier; every
merchant, user, report and figure on those pages is invented sample data.
