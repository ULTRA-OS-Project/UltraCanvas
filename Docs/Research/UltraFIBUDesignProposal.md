# UltraFIBU — a German double-entry accounting application (Investigation and Proposal)

**Date:** 2026-09-17
**Status:** Proposal — investigation only, no implementation yet
**Scope:** a new application, `Apps/UltraFIBU`, a German *Buchhaltungsprogramm*
(Finanzbuchhaltung) with DATEV import/export, UStVA/ELSTER submission via ERiC,
One-Stop-Shop (OSS) reporting, a flexible *Geschäftsjahr* (e.g. 1 April –
31 March), customer/supplier master data with European VAT numbers, a German
user interface, and **two deployment modes — a local single-user database and a
shared server database several users work on** (§10).

This document is written in the style of the other investigations in this
directory — [`ArtCreatorVectorCanvasProposal.md`](ArtCreatorVectorCanvasProposal.md)
is the closest sibling in shape, because it too asks *which parts of a new
application belong in the framework*. Everything said below about the
repository was verified against the tree on this date; file references are to
the current `main`. Everything said about German tax and DATEV formats is
sourced, and the places where a source could not be reached from this
environment are marked **[unverified]** rather than guessed.

Domain vocabulary stays German throughout (*Geschäftsjahr*, *Buchungsstapel*,
*Festschreibung*), because that is what the data model, the DATEV files and the
user interface all call these things. Inventing English names for them would
only create a translation layer nobody needs.

---

## 1. Recommendation

**Build it as an application — `Apps/UltraFIBU` — with a headless engine
library and a German UI on top, exactly the way `EmailCleaner` and
`UltraSocial` are built. Add four things to the framework, because four of the
pieces this application needs are missing there and every future data-heavy app
will want them:**

| Add to framework | Why it cannot live in the app |
|---|---|
| **Sorting and filtering for `UltraCanvasListView`** — which already is the multi-column, virtualised, model-driven table (§3.3) | Every screen in this app is a table of thousands of rows, and the one thing the existing view could not do was order or narrow them. A *second* grid would have been the wrong answer |
| `UltraCanvasXML` — an owned XML reader/writer over the **tinyxml2 the framework already links** | XRechnung, ZUGFeRD, ELSTER, CAMT.053 and the GoBD `index.xml` are all XML. tinyxml2 is a core dependency already, but nothing owns it: every caller parses its own way and two of them parse the same file twice (§3.4). No new dependency — a facade |
| `UltraCanvasMoney` — exact decimal money (integer minor units) with parse/format | An accounting program may not represent 37,28 € as a `double`. The one currency type in the tree (`CurrencyValue`, `UltraCanvasSpreadsheetTypes.h:267`) is a `double` and must not be used for a ledger |
| `UltraCanvasNumberInput` / currency field, and a minimal string catalogue | The UI is German: comma decimals, `1.234,56 €`, and ~800 labels. Hard-coding literals throughout is what every other app did, and this app is large enough that it hurts |
| **The `libpq` (PostgreSQL) driver for UltraDatabase** — its own documented Stage 2 plan, not yet built | The multi-user requirement has no other answer. A shared SQLite file on a network share or a synced folder is data loss, not a deployment mode (§10.1). The driver is framework work every app inherits |

**Do not build a tax engine that talks to the tax office by itself.** UStVA
submission to ELSTER legally goes through **ERiC**, a closed native library that
must be downloaded under agreement, cannot be vendored into this repository, and
is re-released twice a year. Treat it exactly as the PDF and OCR backends are
treated: an interface in the engine, a dynamically loaded backend, soft-fail
when absent — plus a *second*, always-available path that writes the ELSTER XML
to disk for manual upload in *Mein ELSTER*. That second path is what makes the
application useful on day one and on any machine without the SDK.

**Do not chase OSS automation.** The BZSt publishes no machine interface for
One-Stop-Shop returns; the only supported bulk path is a CSV transport file
uploaded by hand in *Mein BOP* (§5.3). Generate that file, compute the figures,
and stop there — automation that does not exist cannot be built.

**Write the application for both storage modes from the first commit, and
implement server mode when the driver lands.** Local and shared are the *same*
schema behind the *same* store API and one named UltraDatabase connection whose
`driver` is `sqlite` or `postgresql`. Keeping that true costs four disciplines
(no SQLite-specific SQL, bound parameters everywhere, explicit transactions, no
"I am the only writer" assumption) and nothing else — but skipping them makes
multi-user a rewrite, because the things that break under two writers are
invoice numbering, document editing and migrations, and all three are decided by
the schema (§10).

Four findings drive the overall shape:

1. **The persistence, import and export substrate is already here.** UltraDatabase
   (SQLite, migrations, bound parameters) is built and in production use by two
   apps; the CSV layer already speaks **CP1252**, semicolons and quoted fields,
   which is precisely the DATEV dialect; `UCZipPackageWriter` gives the ZIP
   containers that DATEV XML, ZUGFeRD and the GoBD Z3 export all need; the
   Spreadsheet element reads and writes ODS/XLSX/CSV for report export. None of
   that has to be invented.
2. **The document side is half-built.** PDF *reading* is solid (MuPDF), and a
   hand-written PDF *writer* exists (`PDFVectorConverter`) — but it embeds no
   fonts and knows nothing of PDF/A-3 or embedded attachments, which is exactly
   what ZUGFeRD requires. So outgoing invoices as plain **XRechnung XML** are
   easy and outgoing **ZUGFeRD** is a real project (§7.2). OCR, which receipt
   capture wants, is still only a proposal (`Docs/Modules/OCR/README.md`).
3. **Two of the requirements fight each other and the data model must know it.**
   A *Geschäftsjahr* starting 1 April and an *Umsatzsteuer-Voranmeldung* that is
   always a calendar month or calendar quarter are two independent calendars over
   the same journal (§6.3). Every accounting package that got this wrong
   discovered it at the first year-end. It is a schema decision, not a report
   decision, and it is cheap if made now.
4. **Multi-user is a database question with three sharp edges, not a UI
   question.** UltraDatabase was built for exactly this swap, and only its
   SQLite driver exists today (verified — `core/UltraDatabase/` has one driver
   and there is no `Plugins/UltraDatabase/`). So the work splits cleanly: a
   reusable framework driver, plus three application concerns that must be right
   regardless of mode — gap-free invoice numbering allocated inside the posting
   transaction, optimistic locking on every editable document, and a startup
   migration gate so an old client cannot write into a newer schema (§10.3).

### 1.1 Decisions already taken (2026-09-17)

Three questions that would otherwise steer the plan were answered by the
owner while this document was being written, and the plan below reflects them:

| Question | Decision | What it changes |
|---|---|---|
| Does the *Kanzlei* keep doing the *Jahresabschluss*? | **No — UltraFIBU should eventually produce the annual accounts itself** | *Bilanz*/GuV and **E-Bilanz** move from "out of scope" to a named later track (A10, §13). Two things change *early*: each account carries a **balance-sheet classification** from the first chart import, and closing entries / *Saldenvortrag* are journal concepts rather than report code (§2) |
| When is shared-server mode needed? | **Day one, as a parallel track** | The `libpq` driver (B4) starts beside A1 instead of waiting for A8, and the **user table, roles and attribution ship with the very first schema** so no history is written without a real user (§10.2, §13) |
| Which bookkeeping basis is the default? | **SKR03 and *Soll-Versteuerung*** | SKR03 is the chart shipped and tested first (SKR04 beside it as data); VAT falls due at invoice date, so the EÜR/UStVA projection defaults to *Belegdatum* while still storing the payment date (§4.3, §6.4) |

Neither decision removes a switch: the chart, the *Besteuerungsart* and the
storage mode remain per-*Mandant* settings, because the code that hard-codes
one of them is the code that cannot be given to a second company.

---

## 2. What the application is

The screenshots supplied with the request are from a commercial German
bookkeeping service (sevDesk) and show the target feature set precisely. Read as
a specification, they ask for six screens:

| Screen (German) | What it does | Evidence in the screenshots |
|---|---|---|
| **Kontoumsätze** | Bank transactions per account, with a detail pane, a payment-to-document assignment (*Zugewiesene Zahlungen*), and *Automatische Zuordnung* | "Commerz DE82…", the ± marker per row, the assignment card |
| **Belege** (Ausgabe-/Eingangsbelege) | Supplier receipts: scanned image beside extracted fields, category, tax rate, country classification (*Drittland*), immutability once *festgeschrieben* | "Ausgabe-Beleg bearbeiten", "Festgeschrieben: … nur über eine Stornierung möglich" |
| **Rechnungen** | Outgoing invoices, overdue tracking, dunning, per-customer totals | "15 überfällige Rechnungen", *Fällig am … seit 56 Tagen* |
| **Neue Rechnung** | Invoice editor: customer picker or inline new customer, project, template, payment terms, net/gross switch, line items with tax rate, discount | the full form, `{{rechnung.zahlungsfrist}}` placeholder |
| **Berichte** | Cashflow, **BWA**, **EÜR**, **UStVA** | the Berichte dropdown, the income/expense chart |
| Master data | Customers and suppliers with EU VAT number, address, contact | "Lieferant — Richard Evans Acompanado", "Drittland" |

Two things visible in those screenshots are worth calling out because they are
*legal* behaviour, not UI behaviour, and they shape the schema:

- **Festschreibung.** Once a document is *festgeschrieben* its content can no
  longer change; corrections happen by *Storno* (reversal) and a new document.
  That is GoBD §§ *Unveränderbarkeit* — see §8.
- **Drittland handling with a note about input tax.** The receipt screen warns
  that foreign VAT from a third country is not deductible and the gross amount
  should be booked at 0 % German VAT. The *Steuerschlüssel* table (§6.5) has to
  carry that knowledge as data, per country and per validity period.

Explicitly **out of scope** for this proposal: payroll (*Lohnbuchhaltung* — a
separate regulated world with its own ELSTER data types and monthly
*Beitragsnachweise*), cash registers (*DSFinV-K*/TSE), and inventory. Each is a
project of the same size as this one.

**In scope, but as its own later track:** the *Jahresabschluss* — balance sheet
and P&L (*Bilanz*/GuV) and **E-Bilanz**, the XBRL taxonomy transmission that
ERiC's `checkBilanz_<taxonomy>` plugins validate. The decision (§1.1) is that
UltraFIBU should eventually produce the annual accounts itself rather than hand
them to the *Kanzlei*, which changes two things early even though the work is
late: the chart of accounts must carry **balance-sheet classification** per
account from the start (not only EÜR lines and BWA positions), and the closing
machinery — *Abschlussbuchungen*, *Saldenvortrag* into the next
*Geschäftsjahr*, *Rechnungsabgrenzung*, provisions — has to be a first-class
concept in the journal rather than a report. Both are cheap now and expensive
to retrofit; the XBRL taxonomy work itself is phase A10 (§13). The
*Anlagenverzeichnis* (fixed assets with depreciation) moves in with it, since a
balance sheet needs it and a complete EÜR does too.

---

## 3. What the repository already provides — and what it does not

Verified on 2026-09-17 against the tree.

### 3.1 Usable as-is

