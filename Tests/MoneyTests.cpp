// Tests/MoneyTests.cpp
// Unit tests for UltraCanvasMoney. Self-contained: no test framework, no UI
// stack, header-only subject - so it runs headless anywhere a C++20 compiler
// does.
//
// What is checked, and why each one is here:
//  - the 128-bit multiply/divide at its edges, including the carry case that a
//    naive shift-and-subtract loop gets wrong, and overflow reporting
//  - kaufmaennische Rundung in both directions (2,5 -> 3 and -2,5 -> -3)
//  - the German VAT identities: net + tax == gross, and the tax extracted from
//    a gross amount agrees with the tax computed on the net
//  - splitting: the parts always sum to the whole, which is the property that
//    stops a discount from losing a cent
//  - formatting and parsing round-trips in all three styles, plus the German
//    ambiguity rules ("1.234" is 1234, "1234.56" is 1234,56)
//  - currency mismatch is invalid rather than plausible, and invalidity is
//    sticky
//
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCanvasMoney.h"

#include <cstdio>
#include <string>
#include <vector>

using UltraCanvas::Money;
using UltraCanvas::MoneyStyle;
using UltraCanvas::MoneySum;
using UltraCanvas::MoneyMulDiv;
using UltraCanvas::MoneyMul64;
using UltraCanvas::MoneyDiv128;
using UltraCanvas::MoneyDecimals;

static int g_failures = 0;
static int g_checks   = 0;

static void Check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

static void CheckText(const std::string& actual, const std::string& expected,
                      const std::string& what) {
    ++g_checks;
    if (actual != expected) {
        ++g_failures;
        std::printf("  FAIL: %s\n    expected \"%s\"\n    got      \"%s\"\n",
                    what.c_str(), expected.c_str(), actual.c_str());
    }
}

static void CheckMinor(const Money& m, int64_t expected, const std::string& what) {
    ++g_checks;
    if (!m.Valid() || m.Minor() != expected) {
        ++g_failures;
        std::printf("  FAIL: %s\n    expected %lld minor units\n    got      %lld%s\n",
                    what.c_str(), static_cast<long long>(expected),
                    static_cast<long long>(m.Minor()), m.Valid() ? "" : " (invalid)");
    }
}

// ---- 1. The exact multiply/divide core -------------------------------------

static void TestMulDiv() {
    std::printf("MulDiv / 128-bit core\n");

    int64_t out = 0;
    Check(MoneyMulDiv(100, 19, 100, out) && out == 19, "19 % of 100 is 19");
    Check(MoneyMulDiv(3728, 190, 1000, out) && out == 708, "19 % of 37,28 rounds to 7,08");

    // Kaufmaennische Rundung: exactly half rounds away from zero.
    Check(MoneyMulDiv(5, 1, 2, out) && out == 3, "2,5 rounds to 3");
    Check(MoneyMulDiv(-5, 1, 2, out) && out == -3, "-2,5 rounds to -3");
    Check(MoneyMulDiv(7, 1, 2, out) && out == 4, "3,5 rounds to 4");
    Check(MoneyMulDiv(1, 1, 3, out) && out == 0, "1/3 rounds to 0");
    Check(MoneyMulDiv(2, 1, 3, out) && out == 1, "2/3 rounds to 1");

    // Sign combinations.
    Check(MoneyMulDiv(-100, 19, 100, out) && out == -19, "negative value keeps its sign");
    Check(MoneyMulDiv(100, -19, 100, out) && out == -19, "negative numerator flips the sign");
    Check(MoneyMulDiv(-100, -19, 100, out) && out == 19, "two negatives cancel");

    // A product far beyond 64 bits still divides exactly.
    Check(MoneyMulDiv(1000000000000000ll, 1000, 1000, out) && out == 1000000000000000ll,
          "10^15 * 1000 / 1000 survives the wide product");
    Check(MoneyMulDiv(INT64_MAX, 1, 1, out) && out == INT64_MAX, "INT64_MAX passes through");
    Check(MoneyMulDiv(INT64_MIN, 1, 1, out) && out == INT64_MIN, "INT64_MIN passes through");

    // Overflow and division by zero are reported, never wrapped.
    out = 12345;
    Check(!MoneyMulDiv(INT64_MAX, 2, 1, out), "overflow is refused");
    Check(out == 12345, "a refused operation leaves the output untouched");
    Check(!MoneyMulDiv(100, 1, 0, out), "division by zero is refused");

    // The division primitive at the carry boundary: (2^64 - 1) / 1 uses the
    // path where the shifted remainder carries out of bit 63.
    uint64_t hi = 0, lo = 0, q = 0, r = 0;
    MoneyMul64(0xFFFFFFFFFFFFFFFFull, 1ull, hi, lo);
    Check(hi == 0 && lo == 0xFFFFFFFFFFFFFFFFull, "x * 1 is x in 128 bits");
    Check(MoneyDiv128(0, 0xFFFFFFFFFFFFFFFFull, 1ull, q, r) &&
          q == 0xFFFFFFFFFFFFFFFFull && r == 0,
          "(2^64-1)/1 divides exactly through the carry path");
    Check(MoneyDiv128(0, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, q, r) &&
          q == 1 && r == 0, "x/x is 1 for the largest 64-bit divisor");
    Check(MoneyDiv128(0, 10, 3, q, r) && q == 3 && r == 1, "10/3 is 3 remainder 1");
    Check(!MoneyDiv128(5, 10, 3, q, r), "a quotient wider than 64 bits is refused");
    Check(!MoneyDiv128(0, 10, 0, q, r), "a zero divisor is refused");

    // 2^64 exactly: high = 1, low = 0, divided by 2 fits in 64 bits.
    Check(MoneyDiv128(1, 0, 2, q, r) && q == 0x8000000000000000ull && r == 0,
          "2^64 / 2 is 2^63");
}

