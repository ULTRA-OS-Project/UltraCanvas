# UltraFIBU

**Buchhaltung für Deutschland** — German double-entry accounting on UltraCanvas:
DATEV import and export, UStVA and ZM to ELSTER, One-Stop-Shop, a
*Geschäftsjahr* whose start date is free, customers and suppliers with their
European VAT numbers, and a German user interface.

- Design and rationale: [`Docs/Research/UltraFIBUDesignProposal.md`](../../Docs/Research/UltraFIBUDesignProposal.md)
- Version and history: [`Docs/UltraFIBU/CHANGELOG.md`](../../Docs/UltraFIBU/CHANGELOG.md)

> **Status: phase A1.** The headless engine and the `ultrafibu` command line are
> built and tested. The German UI waits for `UltraCanvasDataGrid` (proposal
> §3.3); DATEV, ELSTER, OSS, bank import and the journal are the phases after
> that. Nothing here files a tax return yet.

## Layout

| Path | What it is |
|---|---|
| `engine/` | The headless engine — no UI, no SQL outside the store, unit-tested (`UltraFIBUEngine`) |
| `cli/` | `ultrafibu`, the command line over that engine |
| `report/` | The printed invoice (`UltraFIBUReport`) — builds a VectorDocument for the framework's PDF writer |
| `data/` | The chart of accounts, the tax keys and the DATEV column definitions, as data files |
| `ui/` | The German screens (`ultrafibu-ui`) — Belege, Journal, Summen und Salden, Partner |
| `../../Tests/UltraFIBU/` | The engine test suite |

## Build

```bash
cmake -S . -B build -DULTRACANVAS_BUILD_ULTRAFIBU_TESTS=ON
cmake --build build --target ultrafibu UltraFIBUEngineTests
ctest --test-dir build -R UltraFIBUEngine
```

The engine needs **UltraDatabase** (SQLite) and, for passwords, **UltraCrypt**
(libsodium). Without libsodium the store refuses to set a password rather than
storing one badly. The test suite compiles the engine sources directly, so it
runs on a machine that cannot build the UI library.

## Using it

```bash
# A company whose Geschäftsjahr starts on 1 April
ultrafibu einrichten buch.db --firma "Beispiel GmbH" --gj-beginn 01.04.2026 \
          --ort Olpe --ust-idnr DE136695976

ultrafibu info buch.db          # Mandant, Geschäftsjahre, Bestände
ultrafibu perioden buch.db      # April is period 1, January is period 10
ultrafibu konten buch.db        # the imported SKR03 accounts

ultrafibu partner-neu buch.db --name "olonda s.r.o." --land SK \
          --ust-idnr SK2022513009 --ort Bratislava
ultrafibu partner buch.db bratislava

ultrafibu ustid DE136695976     # offline: format and check digit
ultrafibu termine buch.db 2026  # UStVA deadlines, with the weekend shift
ultrafibu protokoll buch.db     # who did what, when (GoBD)
```

### A document, posted, paid, reversed

