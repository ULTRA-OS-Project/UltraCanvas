- **The marketplace.** The merchant's side of the operator's marketplace
  service (`Market.h`; `Server/UltraStoreMarket`): `market submit <app-id>`
  sends a release to the review, the app's component object signed with
  this backend's key, every request signed the way the service verifies
  it; `market notices` and `ack`, `market terms` and `terms accept`,
  `market standing`, `market submissions`, `withdraw`, `market reports`.
  The settings `market.url`, `market.merchant_id` and `market.key`. The
  window's *Marketplace* page does the same, and the operator's
  unacknowledged notices show as a banner above every page until
  acknowledged.
