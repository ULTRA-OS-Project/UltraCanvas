// UltraCanvas/include/UltraCanvasMoney.h
// Exact monetary amounts: an integer count of minor units (cents) plus an
// ISO 4217 code. No binary floating point is involved anywhere - not in
// storage, not in arithmetic, not in parsing, not in formatting - because a
// ledger, an invoice total and a VAT split have to be reproducible to the
// last cent, and 0.1 + 0.2 is not 0.3 in a double.
//
// Three properties are worth knowing before using this:
//
//  1. **Rates are applied exactly.** Every multiplication/division goes
//     through MoneyMulDiv, which forms the full 128-bit product in 32-bit
//     limbs and divides it with kaufmaennische Rundung (half away from zero) -
//     the rounding German tax arithmetic uses. Overflow is reported, never
//     wrapped.
//  2. **Parsing and formatting are locale-free by construction.** Digits are
//     assembled from integers, so LC_NUMERIC cannot reach them. That matters
//     here: the Linux backend calls setlocale(LC_ALL, "") for XIM, which has
//     twice turned dot-decimal file formats into comma-decimal ones. German UI
//     text (1.234,56), dot-decimal file formats (1234.56) and DATEV CSV's
//     comma-decimal amounts (1234,56) are three explicit styles, never a
//     locale side effect.
//  3. **A split always sums to the whole.** SplitProportionally distributes by
//     largest remainder, so discounting 100,00 EUR over three positions gives
//     33,34 / 33,33 / 33,33 and never 99,99.
//
// Header-only and free of every other UltraCanvas header, like
// UltraCanvasCSVImport.h - so a headless engine, a test target and the UI can
// all use it without dragging in the rendering stack.
// Version: 1.0.0
// Last Modified: 2026-09-18
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace UltraCanvas {

// ===== CURRENCY SCALE =====
// Minor units per major unit, as an exponent: EUR/USD 2, JPY 0, the three
// dinar-class currencies 3. Anything unknown is treated as 2 decimals, which
// is right for every currency this framework is likely to meet and wrong in a
// way the caller can override by asking for the code explicitly.
inline int MoneyDecimals(const std::string& currency) {
    if (currency.size() != 3) return 2;
    // Zero-decimal currencies (ISO 4217 exponent 0).
    static const char* kZero[] = { "JPY", "KRW", "VND", "CLP", "ISK", "HUF",
                                   "TWD", "XAF", "XOF", "XPF", "RWF", "UGX",
                                   "DJF", "GNF", "KMF", "PYG", "VUV", nullptr };
    // Three-decimal currencies (ISO 4217 exponent 3).
    static const char* kThree[] = { "BHD", "IQD", "JOD", "KWD", "LYD", "OMR",
                                    "TND", nullptr };
    for (const char** c = kZero;  *c; ++c) if (currency == *c) return 0;
    for (const char** c = kThree; *c; ++c) if (currency == *c) return 3;
    return 2;
}

// 10^exp for exp in [0, 18]; 1 outside that range.
inline int64_t MoneyPowerOfTen(int exp) {
    if (exp <= 0 || exp > 18) return 1;
    int64_t v = 1;
    for (int i = 0; i < exp; ++i) v *= 10;
    return v;
}

// ===== EXACT MULTIPLY / DIVIDE =====
// The 128-bit product of two unsigned 64-bit values, as (high, low). Built
// from 32-bit limbs so it is correct on every platform and needs neither
// __int128 nor a compiler intrinsic.
inline void MoneyMul64(uint64_t a, uint64_t b, uint64_t& high, uint64_t& low) {
    const uint64_t aLo = a & 0xFFFFFFFFull, aHi = a >> 32;
    const uint64_t bLo = b & 0xFFFFFFFFull, bHi = b >> 32;

    const uint64_t p0 = aLo * bLo;
    const uint64_t p1 = aLo * bHi;
    const uint64_t p2 = aHi * bLo;
    const uint64_t p3 = aHi * bHi;

    const uint64_t middle = (p0 >> 32) + (p1 & 0xFFFFFFFFull) + (p2 & 0xFFFFFFFFull);
    low  = (p0 & 0xFFFFFFFFull) | (middle << 32);
    high = p3 + (p1 >> 32) + (p2 >> 32) + (middle >> 32);
}

// Divide the 128-bit value (high, low) by `divisor`, returning the quotient
// and remainder. Reports false when the quotient does not fit in 64 bits or
// the divisor is zero. Bitwise long division: 128 iterations, exact, and fast
// enough for money (this is not an inner rendering loop).
inline bool MoneyDiv128(uint64_t high, uint64_t low, uint64_t divisor,
                        uint64_t& quotient, uint64_t& remainder) {
    if (divisor == 0) return false;
    if (high >= divisor) return false;          // quotient would exceed 64 bits

    // Invariant: rem < divisor at the top of every iteration. When the shift
    // carries a bit out of the top, the true remainder is 2^64 + rem, which is
    // certainly >= divisor, so one subtraction restores the invariant and the
    // unsigned wrap computes 2^64 + rem - divisor exactly. That case is why
    // the carry is tested separately instead of relying on `rem >= divisor`.
    uint64_t rem = high;
    uint64_t quot = 0;
    for (int bit = 63; bit >= 0; --bit) {
        const uint64_t nextBit = (low >> bit) & 1ull;
        const bool carry = (rem >> 63) != 0;
        rem = (rem << 1) | nextBit;
        quot <<= 1;
        if (carry || rem >= divisor) { rem -= divisor; quot |= 1ull; }
    }
    quotient  = quot;
    remainder = rem;
    return true;
}

// value * numerator / denominator, truncated toward zero, with the remainder
// of the division reported as a non-negative magnitude. Returns false on
// overflow or a zero denominator.
inline bool MoneyMulDivTrunc(int64_t value, int64_t numerator, int64_t denominator,
                             int64_t& outQuotient, uint64_t& outRemainder) {
    if (denominator == 0) return false;

    // Work on magnitudes; INT64_MIN has no positive counterpart, so widen it
    // by hand rather than negating it.
    auto magnitude = [](int64_t v) -> uint64_t {
        return v < 0 ? (~static_cast<uint64_t>(v) + 1ull) : static_cast<uint64_t>(v);
    };
    const uint64_t vMag = magnitude(value);
    const uint64_t nMag = magnitude(numerator);
    const uint64_t dMag = magnitude(denominator);

    int sign = 1;
    if (value < 0)       sign = -sign;
    if (numerator < 0)   sign = -sign;
    if (denominator < 0) sign = -sign;

    uint64_t high = 0, low = 0;
    MoneyMul64(vMag, nMag, high, low);

    uint64_t quot = 0, rem = 0;
    if (!MoneyDiv128(high, low, dMag, quot, rem)) return false;

    const uint64_t limit = sign < 0 ? (static_cast<uint64_t>(INT64_MAX) + 1ull)
                                    : static_cast<uint64_t>(INT64_MAX);
    if (quot > limit) return false;

    outQuotient = sign < 0 ? -static_cast<int64_t>(quot) : static_cast<int64_t>(quot);
    outRemainder = rem;
    return true;
}

// value * numerator / denominator with kaufmaennische Rundung: a remainder of
// exactly half rounds away from zero (2,5 -> 3 and -2,5 -> -3), which is what
// German tax arithmetic does and what every invoice total is checked against.
// Returns false on overflow or a zero denominator, leaving `out` untouched.
inline bool MoneyMulDiv(int64_t value, int64_t numerator, int64_t denominator,
                        int64_t& out) {
    int64_t quotient = 0;
    uint64_t remainder = 0;
    if (!MoneyMulDivTrunc(value, numerator, denominator, quotient, remainder))
        return false;
    if (remainder == 0) { out = quotient; return true; }

    auto magnitude = [](int64_t v) -> uint64_t {
        return v < 0 ? (~static_cast<uint64_t>(v) + 1ull) : static_cast<uint64_t>(v);
    };
    const uint64_t dMag = magnitude(denominator);

    // remainder * 2 >= |denominator|, written so it cannot overflow.
    const bool roundAway = remainder >= dMag - remainder;
    if (!roundAway) { out = quotient; return true; }

    int sign = 1;
    if (value < 0)       sign = -sign;
    if (numerator < 0)   sign = -sign;
    if (denominator < 0) sign = -sign;

    if (sign < 0) {
        if (quotient == INT64_MIN) return false;
        out = quotient - 1;
    } else {
        if (quotient == INT64_MAX) return false;
        out = quotient + 1;
    }
    return true;
}

// ===== TEXT STYLES =====
// How an amount is written. These are formats, not locales: the style is
// chosen by the destination (a German label, a dot-decimal file format, a
// DATEV CSV column), never inherited from the process locale.
enum class MoneyStyle {
    German,   // 1.234,56   - grouped with '.', decimal ','      (UI)
    Plain,    // 1234.56    - no grouping, decimal '.'           (file formats, JSON, SQL text)
    Datev     // 1234,56    - no grouping, decimal ','           (DATEV CSV amounts)
};

// ===== MONEY =====
class Money {
public:
    Money() = default;

    static Money FromMinor(int64_t minor, const std::string& currency = "EUR") {
        Money m;
        m.minor_ = minor;
        m.currency_ = currency;
        m.valid_ = true;
        return m;
    }
    static Money FromMajor(int64_t major, const std::string& currency = "EUR") {
        int64_t scaled = 0;
        if (!MoneyMulDiv(major, MoneyPowerOfTen(MoneyDecimals(currency)), 1, scaled))
            return Invalid(currency);
        return FromMinor(scaled, currency);
    }
    static Money Zero(const std::string& currency = "EUR") { return FromMinor(0, currency); }
    static Money Invalid(const std::string& currency = "EUR") {
        Money m;
        m.currency_ = currency;
        m.valid_ = false;
        return m;
    }

    // An amount is invalid when it came out of a failed parse, a currency
    // mismatch or an overflow. Invalidity is sticky: it propagates through
    // arithmetic instead of throwing, so one check at the end of a
    // calculation is enough.
    bool Valid() const { return valid_; }

    int64_t            Minor()    const { return minor_; }
    const std::string& Currency() const { return currency_; }
    int                Decimals() const { return MoneyDecimals(currency_); }

    bool IsZero()    const { return valid_ && minor_ == 0; }
    bool IsNegative() const { return valid_ && minor_ < 0; }
    bool IsPositive() const { return valid_ && minor_ > 0; }

    // ---- Arithmetic --------------------------------------------------------
    // Adding two currencies is a programming error, not a conversion, so it
    // yields an invalid amount rather than a plausible wrong one. A zero with
    // an empty currency is neutral, which lets a sum start from Money().
    Money operator+(const Money& other) const {
        const std::string cur = CommonCurrency(other);
        if (cur.empty() || !valid_ || !other.valid_) return Invalid(currency_);
        int64_t sum = 0;
        if (!AddChecked(minor_, other.minor_, sum)) return Invalid(cur);
        return FromMinor(sum, cur);
    }
    Money operator-(const Money& other) const { return *this + (-other); }
    Money operator-() const {
        if (!valid_ || minor_ == INT64_MIN) return Invalid(currency_);
        return FromMinor(-minor_, currency_);
    }
    Money& operator+=(const Money& other) { *this = *this + other; return *this; }
    Money& operator-=(const Money& other) { *this = *this - other; return *this; }

    // Comparison only ever compares like with like: a mismatch is not less,
    // not greater and not equal.
    bool operator==(const Money& other) const {
        return valid_ && other.valid_ && minor_ == other.minor_ &&
               !CommonCurrency(other).empty();
    }
    bool operator!=(const Money& other) const { return !(*this == other); }
    bool operator<(const Money& other) const {
        return valid_ && other.valid_ && !CommonCurrency(other).empty() &&
               minor_ < other.minor_;
    }
    bool operator>(const Money& other)  const { return other < *this; }
    bool operator<=(const Money& other) const { return *this < other || *this == other; }
    bool operator>=(const Money& other) const { return other < *this || *this == other; }

    // Quantity: three of this.
    Money Times(int64_t count) const {
        if (!valid_) return Invalid(currency_);
        int64_t product = 0;
        if (!MoneyMulDiv(minor_, count, 1, product)) return Invalid(currency_);
        return FromMinor(product, currency_);
    }

    // An exact rate as a fraction, rounded half away from zero: a unit price
    // of 2,49 for 7 items is ScaledBy(7, 1); an exchange rate of 1,0967 is
    // ScaledBy(10967, 10000).
    Money ScaledBy(int64_t numerator, int64_t denominator) const {
        if (!valid_) return Invalid(currency_);
        int64_t scaled = 0;
        if (!MoneyMulDiv(minor_, numerator, denominator, scaled)) return Invalid(currency_);
        return FromMinor(scaled, currency_);
    }

    // A rate in permille (thousandths): 190 is 19 %, 70 is 7 %, 25 is 2,5 %.
    // Permille rather than percent because German VAT keys and Skonto rates
    // both need one decimal place and neither should be a double.
    Money Permille(int64_t permille) const { return ScaledBy(permille, 1000); }

    // ---- German VAT ---------------------------------------------------------
    // tax = net * p / 1000            (Steuer aus dem Netto)
    Money TaxOnNet(int64_t permille) const { return ScaledBy(permille, 1000); }
    // gross = net * (1000 + p) / 1000
    Money GrossFromNet(int64_t permille) const { return ScaledBy(1000 + permille, 1000); }
    // tax contained in a gross amount = gross * p / (1000 + p)
    Money TaxInGross(int64_t permille) const { return ScaledBy(permille, 1000 + permille); }
    // net = gross - tax, so that net + tax == gross exactly, whatever the
    // rounding did.
    Money NetFromGross(int64_t permille) const { return *this - TaxInGross(permille); }

    // ---- Splitting ----------------------------------------------------------
    // Distribute this amount over `weights` so the parts sum exactly to it.
    // Largest-remainder: the truncated shares are handed out first, then the
    // leftover minor units go to the largest remainders, ties to the earlier
    // position. Negative or all-zero weights, or an overflow, yield an empty
    // vector rather than a wrong distribution.
    std::vector<Money> SplitProportionally(const std::vector<int64_t>& weights) const {
        std::vector<Money> parts;
        if (!valid_ || weights.empty()) return parts;

        int64_t totalWeight = 0;
        for (int64_t w : weights) {
            if (w < 0) return parts;
            if (!AddChecked(totalWeight, w, totalWeight)) return parts;
        }
        if (totalWeight == 0) return SplitEvenly(weights.size());

        std::vector<int64_t>  share(weights.size(), 0);
        std::vector<uint64_t> remainder(weights.size(), 0);
        int64_t distributed = 0;
        for (size_t i = 0; i < weights.size(); ++i) {
            int64_t  q = 0;
            uint64_t r = 0;
            if (!MoneyMulDivTrunc(minor_, weights[i], totalWeight, q, r)) return {};
            share[i] = q;
            remainder[i] = r;
            if (!AddChecked(distributed, q, distributed)) return {};
        }

        // What truncation left over, in whole minor units, signed like the
        // amount itself.
        int64_t leftover = minor_ - distributed;
        const int64_t step = leftover < 0 ? -1 : 1;
        while (leftover != 0) {
            size_t best = 0;
            bool found = false;
            for (size_t i = 0; i < remainder.size(); ++i) {
                if (!found || remainder[i] > remainder[best]) { best = i; found = true; }
            }
            if (!found) break;
            share[best] += step;
            remainder[best] = 0;                 // each position gets at most one extra unit
            leftover -= step;
            bool anyLeft = false;
            for (uint64_t r : remainder) if (r > 0) { anyLeft = true; break; }
            if (!anyLeft && leftover != 0) {
                // More leftover than positions with a remainder: keep going
                // round-robin from the front so the total still matches.
                for (size_t i = 0; i < share.size() && leftover != 0; ++i) {
                    share[i] += step;
                    leftover -= step;
                }
                break;
            }
        }

        parts.reserve(share.size());
        for (int64_t s : share) parts.push_back(FromMinor(s, currency_));
        return parts;
    }

    // Equal parts, with the odd minor units going to the first positions.
    std::vector<Money> SplitEvenly(size_t parts) const {
        if (!valid_ || parts == 0) return {};
        return SplitProportionally(std::vector<int64_t>(parts, 1));
    }

    // ---- Text ---------------------------------------------------------------
    // Digits are assembled from the integer value; no stream, no printf, no
    // locale. An invalid amount formats as an empty string, so it cannot be
    // mistaken for zero in a report.
    std::string ToString(MoneyStyle style = MoneyStyle::German) const {
        if (!valid_) return std::string();

        const int decimals = Decimals();
        const bool negative = minor_ < 0;
        // |minor_| without negating INT64_MIN.
        uint64_t mag = negative ? (~static_cast<uint64_t>(minor_) + 1ull)
                                : static_cast<uint64_t>(minor_);

        std::string fraction;
        for (int i = 0; i < decimals; ++i) {
            fraction.insert(fraction.begin(), static_cast<char>('0' + (mag % 10)));
            mag /= 10;
        }
        std::string whole;
        if (mag == 0) whole = "0";
        while (mag > 0) {
            whole.insert(whole.begin(), static_cast<char>('0' + (mag % 10)));
            mag /= 10;
        }

        if (style == MoneyStyle::German) {
            for (size_t pos = whole.size() > 3 ? whole.size() - 3 : 0; pos > 0; pos -= 3) {
                whole.insert(pos, ".");
                if (pos < 3) break;
            }
        }

        std::string out;
        if (negative) out += "-";
        out += whole;
        if (decimals > 0) {
            out += (style == MoneyStyle::Plain) ? "." : ",";
            out += fraction;
        }
        return out;
    }

    // "1.234,56 EUR" / "1.234,56 €" for the UI. The symbol is only special-cased
    // where it is unambiguous; everything else gets its ISO code, which is
    // always correct and never wrong-looking.
    std::string ToStringWithSymbol() const {
        if (!valid_) return std::string();
        std::string out = ToString(MoneyStyle::German);
        if (currency_ == "EUR")      out += " \xE2\x82\xAC";   // €
        else if (currency_ == "USD") out += " $";
        else if (currency_ == "GBP") out += " \xC2\xA3";       // £
        else if (currency_ == "CHF") out += " CHF";
        else if (!currency_.empty()) out += " " + currency_;
        return out;
    }

    // Read an amount written by a human or by a file format.
    //
    // Accepted: an optional leading or trailing sign, parentheses for a
    // negative, spaces (including NBSP) and thin spaces as grouping, '.' and
    // ',' as grouping or decimal separator, a trailing currency symbol or ISO
    // code. More decimals than the currency has are rounded half away from
    // zero, so "1,005" is 1,01 EUR.
    //
    // Which separator is decimal:
    //  - both present  -> the rightmost one is the decimal separator;
    //  - one present   -> MoneyStyle::German reads ',' as decimal and '.' as
    //    grouping, MoneyStyle::Plain the other way round, and MoneyStyle::Datev
    //    accepts only ',' as decimal (a '.' is then grouping, which is what a
    //    DATEV column may legitimately contain);
    //  - a single separator followed by exactly three digits, in the style that
    //    would call it grouping, stays grouping ("1.234" is 1234).
    //
    // The two machine styles are strict about that last case: a grouping
    // separator standing anywhere but three digits from the end means the field
    // is malformed and the parse fails, because an importer that misreads an
    // amount does more damage than one that rejects a line.
    static bool TryParse(const std::string& text, Money& out,
                         const std::string& currency = "EUR",
                         MoneyStyle style = MoneyStyle::German) {
        std::string digits;
        bool negative = false;
        bool sawParenOpen = false;
        int  lastDot = -1, lastComma = -1;
        int  digitCount = 0;

        for (size_t i = 0; i < text.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            if (c >= '0' && c <= '9') {
                digits += static_cast<char>(c);
                ++digitCount;
                continue;
            }
            switch (c) {
                case '-': negative = true; break;
                case '+': break;
                case '(': sawParenOpen = true; break;
                case ')': if (sawParenOpen) negative = true; break;
                case '.': lastDot   = digitCount; break;
                case ',': lastComma = digitCount; break;
                default: break;   // spaces, NBSP bytes, symbols, ISO codes
            }
        }
        if (digits.empty()) { out = Invalid(currency); return false; }

        // Decide where the decimal point sits, expressed as digits after it.
        int fractionDigits = 0;
        if (lastDot >= 0 && lastComma >= 0) {
            const int decimalAt = lastDot > lastComma ? lastDot : lastComma;
            fractionDigits = digitCount - decimalAt;
        } else if (lastDot >= 0 || lastComma >= 0) {
            const bool isComma = lastComma >= 0;
            const int  only    = isComma ? lastComma : lastDot;
            const int  after   = digitCount - only;
            bool decimal = false;
            switch (style) {
                case MoneyStyle::Plain:
                    decimal = !isComma;                  // '.' decimal, ',' grouping
                    // A machine format is strict: a grouping separator that is
                    // not three digits from the end means the field is
                    // malformed, and misreading an amount is far worse than
                    // rejecting it.
                    if (!decimal && after != 3) { out = Invalid(currency); return false; }
                    break;
                case MoneyStyle::Datev:
                    decimal = isComma;                   // ',' decimal, '.' grouping
                    if (!decimal && after != 3) { out = Invalid(currency); return false; }
                    break;
                case MoneyStyle::German:
                    // ',' is always the decimal separator; a lone '.' is one
                    // too, unless it stands exactly three digits from the end
                    // and so reads as grouping ("1.234" is 1234, "1234.56" is
                    // 1234,56). A German numpad types ',' but plenty of people
                    // type '.', and refusing that is a worse answer than
                    // reading it.
                    decimal = isComma || after != 3;
                    break;
            }
            fractionDigits = decimal ? after : 0;
        }

        const int decimals = MoneyDecimals(currency);

        // Integer value of all digits, then rescale to the currency's minor
        // units, rounding half away from zero when the input was finer.
        int64_t value = 0;
        for (char d : digits) {
            if (!MoneyMulDiv(value, 10, 1, value)) { out = Invalid(currency); return false; }
            int64_t sum = 0;
            if (!AddChecked(value, static_cast<int64_t>(d - '0'), sum)) {
                out = Invalid(currency);
                return false;
            }
            value = sum;
        }
        int64_t minor = value;
        if (fractionDigits > decimals) {
            if (!MoneyMulDiv(value, 1, MoneyPowerOfTen(fractionDigits - decimals), minor)) {
                out = Invalid(currency);
                return false;
            }
        } else if (fractionDigits < decimals) {
            if (!MoneyMulDiv(value, MoneyPowerOfTen(decimals - fractionDigits), 1, minor)) {
                out = Invalid(currency);
                return false;
            }
        }

        if (negative) {
            if (minor == INT64_MIN) { out = Invalid(currency); return false; }
            minor = -minor;
        }
        out = FromMinor(minor, currency);
        return true;
    }

private:
    static bool AddChecked(int64_t a, int64_t b, int64_t& out) {
        if (b > 0 && a > INT64_MAX - b) return false;
        if (b < 0 && a < INT64_MIN - b) return false;
        out = a + b;
        return true;
    }

    // The currency two amounts share, or "" when they cannot be combined. An
    // empty currency on a zero amount is neutral, so a sum can start at
    // Money::Zero("").
    std::string CommonCurrency(const Money& other) const {
        if (currency_ == other.currency_) return currency_;
        if (currency_.empty()  && minor_ == 0)       return other.currency_;
        if (other.currency_.empty() && other.minor_ == 0) return currency_;
        return std::string();
    }

    int64_t     minor_ = 0;
    std::string currency_;
    bool        valid_ = true;
};

// Sum of a list, invalid as soon as one element is invalid or the currencies
// disagree. Starts from a neutral zero so an empty list sums to zero.
inline Money MoneySum(const std::vector<Money>& amounts) {
    Money total = Money::Zero(std::string());
    for (const Money& m : amounts) total += m;
    return total;
}

} // namespace UltraCanvas