| Need | What exists | Where |
|---|---|---|
| Local persistence, migrations, transactions, bound parameters | **UltraDatabase** Stage 1 (SQLite bundled, `UltraDb_RegisterConnection` / `_Exec` / `_Query` / `_Begin` / `_Migrate`), 14 tests | `UltraCanvas/{include,core}/UltraDatabase/`, `Docs/Modules/UltraDatabase/README.md` |
| A worked example of an app-owned store on top of it | `EmailCleaner::AnalysisStore` — schema version constant, migration list, no SQL in the UI | `Apps/EmailCleaner/engine/EmailCleanerStore.{h,cpp}` |
| CSV **in the DATEV dialect** | `CSVImportOptions`/`CSVExportOptions` with a **CP1252 ⇄ UTF-8 codec**, delimiter/quote detection, quoted multi-line fields, number parsing | `UltraCanvas/include/UltraCanvasCSVImport.h` (`CSVDecodeToUtf8`, `CSVEncodeFromUtf8`, `CSVParse`, `CSVDetectOptions`) |
| Import/export dialogs | `UltraCanvasCSVImportDialog`, `UltraCanvasCSVExportDialog` | matching headers |
| ZIP containers (DATEV XML package, ZUGFeRD, GoBD Z3 medium) | `UCZipPackageReader` / `UCZipPackageWriter` (miniz) | `UltraCanvasZipPackage.h` |
| Report export to spreadsheets | `UltraCanvasSpreadsheet::Save{ODS,XLSX,CSV}` + `SaveCSVWithOptions` | `UltraCanvasSpreadsheet.h:401-417` |
| Receipt preview (PDF and image) | MuPDF-backed `IPDFDocument` + `UltraCanvasPDFView`; `UltraCanvasImageElement` | `include/Plugins/Documents/UltraCanvasPDF{,View}.h` |
| Charts for Cashflow/BWA | Line, area, bar, waterfall, financial charts | `Docs/UltraCanvas/UltraCanvas*Chart*.md` |
| Forms whose captions line up and survive translation | `CreateFormGrid` / `AddFormRow` | `UltraCanvasFormLayout.h`, [doc](../UltraCanvas/UltraCanvasFormLayout.md) |
| Date entry and ranges | `UltraCanvasDatePicker`, `UltraCanvasDateRangePicker`, `UltraCanvasCalendarView` | `UltraCanvasDatePicker.h` |
| Status pills, tabs, toolbars, modals, tooltips | `UltraCanvasBadge`, `UltraCanvasChip`, `UltraCanvasTabbedContainer`, `UltraCanvasToolbar`, `UltraCanvasModalDialog` | catalogue |
| HTTPS with client certificates, OAuth2 + PKCE | `UltraNetHttpTlsOptions::clientCertPem`, `UltraNet_OAuth2*` | `include/UltraNet/UltraNetHttp.h:60`, `UltraNetOAuth2.h` |
| Secret storage for the ELSTER PIN, bank and API credentials | **UltraVault** | `include/UltraVault/UltraVault.h` |
| Hashing for the journal hash chain and DATEV document hashes | `UltraCrypt_Hash`, `UltraCrypt_HashFile`, `UltraCrypt_Hmac`, `UltraCrypt_ToHex` | `include/UltraCrypt/UltraCryptCore.h` |
| Printing | IODeviceManager printer subsystem incl. CUPS and a print dialog | `include/IODeviceManager/UltraCanvasIODevicePrinter*.h` |
| Payment QR on invoices (GiroCode/EPC) | QR code element | `Docs/UltraCanvas/UltraCanvasQRCodeExamples.md` |

### 3.2 A PDF writer exists, but not the one ZUGFeRD needs

`UltraCanvas/Plugins/Vector/UltraCanvasPDFVectorConverter.cpp` writes a
self-contained **PDF 1.4** from a `VectorStorage::VectorDocument`: catalog, page
tree, an uncompressed content stream, base-14 Type 1 fonts, `ExtGState` for
opacity. It is export-only and its own header records the limits — no embedded
font programs (so centre/right text anchoring is *approximated from an average
glyph width*), and gradients collapse to a blend of their end stops.

**Built (0.3.0).** That writer now produces the invoice: `Apps/UltraFIBU/report/`
builds a `VectorDocument` and hands it over, rather than adding a second
emitter. Two things had to be established first and both held — the writer and
`VectorStorage` reference nothing outside `VectorStorage`, so the invoice stays
headless, and one defect had to be fixed on the way: the escaper declared
`/WinAnsiEncoding` but replaced every code point above U+00FF with `?`, so the
**euro sign** — WinAnsi 0x80, outside Latin-1 — printed as a question mark
(framework 0.9.3). Right alignment is computed from the base-14 advance widths
rather than left to the writer's average-glyph approximation, since a money
column cannot be approximate. The writer still emits **one page**, so an invoice
with more positions than fit is refused rather than truncated; multi-page output
is the next thing that writer needs.

For an invoice PDF that is enough to start: base-14 Helvetica is legal on an
invoice. It is **not** enough for **ZUGFeRD**, which is a PDF/A-3 file with the
CII XML as an embedded file — PDF/A-3 requires embedded, subset fonts, an output
intent, XMP metadata including the ZUGFeRD identification, and an
`/AF`-associated embedded file stream. None of that exists today. Hence the
phasing in §7.2: XRechnung (pure XML) first, ZUGFeRD when the PDF writer grows
up. `IPDFDocument` cannot substitute — it opens and edits existing documents
(`InsertBlankPage`, `ReplaceText`, annotations) and has no content-drawing API.

### 3.3 The table already exists; sorting and filtering did not

**Correction to an earlier draft of this document.** This section previously
claimed that the framework had no data grid and that `UltraCanvasListView` "has
no column API at all". That was wrong, and it was wrong in the most avoidable
way: the conclusion came from grepping the view's header for `AddColumn` /
`SetColumns` instead of reading the model beside it.

What is actually in the tree:

- **`UltraCanvasListView` is a multi-column, virtualised, model-driven table.**
  It has a model interface (`IListModel`: `GetRowCount`, `GetColumnCount`,
  `GetData`/`SetData` by role), column definitions (`ListColumnDef` — title,
  width, alignment, header tooltip), a ready-made `UltraCanvasMultiColumnListModel`,
  painting delegates (`IItemDelegate`, so a cell can be a badge, a bar, a
  checkbox or anything else), selection models (single and multi), a header
  band, per-cell tooltips, variable row heights, cell-level click and hover
  callbacks, keyboard navigation, a scrollbar, and row culling that paints only
  what is visible.
- `UltraCanvasTableView`, which the element catalogue named, genuinely does not
  exist — the catalogue row was simply wrong, and it now points at the view that
  does the job.

So the gap was never "a grid". Grepping for the *name* of a feature found
nothing for **sort**, **filter**, **edit** or **footer** across every list
header, and that is exactly what was missing:

| Missing | Status |
|---|---|
| Sorting (header click, indicator, comparators) | **Built** — `UltraCanvasListSortFilterProxy` + `UltraCanvasListView::onHeaderClicked` / `SetSortIndicator` |
| Filtering (text, per-column, arbitrary predicate) | **Built** — same proxy |
| Sort-by-value-not-by-text | **Built** — `ListDataRole::SortRole` |
| Inline cell editing | Still missing in the view; `IListModel::SetData` is there, so this is an editor widget over an existing seam |
| Footer / aggregate row (the `652,57 €` total in the screenshot) | Still missing |

The proxy is the Qt-proven shape: it *is* an `IListModel` wrapping another one,
so the view needs no knowledge of either sorting or filtering, and every
existing `ListView` caller in the repository gains both without changing a line.
The one thing callers must respect is that a proxy row is not a source row —
`MapToSource()` exists for that, and a sorted table that deletes the wrong
record is what forgetting it looks like. See
[`UltraCanvasListSortFilterProxy.md`](../UltraCanvas/UltraCanvasListSortFilterProxy.md).

**The lesson worth keeping**, because it cost a wrong recommendation in a
document whose whole job was to survey the tree: *grep for the concept, not for
one spelling of it, and read the neighbouring header before concluding that
something is absent.* A second grid built on that mistake would have been
hundreds of lines duplicating a view that already worked.

### 3.4 There is an XML parser, and nobody owns it

Six of this application's formats are XML: XRechnung (UBL *and* CII),
ZUGFeRD/Factur-X (CII), the ELSTER data types, CAMT.053 bank statements, the
DATEV XML interface (`document.xml`), and the GoBD `index.xml`.

**tinyxml2 is already a core dependency** — `Docs/UltraCanvas/README.md` lists
it among the framework's utility libraries, the CMake configure requires it, and
the COLLADA reader, the mind-map IO and `UltraCanvasPropertyList`'s XML form all
go through it. What does not exist is an **owned facade** over it. The
consequences are already recorded in this repository:
`UltraCanvasPropertyList` parses XML one way, the diagram readers another, the
SVG plugin has its own parser entirely, and
[`VersioningInvestigation.md`](../UltraCanvas/VersioningInvestigation.md) notes a
file being parsed "a second time, with tinyxml2 again" into a different model —
*"two parsers, two"* models of the same document.

