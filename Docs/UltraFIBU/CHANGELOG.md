#### 2026-09-18 *0.1.0*
- **UltraFIBU exists: the engine a German bookkeeping program is built on, and
  a command line that already uses it.** Phase A1 of
  `Docs/Research/UltraFIBUDesignProposal.md` - master data, the two calendars,
  and a store written for both a local file and a shared server. No UI yet: the
  German screens wait for the framework's data grid (proposal §3.3), and
  everything below is reachable through `ultrafibu` in the meantime.
- **A Geschäftsjahr whose start date is free.** `01.04.2026 - 31.03.2027` is an
  ordinary row: period 1 is April, period 10 is January, and a
  Rumpfwirtschaftsjahr is simply a year of fewer than twelve periods. The store
  refuses an overlapping year and one that leaves a gap, because a posting that
  belongs to two fiscal years - or to none - has no correct report.
- **Two calendars over one journal.** The Umsatzsteuer-Voranmeldung is always a
  calendar month or quarter (§ 18 UStG) whatever the fiscal year is, so
  `Voranmeldungszeitraum` is a separate type: ELSTER period codes (01..12 for
  months, 41..44 for quarters), the deadline on the 10th, a month later with a
  Dauerfristverlängerung, moved off a Saturday or Sunday (§ 108 Abs. 3 AO) and
  deliberately *not* off a public holiday, since those differ by Bundesland.
  OSS quarters and their end-of-following-month deadline sit beside it. For a
  1 April fiscal year the reporting periods span two calendar years, which the
  tests assert rather than hope for.
- **Festschreibung only moves forward.** Freezing a period is irreversible by
  design (GoBD): an attempt to move the date backwards is refused *and*
  recorded in the audit trail, and every correction after that is a Storno.
- **European VAT numbers, checked offline.** Formats for all 27 member states
  plus Northern Ireland's XI, and check digits for the fifteen whose rule is
  unambiguous (DE, AT, BE, DK, EE, EL, FI, FR, HU, IT, LU, PL, PT, SE, SI, SK).
  Where a state relaxed its rule - the Netherlands - or mixes several schemes -
  Spain, Ireland - the answer is "format correct, no check digit computed"
  rather than an invented verdict: a validator that rejects a valid number is
  worse than one that admits what it cannot tell. Every verdict carries a
  German sentence for the UI.
- **VIES and the BZSt, with the transport injected.** `PruefeUstIdNrVies` builds
  the European Commission's REST request (the simple `GET .../ms/{LAND}/vat/{NR}`,
  or the `POST` that carries the enquirer's own number and therefore returns a
  consultation number) and reads the answer; `PruefeUstIdNrBzst` does the same
  for the qualified confirmation under § 18e UStG, the only one that actually
  protects a zero-rated intra-community supply. The response is stored **byte
  for byte**, because the authority's data set *is* the evidence. A member state
  that is not answering (`MS_UNAVAILABLE`) is a failed enquiry and never an
  invalid customer - the tests pin that distinction down, along with a
  non-JSON answer, a partly confirmed qualified enquiry, and the cases that are
  refused before a request is made.
- **Money is never a double**, anywhere: amounts are `UltraCanvasMoney`
  (framework 0.8.85), and a Steuerschlüssel computes its tax through it.
- **The chart of accounts and the tax keys are data, not code.** `data/SKR03.csv`
  ships 74 of the most-used SKR03 accounts - each with its BWA position and its
  balance-sheet classification, because the Jahresabschluss is in scope -
  and `data/Steuerschluessel.csv` 14 tax keys. Only UStVA Kennzahlen that have
  been verified are filled in (81, 86, 87, 41, 66, 45); the rest are
  deliberately empty, and a test fails if an unverified one ever appears. A
  guessed Kennzahl produces a wrong return, an empty one is caught when the
  return is built. Both files are a starting point that the Kanzlei's own
  category-20 export overrides.
- **The store is written for a server from the first commit.** Surrogate keys
  come from a `sequenz` table rather than AUTOINCREMENT, dates are ISO text,
  money is BIGINT minor units, every value is bound and every write is a
  transaction - so moving from SQLite to PostgreSQL is a change of one
  configuration field once UltraDatabase's Stage 2 driver exists. Until then
  `OpenServer` says exactly that instead of silently opening a local file, and
  refuses a literal password outright (credentials belong in UltraVault).
- **What multi-user needs is already here**: document numbers allocated inside
  the transaction that uses them (never `SELECT MAX(...) + 1`, which duplicates
  under two writers) with an optional yearly reset; optimistic locking on
  partner records, so saving a row somebody else changed is refused with a
  German message instead of overwriting their work; users, five roles and a
  permission matrix enforced **in the store**, not only in the UI, because in
  server mode the database is reachable without it; Argon2id passwords through
  UltraCrypt with the parameters stored per user; an audit trail on every write
  and on every refusal; and a startup gate that refuses to open a database
  whose schema is newer than the binary.
- **Person accounts follow the Sachkontenlänge** rather than being hard-coded
  for four digits: customers from 10000 and suppliers from 70000 with a 4-digit
  chart, 100000/700000 with five, and so on.
- **`ultrafibu`** (`cli/main.cpp`): `einrichten` (company, fiscal year, chart,
  tax keys, number ranges in one call), `info`, `konten`, `partner`,
  `partner-neu`, `ustid`, `termine`, `perioden`, `festschreiben` (which asks
  before doing something irreversible) and `protokoll`. German output
  throughout.
- **`Tests/UltraFIBU`**: 493 checks over the calendar, the fiscal year, both
  reporting calendars, the VAT-number rules, the data files, the online
  confirmation readers and the store. The VAT check-digit tests assert the
  *properties* a correct rule must have - exactly one accepted check digit per
  prefix, every single mistyped digit detected - rather than quoting test
  vectors from memory, which would prove nothing if the memory were wrong.
  Enabled with `-DULTRACANVAS_BUILD_ULTRAFIBU_TESTS=ON`; runs headless, without
  the UI library.
