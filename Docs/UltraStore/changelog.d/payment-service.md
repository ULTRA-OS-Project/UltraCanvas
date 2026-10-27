- **The payment service** (`Server/UltraStorePay`): a Skrill Quick Checkout
  session per purchase, priced from the signed catalog with the code in
  force; Skrill's status report verified (the signature, the amount, the
  currency) before an order is paid; the licence an app checks offline
  (Ed25519-signed), the numbered receipt, the signed ten-minute URLs for
  the packages of paid apps, and the sales for the admin backend's ledger.
  The store's *Buy* through it is the next step.
