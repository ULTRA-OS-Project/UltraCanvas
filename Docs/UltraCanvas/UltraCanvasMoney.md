# UltraCanvasMoney

**Exact monetary amounts: an integer count of minor units plus an ISO 4217
code.** Header-only (`UltraCanvas/include/UltraCanvasMoney.h`), free of every
other UltraCanvas header, so a headless engine, a test target and the UI can all
use it. Tests: `Tests/MoneyTests.cpp` (target `MoneyTests`).

```cpp
#include "UltraCanvasMoney.h"
using UltraCanvas::Money;

Money net = Money::FromMinor(3728);           // 37,28 EUR
Money tax = net.TaxOnNet(190);                // 19 % -> 7,08 EUR
Money gross = net.GrossFromNet(190);          // 44,36 EUR — and net + tax == gross, exactly

std::string label = gross.ToStringWithSymbol();          // "44,36 €"
std::string csv   = gross.ToString(UltraCanvas::MoneyStyle::Datev);  // "44,36"
std::string json  = gross.ToString(UltraCanvas::MoneyStyle::Plain);  // "44.36"
```

## Why not a double

`double` cannot hold 0,01 exactly, so a ledger built on it drifts: totals that
disagree with the sum of their rows, a VAT split that is a cent off, a
reconciliation that never closes. `Money` stores `int64_t` minor units — cents
for EUR, whole yen for JPY, millimes for TND — and never converts to floating
point, not even while parsing or formatting. `CurrencyValue` in
`UltraCanvasSpreadsheetTypes.h` *is* a `double`; it is a spreadsheet cell value
and must not be used for accounting.

The range at two decimals is ±92 000 000 000 000 000,00 — every overflow past
that is reported, never wrapped.

## Construction

| Call | Meaning |
|---|---|
| `Money()` | zero, currency unset — a *neutral* zero that adds to any currency |
| `Money::FromMinor(3728)` | 37,28 EUR |
| `Money::FromMajor(1234)` | 1.234,00 EUR |
| `Money::FromMinor(1234, "JPY")` | ¥1.234 (a zero-decimal currency) |
| `Money::Zero("USD")`, `Money::Invalid()` | zero / the invalid value |

`MoneyDecimals(code)` gives a currency's scale: 2 by default, 0 for JPY, KRW,
HUF and the rest of the zero-decimal list, 3 for the dinar-class currencies.

## Validity instead of exceptions

An amount is **invalid** when it came out of a failed parse, a currency
mismatch or an overflow, and invalidity is **sticky** — it propagates through
arithmetic, so one `Valid()` check at the end of a calculation is enough. An
invalid amount formats as an **empty string**, never as `0,00`, so it cannot be
mistaken for zero in a report.

```cpp
Money total = eur + usd;      // invalid: a mismatch is a bug, not a conversion
total += eur;                 // still invalid
if (!total.Valid()) { /* report it */ }
```

Comparison compares like with like: across currencies an amount is not less,
not greater and not equal.

## Arithmetic

| Call | Meaning |
|---|---|
| `a + b`, `a - b`, `-a`, `a += b` | same-currency addition and subtraction |
| `a.Times(7)` | quantity: seven of `a` |
| `a.ScaledBy(10967, 10000)` | an exact rate as a fraction (here 1,0967) |
| `a.Permille(25)` | 2,5 % of `a` — permille, because German VAT keys and Skonto rates need one decimal |
| `MoneySum(list)` | the sum of a list, invalid if any element is |

Every multiplication and division goes through `MoneyMulDiv`, which forms the
full 128-bit product in 32-bit limbs and divides it with **kaufmännische
Rundung** (half away from zero: 2,5 → 3 and −2,5 → −3) — the rounding German tax
arithmetic uses. No compiler intrinsic and no `__int128` is needed, so it
behaves identically on every platform.

## German VAT

