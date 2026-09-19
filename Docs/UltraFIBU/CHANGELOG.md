#### 2026-09-19 *0.5.0*
- **DATEV-Export: der Buchungsstapel.** `Apps/UltraFIBU/engine/UltraFIBUDatev.{h,cpp}`,
  `ultrafibu datev-export` und `ultrafibu datev-pruefen`, 49 weitere Prüfungen
  (707 insgesamt). Phase A3 des Vorschlags, Kategorie 21 (Buchungsstapel) und
  20 (Kontenbeschriftungen).
- **Die Spaltenreihenfolge ist eine Datendatei, kein Quelltext**
  (`data/DATEV-Buchungsstapel-v700.csv`). DATEV gibt rund 120 Spalten in fester
  Reihenfolge vor, und **diese Datei ist noch nicht an einer echten DATEV-Datei
  geprüft** - sie sagt das oben in sich selbst. Der Export schreibt jeden Wert
  **über den Spaltennamen**, nicht über die Position, also wandern die Werte
  beim Korrigieren mit und es braucht keinen neuen Build.
  - **`ultrafibu datev-pruefen <echte_datei.csv>`** liest die Kopf- und die
    Spaltenzeile einer echten DATEV-Datei und meldet jede Abweichung mit ihrer
    Position. Damit hört die Reihenfolge in dem Moment auf, eine Vermutung zu
    sein, in dem eine echte Datei vorliegt - ein Befehl, keine Nachprogrammierung.
    Der Test korrumpiert einen Spaltennamen und prüft, dass genau diese Position
    gemeldet wird.
- **Drei Formatregeln, die eingebaut und nicht später entdeckt sind:**
  - **Belegdatum ist TTMM - vier Stellen, kein Jahr.** DATEV leitet das Jahr aus
    dem Wirtschaftsjahr ab. Deshalb schreibt der Export **eine Datei je
    Kalendermonat** und weist einen Monat zurück, der nicht vollständig im
    Geschäftsjahr liegt. Für ein Geschäftsjahr ab 1. April ist das der
    Unterschied zwischen einem richtigen Export und einem, der Dezember
    stillschweigend in den Januar bucht.
  - **Umsatz ist vorzeichenlos**; die Richtung trägt das
    Soll-/Haben-Kennzeichen. Ein vorzeichenbehafteter Betrag ergibt ein
    plausibel aussehendes, falsches Hauptbuch. Der Test prüft, dass in der
    ganzen Datei kein Minuszeichen steht.
  - **Die Datei ist CP1252 mit CRLF**, nicht UTF-8. Ein Umlaut als UTF-8
    geschrieben kommt in der Kanzlei als zwei falsche Zeichen an und bleibt dort
    zehn Jahre stehen. Derselbe CP1252-Bereich wie beim Euro-Zeichen im
    PDF-Writer; hier wird er direkt getestet, Byte für Byte.
- **Was fehlt, wird gesagt statt weggelassen.** Ohne Berater- und
  Mandantennummer - beide vergibt die Kanzlei - lehnt DATEV den Import ab, also
  lehnt der Export vorher ab und nennt beide. Hat eine Buchung einen
  Steuerschlüssel, aber keinen DATEV-BU-Schlüssel, warnt der Export: die Datei
  importiert, aber DATEV bucht ohne Steuerautomatik. Die Zuordnung steht in
  `data/Steuerschluessel.csv`, Spalte `datev_bu`, und ist dort noch leer.
- **Kategorie 16 (Debitoren/Kreditoren) ist bewusst nicht dabei.** Rund 240
  Spalten, deren Reihenfolge zu raten schlechter wäre als sie nicht anzubieten.
  Sie wartet auf eine echte Datei - dann ist sie eine weitere Datendatei.
- **`ultrafibu einrichten` kennt jetzt Berater- und Mandantennummer**, wie
  zuvor schon die Pflichtangaben nach § 14 UStG.

#### 2026-09-19 *0.4.0*
- **Die Oberfläche: vier Bildschirme über der Engine.** `Apps/UltraFIBU/ui/`,
  target `ultrafibu-ui`. Belege, Journal, Summen und Salden, Partner - each a
  sortable, filterable table over `UltraCanvasListView` and
  `UltraCanvasListSortFilterProxy`, in German, reading a real bookkeeping file.
  This is what phase B1's sorting work was built for.
- **One panel, four screens.** `TabellenPanel` is a search box, a table and a
  summary line; the screens differ only in the columns they declare and the
  store call that fills them. Writing that four times is how the fourth one
  ends up subtly different from the first.
- **The columns sort by value, not by their text.** `TabellenModell` answers
  `ListDataRole::SortRole`, so `1.232,80` sorts after `404,60` instead of
  before it the way those read as text, and `15.06.2026` sorts by its day
  number rather than by its day of month.
  - `ListDataValue` has no `int64` alternative - its choices are string, int,
    float and Color - and an amount in minor units passes 2^31 at
    **21.474.836,47**, a figure a company can genuinely invoice in a year.
    Truncating there would have mis-sorted silently. So the panel installs a
    per-column comparator through the proxy's `SetColumnComparator` seam, which
    reads the exact `int64` from the model; `SortRole` stays as the fallback
    for anything driving the model without one.