**Recommendation:** add `UltraCanvasXML` next to `UltraCanvasJSON`, wrapping the
tinyxml2 that is already linked. That is the framework's standing rule for
engines (*"public engines are always wrapped … never expose a third-party type
in a public header"*), and `UltraCanvasJSON` over yyjson is the pattern to copy.
**No new third-party dependency is involved**, so `Docs/Dependencies.md`,
`master_dependencies.yaml` and `THIRD_PARTY_LICENSES.md` need nothing new —
which also makes this the cheapest of the four framework additions. XSD
*validation* should not be attempted with it: ELSTER validation is ERiC's job
and XRechnung validation belongs to the KoSIT validator, which is a separate
tool the user runs.

### 3.5 There is no money type, and no translation layer

- The only currency type in the tree is `CurrencyValue { double amount; std::string code; }`
  (`UltraCanvasSpreadsheetTypes.h:267`). Fine for a spreadsheet cell, unusable
  for a ledger: `0.1 + 0.2` and a 19 % VAT split must both be exact and
  reproducible. **Recommendation:** `UltraCanvasMoney` — `int64_t` minor units
  plus ISO 4217 code, explicit rounding (*kaufmännische Rundung*, half-up),
  allocation helpers (`SplitProportionally`, so a discount distributed over
  line items still sums to the total), and parse/format for both the German UI
  (`1.234,56 €`) and machine formats.
- There is **no i18n mechanism** at all — no `Tr()`, no catalogue, no locale
  service; every app hard-codes its English literals. This app is German-only
  by requirement, so the cheap answer is right: one keyed German string table in
  the app (`UBText::Get("rechnung.ueberfaellig")`), loaded from a data file, no
  gettext, no runtime language switch. It costs nothing now and is the only
  thing that makes a later English or Austrian/Swiss variant possible. The
  `UltraCanvasFormLayout` doc was already written with translation in mind.
- **A locale trap the framework has been bitten by twice.** AGENTS.md records
  that numbers in file formats are dot-decimal and must be parsed with
  `TryParseFloat` and written through a `std::locale::classic()` stream, because
  the Linux backend calls `setlocale(LC_ALL, "")`. DATEV CSV inverts this: its
  amounts are **comma**-decimal by specification. So the DATEV writer must
  format through the classic locale and then substitute the comma *deliberately*,
  never by leaning on `LC_NUMERIC` — and the DATEV reader must never use
  `std::stod`. Same defect, opposite direction.

### 3.6 Not implemented yet, and needed

| Missing | State | Consequence for this app |
|---|---|---|
| OCR | Proposal only (`Docs/Modules/OCR/README.md`) | Receipt capture starts as "attach the file and type the fields"; automatic extraction waits, or goes through an UltraAI `IVisionAnalyzer` adapter with the user's consent (their receipts would leave the machine — must be opt-in and off by default) |
| PDF/A-3 + font embedding | Not present (§3.2) | ZUGFeRD *output* is phase 5, not phase 1 |
| Networked UltraDatabase drivers | Stage 2 plan | Irrelevant: SQLite is the right store for a single-company ledger. Multi-user comes later, and the store API must not assume otherwise |

---

## 4. DATEV — the two interfaces that matter

DATEV is not one format. For this application exactly two of its interfaces
count, and they are very different in character.

### 4.1 DATEV-Format (the CSV family, a.k.a. EXTF)

A semicolon-separated, CP1252-encoded, quoted CSV family. Every file has the
same two-line preamble followed by the column-name line and then the data:

```
"EXTF";700;21;"Buchungsstapel";13;20180306102500000;;"XY";"Chief Accounting Officer";;1001;456;20180101;4;20180201;20180228;"Beispiel-Buchungen";;1;;0;"EUR";;;;;;;;;
```

([example file](https://github.com/ledermann/datev/blob/master/examples/EXTF_Buchungsstapel.csv))

The header fields this application must get right:

| Pos | Field | Value for us |
|---|---|---|
| 1 | Kennzeichen | `EXTF` (data from an external program; `DTVF` is DATEV's own) |
| 2 | Versionsnummer | **700** — the current format version; 510 and 600 are the predecessors |
| 3 | Format-Kategorie | `21` Buchungsstapel, `16` Debitoren/Kreditoren, `20` Sachkontenbeschriftungen |
| 5 | Formatversion | version of that category (13 in the example above) |
| 6 | Erzeugt am | `YYYYMMDDHHMMSSFFF` |
| 10, 11 | **Beraternummer, Mandantennummer** | from the *Kanzlei* — the tax adviser assigns these; without them an import is refused |
| 12 | **WJ-Beginn** | `YYYYMMDD` — *the flexible fiscal year lands here*, e.g. `20260401` |
| 13 | **Sachkontenlänge** | 4 … 8; decides how wide G/L accounts are and therefore where the *Personenkonten* start |
| 14, 15 | Datum von / bis | `YYYYMMDD`, must lie inside the *Wirtschaftsjahr* |
| 16 | Bezeichnung | the stack's name as it appears in DATEV |
| 22 | Währungskennzeichen | `EUR` |

The data columns (~120 of them) begin `Umsatz (ohne Soll/Haben-Kz)`,
`Soll/Haben-Kennzeichen`, `WKZ Umsatz`, `Kurs`, `Basis-Umsatz`, `Konto`,
`Gegenkonto (ohne BU-Schlüssel)`, `BU-Schlüssel`, `Belegdatum`, `Belegfeld 1`,
`Belegfeld 2`, `Skonto`, `Buchungstext`, … through *Kostenstellen*, EU VAT
fields, and *Festschreibung*.

**Three traps that have to be designed for, not discovered:**

1. **`Belegdatum` is `TTMM` — four digits, no year.** The year is inferred from
   the stack's *Wirtschaftsjahr* and period. All postings in one file must belong
   to the same *Wirtschaftsjahr*, and exports spanning two calendar years are
   split into one file per year.
   ([DATEV community](https://www.datev-community.de/t5/Betriebliches-Rechnungswesen/csv-Buchungsstapel-Import-Belegdatum-nur-4stellig-ttmm-gt/td-p/468371?nobounce),
   [format guide](https://dokuwandel.de/ratgeber/datev-buchungsstapel-erklaerung))
   For a 1 April – 31 March *Geschäftsjahr* this is the single most important
   consequence in this document: **export one Buchungsstapel per calendar
   month.** A monthly stack can never straddle a year boundary, is what an
   accountant expects to receive anyway, and makes the year inference
   unambiguous. Exporting "the fiscal year" as one file is the option that
   silently mis-books December into January.
2. **Amounts are comma-decimal** and `Umsatz` is always **unsigned** — the sign
   lives in `Soll/Haben-Kennzeichen` (`S`/`H`). Feeding a signed amount produces
   a plausible-looking, wrong ledger. See the locale note in §3.5.
3. **`BU-Schlüssel`** (*Buchungsschlüssel*) encodes the tax treatment and may be
   prefixed onto `Gegenkonto`. It is DATEV's tax vocabulary, not ours; the
   mapping table in §6.5 owns the translation in both directions.

Categories to support, in order of value: **21 Buchungsstapel** (both
directions — this is the core), **16 Debitoren/Kreditoren** (master data with
`EU-Land` and `EU-USt-IdNr.` fields, so customers and suppliers round-trip), and
**20 Sachkontenbeschriftungen** (account names, so a *Kanzlei*'s own chart can
be adopted rather than retyped).
([Debitoren/Kreditoren field list](https://handbuch.conaktiv.de/wiki/version-15/buchhaltungsmodule/buchhaltung-in-conaktiv/nutzung-der-datev-schnittstelle-2017/datev-debitorenkreditoren-stammdaten-datei-extf-stammdaten-deb-kred-csv/),
[category overview](https://smartkontoauszug.de/blog/datev-extf-format-erklaert))

Import matters as much as export: the user's existing bookkeeping arrives as a
`Buchungsstapel` from their *Kanzlei*, and that file is also the best possible
test fixture (§15).

### 4.2 DATEV XML-Schnittstelle / Rechnungsdatenservice 1.0 (documents with their bookings)

The second interface is a ZIP *Document-Package* containing a
`document.xml` (*Verwaltungsdatendatei* — what is in the package, with a
**hash value per receipt**), one or more *Belegsatzdatendateien* (the invoice
data), and the receipt files themselves (PDF/image). DATEV *Belegtransfer*
ingests the ZIP **without unpacking it**, and the hash in `document.xml` is what
later links the receipt image in *DATEV Unternehmen online* to the posting in
the accounting.
([DATEV help 1071255](https://wissensplattform.apps.datev.de/help/document/1071255),
[developer portal](https://developer.datev.de/de/file-format/details/datev-xml-interface-online/getting-started-))

This is the interface that answers *"keine Buchung ohne Beleg"* — it ships the
receipt together with the booking, which the CSV format cannot do. It needs
`UltraCanvasXML` (§3.4), `UCZipPackageWriter`, and `UltraCrypt_HashFile`. Worth
phase 4; not worth phase 1.

`developer.datev.de` and the DATEV help centre are both blocked by this
environment's egress proxy, so the field-level specifics above come from
secondary sources and from the example file. **[unverified]** — every field
offset must be re-checked against the official *DATEV-Format* specification (and
against the user's own export, §15) before the writer is trusted.

### 4.3 The chart of accounts (SKR03 / SKR04)

SKR03 (process-ordered) and SKR04 (balance-sheet-ordered) are the de-facto
standard charts. The *frameworks themselves* — the account numbers and names —
are not protected and are implemented by essentially every German accounting
package; DATEV's *printed editions* are.
([discussion](https://www.gutefrage.net/frage/ist-der-datev-kontenrahmen-zb-skr03-etc-urheberrechtlich-geschuetzt),
[DATEV overview](https://www.datev.de/web/de/berufsgruppenuebergreifend/ratgeber/rechnungswesen/datev-standard-kontenrahmen))
Ship both as **data files, not code** — one JSON/CSV per chart per validity year,
carrying for each account: number, name, type, default *Steuerschlüssel*,
UStVA *Kennzahl*, EÜR line, BWA position, **and balance-sheet classification**
(the last one because the annual accounts are in scope, §2). **SKR03 is the
default and the one shipped and tested first** (§1.1); SKR04 sits beside it as a
second data file, not as a second code path. The user's actual chart, exported
from their *Kanzlei* as category 20, then overrides either.

---

## 5. The tax channels

### 5.1 UStVA and the ELSTER/ERiC reality

Since 1 January 2019 the *Umsatzsteuer-Voranmeldung* is transmitted through
**ERiC**, the ELSTER Rich Client: a C library the tax administration provides
free of charge for integration into tax and accounting software.
([ELSTER developer page](https://www.elster.de/eportal/infoseite/entwickler),
[background](https://www.pikon.com/de/blog/elster-umstellung-auf-eric-sind-sie-schon-bereit-dafuer/))
What integration actually entails, from the public record:

- **Registration first.** Participating as a software manufacturer requires
  registration and a *Hersteller-ID*; the SDK download itself is
  password-protected and has to be requested. An open-source reference
  integration ([ECTElster](https://github.com/Thomas-Mielke-Software/ECTElster),
  LGPL-2.1) documents exactly this: *"aktuelle Version des ERiC-SDK downloaden
  (wofür man erst einmal ein Passwort anfragen muss)"*.
- **Native shared libraries, dynamically loaded**, plus **per-year validation
  plugins** — that project copies `ericapi.dll`, `ericxerces.dll`, `eSigner.dll`
  and a `plugins2` folder holding `checkUStVA`, `checkEUER`, `commonData`
  libraries that are replaced every year.
- **The C API** is `EricInitialiseInstance` / `EricBearbeiteVorgang` /
  `EricCheckXML` / certificate handles, with structs for print
  (`eric_druck_parameter_t`) and signature/encryption
  (`eric_verschluesselungs_parameter_t`), a progress callback, and results in a
  `rueckgabeXmlPuffer`. Current documented version is the ERiC 43.x series.
  ([API reference index](https://www.instantview.org/data/CyberEnterprise/ERiC/ERiC-API-Referenz.pdf),
  [Rust bindings, which mirror the surface](https://docs.rs/eric-bindings/latest/eric_bindings/))
- **The XML schemas and per-data-type documentation ship inside the ERiC
  release** (`Dokumentation/Schnittstellenbeschreibungen`,
  `Dokumentation/Plausipruefungen`) — not on a public page. Data types relevant
  here: `UStVA`, `UStDV` (*Dauerfristverlängerung*), `ZM`, `EUER`, the annual
  `USt` return.
- **A release cadence to plan around:** a main release in November and a second
  early in the year; each carries new *Datenartversionen* (e.g. ERiC 37 added
  taxonomy 6.6, ERiC 39 added 6.7). An accounting program that speaks ERiC
  acquires a **twice-yearly maintenance obligation, forever.**

`elster.de` and `instantview.org` are blocked from this environment, so the API
names and the flag vocabulary above are **[unverified]** against the official
handbook; they are consistent across three independent secondary sources, which
is enough to plan with and not enough to code against. The first implementation
task is to obtain the SDK and check every signature.

**Design consequence — two paths, one of which always works:**

```
UltraFIBU::Elster::IElsterTransport          // engine interface
  ├── ElsterXmlFileTransport   (always built) → writes the UStVA XML + a
  │                                              protocol/PDF-preview to disk
  │                                              for manual upload in Mein ELSTER
  └── ElsterEricTransport      (optional)     → dlopen/LoadLibrary of ERiC,
                                                 validate + send + store the
                                                 Transferticket
```

The engine builds and the app runs with no ERiC present, exactly as the PDF and
OCR backends soft-fail (`ULTRACANVAS_PLUGIN_PDF` pattern). ERiC binaries are
**never vendored into this repository** — the distribution agreement does not
permit it and `THIRD_PARTY_LICENSES.md` is not a workaround; the user (or an
installer step) places them in a configured directory, and the app reports
clearly when it cannot find them. The ELSTER certificate (`.pfx`) path lives in
the config and its **PIN goes into UltraVault**, never into a config file.

**The UStVA figures themselves are a mapping, and the mapping is data.** Every
*Kennzahl* is a sum over the journal for a calendar period, selected by
*Steuerschlüssel*. The ones confirmed while writing this document:

| Kz | Meaning |
|---|---|
| 81 / 86 | Taxable supplies at 19 % / 7 % (*Bemessungsgrundlage*) |
| 35 + 36 | Supplies at other rates: base + tax |
| 41 | Intra-community supplies, § 4 Nr. 1b UStG |
| 43 | New for **2026**: supplies under the EU defence-industry instrument (SAFE), line 22 |
| 45 | Other non-taxable supplies (place of supply abroad) — **where OSS turnover is reported, base only, no tax** |
| 66 | Input tax from incoming invoices |
| 87 | Supplies at 0 % |

([sevDesk Kennzahlen reference](https://hilfe.sevdesk.de/de/articles/9886922-kennzahlen-der-umsatzsteuervoranmeldung),
[BMF Vordruckmuster UStVA 2026](https://www.bundesfinanzministerium.de/Content/DE/Downloads/BMF_Schreiben/Steuerarten/Umsatzsteuer/2025-12-29-vordruckmuster-USt-voranmeldung-2026.pdf?__blob=publicationFile&v=7),
[2026 form changes](https://www.steuerschroeder.de/blog/umsatzsteuer-2026-neue-vordrucke-fuer-voranmeldung-und-dauerfristverlaengerung/))

The rest (intra-community acquisitions, § 13b reverse charge both ways, import
VAT, § 19 *Kleinunternehmer*, the *Sondervorauszahlung* offset, the resulting
*Zahllast*) come from the BMF *Vordruckmuster*, which **changes every year** —
Kz 43 appearing in 2026 is the proof. Therefore: `ustva_kennzahl_mapping` is a
table keyed by validity year, shipped as data, diffed against the BMF form each
December. Not a `switch` statement in C++.

**The *Dauerfristverlängerung*** (§ 46 UStDV) shifts every deadline by a month
against an 11th-of-the-annual-tax *Sondervorauszahlung*; it is its own ELSTER
data type and its own January workflow, and the deadline engine must know
whether the company has one.

### 5.2 Zusammenfassende Meldung (ZM)

Intra-community supplies and § 18b services are additionally reported to the
**BZSt** as a *Zusammenfassende Meldung*, monthly or quarterly depending on
volume, per customer VAT number and per type. It is an ELSTER data type (`ZM`),
so it rides the same two transports as the UStVA, and it is the natural
companion of the Kz 41 total — the two must reconcile, and the app should say so
when they do not. ([BZSt on electronic ZM filing](https://www.bzst.de/DE/Unternehmen/Umsatzsteuer/ZusammenfassendeMeldung/ElektronischeAbgabe/Elster/Elster.html))

### 5.3 One-Stop-Shop — there is no machine interface

OSS returns are quarterly, due within one month of quarter end, and filed at the
BZSt through **Mein BOP**. The only bulk path the BZSt offers is a **CSV
transport file** uploaded by hand in the portal (an import function available
since 2022, for periods from Q3/2021); everything else is manual entry in the
form. There is **no published API**.
([BZSt — Elektronische Datenübermittlung (OSS)](https://www.bzst.de/DE/Unternehmen/Umsatzsteuer/One-Stop-Shop_EU/ElektronischeAbgabe/elektronischeabgabe_oss.html),
[BOP OSS registration form](https://www.elster.de/bportal/formulare-leistungen/alleformulare/osseureg),
[practitioner account](https://help.lexware.de/de-form/articles/5588384-quartalsmeldungen-der-eu-zielland-steuer-beim-bundeszentralamt-fur-steuern))

So the honest design is: **compute, generate, hand over.**

1. An `oss_umsatz` ledger derived from the journal: per quarter, per destination
   member state, per rate (standard and reduced, *as that state defines them*),
   base and tax — plus the invoices behind each figure, because the BZSt's
   queries come months later.
2. Export the BOP **CSV transport file** (the portal's export ZIP contains two
   CSV layouts — one machine-oriented, one human-readable; ship the machine one
   and offer the readable one for checking). **[unverified]** — the exact column
   layout must be taken from a current BOP export, which is one of the files to
   upload (§15).
3. Watch the **§ 3c UStG delivery threshold**: EU-wide €10 000 of B2C distance
   sales and electronic services, after which taxation moves to the destination
   country. The app must track the running total across all member states and
   warn *before* it is crossed, because crossing it silently is the expensive
   mistake.
4. Keep OSS turnover **out of the German VAT base** and report it in **UStVA
   Kz 45** (base only, no tax, no effect on the *Zahllast*), with the same
   treatment in the annual return. ([reference](https://wissen.buchhaltungsbutler.de/hc/de/articles/11322803594269-Vorbereitung-der-OSS-Meldung-in-der-Buchhaltung))
5. Book each destination country's VAT to its own liability account, since it is
   owed to the BZSt and forwarded, not to the local *Finanzamt*.

**Import-One-Stop-Shop (IOSS)** for consignments ≤ €150 is the same shape with a
different registration and a monthly period. Model it as a second *Verfahren* on
the same tables rather than a copy.

### 5.4 Confirming European VAT numbers — the interface just changed

Two things are needed and they are not the same thing:

- **Checksum and format validation**, offline, per country (the 27 national
  rules). Cheap, instant, catches typing errors, no network.
- **A qualified confirmation** (*qualifizierte Bestätigungsabfrage*, § 18e UStG)
  from the BZSt, which is what actually protects a zero-rated intra-community
  supply — and **the data set the BZSt returns is itself the evidence**, so it
  must be stored verbatim, per query, forever, not reduced to a boolean.

The BZSt's **XML-RPC interface became obsolete on 30 November 2025** and is
replaced by a **REST API** (`https://api.evatr.vies.bzst.de`, OpenAPI at
`/api-docs`), carrying both the simple and the qualified confirmation over
HTTPS/TLS.
([BZSt — XML-RPC obsolete from 30.11.2025](https://www.bzst.de/DE/Unternehmen/Identifikationsnummern/Umsatzsteuer-Identifikationsnummer/eVatR/eVatR_Info_Schnittstelle/eVatR_info_schnittstelle.html),
[BZSt — eVatR-Anbindung](https://www.bzst.de/DE/Unternehmen/Identifikationsnummern/Umsatzsteuer-Identifikationsnummer/eVatR/eVatR_node.html),
[migration note](https://www.tso.de/en/news/news/change-to-vat-id-verification-bzst-switches-to-new-rest-api/))
`UltraNet_HttpPost` plus `UltraCanvasJSON` covers it with no new dependency.
`api.evatr.vies.bzst.de` is blocked from this environment, so the request and
response fields are **[unverified]**; the OpenAPI document must be read first.
Any bookkeeping code still written against the old XML-RPC endpoint is now dead
code — a useful thing to know before copying an example from the internet.

---

## 6. The data model

### 6.1 Money

`UltraFIBU::Money` — or better `UltraCanvasMoney` in the framework (§3.5):
`int64_t` minor units + ISO 4217 code. No `double` anywhere between the parser
and the report. Explicit *kaufmännische Rundung* (half away from zero) at every
point where a rate is applied, because that is what the tax authority's own
arithmetic does; allocation helpers so a rounded total always equals the sum of
its rounded parts. Foreign currency keeps *both* the original amount with its
rate and the EUR amount — DATEV's `WKZ Umsatz` / `Kurs` / `Basis-Umsatz` columns
expect exactly that, and so does § 16 Abs. 6 UStG conversion.

### 6.2 Geschäftsjahr — flexible by construction

```
geschaeftsjahr(id, mandant_id, beginn DATE, ende DATE,
               status TEXT,          -- offen | festgeschrieben | abgeschlossen
               festschreibung_bis DATE,
               skr TEXT,             -- SKR03 | SKR04 | eigener
               sachkontenlaenge INT) -- 4..8, mirrors the DATEV header
```

Rules the store enforces, not the UI:

- **No overlap, no gap** between consecutive years of one *Mandant*.
- **Any start date.** `beginn = 2026-04-01`, `ende = 2027-03-31` is an ordinary
  row, not a special case. A *Rumpfwirtschaftsjahr* (a short year, which is what
  a changeover produces) is simply a row shorter than twelve months, and the
  report code must never assume twelve periods.
- **Periods are relative to the year's start.** For a 1 April year, period 1 is
  April. A `Periode(date) → 1..n` function on the *Geschäftsjahr* is the only
  place that arithmetic exists.
- The *Geschäftsjahr* is what feeds the DATEV `WJ-Beginn` header field (§4.1)
  and what EÜR, BWA and the balance sheet aggregate over.

This is the requirement the user singled out, and it is worth being precise
about why it is cheap here and expensive elsewhere: it costs nothing as long as
*no* date arithmetic in the codebase assumes January. It costs a rewrite the
moment one report says `month - 1`.

### 6.3 Two calendars over one journal

The *Umsatzsteuer-Voranmeldung* period is a **calendar** month or **calendar**
quarter (§ 18 UStG) and does not care what the company's *Geschäftsjahr* is.
So the journal is aggregated two ways that must never be conflated:

| Aggregate | Calendar | Driven by |
|---|---|---|
| EÜR, BWA, balance sheet, *Jahresabschluss*, depreciation | **fiscal** period 1..n of the *Geschäftsjahr* | `geschaeftsjahr.beginn` |
| UStVA, ZM, OSS quarter, annual VAT return | **calendar** month / quarter / year | statute |

Consequences the schema must carry: every posting stores **both** its
`geschaeftsjahr_id`/`periode` **and** its plain `belegdatum`; the tax period is
derived from the date alone and never from the fiscal period; and a *Voranmeldung*
may legitimately span two *Geschäftsjahre* (for a 1 April year, the Q1 2027
return covers a fiscal-year boundary). A "close the year" operation must
therefore not lock the calendar periods a not-yet-filed UStVA still needs.

### 6.4 The journal, the documents, and Soll-/Ist-Versteuerung

Store **double-entry postings** as the single source of truth, in DATEV's own
shape — `umsatz` (unsigned), `soll_haben`, `konto`, `gegenkonto`, `bu_schluessel`,
`belegdatum`, `belegfeld1/2`, `buchungstext`, `kost1/kost2` — because that shape
is what must be exported (§4.1), because it is what the *Kanzlei* understands,
and because every other view can be derived from it. The reverse is not true: a
cash-basis EÜR list cannot be turned back into a ledger.

**EÜR is then a projection, not a second store.** Whether a receipt hits the
EÜR at invoice date or at payment date is the *Soll-* vs *Ist-Versteuerung*
question (§ 20 UStG), it is a per-company setting, and it also decides which
period a VAT amount belongs to. **The default is *Soll-Versteuerung*** (§1.1):
VAT falls due with the invoice, so the projection keys on *Belegdatum*. Having
both that date and the linked payment date on hand is what makes the other
setting a one-flag change — storing only one of the two dates is the mistake
that cannot be undone later.

Documents (`beleg`) sit beside the journal, not inside it: an invoice has
positions, a supplier, a currency, a file, a status, a dunning history — and it
produces postings. One document, one or more postings, and a *Storno* is a new
document with its own postings pointing back at the original.

### 6.5 Steuerschlüssel — the table that holds all the tax knowledge

One table, versioned by validity date, is the single place that knows how a
transaction is taxed:

```
steuerschluessel(id, bezeichnung,
                 art,              -- inland | ig_lieferung | ig_erwerb |
                                   -- drittland | reverse_charge_13b |
                                   -- oss | kleinunternehmer_19 | nicht_steuerbar
                 satz_promille,    -- 190, 70, 0 … (integer, no floats)
                 land,             -- ISO country, for OSS and Drittland rows
                 datev_bu,         -- the DATEV BU-Schlüssel, both directions
                 kz_bemessung, kz_steuer,   -- UStVA Kennzahlen (§5.1)
                 konto_umsatz, konto_steuer,
                 gueltig_von, gueltig_bis)
```

Everything the screenshots show as a decision — *Drittland*, "0 % with the gross
amount as the base", 19 % vs 7 %, reverse charge, OSS by destination country —
becomes a row here. Rate changes and the yearly UStVA form changes become new
rows with new validity dates, which is also what makes prior years still
reportable after a change. No tax rule is written as a C++ branch.

### 6.6 Master data: Kunden und Lieferanten

The request asks for clients and suppliers with European VAT number, address and
contact info. Concretely:

```
partner(id, typ,                 -- kunde (Debitor) | lieferant (Kreditor) | beide
        konto INT,               -- Personenkonto, width follows sachkontenlaenge
        name, name2, anrede, kontaktperson, abteilung,
        strasse, plz, ort, land,       -- ISO 3166-1 alpha-2
        steuerkategorie,         -- inland | eu_unternehmer | eu_privat |
                                 -- drittland  → drives the default Steuerschlüssel
        ust_idnr, ust_idnr_status, ust_idnr_geprueft_am,
        ust_idnr_protokoll,      -- the BZSt response, verbatim (§5.4)
        steuernummer,
        email, telefon, webseite,
        iban, bic, zahlungsbedingung_id, skonto_prozent, skonto_tage,
        sprache, notiz, angelegt_am, geaendert_am)
```

Notes that matter:

- **`konto` is a DATEV *Personenkonto*** and its range depends on
  `sachkontenlaenge` (with 4-digit G/L accounts, person accounts are 5 digits —
  customers low, suppliers high). The store allocates the next free number in
  the configured range; it must not hard-code 10000/70000.
- `steuerkategorie` + `land` + the presence of a **confirmed** VAT number is what
  makes an invoice zero-rated. That is a three-field decision and the UI should
  show it as one ("*innergemeinschaftliche Lieferung, steuerfrei*") with the
  reason visible.
- The VAT-number **check protocol** is retained as evidence (§5.4), with a
  re-check reminder — a number confirmed three years ago proves nothing today.
- Address and contact data of natural persons is personal data: the GoBD
  10-year retention and the GDPR *Löschkonzept* both apply, and they conflict.
  Bookkeeping retention wins for booked documents; unbooked contacts are
  deletable. Worth one paragraph in the *Verfahrensdokumentation* (§8).

### 6.7 Schema sketch

One SQLite database per *Mandant* (company), through UltraDatabase, migrated by
`UltraDb_Migrate` with a `kSchemaVersion` constant exactly as
`EmailCleaner::AnalysisStore` does it:

```
mandant            -- name, address, Steuernummer, USt-IdNr, Finanzamt,
                   -- Berater-/Mandantennummer, besteuerung(soll|ist),
                   -- kleinunternehmer, ustva_rhythmus(monat|quartal),
                   -- dauerfristverlaengerung, oss_registriert, waehrung
geschaeftsjahr     -- §6.2
konto              -- chart of accounts: nummer, bezeichnung, typ, skr,
                   -- eur_zeile, bwa_position, default steuerschluessel
steuerschluessel   -- §6.5
partner            -- §6.6
zahlungsbedingung  -- net days, Skonto, dunning levels
beleg              -- art(ausgangsrechnung|eingangsbeleg|gutschrift|kasse),
                   -- nummer, datum, leistungszeitraum, partner_id, waehrung,
                   -- kurs, netto, steuer, brutto, status, faellig_am,
                   -- festgeschrieben, datei_pfad, datei_hash, erechnung_xml
beleg_position     -- titel, konto, steuerschluessel_id, menge, einzelpreis,
                   -- netto, steuer, kostenstelle
buchung            -- the journal, §6.4, + storno_von, hash, prev_hash,
                   -- erfasst_von, erfasst_am, festgeschrieben
bankkonto          -- IBAN, BIC, bank, G/L account, last import state
bankumsatz         -- datum, valuta, betrag, waehrung, gegen_iban, gegen_name,
                   -- verwendungszweck, e2e_ref, camt_ref(unique), status
zuordnung          -- bankumsatz_id ↔ beleg_id, betrag  (n:m, part payments)
oss_umsatz         -- quartal, ziel_land, satz, bemessung, steuer, beleg_id
meldung            -- art(ustva|zm|oss|dfv), zeitraum, kennzahlen(JSON),
                   -- status, xml_hash, transferticket, eingereicht_am
audit              -- tabelle, row_id, aktion, alt, neu, benutzer, zeit
anlage             -- (phase 6) fixed assets, AfA method, useful life
```

`bankumsatz.camt_ref` unique is what makes re-importing the same statement
idempotent — the single most common source of duplicate bookkeeping.

---

## 7. Documents in and out (E-Rechnung)

### 7.1 Incoming — already mandatory

Since **1 January 2025** every German business must be able to *receive* a
structured electronic invoice. That is not a future project, it is a present
obligation, and it means UltraFIBU must read:

- **XRechnung** — a CIUS of EN 16931, in either **UBL** or **CII** syntax.
  Current version **3.0.2**; version 4.0, implementing the revised
  EN 16931-1:2026, is expected mid-to-late 2026, so the reader must be
  version-tolerant and the version must be recorded per document.
- **ZUGFeRD / Factur-X** — a PDF/A-3 carrying the CII XML as an embedded file.
  **2.5 was released on 20 May 2026**; a ZUGFeRD document on the EN 16931
  profile is simultaneously XRechnung-3.0.2-conformant, so one CII reader
  serves both. Reading it needs the embedded-file extraction that MuPDF gives
  us and nothing new in the PDF writer.

([timeline and formats](https://www.comarch.de/produkte/datenaustausch-und-dokumentenmanagement/e-invoicing/e-invoicing-in-deutschland/xrechnung-zugferd/),
[ZUGFeRD 2.5 release](https://www.truecommerce.com/de/blog/neue-informationen-und-zeitplan-zur-einfuehrung-der-e-rechnung-in-deutschland/),
[deadline overview](https://www.e-rechnungen.org/e-rechnung-pflicht-fristen))

A received invoice should arrive as a *proposed* `beleg` with positions, tax
breakdown, supplier matched by VAT number or IBAN, and the XML **stored
verbatim** next to the extracted fields — the XML is the original document for
retention purposes, and the rendered PDF is only a view of it. That is also why
the archive is content-addressed (§10.4).

### 7.2 Outgoing — XRechnung now, ZUGFeRD after the PDF work

The issuing obligation phases in: **1 January 2027** for businesses with more
than €800 000 prior-year turnover, **1 January 2028** for all domestic B2B, with
exceptions for small amounts up to €250, tickets, *Kleinunternehmer* and B2C.
([2027/2028 phases](https://rickert.law/e-rechnung-b2b-2027/))
So outgoing structured invoices are not optional for long, and the order is
dictated by §3.2:

1. **XRechnung XML** — pure `UltraCanvasXML` output from the `beleg` model. No
   PDF machinery involved. Cheap, and legally sufficient B2B.
2. **A printable PDF** — through `VectorStorage::VectorDocument` and the
   existing `PDFVectorConverter`, or through the IODeviceManager printer path.
   Good enough for B2C and for a human-readable copy.
3. **ZUGFeRD** — needs PDF/A-3: embedded subset fonts, output intent, XMP with
   the ZUGFeRD identification, and the `/AF` embedded-file stream. This is
   framework work on the PDF writer, sized in its own right, and it is the one
   piece of this application that should *not* be attempted quickly.

Invoice numbering must be unique and unbroken per number range (§ 14 UStG) —
see §10.3, because that requirement and multi-user access interact.

---

## 8. GoBD — immutability is a schema property

The GoBD demand *Unveränderbarkeit*, traceability, completeness, timely
recording, and 10-year retention, plus a *Verfahrensdokumentation* describing
the procedure. The screenshots show what that looks like in practice:
*"Festgeschrieben: Der Inhalt dieses Dokuments ist nicht mehr änderbar.
Änderungen sind nur über eine Stornierung möglich."*

Design:

1. **Append-only journal.** `buchung` rows are inserted, never updated. A
   correction is a *Storno* row (same amounts, reversed *Soll/Haben*,
   `storno_von = <original id>`) plus the corrected posting. `UPDATE` on a
   `festgeschrieben` row is refused by the store, not by the UI.
2. **Festschreibung as a date, not a flag per row.** `geschaeftsjahr.festschreibung_bis`
   plus the per-row `festgeschrieben` marker (DATEV carries the same concept in
   its stack, and an unfestgeschriebener stack is what a *Kanzlei* can still
   correct). Everything up to that date is frozen atomically.
3. **A hash chain over the journal**, using `UltraCrypt_Hash`: each row stores
   `hash = H(prev_hash ‖ canonical row)`. Cheap, verifiable in one pass, and it
   turns "the database file was edited with sqlite3" from undetectable into
   obvious. It is not a qualified signature and must not be described as one —
   it is tamper *evidence*, which is what the GoBD ask of a bookkeeping system.
4. **A real audit trail** (`audit`): who, when, what changed, before and after —
   including every failed *Festschreibung* attempt. In multi-user mode "who" has
   to be a real authenticated user (§10.2).
5. **Z3 export — *Datenträgerüberlassung* in the *Beschreibungsstandard*.** The
   auditor's own software (IDEA) reads a medium containing
   `gdpdu-01-09-2004.dtd`, an **`index.xml`** describing the structure, and the
   data as CSV or fixed-length files. Only the description is XML; the data is
   flat.
   ([Beschreibungsstandard](https://www.caseware.net/fileadmin/audicon/media/allgemeine_bilder/Beschreibungsstandard-GDPdU-01-03-2019.pdf),
   [what a medium must contain](https://www.caseware.net/loesungen/tax-compliance/gobd/z3-datenzugriff-wie-kann-ich-meine-daten-im-beschreibungsstandard-exportieren/))
   `UCZipPackageWriter` + the CSV writer + `UltraCanvasXML` produce it. This
   export is also the **archival format** that makes the 10-year retention
   real — a yearly Z3 medium plus the original documents will still be readable
   when the application's own schema is four migrations further on.
6. **Ship a *Verfahrensdokumentation* template** with the application, filled in
   with what the software actually does (data flows, controls, the
   *Festschreibung* rules, the backup regime). The obligation is the company's,
   but the software is the only place that knows half the content, and every
   commercial competitor ships one.

---

## 9. Bank statements and automatic assignment

The *Kontoumsätze* screen implies three capabilities.

1. **Import.** **CAMT.053** (ISO 20022 XML) is the format to build on: German
   banks completed the replacement of **MT940** in November 2025, and one
   CAMT.053 file carries one account. Keep an MT940 reader for archives and a
   configurable CSV reader for banks that only export CSV.
   ([MT940 → CAMT changeover](https://support.immoware24.de/hc/de/articles/30222978488477-Umstellung-auf-CAMT-V8-Abl%C3%B6sung-von-SWIFT-MT940-MT942-bis-November-2025),
   [format comparison](https://smartkontoauszug.de/blog/camt-053-vs-mt940))
   Idempotency comes from the statement/entry reference, stored unique
   (§6.7) — importing the same file twice must change nothing.
2. **Fetching, later.** **FinTS 3.0** is the German bank channel, and since
   1 August 2019 only **registered products** may use it: a registration form to
   `registrierung@hbci-zka.de`, a product registration number returned in
   10–15 business days, and that number sent in every dialog initialisation.
   ([FinTS product registration](https://www.fints.org/de/hersteller/produktregistrierung),
   [FAQ](https://www.fints.org/de/hersteller/faq-produktregistrierung))
   That is an administrative dependency with a lead time, so it belongs in a
   later phase — and note the screenshot's own warning
   ("*Deine Konto-Anmeldung ist ausgelaufen*"), which is the permanent state of
   affairs with bank connections: whatever is built must treat re-authentication
   as normal, not exceptional.
3. **Automatic assignment** (*Automatische Zuordnung*). A scoring matcher, not
   magic: exact amount + document number found in the *Verwendungszweck* is a
   certain match; amount + IBAN + a date window is a probable one; partial
   payments need the n:m `zuordnung` table; and every accepted suggestion feeds
   a per-partner rule ("this IBAN with this text is always account 4980"), which
   is what makes month two faster than month one. Suggestions are always
   *proposals* — nothing posts without confirmation, because a wrong automatic
   posting inside a *festgeschriebener* period can only be fixed by *Storno*.

---

## 10. Storage: one machine, or a server with several users

The requirement is *"store data local but also on a server to have multiple
users access it"*. Both modes are the same application with the same schema and
the same store API; what changes is the named UltraDatabase connection behind
it.

```
UltraFIBU::Store  →  UltraDb_RegisterConnection({ name = "fibu", driver = ... })
                      ├── "sqlite"      → ~/.local/share/UltraFIBU/<mandant>.db   (Einzelplatz)
                      └── "postgresql"  → host, TLS required, credentials from UltraVault  (Mehrplatz)
```

That indirection is exactly what UltraDatabase was designed for — *"switching
from Postgres to MySQL later is a one-line change … no query code changes"* — and
it means server mode costs nothing in the application if four rules are followed
from the first commit.

### 10.1 What "server mode" costs, honestly

**The PostgreSQL driver does not exist yet.** Verified: `core/UltraDatabase/`
contains `UltraDatabaseSqliteDriver.cpp`, `UltraDatabaseValue.cpp` and
`UltraDatabaseManager.cpp`, and there is no `Plugins/UltraDatabase/` directory —
the networked drivers, pooling and async queries are the documented Stage 2/3
plan. So multi-user UltraFIBU has a **named framework prerequisite**: implement
the `libpq` driver behind `IDatabaseDriverPlugin`, with TLS on by default and
credentials resolved through UltraVault, as the module's README already
specifies. That is bounded, reusable framework work — UltraSocial and
EmailCleaner would inherit it — but it is not free, and it must be scheduled
(phase 7, §13) rather than assumed.

Until it lands, the four rules keep the door open:

1. **No SQLite-specific SQL.** No `AUTOINCREMENT`, no `INSERT OR REPLACE`, no
   `strftime`/`julianday` in queries, no reliance on dynamic typing. Dates as
   ISO-8601 text or integer epoch, money as `BIGINT` minor units, every date
   computation in C++ where the *Geschäftsjahr* logic already lives.
2. **Everything through `UltraDb_*` with bound parameters** — already the
   framework rule, and the thing that makes the swap a config change.
3. **Every write in an explicit transaction** (`UltraDb_Begin` /
   `ExecInTx` / `Commit`), because on a server the alternative is a half-posted
   document.
4. **No assumption that this process is the only writer.** Which is §10.2–10.3.

**What must not be done:** putting the SQLite file on an SMB/NFS share, a cloud
drive, or an UltraCloud-synced folder and letting several people open it.
SQLite's locking is not reliable over network filesystems, and a file
synchroniser resolves two concurrent versions of a ledger by picking one — that
is silent, unrecoverable data loss in a book that must be complete and
unalterable by law. Local file **or** a real database server; there is no third
option. UltraCloud's place in this application is off-site backup and the
document archive (§10.4), never the live ledger.

### 10.2 Users, roles and attribution

GoBD attribution (§8.4) stops being a formality the moment two people share a
book: every row must name a *real* user. The framework has no authentication or
user-management module (`Apps/UltraAuthenticator` is a TOTP generator, not a
login service), so this is app-level:

```
benutzer(id, anmeldename, anzeigename, email, rolle,
         passwort_hash, passwort_salt, kdf_params,   -- UltraCrypt_DeriveKeyFromPassword
         totp_secret_ref,                            -- optional 2FA, secret in UltraVault
         aktiv, angelegt_am, letzter_login)
```

Roles, kept few and meaningful: **Administrator** (users, number ranges, chart
of accounts, *Festschreibung*), **Buchhalter** (post, reverse, file returns),
**Erfasser** (create documents and proposals, cannot post or file), **Steuerberater**
(read everything, export DATEV, no writes), **Nur-Lesen**. Permissions are
enforced in the *store*, not in the UI, because in server mode the database is
reachable without the UI — and for the same reason the database user must not be
a superuser, and per-role database roles are worth having once the `libpq`
driver exists.

Password hashing uses `UltraCrypt_DeriveKeyFromPassword` with
`UltraCrypt_RecommendedKdfParams`; the parameters are stored per user so they can
be raised later. Local mode may skip login entirely (one operator, OS account is
the boundary) but must still record a user id, so that the same database opened
in server mode later has an unbroken attribution history.

### 10.3 Concurrency: the three places it actually bites

- **Gap-free numbering.** Invoice numbers, document numbers and journal
  sequences must be unique and without unexplained gaps (§ 14 UStG). Under two
  writers `SELECT MAX(nummer) + 1` produces duplicates, reliably, on the first
  busy day. Allocation belongs to a `nummernkreis` table, incremented **inside
  the posting transaction** (`UPDATE … SET naechste = naechste + 1` with the row
  locked / `RETURNING` on PostgreSQL), never from a client-side counter, and a
  cancelled draft must not consume a number — which is why the number is
  assigned at *posting* time, not when the editor opens. (The screenshots show
  this exact behaviour: a draft is *Entwurf* until saved.)
- **Two people editing one document.** Optimistic locking: every mutable row
  carries a `version`; an `UPDATE … WHERE id = ? AND version = ?` that affects
  zero rows is reported as *"dieser Beleg wurde inzwischen von X geändert"*
  rather than overwriting. For the invoice editor, add a short soft lock
  (`bearbeitet_von`, `bearbeitet_seit`, expiring) so the collision is prevented
  rather than reported.
- **Migrations with several clients.** `UltraDb_Migrate` is per-connection and
  assumes it is alone. In server mode the app must: read the schema version
  first; **refuse to run** against a *newer* schema than the binary knows
  (an old client writing into a new schema is how ledgers get corrupted);
  and take a database-level advisory lock so exactly one client migrates while
  the others wait and then reconnect. A one-line startup gate that saves a
  restore-from-backup later.

Append-only tables (§8.1) are a gift here: the journal has no update contention
at all, so the only hot rows are the number ranges.

### 10.4 Documents on a server, and backup

Receipt files are the other half of the data and they do not belong in the
database as blobs at ten-year scale. Store them **content-addressed** — the file
path is derived from its SHA-256, which the schema already keeps as
`beleg.datei_hash`:

- deduplication is automatic (the same PDF mailed twice is one file);
- the hash is the tamper evidence GoBD wants, and the same value DATEV's
  `document.xml` needs (§4.2);
- the store can live on the server's filesystem, on a WebDAV/Nextcloud target
  through **UltraCloud**, or locally with a cache — the address does not change,
  so moving the archive is not a migration.

Backup is part of the design, not an afterthought, because the retention
obligation is ten years: a nightly database dump plus the document store,
encrypted (UltraCrypt AEAD) to an UltraCloud destination, **plus** the yearly
GoBD Z3 medium (§8.5) as the format-independent archival copy. A restore
rehearsal belongs in the *Verfahrensdokumentation*.

### 10.5 What server mode deliberately does not include

**Offline editing with later synchronisation.** Two-way sync of an append-only,
legally immutable, gap-free-numbered ledger is a distributed-systems project
several times the size of this application, and every shortcut produces either
duplicate invoice numbers or lost postings. Server mode requires the server to
be reachable (LAN or VPN); local mode is for one machine. A read-only offline
snapshot for reporting is a reasonable later convenience and is not the same
feature.

Direct database connections should not be exposed to the open internet; the
deployment shapes to support are LAN and VPN, with TLS required in both. A thin
HTTP application server in front of the database (UltraNet can host one) is the
architecture that would allow internet access without a VPN — worth noting as a
future option, not worth building before the `libpq` driver exists and the
application is in daily use.

---

## 11. The German user interface

The application shell follows `Apps/EmailCleaner` and `Apps/UltraSocial`:
`UltraCanvasApplication`, one window, a top **`UltraCanvasToolbar`** of icon
tabs, and a content area swapped per section — which is also exactly the shape
of the supplied screenshots.

| Section (the label the user sees) | Layout | Elements |
|---|---|---|
| **Kontoumsätze** | `UltraCanvasSplitPane`: transaction grid left, detail card right | `UltraCanvasDataGrid` (§3.3, with the per-row status marker and the footer total), `UltraCanvasBadge` (*Erledigt*), `UltraCanvasDropdown` (*Alle / offen / zugeordnet*), `UltraCanvasTextInput` (search), `UltraCanvasDateRangePicker` |
| **Belege** | grid + editor; editor is receipt preview beside a form | `UltraCanvasPDFView` / `UltraCanvasImageElement` for the scan, `CreateFormGrid`/`AddFormRow` for the fields, `UltraCanvasAutoComplete` (supplier), `UltraCanvasDropdown` (category, tax rate, country), currency field (§3.5), `UltraCanvasAlert` for the *Drittland* input-tax note, `UltraCanvasBadge` (*Bezahlt*, *Festgeschrieben*) |
| **Rechnungen** | grid with overdue emphasis, selection column, bulk actions | `UltraCanvasDataGrid`, `UltraCanvasChip` (*Überfällig*), `UltraCanvasToolbar` (*Löschen / Archivieren / PDF*), `UltraCanvasPagination` |
| **Neue Rechnung** | form + line-item grid + totals | `CreateFormGrid`, `UltraCanvasAutoComplete` (customer), `UltraCanvasDatePicker`, `UltraCanvasDropdown` (project, template, *Zahlungsbedingungen*), `UltraCanvasSegmentedControl` (*Netto / Brutto*), editable `UltraCanvasDataGrid` for positions, `UltraCanvasSwitch`, `UltraCanvasSpinner` |
| **Berichte** | a menu (*Cashflow, BWA, EÜR, UStVA*) over a chart + table | `UltraCanvasMenu`, `UltraCanvasAreaChart`/`UltraCanvasLineChart`/`UltraCanvasBarChart`, `UltraCanvasDataGrid`, `UltraCanvasSpreadsheet` for the export view |
| **Stammdaten** | customers, suppliers, chart of accounts, tax keys, number ranges, users | grids + `CreateFormGrid` forms, `UltraCanvasTreeView` for the account hierarchy |
| **Steuer** | UStVA / ZM / OSS periods, each with figures, validation messages, submission state | `UltraCanvasDataGrid`, `UltraCanvasModalDialog` for the submission dialog, `UltraCanvasProgressDialog` during transmission |

Rules for this UI, taken from AGENTS.md and from the domain:

- **Nothing is hand-painted.** The two elements that do not exist yet
  (`UltraCanvasDataGrid`, the currency input) go into the framework, not into
  `Apps/UltraFIBU`. `scripts/check_ui_reuse.py` enforces this and its baseline
  file is empty; it stays empty.
- **German number and date formatting everywhere in the UI** (`1.234,56 €`,
  `17.09.2026`) and dot-decimal in every file format except DATEV CSV, which is
  comma — all of it through the formatting helpers, never through `LC_NUMERIC`
  (§3.5).
- **Strings come from one keyed German catalogue** (§3.5), so the vocabulary
  stays consistent (*Beleg*, *Buchung*, *Festschreibung* mean one thing each) and
  a later locale is possible.
- **Destructive and legal actions are confirmed and explained**: *Festschreiben*,
  *Stornieren*, submitting a UStVA. Each states what becomes impossible
  afterwards.
- **Keyboard-first data entry.** Bookkeeping is typed, not clicked: tab order
  through the posting form, amount fields that accept `1234,56`, account fields
  that accept a number or a name fragment, and *Enter* to post and start the
  next. This is the difference between a program an accountant uses and one they
  tolerate; `UltraCanvasAutoComplete` and the grid's inline editing carry it.

---

## 12. Build, versioning and testing

**Layout** (the `EmailCleaner`/`UltraSocial` pattern, verified against both):

```
Apps/UltraFIBU/
  CMakeLists.txt        # UltraFIBUEngine (STATIC, headless) + UltraFIBU (GUI)
  main.cpp
  engine/               # no UI, no SQL outside the store, unit-testable
    UltraFIBUTypes.{h,cpp}        Money, Beleg, Buchung, Geschaeftsjahr
    UltraFIBUStore.{h,cpp}        UltraDatabase schema + queries (kSchemaVersion)
    UltraFIBUGeschaeftsjahr.{h,cpp}   fiscal calendar, periods, Rumpfjahr
    UltraFIBUSteuer.{h,cpp}       Steuerschlüssel evaluation, UStVA mapping
    UltraFIBUDatevRead/Write.cpp  EXTF categories 21 / 16 / 20
    UltraFIBUElster.{h,cpp}       IElsterTransport + XML writer
    UltraFIBUElsterEric.cpp       optional ERiC backend (dynamically loaded)
    UltraFIBUOss.{h,cpp}          OSS/IOSS ledger + BOP CSV
    UltraFIBUEInvoice.{h,cpp}     XRechnung/ZUGFeRD read, XRechnung write
    UltraFIBUBank.{h,cpp}         CAMT.053 / MT940 / CSV + matcher
    UltraFIBUReports.{h,cpp}      EÜR, BWA, Cashflow, journal, Z3 export
    UltraFIBUVatId.{h,cpp}        checksums + BZSt eVatR REST
    UltraFIBUUsers.{h,cpp}        users, roles, permissions (§10.2)
    UltraFIBUAbschluss.{h,cpp}    closing entries, Saldenvortrag, Bilanz/GuV (A9/A10)
  ui/                   # the German UI, one file per section
Docs/UltraFIBU/CHANGELOG.md   # first line is the version — the only place it lives
Docs/UltraFIBU/README.md
Tests/UltraFIBU/              # ctest, gated by ULTRACANVAS_BUILD_FIBU_TESTS
```

- `cmake/UltraCanvasVersion.cmake` gets one
  `_ultracanvas_declare_product(ULTRAFIBU "Docs/UltraFIBU/CHANGELOG.md")` line;
  the version then exists exactly once, in the changelog's first line, per the
  versioning rules.
- The engine **must build and its tests must run without the UI**, as
  `EmailCleanerEngine` does — that is what makes the format work testable in CI
  on three platforms.
- Soft-fail everywhere an optional dependency is involved (`ERiC`, PDF, OCR):
  `message(STATUS "  [−] … SKIPPED (…)")` and a runtime capability query, never a
  hard failure.
- If the docs should reach `llms.txt`, add `"UltraFIBU"` to `APP_DOC_DIRS` in
  `scripts/generate_llms_txt.py` (`Docs/Research/` is deliberately excluded, so
  *this* document does not change the generated files).

**Testing — this is where an accounting program is won or lost:**

| Test | Why |
|---|---|
| **DATEV round-trip** ✅: export a stack, re-import it, compare the journal | The one test that catches sign/`Soll-Haben`, comma-decimal and `TTMM` mistakes at once. In place as `TestDatevRundlauf`; a deliberately swapped `Soll/Haben` flag in the exporter is reported by it |
| **Golden files** for every generated artefact (DATEV CSV, UStVA XML, XRechnung, OSS CSV, `index.xml`) | Byte-comparison against a reviewed fixture; a diff in CI is the earliest possible warning |
| **Fiscal-calendar property tests**: 1 January, **1 April**, 1 July years, a *Rumpfwirtschaftsjahr*, a leap February | The flexible *Geschäftsjahr* is the requirement most likely to be broken by an innocent change elsewhere |
| **Money property tests**: rounding at .005, 19 % and 7 % splits, discount allocation summing to the total, currency conversion | Half-up rounding and allocation are the arithmetic the tax office will re-do |
| **UStVA mapping per year**: a fixture ledger with an expected *Kennzahl* vector for 2025 and 2026 | Proves a form change (Kz 43) landed as data and did not disturb prior years |
| **Immutability tests**: update after *Festschreibung* refused; *Storno* balances; hash chain verifies and detects a doctored row | GoBD compliance as executable assertions |
| **Concurrency tests** (server mode): two writers racing for one invoice number; the stale-version update refused | §10.3 is only true if it is tested |

**Fixtures and privacy.** This repository is public. Real bookkeeping data —
customer names, IBANs, VAT numbers, amounts — must never be committed. The
user's own DATEV export (§15) is the reference for *writing* the readers, and
the committed fixtures are anonymised or synthetic files derived from it, with a
small generator script so they can be regenerated. Keep the originals outside
the repository.

---

## 13. Phase plan

Two tracks: framework prerequisites (B) and the application (A). B1 and B2 gate
the first real screens. **B4 (the `libpq` driver) runs in parallel from the
start** rather than late, because server mode is wanted from day one (§1.1) —
and it *can* run in parallel precisely because §10.1's rules keep the
application driver-agnostic meanwhile, so the two tracks only meet at A8.

| # | Deliverable | Verifiable when |
|---|---|---|
| **B4** *(parallel from day one)* | `libpq` PostgreSQL driver for UltraDatabase behind `IDatabaseDriverPlugin`: TLS required, credentials via UltraVault, pooling, the Stage 2 shape its README already specifies + `Tests/UltraDatabase` coverage against a real server | The existing UltraDatabase test suite passes unchanged against PostgreSQL as well as SQLite |
| **B1** ✅ | **Sorting and filtering over the existing `UltraCanvasListView`**: `UltraCanvasListSortFilterProxy` (stable sort, comparators, `SortRole`, text/column/predicate filters, source↔proxy mapping), header-click sorting with an indicator, and the catalogue row corrected (§3.3). Inline editing and a footer/aggregate row remain | Done: 78 proxy tests; a column of amounts written `1.234,56` sorts numerically and one of `R-2026070…` document numbers no longer sorts backwards |
| **B2** | `UltraCanvasXML` over the tinyxml2 the framework already links — a facade, not a new dependency | Round-trips a ZUGFeRD CII file and a CAMT.053 statement |
| **B3** | `UltraCanvasMoney` + currency/number input element + German formatting helpers | Property tests in `Tests/` |
| **A1** | Engine skeleton, store + migrations, *Mandant*, **Geschäftsjahr (flexible start)**, chart of accounts (**SKR03 shipped first**, SKR04 beside it, both as data, each account carrying its EÜR line, BWA position **and balance-sheet classification**), *Steuerschlüssel* table, number ranges; master data for **Kunden/Lieferanten with USt-IdNr, address, contact** incl. offline VAT-number checksums; **user table and roles from the first schema** (§10.2), so attribution is unbroken when the server arrives; German UI shell | Create a 1 April fiscal year, import SKR03, enter a supplier, see it in a grid |
| **A2** *(engine ✅, PDF ✅, screens ✅ read-only)* | Documents and the journal: `Beleg`/`BelegPosition`/`Buchung`, posting with the automatic tax split, *Storno*, payments, **Festschreibung** reaching the rows, the hash chain and its verifier, the Summen- und Saldenliste, file attachment with a SHA-256, and nine `ultrafibu` commands. **Still open: the document editor, and multi-page PDF output** | Done for the engine: a month entered, posted, paid, partly reversed, frozen and verified — 126 checks, including three lines of 33,33 EUR at 19 % owing 19,00 and a row edited with plain SQL being caught |
| **A3** *(export ✅ and import ✅ for category 21, 20 ✅; 16 open)* | **DATEV**: `EXTF` Buchungsstapel and Kontenbeschriftungen, one file per calendar month, `WJ-Beginn` from the *Geschäftsjahr*, CP1252/CRLF, unsigned `Umsatz`, and `datev-pruefen` to check the shipped column definition against a real file. **Import reads a stack by the column names the file itself carries**, so it does not depend on that definition; it is all-or-nothing, refuses the same file twice by its hash, and imported rows join the hash chain. **Still open: category 16 (Debitoren/Kreditoren, ~240 columns, not guessed) and the `datev_bu` mapping, which is what a round-trip currently loses** | The user's *Kanzlei* imports a stack without errors — the real acceptance test, which needs their file |
| **A4** *(import ✅ and assignment ✅; learning rules and the reports open)* | Bank: CAMT.053 / MT940 / CSV import, assignment with learning rules; reports: journal, *Summen- und Saldenliste*, **EÜR**, **BWA**, Cashflow, and the UStVA figures on screen. **Import reads all three formats, picks the reader by content, is idempotent per line, and refuses a statement whose balances do not match its entries. Assignment scores candidates and proposes; confirming books through `ZahlungErfassen`. Still open: the per-partner learning rules, EÜR, BWA and Cashflow** | Figures reconcile against the user's existing bookkeeping for one closed month |
| **A5** *(UStVA XML ✅, submission log ✅; ERiC, DFV and ZM open)* | **ELSTER**: UStVA XML for manual upload, ERiC backend behind it, *Dauerfristverlängerung*, **ZM**, submission log with *Transferticket*. **The Kennzahl mapping is data keyed by year, and an amount whose tax key has no *verified* Kennzahl refuses to produce a file rather than under-declaring silently. The return cross-checks the tax it declares against the tax the journal booked.** Still open: the six unverified Kennzahlen (ig-Erwerb, § 13b, Ausfuhr, § 19), the ERiC transport, *Dauerfristverlängerung* and ZM | A test-period submission accepted by ERiC validation |
| **A6** *(ledger ✅, quarterly figures ✅, BOP CSV ✅ unverified layout, §3c monitor ✅, Kz 45 reconciliation ✅; eVatR REST open)* | **OSS/IOSS**: per-country ledger, quarterly figures, BOP CSV, §3c threshold monitor, Kz 45 linkage; BZSt eVatR REST qualified confirmation with stored proof. **The rate charged is what is declared; the shipped EU rates are a check against it and none of them is verified yet, so none is used for comparison. A key with no destination country stops the return. OSS turnover is reconciled against UStVA Kz 45 before either is filed.** Still open: the eVatR REST confirmation, and verifying the BOP column layout against a real export | A quarter's OSS figures matching a hand calculation; a stored confirmation record |
| **A7** | **E-Rechnung**: read XRechnung (UBL+CII) and ZUGFeRD; write XRechnung; DATEV XML document package (§4.2) | A received ZUGFeRD invoice becomes a proposed *Beleg*; an issued XRechnung passes the KoSIT validator |
| **A8** *(as soon as B4 lands)* | **Server mode**: login, role enforcement in the store, optimistic locking with soft edit locks, transactional number allocation, the migration gate, and local↔server database transfer | Two clients on one PostgreSQL database posting concurrently without a duplicate number or a lost update |
| **A9** | GoBD **Z3 export**, *Verfahrensdokumentation* template, backup/restore to UltraCloud, **Anlagenverzeichnis** with depreciation (AfA), *Abschlussbuchungen* and *Saldenvortrag* into the next *Geschäftsjahr* | A Z3 medium that IDEA reads; a restore rehearsal; a closed year carried forward |
| **A10** | **Jahresabschluss**: *Bilanz* and GuV from the balance-sheet classification, then **E-Bilanz** — the XBRL taxonomy submission ERiC validates with its `checkBilanz_<taxonomy>` plugin (§2) | A balance sheet that balances, and an E-Bilanz accepted by ERiC validation for a test period |
| **later** | ZUGFeRD *output* (PDF/A-3 in the PDF writer), FinTS 3.0 fetching (after product registration), OCR-assisted receipt capture | — |

A2's engine, its invoice PDF and its four screens are in. What remains of it is
the document editor - entering an invoice with its positions, which needs a form
rather than a table - and multi-page PDF output in the framework's writer
(§3.2). A1–A3 is the smallest set that replaces a spreadsheet and keeps the *Kanzlei*
happy; A4–A5 is the point at which the application files its own VAT; A6 covers
the OSS obligation; A8 is the multi-user requirement, gated only on B4 running
beside it from the start; A10 is where the *Kanzlei*'s last remaining job comes
in-house, and it is the one phase that should not be started before the ledger
underneath it has survived a full *Geschäftsjahr*.

---

## 14. Legal, licensing and maintenance risks

These are not afterthoughts — two of them can stop the project, so they belong
in the decision.

1. **ERiC may not be redistributed by us.** It is obtained under agreement with
   the tax administration, with a manufacturer registration and a
   *Hersteller-ID*, and the SDK download is password-protected. Consequences:
   nothing from ERiC enters this repository; the user or an installer supplies
   it; the application must run without it (§5.1); and if UltraFIBU is ever
   *distributed to third parties* as tax software, the registration
   obligations are the distributor's.
2. **A permanent maintenance cadence.** ERiC releases roughly twice a year with
   new *Datenartversionen*; the UStVA *Vordruckmuster* changes annually (Kz 43
   in 2026); XRechnung moves to 4.0 with EN 16931-1:2026; ZUGFeRD reached 2.5 in
   May 2026; SKR account frameworks and tax rates change. An accounting program
   is a subscription to legislative change. Budget a December/January release
   every year, and keep every year-dependent rule in data (§5.1, §6.5) so that
   release is a data update and not a refactor.
3. **FinTS needs a product registration** with a 10–15 business-day lead time
   before bank fetching can work at all (§9.2). Start it early or defer the
   feature honestly.
4. **DATEV**: the *frameworks* SKR03/SKR04 are usable; DATEV's printed editions
   are protected (§4.3). Ship account data compiled from openly published lists
   and the user's own export, not scanned from a DATEV publication. The
   *Beraternummer*/*Mandantennummer* must come from the *Kanzlei*.
5. **The software is not tax advice, and correctness is shared.** Every return
   must be shown for review before submission, with the figures traceable to the
   postings behind them, and the submitted XML plus the *Transferticket* stored
   unaltered. That is both good practice and the only defensible position if a
   figure is later disputed.
6. **GDPR against the 10-year retention.** Booked documents are retained;
   unbooked personal data is deletable; the *Löschkonzept* has to say which is
   which. One section in the *Verfahrensdokumentation* (§8.6).
7. **Server mode widens the blast radius.** A shared database means real
   authentication, least-privilege database users, TLS, backups that are tested,
   and no internet exposure without a VPN (§10.5). None of it is exotic; all of
   it is easy to skip and expensive to add after the fact.

---

## 15. What to upload, and what it unblocks

The offer of real DATEV data and exported documents is the most valuable input
this project can get — format specifications are secondary to one real file that
the user's own *Kanzlei* accepts. Concretely, in rough order of value
(anonymised or not — but see the privacy note in §12, and keep originals out of
the repository):

| File | Unblocks |
|---|---|
| `EXTF_Buchungsstapel.csv` — a real export **and**, if possible, a stack the *Kanzlei* sent back | The whole of A3: exact column order for version 700, the `BU-Schlüssel` values actually in use, how the *Kanzlei* names stacks |
| `EXTF_Stammdaten*.csv` (Debitoren/Kreditoren, category 16) and the account-name export (category 20) | Master-data round-trip; the real chart of accounts and *Personenkonten* ranges |
| The DATEV header line of any of those files, with `Beraternummer`/`Mandantennummer` and **`WJ-Beginn`** | Confirms the fiscal-year and `Sachkontenlänge` configuration to write |
| One **ZUGFeRD** or **XRechnung** invoice actually received | A7's reader, against a real document rather than a sample |
| A **CAMT.053** file (and an MT940 if the bank still sends one) | A4's importer and the reference/idempotency keys |
| A **BOP OSS export ZIP** (the two CSV layouts) | A6's transport file — the layout is not published |
| A UStVA XML or PDF *as filed*, plus a *Bescheid*/protocol | A5: the *Kennzahlen* actually used, and the expected values for a golden test |
| A typical outgoing invoice PDF and a typical receipt scan | Invoice layout and the receipt-capture field set |
| The *Kanzlei*'s stated expectations (which categories, which cadence, *Festschreibung* rules) | Scope: how much of the annual accounts UltraFIBU needs to do at all |

---

## 16. Open questions for the user

Three are answered in §1.1 (annual accounts in scope, server mode from day one,
SKR03 + *Soll-Versteuerung*). What is still open — none of it blocking B1–B4 or
A1:

1. **Is the company on *Einnahmen-Überschuss-Rechnung* or on full double-entry
   accounts (*Bilanzierung*)?** The screenshots show EÜR, BWA *and* UStVA, which
   fits EÜR over a ledger; but the decision that UltraFIBU should eventually
   produce the *Jahresabschluss* (§1.1) implies *Bilanzierung* sooner or later,
   and the two differ in what A10 must produce — an EÜR (*Anlage EÜR*) or a
   *Bilanz* with GuV and E-Bilanz. Answering it decides whether A10 is one
   deliverable or two.
2. **Is the chart the standard SKR03 or the *Kanzlei*'s modified version**, and
   can the category-20 export be supplied (§15)?
3. **UStVA rhythm** — monthly or quarterly — and is there a
   *Dauerfristverlängerung*?
4. **Fiscal year**: 1 April – 31 March confirmed; is there a
   *Rumpfwirtschaftsjahr* in the history that must be representable?
5. **OSS**: registered for the EU scheme already, which destination countries,
   and IOSS as well?
6. **Multi-user specifics**, now that server mode is a day-one track: how many
   users, which of the five roles (§10.2) are actually wanted, one *Mandant* or
   several in one installation, and is the database server on a LAN or reached
   over the internet — the latter means a VPN, or the HTTP application server
   noted in §10.5. Also: who administers PostgreSQL, since backups that are
   never restored are not backups (§10.4).
7. **Platforms** — Linux only, or Windows and macOS too? ERiC exists for all
   three, but each needs its own SDK build and its own packaging step.
8. **Foreign currency** beyond EUR: the screenshots show only EUR; if not, the
   conversion and rate-source design in §6.1 grows.
9. **Is UltraFIBU for this company only, or a product?** A product means the
   manufacturer registrations of §14 and a support obligation; for in-house use
   the same code needs neither.

---

## 17. Summary

The requirements land on this repository more comfortably than they might
appear to. Persistence, migrations, CP1252 CSV, ZIP containers, hashing, secret
storage, HTTPS with client certificates, OAuth2, charts, forms, PDF reading and
spreadsheet export are all present and in production use by other applications.
The genuinely missing pieces are few and each is worth having for its own sake:
a real data grid, a facade over the XML parser the build already requires, an
exact money type, and — for the multi-user
requirement, which is wanted from day one — the `libpq` driver that
UltraDatabase's own roadmap already promises, started in parallel with the first
application phase rather than after it.

The hard parts are not the framework. They are ERiC's distribution terms and
release cadence, the absence of any OSS machine interface, PDF/A-3 for ZUGFeRD
output, and the discipline of keeping every year-dependent tax rule in data
rather than in code. The design above answers each of those with a
deliberately modest mechanism — soft-failing backends, generated files handed to
a human, XML before hybrid PDF, tables before branches — because in a program
whose output is filed with a tax authority, the boring answer is the one that is
still right next year.

---

### Sources

- [DATEV-Format — Buchungsstapel (developer portal)](https://developer.datev.de/de/file-format/details/datev-format/format-description/booking-batch) · [Debitoren/Kreditoren](https://developer.datev.de/de/file-format/details/datev-format/format-description/debitorskreditors) · [DATEV XML-Schnittstelle online](https://developer.datev.de/de/file-format/details/datev-xml-interface-online/getting-started-)
- [EXTF_Buchungsstapel.csv example (ledermann/datev)](https://github.com/ledermann/datev/blob/master/examples/EXTF_Buchungsstapel.csv) · [EXTF header explained](https://smartkontoauszug.de/blog/datev-extf-format-erklaert) · [Buchungsstapel format guide](https://dokuwandel.de/ratgeber/datev-buchungsstapel-erklaerung) · [`Belegdatum` TTMM discussion](https://www.datev-community.de/t5/Betriebliches-Rechnungswesen/csv-Buchungsstapel-Import-Belegdatum-nur-4stellig-ttmm-gt/td-p/468371?nobounce) · [Debitoren/Kreditoren field list](https://handbuch.conaktiv.de/wiki/version-15/buchhaltungsmodule/buchhaltung-in-conaktiv/nutzung-der-datev-schnittstelle-2017/datev-debitorenkreditoren-stammdaten-datei-extf-stammdaten-deb-kred-csv/)
- [DATEV Rechnungsdatenservice 1.0 (help 1071255)](https://wissensplattform.apps.datev.de/help/document/1071255) · [DATEV SKR03/SKR04](https://www.datev.de/web/de/berufsgruppenuebergreifend/ratgeber/rechnungswesen/datev-standard-kontenrahmen) · [SKR copyright discussion](https://www.gutefrage.net/frage/ist-der-datev-kontenrahmen-zb-skr03-etc-urheberrechtlich-geschuetzt)
- [ELSTER — Entwickler](https://www.elster.de/eportal/infoseite/entwickler) · [ERiC API-Referenz 43.3.2.0](https://www.instantview.org/data/CyberEnterprise/ERiC/ERiC-API-Referenz.pdf) · [eric-bindings (Rust) API surface](https://docs.rs/eric-bindings/latest/eric_bindings/) · [ECTElster — open-source ERiC integration](https://github.com/Thomas-Mielke-Software/ECTElster) · [ERiC as the mandatory channel](https://www.pikon.com/de/blog/elster-umstellung-auf-eric-sind-sie-schon-bereit-dafuer/) · [ERiC release notes (37.2)](https://rechtlogisch.de/wp-content/uploads/2022/12/2022-11-24-BayLfSt-ERiC-37.2.pdf)
- [UStVA *Kennzahlen* reference](https://hilfe.sevdesk.de/de/articles/9886922-kennzahlen-der-umsatzsteuervoranmeldung) · [BMF Vordruckmuster UStVA 2026](https://www.bundesfinanzministerium.de/Content/DE/Downloads/BMF_Schreiben/Steuerarten/Umsatzsteuer/2025-12-29-vordruckmuster-USt-voranmeldung-2026.pdf?__blob=publicationFile&v=7) · [2026 form changes](https://www.steuerschroeder.de/blog/umsatzsteuer-2026-neue-vordrucke-fuer-voranmeldung-und-dauerfristverlaengerung/)
- [BZSt — OSS electronic filing](https://www.bzst.de/DE/Unternehmen/Umsatzsteuer/One-Stop-Shop_EU/ElektronischeAbgabe/elektronischeabgabe_oss.html) · [BZSt — IOSS](https://www.bzst.de/DE/Unternehmen/Umsatzsteuer/ImportOneStopShop/Elektr_Datenuebermittlung/elektronische_datenuebermittlung.html) · [BOP OSS registration form](https://www.elster.de/bportal/formulare-leistungen/alleformulare/osseureg) · [OSS bookkeeping preparation and Kz 45](https://wissen.buchhaltungsbutler.de/hc/de/articles/11322803594269-Vorbereitung-der-OSS-Meldung-in-der-Buchhaltung) · [quarterly OSS practice](https://help.lexware.de/de-form/articles/5588384-quartalsmeldungen-der-eu-zielland-steuer-beim-bundeszentralamt-fur-steuern)
- [BZSt — ZM by ELSTER](https://www.bzst.de/DE/Unternehmen/Umsatzsteuer/ZusammenfassendeMeldung/ElektronischeAbgabe/Elster/Elster.html)
- [BZSt — eVatR XML-RPC obsolete from 30.11.2025](https://www.bzst.de/DE/Unternehmen/Identifikationsnummern/Umsatzsteuer-Identifikationsnummer/eVatR/eVatR_Info_Schnittstelle/eVatR_info_schnittstelle.html) · [BZSt — eVatR-Anbindung](https://www.bzst.de/DE/Unternehmen/Identifikationsnummern/Umsatzsteuer-Identifikationsnummer/eVatR/eVatR_node.html) · [switch to the REST API](https://www.tso.de/en/news/news/change-to-vat-id-verification-bzst-switches-to-new-rest-api/)
- [XRechnung / ZUGFeRD in Germany](https://www.comarch.de/produkte/datenaustausch-und-dokumentenmanagement/e-invoicing/e-invoicing-in-deutschland/xrechnung-zugferd/) · [ZUGFeRD 2.5 and the timeline](https://www.truecommerce.com/de/blog/neue-informationen-und-zeitplan-zur-einfuehrung-der-e-rechnung-in-deutschland/) · [deadlines 2025/2027/2028](https://www.e-rechnungen.org/e-rechnung-pflicht-fristen) · [2027 issuing obligation](https://rickert.law/e-rechnung-b2b-2027/)
- [GoBD *Beschreibungsstandard* für die Datenträgerüberlassung](https://www.caseware.net/fileadmin/audicon/media/allgemeine_bilder/Beschreibungsstandard-GDPdU-01-03-2019.pdf) · [what a Z3 medium must contain](https://www.caseware.net/loesungen/tax-compliance/gobd/z3-datenzugriff-wie-kann-ich-meine-daten-im-beschreibungsstandard-exportieren/)
- [FinTS product registration](https://www.fints.org/de/hersteller/produktregistrierung) · [FinTS registration FAQ](https://www.fints.org/de/hersteller/faq-produktregistrierung) · [MT940 → CAMT.053 changeover](https://support.immoware24.de/hc/de/articles/30222978488477-Umstellung-auf-CAMT-V8-Abl%C3%B6sung-von-SWIFT-MT940-MT942-bis-November-2025) · [CAMT.053 vs MT940](https://smartkontoauszug.de/blog/camt-053-vs-mt940)
