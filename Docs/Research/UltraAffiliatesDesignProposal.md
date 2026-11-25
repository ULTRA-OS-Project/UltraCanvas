# UltraAffiliates — the affiliate programme of the ULTRA OS shops and marketplaces (Investigation and Proposal)

**Date:** 2026-11-25
**Status:** Proposal. Nothing of it is built; §3 says what the repository
already has that it builds on, and §9 lists the decisions it needs from the
owner before the first line of code. **Rechecked on the same date**, claim by
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
another affiliate's link to that very product.

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
portal beside advertising. Bind a new customer for good to the affiliate
whose link brought them; pay that affiliate on every later purchase in every
shop; let a link to a specific product win that product's commission for the
affiliate who sent it, without moving the binding; and tie the binding to the
customer's account, so it is suspended when the account is closed and comes
back when it is reactivated.**

| Decision | Reason |
|---|---|
| One programme, one affiliate account, one balance, every shop (§5.1) | The same reasoning that made UltraAds one service: a programme per shop is seven sign-ups, seven ledgers and seven payouts for one affiliate, and the binding the owner asked for - "all subsequent purchases" - is meaningless if it stops at a shop's border. |
| The affiliate is any UltraAccount that accepts the affiliate terms (§5.1) | The account already exists for every shop and speaks OpenID Connect (the portal's *One sign-in for every service*). An affiliate needs no second identity, only a public handle (which the portal's accounts do not have yet, §3), a payout method, tax details and the terms. |
| The operator is the network (§5.12) | The affiliate contracts with the operator, not with each merchant: one set of terms, one self-billed credit note a month, one payout, one tax relationship. The merchant pays the commission out of its share of the sale, which the operator already settles. This is how the affiliate networks work (Awin invoices the advertiser a network fee beside the commissions, §4), and it is the only model in which a customer bound in the games shop can earn their affiliate money in the book shop. |
| Two ways in, from one programme (§5.2, §5.4) | **Merchants publish programmes** (which products, percentage or fixed amount per product, open or by approval) and **affiliates browse them** and take links. The same programme record serves both doors. |
| The binding is first-touch and permanent (§5.5) | The owner's rule. The first affiliate whose link brings a **new** customer - one who has never paid for anything in any shop - is that customer's referrer for good. Existing customers cannot be claimed by sending them a link. |
| A product link wins its product, last click, 30 days (§5.6) | The owner's override. A link to a product credits its affiliate for that product, bought within the window, and nothing else in the basket; the binding does not move. 30 days is the window the store's codes already use (`StoreSettings::kCodeDays`). |
| What a bound customer's purchase pays: the merchant's rule, else the operator's base rate (§5.3) | "Every later purchase" must pay something even where the merchant runs no programme, or the binding is worth nothing in most of the catalogue. The operator funds a small base rate out of its own fee; a merchant's programme replaces it where it has one. This is open decision D1. |
| Commissions are pending, then approved, then paid (§5.8) | A sale can be refunded, returned or charged back. A commission is approved only after the product's return window and a margin, is taken back in proportion to a refund (as `Apps/UltraStoreAdmin` already does), and is paid monthly above a threshold. |
| The binding follows the account (§5.9) | Closed: suspended, nothing accrues. Reactivated: restored, to the same affiliate. Erased: gone, and a new account is a new person. The portal's accounts cannot be closed at all today (§3), so closing and reactivating are built with the binding. |
| Nobody sees who (§5.10) | The affiliate sees orders and amounts, never the customer; the merchant sees which affiliate earned what, never the customer; the customer sees who their referrer is. No tracking pixel, no third-party cookie: links land on the shop's own address. |

**Do not** build more than one level of recruiting (§5.15, open decision D6),
let a commission depend on anything but a paid, unreversed sale, or let a
merchant keep a separate affiliate ledger outside the programme.

---

## 2. The rules as asked, made precise

The owner's description, item by item, with the reading this proposal takes
where the words leave a choice:

