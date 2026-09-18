// Apps/UltraFIBU/engine/UltraFIBUGeschaeftsjahr.cpp
// The fiscal and the tax calendar. Both are pure functions of dates - no
// database, no locale, no assumption that a year starts in January.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUGeschaeftsjahr.h"

namespace UltraFIBU {

namespace {

const char* const kMonthNames[12] = {
    "Januar", "Februar", "März", "April", "Mai", "Juni",
    "Juli", "August", "September", "Oktober", "November", "Dezember"
};

std::string Number(int value) {
    std::string digits;
    int v = value < 0 ? -value : value;
    if (v == 0) digits = "0";
    while (v > 0) {
        digits.insert(digits.begin(), static_cast<char>('0' + (v % 10)));
        v /= 10;
    }
    if (value < 0) digits.insert(digits.begin(), '-');
    return digits;
}

std::string Pad2(int value) {
    std::string s = Number(value);
    if (s.size() < 2) s.insert(s.begin(), '0');
    return s;
}

// Whole months between two month positions, inclusive of both ends.
int MonthSpan(const Date& from, const Date& to) {
    return (to.year - from.year) * 12 + (to.month - from.month) + 1;
}

// 0 = Thursday for epoch day 0 (1970-01-01); this returns 0 = Sunday .. 6 = Saturday.
int WeekdayOf(const Date& date) {
    const int64_t epochDay = date.ToEpochDay();
    int weekday = static_cast<int>((epochDay + 4) % 7);   // 1970-01-01 was a Thursday
    if (weekday < 0) weekday += 7;
    return weekday;
}

// § 108 Abs. 3 AO: a deadline falling on a Saturday or Sunday moves to the next
// working day. Public holidays are not applied - see the header.
Date NextWorkingDay(const Date& date) {
    Date d = date;
    for (int guard = 0; guard < 7; ++guard) {
        const int weekday = WeekdayOf(d);
        if (weekday != 0 && weekday != 6) return d;
        d = d.AddDays(1);
    }
    return d;
}

} // namespace

// ===== GESCHAEFTSJAHR =====

std::string GeschaeftsjahrStatusToText(GeschaeftsjahrStatus status) {
    switch (status) {
        case GeschaeftsjahrStatus::Offen:           return "offen";
        case GeschaeftsjahrStatus::Festgeschrieben: return "festgeschrieben";
        case GeschaeftsjahrStatus::Abgeschlossen:   return "abgeschlossen";
    }
    return "offen";
}

bool GeschaeftsjahrStatusFromText(const std::string& text, GeschaeftsjahrStatus& out) {
    if (text == "offen")           { out = GeschaeftsjahrStatus::Offen;           return true; }
    if (text == "festgeschrieben") { out = GeschaeftsjahrStatus::Festgeschrieben; return true; }
    if (text == "abgeschlossen")   { out = GeschaeftsjahrStatus::Abgeschlossen;   return true; }
    return false;
}

bool Geschaeftsjahr::Valid() const {
    if (!beginn.Valid() || !ende.Valid()) return false;
    if (ende < beginn) return false;
    if (beginn.day != 1) return false;                          // whole months only
    if (ende != ende.LastOfMonth()) return false;
    if (MonthSpan(beginn, ende) > 12) return false;             // never longer than a year
    if (sachkontenlaenge < 4 || sachkontenlaenge > 8) return false;
    if (festschreibungBis.Valid() && !Contains(festschreibungBis)) return false;
    return true;
}

int Geschaeftsjahr::PeriodCount() const {
    if (!beginn.Valid() || !ende.Valid() || ende < beginn) return 0;
    return MonthSpan(beginn, ende);
}

int Geschaeftsjahr::PeriodOf(const Date& date) const {
    if (!Contains(date)) return 0;
    return (date.year - beginn.year) * 12 + (date.month - beginn.month) + 1;
}

Date Geschaeftsjahr::PeriodStart(int period) const {
    if (period < 1 || period > PeriodCount()) return Date();
    return beginn.AddMonths(period - 1).FirstOfMonth();
}

Date Geschaeftsjahr::PeriodEnd(int period) const {
    const Date start = PeriodStart(period);
    if (!start.Valid()) return Date();
    const Date last = start.LastOfMonth();
    // The final period of a Rumpfjahr may end before its month does only if
    // the year itself does, which Valid() forbids - but guard anyway rather
    // than report a date outside the year.
    return last > ende ? ende : last;
}

std::string Geschaeftsjahr::PeriodName(int period) const {
    const Date start = PeriodStart(period);
    if (!start.Valid()) return std::string();
    return std::string(kMonthNames[start.month - 1]) + " " + Number(start.year);
}

std::string Geschaeftsjahr::DefaultBezeichnung() const {
    if (!beginn.Valid() || !ende.Valid()) return std::string();
    if (beginn.year == ende.year) return Number(beginn.year);
    return Number(beginn.year) + "/" + Number(ende.year);
}

Geschaeftsjahr MakeGeschaeftsjahr(const Date& beginn) {
    Geschaeftsjahr jahr;
    if (!beginn.Valid()) return jahr;
    jahr.beginn = beginn.FirstOfMonth();
    jahr.ende   = jahr.beginn.AddMonths(12).AddDays(-1);
    jahr.bezeichnung = jahr.DefaultBezeichnung();
    return jahr;
}

Geschaeftsjahr MakeRumpfjahr(const Date& beginn, const Date& ende) {
    Geschaeftsjahr jahr;
    if (!beginn.Valid() || !ende.Valid()) return jahr;
    jahr.beginn = beginn.FirstOfMonth();
    jahr.ende   = ende.LastOfMonth();
    jahr.bezeichnung = jahr.DefaultBezeichnung();
    return jahr;
}

Geschaeftsjahr NextGeschaeftsjahr(const Geschaeftsjahr& previous) {
    Geschaeftsjahr jahr = MakeGeschaeftsjahr(previous.ende.AddDays(1));
    jahr.mandantId        = previous.mandantId;
    jahr.skr              = previous.skr;
    jahr.sachkontenlaenge = previous.sachkontenlaenge;
    jahr.bezeichnung      = jahr.DefaultBezeichnung();
    return jahr;
}

bool GeschaeftsjahreOverlap(const Geschaeftsjahr& a, const Geschaeftsjahr& b) {
    if (!a.beginn.Valid() || !a.ende.Valid() || !b.beginn.Valid() || !b.ende.Valid())
        return false;
    return a.beginn <= b.ende && b.beginn <= a.ende;
}

bool GeschaeftsjahreContiguous(const Geschaeftsjahr& previous, const Geschaeftsjahr& next) {
    if (!previous.ende.Valid() || !next.beginn.Valid()) return false;
    return previous.ende.AddDays(1) == next.beginn;
}

// ===== VORANMELDUNGSZEITRAUM =====

std::string UstvaRhythmusToText(UstvaRhythmus rhythmus) {
    switch (rhythmus) {
        case UstvaRhythmus::Monatlich:        return "monatlich";
        case UstvaRhythmus::Vierteljaehrlich: return "vierteljaehrlich";
        case UstvaRhythmus::Jaehrlich:        return "jaehrlich";
    }
    return "vierteljaehrlich";
}

bool UstvaRhythmusFromText(const std::string& text, UstvaRhythmus& out) {
    if (text == "monatlich")        { out = UstvaRhythmus::Monatlich;        return true; }
    if (text == "vierteljaehrlich") { out = UstvaRhythmus::Vierteljaehrlich; return true; }
    if (text == "jaehrlich")        { out = UstvaRhythmus::Jaehrlich;        return true; }
    return false;
}

bool Voranmeldungszeitraum::Valid() const {
    if (year < 1900 || year > 2999) return false;
    switch (rhythmus) {
        case UstvaRhythmus::Monatlich:        return period >= 1 && period <= 12;
        case UstvaRhythmus::Vierteljaehrlich: return period >= 1 && period <= 4;
        case UstvaRhythmus::Jaehrlich:        return period == 1;
    }
    return false;
}

Date Voranmeldungszeitraum::Start() const {
    if (!Valid()) return Date();
    switch (rhythmus) {
        case UstvaRhythmus::Monatlich:        return Date(year, period, 1);
        case UstvaRhythmus::Vierteljaehrlich: return Date(year, (period - 1) * 3 + 1, 1);
        case UstvaRhythmus::Jaehrlich:        return Date(year, 1, 1);
    }
    return Date();
}

Date Voranmeldungszeitraum::End() const {
    if (!Valid()) return Date();
    switch (rhythmus) {
        case UstvaRhythmus::Monatlich:        return Date(year, period, 1).LastOfMonth();
        case UstvaRhythmus::Vierteljaehrlich: return Date(year, (period - 1) * 3 + 3, 1).LastOfMonth();
        case UstvaRhythmus::Jaehrlich:        return Date(year, 12, 31);
    }
    return Date();
}

std::string Voranmeldungszeitraum::Bezeichnung() const {
    if (!Valid()) return std::string();
    switch (rhythmus) {
        case UstvaRhythmus::Monatlich:
            return std::string(kMonthNames[period - 1]) + " " + Number(year);
        case UstvaRhythmus::Vierteljaehrlich:
            return Number(period) + ". Quartal " + Number(year);
        case UstvaRhythmus::Jaehrlich:
            return "Jahr " + Number(year);
    }
    return std::string();
}

std::string Voranmeldungszeitraum::ElsterZeitraum() const {
    if (!Valid()) return std::string();
    switch (rhythmus) {
        case UstvaRhythmus::Monatlich:        return Pad2(period);          // 01..12
        case UstvaRhythmus::Vierteljaehrlich: return Number(40 + period);   // 41..44
        case UstvaRhythmus::Jaehrlich:        return std::string();         // the annual return is its own data type
    }
    return std::string();
}

Date Voranmeldungszeitraum::Deadline(bool dauerfristverlaengerung) const {
    const Date end = End();
    if (!end.Valid()) return Date();
    // The 10th of the month following the period, plus a month when the
    // Dauerfristverlaengerung applies.
    Date due = end.AddMonths(dauerfristverlaengerung ? 2 : 1).FirstOfMonth();
    due = Date(due.year, due.month, 10);
    return NextWorkingDay(due);
}

Voranmeldungszeitraum ZeitraumOf(const Date& date, UstvaRhythmus rhythmus) {
    Voranmeldungszeitraum zeitraum;
    if (!date.Valid()) return zeitraum;
    zeitraum.rhythmus = rhythmus;
    zeitraum.year     = date.year;
    switch (rhythmus) {
        case UstvaRhythmus::Monatlich:        zeitraum.period = date.month; break;
        case UstvaRhythmus::Vierteljaehrlich: zeitraum.period = (date.month - 1) / 3 + 1; break;
        case UstvaRhythmus::Jaehrlich:        zeitraum.period = 1; break;
    }
    return zeitraum;
}

std::vector<Voranmeldungszeitraum> ZeitraeumeInGeschaeftsjahr(const Geschaeftsjahr& jahr,
                                                              UstvaRhythmus rhythmus) {
    std::vector<Voranmeldungszeitraum> zeitraeume;
    if (!jahr.Valid()) return zeitraeume;

    // Walk the fiscal year month by month and collect each distinct reporting
    // period the months fall into. For a 1 April year with quarterly returns
    // that is Q2, Q3, Q4 of the first calendar year and Q1 of the next - five
    // fiscal months share Q1 and Q4 with the neighbouring fiscal years, which
    // is exactly the overlap the caller has to be aware of.
    Date cursor = jahr.beginn;
    while (cursor <= jahr.ende) {
        const Voranmeldungszeitraum zeitraum = ZeitraumOf(cursor, rhythmus);
        bool alreadyThere = false;
        for (const Voranmeldungszeitraum& existing : zeitraeume)
            if (existing == zeitraum) { alreadyThere = true; break; }
        if (!alreadyThere) zeitraeume.push_back(zeitraum);
        cursor = cursor.AddMonths(1).FirstOfMonth();
    }
    return zeitraeume;
}

// ===== OSS =====

Date OssQuartal::Start() const {
    if (!Valid()) return Date();
    return Date(year, (quarter - 1) * 3 + 1, 1);
}

Date OssQuartal::End() const {
    if (!Valid()) return Date();
    return Date(year, (quarter - 1) * 3 + 3, 1).LastOfMonth();
}

Date OssQuartal::Deadline() const {
    const Date end = End();
    if (!end.Valid()) return Date();
    // The end of the month following the quarter. No Dauerfristverlaengerung
    // exists for OSS, and the BZSt does not move the date for a weekend
    // either - the return is simply due by then.
    return end.AddMonths(1).LastOfMonth();
}

std::string OssQuartal::Bezeichnung() const {
    if (!Valid()) return std::string();
    return "Q" + Number(quarter) + " " + Number(year);
}

OssQuartal OssQuartalOf(const Date& date) {
    OssQuartal quartal;
    if (!date.Valid()) return quartal;
    quartal.year    = date.year;
    quartal.quarter = (date.month - 1) / 3 + 1;
    return quartal;
}

} // namespace UltraFIBU