```bash
# A draft. Positions are "Text;Menge;Einzelpreis;Konto;Steuerschlüssel".
ultrafibu beleg-neu buch.db --datum 15.06.2026 --partner "olonda s.r.o." \
          --position "Beratung;10;100,00;8400;USt19" \
          --position "Fachbuch;2;20,00;8300;USt7"
#   Summe 1.040,00 netto / USt 192,80 / Gesamt 1.232,80 brutto

ultrafibu buchen buch.db R-202606001     # from here it is immutable
#   10000  S  1.190,00 an 8400  (davon 190,00 USt auf 1776)
#   10000  S     42,80 an 8300  (davon   2,80 USt auf 1771)

ultrafibu zahlung buch.db R-202606001 --betrag 1.232,80 --datum 01.07.2026
ultrafibu belege  buch.db --offen
ultrafibu belege  buch.db --ueberfaellig --stichtag 31.07.2026
ultrafibu journal buch.db --von 01.06.2026 --bis 30.06.2026
ultrafibu salden  buch.db                # and whether Soll and Haben agree

ultrafibu storno  buch.db R-202606001 --datum 25.07.2026 \
          --grund "Falsche Menge" --ja

ultrafibu rechnung-pdf buch.db R-202606001 --datei rechnung.pdf

# DATEV: one Buchungsstapel per calendar month
ultrafibu datev-export buch.db --monat 2026-06 --ziel ./datev
ultrafibu datev-export buch.db --konten --ziel ./datev
ultrafibu datev-pruefen ./datev/EXTF_Buchungsstapel_202606.csv

# DATEV import: looking is the default, writing is the second step
ultrafibu datev-import  buch.db ./von-der-kanzlei/EXTF_Buchungsstapel_202606.csv
ultrafibu datev-import  buch.db ./von-der-kanzlei/EXTF_Buchungsstapel_202606.csv --uebernehmen
ultrafibu datev-importe buch.db

ultrafibu festschreiben buch.db 30.06.2026 --ja
ultrafibu pruefen buch.db                # the journal's hash chain
```

`rechnung-pdf` exits non-zero and names every field § 14 UStG wants that is
not filled in — both addresses, the Steuernummer or USt-IdNr., the date of
supply, the rate per line, the customer's VAT number on an intra-community
supply. An invoice missing them is legally deficient and its recipient cannot
deduct the input tax, so it is a warning on the way out rather than a footnote.

Once a period is frozen, a document dated into it, a posting into it, a
reversal into it and a payment into it are each refused with the date and the
reason — the check is in the store, not in the front end.

`ULTRAFIBU_DATA_DIR` points at the data files when they are not beside the
binary.

### The screens

```bash
ultrafibu-ui buch.db
```

Four tabs over the same file the CLI writes: **Belege**, **Journal**, **Summen
und Salden** and **Partner**. Each is a table you can sort by clicking a column
and narrow by typing; amounts sort by their value and dates by their day, not
by how they read as text. The summary line under each table describes the rows
actually showing, so filtering to one customer gives that customer's open
total and says it is filtered.

The journal re-checks the hash chain and the double entry every time it loads
and reports both in its summary line.

Two buttons write: **Beleg buchen** posts the selected draft, and **Rechnung
als PDF** prints it. Everything else is read-only — entering a document is
`ultrafibu beleg-neu` until the position editor exists. Both buttons go through
the store, so a frozen period or an already-posted document is refused there
and the reason appears in the status line.

## The decisions worth knowing

- **Amounts are never floating point.** Everything is `UltraCanvas::Money` —
  integer cents with *kaufmännische Rundung* — because a VAT split has to be
  reproducible to the cent.
- **Tax knowledge is data.** Rates, UStVA *Kennzahlen*, DATEV *BU-Schlüssel* and
  validity dates live in `data/Steuerschluessel.csv`, versioned by date. The
  UStVA form changes every year; that is a data update, not a code change. Only
  *verified* Kennzahlen are shipped — a test fails if a guessed one appears.
- **Two calendars.** Reports follow the *Geschäftsjahr*; UStVA, ZM and OSS
  follow the calendar. Neither is derived from the other.
- **Immutability is a schema property.** *Festschreibung* only moves forward,
  corrections are a *Storno*, and every write and every refusal is in the audit
  trail with a real user against it — which is why users exist from the first
  schema and not from the day the server arrives. Over the journal sits a
  SHA-256 hash chain, and `ultrafibu pruefen` is the thing that checks it: a
  chain nothing verifies is decoration. It is tamper *evidence*, which is what
  the GoBD ask of a bookkeeping system, and not a qualified signature.