| Asked | Read as |
|---|---|
| "An ecosystem-wide affiliate system" | One programme across every shop on the portal and, later, every service that sells through the ULTRA account (§5.13). |
| "Offers the possibility to affiliates to advertise for our merchants' products" | Affiliates browse a directory of programmes and take links to a merchant's whole range, a category or a single product (§5.4). |
| "Merchants set rules for their products to be advertised" | A programme per merchant, with rules per scope (all, a shop, a category, a product) and per kind of sale (a product link, a bound customer) (§5.2). |
| "Percentage of the sales price or fixed commission per product" | A rule is either a percentage of the line's net price or a fixed amount per unit, in the product's currency (§5.7). |
| "A referred customer remains assigned to an affiliate for all subsequent purchases" | A **binding** from the customer's account to the affiliate, made once, when a new customer arrives through an affiliate's link or code (§5.5). |
| "Unless that customer uses an affiliate link to a specific product and buys that product" | A **product link** for product P, clicked within 30 days, credits its affiliate for P alone - per line, last click wins (§5.6). |
| "The referred customer will not shift his common reference to the new affiliate" | The binding never moves, whatever links the customer clicks (§5.5). |
| "If the customer closes his UltraOS account the affiliate connection is lost, unless he reactivates that account again" | The binding is **suspended** while the account is closed and **restored** on reactivation; only the account's erasure ends it (§5.9). |

### 2.1 Worked examples

Customer **C** signs up through affiliate **A**'s link. **B** and **D** are
other affiliates. Each line is one purchase; "rule" is the merchant's rule
for that product, or the operator's base rate where the merchant has none.

| # | What happens | Who is credited | C's binding |
|---|---|---|---|
| 1 | C buys app X in the app store | A, at X's *bound customer* rule | A |
| 2 | C buys a game and an e-book a month later, no links | A, for each, at each one's rule | A |
| 3 | C clicks B's link to album Y, buys Y the same week | **B**, at Y's *product link* rule | A |
| 4 | C's basket: Y (B's link, clicked 3 days ago) and Z (no link) | B for Y, **A for Z** | A |
| 5 | C buys Y again 40 days after B's click | A (the 30-day window has passed) | A |
| 6 | C clicks B's link to Y, then D's link to Y, buys Y | **D** (last click on the product) | A |
| 7 | C clicks B's link to Y, buys W instead | A, at W's rule (B's link was for Y only) | A |
| 8 | C clicks B's *general* link (no product), buys V | A (only a product link overrides) | A |
| 9 | C clicks A's own link to Y, buys Y | A, at Y's *product link* rule | A |
| 10 | C closes the account | nothing accrues to A while it is closed | **suspended** |
| 11 | C reactivates the account, buys X2 | A | A |
| 12 | C's account is erased; the same person signs up again through D's link | D gets nothing for the old purchases; the new account is a new customer, so **D** binds it | D |
| 13 | New visitor E clicks B's link to Y, signs up, buys Y and Q | B for Y (product link) and for Q (bound customer) | **B** |
| 14 | Existing customer F (has bought before, never referred) clicks B's link to Y, buys Y | B, for Y | none: F is not a new customer, so F is not bound |
| 15 | C's subscription bought through B's link to it renews | B, for as many renewals as the rule says (§5.7) | A |
| 16 | Y from example 3 is refunded | B's commission for Y is taken back | A |

Example 12 is the one point where the owner's words leave room: "lost unless
he reactivates" is read as *erasure ends it, closure does not*. §5.9 explains
why a binding cannot outlive the account's erasure, and open decision D4 asks
how long a closed account stays reactivatable - a question the portal has
not answered yet, because its accounts cannot be closed at all (§3).

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
| Click attribution in advertising | **Built, and the pattern to follow.** `POST /v1/checkout` takes `promote`, the token of the advertisement last clicked; `create_order` hands it to `promote.services.order_opened`, the paid order to `order_paid`; `promote.Attribution` is one row per order. Clicks go through `/go/<token>/`. Totals, never who; small numbers folded. The native store does not send the token yet | the portal's `billing/services.py`, `promote/models.py`, `urls.py`; `Docs/Research/UltraAdsDesignProposal.md` §4.9 |
| The shops besides apps | Design variants only: games, marketplace (baskets with several sellers, delivery, returns), video, music, books (editions, a credit subscription) | `Docs/UltraStore/design/README.md` |

What this means for the design:

- **The binding needs the buyer account on the order.** Today an order has
  no person behind it. UltraAffiliates' customer binding cannot start before
  the portal's next step 3; product links and codes can (they attribute an
  order, not a person).
- **Payouts need the split of a sale.** A commission is paid out of the
  merchant's share; until the portal settles shares (next step 2) the
  programme can count and approve, not pay - as `UltraStoreAdmin` did before
  the payment service existed.
- **The rule of `UltraStoreAdmin` moves, its code does not.** The portal
  replaces the desktop tools; the two-tier credit, the proportional
  reversal and the go/join pages are the design this proposal keeps or
  changes deliberately (§5.14, §5.15), not code it ports.
