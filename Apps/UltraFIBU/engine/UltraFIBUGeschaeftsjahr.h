// Apps/UltraFIBU/engine/UltraFIBUGeschaeftsjahr.h
// The two calendars this application lives in, and the code that keeps them
// apart.
//
//  1. The **Geschaeftsjahr** (fiscal year) - whose start date is free. A year
//     beginning 1 April is an ordinary row here, not a special case: period 1
//     is April, period 12 is March, and a Rumpfwirtschaftsjahr (a short year,
//     which is what a changeover produces) is simply a year of fewer than
//     twelve periods. EUeR, BWA, the balance sheet and depreciation all
//     aggregate over this.
//
//  2. The **Voranmeldungszeitraum** (VAT return period) - which is always a
//     calendar month or a calendar quarter (§ 18 UStG) and does not care what
//     the Geschaeftsjahr is. UStVA, ZM and the OSS quarter aggregate over this.
//
// Conflating the two is the mistake that shows up at the first year-end: for a
// 1 April year, the Q1 return covers a fiscal-year boundary, and closing the
// year must not lock a calendar period whose return has not been filed yet.
// So no report derives a tax period from a fiscal period, or the reverse -
// both come from the Belegdatum, independently.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraFIBUDate.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraFIBU {

// ===== GESCHAEFTSJAHR =====

enum class GeschaeftsjahrStatus {
    Offen,             // postings may be added and changed
    Festgeschrieben,   // frozen up to festschreibungBis; corrections by Storno only
    Abgeschlossen      // closed: carried forward into the next year
};

std::string GeschaeftsjahrStatusToText(GeschaeftsjahrStatus status);
bool        GeschaeftsjahrStatusFromText(const std::string& text, GeschaeftsjahrStatus& out);

struct Geschaeftsjahr {
    int64_t              id        = 0;
    int64_t              mandantId = 0;
    Date                 beginn;
    Date                 ende;
    GeschaeftsjahrStatus status = GeschaeftsjahrStatus::Offen;
    Date                 festschreibungBis;   // invalid => nothing frozen yet
    std::string          skr = "SKR03";       // SKR03 | SKR04 | EIGEN
    int                  sachkontenlaenge = 4;// 4..8, mirrors the DATEV header field
    std::string          bezeichnung;         // "2026/2027"

    // A year is well-formed when both dates are real, the end follows the
    // start, the span is at most twelve months, and it starts on the first of a
    // month and ends on the last of one. The last rule is not pedantry: DATEV,
    // every VAT period and every monthly report assume whole months, and a year
    // starting on the 15th would silently produce half periods everywhere.
    bool Valid() const;

    bool Contains(const Date& date) const {
        return date.Valid() && beginn.Valid() && ende.Valid() &&
               date >= beginn && date <= ende;
    }

    // Whole months in the year: 12 for a normal one, fewer for a Rumpfjahr.
    int  PeriodCount() const;
    bool IsRumpfjahr() const { return PeriodCount() < 12; }

    // The period a date falls in, counted from the year's start (1..n), or 0
    // when the date is outside the year. For a 1 April year, April is 1 and
    // the following March is 12 - this function is the only place that
    // arithmetic exists.
    int  PeriodOf(const Date& date) const;

    Date PeriodStart(int period) const;   // invalid Date for an out-of-range period
    Date PeriodEnd(int period) const;

    // "Juli 2026" for a period, empty when out of range.
    std::string PeriodName(int period) const;

    // Everything on or before festschreibungBis is immutable.
    bool IsFrozen(const Date& date) const {
        return festschreibungBis.Valid() && date.Valid() && date <= festschreibungBis;
    }
    bool AcceptsPostings() const { return status != GeschaeftsjahrStatus::Abgeschlossen; }

    // "2026" for a calendar year, "2026/2027" for one that crosses New Year.
    std::string DefaultBezeichnung() const;
};

// Twelve months from `beginn`, ending the day before the anniversary: a year
// starting 2026-04-01 ends 2027-03-31.
Geschaeftsjahr MakeGeschaeftsjahr(const Date& beginn);

