# UltraAffiliates — the affiliate programme of the ULTRA OS shops and marketplaces (Investigation and Proposal)

**Date:** 2026-11-25
**Status:** Proposal, **with the owner's decisions of 2026-11-26 worked in**
(§9). Nothing of it is built; §3 says what the repository already has that
it builds on. **No question to the owner is open** (§9.1); what remains
before building is the lawyer's and the tax adviser's (§7). **Rechecked on the same date**, claim by
claim, against the tree and against outside sources: §12 is the record of
what was confirmed, what was corrected and what is still unverified.
**Scope:** one affiliate programme for every shop and marketplace of ULTRA OS:
the app store, the games shop, the marketplace, e-books and audiobooks, music
and video, and the subscriptions sold through them. It covers both directions
the programme has to serve - **an affiliate looking for merchants' products to
advertise**, and **a merchant setting the rules under which its products may
be advertised** - and the rule that makes this programme different from most:
**a referred customer stays assigned to the affiliate who brought them, for
every later purchase, in every shop**, unless they buy a product through
another affiliate's link to that very product, or have bought nothing for six
months and are won back by another affiliate.

This document follows the shape of the other investigations in this
directory ([`UltraAdsDesignProposal.md`](UltraAdsDesignProposal.md),
[`UltraFIBUDesignProposal.md`](UltraFIBUDesignProposal.md)). Everything said
about the repository was checked against the tree on this date; file
references are to the current `main`. What is said about other companies'
programmes and about the law is from their published behaviour and texts as
the author knows them; a detail that could not be checked from this
environment is marked **[unverified]**, and §7 is a list for a lawyer and a
tax adviser, not advice.

**Name.** *UltraAffiliates*. The `UOS` prefix is used for the parts of the
system itself (UOS-Settings, UOS-Localisation); a service the shops share
follows UltraAds, UltraAccount and UltraStore.

---

## 1. Recommendation

**Build one affiliate programme for the whole ecosystem, run by the operator
as the network between merchants and affiliates, as a module of the store
portal beside advertising. Bind a new customer to the affiliate whose link
brought them; pay that affiliate on every later purchase in every shop; let a
link to a specific product win that product's commission for the affiliate
who sent it, without moving the binding; move the binding only when a
customer who has bought nothing for six months is won back by another
affiliate; pay an affiliate's superaffiliate - whoever brought them in - a
share on top of each commission; and tie the binding to the customer's
account, so it is suspended when the account is closed and comes back when
it is reactivated.**

| Decision | Reason |
|---|---|
| One programme, one affiliate account, one balance, every shop (§5.1) | The same reasoning that made UltraAds one service: a programme per shop is seven sign-ups, seven ledgers and seven payouts for one affiliate, and the binding the owner asked for - "all subsequent purchases" - is meaningless if it stops at a shop's border. |
| Free for the merchant, and every product is in it (§5.2) | Decided 2026-11-26. No fee, no sign-up, no opt-in and no opt-out: every product of every merchant can be advertised by every active affiliate. A merchant who does nothing pays nothing. |
| 5 % on every affiliate sale, paid by the operator (§5.2, §5.3) | Decided 2026-11-26. The minimum commission is 5 % of the net price, out of the operator's fee of 15 %, whatever the merchant offers. It is what makes "every later purchase" pay in the whole catalogue, not only where a merchant chose to take part. |
| The merchant may add a boost of 1 to 45 % (§5.2) | Decided 2026-11-26. On top of the 5 %, taken in full from the merchant's proceeds of that sale: a percentage for software, a percentage or a fixed amount per unit for hardware. It is the merchant's one lever, and its Owner sets it. |
| The affiliate is any UltraAccount that accepts the affiliate terms (§5.1) | The account already exists for every shop and speaks OpenID Connect (the portal's *One sign-in for every service*). An affiliate needs no second identity, only a public handle (which the portal's accounts do not have yet, §3), a payout method, tax details and the terms. |
| Affiliates are identified (§5.1) | Decided 2026-11-26: a verified phone number and a valid ID before the profile is active. It gives the credit notes a real name, proves the age, and holds one person to one profile - without which a ban or the superaffiliate rule could be walked around with a second account. |
| The operator is the network (§5.12) | The affiliate contracts with the operator, not with each merchant: one set of terms, one self-billed credit note a month, one payout, one tax relationship. The operator pays the base and the superaffiliate's share out of its own fee and passes the merchant's boost through. It is the only model in which a customer bound in the games shop can earn their affiliate money in the book shop. |
| Two ways in, from one programme (§5.2, §5.4) | **Affiliates browse** every shop's products with what each pays - 5 %, or more where the merchant boosts - and take links. **Merchants raise the boost** on what they want sold. |
| The binding is first-touch, and moves in one case only (§5.5) | The owner's rule. The first affiliate whose link brings a **new** customer - one who has never paid for anything in any shop - is that customer's referrer. Customers who are buying cannot be claimed by sending them a link. Decided 2026-11-26: a customer who has bought nothing for more than six months and then buys within two weeks of an affiliate's link is bound to that affiliate, whether they had a referrer before or not. |
| A product link wins its product, last click, 90 days (§5.6) | The owner's override. A link to a product credits its affiliate for that product, bought within the window, and nothing else in the basket; the binding does not move. **A link and a code count for the same 90 days** (decided 2026-11-26; the store keeps a code for 30 today, `StoreSettings::kCodeDays`). |
| One commission per sale, plus the superaffiliate's share (§5.15) | Decided 2026-11-26. An affiliate who brings in another affiliate - by a recruiting link, or because their referred customer becomes an affiliate - earns a percentage (10 % unless set otherwise) of that affiliate's commissions, on top, paid by the operator. One level up and no further, as the store's earlier scheme had it. |
| A bound customer's purchase pays the base (§5.2) | "Every later purchase" pays the 5 % everywhere, because the operator pays it. The merchant's boost is for the affiliate who promoted the product with a link, and for no one else (decided 2026-11-26). |
| Commissions are pending, then approved, then paid, with 5 % held back (§5.8) | A sale can be refunded, returned or charged back. A commission is approved 30 days after the sale, is taken back in proportion to a refund (as `Apps/UltraStoreAdmin` already does), and is paid monthly above a threshold. Decided 2026-11-26: 5 % of each commission is held back for six months against late refunds and chargebacks, and the affiliate's statistics show the amount held. |
| The binding follows the account (§5.9) | Closed: suspended, nothing accrues. Reactivated: restored, to the same affiliate. Erased: gone, and a new account is a new person. The portal's accounts cannot be closed at all today (§3), so closing and reactivating are built with the binding. |
| Blocked and banned, with a record (§5.16) | Decided 2026-11-26. An affiliate who breaks the rules is blocked (for a time) or banned (for good). Each case has a record - the rule, the facts, who decided - which the operator keeps and the affiliate gets on request. |
| Nobody sees who (§5.10) | The affiliate sees orders and amounts, never the customer; the merchant sees which affiliate earned what, never the customer; the customer sees who their referrer is. No tracking pixel, no third-party cookie: links land on the shop's own address. |

**Do not** pay more than one level up (a superaffiliate's own superaffiliate
earns nothing, §5.15), let a commission depend on anything but a paid, unreversed sale, or let a
merchant keep a separate affiliate ledger outside the programme.

---

## 2. The rules as asked, made precise

The owner's description, item by item, with the reading this proposal takes
where the words leave a choice:

| Asked | Read as |
|---|---|
| "An ecosystem-wide affiliate system" | One programme across every shop on the portal and, later, every service that sells through the ULTRA account (§5.13). |
| "Offers the possibility to affiliates to advertise for our merchants' products" | Affiliates browse every shop's products with what each pays and take links to a shop or to a single product (§5.4). Every product is open to every active affiliate (decided 2026-11-26). |
| "Merchants set rules for their products to be advertised" | Decided 2026-11-26: the merchant sets one thing, a **boost** of 1 to 45 % on top of the 5 % the operator pays - for one product, a category, a shop or everything it sells (§5.2). There is no opt-in and no opt-out. |
| "Percentage of the sales price or fixed commission per product" | The base is 5 % of the line's net price. The boost is a percentage; for hardware it may instead be a fixed amount per unit, in the product's currency. Software takes a percentage only (decided 2026-11-26, §5.2). |
| "A referred customer remains assigned to an affiliate for all subsequent purchases" | A **binding** from the customer's account to the affiliate, made once, when a new customer arrives through an affiliate's link or code (§5.5). |
| "Unless that customer uses an affiliate link to a specific product and buys that product" | A **product link** for product P, clicked within 90 days, credits its affiliate for P alone - per line, last click wins (§5.6). |
| "The referred customer will not shift his common reference to the new affiliate" | The binding does not move for a link or a code. One exception, decided 2026-11-26: a customer who has bought nothing for more than six months and buys within two weeks of another affiliate's link moves to that affiliate (§5.5). |
| "If the customer closes his UltraOS account the affiliate connection is lost, unless he reactivates that account again" | The binding is **suspended** while the account is closed and **restored** on reactivation; only the account's erasure ends it (§5.9). |

### 2.1 Worked examples

Customer **C** signs up through affiliate **A**'s link. **B** and **D** are
other affiliates. Each line is one purchase. The *base* is the 5 % the
operator pays; the *boost* is what the merchant added for that product, if
anything.