- **Three things the proposal leans on are not in the portal.** A public
  **handle** for the affiliate, the **codes** in the catalog, and any way to
  **close an account**. Each exists only in what the portal replaces. The
  handle and the codes are small (phase 1); closing and reactivating an
  account is a piece of work of its own, and the owner's closure rule cannot
  be built before it (phase 2).
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
| Affiliate networks | Awin | The network contracts with both sides and invoices the advertiser a network fee beside the commissions. The fee's base differs by plan: a tracking fee of **3.5 % or 2.5 % of the sale** on Awin's current plans, or a **percentage of the commission** (Awin's own example: 30 %) where no sale value is tracked; otherwise as the contract says (Awin's pricing and help pages) | The operator-as-network model; a fee charged to the merchant, never taken from the affiliate. The pending-validated-paid life and the payout threshold are the networks' common practice [unverified in the figures]. |
| SaaS referral tools | Rewardful, FirstPromoter | First-touch attribution of the customer (Rewardful's default for a new campaign); commission per payment, for a period or **for life** (the vendors' own pages) | Lifetime customer binding is an established model where the product is an account-based relationship - which every ULTRA OS shop is. |
| Digital marketplaces | Digistore24 | Product links on a **180-day** cookie, last cookie wins. Beside it a **lifetime commission**: the vendor switches it on, the customer is tied to the lifetime affiliate, who is paid on later purchases; it is a rate of its own, meant to be lower than the product commission; and the vendor chooses what happens when another affiliate's link brings the sale - the lifetime affiliate gets nothing, or both are paid [from search summaries of Digistore24's help centre; the pages could not be opened from this environment, and one summary did not find the setting] | **The closest precedent.** A bound-customer rate separate from the product-link rate (§5.2); the product link winning over the binding (§5.6); and the question Digistore24 leaves to the vendor - pay one or both - which this proposal answers "one" (D11). |
| Game stores and creator codes | Epic Games *Support-A-Creator* | A creator code, once entered, stays set for **14 days** unless another is entered; store games work through referral links instead (Epic's help pages) | A code as a carrier, equivalent to a link (§5.4), with a window. |
| App stores | Apple | Apps and in-app purchases left Apple's affiliate programme on **1 October 2018**; music, films and books stayed. The rate for apps had been 7 %, for in-app purchases 2.5 % (press reports of Apple's notice) | App-store affiliate commissions are a margin decision. Here each merchant makes it, and the operator only for the base rate. |
| Two-tier programmes | The store's earlier scheme (§3) | One level, a share of the recruit's sales | Kept as an option at most (§5.15). The EU's ban on pyramid schemes is about a consumer *paying* for the chance of rewards that come mainly from recruiting (§7, L5). |

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
| **Affiliate** | Any UltraAccount holder aged 18 or over who accepts the affiliate terms and gives payout and tax details | Browses programmes, joins them, takes links and codes, sees their earnings and statements |
| **Customer** | Any UltraAccount holder (a guest buyer by email can be credited for a product link, never bound - §5.5) | Buys; sees who their referrer is |
| **Merchant** | A portal `Publisher` (all shops), with a new member role **Affiliate manager** beside *Marketing*, *Billing*, *Analyst* | Publishes a programme, sets rules, approves affiliates, reads the programme's report |
| **Operator** | The ULTRA OS Project, with the operator roles the portal already has | Sets the base rate and the network fee, reviews affiliates, handles fraud, approves and pays |

One affiliate profile per account. A merchant's people may be affiliates of
*other* merchants' programmes; a sale of a merchant's own product never pays
a commission to an affiliate account that is a member of that merchant
(§5.11).

The affiliate's public name is a **handle**; links carry it (§5.4). The
portal's accounts have none (§3), so the affiliate profile holds it, chosen
when the person becomes an affiliate, under the rule the account service
used: 3 to 32 letters, digits, dots or underscores, unique. If the portal's
account later gets a handle of its own, the profile's moves there.

### 5.2 Programmes: what a merchant sets

A merchant has **one programme**, off until switched on. It holds:

| Field | Values | Default |
|---|---|---|
| State | `off`, `open` (any affiliate may take links), `approval` (an affiliate applies, the merchant decides), `invite` (only affiliates the merchant invites) | `off` |
| Description and terms | Free text shown in the directory: what the merchant wants and does not want (no brand-word bidding, no coupon sites, no e-mail campaigns, ...) | empty |
| Channels refused | Ticks: search ads on the merchant's brand, coupon and voucher sites, cashback sites, e-mail, social posts without disclosure | none |
| Rules | A list, below | none |
| Pays for bound customers | `yes` (its *bound customer* rules apply to a customer bound to **any** affiliate, a member of the programme or not - the merchant did not choose that affiliate, the customer's history did) or `no` (the operator's base rate applies instead, §5.3) | `yes` |