// A short year from `beginn` to `ende` - a Rumpfwirtschaftsjahr, which is what
// a company gets when it is founded mid-year or changes its fiscal year.
Geschaeftsjahr MakeRumpfjahr(const Date& beginn, const Date& ende);

// The year that follows, starting the day after `previous` ends. Carries the
// chart, the account width and the Mandant across.
Geschaeftsjahr NextGeschaeftsjahr(const Geschaeftsjahr& previous);

// Do two years overlap? The store refuses an overlapping year, because a
// posting that belongs to two fiscal years has no correct report.
bool GeschaeftsjahreOverlap(const Geschaeftsjahr& a, const Geschaeftsjahr& b);

// Is `next` seamless after `previous` (starts exactly the day after)? A gap
// means postings that belong to no year at all.
bool GeschaeftsjahreContiguous(const Geschaeftsjahr& previous, const Geschaeftsjahr& next);

// ===== VORANMELDUNGSZEITRAUM (the tax calendar) =====

enum class UstvaRhythmus {
    Monatlich,          // monthly - new businesses, and above 9.000 EUR annual VAT
    Vierteljaehrlich,   // quarterly - the common case
    Jaehrlich           // annual return only (below the threshold, or Kleinunternehmer)
};

std::string UstvaRhythmusToText(UstvaRhythmus rhythmus);
bool        UstvaRhythmusFromText(const std::string& text, UstvaRhythmus& out);

struct Voranmeldungszeitraum {
    int           year    = 0;
    int           period  = 0;   // 1..12 for months, 1..4 for quarters, 1 for a year
    UstvaRhythmus rhythmus = UstvaRhythmus::Vierteljaehrlich;

    bool Valid() const;
    Date Start() const;
    Date End() const;

    // "Juli 2026", "3. Quartal 2026", "Jahr 2026".
    std::string Bezeichnung() const;

    // The ELSTER Zeitraum code: "01".."12" for calendar months and "41".."44"
    // for calendar quarters, with the year carried separately.
    // [unverified] - taken from the published coding used by every filing
    // client; it must be checked against the ERiC interface description
    // (Dokumentation/Schnittstellenbeschreibungen) before a real submission.
    std::string ElsterZeitraum() const;

    // Filing deadline: the 10th day after the period ends (§ 18 Abs. 1 UStG),
    // one month later with a Dauerfristverlaengerung (§ 46 UStDV), and moved to
    // the next working day when it falls on a Saturday or Sunday
    // (§ 108 Abs. 3 AO). Public holidays are deliberately NOT applied: they
    // differ by Bundesland, so a date that looks authoritative here would be
    // wrong for some users. The UI shows the deadline as the earliest it can
    // be, never as a promise.
    Date Deadline(bool dauerfristverlaengerung) const;

    bool operator==(const Voranmeldungszeitraum& o) const {
        return year == o.year && period == o.period && rhythmus == o.rhythmus;
    }
};

// The period a date is reported in, for a given rhythm.
Voranmeldungszeitraum ZeitraumOf(const Date& date, UstvaRhythmus rhythmus);

// Every reporting period that overlaps a fiscal year - which for a non-calendar
// Geschaeftsjahr spans two calendar years and therefore includes periods that
// also belong to the neighbouring fiscal years.
std::vector<Voranmeldungszeitraum> ZeitraeumeInGeschaeftsjahr(const Geschaeftsjahr& jahr,
                                                              UstvaRhythmus rhythmus);

// ===== OSS =====
// The One-Stop-Shop return is always a calendar quarter, whatever the UStVA
// rhythm and whatever the Geschaeftsjahr, and is due at the end of the month
// following the quarter (not the 10th, and no Dauerfristverlaengerung exists
// for it).
struct OssQuartal {
    int year    = 0;
    int quarter = 0;   // 1..4

    bool        Valid() const { return year >= 2021 && quarter >= 1 && quarter <= 4; }
    Date        Start() const;
    Date        End() const;
    Date        Deadline() const;
    std::string Bezeichnung() const;   // "Q3 2026"
};

OssQuartal OssQuartalOf(const Date& date);

} // namespace UltraFIBU