| # | What happens | Who is credited | C's binding |
|---|---|---|---|
| 1 | C buys app X in the app store | A, the base | A |
| 2 | C buys a game and an e-book a month later, no links | A, the base on each | A |
| 3 | C clicks B's link to album Y, buys Y the same week | **B**, the base plus Y's boost | A |
| 4 | C's basket: Y (B's link, clicked 3 days ago) and Z (no link) | B for Y, **A for Z** (the base) | A |
| 5 | C buys Y again 100 days after B's click | A, the base (the 90-day window has passed) | A |
| 6 | C clicks B's link to Y, then D's link to Y, buys Y | **D** (last click on the product) | A |
| 7 | C clicks B's link to Y, buys W instead | A, the base (B's link was for Y only) | A |
| 8 | C, who bought last month, clicks B's *general* link (no product), buys V | A, the base (only a product link overrides) | A |
| 9 | C clicks A's own link to Y, buys Y | A, the base plus Y's boost | A |
| 10 | C closes the account | nothing accrues to A while it is closed | **suspended** |
| 11 | C reactivates the account, buys X2 | A | A |
| 12 | C's account is erased; the same person signs up again through D's link | D gets nothing for the old purchases; the new account is a new customer, so **D** binds it | D |
| 13 | New visitor E clicks B's link to Y, signs up, buys Y and Q | B: base plus boost for Y (product link), the base for Q (bound customer) | **B** |
| 14 | Existing customer F (bought last month, never referred) clicks B's link to Y, buys Y | B, for Y | none: F is neither new nor lapsed |
| 15 | C's subscription bought through B's link to it renews | B: the base on every renewal; the boost as the merchant set it for renewals (§5.2) | A |
| 16 | Y from example 3 is refunded | B's commission for Y is taken back | A |
| 17 | C has bought nothing for seven months, clicks B's link (general or product), buys V ten days later | **B** - C is won back | **B**, from this order on |
| 18 | The same, but C buys V twenty days after the click | A (the two weeks have passed); had the link been to V itself, B for V as in example 3 | A |
| 19 | C, bound to A, becomes an affiliate; a sale through C's link earns C 10.00 | C 10.00, and **A 1.00 on top** as C's superaffiliate (10 %), paid by the operator | - |
| 20 | Affiliate S invites R with a recruiting link; R invites T; T's sale earns T 10.00 | T 10.00 and R 1.00; **S nothing** (one level up) | - |
| 21 | A commission of 10.00 is approved, 30 days after the sale | 9.50 to the balance; **0.50 held back** until six months after the sale | - |
| 22 | F, never referred, has bought nothing for seven months, clicks B's link, buys within two weeks | **B** | **B**: a lapsed customer is bound like a new one |
| 23 | Merchant M has set no boost and never heard of the programme; C buys M's app for 100.00 net | A 5.00, paid by the operator out of its 15.00; **M pays nothing** | A |
| 24 | M sets a boost of 10 % on that app; B's link sells it for 100.00 net | B 15.00: 5.00 from the operator, 10.00 from M's proceeds | - |

Example 12 is the one point where the owner's words leave room: "lost unless
he reactivates" is read as *erasure ends it, closure does not*. §5.9 explains
why a binding cannot outlive the account's erasure, and D4 (§9) gives a
closed account twelve months to be reactivated - something the portal has to
build, because its accounts cannot be closed at all today (§3).

---

## 3. What exists today

| Piece | What is there | Where |
|---|---|---|
| An affiliate scheme, merchant-side | Partners with codes and a share of each sale made with them; **two tiers**: a recruiter earns a recruit share one level up, never further; QR codes and `go/<CODE>`, `join/<affiliate>` pages; a sales ledger with credits, reversals taken back in proportion, payouts recorded, a report of what is owed | `Apps/UltraStoreAdmin/core/Affiliates.{h,cpp}`, `Tests/UltraStoreAdmin/test_affiliates.cpp`, `Docs/UltraStoreAdmin/README.md` |
| Codes in the catalog | `codes[]`: kind `discount` or `affiliate`, what it takes off, the apps, the validity, the partner's opaque id | `Docs/UltraStore/README.md`, *Affiliates and codes* |
| Attribution in the store app | One code kept in the settings, in force **30 days**, sent with the checkout; `ultrastore://code/<CODE>` and `ultrastore://app/<id>?code=<CODE>` links | `Apps/UltraStore/core/StoreSettings.h` (`kCodeDays`) |
| The portal, which replaces all of the above | One Django application for the store at `store.ultraos.eu`; it **decided to keep no compatibility** with the earlier services and desktop tools, which "were not in use" (2026-11-20). An order keeps the `code` sent with it and **takes nothing off and credits no one** (`code_applied=False`: "discount codes are not built"). The catalog the portal writes carries **no `codes[]`** | `Server/UltraStorePortal/README.md`, *Decisions*, *What comes next* 11; `billing/models.py` (`Order.code`), `billing/services.py` (`create_order`) |
| Orders | One app per order, no buyer account on it (a purchase is reached by the order's secret); `Payment` rows for payments, refunds and chargebacks | `billing/models.py` |
| Buyer accounts on orders | **Missing**, the portal's next step 3 | the portal's README, *What comes next* |
| The split of a sale, tax invoices, payouts | **Missing**, the portal's next step 2 | idem |
| One account for every shop | The portal as an OpenID Connect provider. Its `User` is an email, a name and a password: **no handle, no country, no closure** | the portal's README, *One sign-in for every service*; `accounts/models.py` |
| A handle, and account deletion | **Only in the account service the portal is built to replace**, which the UltraAccount app still signs in to: a handle (3 to 32 letters, digits, dots, underscores); a deletion requested with the password, `deleting` for 30 days (signed in still, *cancel* restores it), then carried out - the row kept under its id with the email replaced. **The portal has neither**: no page and no code closes, deactivates or deletes an account | `Server/UltraStoreAccount/README.md`, *What a deletion does*; `Docs/UltraAccount/README.md` |
| A phone number on the account | **Only in the replaced account service** (verified by a texted code). The portal's `User` has none and the portal sends no text messages | `Server/UltraStoreAccount/README.md`; no `phone` in the portal's code |
| Click attribution in advertising | **Built, and the pattern to follow.** `POST /v1/checkout` takes `promote`, the token of the advertisement last clicked; `create_order` hands it to `promote.services.order_opened`, the paid order to `order_paid`; `promote.Attribution` is one row per order. Clicks go through `/go/<token>/`. Totals, never who; small numbers folded. The native store does not send the token yet | the portal's `billing/services.py`, `promote/models.py`, `urls.py`; `Docs/Research/UltraAdsDesignProposal.md` §4.9 |
| The shops besides apps | Design variants only: games, marketplace (baskets with several sellers, delivery, returns), video, music, books (editions, a credit subscription) | `Docs/UltraStore/design/README.md` |

What this means for the design:

- **The binding needs the buyer account on the order.** Today an order has
  no person behind it. UltraAffiliates' customer binding cannot start before
  the portal's next step 3; product links and codes can (they attribute an
  order, not a person).
- **Payouts need the split of a sale.** The base comes out of the
  operator's fee and the boost out of the merchant's proceeds; until the
  portal settles shares (next step 2) the programme can count and approve,
  not pay - as `UltraStoreAdmin` did before the payment service existed.
  **The general shop fee is 15 %** (decided 2026-11-26), and it is not
  built: the portal's code and README name no store fee, so the fee itself
  is part of that step.
- **The rule of `UltraStoreAdmin` moves, its code does not.** The portal
  replaces the desktop tools; the two-tier credit, the proportional
  reversal and the go/join pages are the design this proposal keeps or
  changes deliberately (§5.14, §5.15), not code it ports.
- **Four things the proposal leans on are not in the portal.** A public
  **handle** for the affiliate, a verified **phone number**, the **codes**
  in the catalog, and any way to **close an account**. Each exists only in
  what the portal replaces. The handle, the phone number and the codes are
  small (phase 1); closing and reactivating an account is a piece of work of
  its own, and the owner's closure rule cannot be built before it (phase 2).
- **The advertising module is the template.** It already attributes an
  order to a click with a token sent at the checkout, hooks into the paid
  order and keeps its own ledger; `/go/` is its address, so the affiliate
  links take `/r/`, which no route of the portal uses.

---

## 4. How others do it

The owner's model - **the customer bound for good to the first affiliate**,
plus **a link to a product winning that product** - is not new. One large
marketplace for digital goods offers nearly the same thing per vendor
(Digistore24, below), which is the strongest outside evidence that the model
is workable. What exists, by kind:

| Kind | Examples | Attribution | What we take from it |
|---|---|---|---|
| Retail associates | Amazon Associates | A commission on what is put in the basket within **24 hours** of arriving through the link; an item added in that time still earns if the order is placed before the basket expires, usually **90 days**; a new click opens a new 24 hours. Commission by product category. No binding of the customer (Amazon's Associates help pages) | Rates per category and per product; a product link credits the product; short windows are the norm for retail. |
| Affiliate networks | Awin | The network contracts with both sides and invoices the advertiser a network fee beside the commissions. The fee's base differs by plan: a tracking fee of **3.5 % or 2.5 % of the sale** on Awin's current plans, or a **percentage of the commission** (Awin's own example: 30 %) where no sale value is tracked; otherwise as the contract says (Awin's pricing and help pages) | The operator-as-network model, **without the fee**: the owner decided the programme costs a merchant nothing (§5.2). The pending-validated-paid life and the payout threshold are the networks' common practice [unverified in the figures]. |
| SaaS referral tools | Rewardful, FirstPromoter | First-touch attribution of the customer (Rewardful's default for a new campaign); commission per payment, for a period or **for life** (the vendors' own pages) | Lifetime customer binding is an established model where the product is an account-based relationship - which every ULTRA OS shop is. |
| Digital marketplaces | Digistore24 | Product links on a **180-day** cookie, last cookie wins. Beside it a **lifetime commission**: the vendor switches it on, the customer is tied to the lifetime affiliate, who is paid on later purchases; it is a rate of its own, meant to be lower than the product commission; and the vendor chooses what happens when another affiliate's link brings the sale - the lifetime affiliate gets nothing, or both are paid [from search summaries of Digistore24's help centre; the pages could not be opened from this environment, and one summary did not find the setting] | **The closest precedent.** A lower rate for the bound customer than for the product link (here: the base against the base plus the boost, §5.2); the product link winning over the binding (§5.6); and the question Digistore24 leaves to the vendor - pay one or both - which this proposal answers "one" (D11). |
| Game stores and creator codes | Epic Games *Support-A-Creator* | A creator code, once entered, stays set for **14 days** unless another is entered; store games work through referral links instead (Epic's help pages) | A code as a carrier, equivalent to a link (§5.4), with a window. |
| App stores | Apple | Apps and in-app purchases left Apple's affiliate programme on **1 October 2018**; music, films and books stayed. The rate for apps had been 7 %, for in-app purchases 2.5 % (press reports of Apple's notice) | App-store affiliate commissions are a margin decision. Here each merchant makes it, and the operator only for the base rate. |
| Two-tier programmes | The store's earlier scheme (§3) | One level, a share of the recruit's sales | Kept as the superaffiliate, one level (§5.15). The EU's ban on pyramid schemes is about a consumer *paying* for the chance of rewards that come mainly from recruiting (§7, L5). |

