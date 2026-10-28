- **Buy through the payment service.** A catalog that names one
  (`payment_url`, `licence_key`) makes *Buy* ask it for the order with the
  code in force, open the checkout in the browser, wait for the payment
  (the download panel shows it; half an hour at most), verify the licence
  against the catalog's key and keep it in the registry; a bought app
  shows *Bought*, *Install* instead of *Buy*, its receipt number and a
  *Receipt* button, and its restricted package is fetched through the URL
  the service signs for the order, on every install and update. Without
  a payment service *Buy* still goes to the publisher's page. `ultrastore
  buy` and `licences` on the command line.
