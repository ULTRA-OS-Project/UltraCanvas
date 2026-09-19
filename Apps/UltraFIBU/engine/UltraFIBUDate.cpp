// Apps/UltraFIBU/engine/UltraFIBUDate.cpp
// Date arithmetic and the four text forms this application needs. Every digit
// is assembled by hand: no stream, no printf, no locale - the same rule the
// money type follows, and for the same reason.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUDate.h"

namespace UltraFIBU {

namespace {

// Zero-padded decimal, no locale involved.
std::string Pad(int value, int width) {
    std::string digits;
    int v = value < 0 ? -value : value;
    if (v == 0) digits = "0";
    while (v > 0) {
        digits.insert(digits.begin(), static_cast<char>('0' + (v % 10)));
        v /= 10;
    }
    while (static_cast<int>(digits.size()) < width) digits.insert(digits.begin(), '0');
    if (value < 0) digits.insert(digits.begin(), '-');
    return digits;
}

// All digits of a text, in order, with the count of them - used by the lenient
// parsers below.
bool DigitsOnly(const std::string& text, std::string& digits) {
    digits.clear();
    for (char c : text) {
        if (c >= '0' && c <= '9') digits += c;
        else if (c == '-' || c == '.' || c == '/' || c == ' ') continue;
        else return false;
    }
    return !digits.empty();
}

int ToInt(const std::string& digits, size_t from, size_t count) {
    int value = 0;
    for (size_t i = from; i < from + count && i < digits.size(); ++i)
        value = value * 10 + (digits[i] - '0');
    return value;
}

} // namespace

std::string Date::ToIso() const {
    if (!Valid()) return std::string();
    return Pad(year, 4) + "-" + Pad(month, 2) + "-" + Pad(day, 2);
}

bool Date::TryParseIso(const std::string& text, Date& out) {
    std::string digits;
    if (!DigitsOnly(text, digits)) return false;
    if (digits.size() != 8) return false;           // YYYYMMDD, with or without separators
    Date parsed(ToInt(digits, 0, 4), ToInt(digits, 4, 2), ToInt(digits, 6, 2));
    if (!parsed.Valid()) return false;
    out = parsed;
    return true;
}

// Days from 1970-01-01, by the civil-calendar algorithm (Howard Hinnant's
// days_from_civil): exact for every date in the supported range, no tables.
int64_t Date::ToEpochDay() const {
    int64_t y = year;
    const int64_t m = month;
    const int64_t d = day;
    y -= m <= 2 ? 1 : 0;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;                                  // [0, 399]
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; // [0, 365]
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;          // [0, 146096]
    return era * 146097 + doe - 719468;
}

Date Date::FromEpochDay(int64_t epochDay) {
    int64_t z = epochDay + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;                               // [0, 146096]
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = yoe + era * 400;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);        // [0, 365]
    const int64_t mp = (5 * doy + 2) / 153;                             // [0, 11]
    const int64_t d = doy - (153 * mp + 2) / 5 + 1;                     // [1, 31]
    const int64_t m = mp + (mp < 10 ? 3 : -9);                          // [1, 12]
    y += (m <= 2 ? 1 : 0);
    return Date(static_cast<int>(y), static_cast<int>(m), static_cast<int>(d));
}

Date Date::AddMonths(int months) const {
    int64_t total = static_cast<int64_t>(year) * 12 + (month - 1) + months;
    int newYear  = static_cast<int>(total / 12);
    int newMonth = static_cast<int>(total % 12);
    if (newMonth < 0) { newMonth += 12; --newYear; }
    ++newMonth;
    const int maxDay = DaysInMonth(newYear, newMonth);
    return Date(newYear, newMonth, day > maxDay ? maxDay : day);
}

std::string FormatDateGerman(const Date& date) {
    if (!date.Valid()) return std::string();
    return Pad(date.day, 2) + "." + Pad(date.month, 2) + "." + Pad(date.year, 4);
}

std::string FormatDateDatev(const Date& date) {
    if (!date.Valid()) return std::string();
    return Pad(date.day, 2) + Pad(date.month, 2);
}

std::string FormatDateCompact(const Date& date) {
    if (!date.Valid()) return std::string();
    return Pad(date.year, 4) + Pad(date.month, 2) + Pad(date.day, 2);
}

bool TryParseDateGerman(const std::string& text, Date& out) {
    // An ISO date is accepted as-is, so one field can take either form.
    if (Date::TryParseIso(text, out)) return true;

    // Otherwise read it as day, month, year separated by anything non-numeric.
    int parts[3] = { 0, 0, 0 };
    int partIndex = 0;
    int digitsInPart = 0;
    bool any = false;
    for (size_t i = 0; i <= text.size(); ++i) {
        const char c = i < text.size() ? text[i] : '\0';
        if (c >= '0' && c <= '9') {
            if (partIndex > 2) return false;
            parts[partIndex] = parts[partIndex] * 10 + (c - '0');
            ++digitsInPart;
            any = true;
            if (digitsInPart > 4) return false;
        } else if (digitsInPart > 0) {
            ++partIndex;
            digitsInPart = 0;
        }
    }
    if (!any || partIndex < 3) return false;

    int year = parts[2];
    // A two-digit year: 70..99 is the 20th century, 00..69 the 21st. Written
    // out because bookkeeping data from the 1990s exists and guessing wrong by
    // a century is not a rounding error.
    if (year < 100) year += (year >= 70) ? 1900 : 2000;

    Date parsed(year, parts[1], parts[0]);
    if (!parsed.Valid()) return false;
    out = parsed;
    return true;
}

} // namespace UltraFIBU
