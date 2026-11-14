- **The Finance page** (the design's `AdminFinance` artboard, on
  `Finance.h`): the books from the ledger for a period - all time, a year,
  a month - as the statement by currency (paid, given back, the partners'
  credits, net to the store), net by month as a bar chart in the main
  currency, revenue by app, the partners with what they earned, what a
  reversal took back, what was paid out and what they are owed, and the
  refunds and chargebacks of the period; *Statement CSV* writes the period
  for the books. The same from the command line: `finance report`,
  `finance periods`, `finance statement [-o <file.csv>]`.
- **Refunds and chargebacks in the ledger.** `sale sync` (the window's
  *Sync sales* too) pulls the payment service's reversals
  (`GET /v1/reversals`) once each, the sequence kept in
  `payment.reversals_seq`; `sale reversal` records one by hand;
  `sale reversals` lists them. A reversal takes the partners' credits back
  in the proportion of the amount reversed, never more than the sale
  credited, so a partner keeps no credit on money the store gave back.
- **Payouts.** `affiliate payout <id> <amount> <currency>` and the Finance
  page's *Record payout* record what was paid to a partner; `affiliate
  report` and the page say what is still owed: earned, less taken back,
  less paid out. `admin.sqlite` schema 5: the `reversals` and `payouts`
  tables; an older database is upgraded on opening.
