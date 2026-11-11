- **The marketplace call speaks PUT.** `Market.h`'s HTTP call (shared with
  the operator's client, `Apps/UltraStoreOperator`) sends a PUT with a
  JSON body the way it sends a POST; nothing the merchant backend does
  changes.