- **A row carries its record's id, never its position.** The proxy re-orders
  rows, so a selection is mapped back through `MapToSource` before anything
  acts on it. That is the mistake the proxy's own documentation warns about,
  and a sorted table that opens the wrong invoice is how it looks.
- **Two actions, and only two**: post the selected draft, and print it as a
  PDF. Each is one engine call, and every rule that protects the ledger - the
  draft state, the frozen period, the role - stays in the store, so the button
  can do nothing the CLI could not. Entering a document still belongs to
  `ultrafibu beleg-neu` until the position editor exists.
- **The journal screen verifies the hash chain and the double entry every time
  it loads**, and says so in its summary line. Both are statements about the
  whole ledger, so they are deliberately not recomputed when a filter narrows
  the view - half a ledger has no reason to balance.
- **A bug found by looking at the running window, not by reading the code.**
  With a static summary line, filtering the Belege list to one customer left
  *"4 Beleg(e), offen insgesamt 3.904,60 EUR"* under a single row. A total that
  does not describe what is above it is worse than no total, because somebody
  reads it. The summary is now a function of the visible rows, and says
  "1 Beleg(e) von 4 (gefiltert)" when it is showing a subset. Two column widths
  that clipped `05.08.20…` and `Teilweise bez…` came from the same look.

#### 2026-09-19 *0.3.0*
- **Die Rechnung als PDF.** `Apps/UltraFIBU/report/UltraFIBURechnungPdf.{h,cpp}`,
  the target `UltraFIBUReport`, `ultrafibu rechnung-pdf`, and 32 further checks
  (658 in total). Phase A2's remaining half, minus the screens.
- **There is no PDF writer in it.** The framework already has one -
  `UltraCanvas::VectorConverter::PDFVectorConverter`, which writes a
  self-contained PDF 1.4 from a `VectorStorage::VectorDocument`. This module
  builds that document and hands it over. Writing a second emitter beside the
  existing one would have been the same mistake as building a second data grid
  beside `UltraCanvasListView`, and this time the tree was checked first: both
  sources the writer needs reference nothing outside `VectorStorage`, so an
  invoice can still be produced on a server with no display, no pango and no
  vips. The build compiles those two files directly rather than linking the
  Vector plugin, because the plugin links the UltraCanvas core and would drag
  the whole rendering stack in behind it.
- **§ 14 UStG decides the content, not taste.** `PruefePflichtangaben` checks
  every mandatory field - both addresses in full, the supplier's Steuernummer
  or USt-IdNr., the date of issue, the sequential number, quantity and
  description, the date of supply, the base and rate per tax rate, and the
  recipient's USt-IdNr. when the supply is an intra-community one. What is
  missing is **named**, in German, and `ultrafibu rechnung-pdf` exits non-zero
  for it: an invoice missing these is legally deficient and its recipient
  cannot deduct the input tax from it, which is the customer's problem as much
  as ours.
- **A zero-rated line names its exemption**, which § 14 Abs. 4 Nr. 8 requires:
  innergemeinschaftliche Lieferung, Ausfuhr, § 13b reverse charge, One-Stop-Shop,
  § 19 Kleinunternehmer and nicht steuerbar each have their wording, taken from
  the Steuerschlüssel valid on the Belegdatum rather than from today's table.
- **The VAT summary is per rate**, one line per tax key in the order the
  positions introduced them - so the invoice and the journal read the same way
  round. A Gutschrift says "Gutschrift" and a reversal says "Stornorechnung",
  because a credit note that looks like an invoice is how a customer pays twice.
- **Amounts line up.** Nothing is embedded, so the writer approximates centre
  and right anchoring from an average glyph width - which a money column cannot
  be. Right alignment is therefore computed here from the base-14 Helvetica
  advance widths: every digit is 556/1000 em, which is exactly the property a
  column of amounts depends on, and the test pins it.
- **Three layout faults found by looking at the rendered page**, not by reading
  the code: the Leistungsdatum value printed straight through its own label, the
  ENTWURF mark was drawn across the opening paragraph, and the standard sentence
  about the date of supply sat in a field where it belonged in a note. A meta
  value too wide for its row now takes the next line, the document's state is a
  line under the heading instead of a watermark, and the sentence moved under
  the total where a German invoice puts it.
- **An invoice that does not fit one page is refused**, with the reason. The
  framework's writer emits a single page (proposal §3.2); dropping the last
  positions off the bottom of an invoice is a failure nobody notices until the
  customer pays the wrong amount, and shrinking the type until it fits produces
  something nobody can read. Multi-page output is the writer's to grow.
- **`ultrafibu einrichten` can now record the company's own address, tax
  number, telephone, e-mail, IBAN, BIC and bank** - the fields § 14 asks for,
  which until now could not be entered at all.

