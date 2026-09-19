// Apps/UltraFIBU/engine/UltraFIBUDate.h
// A calendar date, and nothing else: year, month, day, with ISO-8601 text as
// its only serialised form.
//
// Why this exists rather than std::chrono's calendar types: every date in this
// application travels through SQL and through file formats (DATEV's TTMM, the
// ELSTER XML, CAMT.053, the GoBD index), it is compared and grouped far more
// often than it is arithmetic'd, and it must round-trip through SQLite *and*
// PostgreSQL as plain text. A 12-byte value with ISO text in and out is the
// whole requirement, and it keeps every date computation in C++ where the
// Geschaeftsjahr logic lives instead of in driver-specific SQL.
//
// There is no time of day here on purpose. A Belegdatum is a date; the moment
// a row was entered is a separate epoch-seconds field.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <string>

namespace UltraFIBU {

struct Date {
    int year  = 0;
    int month = 0;   // 1..12
    int day   = 0;   // 1..DaysInMonth

    Date() = default;
    Date(int y, int m, int d) : year(y), month(m), day(d) {}

    static bool IsLeapYear(int year) {
        return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    }
    static int DaysInMonth(int year, int month) {
        static const int kDays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        if (month < 1 || month > 12) return 0;
        if (month == 2 && IsLeapYear(year)) return 29;
        return kDays[month - 1];
    }

    bool Valid() const {
        return year >= 1900 && year <= 2999 &&
               month >= 1 && month <= 12 &&
               day >= 1 && day <= DaysInMonth(year, month);
    }

    // "2026-04-01". An invalid date yields an empty string, so it cannot be
    // mistaken for a real one in a file or a database column.
    std::string ToIso() const;

    // Accepts "YYYY-MM-DD" and the compact "YYYYMMDD" that DATEV headers use.
    static bool TryParseIso(const std::string& text, Date& out);

    // Days since 1970-01-01, negative before it. The one place a date becomes
    // a number - used for differences, for day arithmetic and for sorting in
    // memory, never for storage.
    int64_t ToEpochDay() const;
    static Date FromEpochDay(int64_t day);

    Date AddDays(int64_t days) const { return FromEpochDay(ToEpochDay() + days); }
    // Calendar-correct month arithmetic: the day is clamped to the target
    // month's length, so 31 January plus one month is 28 (or 29) February.
    Date AddMonths(int months) const;
    Date FirstOfMonth() const { return Date(year, month, 1); }
    Date LastOfMonth() const  { return Date(year, month, DaysInMonth(year, month)); }

    // Difference in whole days (this - other).
    int64_t DaysUntil(const Date& other) const { return other.ToEpochDay() - ToEpochDay(); }

    bool operator==(const Date& o) const { return year == o.year && month == o.month && day == o.day; }
    bool operator!=(const Date& o) const { return !(*this == o); }
    bool operator<(const Date& o) const {
        if (year  != o.year)  return year  < o.year;
        if (month != o.month) return month < o.month;
        return day < o.day;
    }
    bool operator>(const Date& o)  const { return o < *this; }
    bool operator<=(const Date& o) const { return !(o < *this); }
    bool operator>=(const Date& o) const { return !(*this < o); }
};

// German presentation: "01.04.2026". Empty for an invalid date, like ToIso().
std::string FormatDateGerman(const Date& date);

// "TTMM" - the four-digit, year-less form the DATEV Buchungsstapel uses for
// Belegdatum. The year is carried by the stack's Wirtschaftsjahr and period,
// which is why a stack may never straddle a calendar-year boundary.
std::string FormatDateDatev(const Date& date);

// "YYYYMMDD" - the compact form the DATEV header fields use (WJ-Beginn,
// Datum von/bis).
std::string FormatDateCompact(const Date& date);

// Reads "01.04.2026" and "1.4.2026"; also accepts ISO input, so a field that
// people paste into can take either.
bool TryParseDateGerman(const std::string& text, Date& out);

} // namespace UltraFIBU