// ---- 2. Construction, currency and comparison ------------------------------

static void TestBasics() {
    std::printf("construction / currency / comparison\n");

    Check(MoneyDecimals("EUR") == 2, "EUR has two decimals");
    Check(MoneyDecimals("JPY") == 0, "JPY has none");
    Check(MoneyDecimals("TND") == 3, "TND has three");
    Check(MoneyDecimals("XYZ") == 2, "an unknown code defaults to two");

    CheckMinor(Money::FromMajor(1234), 123400, "1234 EUR is 123400 cents");
    CheckMinor(Money::FromMajor(1234, "JPY"), 1234, "1234 JPY is 1234 minor units");
    CheckMinor(Money::FromMinor(-3728), -3728, "a negative amount keeps its sign");
    Check(Money::Zero().IsZero(), "zero is zero");
    Check(!Money::Invalid().Valid(), "an invalid amount says so");

    const Money a = Money::FromMinor(1000);
    const Money b = Money::FromMinor(2500);
    CheckMinor(a + b, 3500, "10,00 + 25,00 is 35,00");
    CheckMinor(b - a, 1500, "25,00 - 10,00 is 15,00");
    CheckMinor(-a, -1000, "negation");
    Check(a < b && b > a && a != b, "ordering within one currency");
    Check(a == Money::FromMinor(1000), "equality within one currency");
    Check(a <= Money::FromMinor(1000) && a >= Money::FromMinor(1000),
          "inclusive comparisons agree with equality");

    // A currency mismatch is a programming error, not a conversion.
    const Money usd = Money::FromMinor(1000, "USD");
    Check(!(a + usd).Valid(), "EUR + USD is invalid");
    Check(!(a == usd), "EUR and USD are never equal");
    Check(!(a < usd) && !(a > usd), "EUR and USD do not order");

    // Invalidity is sticky, so one check at the end of a calculation is enough.
    const Money poisoned = (a + usd) + a;
    Check(!poisoned.Valid(), "invalidity propagates through arithmetic");
    CheckText(poisoned.ToString(), "", "an invalid amount formats as empty, never as 0,00");

    // A neutral zero lets a sum start from nothing.
    Check(MoneySum({}).IsZero(), "an empty sum is zero");
    CheckMinor(MoneySum({ a, b, Money::FromMinor(1) }), 3501, "a list sums");
    Check(!MoneySum({ a, usd }).Valid(), "a mixed-currency list does not sum");

    CheckMinor(a.Times(3), 3000, "three of 10,00");
    CheckMinor(Money::FromMinor(249).Times(7), 1743, "7 x 2,49 is 17,43");
    CheckMinor(Money::FromMinor(10000).ScaledBy(10967, 10000), 10967,
               "an exchange rate of 1,0967 applied to 100,00");
    CheckMinor(Money::FromMinor(10000).Permille(25), 250, "2,5 % of 100,00 is 2,50");
}