#### 2026-09-19 *0.2.0*
- **Belege und Buchungen: the documents and the journal they produce.** Phase
  A2 of `Docs/Research/UltraFIBUDesignProposal.md`, minus the invoice PDF and
  the screens. `UltraFIBUBeleg.{h,cpp}`, `UltraFIBUBuchung.{h,cpp}`, schema
  version 2 (`beleg`, `beleg_position`, `buchung`, `zahlung`), nine new
  commands in `ultrafibu`, and 126 further checks in
  `Tests/UltraFIBU/UltraFIBUEngineTests.cpp` (626 in total).
- **A document produces postings; it is never derived from them.** A `Beleg`
  has positions, a partner, a currency, a file, a status and a payment history;
  a `Buchung` has two accounts and an amount. While a document is a draft it can
  be edited and re-priced freely. `Buchen()` turns it into journal rows, and
  from that moment the store refuses every change to it - a correction is a
  Storno, which is what the GoBD require and what `SaveBeleg` answers with
  rather than leaving it to the UI.
- **The journal row is DATEV-shaped**: `umsatz` (always positive) with a
  Soll-/Haben-Kennzeichen, a `konto`, a `gegenkonto` and a BU-Schlüssel - the
  shape the Buchungsstapel exports and the Kanzlei reads. In that shape the tax
  is not a third row: one account is net, the other gross, and the difference is
  an automatic posting to a tax account nobody types. The row therefore also
  records what that automatic posting was - `netto`, `steuer`, `steuerkonto`,
  `satzPromille` and which side was the net one - because a Saldenliste has to
  show the tax account, because the rate that applied on the Belegdatum is
  history rather than configuration, and because rounding happened once and
  re-deriving it later can differ by a cent.
- **Tax is computed per rate, not per position.** Three lines of 33,33 EUR at
  19 % owe **19,00 EUR**; rounding each line first gives 18,99. The one tax
  figure is then distributed back over the lines by largest remainder, so the
  invoice's tax column adds up to its tax total - the property the recipient's
  own system checks. The test asserts both numbers, so the difference cannot be
  optimised away by accident.
- **Storno is a reversal, never a deletion or a negative amount.** A new
  document with the amounts negated and its own number, postings with Soll and
  Haben exchanged at the *same* positive amounts, both documents and both
  postings pointing at each other, dated into an open period while the original
  may sit in a frozen one. Reversing the same document twice is refused.
  - **A payment already recorded is not reversed with it.** The money arrived;
    reversing the bank leg would make the bank balance disagree with the bank
    statement, which is the one figure in a bookkeeping system that is checked
    against the outside world. What remains is a credit on the person account -
    the customer paid for an invoice that no longer exists and is owed the
    money - which is the true position and the start of a refund. This was
    wrong in the first version of the code and is now pinned by a test that
    asserts the bank is untouched.
- **A hash chain over the journal, and something that checks it.**
  `hash = SHA-256(prevHash ‖ KanonischeForm(buchung))` over a length-prefixed
  canonical form, so a Buchungstext containing a semicolon cannot imitate a
  field separator. `PruefeHashKette` walks the chain from the first row and
  re-computes every link; the tests edit an amount and then delete a row with
  plain SQL - exactly what somebody with the database file would do - and assert
  that both are detected, the first by the hash and the second by the gap in the
  running numbers. It is tamper *evidence*, not a qualified signature, and the
  CLI says so.
- **Festschreibung now reaches the rows.** Freezing a period sets the date on
  the Geschäftsjahr *and* marks every posting and posted document in it, in one
  transaction. Afterwards the store refuses a document dated into the period, a
  posting dated into it, a reversal dated into it and a payment dated into it -
  each with the date it is refusing and why.
- **Payments and the overdue list.** `ZahlungErfassen` posts the money account
  against the person account and carries the document from Offen through
  Teilweise bezahlt to Bezahlt; an over-payment is refused, because the usual
  cause is the same payment entered twice. The due date is derived once from the
  partner's terms and then stored, so changing a customer's terms next year does
  not move when last year's invoices were due. `BelegFilter` covers the
  Rechnungen screen - open, overdue, by kind, by partner, by date range, by
  search text - and takes the cut-off date as a parameter rather than reading
  the clock, so a report is reproducible and a test is not flaky.
- **`SummenUndSalden` expands the automatic tax posting** into the leg it always
  was, which is what makes the list balance, and names a Personenkonto after its
  partner since it is not in the chart of accounts.
  `Buchungskreisdifferenz` is the one-line answer to whether the ledger is still
  a ledger; every test in this area ends with it, and so does `ultrafibu salden`.
- **Nine commands**: `beleg-neu`, `belege`, `buchen`, `storno`, `zahlung`,
  `journal`, `salden`, `pruefen`, beside the existing ones. A whole month can be
  entered, posted, paid, partly reversed, frozen and verified without a window -
  which is what keeps the engine honest about being headless.

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
  (framework 0.8.92), and a Steuerschlüssel computes its tax through it.
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
