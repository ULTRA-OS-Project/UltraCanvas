# UltraAds — the advertising service of the ULTRA OS shops and services (Investigation and Proposal)

**Date:** 2026-11-24
**Status:** Proposal — no implementation of the advertising service yet.
The one sign-in of §4.6 is built (`Server/UltraStorePortal` 0.4.0, same
date); §4.6 says what of it stands and what remains.
**Scope:** a new service, **UltraAds**, with which a merchant advertises in
the shops and services of ULTRA OS - the app store, the games shop, the
marketplace, B2B trade, the video portal, music and podcasts, e-books and
audiobooks; the one registration and sign-in every shop and service shares;
the question whether a shop sells advertising for itself or for the whole
ecosystem; and the functions proposed, in the order they would be built.

This document follows the shape of the other investigations in this
directory ([`UltraCalendarDesignProposal.md`](UltraCalendarDesignProposal.md),
[`UltraPressDesignProposal.md`](UltraPressDesignProposal.md)). Everything said
about the repository was checked against the tree on this date; file
references are to the current `main`. What is said about other companies'
advertising products and about the law is from their published behaviour and
texts as the author knows them; a detail that could not be checked from this
environment is marked **[unverified]**, and §9 is a list for a lawyer, not
legal advice.

---

## 1. Recommendation

**Build one advertising service for the whole ecosystem, never one per shop,
and give it two doors: a *Promote* button inside every shop's seller pages,
which advertises in that shop by default and can be widened to the other
areas in the same form, and the UltraAds console for campaigns planned across
all of them. Sell only what the ecosystem itself lists - an app, a product, a
game, a video, a book - choose advertisements from what is on the page, never
from who is looking, and mark every one of them. Sign-in is the one ULTRA
account, which the account service starts speaking OpenID Connect for, so a
shop, a service and a merchant's own website all offer "Sign in with
UltraAccount".**

Why these are the right calls, in one line each; the rest of the document is
the evidence:

| Decision | Reason |
|---|---|
| One service, one advertiser account, one balance (§4.1) | An ad system per shop is seven wallets, seven reviews, seven reports and seven sets of rules for the same merchant. Every shop after the app store is a variant of one design on one portal (`Docs/UltraStore/design/README.md`); its advertising should be too. |
| Two doors onto it: *Promote* in the shop, and the console (§4.2) | The answer to "this shop only, or all systems?" is **both, from one form** (decided 2026-11-24): the form lists every shop and marketplace with its placements - search, lists, detail pages, the home row - with this shop's ticked and all the others there to tick. Where a campaign runs is what the merchant ticked, not where they clicked. |
| Closed loop first: an advertisement leads to a listing, not to a web address (§4.3) | The listing was already reviewed, so most ads need no second review; the sale happens in our checkout, so it can be counted without following anyone; and there is no landing page to turn into malware after approval. |
| A shop's search shows only that shop's goods; other areas get one marked row (§4.4, §4.5) | Someone searching the app store for a photo editor must never be answered with a toaster. Cross-area advertising is real, and bounded: a single row per page that says where it comes from. |
| The context, never the person (§4.7) | `VideoHome.dc.html` already promises "No tracking, no autoplay ads", and the privacy notes of UltraMail and UltraPress say "no advertising". Search words, category, language, country and platform are enough for a shop; a behavioural profile is the thing people leave other platforms over. |
| An auction with the operator's rate card as its floor (§4.8) | With three advertisers an auction alone gives the placements away; with a floor it behaves as a price list on day one and becomes an auction as demand arrives, without a second mechanism. |
| Prepaid balance through the store's own checkout (§4.10) | No credit risk, no dunning, and the top-up is an order the portal's `billing/` already knows how to sell. |
| The editor's pick is never for sale (§4.12) | `featured` (`Docs/UltraStore/README.md`, *The admin backend's settings*) is the store's own voice. A paid placement that could pass for it would spend the trust the mark "Sponsored" is there to keep. |
| No advertising in the system itself (§4.14) | Not in UltraDesktop, the notifications, the settings, UltraFiler, UltraMail or any application. Advertising belongs where people came to shop or to browse a catalogue. |
| One account that speaks OpenID Connect (§4.6) | "Like Google" is a protocol, not a page: the account issues a signed statement of who signed in, and anything - a shop under the same root, a service on another host, a merchant's UltraPress shop - can rely on it. UltraNet already has the client half (`UltraNet/UltraNetOAuth2.h`). |

**Do not** build a tracking pixel, a cross-site identifier or a third-party
ad exchange connection, and **do not** let a shop keep a balance or a review
queue of its own.

---

## 2. What was asked, restated

1. An advertising service - UltraAds, or UOS Ads - with which **merchants
   advertise in different areas of the ULTRA OS ecosystem**: app store,
   marketplace, games shop, B2B, video, podcasts, e-books and so on.
2. **One registration** gives a sign-in to every shop and service, the way a
   Google account does.
3. A question to answer: may a merchant, **from one shop**, advertise only in
   **that shop's own system**, or is advertising on **all systems** offered
   directly from there?
4. **Proposals for the functionality.**

Two things the request does not say and the design has to settle anyway:

- **Most of those areas are drawings today.** Only the app store exists as
  software. The games shop, the marketplace, trade, video, music and books
  are design variants with invented sample data (`Docs/UltraStore/design/`;
  the README there says of each what exists and what does not). UltraAds
  therefore cannot be written against seven shops; it has to be written so
  that a shop **registers** its placements and its kinds of goods when it is
  built (§4.4, §6).