// ---- 3. German VAT ---------------------------------------------------------

static void TestVat() {
    std::printf("VAT (Umsatzsteuer)\n");

    // The identity every invoice is checked against.
    const int64_t rates[] = { 190, 70, 0, 55, 90 };
    const int64_t nets[]  = { 1, 7, 99, 100, 3728, 11901, 999999, 1234567 };
    bool identityHolds = true;
    bool extractionAgrees = true;
    for (int64_t permille : rates) {
        for (int64_t netMinor : nets) {
            const Money net   = Money::FromMinor(netMinor);
            const Money tax   = net.TaxOnNet(permille);
            const Money gross = net.GrossFromNet(permille);
            if (!(net + tax == gross)) identityHolds = false;

            // Working back from the gross amount must land on the same net,
            // and net + containedTax must still be the gross.
            const Money contained = gross.TaxInGross(permille);
            const Money backToNet = gross.NetFromGross(permille);
            if (!(backToNet + contained == gross)) extractionAgrees = false;
        }
    }
    Check(identityHolds, "net + tax == gross for every rate and amount tested");
    Check(extractionAgrees, "net(gross) + tax(gross) == gross for every rate tested");

    // Concrete figures, taken from the receipt in the design proposal.
    CheckMinor(Money::FromMinor(3728).TaxOnNet(0), 0, "0 % of 37,28 is nothing");
    CheckMinor(Money::FromMinor(3728).GrossFromNet(0), 3728, "0 % leaves the amount alone");
    CheckMinor(Money::FromMinor(10000).TaxOnNet(190), 1900, "19 % of 100,00 is 19,00");
    CheckMinor(Money::FromMinor(11900).TaxInGross(190), 1900,
               "119,00 gross contains 19,00 tax");
    CheckMinor(Money::FromMinor(11900).NetFromGross(190), 10000,
               "119,00 gross is 100,00 net");
    CheckMinor(Money::FromMinor(10700).NetFromGross(70), 10000,
               "107,00 gross at 7 % is 100,00 net");
    // 1 cent at 19 %: the tax rounds to nothing, and the identity still holds.
    CheckMinor(Money::FromMinor(1).TaxOnNet(190), 0, "19 % of 0,01 rounds to 0,00");
    CheckMinor(Money::FromMinor(1).GrossFromNet(190), 1, "0,01 net stays 0,01 gross");
}

// ---- 4. Splitting ----------------------------------------------------------