```cpp
net.TaxOnNet(190)        // tax on a net amount:      net * 190/1000
net.GrossFromNet(190)    // gross from net:           net * 1190/1000
gross.TaxInGross(190)    // tax contained in a gross: gross * 190/1190
gross.NetFromGross(190)  // net from gross:           gross - TaxInGross(190)
```

`permille` is thousandths: 190 is 19 %, 70 is 7 %, 0 is 0 %, 25 is 2,5 %.

Two identities hold for every rate and amount, and the tests assert them across
a matrix of both: **`net + TaxOnNet == GrossFromNet`**, and
**`NetFromGross + TaxInGross == gross`**. `NetFromGross` is deliberately
defined as *gross minus the contained tax* rather than as its own division, so
the second identity survives whatever the rounding did.

## Splitting so the parts sum to the whole

```cpp
auto thirds = Money::FromMinor(10000).SplitEvenly(3);   // 33,34 / 33,33 / 33,33
auto share  = Money::FromMinor(1000).SplitProportionally({ 7000, 3000 });  // 7,00 / 3,00
```

Largest-remainder distribution: the truncated shares go out first, then the
leftover minor units go to the largest remainders. **The parts always sum back
to the original amount** — that is the property that stops a discount spread
over invoice positions from losing a cent, and it is tested over a matrix of
amounts (including negative ones, for credit notes) and weight sets. Degenerate
input (no weights, a negative weight, zero parts, an invalid amount) yields an
empty vector rather than a wrong distribution.

## Text: three styles, no locale

```cpp
enum class MoneyStyle { German, Plain, Datev };
```

| Style | Writes | For |
|---|---|---|
| `German` | `1.234,56` | UI labels, printed documents |
| `Plain` | `1234.56` | dot-decimal file formats, JSON, SQL text |
| `Datev` | `1234,56` | DATEV CSV amount columns (comma decimal, no grouping) |

**The style is chosen by the destination, never inherited from the process
locale.** Digits are assembled from the integer value — no stream, no `printf`,
no `std::to_string(double)` — so `LC_NUMERIC` cannot reach them. That matters in
this framework specifically: the Linux backend calls `setlocale(LC_ALL, "")` for
XIM, which has twice turned dot-decimal file output into comma-decimal output
(see the numbers rule in [AGENTS.md](../../AGENTS.md)). `Money` cannot have that
bug, in either direction.

`ToStringWithSymbol()` appends `€`, `$`, `£` or the ISO code.

### Parsing

```cpp
Money m;
Money::TryParse("1.234,56", m);                              // 123456 minor
Money::TryParse("1234.56", m);                               // 123456 — a lone dot two digits from the end is decimal
Money::TryParse("1.234", m);                                 // 123400 — three digits behind it, so grouping
Money::TryParse("(37,28)", m);                               // -3728  — parentheses are negative
Money::TryParse("652,57 €", m);                              // 65257  — symbols and ISO codes are ignored
Money::TryParse("1,005", m);                                 // 101    — finer input rounds half away from zero
Money::TryParse("1234.56", m, "EUR", MoneyStyle::Datev);     // false  — malformed for that style
```

Which separator is the decimal one:

- **both present** → the rightmost one;
- **one present** → `German` reads `,` as decimal and a lone `.` as decimal too
  *unless* it stands exactly three digits from the end (so `1.234` is 1234);
  `Plain` reads `.` as decimal, `Datev` reads `,` as decimal;
- **the machine styles are strict**: in `Plain` and `Datev`, a grouping
  separator standing anywhere but three digits from the end means the field is
  malformed and the parse fails. An importer that misreads an amount does more
  damage than one that rejects a line.

Format-then-parse is the identity in every style; the tests assert it over a
range of values.

## Where this is used

`Apps/UltraFIBU` (the German accounting application — see
[`Docs/Research/UltraFIBUDesignProposal.md`](../Research/UltraFIBUDesignProposal.md))
uses it for every amount it stores, computes, prints, exports to DATEV and
reports to the tax authority. Anything else that holds a price, a total or a
balance should use it too.
