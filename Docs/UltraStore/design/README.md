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
| `PhoneHome.dc.html`, `PhoneDetail.dc.html` | the same two screens on a phone, with a bottom tab bar | 390 × 844 |
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

The `Super*` pages are a **proposal** for a third tier, later: the operator
of UltraStore running a marketplace in which other publishers (merchants)
open shops. Where its records live is settled in the store's README,
*The operator tier*. The operator verifies merchants,
reviews and releases their submissions with the catalog publish, handles
reports, warns, suspends and reinstates merchants and apps, and keeps every
merchant on the current terms. The dark sidebar marks that tier; every
merchant, user, report and figure on those pages is invented sample data.
