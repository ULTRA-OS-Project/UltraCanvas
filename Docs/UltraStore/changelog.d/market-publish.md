- **Report a problem.** The app page gets *Report a problem* when the
  catalog names the operator's marketplace service (`market_url`): the
  kind and the text go to the service's reports, the licence names the
  reporter when the app was bought here, and the outcome the operator
  gives shows on the page after a refresh. `ultrastore report
  <catalog.json> <app-id> <kind> <text>` and `ultrastore reports` do the
  same from the command line; the receipts are kept in the settings.
- **The operator publishes the catalog.** The marketplace service (0.2.0)
  generates the signed `catalog.json` from the released submissions
  (`POST /v1/publish`): the merchants' components as sent, the
  publishers, the trusted keys, the payment service, `market_url`; the
  operator's signing key is kept by the service and `GET /v1/publish/key`
  gives its public half for the store's keys.