- **Promises already made.** The video portal's home page says "No tracking,
  no autoplay ads"; the games page uses "no advertising" as a content
  descriptor; two privacy notes say an application "contains no advertising"
  (`Docs/Legal/Privacy/UltraMail.md`, `UltraPress.md`). §4.14 says which of
  these stay as written and what the one conflict is.

---

## 3. What the repository already has, and what it lacks

| Need | What exists | Where |
|---|---|---|
| A design or proposal for advertising | **Nothing.** The name appears in no file, branch, issue or pull request | - |
| A paid placement, drawn | One listing with a *Sponsored* mark and the line "Sponsored listings are marked" - sample data, no rule behind it | `Docs/UltraStore/design/MarketSearch.dc.html` |
| An unpaid placement | `featured`: the admin's pick, the Discover hero and *Editor's picks* | `Docs/UltraStore/README.md`; `App.featured` in `Server/UltraStorePortal/ultrastore_portal/store/models.py` |
| The surfaces | The app store as software; games, marketplace, trade, video, music, books as design variants | `Apps/UltraStore`, `Docs/UltraStore/design/README.md` |
| One server for the store | The portal: a Django project under one root, with the developer console, the operator's pages, the store's pages, buying and the JSON service; built to replace the three earlier services | `Server/UltraStorePortal/README.md` |
| A business that sells | `Publisher` with `PublisherMember` (roles *Owner*, *Developer*); one publisher per account; applied for, approved by the operator, suspended and reinstated | the portal's `store/models.py` and README |
| Review, reports, audit | The release review, `Report`, `AuditEntry` with who did what | the portal's `store/` |
| Taking money | `Order`, `Payment`, Skrill Quick Checkout, refunds, a test provider | the portal's `billing/` |
| Counting a sale back to its source | The affiliate code: kept by the store for 30 days, sent with the checkout, credited in the sale | `Docs/UltraStore/README.md`, *Affiliates and codes* |
| One account for a person | UltraAccount: "the one account a person has across ULTRA OS", the session shared by every native application on a machine | `Docs/UltraAccount/README.md`, `Server/UltraStoreAccount` |
| A second account store | The portal's own `accounts/` (the email as the sign-in name, a mailed link, a password of 8 to 64 characters, no second factor) | `Server/UltraStorePortal` |
| The client half of "sign in with ..." | OAuth 2.0 authorization code with PKCE, the loopback redirect, token refresh | `UltraNet/UltraNetOAuth2.h`, `Docs/Modules/UltraNet/README.md` |
| The server half | **Built since** (the portal's 0.4.0): an OpenID Connect provider in the portal, at an address of its own when one is set. When this was written neither account store issued a token to another service | `Server/UltraStorePortal/README.md`, *One sign-in for every service* |
| Tax invoices, the split of a sale, payouts | **Missing**, and named as the portal's next step but one | the portal's README, *What comes next*, 2 |
| The word *Sponsored* | **Missing** from the string sets, `Shopping.json` included | `media/locale/en/` |
| Charts for a dashboard | The Charts plug-in elements | `UltraCanvas/include/Plugins/Charts/` |

Two of these rows decide the order of the work. **Two account stores exist**
with different rules, and one registration is not true until there is one
(§4.6: the portal's is now the one other services sign in through; the
native applications still use the account service's). And **payouts and tax are not built**, so anything that pays a third
party from advertising - a creator's share (§4.13) - waits for them.

---

## 4. Design decisions

### 4.1 One service, not one per shop

Every shop on the design canvas is the same chrome with another accent colour
and another kind of goods, and the portal that serves the first of them is
built as one application under one root. An advertising system per shop would
repeat, for each: an advertiser's registration and verification, a balance
and its invoices, a review queue and its policy, a report and its export, and
the code that picks and counts. The merchant who sells apps and books would
hold two balances and read two reports that cannot be added up.

So: **one service, one advertiser record, one balance, one policy, one
review, one report.** What differs per shop is data, not code: which
placements it has, what kinds of goods it lists, what its context is made of
(a genre in the games shop, a department in the marketplace, a quantity tier
in trade). §4.4 makes that a registry.

The product is named **UltraAds**, as the applications are named
(UltraStore, UltraAccount); the *UOS-* prefix is used for the parts of the
system itself (UOS-Settings, UOS-Localisation), which this is not.

One practical warning about the name. Content blockers refuse addresses by
pattern, and `/ads/` is among the commonest patterns **[unverified against
the current lists]**. That is the viewer's right and nothing here works
around it - but a merchant who runs a blocker must still be able to open the
console and read a report. The addresses therefore say *promote*
(`/promote/...`, `/v1/promote/...`), not *ads*; §7.

### 4.2 Two doors: *Promote* in the shop, and the console

This is the question the brief asks, and the recommendation is **both, on one
campaign record**.

**The shop's door.** In a shop's seller pages - today the developer console's
app page (`/console/...`), later a marketplace seller's product list, a
channel's Studio, a publisher's book list - every listing that is public has
a **Promote** button. The form behind it has three things to fill in:

1. **Where** - the whole list, on this one page (decided 2026-11-24):
   **every shop and marketplace, and under each its placements** - in its
   search, in its category lists, on its detail pages, in its home row.
   **This shop's are ticked; every other one is listed and not ticked**, so
   the merchant sees at the first glance where else the listing could
   appear, and widens the campaign with a tick, without leaving the page.
   Each line says what an advertisement looks like there and what a click
   costs at the least. A place this kind of listing may not appear in (§4.5)
   is listed as well, greyed, with the reason in a few words - "trade shows
   business goods only" - so the list is the same for everyone and nothing
   in it is a surprise. *All of this shop* and *Everywhere it fits* tick a
   whole group at once.
2. **How much** - a daily budget and, optionally, the most to pay for a
   click. Left empty, the service bids for the merchant up to the budget.
3. **How long** - until stopped, or an end date.

Search words, categories and languages are taken from the listing itself, and
the advertisement is the listing's own card, so there is nothing to design
and nothing to review. That is a campaign in under a minute, in the place the
merchant already is.

**The console.** `UltraAds` proper: every campaign of the business, in every
area, with what the shop's door hides - several ad groups, search words with
their match rules and exclusions, a bid per placement, banners and fixed
slots, the reports, the balance, the invoices, the people who may spend. A
campaign started at a shop's door opens here unchanged; *Promote* is a preset
of the console, not a second system.

**Why not "this shop only".** A merchant with a new game wants the games
shop's search, and also the row on the video portal where people watch games
being played. Sending that merchant to a second product to buy the second
half is how advertising becomes something only agencies can do.

**Why not "always everywhere".** A seller of screws who clicks *Promote* in
trade must not find the budget spent on the books shop. Three protections:

- a campaign starts with *this shop* ticked and nothing else, and every
  further place is the merchant's own tick;
- outside its home shop a campaign spends **at most a share** of its budget
  (proposed: 30 %) until the merchant changes it;
- the report always splits by area, so what each one cost and brought is one
  glance.

### 4.3 What may be advertised: closed loop first

**An advertisement promotes something the ecosystem lists**: an app, a game,
a product, a supplier's profile, a video, a channel, an album, an artist, a
podcast, a book, a publisher's page. Its target is that listing's page, never
a web address the advertiser types.

What that buys:

- **Review once.** The listing went through the shop's review; its card as an
  advertisement shows nothing the review did not see. Only what an advertiser
  *adds* - a headline, a banner picture - is reviewed again (§4.11).
- **Counting without following.** The click lands on our page and the order
  is opened by our checkout, so "this sale came from that advertisement" is
  known by the same means the affiliate code uses (§4.9), with no pixel on
  anybody's website.
- **Nothing to swap.** The classic fraud is a clean landing page at review
  and another one after. A listing changes only through the shop's own
  rules.
- **Standing carries over.** A publisher who is suspended or behind on the
  terms is taken out of the catalog by the publish; the same state stops
  their campaigns.

Advertising something **outside** - a merchant's own website, an UltraPress
shop on another host - is a later class of its own (§10, phase 4): the
address verified as the advertiser's, the page reviewed, the click counted
and nothing after it.

### 4.4 Where: placements and formats

A **placement** is a named position on a page that may hold advertisements:
which area, which page, which format, how many, what the floor price is. A
shop registers its placements when it is built; UltraAds holds the table and
the operator edits the prices. The first rows, by area:

| Area (design pages) | Placements proposed | Format |
|---|---|---|
| App store (`Main`, `Categories`, `AppDetail`, `Phone*`) | in the search and category lists; a *Sponsored* row on Discover, below *Editor's picks*; *Sponsored - similar apps* on an app's page, below the description | listing |
| Games shop (`Games*`) | the same three, with the genre as the context and the age rating as a gate | listing |
| Marketplace (`Market*`) | in search results (the position `MarketSearch` already draws); a brand banner above a search (a headline, the shop's logo, three products); *Sponsored related items* on a product page; a deal of the day; a home banner | listing, brand, slot, banner |
| Trade (`Trade*`) | in the product search and the supplier search. **Never** among the quotations of a request: those are ranked by the buyer's sort and nothing else | listing |
| Video (`Video*`) | a sponsored video or channel card in the grid and in *Up next* | listing; video later (§4.13) |
| Music and podcasts (`Music*`) | a sponsored album, playlist, artist or podcast card on the home and search pages | listing; audio later (§4.13) |
| Books (`Books*`) | a sponsored title in browse and search, and in *Readers also bought*. **Never** inside a book or an audiobook | listing |
| Every area | **one** cross-area row on the home page: "From the games shop", "From the marketplace" | listing |

Four formats to begin with, two more later:

| Format | What it is | Made of | Review |
|---|---|---|---|
| **Sponsored listing** | the listing's own card, with the mark | nothing new | none beyond the listing's |
| **Brand banner** | a headline, the business's logo, up to three of its listings | a headline per language | the headline |
| **Display banner** | a picture in one of a few fixed sizes | the picture | the picture |
| **Fixed slot** | a position booked for days at a fixed price: a deal of the day, a department's banner for a week | a listing or a banner | as its content |
| Video (later) | a short film before or between videos | a video | the video |
| Audio (later) | a spot at a chapter mark of a podcast | an audio file, or the host's own read | the audio |

**How much of a page may be paid** is a setting of the service, like the
marketplace's enforcement policy, not a constant. Proposed to start: at most
one sponsored listing in five results, at most two before the first scroll,
and one cross-area row per page.

### 4.5 What may appear where

The rule in one sentence: **a shop's search and lists hold only that shop's
goods; anything from elsewhere appears only in the one cross-area row.**

| Promoted | Home area: search, lists, detail pages | Cross-area row of |
|---|---|---|
| App | app store | games shop, video, music, books |
| Game | games shop, app store | video, music |
| Product | marketplace | app store, games shop, video, music, books |
| Trade product, supplier | trade | - |
| Video, channel | video | app store, games shop, music, books |
| Album, artist, podcast | music | video, books |
| Book, audiobook | books | music (the audiobook), video |
| Business software (an app in a business category) | app store | trade |

Trade is closed on both sides: consumer goods do not appear to a company's
buyer at work, and a pallet of gateways does not appear beside a novel. The
one way in is business software, which a company buys as it buys machines.

The table is data - a row per pair, with the operator's switch - so the
first version can open one pair at a time and see what it does.

**Gates that apply everywhere**, before any price is looked at:

- the listing is public and its business in good standing;
- on the app and games shops, the machine can install it (the store already
  asks this of its Discover hero);
- the age rating fits the page and, where an account is signed in and the
  service knows the year of birth, the viewer. An account of a minor is
  shown no advertising chosen by anything but the page (§9);
- the advertiser's own exclusions and the page's (a publisher's page does
  not carry a competitor's brand banner).

### 4.6 One registration: the ULTRA account, speaking OpenID Connect

**What "like Google" is made of.** One place holds the person and the
password. Everything else never sees the password: it sends the person to
that place and gets back a short, signed statement - who this is, for whom
the statement was made, until when. That is OpenID Connect (the authorization
code flow with PKCE), and it is what makes one registration reach a shop on
the same server, a service on another host and somebody else's website alike.

**Where the tree is.**

- UltraAccount is already described as "the one account a person has across
  ULTRA OS" (`Docs/UltraAccount/README.md`), with the store's purchases,
  UltraAuthenticator's recovery, and UltraCloud and UltraAI named as next.
  On a machine the sign-in is already shared: every native application reads
  the one `session.json`.
- The portal has a second account store of its own, with other rules (a
  password of 8 characters where the account service asks for 12, a mailed
  link where it mails a code, no second factor). The portal's README lists
  buyer accounts as its third next step and says it is to replace the
  account service.
- Neither store issues a statement another service can rely on.

**Proposed, and where each stands** (decided and built 2026-11-24 where it
says so; the portal's README, *One sign-in for every service*, is the
record).

| | | |
|---|---|---|
| One store of people | **Decided: the portal's.** It is the one other services sign in through. What the account service holds beyond it - the second factor, the sessions as devices, the recovery keys, the plan - has still to move over, and `UltraAccountCore` with it. | open |
| It becomes an OpenID Connect provider | A discovery document, the authorization and token endpoints, signed identity tokens, a registry of the clients that may ask. Written as the one flow (the authorization code with PKCE) on the `cryptography` package the portal already has, not on a further dependency. | **built** |
| The issuer's address | **Decided: an address of its own, `https://account.ultraos.eu`**, answered by the same portal: the account is every service's and not the store's, each host keeps its own session cookie, and the address is written into every token for good. | **built** (a setting; unset, the account stays under the store's root) |
| Our own shops and services | Registered as first-party clients: no consent page, one sign-in at the account, so a person signed in to the app store is signed in to the marketplace, the video portal, the chat and the UltraAds console. The store's own host is the first such client. | **built** for the store; each later service registers |
| Native applications | As today through `UltraAccountCore` and `session.json`; the core gains "a token for this service", so an application never holds the password. The provider answers an application on the person's machine at `http://127.0.0.1` on whatever port it got, which is how `UltraNetOAuth2.h` signs in. | provider side **built**; the core open |
| Somebody else's site or app | *Sign in with UltraAccount*: a consent page naming what is passed on (a name, an address - each only when asked for), and a **different identifier per site**, so two shops cannot compare notes about one person. That is more than Google gives. | **built**; no outside service is let in before the conformance suite and a review |

**A role is not an account.** One person signs in once and is, by what they
have been granted, any of: a buyer (everyone), a member of a business, a
creator with a channel, a company's buyer in trade. No second registration
for any of them, and none for advertising:

| | |
|---|---|
| The business | The portal's `Publisher`, generalised: the one record of a company that sells - apps today, products, videos, books later. Applied for once, verified once by the operator; each shop then grants what it needs on top (trade asks more of a supplier than the app store of a developer). |
| Advertising | A capability of that business, on from its first published listing. **No advertiser registration.** |
| Its people | `PublisherMember`, with roles beyond *Owner* and *Developer*: **Marketing** (campaigns and reports), **Billing** (the balance and invoices), **Analyst** (reads, changes nothing). |
| Spending money | A member who can top up or raise a budget signs in with a second factor. |
| The operator's staff | Stay separate from customers' accounts, as now: their own sign-in, their roles, a second factor required. |

An agency that runs campaigns for several businesses is a person who is a
*Marketing* member of each; nothing more is needed for it.

### 4.7 Targeting: the context, never the person

*Decided 2026-11-24: context only, and for good.*

An advertisement is chosen from **what is on the page**:

| Signal | Example |
|---|---|
| Area, page and placement | the games shop's search |
| The search words | "photo editor", matched exactly, as a phrase, or broadly; with exclusions |
| Category, department, genre | Graphics; Racing; Tools |
| The listing being looked at | *Similar to this* on a detail page |
| Language | of the page |
| Country | the store's setting or the account's country - the one the prices and VAT already use |
| Platform | system, processor, desktop or phone - what the store already asks before offering an install |
| Day and hour | a campaign's schedule |

and from nothing else. There is **no profile**: no history of what a person
looked at, bought, watched or listened to, no interest groups, no list of
"people who visited my page", no matching of customers' addresses, no
identifier shared between areas, nothing bought from or sold to a third
party. Nothing follows anyone from the video portal to the marketplace.

This is a decision about what kind of platform this is - taken, as above -
and it has a price:
advertising that follows people earns more per impression. It is also why the
arrangement is simple - no consent banner for advertising, no profile to hand
over in a data archive, no special treatment of minors beyond the age gate -
and it is what the video portal already says on its front page.

The same rule shapes the request (§7): the question "what goes in this
placement?" is sent **without the session**, and a request that carries one
is refused, so that no client can leak it by accident.

**Not seeing the same thing forty times** is kept on the device: the client
counts what it showed and names, in its next request, the advertisements that
have had their share. The server never learns who saw what.

### 4.8 Choosing: an auction on top of a rate card

For each placement, each time:

1. **Who may** - the gates of §4.5, a campaign that is running, has budget
   left today and matches the context.
2. **Is it relevant** - a floor on how well the listing matches the search
   or the category. Below it the advertisement is not shown at any price; a
   shop whose paid results answer another question stops being used.
3. **Rank** - the offer multiplied by a quality figure: how often this
   listing is clicked in this kind of placement, from the counts of §4.9.
   A better listing wins against a slightly higher offer.
4. **Price** - the winner pays what was needed to stay ahead of the next
   one, never more than they offered and never less than the placement's
   **floor**, which the operator sets.

With one advertiser, step 4 is the floor: a price list. With several it is an
auction. One mechanism serves both, and nothing has to be replaced when
demand arrives.

Charged **per click** for listings and brand banners, **per thousand shown**
for display banners, a **fixed price** for a slot. A budget is spread over
the day rather than spent by breakfast.

**What money never touches.** The shop's own search and its ranking
(`store/queries.py`, "the one search the pages and the JSON service share")
are computed first and left as they are; advertisements are placed among the
results afterwards. Spending on advertising does not move a listing in the
unpaid results, and the terms say so (§9).

### 4.9 Counting, without following anyone

| | How |
|---|---|
| Shown | Each chosen advertisement comes with a signed token: the ad, the placement, the price, an expiry, a number used once. The client reports a token when the card was really on the screen. The service keeps **totals** - per advertisement, placement, country and hour - not a row per viewing. |
| Clicked | The same token. Kept for a short time (proposed: 30 days), for the invoice and to find invalid clicks, with no account and no address beside it. |
| Bought | The affiliate code's way: the store keeps the last clicked token for a few days (proposed: 7) and sends it with the checkout; the order then names the campaign. The merchant reads "14 orders, 418.60 EUR from this campaign", never who. |
| Small numbers | A line of a report that rests on fewer than ten people is folded into "other", search words included, so that a report cannot be read back into a person. |
| Not charged | A second click on one token, a click without a showing, bursts from one source, the advertiser's own, and what the operator finds afterwards - credited back, and listed in the report as such. |

### 4.10 Money

- **Prepaid.** The business tops up a balance; clicks draw it down; at zero
  the campaigns pause and say why. A top-up is an order of its own kind
  through the portal's `billing/` and Skrill Quick Checkout, the hosted page
  the store already sells with. No credit, no dunning.
- **A ledger, not a number.** Top-ups, spending by day, credits for invalid
  clicks, refunds of what was not used - appended, never edited, in the way
  the audit log is kept.
- **Invoices.** What was consumed, per month, as a proper invoice
  (XRechnung / ZUGFeRD, as `UltraFIBUDesignProposal.md` describes them).
  This rests on the portal's own next step, *Tax and payouts*; until that is
  built a top-up has a receipt and nothing more, and advertising should not
  be sold for money before it is.
- **Limits.** A new business has a ceiling per day for its first weeks; a
  campaign has a daily and a total budget; a member's role decides who may
  raise either.
- **Later: from the proceeds.** Once the store pays publishers out,
  advertising can be taken from what a merchant earned instead of paid in
  advance.
- **Credit for a start.** A voucher on approval - a first balance for a new
  merchant - is how the placements fill before there is demand.

### 4.11 Review and policy

- **A listing as it is needs no second review.** It is the common case, and
  it is why a campaign can start at once.
- **What an advertiser adds is reviewed**: a headline, a banner, later a
  film or a spot. The queue is one more page of the operator's
  (`/operator/...`), with the decisions the release review already has -
  approve, ask for changes, reject with the reason - and every decision a
  row in the audit log.
- **The policy** is a document with versions, accepted like the publisher
  terms. Proposed to refuse outright: political advertising (§9), anything
  the shops do not list, claims about health or money that the listing
  itself could not make, comparisons with a named competitor, and prices or
  ratings in a headline (the card shows the real ones).
- **Reports.** *Report this advertisement* on every one of them, into the
  table the store's *Report a problem* already fills, with the advertisement
  as one more kind of target.
- **Strikes** as for any other breach: the marketplace's notices, warnings
  and suspensions apply to a business's advertising as to its listings.

### 4.12 Paid is marked; the editor's pick is not for sale

- Every advertisement carries the word **Sponsored** (through `Tr`; German
  *Anzeige*), placed where the eye starts on the card, in a contrast that
  can be read - not the pale grey of the drawing in `MarketSearch`.
- Behind the mark, **Why am I seeing this**: who paid, and what matched -
  "your search for *photo editor*", "the category Graphics", "your country".
  With no profile the answer is always short and always complete.
- **Editor's picks, the hero, *New and updated*, the charts, best sellers
  and ratings are never sold** and never influenced by spending. A fixed
  slot (a deal of the day) is its own box with its own mark.
- A **public library** of every advertisement running or run in the last
  year - the advertisement, who paid, from when to when, in which areas,
  for which context - at one address anyone can read. Small platforms are
  not obliged to keep one **[unverified]**; it costs a page and it is the
  cheapest proof of the rest of this section.

### 4.13 Creators' share: video and podcasts, later

A film before a video or a spot inside a podcast is advertising **on
somebody's work**, and the creator is owed a part of it. That needs what the
portal does not have yet - the split of a payment and a payout - and a video
or music service that exists. So it is the last phase, and its rules are
proposed now only so the earlier phases do not close them off:

- the creator **opts in** per channel or per podcast, and chooses the kinds
  of advertiser to refuse;
- the share is a setting, published;
- a spot read by the host is marked as one;
- a paid subscription (UltraAccount Plus, or the service's own) plays none.

### 4.14 The promises already made

| Promise | Where | Verdict |
|---|---|---|
| "contains no advertising" | UltraMail's and UltraPress's privacy notes | **Stays.** UltraAds puts nothing into an application. A site built with UltraPress shows what its owner puts on it. |
| "no advertising" as a game's descriptor | `GamesDetail.dc.html` | **Stays.** It describes the game's content. The page around it may carry a marked row; the game does not. |
| "No tracking" | `VideoHome.dc.html` | **Stays, and becomes the rule for every area** (§4.7). |
| "no autoplay ads" | the same line | **The one conflict.** Sponsored cards keep it. A film before a video breaks it. Recommended: keep the promise, and if films are ever sold, only on channels that opted in, skippable, and never started by the page itself - then reword the line to what is true (§12, 5). |

And one promise to **add**, in the design's own words on every shop's "why
buy here": no advertising in UltraDesktop, in notifications, in the lock
screen, the settings, UltraFiler, UltraMail, UltraWeb or any other
application, and none in a notice sent to an account. Operating systems have
lost people over exactly this.

---

## 5. Functions proposed

**M** - the first release cannot do without it. **S** - should follow
directly. **L** - later.

### 5.1 For the merchant

| | Function | |
|---|---|---|
| 1 | **Promote** on every public listing: where, how much, how long (§4.2) | M |
| 2 | Where a campaign runs: every shop and marketplace with its placements (search, lists, detail pages, the home row) listed on the *Promote* page, this shop's ticked, the others to tick; a ceiling on the share spent outside the home shop | M |
| 3 | Daily and total budget, start and end, pause and resume | M |
| 4 | Automatic bidding up to the budget; or a highest price per click | M |
| 5 | The balance: top up, the ledger, pause at zero with a notice before it | M |
| 6 | The report: shown, clicked, spent, orders and their value, the cost per order - by campaign, area, placement, country and day | M |
| 7 | Search words: exact, phrase, broad; exclusions; the words people really searched, above the threshold of §4.9 | S |
| 8 | Ad groups: several listings and several sets of words in one campaign | S |
| 9 | A bid per placement and per area ("40 % more in search, nothing on detail pages") | S |
| 10 | Brand banner and display banner, with the texts per language (machine-drafted by UltraAI and marked so, as listings are) | S |
| 11 | Fixed slots, booked from a calendar that shows what is free | S |
| 12 | Members and roles: Marketing, Billing, Analyst (§4.6) | S |
| 13 | Suggestions: words the listing matches and is not bidding on, a budget that ran out by noon, a listing whose page loses the visitors the ad brought | S |
| 14 | Export of every report (CSV, XLSX); a monthly summary by mail | S |
| 15 | Invoices per month, as documents UltraFIBU reads | S |
| 16 | A schedule by weekday and hour; a voucher or deal attached to a campaign | L |
| 17 | Two versions of a headline or banner against each other | L |
| 18 | The programming interface, for agencies and for a merchant's own tools | L |
| 19 | A campaign for a page outside the ecosystem (§4.3) | L |
| 20 | Pay from the proceeds instead of in advance | L |
| 21 | Film and audio spots, with the creators' share (§4.13) | L |

### 5.2 For the operator

| | Function | |
|---|---|---|
| 1 | The placement table: area, page, format, how many, the floor price, on or off | M |
| 2 | The cross-area table of §4.5, a switch per pair | M |
| 3 | How much of a page may be paid, as a setting | M |
| 4 | Every campaign, with stop and the reason; a business's advertising stopped with its standing | M |
| 5 | Invalid clicks: the automatic rules, the cases to look at, the credit | M |
| 6 | The review queue for headlines and banners | S |
| 7 | The policy with its versions and acceptances | S |
| 8 | Revenue by area, placement and day; how full each placement is; what the floors do | S |
| 9 | Vouchers for a first balance | S |
| 10 | The public library (§4.12) | S |
| 11 | A shop registers its placements and its kinds of goods (§6) | S |
| 12 | The creators' share and its payout | L |

### 5.3 For the person looking at the page

| | Function | |
|---|---|---|
| 1 | The mark *Sponsored* on every advertisement | M |
| 2 | *Why am I seeing this* (§4.12) | M |
| 3 | *Report this advertisement* | M |
| 4 | *Not this one again*, kept on the device | S |
| 5 | The public library | S |
| 6 | With a paid plan: no banners, no cross-area rows, no films or spots. Marked listings in a search stay, as they are answers to the search (§12, 6) | L |

---

## 6. Data model

One Django app in the portal, `ultrastore_portal/promote/`, beside `store/`
and `billing/`, on the same database.

| Record | Holds |
|---|---|
| `Placement` | its id (`store.search.listing`), area, page, format, how many, the floor and its currency, on or off |
| `PromotableKind` | a kind of goods a shop lists (`app`, `product`, `video`): how to find one by id, its card, its standing, its age rating, its context |
| `CrossAreaRule` | a kind, an area, allowed or not (§4.5) |
| `Campaign` | the business, a name, the placements it was ticked for, the budgets, the dates, the share outside the home area, its state and who changed it |
| `AdGroup` | the campaign, the offer, the context it wants: words with their match rules, exclusions, categories, countries, languages, platforms; offers per placement |
| `Ad` | the ad group, what is promoted (kind and id), the format, what was added (headline, picture) and its review |
| `FixedSlotBooking` | a placement, the days, the price, the ad |
| `Balance`, `LedgerEntry` | per business and currency; the appended entries of §4.10 |
| `HourlyCount` | ad, placement, country, hour: shown, clicked, spent. **Totals only** |
| `Click` | the token, the ad, the placement, the time, the price, valid or not and why. No account, no address. Deleted after the period of §4.9 |
| `Attribution` | an order and the click it came from |
| `PolicyVersion`, `PolicyAcceptance` | as `TermsVersion` is kept |
| `PromoteSetting` | the page share, the periods, the thresholds, the ceilings |

Reused as they are: `Publisher` and `PublisherMember` (with the roles of
§4.6), `Order` and `Payment` (a top-up is an order of a new kind), `Report`
(a new kind of target), `AuditEntry`.

`PromotableKind` is the seam the shops plug into. The app store registers
`app` and `component`; a shop built later registers its own kinds and its
placements, and UltraAds changes by rows.

---

## 7. The service

Everything under the portal's one root, as it decides for itself.

| Address | |
|---|---|
| `/promote/...` | the console: campaigns, reports, the balance, invoices, members |
| `/console/apps/<id>/promote/` | the shop's door (§4.2) for an app; each later shop adds its own |
| `/operator/promote/...` | the placements, the rules, the review, invalid clicks, revenue |
| `/promote/library/` | the public library |
| `POST /v1/promote/decide` | `{placement, count, context: {area, query, category, listing, language, country, os, arch, form}, seen: [ad ids that had their share]}` → `{ads: [{token, kind, id, format, headline?, image?, payer: {id, name}, why: [..]}]}`. **Sent without the session; refused with one.** An empty list is a normal answer |
| `POST /v1/promote/events` | `[{token, type: shown\|clicked}]`, sent in batches |
| `GET /v1/promote/why/<token>` | who paid and what matched, for the mark's page |
| `POST /v1/checkout` | gains the optional field `promote`, the last clicked token, beside `code` |
| `/v1/promote/campaigns...` | later: the programming interface, with the business's own credentials |

The pages the portal renders itself ask the same function in the same
request and write the cards into the page; a blocker that hides them hides
them, and nothing is done about that.

**Not in the catalog.** `catalog.json` is signed, cached and the same for
everyone; an advertisement is none of the three. The catalog gains one
address, `promote_url`, the way it names the payment service, and a store
that finds none shows no advertising.

**A considered alternative: choosing on the device.** The service could
publish every running campaign as a signed file and let the client pick, so
that even the search words never leave the machine for this purpose. It was
set aside for the first version: every offer would be public, a budget could
not be held to, and a client would be trusted to report its own clicks. It
remains the stricter form of §4.7 and is worth a second look once the volume
is known (§12, 8).

---

## 8. The clients

| Client | What it gains |
|---|---|
| The portal's pages | the cards and the mark in its templates; *Why am I seeing this*; the console and the operator's pages |
| The native store (`Apps/UltraStore`) | a small client beside `Account.h` and `Purchase.h`: ask, report, keep the counts and the last clicked token. The cards are the cards the pages already draw (`ui/`), with the mark - **no new framework element** |
| The web store (`Apps/UltraStore/web`) | the same core, compiled for WebAssembly |
| A shop built later | registers its placements and kinds; draws its own cards |
| `Apps/UltraAds` (later) | the console as a native window and a WebAssembly site, on the programming interface: campaigns in a table, reports in the Charts plug-in's elements, exports through `UltraCanvasFileLoader`. A phase of its own, because the web console must exist first and the merchant's daily door is the shop's |

New texts go through `Tr`. *Sponsored*, *Promote*, *Why am I seeing this*
and *Report this advertisement* belong in the shopping base set
(`media/locale/en/base/Shopping.json`), which every shop variant already
shares - and a text in a shared set brings its translations with it.

---

## 9. Law: a list for a lawyer

From the published texts as the author knows them; every line is to be
confirmed before anything is sold. **[unverified]** throughout.

| Rule | What it asks | Where the design answers |
|---|---|---|
| Digital Services Act, art. 26 | each advertisement recognisable as one; who it is for and who paid; the main reasons it was shown and how to change them | §4.12 |
| Digital Services Act, art. 26 (3), art. 28 (2) | no profiling on sensitive data; no profiling-based advertising to minors | §4.7: no profiling at all |
| Digital Services Act, art. 39 | a public repository - for the very large platforms only | §4.12, kept anyway |
| Regulation (EU) 2019/1150 (platform to business), art. 5 | the terms say what decides the ranking and whether payment influences it | §4.8 |
| Unfair Commercial Practices Directive, annex I no. 11a; in Germany UWG § 5a | paid results in a search disclosed as paid | §4.12 |
| Regulation (EU) 2024/900 (political advertising) | labels, records and limits that are a product of their own | §4.11: refused outright |
| GDPR; in Germany TDDDG § 25 | a basis for what is processed; consent for what is stored on or read from a device beyond what the service asked for needs | §4.7, §4.9. Whether the device's own counts and the clicked token fall under "strictly necessary" is the first question to put |
| Youth protection (JMStV in Germany) | advertising fit for the audience | §4.5: the age gate |
| VAT | advertising sold to a business in another member state; the invoice | §4.10, with the portal's *Tax and payouts* |

---

## 10. Roadmap

**Phase 0 - what must be there first.** One store of people, speaking
OpenID Connect (§4.6: the provider is built; the account service's extras
and the native applications have still to move to it). Tax invoices in the
portal (its own next step). The decisions of §12.

**Phase 1 - sponsored listings in the app store.** The only shop that
exists. *Promote* on an app's console page; a campaign with its scope fixed
to the app store; the prepaid balance; the context of §4.7; the choice of
§4.8 with floors; the counts of §4.9 and the orders counted; the mark and
*Why am I seeing this*; the report; the operator's placement table and stop
button. Three placements: search and category lists, the Discover row, the
detail page. Clients: the portal's pages, the native store, the web store.
*Done when* a publisher promotes an app in a minute, sees what it cost and
sold the next morning, and a person can tell every paid card from an unpaid
one.

**Phase 2 - the console.** Search words and ad groups, offers per placement,
brand and display banners with the review queue, fixed slots, members and
roles, exports, invoices, suggestions, the policy with its versions, the
public library.

**Phase 3 - each shop as it is built.** The games shop first: the catalog
already has the category `games`, so its goods are apps and only the
placements are new. Then whichever of the marketplace, books, music, video
and trade comes into being, each registering its kinds and placements and
opening its pairs in the cross-area table. Each one adds its group of lines
to the *Promote* page's list; until the second shop exists the list has one
group.

**Phase 4 - beyond.** `Apps/UltraAds`. The programming interface. Pages
outside the ecosystem. Payment from the proceeds. Film and audio with the
creators' share. A block that the owner of an UltraPress site places to
earn from UltraAds - an advertising network, and a different business; it
is named here so that nothing earlier is built in its way.

---

## 11. Gap analysis

| Piece | Has | Lacks |
|---|---|---|
| The portal | publishers, review, reports, audit, orders, the console, the operator's pages | the `promote` app; a second kind of order; members' roles beyond two; tax invoices |
| Accounts | two stores; one shared session on a machine; an OpenID Connect provider in the portal, with the account at its own address (0.4.0) | the account service's records and `UltraAccountCore` moved to the portal's; a second factor in the portal; the conformance suite |
| The native store | the catalog, the cards, the code kept for 30 days and sent with the checkout | the client of §7; the mark; the clicked token |
| The catalog | `payment_url`, `market_url`, `account_url` | `promote_url` |
| String sets | the shopping base set | *Sponsored* and its neighbours, in every language |
| The shops | the app store | every other one |
| Design | one *Sponsored* mark in `MarketSearch` | the mark made readable; the *Promote* form; the console; *Why am I seeing this*; the operator's pages; the cross-area row on each home page |

---

## 12. Open questions for the owner

1. **The scope's default.** *Decided 2026-11-24:* *this shop*, and every
   other shop, marketplace and placement listed on the same page to tick
   (§4.2). Still open: the ceiling on what a widened campaign spends outside
   its home shop - proposed 30 % until the merchant changes it.
2. **The account's address.** *Decided 2026-11-24:* a name of its own,
   `account.ultraos.eu`, answered by the same portal.
3. **Which account store survives.** *Decided 2026-11-24:* the portal's;
   it is the one that speaks OpenID Connect. Open: when the account
   service's records and the native applications move to it.
4. **Context only, for good?** *Decided 2026-11-24:* yes. An advertisement
   is chosen from what is on the page and never from who is looking (§4.7),
   and the shops say so.
5. **A film before a video.** Recommended: no, and keep "no autoplay ads".
   If yes: only on channels that opt in, and the line is reworded.
6. **A paid plan without advertising.** Should UltraAccount Plus remove
   banners and cross-area rows? Recommended: yes, and marked listings in a
   search stay.
7. **A competitor's advertisement on a listing's own page.** Recommended:
   allowed below the description, never above the install or buy button. Or
   may a business buy its own page free of them?
8. **Choosing on the server or on the device** (§7). Recommended: the
   server, without the session, for the first version.
9. **Who may advertise at the start.** Recommended: only a business with a
   published listing. Brands with nothing listed, and agencies as customers
   of their own, later.
10. **The first floors and the first balance.** What a click costs at the
    least in each placement, the smallest top-up, and the voucher a new
    merchant starts with.
11. **Advertising inside applications and games** - a library with which a
    developer earns from UltraAds in a free app. Not proposed. It is the
    business the descriptor "no advertising" exists to tell apart, and it is
    how tracking usually arrives.
12. **Trade.** Recommended: closed on both sides, business software
    excepted (§4.5). Or should suppliers reach consumers?

---

## See also

- `Docs/UltraStore/README.md` - the catalog, *Payment*, *Affiliates and
  codes*, *The portal*, *The operator tier*, *The account tier*
- `Server/UltraStorePortal/README.md` - the one application this proposal
  adds an app to; *What comes next*
- `Docs/UltraStore/design/README.md` - every shop variant, and what of each
  exists
- `Docs/UltraAccount/README.md`, `Server/UltraStoreAccount/README.md` - the
  one account, and the session a machine's applications share
- `Docs/Modules/UltraNet/README.md` - OAuth 2.0 with PKCE, the client half
  of §4.6
- `Docs/UltraStoreOperator/README.md` - the operator's pages the review
  queue and the placement table join
- `Docs/Legal/Privacy/README.md` - the privacy notes §4.14 keeps true
- [`UltraPressDesignProposal.md`](UltraPressDesignProposal.md) - the shops
  merchants build on their own hosts, phase 4's outside pages
- [`UltraFIBUDesignProposal.md`](UltraFIBUDesignProposal.md) - the invoice
  formats of §4.10