- **A document produces postings, never the other way round.** A draft can be
  edited freely; from `buchen` onwards the store refuses every change to it. The
  journal row keeps DATEV's shape — positive `Umsatz` with a Soll-/Haben flag,
  `Konto`, `Gegenkonto`, BU-Schlüssel — and records beside it what the automatic
  tax posting was, because a Saldenliste has to show a tax account nobody typed
  and because the rate that applied on the *Belegdatum* is history rather than
  configuration.
- **Tax is computed per rate, not per position.** Three lines of 33,33 € at 19 %
  owe 19,00 €, not the 18,99 € that rounding each line first would give; the
  single figure is then shared back over the lines so the invoice's tax column
  still adds up to its total.
- **The DATEV column order is a data file, and it is not yet verified.**
  `data/DATEV-Buchungsstapel-v700.csv` is the best reconstruction available
  without an original, and it says so in its own header. The export writes each
  value **by column name**, so correcting the file moves the values with it.
  `ultrafibu datev-pruefen <real file>` compares the definition against a real
  DATEV export and names the position of every difference — one command, not a
  rewrite.
- **The importer does not depend on that guessed order at all.** A real DATEV
  file names its own columns, and `LeseBuchungsstapel` reads the values by
  those names. The uncertainty that hangs over the export does not reach the
  import.
- **An import is all of the stack or none of it.** The fiscal year, the
  year-end and the Festschreibung are checked for every row before any row is
  written, because half an imported stack is worse than none and leaves no
  record of where it stopped. The same file cannot be imported twice by
  accident either — it is recognised by its SHA-256, and a doubled month shows
  up only as a balance that is wrong by exactly one stack.
- **A BU-Schlüssel we cannot map produces no tax split**, rather than an
  invented one in a tax account. The amount comes in whole, the DATEV key is
  kept verbatim, and the warning names the keys concerned so the `datev_bu`
  column in `data/Steuerschluessel.csv` can be filled in and the file read
  again. When *no* posting got a split, the import says so outright — otherwise
  a gross amount on a revenue account is a mystery that takes a long time to
  solve.
- **A Buchungsstapel is always one calendar month.** The Belegdatum field is
  `TTMM` with no year; DATEV infers it from the Wirtschaftsjahr. A month not
  wholly inside the fiscal year is refused rather than mis-booked, which for a
  1 April Geschäftsjahr is the difference that matters.
- **The invoice PDF reuses the framework's PDF writer**, it does not contain
  one. `PDFVectorConverter` writes the file; `report/` only builds the
  `VectorDocument` it consumes. The two sources that writer needs were checked
  and reference nothing outside `VectorStorage`, so an invoice can be produced
  on a headless server — which is where an accounting system runs.
- **§ 14 UStG decides what is on the page**, and what is missing is named
  rather than quietly left off. A zero-rated line states its exemption, because
  the statute requires the invoice to say why no tax was charged.
- **ZUGFeRD is not this.** It is a PDF/A-3 carrying the CII XML as an embedded
  file, and it needs embedded subset fonts, an output intent and XMP metadata
  the writer does not do yet. XRechnung — pure XML, no PDF — is the route to a
  real e-invoice meanwhile, and that is phase A7.
- **Reversing an invoice does not reverse its payment.** The money arrived, and
  a bank balance that disagrees with the bank statement is worse than an
  unmatched credit. What is left is a credit on the customer — which is the
  truth, and the start of a refund.
- **One schema, two storage modes.** Local SQLite or a shared PostgreSQL
  database, chosen by one configuration field. The server driver is
  UltraDatabase Stage 2 and not built yet; `OpenServer` says so rather than
  falling back. A shared SQLite file on a network share or a synced folder is
  never an option — that is silent loss of a book the law requires to be
  complete.
- **The VAT number check is honest about itself.** Offline it catches typing
  errors; VIES confirms registration EU-wide with no registration needed; only
  the BZSt's *qualified* confirmation protects a zero-rated intra-community
  supply, and the authority's response is stored verbatim because that data set
  is the evidence.
