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
| `data/` | The chart of accounts and the tax keys, as data files |
| `ui/` | The German UI (empty until the data grid exists) |
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
ultrafibu festschreiben buch.db 30.06.2026 --ja
ultrafibu protokoll buch.db     # who did what, when (GoBD)
```

`ULTRAFIBU_DATA_DIR` points at the data files when they are not beside the
binary.

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
  schema and not from the day the server arrives.
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