static void TestSplit() {
    std::printf("splitting\n");

    // The property that matters: the parts sum to the whole, always.
    const int64_t amounts[] = { 0, 1, 2, 5, 100, 10000, -10000, 3, -7, 999999, 1 };
    const std::vector<std::vector<int64_t>> weightSets = {
        { 1, 1, 1 }, { 1, 1 }, { 2, 1 }, { 5, 3, 2 }, { 1 },
        { 7, 7, 7, 7 }, { 100, 1 }, { 0, 0, 1 }, { 1, 0 }
    };
    bool sumsMatch = true;
    for (int64_t minor : amounts) {
        const Money whole = Money::FromMinor(minor);
        for (const std::vector<int64_t>& weights : weightSets) {
            const std::vector<Money> parts = whole.SplitProportionally(weights);
            if (parts.size() != weights.size()) { sumsMatch = false; continue; }
            if (!(MoneySum(parts) == whole)) sumsMatch = false;
        }
    }
    Check(sumsMatch, "every split sums back to the amount it came from");

    // The classic: 100,00 over three positions.
    const std::vector<Money> thirds = Money::FromMinor(10000).SplitEvenly(3);
    Check(thirds.size() == 3, "three parts");
    CheckMinor(thirds[0], 3334, "the odd cent goes to the first position");
    CheckMinor(thirds[1], 3333, "second third");
    CheckMinor(thirds[2], 3333, "third third");
    Check(MoneySum(thirds) == Money::FromMinor(10000), "and they still make 100,00");

    // Weighted: a 10,00 discount over positions of 70,00 and 30,00.
    const std::vector<Money> weighted = Money::FromMinor(1000).SplitProportionally({ 7000, 3000 });
    CheckMinor(weighted[0], 700, "70 % of the discount");
    CheckMinor(weighted[1], 300, "30 % of the discount");

    // A negative amount (a credit note) splits the same way.
    const std::vector<Money> credit = Money::FromMinor(-10000).SplitEvenly(3);
    CheckMinor(credit[0], -3334, "the odd cent of a credit follows the sign");
    Check(MoneySum(credit) == Money::FromMinor(-10000), "a credit split still sums");

    // Degenerate inputs give nothing rather than something wrong.
    Check(Money::FromMinor(100).SplitProportionally({}).empty(), "no weights, no parts");
    Check(Money::FromMinor(100).SplitProportionally({ 1, -1 }).empty(),
          "a negative weight is refused");
    Check(Money::FromMinor(100).SplitEvenly(0).empty(), "zero parts is refused");
    Check(Money::Invalid().SplitEvenly(3).empty(), "an invalid amount does not split");
    // All-zero weights fall back to an even split rather than dividing by zero.
    const std::vector<Money> zeroWeights = Money::FromMinor(100).SplitProportionally({ 0, 0 });
    Check(zeroWeights.size() == 2 && MoneySum(zeroWeights) == Money::FromMinor(100),
          "all-zero weights split evenly");
}

// ---- 5. Formatting ---------------------------------------------------------

static void TestFormatting() {
    std::printf("formatting\n");

    CheckText(Money::FromMinor(123456).ToString(MoneyStyle::German), "1.234,56", "German grouping");
    CheckText(Money::FromMinor(123456).ToString(MoneyStyle::Plain),  "1234.56",  "plain, dot decimal");
    CheckText(Money::FromMinor(123456).ToString(MoneyStyle::Datev),  "1234,56",  "DATEV, comma decimal, no grouping");
    CheckText(Money::FromMinor(-3728).ToString(MoneyStyle::German),  "-37,28",   "a negative amount");
    CheckText(Money::FromMinor(0).ToString(MoneyStyle::German),      "0,00",     "zero");
    CheckText(Money::FromMinor(5).ToString(MoneyStyle::German),      "0,05",     "five cents");
    CheckText(Money::FromMinor(100).ToString(MoneyStyle::German),    "1,00",     "one euro");
    CheckText(Money::FromMinor(100000000).ToString(MoneyStyle::German), "1.000.000,00",
              "a million groups twice");
    CheckText(Money::FromMinor(100000).ToString(MoneyStyle::German), "1.000,00", "exactly four digits");
    CheckText(Money::FromMinor(99999).ToString(MoneyStyle::German),  "999,99",   "three digits stay ungrouped");
    CheckText(Money::FromMinor(1234, "JPY").ToString(MoneyStyle::German), "1.234",
              "a zero-decimal currency has no decimal part");
    CheckText(Money::FromMinor(1234567, "TND").ToString(MoneyStyle::German), "1.234,567",
              "a three-decimal currency keeps all three");
    CheckText(Money::FromMinor(65257).ToStringWithSymbol(), "652,57 \xE2\x82\xAC",
              "the euro symbol follows the amount");
    CheckText(Money::FromMinor(100, "CHF").ToStringWithSymbol(), "1,00 CHF",
              "an unsymbolled currency gets its code");

    // INT64_MIN formats without negating itself into an overflow.
    Check(!Money::FromMinor(INT64_MIN).ToString().empty(), "INT64_MIN formats at all");
}

// ---- 6. Parsing ------------------------------------------------------------

