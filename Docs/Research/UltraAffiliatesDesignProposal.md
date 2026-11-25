# UltraAffiliates — the affiliate programme of the ULTRA OS shops and marketplaces (Investigation and Proposal)

**Date:** 2026-11-25
**Status:** Proposal. Nothing of it is built; §3 says what the repository
already has that it builds on, and §9 lists the decisions it needs from the
owner before the first line of code.
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
| The affiliate is any UltraAccount that accepts the affiliate terms (§5.1) | The account already exists for every shop and speaks OpenID Connect (the portal's *One sign-in for every service*). An affiliate needs no second identity, only a payout method, tax details and the terms. |
| The operator is the network (§5.12) | The affiliate contracts with the operator, not with each merchant: one set of terms, one self-billed credit note a month, one payout, one tax relationship. The merchant pays the commission out of its share of the sale, which the operator already settles. This is how Awin, CJ and Impact work [unverified in detail], and it is the only model in which a customer bound in the games shop can earn their affiliate money in the book shop. |
| Two ways in, from one programme (§5.2, §5.4) | **Merchants publish programmes** (which products, percentage or fixed amount per product, open or by approval) and **affiliates browse them** and take links. The same programme record serves both doors. |
| The binding is first-touch and permanent (§5.5) | The owner's rule. The first affiliate whose link brings a **new** customer - one who has never paid for anything in any shop - is that customer's referrer for good. Existing customers cannot be claimed by sending them a link. |
| A product link wins its product, last click, 30 days (§5.6) | The owner's override. A link to a product credits its affiliate for that product, bought within the window, and nothing else in the basket; the binding does not move. 30 days is the window the store's codes already use (`StoreSettings::kCodeDays`). |
| What a bound customer's purchase pays: the merchant's rule, else the operator's base rate (§5.3) | "Every later purchase" must pay something even where the merchant runs no programme, or the binding is worth nothing in most of the catalogue. The operator funds a small base rate out of its own fee; a merchant's programme replaces it where it has one. This is open decision D1. |
| Commissions are pending, then approved, then paid (§5.8) | A sale can be refunded, returned or charged back. A commission is approved only after the product's return window and a margin, is taken back in proportion to a refund (as `Apps/UltraStoreAdmin` already does), and is paid monthly above a threshold. |
| The binding follows the account (§5.9) | Closed: suspended, nothing accrues. Reactivated: restored, to the same affiliate. Erased: gone; a new account is a new person - but one with a purchase history is not a *new customer*, so it cannot be bound again by a link. |
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
| 10 | C closes the account | nothing accrues | **suspended** |
| 11 | C reactivates the account, buys X2 | A | A |
| 12 | C's account is erased; the same person signs up again through D's link | D gets nothing for the old purchases; the new account is a new customer, so **D** binds it | D |
| 13 | New visitor E clicks B's link to Y, signs up, buys Y and Q | B for Y (product link) and for Q (bound customer) | **B** |
| 14 | Existing customer F (has bought before, never referred) clicks B's link to Y, buys Y | B, for Y | none: F is not a new customer, so F is not bound |
| 15 | C's subscription bought through B's link to it renews | B, for as many renewals as the rule says (§5.7) | A |
| 16 | Y from example 3 is refunded | B's commission for Y is taken back | A |

Example 12 is the one point where the owner's words leave room: "lost unless
he reactivates" is read as *erasure ends it, closure does not*. §5.9 explains
why a binding cannot outlive the account's erasure, and open decision D4 asks
whether a closed account should stay reactivatable longer than the 30 days of
grace the account service has today.

---

## 3. What exists today

| Piece | What is there | Where |
|---|---|---|
| An affiliate scheme, merchant-side | Partners with codes and a share of each sale made with them; **two tiers**: a recruiter earns a recruit share one level up, never further; QR codes and `go/<CODE>`, `join/<affiliate>` pages; a sales ledger with credits, reversals taken back in proportion, payouts recorded, a report of what is owed | `Apps/UltraStoreAdmin/core/Affiliates.{h,cpp}`, `Tests/UltraStoreAdmin/test_affiliates.cpp`, `Docs/UltraStoreAdmin/README.md` |
| Codes in the catalog | `codes[]`: kind `discount` or `affiliate`, what it takes off, the apps, the validity, the partner's opaque id | `Docs/UltraStore/README.md`, *Affiliates and codes* |
| Attribution in the store app | One code kept in the settings, in force **30 days**, sent with the checkout; `ultrastore://code/<CODE>` and `ultrastore://app/<id>?code=<CODE>` links | `Apps/UltraStore/core/StoreSettings.h` (`kCodeDays`) |
| The portal, which replaces all of the above | One Django application for the store at `store.ultraos.eu`; it **decided to keep no compatibility** with the earlier services and desktop tools (2026-11-20). An order keeps the `code` sent with it and **takes nothing off and credits no one** | `Server/UltraStorePortal/README.md`, *Decisions*, *What comes next* 11; `billing/models.py` (`Order.code`) |
| Orders | One app per order, no buyer account on it (a purchase is reached by the order's secret); `Payment` rows for payments, refunds and chargebacks | `billing/models.py` |
| Buyer accounts on orders | **Missing**, the portal's next step 3 | the portal's README, *What comes next* |
| The split of a sale, tax invoices, payouts | **Missing**, the portal's next step 2 | idem |
| One account for every shop | The portal as an OpenID Connect provider; UltraAccount with a **handle** (3 to 32 letters, digits, dots, underscores) | the portal's README, *One sign-in for every service*; `Server/UltraStoreAccount/README.md` |
| Account deletion | Requested with the password, `deleting` for 30 days (signed in still, *cancel* restores it), then carried out: the row kept under its id with the email replaced | `Server/UltraStoreAccount/README.md`, *What a deletion does* |
| Click attribution in advertising | UltraAds' signed click token, kept a few days and sent with the checkout so the order names the campaign; totals, never who; small numbers folded | `Docs/Research/UltraAdsDesignProposal.md` §4.9, the portal's `promote/` |
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

---

## 4. How others do it

The owner's model is a combination that few programmes offer in one place:
**lifetime first-touch attribution of the customer** plus **last-click
attribution of a product**. What exists, by kind:

| Kind | Examples | Attribution | What we take from it |
|---|---|---|---|
| Retail associates | Amazon Associates | Last click, a cookie of 24 hours, 90 days for an item put in the basket; commission by product category; no lifetime binding [unverified in the figures] | Rates per category and per product; a product link credits the product; short windows are the norm for retail. |
| Affiliate networks | Awin, CJ, Impact, Rakuten Advertising | The network contracts with both sides; the advertiser pays commission plus a network override (often around a third of the commission) [unverified]; transactions *pending* until validated by the advertiser after the return period; payouts monthly above a threshold | The operator-as-network model, the pending-approved-paid life, the validation window, the threshold. |
| SaaS referral tools | Rewardful, FirstPromoter, PartnerStack | The customer is attributed for good to the first referrer; commission on each recurring payment, for a set number of months or for life [unverified per tool] | Lifetime customer binding is an established, accepted model where the product is an account-based relationship - which every ULTRA OS shop is. |
| Digital marketplaces | Digistore24, ClickBank, Envato | Per-product commissions set by the vendor; vendors list products in a directory affiliates browse; some offer lifetime commissions on a buyer's later purchases from the same vendor [unverified] | The **directory of programmes**: the merchant publishes terms, the affiliate picks and gets links - the two doors the owner asked for. |
| Game stores and creator codes | Epic Games *Support-A-Creator* (a creator code stays applied for a set number of days after entry) [unverified]; Apple ended commission on apps in its affiliate programme in 2018 [unverified] | A code entered once, kept for a time | A code as a carrier, equivalent to a link (§5.4). Apple's withdrawal is a reminder that app-store affiliate commissions are a margin decision, made per merchant here, not imposed. |
| Two-tier programmes | Many networks pay a small recruiting bonus on a referred affiliate's earnings | One level, a share of the recruit's commission | Kept as an option at most (§5.15); deeper levels are where affiliate programmes turn into pyramid schemes, which the EU unfair-practices directive forbids [unverified - §7]. |

What none of them does, and this one must: **the customer binding crosses
merchants**. A Digistore-style lifetime commission binds a buyer to an
affiliate *for one vendor*. Here, a customer brought to the games shop earns
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

The affiliate's public name is their UltraAccount **handle**; links carry it
(§5.4). An affiliate without a handle is asked for one when they sign up.

### 5.2 Programmes: what a merchant sets

A merchant has **one programme**, off until switched on. It holds:

| Field | Values | Default |
|---|---|---|
| State | `off`, `open` (any affiliate may take links), `approval` (an affiliate applies, the merchant decides), `invite` (only affiliates the merchant invites) | `off` |
| Description and terms | Free text shown in the directory: what the merchant wants and does not want (no brand-word bidding, no coupon sites, no e-mail campaigns, ...) | empty |
| Channels refused | Ticks: search ads on the merchant's brand, coupon and voucher sites, cashback sites, e-mail, social posts without disclosure | none |
| Rules | A list, below | none |
| Pays for bound customers | `yes` (its *bound customer* rules apply to any customer bound to any affiliate) or `no` (the operator's base rate applies instead, §5.3) | `yes` |

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
   is refused, and so is one to a member of a merchant whose staff account
   it is (§5.11).
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
           whose affiliate may earn on P          # in the programme, not suspended,
                                                  # not the seller's own member
    if link and P's programme has a product-link rule for that affiliate:
        credit link's affiliate at that rule      # the binding does not move
    else if the customer has an active binding to affiliate A,
            and A may earn on P:
        credit A at P's bound-customer rule,
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
  affiliate never both earn on the same unit.
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
| `reversed` (in part or whole) | a refund, a return or a chargeback before approval | in proportion to the amount refunded, as `UltraStoreAdmin`'s reversals do |
| `payable` | approved, and the affiliate's approved balance in that currency is **25.00** or more at the month's end | below it, carried to the next month |
| `paid` | the monthly payout ran and the provider confirmed | the self-billed credit note is issued (§5.12) |
| `clawed back` | a chargeback after approval or payment | a negative line on the next statement; the operator carries the loss if the affiliate leaves with a negative balance |

A merchant may **decline** a pending commission only for a reason the terms
list (the order was fraudulent, the channel was one the programme refuses),
the affiliate sees the reason, and the operator decides a dispute.

### 5.9 Closing, reactivating and erasing accounts

**The customer's account.**

| Account | Binding | What accrues |
|---|---|---|
| active | `active` | as §5.6 |
| closure requested (`deleting` today, 30 days of grace) | **`suspended`** from the moment of the request | nothing for the bound affiliate; a product-link purchase made in the grace still credits its link (the account is signed in still) |
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

**How long a closed account can be reactivated** is today the 30 days of the
account service's grace. If the owner wants "reactivate" to mean months, the
account needs a `closed` state that is not a deletion: signed out, nothing
sold to it, kept for a set time (say 12 months) and then erased unless
reactivated. Open decision D4; the binding rules above apply unchanged to
either.

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
  provider the store already uses, which can send money to a Skrill account
  [unverified for the merchant account's API]) or by SEPA transfer later.
- **Each payout comes with a self-billed credit note** (*Gutschrift*) the
  operator issues in the affiliate's name, which the affiliate agrees to in
  the terms: the net commission, VAT where the affiliate charges it, the
  reverse-charge note for an affiliate with an EU VAT number in another
  country, none for a small business or a private person who says so [all
  §7, T1-T3]. UltraFIBU's design already plans self-billed documents and
  DATEV export; this is a customer of it.
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

- The catalog's `codes[]` stay as they are; an affiliate code names the
  affiliate's **handle** rather than an opaque partner id once the portal
  writes them.
- The store's `code.value` / `code.enteredAt` keep the code; a new
  `referral.token` / `referral.at` keep the last `ultrastore://r/` link; both
  go with `POST /v1/checkout` (`{app, code, ref}`).
- `UltraStoreAdmin`'s partner list, two-tier credits, go/join pages and
  payouts are not migrated: the portal replaced the desktop tools with no
  compatibility kept, and no merchant used them in production [unverified -
  owner to confirm].

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
| An order paid → bind, then attribute | `billing/services.py`, where status 2 pays an order |
| A refund, return or chargeback → reverse | the same file, where a `Payment` of kind refund or chargeback is recorded |
| Sign-up → bind | `web/account.py`, the sign-up's last step |
| Account closure, cancellation, erasure → suspend, restore, remove | the account's deletion request, cancel and maintenance (the portal's own accounts; `Server/UltraStoreAccount` while it lives) |
| `/r/` pages | `urls.py`, one view per shop root |
| The checkout's `ref` | `api/billing.py` (`POST /v1/checkout`) |
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
| L1 | Affiliates' disclosure: the terms require every post or page that carries a link to say it is advertising (UWG §5a(4), the UCPD). What must the operator do to police it, and is it liable for an affiliate who does not? |
| L2 | Recording a click on the device of a visitor who is not signed in: is it storage "strictly necessary" (TTDSG §25, ePrivacy Art. 5(3)) or does it need consent? The proposal keeps it to the session without consent. |
| L3 | The binding as personal data: the legal basis (contract, or legitimate interest with an objection right), what the privacy notice says, and whether a customer may ask for the binding to be removed. |
| L4 | Confirm that nothing may be kept after erasure to restore a binding (§5.9). |
| L5 | The second tier (§5.15): what keeps it clear of the pyramid-scheme ban (UCPD Annex I, 14). |
| L6 | Paying affiliates out of money collected for merchants: is the operator acting as a payment service (PSD2), or within the commercial-agent exemption? |
| L7 | Minimum age and capacity of an affiliate; affiliates outside the EU (sanctions screening). |
| T1 | Self-billing credit notes for affiliates: the agreement, the wording, VAT for a German affiliate, a small business (§19 UStG), an EU business (reverse charge), a private person, and outside the EU. |
| T2 | Whether the operator must report affiliate income (DAC7 covers sellers on platforms; affiliates are probably not "sellers" [unverified]). |
| T3 | The base of the commission and of the network fee for VAT; the network fee as a supply to the merchant. |

---

## 8. Phases

| Phase | What | Needs first |
|---|---|---|
| 0 | The owner's decisions (§9); the lawyer's and tax adviser's answers to L1-L4, T1 | - |
| 1 | **Product links and programmes in the app store**: affiliate profiles, programmes with percent and fixed rules, the directory, `/r/` links and codes, clicks, attribution of a paid order to a product link, commissions pending and reversed, the three dashboards; approval and payout marked by the operator by hand | nothing new: an order is enough |
| 2 | **The binding**: bind at sign-up and first order, bound-customer rules, the operator's base rate, account closure and reactivation, the customer's "Referred by" | buyer accounts on orders (portal next step 3); the store's fee per line (next step 2) for the base rate |
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
| D3 | A network fee on top of commissions for merchants | **20 % of the commission**, waived for the first year to start programmes |
| D4 | "Reactivate": within the 30-day deletion grace, or a separate `closed` state for longer | A `closed` state of **12 months** if reactivation is meant to be possible later; else the grace |
| D5 | Does a general (not product) affiliate code take the commission from the bound affiliate | **No** - only a product link or a product code moves a commission |
| D6 | Keep the second tier (recruiting affiliates) | **Not at launch**; revisit after phase 3 |
| D7 | Balances per currency, or converted to one currency | **Per currency**, converted only at payout |
| D8 | May an affiliate earn on their own purchases through a product link | **No** by default; a merchant may allow it per rule |
| D9 | Windows: product link 30 days, sign-up 30 days, holdback 30 days | as stated |
| D10 | Threshold 25.00 per currency per month | as stated |

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
- A binding to oneself and to a member of the seller is refused.
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