What none of them does, and this one must: **the customer binding crosses
merchants**. Digistore24's lifetime commission is a vendor's setting, for
that vendor's products. Here, a customer brought to the games shop earns
their affiliate a commission in the book shop, from a merchant the affiliate
never promoted. That is only possible because the operator is the network and
can fund what a merchant does not (§5.3).

---

## 5. The design

### 5.1 Who takes part

| Role | Who | What they do |
|---|---|---|
| **Affiliate** | Any UltraAccount holder aged 18 or over who accepts the affiliate terms, **verifies a phone number, proves their identity with a valid ID** and gives payout and tax details | Browses the shops' products and what each pays, takes links and codes, sees their earnings and statements |
| **Superaffiliate** | An affiliate who brought another affiliate in (§5.15) | Earns a share on top of that affiliate's commissions; sees the share, not the sales |
| **Customer** | Any UltraAccount holder (a guest buyer by email can be credited for a product link, never bound - §5.5) | Buys; sees who their referrer is |
| **Merchant** | A portal `Publisher` (all shops). It signs up for nothing: its products are in the programme (§5.2). Its **Owner** may set a boost; *Marketing* and *Analyst* read the results | Sets boosts, reads what affiliates sold, reports an affiliate who breaks the terms |
| **Operator** | The ULTRA OS Project, with the operator roles the portal already has | Pays the base commission and the superaffiliate's share, and sets them; reviews affiliates and their ID; blocks and bans; approves and pays |

One affiliate profile per account. A merchant's people may be affiliates and
earn on other merchants' products; a sale of a merchant's own product never pays
a commission to an affiliate account that is a member of that merchant
(§5.11).

The affiliate's public name is a **handle**; links carry it (§5.4). The
portal's accounts have none (§3), so the affiliate profile holds it, chosen
when the person becomes an affiliate, under the rule the account service
used: 3 to 32 letters, digits, dots or underscores, unique. If the portal's
account later gets a handle of its own, the profile's moves there.

**Identified, not anonymous** (decided 2026-11-26). A profile becomes
`active` only when

1. a **phone number** is verified by a texted code - the portal's account
   has none yet and sends no texts (§3), so this is built with the profile;
2. a **valid identity document** has been checked;
3. the operator has reviewed the application.

The ID check does three things: it gives the credit notes a real name,
proves the age, and holds **one person to one affiliate profile** - which
is what keeps someone from being their own superaffiliate (§5.15) or coming
back after a ban (§5.16). What is kept is the *result* - name, date of
birth, document type and country, its expiry, when and by whom it was
checked - **not a picture of the document**. German law allows a copy of an
ID only with the holder's consent and marked as a copy, and the
data-protection authorities advise a note of the check in place of the copy
outside regulated branches (§7, L8). The check is made by an
identity-verification service, or by the operator's staff from an upload
that is deleted once checked; which of the two is chosen in phase 1.

### 5.2 What every sale pays, and what a merchant can add

Decided 2026-11-26. There are no merchant programmes. **Every product of
every merchant, in every shop, can be advertised by every active
affiliate** - the merchant signs up for nothing, cannot opt in and cannot
opt out, and pays no fee. A commission has two parts:

| Part | How much | Who pays | Who sets it |
|---|---|---|---|
| **Base** | **5 %** of the line's net price, on every sale an affiliate is credited for | the operator, out of its fee of 15 % | nobody: it is the minimum, "regardless what the merchant offers" |
| **Boost** | **+1 to +45 %** of the net price, on top; for hardware it may instead be a fixed amount per unit | the merchant: taken in full from the merchant's proceeds of that sale | the merchant's Owner, for the products it chooses, or not at all |

So an affiliate earns between 5 % (the merchant set nothing) and 50 % (the
largest boost) of the net price. A merchant who sets nothing pays nothing
and is advertised all the same.

**A boost** is one row:

| Field | Values |
|---|---|
| Scope | everything the merchant sells, one shop, one category, or one product (a listing; a marketplace listing's variants are one product). The most specific wins |
| Amount | a percentage from 1 to 45. **For hardware only** - physical goods, which today means the marketplace - instead a fixed amount per unit in the product's currency, not above 45 % of the unit's net price. Software and every other digital product (apps, games, e-books, audiobooks, music, video, subscriptions) take a percentage and nothing else |
| Renewals | for a subscription: the boost on the first payment only, or on every renewal |
| Validity | from, until - a boost for a launch or a season |
| Set by | the merchant's **Owner**; no other member role saves one |

**When the boost is paid.** On a sale won by a **product link** or a product
code: that affiliate promoted that product. A bound customer's purchase with
no link earns the **base alone** - the merchant pays for promotion, not for
every later purchase of a customer someone once referred. Digistore24
advises its vendors the same way: the lifetime rate below the product's
(§4). Decided 2026-11-26: "the bound affiliate gets only the 5 % base".

**What a merchant cannot do.** Approve or refuse affiliates, give a named
affiliate a better rate, forbid a channel for its own products, or decline a
commission. Earlier versions of this document had those as features of a
merchant's own programme; with no opt-in there is nothing to attach them
to (§9.2). What an affiliate may not do - bid on a merchant's brand name in
search advertising, post without saying it is advertising, run coupon
sites - is written once, in the operator's affiliate terms, for every
product. A merchant who sees them broken reports it; the operator decides
(§5.16).

**Limits the portal enforces.** A boost outside 1 to 45 is refused; a fixed
amount on a digital product is refused; a fixed amount above 45 % of the
unit's net price is refused, with the figure. A changed boost applies to
sales paid after the change; a commission keeps the base and the boost it
was paid under (a snapshot on the commission row).

### 5.3 Where each part comes from

On a sale of 100.00 net, with the general shop fee of 15 %:

| | Affiliate earns | Superaffiliate (10 %, if there is one) | Merchant keeps | Operator keeps of its 15.00 |
|---|---|---|---|---|
| No affiliate involved | - | - | 85.00 | 15.00 |
| Affiliate sale, no boost | 5.00 | 0.50 | 85.00 | 9.50 |
| Boost of 10 % | 15.00 | 1.50 | 75.00 | 8.50 |
| Boost of 45 % | 50.00 | 5.00 | 40.00 | 5.00 |

Where the affiliate has no superaffiliate, the operator keeps 10.00 in each
of the three affiliate rows.

- **The base and the superaffiliate's share come out of the operator's
  fee; the boost comes out of the merchant's proceeds.** Nobody is charged
  anything else.
- **The payment provider's fee is not in these figures.** Whoever bears it,
  it comes off that party's column.
- **The superaffiliate's share is reckoned on the whole commission**, as
  decided ("a percentage of the affiliate's commission"), so it grows with
  the merchant's boost while the operator alone pays it: at the largest
  boost the operator keeps 5.00 of its 15.00, before the provider's fee.
  Confirmed 2026-11-26: the whole commission of the affiliate, not taken
  from the affiliate but out of the operator's fee.
- **15 % is the general shop fee** (decided 2026-11-26), the same in every
  shop, so the table holds for apps, games, books and hardware alike. If a
  shop were ever given a lower fee, the base would need looking at again
  for that shop, since 5 % and the superaffiliate's share come out of it.

### 5.4 Links, codes and clicks

**Links.** Every shop answers `/r/` under its own root, so a link never names
a host of ours the customer does not know and needs no redirect service:

| Link | Means |
|---|---|
| `https://<shop>/r/<handle>` | A general referral: the shop's home, the affiliate noted |
| `https://<shop>/r/<handle>/<listing>` | A **product link**: the product's page, the affiliate noted for that product |
| `https://<shop>/affiliate/join/<handle>` | A **recruiting link**: the page *Become an affiliate*, the inviting affiliate noted as the superaffiliate (§5.15) |
| `...?s=<label>` | The affiliate's own campaign label (a "sub-id"), up to 32 characters, shown only in their report |

Today `<shop>` is `store.ultraos.eu` (the portal decided everything of the
store under one root); the other shops get `/r/` under theirs. The native
store adds `ultrastore://r/<handle>[/<app>]`, beside the `code` links it
already registers.

Any active affiliate may take a link to any product of any merchant: there
is nothing to join (§5.2).

**Codes.** The catalog's affiliate codes stay a carrier, equivalent to a
link: a code restricted to products is a product link for those products; a
code for everything is a general referral. A code's *discount* applies as
today. A general code does **not** take the commission from the bound
affiliate (decided 2026-11-26, D5): it gives its discount and changes
nothing else, unless the customer is won back with it (§5.5).

**One window for links and codes: 90 days** (decided 2026-11-26). A click
on a link and a code entered in the store both count for 90 days - for the
product they name, and towards binding a new customer who signs up or first
buys in that time. `StoreSettings::kCodeDays` is 30 today and becomes 90.
The win-back's two weeks (§5.5) and the approval period's 30 days (§5.8)
are different waits and stay as they are.

**A click** is recorded by the shop's server when the `/r/` page is opened as
a top-level navigation by a person (`Sec-Fetch-Dest: document`,
`Sec-Fetch-User: ?1`, not from inside a frame or an image). It records the
affiliate, the product if any, the shop, the label, the time and - when the
visitor is signed in - the account. It is kept server-side:

- **signed in**: against the account, nothing stored on the device;
- **not signed in**: in the server session, which signing up or in within
  the session carries onto the account; in the native store, as a referral
  token in the settings, like `code.value` today. Whether the web store may
  keep the click across sessions in a cookie needs consent (§7, question
  L2); without it, the click lasts the session.

Clicks are aggregated after 90 days into counts per affiliate, label,
product and day; no row of a click outlives its window.

### 5.5 The binding

A binding is one row: the customer's account, the affiliate, when, through
what (sign-up, first purchase, code), and its state (`active`, `suspended`,
`ended`). One per customer at a time:

1. **When.** At **sign-up**, when the person clicked an affiliate's link or
   entered an affiliate code in the 90 days before; or at the **first paid
   order** of an account with no binding, when that order carries a click or
   code from the 90 days before. The last click before that moment decides
   who.
2. **Only new customers - and lapsed ones** (decided 2026-11-26, D2 and
   D5). An account that has a paid order in any shop is not bound by a later
   link (example 14). Without this, an affiliate could claim loyal customers
   by mailing them links, and every existing customer would be up for grabs
   on launch day. The exception is the customer who has stopped buying:
   point 4.
3. **Never to oneself.** A binding to the customer's own affiliate profile
   is refused. (A merchant's staff may be bound like anyone else; what is
   refused is a commission on the merchant's own products, §5.11.)
4. **It does not move for a link or a code - except when a customer is won
   back** (decided 2026-11-26, D5). When a customer has paid for nothing in
   any shop for **more than six months** - no order, and no renewal of a
   subscription - and then clicks an affiliate's link (general or to a
   product) or enters their code, and pays for an order **within two weeks**
   of that click, the customer is bound to that affiliate with that order.
   It makes no difference whether the customer was bound to another
   affiliate before or to no one (example 22). A first affiliate keeps what
   they earned and earns nothing after; the old binding row is `ended` with
   the reason `won_back` and a new one is written, so the history stays. A
   purchase after the two weeks, or by a customer who bought within the six
   months, binds and moves nothing. Beyond this, a binding is suspended,
   restored and ended only by the accounts' states (§5.9) and by the
   operator (§5.16).
5. **Guests.** A purchase by e-mail without an account is credited for a
   product link and bound to no one. A guest who makes an account from the
   order's page within 90 days is bound to the affiliate that order carried,
   if it is their first paid order.

The customer sees their binding in the account's *Privacy and data*:
"Referred by *handle* since 3 December 2026", with a line on what it means
(the affiliate earns from their purchases; learns nothing about them).

### 5.6 Attribution of an order

When an order is paid, before anything else, the binding is settled: one is
made if §5.5 says so, or moved if the customer is won back. Then **each
line** of the order is attributed on its own, and last the superaffiliate's
share is added to each commission:

```
for each line (product P, quantity, net price):
    link = the last product-link click (or product code) for P
           by this customer or this session, within 90 days,
           whose affiliate is active,             # not blocked or banned
           not the buyer, and not a member of the seller
    if link:
        credit link's affiliate the base 5 %      # paid by the operator
                          plus P's boost, if any  # paid by the merchant
                                                  # the binding does not move
    else if the customer has an active binding to affiliate A,
            and A is active and not a member of the seller:
        credit A the base 5 %
    else:
        no commission

    if the line credited an affiliate X,
            and X has a superaffiliate S with a share set, and S is active:
        credit S that share of X's commission     # on top, paid by the operator
```

Consequences worth naming:

- **Every product link wins its product.** Every product pays at least the
  base, so there is no link with nothing behind it.
- **One commission per line, plus the superaffiliate's share** (decided
  2026-11-26, D11). The bound affiliate and the link's affiliate never both
  earn on the same unit; what is added is only the share of the earning
  affiliate's superaffiliate, where one is set. Digistore24 lets the vendor
  choose between one affiliate and both (§4); here it is one.