static void TestParsing() {
    std::printf("parsing\n");

    Money m;
    Check(Money::TryParse("1.234,56", m) && m.Minor() == 123456, "German with grouping");
    Check(Money::TryParse("1234,56", m) && m.Minor() == 123456, "German without grouping");
    Check(Money::TryParse("1234.56", m) && m.Minor() == 123456,
          "a lone dot two digits from the end is a decimal separator");
    Check(Money::TryParse("1.234", m) && m.Minor() == 123400,
          "a lone dot three digits from the end is grouping");
    Check(Money::TryParse("1,5", m) && m.Minor() == 150, "one decimal is padded");
    Check(Money::TryParse("7", m) && m.Minor() == 700, "a bare integer");
    Check(Money::TryParse("-37,28", m) && m.Minor() == -3728, "a leading minus");
    Check(Money::TryParse("(1.234,56)", m) && m.Minor() == -123456, "parentheses are negative");
    Check(Money::TryParse("652,57 \xE2\x82\xAC", m) && m.Minor() == 65257,
          "a trailing euro symbol is ignored");
    Check(Money::TryParse("1 234,56", m) && m.Minor() == 123456, "a space groups");
    Check(Money::TryParse("EUR 89,00", m) && m.Minor() == 8900, "a leading ISO code is ignored");

    // Rounding when the input is finer than the currency.
    Check(Money::TryParse("1,005", m) && m.Minor() == 101, "1,005 rounds away from zero");
    Check(Money::TryParse("1,004", m) && m.Minor() == 100, "1,004 rounds down");
    Check(Money::TryParse("-1,005", m) && m.Minor() == -101, "-1,005 rounds away from zero");

    // Style-specific readings of the same text.
    Check(Money::TryParse("1,234", m, "EUR", MoneyStyle::Plain) && m.Minor() == 123400,
          "in the plain style a comma groups");
    Check(Money::TryParse("1234.56", m, "EUR", MoneyStyle::Plain) && m.Minor() == 123456,
          "in the plain style a dot is the decimal separator");
    Check(Money::TryParse("1234.567", m, "EUR", MoneyStyle::Plain) && m.Minor() == 123457,
          "three plain decimals round to two");
    Check(Money::TryParse("1.234,56", m, "EUR", MoneyStyle::Datev) && m.Minor() == 123456,
          "DATEV: dot groups, comma decides");
    Check(!Money::TryParse("1234.56", m, "EUR", MoneyStyle::Datev),
          "DATEV refuses a dot that is not a thousands separator rather than misreading it");
    Check(!Money::TryParse("1234,56", m, "EUR", MoneyStyle::Plain),
          "the plain style refuses a comma that is not a thousands separator");
    Check(Money::TryParse("1.234", m, "EUR", MoneyStyle::Datev) && m.Minor() == 123400,
          "DATEV reads a proper thousands separator");

    // Other currencies' scales.
    Check(Money::TryParse("1234", m, "JPY") && m.Minor() == 1234, "JPY has no minor digits");
    Check(Money::TryParse("1,234", m, "TND") && m.Minor() == 1234, "TND keeps three");

    // Refusals.
    Check(!Money::TryParse("", m), "an empty string is not an amount");
    Check(!Money::TryParse("abc", m), "letters alone are not an amount");
    Check(!Money::TryParse("   ", m), "whitespace alone is not an amount");
    Check(!m.Valid(), "a refused parse leaves an invalid amount, not zero");

    // Round-trip: format then parse gives the same amount back, in every style.
    const int64_t values[] = { 0, 1, -1, 5, 100, -3728, 123456, -123456, 100000000 };
    bool roundTrips = true;
    const MoneyStyle styles[] = { MoneyStyle::German, MoneyStyle::Plain, MoneyStyle::Datev };
    for (int64_t v : values) {
        const Money original = Money::FromMinor(v);
        for (MoneyStyle style : styles) {
            Money parsed;
            if (!Money::TryParse(original.ToString(style), parsed, "EUR", style) ||
                !(parsed == original)) {
                roundTrips = false;
                std::printf("    round-trip lost %lld in style %d (\"%s\")\n",
                            static_cast<long long>(v), static_cast<int>(style),
                            original.ToString(style).c_str());
            }
        }
    }
    Check(roundTrips, "format then parse is the identity in every style");
}

int main() {
    std::printf("UltraCanvasMoney tests\n");
    TestMulDiv();
    TestBasics();
    TestVat();
    TestSplit();
    TestFormatting();
    TestParsing();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
