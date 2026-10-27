- **After the purchase.** The licence carries the app's update period as
  the catalog had it when the order was paid, and the store holds to it:
  an update outside the purchase is refused, the card shows *Buy update*,
  *Update all* counts it as "to buy anew", the *Bought* badge names the
  terms. The licence reaches the app as `apps/<id>/licence.ulic`, written
  at install and at purchase, found and verified by the app with
  `FindLicenceFile` and `ReadLicenceFile` (`Purchase.h`); `ultrastore
  licence <app-id>` shows a purchase and checks the file. An order the
  store was closed in the middle of is remembered and picked up at the
  next start: paid meanwhile, the licence is kept; still open, the checkout
  opens again.