- **Nothing to join.** Any active affiliate earns on any product. The only
  affiliates who earn nothing on a line are the buyer themselves (D8) and a
  member of the merchant selling it.
- **A renewal belongs to its first order.** The affiliate credited with a
  subscription's first payment is credited with its renewals, for as long
  as that affiliate is active: the base on every renewal, the boost where
  the merchant set it for renewals (§5.2). A subscription bought with no
  link renews to the bound affiliate, the base each time.
- **A product sold by several sellers** (the marketplace's *other offers*):
  a product link is to one seller's listing; buying the same product from
  another seller is not that product, and goes to the bound affiliate.

### 5.7 What a commission is worth

- **Reckoned on:** the line's price as paid, after any discount, **without
  VAT and without delivery**, times the quantity.
- **The base:** 5 % of that, to the cent, half up (`PercentOf` in
  `UltraStoreAdmin` already rounds this way).
- **The boost:** 1 to 45 % of it, rounded the same way; for hardware it may
  be a fixed amount times the quantity, in the product's currency.
- **At most 50 %** of the net price, then: the base and the largest boost.
- **Currency:** the order's. An affiliate's balance is kept per currency;
  the payout converts at the provider's rate on the day (decided, D7).
- **No fee for the merchant** (decided 2026-11-26). The network fee of the
  earlier versions is gone; a merchant's only cost is a boost it chose.
- **The superaffiliate's share:** a percentage of the affiliate's
  commission - 10 % unless the operator set another for that affiliate - to
  the cent, half up, on top of the commission and paid by the operator
  (§5.15).

### 5.8 The life of a commission

| State | When | Notes |
|---|---|---|
| `pending` | the order is paid | shown to the affiliate as "pending", with the date it can be approved |
| `approved` | after the **approval period**: **30 days** after payment for digital goods; **30 days after delivery** for physical goods (the 14-day right of withdrawal plus the time a return takes) | a timer approves what is due daily; **95 %** goes to the affiliate's balance and **5 %** into the holdback |
| `held back` | 5 % of every approved commission, for **6 months** from the day the sale was paid | released to the balance on that day unless a refund or a chargeback used it up |
| `reversed` (in part or whole) | a refund, a return or a chargeback before approval | in proportion to the amount refunded, as `UltraStoreAdmin`'s reversals do (`Proportion` in `Finance.cpp`) |
| `payable` | approved, and the affiliate's balance in that currency is **25.00** or more at the month's end | below it, carried to the next month |
| `paid` | the monthly payout ran and the provider confirmed | the self-billed credit note is issued (§5.12) |
| `clawed back` | a refund, a return or a chargeback after approval or payment | in the same proportion: taken from the affiliate's holdback first, then from the balance, then as a negative line on the next statement; the operator carries the loss if the affiliate leaves with a negative balance |

**Two different waits, named apart.** The *approval period* (30 days, D9) is
the time in which a sale is still likely to be returned; nothing is paid
before it ends. The *holdback* (5 %, six months, decided 2026-11-26) is a
reserve against what comes later - a card chargeback can arrive months after
a sale. On a commission of 10.00, 9.50 is in the balance after 30 days and
0.50 follows six months after the sale.

**The affiliate's statistics show the holdback**: the amount held now, per
currency, and the next releases with their dates ("Held back: 12.40 EUR -
3.10 released on 2 March"), beside pending, balance and paid. The
superaffiliate's share follows its commission through every state, the
holdback included.

**A merchant cannot decline a commission.** A merchant who believes an order
was fraudulent, or that an affiliate broke the terms, reports it; the
operator decides, and a commission reversed for that reason shows the
affiliate why.

### 5.9 Closing, reactivating and erasing accounts

**This has to be built first.** The portal's accounts can be neither closed
nor deleted today (§3); the states below are the ones the account service it
replaces had, and the proposal assumes the portal takes them over.

**The customer's account.**

| Account | Binding | What accrues |
|---|---|---|
| active | `active` | as §5.6 |
| closed (for up to twelve months), or deletion requested (90 days of grace) - D4 | **`suspended`** from the moment of the request | nothing for the bound affiliate; a product-link purchase made in the grace still credits its link (the account is signed in still) |
| closure cancelled / account reactivated | **`active` again, to the same affiliate** | as before |
| erased (the deletion carried out) | **removed** | nothing; the commissions already earned stay in the ledger, naming the order and a customer number that no longer leads to anyone |

**Why a binding cannot outlive erasure.** Restoring a binding after an
account is gone would mean keeping something that recognises the person when
they come back - a hash of their e-mail, say. That is personal data kept
after the person asked for erasure (GDPR Art. 17), for the benefit of a
third party [unverified - §7, L4]. The proposal keeps nothing: a person who
signs up again gets a new account with no paid order, so they count as a new
customer and the next affiliate's link binds them (example 12). The incentive
to do this on purpose is small (a binding gives the customer nothing), and
the operator can end a binding made by obvious collusion (§5.11).

**How long a closed account can be reactivated** - decided 2026-11-26 (D4).
An account gets a `closed` state that is not a deletion: signed out, nothing
sold to it, kept for **twelve months** and then erased unless reactivated.
A deletion the person asks for outright has **90 days** of grace in which a
cancel restores the account; the earlier service had 30, and the store's
account page says 30 in two of its texts (`StoreAccountPage.cpp`), which
change with it.
The binding rules above apply to both.

**The affiliate's account.**

| Affiliate account | Their customers' bindings | Their money |
|---|---|---|
| affiliate profile paused by the affiliate | stay `active`; nothing accrues while paused | pending commissions continue to approval |
| blocked by the operator (§5.16) | stay; nothing accrues | pending commissions, the balance and the holdback are **held** until the block ends |
| banned (§5.16) | **ended**; the customers are bound to no one, until a win-back | as the ban's record says: what the violation earned is reversed, the rest is paid as its holdback is released |
| account closure requested | `suspended` | the balance is paid out at the next run, regardless of the threshold; the holdback as it is released |
| reactivated | `active` | as before |
| erased | **ended**; the customers are bound to no one. Having bought, they are not new customers (a win-back binds them again once they have bought nothing for six months, §5.5) | the balance was paid out; the credit notes stay as the law asks |

### 5.10 Who sees what

| | Sees | Never sees |
|---|---|---|
| Affiliate | clicks, sign-ups bound, orders by date, shop, product, quantity, base and commission, state; per label; customers bound (a count) and their purchases grouped by an anonymous customer number; **the holdback** - the amount held and when it is released; their superaffiliate's handle; if blocked or banned, the reasons at once and the whole record on request (§5.16) | the customer's name, e-mail, handle, country, or other purchases than the ones they earn on |
| Superaffiliate | per affiliate they brought in: the handle, and the share earned by month and currency | that affiliate's customers, orders and links |
| Merchant | per affiliate who sold its products: the handle, clicks, orders, and what its boosts cost | the customer behind any order (as today: "never who bought") |
| Customer | their referrer's handle and the date | what the affiliate earned |
| Operator | everything, for support, fraud and tax | - |

Unlike UltraAds' reports, an affiliate's report is not folded below a
minimum count: the affiliate has to be able to check every commission. To
make up for it, dates are given by day, never by time, so an affiliate who
sent a link to a friend cannot read the friend's evening from it.

### 5.11 Fraud and abuse

| Risk | Defence |
|---|---|
| Self-referral | No binding to oneself; no commission on a purchase by the affiliate's own account (decided, D8; the earlier scheme allowed it) |
| One person, several affiliate profiles (to be one's own superaffiliate, or to come back after a ban) | One profile per verified identity (§5.1) |
| A merchant's staff earning from the merchant | No commission where the affiliate account is a member of the seller |
| Cookie stuffing, forced clicks | Clicks only from a top-level navigation by a person (§5.4); no image, script or frame counts; bursts from one address discarded |
| Claiming existing customers | Only new customers are bound, and customers who have bought nothing for six months (§5.5) |
| Fake accounts bought with stolen cards | Commission only after the approval period, and 5 % held back for six months; chargebacks clawed back; new affiliates' first payout reviewed by the operator |
| Account farming to bind people | A binding earns only from paid, unreversed purchases; nothing is paid for a sign-up |
| Brand bidding, coupon sites, undisclosed posts | The affiliate terms forbid them for every product; a merchant reports, the operator decides and, when repeated, blocks or bans (§5.16) |
| A merchant boosting to pay itself (an affiliate account of its own people) | No commission where the affiliate is a member of the seller; one profile per verified identity |
| Collusion (close an account, re-register through another affiliate) | The operator may end a binding and reverse its commissions; the audit log names who did |

### 5.12 Money: who pays whom

The operator is the network. A sale won by a product link, on which the
merchant set a boost of 10 %:

```
buyer pays            119.00 gross
  VAT                 - 19.00            (19 %, to the tax office)
  net                 100.00
  operator's fee       -15.00            (15 %)
  boost                -10.00            (the merchant's own choice: +10 %)
  merchant             75.00

out of the operator's 15.00
  base commission      - 5.00            (5 % of net, on every affiliate sale)
  superaffiliate       - 1.50            (10 % of the affiliate's 15.00,
                                          where the affiliate has one)
  operator keeps        8.50

the affiliate         15.00            base 5.00 + boost 10.00:
                                       14.25 payable after 30 days,
                                        0.75 held back for six months
```

The payment provider's fee is not shown; §5.3 has the same sale with no
boost and with the largest.

- The **merchant's settlement** (the portal's next step 2) deducts the
  boost, where the merchant set one, and nothing else: the programme is
  free.
- The **base** and the **superaffiliate's share** are the operator's cost,
  out of its fee (decided 2026-11-26).
- The **affiliate** is paid by the operator, monthly, by **Skrill** (the
  provider the store already uses; its Automated Payments Interface sends
  money to a Skrill account, and its Mass Payments take a file of
  recipients - Paysafe's documentation; whether the store's merchant account
  has either enabled is for the sandbox run) or by SEPA transfer later.
- **Each payout comes with a self-billed credit note** (*Gutschrift*) the
  operator issues in the affiliate's name, which the affiliate agrees to in
  the terms: the net commission, VAT where the affiliate charges it, the
  reverse-charge note for an affiliate with an EU VAT number in another
  country, none for a small business or a private person who says so [all
  §7, T1-T3]. UltraFIBU's design has DATEV export and a document kind
  `gutschrift` in its schema, but no self-billing procedure: the credit
  notes are written by the portal and exported to the books.
- **Thresholds:** 25.00 per currency per month (decided, D10); on closure,
  paid regardless. The holdback is paid as it is released.

### 5.13 Per shop

Every shop pays the base. A boost is a percentage in every digital shop; a
fixed amount per unit exists for the marketplace's hardware alone (§5.2).

| Shop | What is particular |
|---|---|
| App store | Single-product orders today; trials earn nothing until paid; a licence update bought anew is a new purchase |
| Games | Same as apps; in-game purchases sold through the store are purchases like any other once they exist; a free-to-play game's sign-up earns nothing |
| Books | An edition is a product (audiobook and e-book are two); a *Books Plus* credit spent is not a sale - the subscription is, and is the operator's own product |
| Marketplace | Baskets with several sellers: attribution per line; physical goods' approval period runs from delivery; the one shop where a boost may be a fixed amount; delivery never in the base; a return reverses in proportion |
| Music, video | Subscriptions are the operator's own product; *Support* payments to a creator are gifts, never commissionable |
| Advertising balances, top-ups, gift cards | Never commissionable (they are money, not a sale) |
| UltraPress shops and other services (later) | A merchant's own website signs the customer in with UltraAccount and reports the sale to the programme with a signed call, like the payment service reported sales to the admin backend (§6.3) |

### 5.14 What happens to the existing codes and the store's settings

- The catalog's `codes[]` keep their format, but **the portal has to start
  writing them**: it publishes none today and applies none at the checkout
  (§3). An affiliate code names the affiliate's **handle** rather than an
  opaque partner id.
- The store's `code.value` / `code.enteredAt` keep the code; a new
  `referral.token` / `referral.at` keep the last `ultrastore://r/` link; both
  go with `POST /v1/checkout` (`{app, code, ref}`, beside the `promote`
  the checkout already takes for advertising).
- **The store's code window goes from 30 to 90 days** (decided 2026-11-26):
  `StoreSettings::kCodeDays`. The two texts that name the number
  (`StoreWindow.cpp`) are built from the constant and follow it; the
  store's README says 30 and is changed with it.
- The store's account page promises "30 days" for a deletion in two texts
  (`StoreAccountPage.cpp`); with D4 they say 90.
- `UltraStoreAdmin`'s partner list, two-tier credits, go/join pages and
  payouts are not migrated: the portal replaced the desktop tools with no
  compatibility kept, because they "were not in use" (the portal's README,
  *Decisions*).

### 5.15 Superaffiliates

Decided 2026-11-26 (items 1 and 2 of the owner's decisions, and D6): an
affiliate who brings another affiliate into the programme is that
affiliate's **superaffiliate**, and earns a share of every commission the
affiliate earns, on top of it.

**How one becomes a superaffiliate.** The relation is set once, at the
moment a person's affiliate profile is created:

1. If the person is a **referred customer** - their account has a binding
   to affiliate A - then **A is their superaffiliate**. This is the owner's
   rule: whoever brought the customer brought the affiliate.
2. Otherwise, if they came through an affiliate's **recruiting link**
   (§5.4) in the 90 days before, that affiliate is.
3. Otherwise they have none.

Rule 1 goes before rule 2: a customer bound to A who signs up through S's
recruiting link has A as superaffiliate.

**What it pays.**

- A percentage of the affiliate's commission: **10 %** by default, and
  "if one is set for that affiliate" - the operator may set another share,
  or none, for a single affiliate.
- **On top**: the affiliate's commission is not reduced (10.00 stays 10.00;
  the superaffiliate gets 1.00 more).
- On **every** commission the affiliate earns, base and boost alike
  (§5.3 shows what that costs the operator at each boost).
- It follows its commission: pending with it, approved with it, 5 % held
  back, reversed and clawed back in the same proportion.
- Never for the recruiting itself. Nothing is paid for a sign-up, and
  becoming an affiliate costs nothing - the two things that keep the
  arrangement on the right side of the pyramid-scheme ban (§7, L5).

**One level, and no further.** R's superaffiliate S earns on R's
commissions; S's own superaffiliate earns nothing from them (example 20).
This is the rule the store's earlier scheme was built on - "two tiers and
no more", with a test showing the third level getting nothing - and the
owner's words describe one level.

**What does not change it.**

- The relation does not move when the affiliate's own binding as a customer
  later moves by a win-back (§5.5): it records who brought the affiliate
  in, not who their referrer is today.
- A superaffiliate who is paused or blocked earns nothing meanwhile. A
  superaffiliate who is banned or erased is gone for good: the affiliates
  they brought have no superaffiliate after.
- Nobody is their own superaffiliate: one profile per verified identity
  (§5.1).

**The operator pays it** (decided 2026-11-26), out of its fee: not the
merchant, and not the affiliate out of their own commission (§5.3, §5.12).

**What each sees.** The superaffiliate sees, per affiliate they brought,
the handle and the share by month - not the sales. The affiliate sees who
their superaffiliate is.

### 5.16 Blocked and banned

Decided 2026-11-26 (item 7): a status for a user who has broken the rules,
with the details kept by the operator and available to the user on request.
The proposal makes it two states of the affiliate profile, because a
measure for a time and one for good have different effects:

| State | When | Effect |
|---|---|---|
| `blocked` | a suspected violation under review, or a sanction for a set time | links and codes attribute nothing; no commission accrues, as affiliate or as superaffiliate; bindings and the superaffiliate relations stay; pending commissions, the balance and the holdback are held; ends on a date or by the operator's decision |
| `banned` | a violation established; for good | as blocked, and: the bindings end, the superaffiliate relations end in both directions; one person, one profile (§5.1) means no new profile |

**The record.** Every block and every ban is one row that is never edited,
only added to:

| Field | Holds |
|---|---|
| The rule | the clause of the affiliate terms |
| The facts | what happened, when, with which merchant's products; the orders and clicks concerned |
| The evidence | what the operator relied on (kept; shown to the affiliate except where it names another person or shows how fraud is detected) |
| The decision | block or ban, until when, by which operator, on what date |
| The money | which commissions are reversed as earned by the violation, and what is paid |
| What the affiliate said | their statement, an appeal, and its outcome |

**What the affiliate gets.** At the moment of the measure, a notice with the
rule, the facts and how to object - the Digital Services Act asks a hosting
service for such a statement of reasons when it suspends an account or
stops payments (§7, L9), and it is the fair thing anyway. **On request, the
whole record**, less other people's data. The operator's pages list every
block and ban with its record; the audit log names who decided.

**Scope.** The state is the affiliate profile's. The person's UltraAccount
stays theirs as a customer; closing an account for other reasons is the
operator's existing business and not this programme's. A merchant cannot
block or ban, and cannot keep an affiliate away from its products: it
reports to the operator, who decides.

---

## 6. Where it lives

### 6.1 A module of the portal

`Server/UltraStorePortal/ultrastore_portal/affiliates/`, beside `promote/`,
off unless `ULTRASTORE_PORTAL_AFFILIATES=1`. The reasons are UltraAds': the
shops are variants on the portal, the orders and the account are there, and
one ledger serves every shop. Models, in outline:

| Model | Holds |
|---|---|
| `AffiliateProfile` | the account, the handle, state (`applied`, `active`, `paused`, `blocked`, `banned`, `closed`), the verified phone number, the identity check's result (name, date of birth, document type and country, expiry, checked when and by whom - no picture), website and channels, payout method reference, tax status and VAT number, terms version accepted; **the superaffiliate** and the share set for it |
| `Sanction` | the profile, block or ban, the rule, the facts, the evidence, decided by and when, until, the money, the affiliate's statement and appeal; rows are added, never edited |
| `Boost` | the publisher, scope (everything / shop / category / listing), a percentage from 1 to 45 - or, for hardware, a fixed amount with currency - renewals, validity, saved by (the Owner) |
| `Click` | affiliate, listing, shop, label, account or session, at; aggregated into `ClickDay` after 90 days |
| `Binding` | customer (one active row at a time), affiliate, at, through, state, why it ended (`won_back`, `erased`, `banned`) |
| `Commission` | order line, affiliate, basis (`product_link`, `bound_customer`, `super` with the commission it is a share of), **the base part (the operator's) and the boost part (the merchant's)** with a snapshot of the boost row, amount, currency, state, approve-after date, the amount held back and its release date, statement |
| `ProgrammeSettings` | the operator's: the base commission (5 %), the default superaffiliate share (10 %), the holdback (5 %, six months), the approval period, the windows |
| `Statement` | affiliate, month, currency, lines, credit note number, payout reference, state |

### 6.2 Where it hooks into what exists

| Hook | Where |
|---|---|
| An order opened → keep the `ref` | `billing/services.py`, `create_order`, where advertising's `order_opened` is called |
| An order paid → bind, then attribute | `process_status`, beside `advertising.order_paid(order)` |
| A renewal → the first order's affiliate | `process_recurring` |
| A refund or chargeback → reverse | `refund` and `_chargeback` |
| Sign-up → bind | `web/account.py`: `signup`, completed in `verify` |
| Account closure, cancellation, erasure → suspend, restore, remove | **nothing to hook into yet**: the portal has no closure (§3, §5.9) |
| `/r/` pages | `urls.py`; no route uses `r/` (`go/` is advertising's click) |
| The checkout's `ref` | `api/billing.py` (`POST /v1/checkout {app, code?, language?, promote?}`) |
| Daily: approve, release holdbacks, end blocks, aggregate clicks; monthly: statements | management commands, run by timers like `promote cleanup` |
| A texted code for the phone number | **nothing to hook into yet**: the portal sends mail only (§3) |

### 6.3 Pages and calls

| Who | Where | What |
|---|---|---|
| Affiliate | `/affiliate/` | Become an affiliate (terms, phone, ID); the dashboard with **pending, balance, held back and paid**; the recruiting link and the affiliates brought in; **every shop's products with what each pays** (5 %, or more where boosted; search by shop, category and boost); links and QR codes for a product or a shop; reports by label; statements and credit notes; payout and tax details |
| Merchant | `/console/affiliates/` | Nothing to switch on. **Boosts** by scope with a preview ("an affiliate earns 5 % from the store and 10 % - 8.10 at today's price - from you"), saved by the Owner only; what affiliates sold and what the boosts cost; *Report an affiliate* |
| Operator | `/operator/affiliates/` | Affiliates to review with their ID check; merchants' reports; fraud queue (bursts, self-purchases, chargebacks); **block and ban, with the record of each**; the base commission, the superaffiliate share, the holdback; what the programme costs the operator by month; statements to pay; bindings to end |
| Customer | the account's *Privacy and data* | "Referred by ..." |
| Services | `POST /v1/affiliates/conversions` | A sale made elsewhere (UltraPress shop, a service), signed by the merchant's key, with the customer's account (from the UltraAccount sign-in) and the ref - phase 4 |
| Native store | `ultrastore://r/<handle>[/<app>]`; `ref` on the checkout | phase 3 |
| UltraAccount app | an *Affiliate* section showing the dashboard and links | phase 3 |

---

## 7. Questions for a lawyer and a tax adviser

| # | Question |
|---|---|
| L1 | Affiliates' disclosure: the terms require every post or page that carries a link to say it is advertising (UWG §5a(4), in force since 28 May 2022, read through the Federal Court's influencer judgments of 2021 and 2022; the lower courts in Munich have applied it to affiliate links in a publisher's teasers, 2024 and 2025). One commentary holds that "affiliate link" or "sponsored" is not enough and "Anzeige" or "Werbung" must stand at the link. What must the terms require, what must the operator do to police it, and is it liable for an affiliate who does not? |
| L2 | Recording a click on the device of a visitor who is not signed in: §25 TDDDG (the TTDSG, renamed in May 2024; ePrivacy Art. 5(3)) requires consent for storage that is not strictly necessary, and an affiliate cookie is very probably not. The proposal therefore keeps the click in the server session and asks for nothing. Is a session enough to stay outside §25, and is the native store's own setting covered? The EU's planned changes to the cookie rules (the "Digital Omnibus") may move this. |
| L3 | The binding as personal data: the legal basis (contract, or legitimate interest with an objection right), what the privacy notice says, whether a customer may ask for the binding to be removed, and whether the customer must be told when it moves to another affiliate (§5.5). |
| L4 | Confirm that nothing may be kept after erasure to restore a binding (§5.9). |
| L5 | The superaffiliate's share (§5.15), now decided: UCPD Annex I, point 14 forbids a scheme "where a consumer gives consideration for the opportunity to receive compensation that is derived primarily from the introduction of other consumers into the scheme rather than from the sale or consumption of products"; the Court of Justice has held that it needs a payment by the consumer, of whatever amount (*4finance*, C-515/12), and that the link between payments and rewards may be indirect (2016). A programme that is free to join and pays only on sales looks outside it - confirm, also for national law. |
| L6 | Paying affiliates out of money collected for merchants: is the operator acting as a payment service (PSD2), or within the commercial-agent exemption? |
| L7 | Minimum age and capacity of an affiliate; affiliates outside the EU (sanctions screening). |
| L8 | The ID check (§5.1): §20(2) PAuswG allows a copy of an identity card only with the holder's consent and marked as a copy, and refers to data-protection law; the authorities advise noting the check (name, address, validity) in place of keeping the copy, and permanent storage is accepted for banks and insurers, not generally. Reading the card electronically needs an authorisation certificate (§21). Which procedure may the operator use, what may it keep and for how long, and does the same hold for passports and foreign documents? |
| L9 | Blocks and bans (§5.16): Article 17 of the Digital Services Act asks a hosting service for a clear and specific statement of reasons - the facts and circumstances included - when it suspends or ends an account or monetary payments. Does it apply to an affiliate profile, what must the notice hold, what complaint procedure is owed, and which commissions may be withheld or reversed from a banned affiliate? What of the record must be given on a request under GDPR Art. 15, and what may be held back? |
| L10 | The holdback (§5.8): keeping 5 % of an affiliate's approved commission for six months - the wording in the terms, and whether the money held needs to be kept apart from the operator's own. |
| T1 | Self-billing credit notes for affiliates (§14(2) UStG): the agreement, the wording, VAT for a German affiliate, a small business (§19 UStG, reworked from 1 January 2025 into an exemption, with invoice rules of its own), an EU business (reverse charge), a private person, and outside the EU. Nothing found addresses affiliate commissions specifically [unverified]. |
| T2 | Whether the operator must report affiliate income under DAC7. "Personal services" facilitated through a platform are a reportable activity; software that only advertises or redirects is not a platform. Where an affiliate paid by the operator falls is not settled by anything found [unverified]. |
| T3 | VAT: the amount a commission is reckoned on; the base commission as the operator's own cost; the boost as a deduction from the merchant's proceeds - what each is for VAT, and how it stands on the merchant's statement. |
| L11 | The merchant terms: every product is advertised by affiliates with no opt-out, and a boost is deducted from the proceeds. What must the terms say, how are merchants who already sell told of it (the EU's platform-to-business regulation asks for notice of changed terms [unverified]), and may an affiliate use a merchant's name, texts and pictures in what they publish? |

---

## 8. Phases

| Phase | What | Needs first |
|---|---|---|
| 0 | The lawyer's and tax adviser's answers to L1-L4, L8, L9, L11, T1. Nothing is open with the owner (§9.1) | - |
| 1 | **Product links in the app store**: affiliate profiles with a handle, **identified by a phone number and an ID**; **the base of 5 % and the merchant's boost**, saved by the Owner; the products with what each pays; `/r/` links, clicks, the `ref` on the checkout; attribution of a paid order to a product link; **recruiting links and the superaffiliate's share**; **block and ban with their record**; commissions pending and reversed; the dashboards; approval and payout marked by the operator by hand. The store's code window raised to 90 days. Affiliate **codes** with it or right after: the portal has to learn to publish and apply codes first (its next step 11) | a way to send a text message from the portal; the net price of a line (VAT is the portal's next step 2) |
| 2 | **The binding**: bind at sign-up and first order, the **win-back** after six months, the base on every bound customer's purchase, the customer's "Referred by"; a referred customer who becomes an affiliate gets their referrer as superaffiliate; **closing and reactivating an account** (twelve months closed, 90 days of grace for a deletion), which the portal does not have, with the binding suspended and restored | buyer accounts on orders (portal next step 3) |
| 3 | **Money and the other shops**: automatic approval after the approval period, **the 5 % holdback and its release**, statements, self-billed credit notes, Skrill payouts, the boost deducted in the merchant's settlement; baskets with several sellers, **fixed boosts for hardware**, physical goods' approval from delivery, subscriptions' renewals; the native store's `ultrastore://r/` and `ref`; UltraAccount's *Affiliate* section | the settlement of shares and tax invoices (next step 2), the operator's fee among them; the shops themselves |
| 4 | **Beyond the portal**: the conversions call for UltraPress shops and services; fraud scoring on the click aggregates | phase 3 |

Phase 1 is useful alone: every product has affiliates selling it without a
merchant lifting a finger, and every rule of §5.6 that does not involve a
binding is already in force.

---

## 9. The owner's decisions (2026-11-26)

Taken in two rounds on the same day; where the second changed the first,
the table has the second.

The eleven questions the proposal asked:

| # | Question | Decided |
|---|---|---|
| D1 | Who pays a bound customer's purchase where the merchant offers nothing | **The operator: a base of 5 % of the net price on every affiliate sale**, out of its fee of 15 % (§5.2) |
| D2 | Whom a link can bind | **New customers, and customers who have bought nothing for six months** (§5.5) |
| D3 | A network fee for merchants | **None. The programme is free for the merchant** (the 20 % of the first round is withdrawn) |
| D4 | How long a closed account can be reactivated | **A `closed` state of twelve months**; a deletion asked for has **90 days** of grace (§5.9) |
| D5 | Does a general affiliate code or link take the commission from the bound affiliate | **No** - only a product link or a product code moves a commission. **But a customer inactive for more than six months who buys within two weeks of an affiliate's link is bound to that affiliate** (§5.5) |
| D6 | The second tier | **The superaffiliate** (§5.15) |
| D7 | Balances | **Per currency**, converted only at payout |
| D8 | An affiliate's own purchases | **No commission** |
| D9 | Windows | **A link or a code counts for 90 days** - for its product, and towards binding a new customer. **Approval 30 days** (the first round had 30 for links too) |
| D10 | Payout threshold | **25.00** per currency per month |
| D11 | Does the bound affiliate also earn when a product link wins | **No** - one commission per line, plus the superaffiliate's share |

And those the owner added:

| # | Decided | Where |
|---|---|---|
| N1 | **Superaffiliates.** An affiliate who promotes the programme to other affiliates earns a percentage of their commissions (10 %, for example). A referred customer who becomes an affiliate has their referring affiliate as superaffiliate. **The operator pays it** | §5.15 |
| N2 | **One commission per sale, plus the superaffiliate's commission** where one is set for that affiliate | §5.6, §5.15 |
| N3 | **The store keeps a code for 90 days, and a link counts as long** | §5.4, §5.14 |
| N4 | **Software takes a percentage only. A fixed amount exists for hardware only.** The merchant's Owner sets both; every digital product counts as software | §5.2 |
| N5 | **A holdback of 5 % for six months** against refunds and chargebacks, shown in the sales statistics | §5.8 |
| N6 | **An affiliate gives a phone number and a valid ID** | §5.1 |
| N7 | **A status "banned or blocked"** for a user who broke the rules; the details stored, for the operator and for the user on request | §5.16 |
| N8 | **The affiliate system is free for the merchant, with no opt-in and no opt-out** | §5.2 |
| N9 | **The minimum affiliate commission is 5 %**, paid by the operator out of its fee of 15 % whatever the merchant offers | §5.2, §5.3 |
| N10 | **The merchant may add +1 to +45 %** to boost its sales, taken in full from the merchant's proceeds | §5.2 |
| N11 | **The general shop fee is 15 %** | §5.3, §5.12 |

### 9.1 Nothing is open

The points the decisions left open were answered on 2026-11-26, and the
sections named are written to the answers:

| Point | Answer | Where |
|---|---|---|
| A link's window, against the 90 days of a code | **90 days for both** | §5.4, §5.5, §5.6 |
| Is the boost paid on a bound customer's purchase with no link | **No: the bound affiliate gets only the 5 % base** | §5.2 |
| The superaffiliate's 10 %: of what, and from whom | **Of the affiliate's whole commission; not taken from the affiliate, but out of the operator's fee** | §5.3, §5.15 |
| Who pays the superaffiliate | **The operator** | §5.12 |
| The 90 days in D4 | **Twelve months closed; 90 days of grace for a requested deletion** | §5.9 |
| "Store owner", and what counts as software | **The merchant's Owner; every digital product is software** | §5.2 |
| Whom a win-back binds | **Also a lapsed customer who had no referrer** | §5.5 |

Read with the 90 days: every window in which a click or a code counts is 90
days - the product link, the sign-up, a guest who makes an account from
their order, and the recruiting link. If any of those was meant to stay at
30, it is one number in `ProgrammeSettings`.

### 9.2 What the simpler model removed

N8 to N10 replace the merchant programmes of the earlier versions. Gone
with them, so that nobody looks for them:

- a merchant's programme with states (off, open, by approval, by invitation)
  and the affiliates' memberships of it;
- a better rate for a named affiliate;
- a merchant's own list of refused channels - now the operator's affiliate
  terms, the same for every product;
- a merchant declining a commission - now a report to the operator;
- the *Affiliate manager* member role;
- a merchant allowing affiliates to earn on their own purchases (D8's
  exception);
- a bound-customer rate of the merchant's own - a bound customer's purchase
  pays the base;
- the network fee.

---

## 10. Acceptance tests

The rules are tested where they live (`affiliates/` in the portal's tests),
one test per row of §2.1, and these besides:

- **Free, and everything in it.** A merchant who has set nothing has every
  product linkable by every active affiliate; a sale through such a link
  credits 5 % of the net price, booked to the operator, and the merchant's
  proceeds are what they would be with no affiliate.
- **The boost.** 0.5 and 46 are refused, 1 and 45 accepted; a product-link
  sale credits base plus boost, the base booked to the operator and the
  boost deducted from the merchant; a bound customer's purchase of the same
  product with no link credits the base alone; a boost on a product beats
  one on its category; a boost changed after a sale does not change that
  sale's commission.
- **Software and hardware.** A fixed boost on a digital product is refused,
  on hardware accepted, above 45 % of the unit's net price refused; a boost
  saved by a member who is not the Owner is refused.
- A refund of half the line reverses half the commission, base and boost
  each by half; a chargeback after payment becomes a negative line on the
  next statement.
- A binding to oneself is refused; an order by the affiliate's own account
  credits them nothing; a purchase of the seller's own product credits no
  affiliate who is a member of that seller.
- A click from an `<img>` or a frame records nothing.
- An account whose closure is requested earns its referrer nothing; the
  cancel restores the binding to the same affiliate; the erasure removes it,
  and the commissions earned before stay.
- An affiliate erased or banned leaves their customers bound to no one.
- **Win-back.** A customer whose last paid order was five months ago is not
  moved by another affiliate's link; one whose last was seven months ago and
  who pays fifteen days after the click is not moved; one who pays within
  fourteen days is, the old affiliate earns nothing on that order, and the
  old binding row is kept as `ended`. A customer with no binding and seven
  months without an order is bound the same way.
- **Superaffiliate.** A commission of 15.00 for R (5.00 base, 10.00 boost)
  credits R's superaffiliate 1.50, booked to the operator, and leaves R's
  15.00 whole; the superaffiliate's own superaffiliate gets nothing; half of
  R's commission reversed reverses half of the share; a referred customer
  who becomes an affiliate through someone else's recruiting link has their
  referrer as superaffiliate.
- **Holdback.** 10.00 approved puts 9.50 in the balance and 0.50 in the
  holdback; it is released six months after the sale was paid; a chargeback
  in the fourth month takes the 0.50 before the balance; the statistics
  show the amount held and the next release.
- **Identity.** A profile with no verified phone number or no ID check does
  not become active; a second profile for an identity already checked is
  refused; no picture of a document is stored.
- **Blocked and banned.** A blocked affiliate's link attributes nothing and
  their balance is held; a banned affiliate's bindings and superaffiliate
  relations are ended; each has a record with the rule and the facts, and
  the affiliate's request returns it without another person's data.
- **The window.** A code entered in the store and a click on a link both
  count on day 90 and not on day 91, for the product and for binding a new
  customer.
- A renewal goes to the first order's affiliate: the base every time, the
  boost only where the merchant set it for renewals.
- Nothing in the affiliate's or the merchant's reports names the customer.

---

## 11. Sources in this repository

- `Docs/UltraStore/README.md` - *Payment*, *Affiliates and codes*
- `Docs/UltraStoreAdmin/README.md` - *Affiliates and codes*, the command line
- `Apps/UltraStoreAdmin/core/Affiliates.{h,cpp}`, `Tests/UltraStoreAdmin/test_affiliates.cpp`
- `Apps/UltraStore/core/StoreSettings.h` - `kCodeDays`
- `Server/UltraStorePortal/README.md` - *Decisions*, *Buying*, *One sign-in for every service*, *Advertising*, *What comes next*
- `Server/UltraStorePortal/ultrastore_portal/billing/models.py`, `accounts/models.py`
- `Server/UltraStoreAccount/README.md` - *What a deletion does*, the handle
- `Server/UltraStorePay/README.md` - the sales handed to the ledger
- `Docs/Research/UltraAdsDesignProposal.md` - §4.1, §4.9
- `Docs/UltraStore/design/README.md` - the shop variants

---

## 12. Verification record (2026-11-25)

Every statement about the repository was checked again against `main` at
`0b148a9b`, and the statements about other programmes and the law against
outside sources, as far as this environment reaches.

### 12.1 The repository

| Statement | Result | Evidence |
|---|---|---|
| A two-tier, code-based scheme in `UltraStoreAdmin`; the third level earns nothing; a partner may use their own code | **Confirmed** | `Affiliates.h` (`CreditsFor`), `test_affiliates.cpp` (lines 2, 123), `Docs/UltraStoreAdmin/README.md` |
| Percentages rounded to the cent, half up | **Confirmed** | `PercentOf`: `(cents * percent + 50) / 100` |
| Refunds and chargebacks take the credits back in proportion; payouts are recorded | **Confirmed** | `Finance.cpp` (`Proportion`), `AdminDatabase.h` (`PayoutRecord`, schema 5) |
| The store keeps one code for 30 days; `ultrastore://code/` and `ultrastore://app/<id>?code=` | **Confirmed** | `StoreSettings.h` (`kCodeDays = 30`), `StoreWindow.cpp` (line 902) |
| The portal replaces the earlier services and tools; no compatibility; decided 2026-11-20 | **Confirmed**, and the reason found: they "were not in use" | the portal's README, *Decisions* |
| An order keeps the code, takes nothing off, credits no one | **Confirmed** | `create_order`: `code_applied=False` |
| One app per order; no buyer account on it | **Confirmed** | `billing/models.py`: the only relations are `Order.app` and `Payment.order` |
| Buyer accounts and the split of a sale are missing, as next steps 3 and 2 | **Confirmed** | the portal's README, *What comes next* |
| Member roles Owner, Developer, Marketing, Billing, Analyst | **Confirmed** | `store/models.py` (`PublisherMember.ROLES`) |
| The other shops exist as designs only | **Confirmed** | `Docs/UltraStore/design/`; no games, books or marketplace model in the portal |
| The account has a handle | **Corrected.** Only the account service the portal replaces has one; the portal's `User` does not | `accounts/models.py`; §3, §5.1 |
| An account can be closed, with 30 days of grace | **Corrected.** Only in the replaced service; the portal has no closure or deletion of any kind | no match in the portal's code or README; §3, §5.9, §6.2, D4 |
| The catalog carries the codes | **Corrected.** The format does; the portal writes none | no `codes` in the portal's code; §3, §5.14, phase 1 |
| Advertising's click attribution is the portal's `promote/` | **Confirmed and made precise**: built, with the checkout's `promote`, `order_opened` / `order_paid` and `promote.Attribution`; `/go/` is its address | `billing/services.py`, `promote/models.py`, `urls.py`; §3, §6.2 |
| UltraFIBU plans self-billed documents | **Corrected.** DATEV export yes; of self-billing only a document kind `gutschrift` in the schema | `UltraFIBUDesignProposal.md` line 769; §5.12 |
| The hooks of §6.2 | **Corrected** to the functions that exist (`create_order`, `process_status`, `process_recurring`, `refund`, `_chargeback`, `signup`, `verify`) | `billing/services.py`, `web/account.py` |

### 12.2 The design itself

Reading the rules against each other found four places where the first
version contradicted itself, all corrected above:

- §5.2 let a bound-customer rule pay any affiliate while §5.6 asked for
  membership of the programme. Membership is for product links only. (The
  merchant programmes this was about are gone since the owner's decisions,
  §9.2.)
- §5.5 refused a binding "to a member of a merchant", which is not a
  binding at all. The rule is about commissions (§5.11).
- §5.8 took back a commission after approval only for a chargeback. A
  refund or a return after approval does the same.
- §5.12's example took 19 % VAT out of a gross 100.00. It now starts from
  119.00 gross (corrected before the first commit).

### 12.3 Other programmes and the law

| Statement | Result | Source |
|---|---|---|
| Amazon: 24 hours, 90 days for the basket | **Confirmed** | Amazon Associates help pages |
| Networks charge about a third of the commission | **Corrected.** Awin: 3.5 % or 2.5 % of the sale on its plans, or a percentage of the commission (30 % in its example) where no sale is tracked | Awin's pricing page, self-service FAQ and invoice help |
| Rewardful, FirstPromoter: first-touch, lifetime commissions | **Confirmed** | the vendors' own pages |
| Digistore24: lifetime commissions on later purchases | **Confirmed in substance, and closer to the owner's model than first said**; the details (per vendor, a rate of its own, one affiliate or both) rest on search summaries, since the pages could not be opened here | Digistore24's help centre and feature pages, as summarised |
| Epic: a creator code kept for a number of days | **Confirmed**: 14 days, not for Epic Games Store games | Epic's help pages |
| Apple ended commissions on apps in 2018 | **Confirmed**: 1 October 2018, apps and in-app purchases only | press reports of 1 and 2 August 2018 |
| UCPD Annex I, 14 forbids pyramid schemes | **Confirmed**, with the wording and two judgments (§7, L5) | the directive's text; summaries of C-515/12 and the 2016 ruling |
| TTDSG §25 | **Corrected**: the law is the TDDDG since May 2024; §25 is unchanged | German legal publishers |
| UWG §5a(4) and disclosure | **Confirmed** as the provision; no Federal Court judgment on affiliate links as such was found | commentary and the Munich decisions of 2024 and 2025 |
| Skrill can pay affiliates | **Confirmed** as a product (send money by API, Mass Payments); not for this merchant account | Paysafe's documentation |
| Self-billing, §19 UStG, DAC7 | **Still unverified** for affiliates; §19 changed on 1 January 2025 | §7, T1 and T2 |
| A ban owes the user its reasons (checked 2026-11-26, for N7) | **Confirmed** as a rule for hosting services: DSA Art. 17, covering suspended accounts and stopped payments, with the facts and circumstances in the statement; whether an affiliate profile falls under it is L9 | the Commission's DSA transparency pages; the regulation's Article 17 |
| A copy of an ID may be kept (checked 2026-11-26, for N6) | **Corrected before it was written**: only with consent and marked as a copy; a note of the check is what the authorities advise | German data-protection and consumer bodies on §20(2) PAuswG |
| The shop fee is 15 % (checked 2026-11-26, for N9 and N11) | **Decided by the owner; not in the repository.** No store fee is named anywhere in the portal's code or README, or in the store's and the operator tool's documents | a search of those files; §3 |
| The portal's account has a phone number (checked 2026-11-26, for N6) | **No**: only the replaced account service has one | no `phone` in the portal's code |
| The store's texts follow `kCodeDays` (checked 2026-11-26, for N3) | **Confirmed** for the two texts in `StoreWindow.cpp`; the account page's "30 days" for a deletion is written out twice | `StoreWindow.cpp` lines 898, 1109; `StoreAccountPage.cpp` lines 853, 866 |

Sources read: <https://affiliate-program.amazon.com/help/node/topic/G9SMD8TQHFJ7728F>,
<https://www.awin.com/gb/pricing/advertisers>,
<https://www.awin.com/us/terms-and-conditions/self-service-faqs>,
<https://help.awin.com/docs/understanding-your-awin-invoice>,
<https://rewardful.com/first-or-last-touch-attribution>,
<https://firstpromoter.com/compare/rewardful>,
<https://www.digistore24.com/features/lifetime-affiliate-commissions/>,
<https://help.digistore24.com/hc/en-us/articles/24291910522769>,
<https://www.epicgames.com/help/c-202300000001640/c-202300000001740/why-is-my-creator-code-disabled-a202300000017158>,
<https://www.macstories.net/news/apple-announces-apps-and-in-app-purchases-will-be-removed-from-its-affiliate-program-october-1st/>,
<https://www.legislation.gov.uk/eudr/2005/29/annex/I/adopted>,
<https://www.e-recht24.de/datenschutz/12834-tdddg.html>,
<https://www.it-recht-kanzlei.de/affiliate-links-korrekte-kennzeichnung.html>,
<https://docs.paysafe.com/docs/digital-wallets/mass-payments/mass-payments>,
<https://docs.paysafe.com/docs/digital-wallets/api-mqi/send-money>,
<https://www.revenue.ie/en/companies-and-charities/international-tax/aeoi/dac7/index.aspx>,
<https://transparency.dsa.ec.europa.eu/page/documentation>,
<https://www.springlex.eu/en/packages/dsa/dsa-regulation/article-17/>,
<https://www.verbraucherzentrale-niedersachsen.de/themen/internet-telefon/datenschutz/airbnb-fordert-ausweiskopie-als-identitaetsnachweis>,
<https://www.dr-datenschutz.de/ldi-nrw-personalausweis-kopieren-oftmals-nach-dsgvo-verboten/>.
The pages were read through search summaries, not opened; a figure taken
from them should be read at its source before it goes into a contract.
