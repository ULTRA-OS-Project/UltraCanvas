- **Subscriptions.** An app priced `monthly` or `yearly` is bought through
  Quick Checkout's recurring billing: the first payment at the price after
  the code, then the list price every month or year. The payment service
  (0.5.0) sets the paid period, re-issues the licence on each renewal
  Skrill reports with a later `valid_until` (the period plus the grace
  days Skrill retries in), numbers a receipt per payment, and hands the
  renewals to the admin backend as sales of their own. The store takes the
  re-issued token when it refreshes the catalog (`StoreEngine::SyncLicences`,
  the licence file with it); `ultrastore licences --sync <catalog.json>`
  does it by hand and shows a subscription's day; `ultrastore unsubscribe
  <catalog.json> <app-id>` ends one - no more charges, the licence until
  the paid period ends. An app checks the day after verifying its file:
  `Licence::ValidOn(today)`.
- **Refunds and chargebacks.** The operator refunds a payment through the
  service (`POST /v1/orders/{order}/refund` with the admin token, whole or
  in part, through Skrill's refund API); a chargeback reaches the service
  from Skrill's status report. When everything paid is reversed the
  licence is taken back: the service hands it out no more and refuses the
  app's downloads, and the store removes it from the registry and the
  app's folder at its next catalog refresh. `GET /v1/licences/{id}` tells
  an app that is online whether its licence stands; `GET /v1/reversals`
  is the feed of refunds and chargebacks for the admin backend. The
  service's database moves to schema 2 (a `payments` table) on its own.