A **rule** says what a sale earns:

| Field | Values |
|---|---|
| Scope | the whole programme, one shop, one category, one product (a listing; a marketplace listing's variants are one product) |
| Kind of sale | `product link` (a product link or a product-restricted code, §5.6), `bound customer`, or both |
| Commission | `percent` of the line's net price (0.5 to 70), or `fixed` amount per unit in the product's currency |
| Renewals | for a subscription: `first payment only`, `N renewals`, or `every renewal` |
| Validity | from, until |
| For whom | every affiliate in the programme, or named affiliates (a better rate for a good partner) |

The **most specific rule wins**: product over category over shop over
programme, and a named-affiliate rule over the general one at the same
scope. No rule for a product means the programme does not pay for it - then
a product link to it earns nothing, and a bound customer's purchase of it
earns the base rate.

**Limits the portal enforces.** A commission is never more than the
merchant's share of the line (the price less VAT, the provider's fee and the
store's fee); a fixed amount above it is cut to it and the merchant is told
when they save the rule. A rule change applies to sales paid after it; a sale
keeps the rule it was paid under (a snapshot on the commission row).

### 5.3 What a bound customer's purchase pays, when the merchant has no rule

The owner's rule says *all* subsequent purchases. Three ways to make that
true, of which the proposal recommends the second (open decision D1):

| Option | How | For | Against |
|---|---|---|---|
| A. Mandatory minimum | The merchant terms make every listing pay at least a set bound-customer rate | Merchants fund it all | A term merchants did not choose, on sales the affiliate had nothing to do with; resistance at sign-up, especially from small publishers |
| **B. Operator base rate** (recommended) | The operator pays a base rate, say **10 % of the store's fee** on the line, wherever the merchant's programme has no bound-customer rule or is off | The binding is worth something in every shop from the first day; merchants opt in for more; the operator pays for what it gains - customers kept in the ecosystem | It costs the operator margin; needs the store's fee to be known per line (next step 2) |
| C. Only where a programme pays | Nothing unless the merchant opted in | Simplest | The binding pays in a fraction of the catalogue; not what was asked |

Under B, a merchant's bound-customer rule **replaces** the base rate on its
products (it does not add to it), and the operator may set the base rate per
shop (a marketplace's thin margins differ from an app's).

### 5.4 Links, codes and clicks

**Links.** Every shop answers `/r/` under its own root, so a link never names
a host of ours the customer does not know and needs no redirect service:

| Link | Means |
|---|---|
| `https://<shop>/r/<handle>` | A general referral: the shop's home, the affiliate noted |
| `https://<shop>/r/<handle>/<listing>` | A **product link**: the product's page, the affiliate noted for that product |
| `...?s=<label>` | The affiliate's own campaign label (a "sub-id"), up to 32 characters, shown only in their report |

Today `<shop>` is `store.ultraos.eu` (the portal decided everything of the
store under one root); the other shops get `/r/` under theirs. The native
store adds `ultrastore://r/<handle>[/<app>]`, beside the `code` links it
already registers.

**Codes.** The catalog's affiliate codes stay a carrier, equivalent to a
link: a code restricted to products is a product link for those products; a
code for everything is a general referral. A code's *discount* applies as
today. Whether a general code that gives a discount should also take the
commission from the bound affiliate for that order is open decision D5 - the
recommendation is **no**: only a product link moves a commission, and a
general code a bound customer types gives the discount and changes nothing
else.

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

Clicks are aggregated after 30 days into counts per affiliate, label,
product and day; no row of a click outlives its window.

### 5.5 The binding

A binding is one row: the customer's account, the affiliate, when, through
what (sign-up, first purchase, code), and its state (`active`, `suspended`,
`ended`). One per customer, made once:

1. **When.** At **sign-up**, when the person clicked an affiliate's link or
   entered an affiliate code in the 30 days before; or at the **first paid
   order** of an account with no binding, when that order carries a click or
   code from the 30 days before. The last click before that moment decides
   who.
2. **Only new customers.** An account that has a paid order in any shop is
   not bound by a later link (example 14). Without this, an affiliate could
   claim loyal customers by mailing them links, and every existing customer
   would be up for grabs on launch day.
3. **Never to oneself.** A binding to the customer's own affiliate profile
   is refused. (A merchant's staff may be bound like anyone else; what is
   refused is a commission on the merchant's own products, §5.11.)
4. **Permanent.** Nothing moves it: not another affiliate's link, not a
   product link, not a code. It is suspended, restored and ended only by the
   accounts' states (§5.9), and by the operator for fraud.
5. **Guests.** A purchase by e-mail without an account is credited for a
   product link and bound to no one. A guest who makes an account from the
   order's page within 30 days is bound to the affiliate that order carried,
   if it is their first paid order.

The customer sees their binding in the account's *Privacy and data*:
"Referred by *handle* since 3 December 2026", with a line on what it means
(the affiliate earns from their purchases; learns nothing about them).

### 5.6 Attribution of an order

When an order is paid, before anything else, a binding is made if §5.5 says
so. Then **each line** of the order is attributed on its own:

```
for each line (product P, quantity, net price):
    link = the last product-link click (or product code) for P
           by this customer or this session, within 30 days,
           whose affiliate is in P's programme,   # open, approved or invited
           not suspended, and not a member of the seller
    if link and P's programme has a product-link rule for that affiliate:
        credit link's affiliate at that rule      # the binding does not move
    else if the customer has an active binding to affiliate A,
            and A is not suspended and not a member of the seller:
        credit A at P's bound-customer rule       # A need not be in the programme
                 or the operator base rate (§5.3)
    else:
        no commission
```

Consequences worth naming:

- **A product link with no rule behind it does not override.** If the
  merchant pays nothing for product links to P, the bound affiliate keeps
  P's purchase - an affiliate cannot take a commission away from another by
  sending a link that earns themselves nothing.
- **One commission per line, never two.** The bound affiliate and the link's
  affiliate never both earn on the same unit. Digistore24 lets the vendor
  choose between this and paying both (§4); paying both is open decision
  D11, and the recommendation is to keep to one, as the owner described.
- **Membership is for links, not for bindings.** A product link earns only
  for an affiliate the programme admitted. A bound customer's purchase earns
  for whoever the customer is bound to: the merchant sets a rate for bound
  customers as a class, or leaves them to the base rate.
- **A renewal belongs to its first order.** A subscription bought through B's
  product link renews to B for the renewals the rule allows, and then to no
  one (not to the bound affiliate: the renewal is the same purchase,
  continued). A subscription bought with no link renews to the bound
  affiliate under the same rule.
- **A product sold by several sellers** (the marketplace's *other offers*):
  a product link is to one seller's listing; buying the same product from
  another seller is not that product, and goes to the bound affiliate.

### 5.7 What a commission is worth

- **Base:** the line's price as paid, after any discount, **without VAT and
  without delivery**, times the quantity.
- **Percent:** of that base, to the cent, half up (`PercentOf` in
  `UltraStoreAdmin` already rounds this way).
- **Fixed:** the amount times the quantity, in the product's currency.
- **Never more than the merchant's share of the line** (§5.2); never below
  zero.
- **Currency:** the order's. An affiliate's balance is kept per currency;
  the payout converts at the provider's rate on the day (open decision D7).
- **The network fee** (if any, open decision D3) is charged to the merchant
  on top of the commission, never taken from the affiliate.

### 5.8 The life of a commission

| State | When | Notes |
|---|---|---|
| `pending` | the order is paid | shown to the affiliate as "pending", with the date it can be approved |
| `approved` | after the holdback: **30 days** after payment for digital goods; **30 days after delivery** for physical goods (the 14-day right of withdrawal plus the time a return takes) | a timer approves what is due daily |
| `reversed` (in part or whole) | a refund, a return or a chargeback before approval | in proportion to the amount refunded, as `UltraStoreAdmin`'s reversals do (`Proportion` in `Finance.cpp`) |
| `payable` | approved, and the affiliate's approved balance in that currency is **25.00** or more at the month's end | below it, carried to the next month |
| `paid` | the monthly payout ran and the provider confirmed | the self-billed credit note is issued (§5.12) |
| `clawed back` | a refund, a return or a chargeback after approval or payment | in the same proportion, as a negative line on the next statement; the operator carries the loss if the affiliate leaves with a negative balance |

A merchant may **decline** a pending commission only for a reason the terms
list (the order was fraudulent, the channel was one the programme refuses),
the affiliate sees the reason, and the operator decides a dispute.

### 5.9 Closing, reactivating and erasing accounts

**This has to be built first.** The portal's accounts can be neither closed
nor deleted today (§3); the states below are the ones the account service it
replaces had, and the proposal assumes the portal takes them over.

**The customer's account.**

| Account | Binding | What accrues |
|---|---|---|
| active | `active` | as §5.6 |
| closure requested (the earlier service's `deleting`, with 30 days of grace) | **`suspended`** from the moment of the request | nothing for the bound affiliate; a product-link purchase made in the grace still credits its link (the account is signed in still) |
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

**How long a closed account can be reactivated** was 30 days in the account
service: the grace before a deletion is carried out. The portal has to decide
it anew. If the owner wants "reactivate" to mean months, the account needs a
`closed` state that is not a deletion: signed out, nothing sold to it, kept
for a set time (say 12 months) and then erased unless reactivated. Open
decision D4; the binding rules above apply unchanged to either.

**The affiliate's account.**

| Affiliate account | Their customers' bindings | Their money |
|---|---|---|
| affiliate profile paused by the affiliate | stay `active`; nothing accrues while paused | pending commissions continue to approval |
| suspended by the operator (fraud review) | stay; nothing accrues | pending and approved commissions **held** until the review ends |
| account closure requested | `suspended` | approved balance paid out at the next run, regardless of the threshold |
| reactivated | `active` | as before |
| erased | **ended**; the customers are free, but having bought, they are not new customers and are bound to no one again | the balance was paid out; the credit notes stay as the law asks |

### 5.10 Who sees what

| | Sees | Never sees |
|---|---|---|
| Affiliate | clicks, sign-ups bound, orders by date, shop, product, quantity, base and commission, state; per label; customers bound (a count) and their purchases grouped by an anonymous customer number | the customer's name, e-mail, handle, country, or other purchases than the ones they earn on |
| Merchant | per affiliate: handle, website and channels they gave, clicks, orders, commission, declined commissions | the customer behind any order (as today: "never who bought") |
| Customer | their referrer's handle and the date | what the affiliate earned |
| Operator | everything, for support, fraud and tax | - |

Unlike UltraAds' reports, an affiliate's report is not folded below a
minimum count: the affiliate has to be able to check every commission. To
make up for it, dates are given by day, never by time, so an affiliate who
sent a link to a friend cannot read the friend's evening from it.

### 5.11 Fraud and abuse

| Risk | Defence |
|---|---|
| Self-referral | No binding to oneself; no commission on a purchase by the affiliate's own account (unless the merchant's rule allows it - open decision D8; the earlier scheme allowed it) |
| A merchant's staff earning from the merchant | No commission where the affiliate account is a member of the seller |
| Cookie stuffing, forced clicks | Clicks only from a top-level navigation by a person (§5.4); no image, script or frame counts; bursts from one address discarded |
| Claiming existing customers | Only new customers are bound (§5.5) |
| Fake accounts bought with stolen cards | Commission only after the holdback; chargebacks clawed back; new affiliates' first payout reviewed by the operator |
| Account farming to bind people | A binding earns only from paid, unreversed purchases; nothing is paid for a sign-up |
| Brand bidding, coupon sites, undisclosed posts | The programme's refused channels; a merchant declines with a reason; repeated, the operator suspends |
| Collusion (close an account, re-register through another affiliate) | The operator may end a binding and reverse its commissions; the audit log names who did |

### 5.12 Money: who pays whom

The operator is the network:

```
buyer pays            119.00 gross
  VAT                 - 19.00            (19 %, to the tax office)
  net                 100.00
  provider fee         - 3.00            (Skrill; an example figure)
  store fee            -15.00            (the operator; 15 % of net, as an example)
  commission           -10.00            (10 % of net, the merchant's rule)
  network fee          - 2.00            (20 % of the commission, D3)
  merchant             70.00
```

- The **merchant's settlement** (the portal's next step 2) deducts the
  commission and the network fee from the merchant's share. Under the base
  rate (§5.3) the commission comes out of the store fee instead.
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
- **Thresholds:** 25.00 per currency per month; on closure, paid regardless.

### 5.13 Per shop

| Shop | What is particular |
|---|---|
| App store | Single-product orders today; trials earn nothing until paid; a licence update bought anew is a new purchase |
| Games | Same as apps; in-game purchases sold through the store are purchases like any other once they exist; a free-to-play game's sign-up earns nothing |
| Books | An edition is a product (audiobook and e-book are two); a *Books Plus* credit spent is not a sale - the subscription is, and is the operator's own programme |
| Marketplace | Baskets with several sellers: attribution per line; physical goods' holdback runs from delivery; delivery never in the base; a return reverses in proportion |
| Music, video | Subscriptions are the operator's own product (its own programme); *Support* payments to a creator are gifts, never commissionable |
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
- `UltraStoreAdmin`'s partner list, two-tier credits, go/join pages and
  payouts are not migrated: the portal replaced the desktop tools with no
  compatibility kept, because they "were not in use" (the portal's README,
  *Decisions*).

### 5.15 Recruiting affiliates (the second tier)

The earlier scheme paid a recruiter a share of a recruit's sales, one level
up. The proposal **leaves it out of the first phases** and asks the owner
(D6). If kept, it should be:

- funded by the operator, never the merchant (the merchant pays for sales,
  not for the network's growth);
- a share of the recruit's *commission*, for 12 months, one level only;
- never paid for the recruiting itself - only on the recruit's approved
  commissions, so no one earns from sign-ups.

---

## 6. Where it lives

### 6.1 A module of the portal

`Server/UltraStorePortal/ultrastore_portal/affiliates/`, beside `promote/`,
off unless `ULTRASTORE_PORTAL_AFFILIATES=1`. The reasons are UltraAds': the
shops are variants on the portal, the orders and the account are there, and
one ledger serves every shop. Models, in outline:

| Model | Holds |
|---|---|
| `AffiliateProfile` | the account, state (`applied`, `active`, `paused`, `suspended`, `closed`), website and channels, payout method reference, tax status and VAT number, terms version accepted |
| `Programme` | the publisher, state, description, refused channels, pays for bound customers |
| `Rule` | programme, scope (shop / category / listing), kind of sale, `percent` or `fixed` with currency, renewals, validity, named affiliate |
| `Membership` | programme, affiliate, state (`applied`, `approved`, `declined`, `removed`), the merchant's note |
| `Click` | affiliate, listing, shop, label, account or session, at; aggregated into `ClickDay` after 30 days |
| `Binding` | customer (unique), affiliate, at, through, state |
| `Commission` | order line, affiliate, basis (`product_link`, `bound_customer`, `base_rate`), rule snapshot, base, amount, currency, state, approve-after date, statement |
| `Statement` | affiliate, month, currency, lines, credit note number, payout reference, state |
| `BaseRate` | shop, percent of the store fee, valid from |

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
| Daily: approve, aggregate clicks; monthly: statements | management commands, run by timers like `promote cleanup` |

### 6.3 Pages and calls

| Who | Where | What |
|---|---|---|
| Affiliate | `/affiliate/` | Become an affiliate; the dashboard; the **directory of programmes** (search, shop, category, rate, open or by approval); one programme with its rules and *Join*; links and QR codes for a product or a shop; reports by label; statements and credit notes; payout and tax details |
| Merchant | `/console/affiliates/` | The programme: state, description, refused channels; rules by scope with a preview ("Y earns 8.10 at today's price"); applications; affiliates with their results; declined commissions |
| Operator | `/operator/affiliates/` | Affiliates to review; fraud queue (bursts, self-purchases, chargebacks); base rate and network fee; statements to pay; bindings to end |
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
| L3 | The binding as personal data: the legal basis (contract, or legitimate interest with an objection right), what the privacy notice says, and whether a customer may ask for the binding to be removed. |
| L4 | Confirm that nothing may be kept after erasure to restore a binding (§5.9). |
| L5 | The second tier (§5.15): UCPD Annex I, point 14 forbids a scheme "where a consumer gives consideration for the opportunity to receive compensation that is derived primarily from the introduction of other consumers into the scheme rather than from the sale or consumption of products"; the Court of Justice has held that it needs a payment by the consumer, of whatever amount (*4finance*, C-515/12), and that the link between payments and rewards may be indirect (2016). A programme that is free to join and pays only on sales looks outside it - confirm, also for national law. |
| L6 | Paying affiliates out of money collected for merchants: is the operator acting as a payment service (PSD2), or within the commercial-agent exemption? |
| L7 | Minimum age and capacity of an affiliate; affiliates outside the EU (sanctions screening). |
| T1 | Self-billing credit notes for affiliates (§14(2) UStG): the agreement, the wording, VAT for a German affiliate, a small business (§19 UStG, reworked from 1 January 2025 into an exemption, with invoice rules of its own), an EU business (reverse charge), a private person, and outside the EU. Nothing found addresses affiliate commissions specifically [unverified]. |
| T2 | Whether the operator must report affiliate income under DAC7. "Personal services" facilitated through a platform are a reportable activity; software that only advertises or redirects is not a platform. Where an affiliate paid by the operator falls is not settled by anything found [unverified]. |
| T3 | The base of the commission and of the network fee for VAT; the network fee as a supply to the merchant. |

---

## 8. Phases

| Phase | What | Needs first |
|---|---|---|
| 0 | The owner's decisions (§9); the lawyer's and tax adviser's answers to L1-L4, T1 | - |
| 1 | **Product links and programmes in the app store**: affiliate profiles with a handle, programmes with percent and fixed rules, the directory, `/r/` links, clicks, the `ref` on the checkout, attribution of a paid order to a product link, commissions pending and reversed, the three dashboards; approval and payout marked by the operator by hand. Affiliate **codes** with it or right after: the portal has to learn to publish and apply codes first (its next step 11) | nothing new: an order is enough |
| 2 | **The binding**: bind at sign-up and first order, bound-customer rules, the operator's base rate, the customer's "Referred by"; **closing and reactivating an account**, which the portal does not have, with the binding suspended and restored | buyer accounts on orders (portal next step 3); the store's fee per line (next step 2) for the base rate |
| 3 | **Money and the other shops**: automatic approval after holdback, statements, self-billed credit notes, Skrill payouts; baskets with several sellers, physical goods' holdback, subscriptions' renewals; the native store's `ultrastore://r/` and `ref`; UltraAccount's *Affiliate* section | the settlement of shares and tax invoices (next step 2); the shops themselves |
| 4 | **Beyond the portal**: the conversions call for UltraPress shops and services; the second tier if decided; fraud scoring on the click aggregates | phase 3 |

Phase 1 is useful alone: merchants get affiliates selling their products,
and every rule of §5.6 that does not involve a binding is already in force.

---

## 9. Decisions needed from the owner

| # | Decision | Recommended |
|---|---|---|
| D1 | Who pays a bound customer's purchase of a product with no programme rule | **B: the operator's base rate** out of the store fee (§5.3) |
| D2 | Bind only new customers (no paid order before), or any customer not yet bound | **New customers only** (§5.5) |
| D3 | A network fee on top of commissions for merchants, and its base: a share of the commission, or of the sale (Awin uses both, by plan, §4) | **20 % of the commission**, waived for the first year to start programmes; a share of the sale would charge a merchant twice, beside the store's fee |
| D4 | "Reactivate": the portal has no account closure yet. A deletion with a grace of 30 days as the earlier service had, or a separate `closed` state for longer | A `closed` state of **12 months** if reactivation is meant to be possible later; else the grace |
| D5 | Does a general (not product) affiliate code take the commission from the bound affiliate | **No** - only a product link or a product code moves a commission |
| D6 | Keep the second tier (recruiting affiliates) | **Not at launch**; revisit after phase 3 |
| D7 | Balances per currency, or converted to one currency | **Per currency**, converted only at payout |
| D8 | May an affiliate earn on their own purchases through a product link | **No** by default; a merchant may allow it per rule |
| D9 | Windows: product link 30 days, sign-up 30 days, holdback 30 days | as stated |
| D10 | Threshold 25.00 per currency per month | as stated |
| D11 | When a product link wins a line, does the bound affiliate also earn on it (Digistore24 lets the vendor choose) | **No** - one commission per line, as the owner described |

---

## 10. Acceptance tests

The rules are tested where they live (`affiliates/` in the portal's tests),
one test per row of §2.1, and these besides:

- A product link with no product-link rule behind it does not take the line
  from the bound affiliate.
- A fixed commission times the quantity is cut to the merchant's share, and
  the merchant was warned when saving the rule.
- A rule changed after a sale does not change that sale's commission.
- A refund of half the line reverses half the commission; a chargeback after
  payment becomes a negative line on the next statement.
- A binding to oneself is refused; a purchase of the seller's own product
  credits no affiliate who is a member of that seller.
- A bound customer's purchase credits their affiliate at the merchant's
  bound-customer rule although the affiliate never joined that programme; a
  product link by an affiliate the programme did not admit credits no one,
  and the bound affiliate keeps the line.
- A click from an `<img>` or a frame records nothing.
- An account whose closure is requested earns its referrer nothing; the
  cancel restores the binding to the same affiliate; the erasure removes it,
  and the commissions earned before stay.
- An affiliate erased leaves their customers unbound and unbindable.
- A renewal goes to the first order's affiliate for the rule's number of
  renewals, then to no one.
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
  membership of the programme. Membership is for product links only.
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
<https://www.revenue.ie/en/companies-and-charities/international-tax/aeoi/dac7/index.aspx>.
The pages were read through search summaries, not opened; a figure taken
from them should be read at its source before it goes into a contract.
