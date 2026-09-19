// Tests/UltraFIBU/UltraFIBUEngineTests.cpp
// Unit tests for the UltraFIBU engine: the calendar, the fiscal year, the two
// reporting calendars, VAT-number validation, the data-file readers and the
// store. Self-contained (no test framework) and headless - the engine has no
// UI, which is the point of splitting it out.
//
// What is deliberately covered, because each one is a requirement rather than
// an implementation detail:
//
//  - a **Geschaeftsjahr starting 1 April**: its periods, its Rumpfjahr, the
//    overlap and gap rules, and the fact that its reporting periods belong to
//    two calendar years (the two-calendars property from the design proposal);
//  - **Festschreibung moves only forward** - the GoBD rule that a correction is
//    a Storno and never an un-freeze;
//  - **gap-free document numbering** allocated from the database, including the
//    yearly reset;
//  - **optimistic locking**: saving a row somebody else changed is refused, not
//    silently applied;
//  - **role permissions enforced in the store**, not only in the UI;
//  - **person-account ranges derived from the Sachkontenlaenge**, never
//    hard-coded;
//  - the shipped SKR03 and Steuerschluessel files load, and the tax keys carry
//    no UStVA Kennzahl that has not been verified;
//  - **tax computed per rate rather than per position**, so three lines of
//    33,33 EUR at 19 % owe 19,00 and not 18,99;
//  - a posted invoice **balances** once the automatic tax posting is expanded,
//    and still balances after a payment and after a Storno;
//  - **the journal is append-only**: a posted document refuses every change and
//    is corrected by a reversal with Soll and Haben exchanged;
//  - **the hash chain detects a row edited or deleted behind the application's
//    back** - the test does both with plain SQL, because a chain nothing checks
//    is decoration.
//
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUDate.h"
#include "UltraFIBUGeschaeftsjahr.h"
#include "UltraFIBUKontenrahmen.h"
#include "UltraFIBUStore.h"
#include "UltraFIBUTypes.h"
#include "UltraFIBUUstIdNr.h"
#include "UltraFIBUUstIdNrOnline.h"

#include <UltraCrypt/UltraCryptCore.h>
// The hash-chain test edits a posting the way somebody with the database
// file and a SQL prompt would - which is the only way to prove the chain
// notices. Nothing else in the engine writes SQL outside the store.
#include <UltraDatabase/UltraDatabaseQuery.h>

#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace UltraFIBU;

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

static void CheckInt(int64_t actual, int64_t expected, const std::string& what) {
    ++g_checks;
    if (actual != expected) {
        ++g_failures;
        std::printf("  FAIL: %s\n    expected %lld\n    got      %lld\n",
                    what.c_str(), static_cast<long long>(expected),
                    static_cast<long long>(actual));
    }
}

// A store call that must succeed, with its German message printed when it does not.
static bool CheckStore(const StoreResult& result, const std::string& what) {
    ++g_checks;
    if (!result.ok) {
        ++g_failures;
        std::printf("  FAIL: %s\n    %s\n", what.c_str(), result.fehler.c_str());
    }
    return result.ok;
}

// A store call that must be refused - and say why in German, because that text
// goes in front of the user.
static void CheckRefused(const StoreResult& result, const std::string& what) {
    ++g_checks;
    if (result.ok) {
        ++g_failures;
        std::printf("  FAIL: %s (the call succeeded but should have been refused)\n",
                    what.c_str());
    } else if (result.fehler.empty()) {
        ++g_failures;
        std::printf("  FAIL: %s (refused, but with no message for the user)\n", what.c_str());
    }
}

// ---- 1. Date ---------------------------------------------------------------

static void TestDate() {
    std::printf("Date\n");

    Check(Date(2026, 4, 1).Valid(), "1 April 2026 is a date");
    Check(!Date(2026, 2, 30).Valid(), "30 February is not");
    Check(!Date(2026, 13, 1).Valid(), "month 13 is not");
    Check(Date(2024, 2, 29).Valid(), "2024 is a leap year");
    Check(!Date(2026, 2, 29).Valid(), "2026 is not");
    Check(Date::IsLeapYear(2000) && !Date::IsLeapYear(1900), "the century rule");

    CheckText(Date(2026, 4, 1).ToIso(), "2026-04-01", "ISO output");
    CheckText(FormatDateGerman(Date(2026, 4, 1)), "01.04.2026", "German output");
    CheckText(FormatDateDatev(Date(2026, 4, 1)), "0104", "DATEV TTMM output");
    CheckText(FormatDateCompact(Date(2026, 4, 1)), "20260401", "compact output for a DATEV header");
    CheckText(Date().ToIso(), "", "an invalid date formats as empty, never as a real date");

    Date parsed;
    Check(Date::TryParseIso("2026-04-01", parsed) && parsed == Date(2026, 4, 1), "ISO input");
    Check(Date::TryParseIso("20260401", parsed) && parsed == Date(2026, 4, 1),
          "the compact form DATEV headers use");
    Check(!Date::TryParseIso("2026-02-30", parsed), "an impossible ISO date is refused");
    Check(TryParseDateGerman("01.04.2026", parsed) && parsed == Date(2026, 4, 1), "German input");
    Check(TryParseDateGerman("1.4.2026", parsed) && parsed == Date(2026, 4, 1),
          "German input without padding");
    Check(TryParseDateGerman("1.4.26", parsed) && parsed == Date(2026, 4, 1),
          "a two-digit year in this century");
    Check(TryParseDateGerman("1.4.95", parsed) && parsed == Date(1995, 4, 1),
          "a two-digit year in the last one");
    Check(TryParseDateGerman("2026-04-01", parsed) && parsed == Date(2026, 4, 1),
          "an ISO date in a German field");
    Check(!TryParseDateGerman("kein Datum", parsed), "text is refused");

    // Epoch day: a known anchor, and a full round-trip.
    CheckInt(Date(1970, 1, 1).ToEpochDay(), 0, "the epoch is day zero");
    CheckInt(Date(2000, 3, 1).ToEpochDay(), 11017, "a known day number");
    bool roundTrips = true;
    for (int64_t day = -20000; day <= 30000; day += 7) {
        const Date date = Date::FromEpochDay(day);
        if (!date.Valid() || date.ToEpochDay() != day) { roundTrips = false; break; }
    }
    Check(roundTrips, "every date round-trips through its epoch day");

    CheckInt(Date(2026, 1, 1).DaysUntil(Date(2027, 1, 1)), 365, "2026 has 365 days");
    CheckInt(Date(2024, 1, 1).DaysUntil(Date(2025, 1, 1)), 366, "2024 has 366");

    // Month arithmetic clamps rather than overflowing into the next month.
    Check(Date(2026, 1, 31).AddMonths(1) == Date(2026, 2, 28),
          "31 January plus a month is 28 February");
    Check(Date(2024, 1, 31).AddMonths(1) == Date(2024, 2, 29), "and 29 in a leap year");
    Check(Date(2026, 12, 15).AddMonths(1) == Date(2027, 1, 15), "across a year boundary");
    Check(Date(2026, 1, 15).AddMonths(-1) == Date(2025, 12, 15), "backwards too");
    Check(Date(2026, 4, 15).LastOfMonth() == Date(2026, 4, 30), "last of month");
    Check(Date(2026, 4, 15).FirstOfMonth() == Date(2026, 4, 1), "first of month");
}

// ---- 2. Geschaeftsjahr -----------------------------------------------------

static void TestGeschaeftsjahr() {
    std::printf("Gesch\xC3\xA4""ftsjahr (fiscal year)\n");

    // The requirement: a year starting 1 April is an ordinary year.
    const Geschaeftsjahr april = MakeGeschaeftsjahr(Date(2026, 4, 1));
    Check(april.Valid(), "a 1 April fiscal year is valid");
    Check(april.beginn == Date(2026, 4, 1), "it begins on 1 April 2026");
    Check(april.ende == Date(2027, 3, 31), "and ends on 31 March 2027");
    CheckInt(april.PeriodCount(), 12, "twelve periods");
    Check(!april.IsRumpfjahr(), "not a short year");
    CheckText(april.DefaultBezeichnung(), "2026/2027", "it is labelled across two years");

    CheckInt(april.PeriodOf(Date(2026, 4, 1)), 1, "April is period 1");
    CheckInt(april.PeriodOf(Date(2026, 4, 30)), 1, "the end of April still is");
    CheckInt(april.PeriodOf(Date(2026, 12, 31)), 9, "December is period 9");
    CheckInt(april.PeriodOf(Date(2027, 1, 1)), 10, "January is period 10, not period 1");
    CheckInt(april.PeriodOf(Date(2027, 3, 31)), 12, "March is the last period");
    CheckInt(april.PeriodOf(Date(2027, 4, 1)), 0, "the next April is outside the year");
    CheckInt(april.PeriodOf(Date(2026, 3, 31)), 0, "and so is the day before it starts");
    Check(april.PeriodStart(1) == Date(2026, 4, 1) && april.PeriodEnd(1) == Date(2026, 4, 30),
          "period 1 spans April");
    Check(april.PeriodStart(10) == Date(2027, 1, 1) && april.PeriodEnd(10) == Date(2027, 1, 31),
          "period 10 spans January");
    Check(!april.PeriodStart(13).Valid(), "there is no period 13");
    CheckText(april.PeriodName(1), "April 2026", "periods are named by their month");

    // A calendar year still works, and is labelled with one year.
    const Geschaeftsjahr kalender = MakeGeschaeftsjahr(Date(2026, 1, 1));
    Check(kalender.ende == Date(2026, 12, 31), "a calendar year ends on 31 December");
    CheckText(kalender.DefaultBezeichnung(), "2026", "and is labelled with one year");
    CheckInt(kalender.PeriodOf(Date(2026, 1, 15)), 1, "January is period 1 there");

    // A Rumpfwirtschaftsjahr - what a changeover produces.
    const Geschaeftsjahr rumpf = MakeRumpfjahr(Date(2026, 1, 1), Date(2026, 3, 31));
    Check(rumpf.Valid(), "a three-month Rumpfjahr is valid");
    CheckInt(rumpf.PeriodCount(), 3, "three periods");
    Check(rumpf.IsRumpfjahr(), "and it knows it is short");
    CheckInt(rumpf.PeriodOf(Date(2026, 3, 15)), 3, "March is its last period");
    CheckInt(rumpf.PeriodOf(Date(2026, 4, 1)), 0, "April is outside it");

    // Whole months only: a year that starts mid-month would produce half
    // periods in every report.
    Geschaeftsjahr krumm;
    krumm.beginn = Date(2026, 4, 15);
    krumm.ende   = Date(2027, 4, 14);
    Check(!krumm.Valid(), "a year starting mid-month is refused");
    Geschaeftsjahr zulang;
    zulang.beginn = Date(2026, 1, 1);
    zulang.ende   = Date(2027, 12, 31);
    Check(!zulang.Valid(), "a two-year year is refused");

    // Sequence rules.
    const Geschaeftsjahr folge = NextGeschaeftsjahr(april);
    Check(folge.beginn == Date(2027, 4, 1) && folge.ende == Date(2028, 3, 31),
          "the next year follows seamlessly");
    Check(GeschaeftsjahreContiguous(april, folge), "and is contiguous");
    Check(!GeschaeftsjahreOverlap(april, folge), "and does not overlap");
    Check(GeschaeftsjahreOverlap(april, MakeGeschaeftsjahr(Date(2026, 7, 1))),
          "a year starting inside another one overlaps");
    Check(!GeschaeftsjahreContiguous(april, MakeGeschaeftsjahr(Date(2027, 5, 1))),
          "a year starting a month late leaves a gap");

    // Festschreibung.
    Geschaeftsjahr frozen = april;
    frozen.festschreibungBis = Date(2026, 6, 30);
    Check(frozen.Valid(), "a freeze date inside the year is valid");
    Check(frozen.IsFrozen(Date(2026, 5, 15)), "May is frozen");
    Check(frozen.IsFrozen(Date(2026, 6, 30)), "the freeze date itself is frozen");
    Check(!frozen.IsFrozen(Date(2026, 7, 1)), "July is not");
    frozen.festschreibungBis = Date(2028, 1, 1);
    Check(!frozen.Valid(), "a freeze date outside the year is refused");
}

// ---- 3. The tax calendar ---------------------------------------------------

static void TestSteuerkalender() {
    std::printf("Voranmeldungszeitraum (tax calendar)\n");

    const Voranmeldungszeitraum juli = ZeitraumOf(Date(2026, 7, 15), UstvaRhythmus::Monatlich);
    CheckInt(juli.period, 7, "15 July is reported in month 7");
    Check(juli.Start() == Date(2026, 7, 1) && juli.End() == Date(2026, 7, 31), "July spans July");
    CheckText(juli.Bezeichnung(), "Juli 2026", "named in German");
    CheckText(juli.ElsterZeitraum(), "07", "the ELSTER code for a month is its number");

    const Voranmeldungszeitraum q3 = ZeitraumOf(Date(2026, 7, 15),
                                                UstvaRhythmus::Vierteljaehrlich);
    CheckInt(q3.period, 3, "15 July is in Q3");
    Check(q3.Start() == Date(2026, 7, 1) && q3.End() == Date(2026, 9, 30), "Q3 spans three months");
    CheckText(q3.Bezeichnung(), "3. Quartal 2026", "named in German");
    CheckText(q3.ElsterZeitraum(), "43", "the ELSTER code for Q3 is 43");
    CheckText(ZeitraumOf(Date(2026, 1, 5), UstvaRhythmus::Vierteljaehrlich).ElsterZeitraum(),
              "41", "and for Q1 it is 41");

    // Deadlines: the 10th, a month later with a Dauerfristverlaengerung, and
    // never on a weekend.
    const Voranmeldungszeitraum juni = ZeitraumOf(Date(2026, 6, 1), UstvaRhythmus::Monatlich);
    Check(juni.Deadline(false) == Date(2026, 7, 10), "June is due on 10 July 2026 (a Friday)");
    Check(juni.Deadline(true) == Date(2026, 8, 10),
          "with a Dauerfristverlaengerung, a month later");
    // 10 January 2027 is a Sunday, so the deadline moves to Monday the 11th.
    const Voranmeldungszeitraum dezember = ZeitraumOf(Date(2026, 12, 1), UstvaRhythmus::Monatlich);
    Check(dezember.Deadline(false) == Date(2027, 1, 11),
          "a deadline on a Sunday moves to the Monday");
    // 10 October 2026 is a Saturday.
    const Voranmeldungszeitraum september = ZeitraumOf(Date(2026, 9, 1), UstvaRhythmus::Monatlich);
    Check(september.Deadline(false) == Date(2026, 10, 12),
          "a deadline on a Saturday moves to the Monday");

    // The two-calendars property: a 1 April fiscal year touches reporting
    // periods in two calendar years, and its Q1/Q4 are shared with the
    // neighbouring fiscal years.
    const Geschaeftsjahr april = MakeGeschaeftsjahr(Date(2026, 4, 1));
    const std::vector<Voranmeldungszeitraum> quartale =
        ZeitraeumeInGeschaeftsjahr(april, UstvaRhythmus::Vierteljaehrlich);
    CheckInt(static_cast<int64_t>(quartale.size()), 4, "four quarters touch the fiscal year");
    Check(quartale[0].year == 2026 && quartale[0].period == 2, "it starts in Q2 2026");
    Check(quartale[3].year == 2027 && quartale[3].period == 1,
          "and ends in Q1 2027 - a quarter that also belongs to the next fiscal year");
    const std::vector<Voranmeldungszeitraum> monate =
        ZeitraeumeInGeschaeftsjahr(april, UstvaRhythmus::Monatlich);
    CheckInt(static_cast<int64_t>(monate.size()), 12, "twelve months touch it");
    Check(monate[0].year == 2026 && monate[0].period == 4, "the first is April 2026");
    Check(monate[11].year == 2027 && monate[11].period == 3, "the last is March 2027");

    // OSS: always a calendar quarter, due at the end of the following month.
    const OssQuartal oss = OssQuartalOf(Date(2026, 8, 20));
    Check(oss.Valid() && oss.quarter == 3 && oss.year == 2026, "August is in OSS Q3");
    Check(oss.Start() == Date(2026, 7, 1) && oss.End() == Date(2026, 9, 30), "Q3 spans Q3");
    Check(oss.Deadline() == Date(2026, 10, 31),
          "the OSS return is due at the end of the following month");
    CheckText(oss.Bezeichnung(), "Q3 2026", "labelled Q3 2026");
    Check(OssQuartalOf(Date(2026, 12, 31)).Deadline() == Date(2027, 1, 31),
          "Q4 is due at the end of January");
}

// ---- 4. USt-IdNr. ----------------------------------------------------------

// No published test vectors are used here on purpose: quoting a number from
// memory and asserting it is valid tests nothing if the memory is wrong. What
// is asserted instead are the *properties* a correct check-digit rule must
// have, which no broken implementation satisfies:
//
//   - for a given prefix, **exactly one** check digit is accepted (a mod-11
//     rule may legitimately accept none, which is also reported);
//   - changing any single digit of an accepted number is detected;
//   - transposing two adjacent, different digits is detected.
static void TestUstIdNr() {
    std::printf("USt-IdNr.\n");

    struct Land { const char* land; const char* prefix; size_t bodyLength; };
    // Prefix = the number without its check digit; one check digit is appended.
    const std::vector<Land> laender = {
        { "DE", "13669597", 9 },
        { "AT", "U1234567", 9 },
        { "FI", "1234567",  8 },
        { "EL", "12345678", 9 },
        { "HU", "1234567",  8 },
        { "IT", "1234567890", 11 },
        { "PL", "123456789", 10 },
        { "PT", "12345678", 9 },
        { "SI", "1234567",  8 },
        { "EE", "12345678", 9 },
    };

    for (const Land& land : laender) {
        std::vector<std::string> accepted;
        for (char digit = '0'; digit <= '9'; ++digit) {
            const std::string number = std::string(land.land) + land.prefix + digit;
            const UstIdNrPruefung pruefung = PruefeUstIdNr(number);
            Check(pruefung.normalisiert.size() == land.bodyLength + 2,
                  std::string(land.land) + ": the number has the documented length");
            if (pruefung.status == UstIdNrStatus::PruefzifferOk) accepted.push_back(number);
            else Check(pruefung.status == UstIdNrStatus::PruefzifferFalsch,
                       std::string(land.land) + ": a wrong check digit is reported as such, "
                       "not as a format error");
        }
        // A modulo-11 rule can reject a prefix outright (no digit fits), which
        // is correct behaviour - but never more than one digit may fit.
        Check(accepted.size() <= 1,
              std::string(land.land) + ": at most one check digit is accepted");

        if (accepted.size() == 1) {
            const std::string good = accepted[0];
            // Every single-digit change must be detected.
            bool allDetected = true;
            for (size_t i = 2; i < good.size(); ++i) {
                if (good[i] < '0' || good[i] > '9') continue;
                for (char digit = '0'; digit <= '9'; ++digit) {
                    if (digit == good[i]) continue;
                    std::string broken = good;
                    broken[i] = digit;
                    if (PruefeUstIdNr(broken).status == UstIdNrStatus::PruefzifferOk)
                        allDetected = false;
                }
            }
            Check(allDetected, std::string(land.land) +
                  ": every single mistyped digit is detected");

            // Adjacent transposition - the other common typing error.
            bool transposeDetected = true;
            for (size_t i = 2; i + 1 < good.size(); ++i) {
                if (good[i] == good[i + 1]) continue;
                if (good[i] < '0' || good[i] > '9') continue;
                if (good[i + 1] < '0' || good[i + 1] > '9') continue;
                std::string swapped = good;
                std::swap(swapped[i], swapped[i + 1]);
                if (swapped == good) continue;
                if (PruefeUstIdNr(swapped).status == UstIdNrStatus::PruefzifferOk)
                    transposeDetected = false;
            }
            // Luhn-style rules cannot catch a 09 <-> 90 transposition; mod-11
            // rules catch every one. Reported rather than asserted for the
            // Luhn countries, so the difference is visible instead of hidden.
            if (!transposeDetected)
                std::printf("    note: %s does not detect every adjacent transposition "
                            "(expected for Luhn-style rules)\n", land.land);
        }
    }

    // Formats, countries and normalisation.
    CheckText(NormalisiereUstIdNr(" de 123.456.789 "), "DE123456789", "normalisation");
    Check(PruefeUstIdNr("").status == UstIdNrStatus::Leer, "empty input says so");
    Check(PruefeUstIdNr("XX123456789").status == UstIdNrStatus::LandUnbekannt,
          "an unknown country prefix is reported");
    Check(PruefeUstIdNr("US123456789").status == UstIdNrStatus::LandUnbekannt,
          "a non-EU country is not an EU VAT number");
    Check(PruefeUstIdNr("DE12345").status == UstIdNrStatus::FormatUngueltig,
          "a German number of the wrong length is a format error");
    Check(PruefeUstIdNr("DE12345678A").status == UstIdNrStatus::FormatUngueltig,
          "a letter in a German number is a format error");
    // The Netherlands relaxed its rule, so format-only is the honest answer.
    Check(PruefeUstIdNr("NL123456789B01").status == UstIdNrStatus::FormatOk,
          "a Dutch number is accepted on format alone");
    Check(PruefeUstIdNr("NL123456789X01").status == UstIdNrStatus::FormatUngueltig,
          "but its shape is still checked");
    // Spain mixes letters and digits by several schemes, so the shape is all
    // that is checked - but it is checked: nine characters, digits in the
    // middle.
    Check(PruefeUstIdNr("ES12345678Z").status == UstIdNrStatus::FormatOk,
          "a well-shaped Spanish number is accepted on format alone");
    Check(PruefeUstIdNr("ES1234567Z").status == UstIdNrStatus::FormatUngueltig,
          "a Spanish number of the wrong length is refused");
    Check(PruefeUstIdNr("ES12X45678Z").status == UstIdNrStatus::FormatUngueltig,
          "and so is one with a letter in the middle");
    Check(PruefeUstIdNr("XI123456789").Plausibel(),
          "Northern Ireland stays in the EU VAT area for goods");
    Check(!PruefeUstIdNr("GB123456789").Plausibel(), "Great Britain does not");
    Check(IstEuLand("DE") && IstEuLand("EL") && IstEuLand("GR"),
          "Greece counts under both its codes");
    Check(!IstEuLand("CH") && !IstEuLand("US") && !IstEuLand("GB"),
          "third countries are not EU countries");
    CheckInt(static_cast<int64_t>(EuLaender().size()), 27, "there are 27 member states");

    // Every verdict carries a German sentence for the UI.
    bool allExplained = true;
    const char* const samples[] = { "", "XX1", "DE12345", "NL123456789B01", "DE136695976" };
    for (const char* sample : samples) {
        const UstIdNrPruefung pruefung = PruefeUstIdNr(sample);
        if (pruefung.hinweis.empty()) allExplained = false;
    }
    Check(allExplained, "every verdict explains itself in German");
}

// ---- 5. Data files ---------------------------------------------------------

static void TestDatenDateien() {
    std::printf("data files (SKR03, Steuerschl\xC3\xBCssel)\n");

    const std::string skrPath = FindeDatenDatei("SKR03.csv");
    if (skrPath.empty()) {
        std::printf("    SKIP: SKR03.csv not found next to the test binary "
                    "(set ULTRAFIBU_DATA_DIR)\n");
    } else {
        std::vector<Konto> konten;
        const LadeErgebnis ergebnis = LadeKontenrahmen(skrPath, "SKR03", konten);
        Check(ergebnis.ok, "the shipped SKR03 chart loads");
        Check(ergebnis.warnungen.empty(), "and loads without warnings");
        Check(konten.size() > 50, "it carries a usable number of accounts");
        for (const std::string& warnung : ergebnis.warnungen)
            std::printf("    warning: %s\n", warnung.c_str());

        // Every account is complete enough to post on, and the numbers are unique.
        std::set<std::string> nummern;
        bool complete = true, unique = true, labelled = true;
        for (const Konto& konto : konten) {
            if (!konto.Valid()) complete = false;
            if (!nummern.insert(konto.nummer).second) unique = false;
            if (konto.skr != "SKR03") labelled = false;
        }
        Check(complete, "every account has a number and a name");
        Check(unique, "account numbers are unique");
        Check(labelled, "every row records the chart it came from");

        // The accounts a bookkeeping application cannot work without.
        const char* const kMustExist[] = { "1000", "1200", "1400", "1600", "1576", "1776",
                                           "8400", "8300", "8125", nullptr };
        bool allThere = true;
        for (const char* const* nummer = kMustExist; *nummer; ++nummer)
            if (!nummern.count(*nummer)) {
                allThere = false;
                std::printf("    missing account %s\n", *nummer);
            }
        Check(allThere, "the essential SKR03 accounts are present");
    }

    const std::string steuerPath = FindeDatenDatei("Steuerschluessel.csv");
    if (steuerPath.empty()) {
        std::printf("    SKIP: Steuerschluessel.csv not found next to the test binary\n");
        return;
    }
    std::vector<Steuerschluessel> schluessel;
    const LadeErgebnis ergebnis = LadeSteuerschluesselDatei(steuerPath, schluessel);
    Check(ergebnis.ok, "the shipped tax keys load");
    Check(ergebnis.warnungen.empty(), "and load without warnings");
    for (const std::string& warnung : ergebnis.warnungen)
        std::printf("    warning: %s\n", warnung.c_str());
    Check(schluessel.size() >= 10, "there are enough of them to post with");

    // The rates and the arithmetic they imply.
    bool found19 = false, found7 = false;
    for (const Steuerschluessel& key : schluessel) {
        if (key.schluessel == "USt19") {
            found19 = true;
            CheckInt(key.satzPromille, 190, "USt19 is 19 %");
            CheckText(key.kzBemessung, "81", "and reports its base in Kennzahl 81");
            CheckInt(key.SteuerAufNetto(Money::FromMinor(10000)).Minor(), 1900,
                     "19 % of 100,00 EUR is 19,00 EUR");
            CheckInt(key.SteuerImBrutto(Money::FromMinor(11900)).Minor(), 1900,
                     "and 119,00 EUR gross contains 19,00 EUR");
        }
        if (key.schluessel == "USt7") {
            found7 = true;
            CheckInt(key.satzPromille, 70, "USt7 is 7 %");
            CheckText(key.kzBemessung, "86", "and reports in Kennzahl 86");
        }
    }
    Check(found19 && found7, "the two domestic rates are there");

    // Only UStVA Kennzahlen that have actually been verified may ship. A
    // guessed Kennzahl produces a wrong return; an empty one is caught when
    // the return is built. This test is what keeps that promise honest.
    const std::set<std::string> verified = { "81", "86", "87", "41", "66", "45" };
    bool onlyVerified = true;
    for (const Steuerschluessel& key : schluessel) {
        for (const std::string& kz : { key.kzBemessung, key.kzSteuer }) {
            if (kz.empty()) continue;
            if (!verified.count(kz)) {
                onlyVerified = false;
                std::printf("    unverified Kennzahl \"%s\" on key %s\n",
                            kz.c_str(), key.schluessel.c_str());
            }
        }
    }
    Check(onlyVerified, "no unverified UStVA Kennzahl is shipped");

    // Validity: the shipped set is valid in 2026 and not before.
    int gueltig2026 = 0, gueltig2025 = 0;
    for (const Steuerschluessel& key : schluessel) {
        if (key.GueltigAm(Date(2026, 6, 15))) ++gueltig2026;
        if (key.GueltigAm(Date(2025, 6, 15))) ++gueltig2025;
    }
    CheckInt(gueltig2026, static_cast<int64_t>(schluessel.size()),
             "every shipped key is valid in 2026");
    CheckInt(gueltig2025, 0, "and none of them claims to be valid in 2025");
}

// ---- 5b. Online confirmation (VIES / BZSt) ---------------------------------
// The transport is injected, so these tests feed the readers recorded authority
// answers - including the unhappy ones, which is where a validator earns its
// keep: "the member state is not answering" must never look like "this
// customer's number is invalid".

static void TestBestaetigung() {
    std::printf("USt-IdNr. online (VIES / BZSt)\n");

    // URL and body construction.
    CheckText(ViesUrl("DE", "136695976"),
              "https://ec.europa.eu/taxation_customs/vies/rest-api/ms/DE/vat/136695976",
              "the simple VIES enquiry URL");
    Bestaetigungsanfrage anfrage;
    anfrage.ustIdNr       = "sk 2022513009";
    anfrage.eigeneUstIdNr = "DE136695976";
    const std::string body = ViesPostBody(anfrage);
    Check(body.find("\"countryCode\":\"SK\"") != std::string::npos,
          "the country code is normalised and uppercased");
    Check(body.find("\"vatNumber\":\"2022513009\"") != std::string::npos,
          "the number is sent without its prefix");
    Check(body.find("\"requesterMemberStateCode\":\"DE\"") != std::string::npos &&
          body.find("\"requesterNumber\":\"136695976\"") != std::string::npos,
          "and the enquirer's own number is sent, which is what earns a consultation number");
    Bestaetigungsanfrage ohneEigene;
    ohneEigene.ustIdNr = "GR123456789";
    Check(ViesPostBody(ohneEigene).find("\"countryCode\":\"EL\"") != std::string::npos,
          "Greece is sent as EL, which is what VIES expects");
    Check(ViesPostBody(ohneEigene).find("requesterNumber") == std::string::npos,
          "and no requester is sent when there is none");

    // A confirmed number.
    const std::string gueltigJson =
        "{\"isValid\":true,\"requestDate\":\"2026-06-12+02:00\","
        "\"userError\":\"VALID\",\"name\":\"BUNDESZENTRALAMT FUER STEUERN\","
        "\"address\":\"An der Kueppe 1\\n53225 Bonn\","
        "\"requestIdentifier\":\"WAPIAAAAW1234567\",\"vatNumber\":\"136695976\","
        "\"countryCode\":\"DE\"}";
    const Bestaetigung gueltig = ViesAntwortLesen(gueltigJson, "DE", "136695976");
    Check(gueltig.ok, "a VIES answer is read");
    Check(gueltig.gueltig, "and reports the number as valid");
    CheckText(gueltig.name, "BUNDESZENTRALAMT FUER STEUERN", "with the registered name");
    CheckText(gueltig.anfrageId, "WAPIAAAAW1234567", "and the consultation number");
    Check(gueltig.anfrageDatum == Date(2026, 6, 12),
          "the request date is read despite its timezone suffix");
    CheckText(gueltig.protokoll, gueltigJson,
              "and the response is kept verbatim - that is the evidence");
    Check(!gueltig.IstQualifiziert(),
          "a VIES answer is never a qualified confirmation, however positive");
    Check(gueltig.Zusammenfassung().find("BZSt") != std::string::npos,
          "and its summary says so, so nobody relies on it for § 6a");

    // A number that is not registered.
    const Bestaetigung ungueltig =
        ViesAntwortLesen("{\"isValid\":false,\"requestDate\":\"2026-06-12\"}", "DE", "999999999");
    Check(ungueltig.ok && !ungueltig.gueltig, "an unregistered number is reported as invalid");
    Check(ungueltig.Zusammenfassung().find("nicht") != std::string::npos,
          "and said so in German");

    // The distinction that matters: a service outage is not an invalid number.
    const Bestaetigung ausfall =
        ViesAntwortLesen("{\"isValid\":false,\"userError\":\"MS_UNAVAILABLE\"}", "SK", "2022513009");
    Check(!ausfall.ok, "an unavailable member state is a failed enquiry");
    Check(!ausfall.gueltig, "and claims nothing about the number");
    Check(ausfall.fehler.find("später") != std::string::npos,
          "with advice to try again later");
    const Bestaetigung eingabe =
        ViesAntwortLesen("{\"userError\":\"INVALID_INPUT\"}", "DE", "1");
    Check(!eingabe.ok && !eingabe.fehler.empty(), "INVALID_INPUT is reported as a failure");
    const Bestaetigung kaputt = ViesAntwortLesen("<html>gateway timeout</html>", "DE", "136695976");
    Check(!kaputt.ok, "a non-JSON answer is a failure, not a verdict");
    CheckText(kaputt.protokoll, "<html>gateway timeout</html>",
              "and is still kept, because it explains the failure");

    // Undisclosed names come back as "---" and are not data.
    const Bestaetigung anonym =
        ViesAntwortLesen("{\"isValid\":true,\"name\":\"---\",\"address\":\"---\"}", "IT", "1");
    Check(anonym.gueltig && anonym.name.empty() && anonym.adresse.empty(),
          "a member state that discloses no name leaves the fields empty, not \"---\"");

    // The whole enquiry through an injected transport.
    std::string requestedUrl;
    std::string requestedBody;
    const HttpAnfrage fakeHttp = [&](const std::string& url, const std::string& post,
                                     HttpAntwort& out) {
        requestedUrl = url;
        requestedBody = post;
        out.status = 200;
        out.body = "{\"isValid\":true,\"requestDate\":\"2026-06-12\","
                   "\"requestIdentifier\":\"WAPIAAAAW7654321\"}";
        return true;
    };
    Bestaetigungsanfrage echt;
    echt.ustIdNr       = "DE136695976";
    echt.eigeneUstIdNr = "DE136695976";
    const Bestaetigung durchgefuehrt = PruefeUstIdNrVies(echt, fakeHttp);
    Check(durchgefuehrt.ok && durchgefuehrt.gueltig, "an enquiry with a transport works");
    CheckText(requestedUrl, ViesPostUrl(),
              "with a requester number it uses the POST endpoint");
    Check(!requestedBody.empty(), "and sends a body");

    Bestaetigungsanfrage einfach;
    einfach.ustIdNr = "DE136695976";
    PruefeUstIdNrVies(einfach, fakeHttp);
    Check(requestedUrl.find("/ms/DE/vat/") != std::string::npos,
          "without one it uses the simple GET endpoint");

    // A number that cannot be right costs no request at all.
    bool called = false;
    const HttpAnfrage countingHttp = [&](const std::string&, const std::string&,
                                         HttpAntwort&) { called = true; return true; };
    Bestaetigungsanfrage unsinn;
    unsinn.ustIdNr = "DE123";
    const Bestaetigung refused = PruefeUstIdNrVies(unsinn, countingHttp);
    Check(!refused.ok && !called,
          "a malformed number is refused offline, without troubling the authority");

    // An unreachable service.
    const HttpAnfrage brokenHttp = [](const std::string&, const std::string&,
                                      HttpAntwort& out) {
        out.fehler = "Verbindung fehlgeschlagen";
        return false;
    };
    const Bestaetigung unreachable = PruefeUstIdNrVies(echt, brokenHttp);
    Check(!unreachable.ok && !unreachable.gueltig, "an unreachable service claims nothing");
    CheckText(unreachable.fehler, "Verbindung fehlgeschlagen", "and reports the transport error");
    const Bestaetigung noTransport = PruefeUstIdNrVies(echt, HttpAnfrage());
    Check(!noTransport.ok && !noTransport.fehler.empty(),
          "and so does a missing transport");

    // --- BZSt: the qualified confirmation ---
    const std::string qualifiziert =
        "{\"statusCode\":\"200\",\"anfrageId\":\"2026-0000123\",\"datum\":\"2026-06-12\","
        "\"firmenname\":\"olonda s.r.o.\",\"ergFirmenname\":\"A\",\"ergStrasse\":\"A\","
        "\"ergPlz\":\"A\",\"ergOrt\":\"A\"}";
    const Bestaetigung bzst = EvatrAntwortLesen(qualifiziert, "SK", "2022513009");
    Check(bzst.ok && bzst.gueltig, "a BZSt answer is read");
    Check(bzst.IstQualifiziert(), "and with every detail confirmed it is qualified");
    Check(bzst.Zusammenfassung().find("Qualifiziert") != std::string::npos,
          "which the summary states plainly");

    const std::string teilweise =
        "{\"statusCode\":\"200\",\"ergFirmenname\":\"A\",\"ergStrasse\":\"B\","
        "\"ergPlz\":\"A\",\"ergOrt\":\"A\"}";
    const Bestaetigung halb = EvatrAntwortLesen(teilweise, "SK", "2022513009");
    Check(halb.ok && halb.gueltig, "a partly confirmed answer is still an answer");
    Check(!halb.IstQualifiziert(),
          "but one mismatching detail means it is not a qualified confirmation");
    Check(halb.Zusammenfassung().find("§ 6a") != std::string::npos,
          "and the summary says what that costs");

    const Bestaetigung abgelehnt = EvatrAntwortLesen(
        "{\"statusCode\":\"217\",\"statusMeldung\":\"Anfrage nicht auswertbar\"}", "SK", "1");
    Check(!abgelehnt.ok, "a refusal is a refusal");
    CheckText(abgelehnt.fehler, "Anfrage nicht auswertbar", "with the BZSt's own wording");

    // The BZSt confirms foreign numbers for a German enquirer - two cases it
    // cannot answer are refused before a request is made.
    Bestaetigungsanfrage deutsche;
    deutsche.ustIdNr       = "DE136695976";
    deutsche.eigeneUstIdNr = "DE136695976";
    bool bzstCalled = false;
    const HttpAnfrage bzstHttp = [&](const std::string&, const std::string&, HttpAntwort& out) {
        bzstCalled = true;
        out.status = 200;
        out.body = qualifiziert;
        return true;
    };
    const Bestaetigung eigenland = PruefeUstIdNrBzst(deutsche, bzstHttp);
    Check(!eigenland.ok && !bzstCalled,
          "the BZSt is not asked about a German number");
    Bestaetigungsanfrage ohneEigeneNummer;
    ohneEigeneNummer.ustIdNr = "SK2022513009";
    const Bestaetigung ohne = PruefeUstIdNrBzst(ohneEigeneNummer, bzstHttp);
    Check(!ohne.ok && ohne.fehler.find("eigene") != std::string::npos,
          "and not without the enquirer's own number");
    Check(EvatrBody(anfrage).find("firmenname") == std::string::npos,
          "a body without name and address asks only for a simple confirmation");
    Bestaetigungsanfrage mitAdresse = anfrage;
    mitAdresse.name = "olonda s.r.o.";
    mitAdresse.ort  = "Bratislava";
    Check(EvatrBody(mitAdresse).find("firmenname") != std::string::npos,
          "supplying the details is what makes the enquiry qualified");
}

// ---- 6. Store --------------------------------------------------------------

static void TestStore() {
    std::printf("Store\n");

    Store store;
    if (!CheckStore(store.Open("fibu-test", ":memory:"), "an in-memory database opens")) {
        std::printf("    skipping the remaining store tests\n");
        return;
    }
    CheckInt(store.SchemaVersion(), Store::kSchemaVersion,
             "the schema matches the version the code declares");
    Check(!store.SchemaIsNewerThanCode(), "and is not newer than the code");

    // Server mode is prepared but refuses clearly rather than silently falling
    // back to a local file - a multi-user installation that quietly became
    // single-user would be discovered by two people overwriting each other.
    Store server;
    CheckRefused(server.OpenServer("fibu-server", "db.local", 5432, "fibu", "fibu",
                                   "vault:fibu-rw"),
                 "server mode reports that the PostgreSQL driver is not built yet");
    CheckRefused(server.OpenServer("fibu-server", "db.local", 5432, "fibu", "fibu",
                                   "hunter2"),
                 "and a literal password is refused outright");

    // --- the first user, and the roles ---
    Check(!store.HatBenutzer(), "a fresh database has no users");
    Akteur setup;                                  // nobody is logged in yet
    Benutzer admin;
    admin.anmeldename = "erika";
    admin.anzeigename = "Erika Mustermann";
    admin.rolle       = BenutzerRolle::Erfasser;   // deliberately too low
    CheckStore(store.SaveBenutzer(admin, setup), "the first user can be created");
    Check(admin.rolle == BenutzerRolle::Administrator,
          "and is forced to Administrator, so a fresh installation is usable");
    Check(store.HatBenutzer(), "the database now has a user");

    Akteur akteur;
    akteur.benutzerId  = admin.id;
    akteur.anmeldename = admin.anmeldename;
    akteur.rolle       = admin.rolle;

    Benutzer zweiter;
    zweiter.anmeldename = "erika";
    CheckRefused(store.SaveBenutzer(zweiter, akteur), "a duplicate login name is refused");

    Benutzer buchhalter;
    buchhalter.anmeldename = "berta";
    buchhalter.rolle       = BenutzerRolle::Buchhalter;
    CheckStore(store.SaveBenutzer(buchhalter, akteur), "a second user can be created");
    Check(buchhalter.rolle == BenutzerRolle::Buchhalter,
          "and keeps the role it was given, now that an administrator exists");

    // Passwords, if the crypto backend is there.
    if (UltraCrypt_IsAvailable()) {
        CheckRefused(store.SetPasswort(admin.id, "kurz", akteur),
                     "a password under eight characters is refused");
        CheckStore(store.SetPasswort(admin.id, "ein-gutes-Passwort", akteur),
                   "a password can be set");
        Benutzer angemeldet;
        Check(store.Anmelden("erika", "ein-gutes-Passwort", angemeldet) &&
              angemeldet.id == admin.id, "and the user can log in with it");
        Check(!store.Anmelden("erika", "falsch", angemeldet), "a wrong password is refused");
        Check(!store.Anmelden("niemand", "ein-gutes-Passwort", angemeldet),
              "an unknown user is refused the same way");
        Akteur fremder;
        fremder.benutzerId = buchhalter.id;
        fremder.rolle      = BenutzerRolle::Buchhalter;
        CheckRefused(store.SetPasswort(admin.id, "noch-ein-Passwort", fremder),
                     "a Buchhalter may not change somebody else's password");
        CheckStore(store.SetPasswort(buchhalter.id, "mein-eigenes-Passwort", fremder),
                   "but may change their own");
    } else {
        std::printf("    SKIP: UltraCrypt is unavailable, password tests skipped\n");
    }

    // Permissions are enforced in the store, not only in the UI.
    Akteur nurLesen;
    nurLesen.benutzerId = 99;
    nurLesen.rolle      = BenutzerRolle::NurLesen;
    Mandant verboten;
    verboten.name = "Darf nicht";
    CheckRefused(store.SaveMandant(verboten, nurLesen),
                 "a read-only role cannot write master data");
    Check(RolleHatRecht(BenutzerRolle::Steuerberater, Recht::DatevExportieren),
          "a Steuerberater may export to DATEV");
    Check(!RolleHatRecht(BenutzerRolle::Steuerberater, Recht::Buchen),
          "but may not post");
    Check(!RolleHatRecht(BenutzerRolle::Erfasser, Recht::Buchen),
          "and neither may an Erfasser");
    Check(RolleHatRecht(BenutzerRolle::Buchhalter, Recht::Festschreiben) &&
          !RolleHatRecht(BenutzerRolle::Buchhalter, Recht::BenutzerVerwalten),
          "a Buchhalter may freeze a period but not manage users");

    // --- the company ---
    Mandant mandant;
    mandant.name            = "Beispiel GmbH";
    mandant.rechtsform      = "GmbH";
    mandant.ort             = "Olpe";
    mandant.land            = "DE";
    mandant.ustIdNr         = "DE136695976";
    mandant.beraternummer   = "1001";
    mandant.mandantennummer = "456";
    mandant.ustvaRhythmus   = UstvaRhythmus::Vierteljaehrlich;
    mandant.besteuerung     = Besteuerungsart::Sollversteuerung;
    CheckStore(store.SaveMandant(mandant, akteur), "the company can be saved");
    Check(mandant.id > 0, "and receives an id");

    Mandant geladen;
    Check(store.LoadMandant(mandant.id, geladen), "it can be read back");
    CheckText(geladen.name, "Beispiel GmbH", "with its name");
    Check(geladen.besteuerung == Besteuerungsart::Sollversteuerung,
          "and its Besteuerungsart - Soll by default");
    Check(geladen.gewinnermittlung == Gewinnermittlung::EinnahmenUeberschuss,
          "and its Gewinnermittlung");
    Check(geladen.ustvaRhythmus == UstvaRhythmus::Vierteljaehrlich, "and its UStVA rhythm");
    CheckInt(static_cast<int64_t>(store.Mandanten().size()), 1, "and appears in the list");

    // --- the fiscal year, with the rules enforced ---
    Geschaeftsjahr jahr = MakeGeschaeftsjahr(Date(2026, 4, 1));
    jahr.mandantId = mandant.id;
    jahr.skr       = "SKR03";
    CheckStore(store.SaveGeschaeftsjahr(jahr, akteur), "a 1 April fiscal year can be saved");
    CheckText(jahr.bezeichnung, "2026/2027", "and is labelled automatically");

    Geschaeftsjahr ueberlappend = MakeGeschaeftsjahr(Date(2026, 7, 1));
    ueberlappend.mandantId = mandant.id;
    CheckRefused(store.SaveGeschaeftsjahr(ueberlappend, akteur),
                 "an overlapping fiscal year is refused");

    Geschaeftsjahr mitLuecke = MakeGeschaeftsjahr(Date(2027, 5, 1));
    mitLuecke.mandantId = mandant.id;
    CheckRefused(store.SaveGeschaeftsjahr(mitLuecke, akteur),
                 "a fiscal year that leaves a gap is refused");

    Geschaeftsjahr folgejahr = NextGeschaeftsjahr(jahr);
    folgejahr.id = 0;
    CheckStore(store.SaveGeschaeftsjahr(folgejahr, akteur),
               "the seamless next fiscal year is accepted");
    CheckInt(static_cast<int64_t>(store.Geschaeftsjahre(mandant.id).size()), 2,
             "both years are stored");

    Geschaeftsjahr gefunden;
    Check(store.GeschaeftsjahrAt(mandant.id, Date(2027, 1, 15), gefunden) &&
          gefunden.id == jahr.id,
          "January 2027 belongs to the 2026/2027 fiscal year, not to a calendar one");
    CheckInt(gefunden.PeriodOf(Date(2027, 1, 15)), 10, "and is its tenth period");
    Check(store.GeschaeftsjahrAt(mandant.id, Date(2027, 6, 1), gefunden) &&
          gefunden.id == folgejahr.id, "June 2027 belongs to the next one");
    Check(!store.GeschaeftsjahrAt(mandant.id, Date(2020, 1, 1), gefunden),
          "a date before any fiscal year belongs to none");

    // Festschreibung only moves forward - a correction is a Storno, never an
    // un-freeze.
    CheckStore(store.Festschreiben(jahr.id, Date(2026, 6, 30), akteur),
               "a period can be frozen");
    CheckRefused(store.Festschreiben(jahr.id, Date(2026, 5, 31), akteur),
                 "the freeze cannot be moved backwards");
    CheckStore(store.Festschreiben(jahr.id, Date(2026, 9, 30), akteur),
               "but can be moved forwards");
    Geschaeftsjahr eingefroren;
    Check(store.GeschaeftsjahrById(jahr.id, eingefroren) &&
          eingefroren.festschreibungBis == Date(2026, 9, 30), "and the date is stored");
    Check(eingefroren.status == GeschaeftsjahrStatus::Festgeschrieben, "with the status");
    CheckRefused(store.Festschreiben(jahr.id, Date(2027, 12, 31), akteur),
                 "a date outside the fiscal year is refused");
    Akteur erfasser;
    erfasser.benutzerId = 42;
    erfasser.rolle      = BenutzerRolle::Erfasser;
    CheckRefused(store.Festschreiben(jahr.id, Date(2026, 12, 31), erfasser),
                 "an Erfasser may not freeze a period");

    // --- the chart of accounts ---
    const std::string skrPath = FindeDatenDatei("SKR03.csv");
    if (!skrPath.empty()) {
        std::vector<Konto> konten;
        LadeKontenrahmen(skrPath, "SKR03", konten);
        int written = 0;
        CheckStore(store.ImportKonten(mandant.id, konten, akteur, written),
                   "the chart imports into the database");
        CheckInt(written, static_cast<int64_t>(konten.size()), "every row is written");
        CheckInt(static_cast<int64_t>(store.Konten(mandant.id).size()),
                 static_cast<int64_t>(konten.size()), "and can be read back");

        // Importing the same chart again updates rather than duplicating.
        int again = 0;
        CheckStore(store.ImportKonten(mandant.id, konten, akteur, again),
                   "a second import is accepted");
        CheckInt(static_cast<int64_t>(store.Konten(mandant.id).size()),
                 static_cast<int64_t>(konten.size()),
                 "and does not duplicate a single account");

        Konto bank;
        Check(store.KontoByNummer(mandant.id, "1200", bank), "account 1200 is there");
        CheckText(bank.bezeichnung, "Bank", "with its name");
        Check(bank.typ == KontoTyp::Aktiv, "and its type");
        Check(!bank.bilanzPosition.empty(),
              "and a balance-sheet classification, carried from the first import");
    }

    // --- tax keys ---
    const std::string steuerPath = FindeDatenDatei("Steuerschluessel.csv");
    if (!steuerPath.empty()) {
        std::vector<Steuerschluessel> schluessel;
        LadeSteuerschluesselDatei(steuerPath, schluessel);
        int written = 0;
        CheckStore(store.ImportSteuerschluessel(mandant.id, schluessel, akteur, written),
                   "the tax keys import");
        CheckInt(written, static_cast<int64_t>(schluessel.size()), "all of them");

        Steuerschluessel ust19;
        Check(store.SteuerschluesselByKey(mandant.id, "USt19", Date(2026, 6, 1), ust19),
              "USt19 is valid in June 2026");
        CheckInt(ust19.satzPromille, 190, "at 19 %");
        Check(!store.SteuerschluesselByKey(mandant.id, "USt19", Date(2025, 6, 1), ust19),
              "and not in 2025, where a different rate may have applied");
        CheckInt(static_cast<int64_t>(
                     store.SteuerschluesselListe(mandant.id, Date(2025, 1, 1)).size()),
                 0, "no key claims validity before its start date");
        Check(store.SteuerschluesselListe(mandant.id).size() == schluessel.size(),
              "querying without a date returns all of them");
    }

    // --- customers and suppliers ---
    Partner kunde;
    kunde.mandantId       = mandant.id;
    kunde.typ             = PartnerTyp::Kunde;
    kunde.name            = "SEOMATIXX GmbH";
    kunde.ort             = "Berlin";
    kunde.land            = "DE";
    kunde.steuerkategorie = Steuerkategorie::Inland;
    CheckStore(store.SavePartner(kunde, akteur), "a customer can be saved");
    CheckText(kunde.konto, "10000", "and gets the first Debitor account for a 4-digit chart");

    Partner zweiterKunde;
    zweiterKunde.mandantId = mandant.id;
    zweiterKunde.name      = "Andrea del Riva";
    CheckStore(store.SavePartner(zweiterKunde, akteur), "a second customer");
    CheckText(zweiterKunde.konto, "10001", "gets the next number, without a gap");

    Partner lieferant;
    lieferant.mandantId = mandant.id;
    lieferant.typ       = PartnerTyp::Lieferant;
    lieferant.name      = "Richard Evans Acompanado";
    lieferant.land      = "PH";
    lieferant.steuerkategorie = Steuerkategorie::Drittland;
    CheckStore(store.SavePartner(lieferant, akteur), "a third-country supplier can be saved");
    CheckText(lieferant.konto, "70000", "and gets a Kreditor account from the other range");

    // The VAT number is checked, and a contradiction between the number and
    // the tax category is refused rather than posted at 19 %.
    Partner euKunde;
    euKunde.mandantId       = mandant.id;
    euKunde.name            = "olonda s.r.o.";
    euKunde.land            = "SK";
    euKunde.ustIdNr         = "SK2022513009";     // whatever its check digit, the shape is right
    euKunde.steuerkategorie = Steuerkategorie::Inland;
    CheckRefused(store.SavePartner(euKunde, akteur),
                 "an EU VAT number with a domestic tax category is refused");
    euKunde.steuerkategorie = Steuerkategorie::EuUnternehmer;
    const StoreResult euSaved = store.SavePartner(euKunde, akteur);
    if (euSaved.ok) {
        Check(!euKunde.ustIdNrStatus.empty(), "the offline check result is recorded");
    } else {
        // A wrong check digit in the sample is a legitimate refusal; what
        // matters is that the reason is about the number.
        Check(euSaved.fehler.find("USt-IdNr") != std::string::npos,
              "or it is refused with a reason about the VAT number");
    }

    Partner falscheNummer;
    falscheNummer.mandantId = mandant.id;
    falscheNummer.name      = "Tippfehler GmbH";
    falscheNummer.ustIdNr   = "DE12345678";        // eight digits: wrong shape
    CheckRefused(store.SavePartner(falscheNummer, akteur),
                 "a malformed VAT number is refused before it reaches an invoice");

    // Optimistic locking: two people, one row.
    Partner erste;
    Check(store.PartnerById(kunde.id, erste), "the customer can be read back");
    Partner zweite = erste;                        // a second session's copy
    erste.ort = "Hamburg";
    CheckStore(store.SavePartner(erste, akteur), "the first save succeeds");
    zweite.ort = "M\xC3\xBCnchen";
    CheckRefused(store.SavePartner(zweite, akteur),
                 "the second save is refused because the row changed in the meantime");
    Partner nachher;
    store.PartnerById(kunde.id, nachher);
    CheckText(nachher.ort, "Hamburg", "and the first writer's value survived");
    CheckInt(nachher.version, 2, "the version advanced exactly once");

    // An authority's confirmation is stored verbatim and survives a version
    // clash, because an answer from an authority must never be lost to one.
    Bestaetigung bestaetigung;
    bestaetigung.ok        = true;
    bestaetigung.gueltig   = true;
    bestaetigung.quelle    = BestaetigungsQuelle::Vies;
    bestaetigung.land      = "DE";
    bestaetigung.nummer    = "136695976";
    bestaetigung.anfrageId = "WAPIAAAAW1234567";
    bestaetigung.anfrageDatum = Date(2026, 6, 12);
    bestaetigung.protokoll = "{\"isValid\":true,\"requestIdentifier\":\"WAPIAAAAW1234567\"}";
    CheckStore(store.SpeichereUstIdNrBestaetigung(kunde.id, bestaetigung, akteur),
               "a VIES confirmation can be recorded");
    Partner bestaetigt;
    store.PartnerById(kunde.id, bestaetigt);
    CheckText(bestaetigt.ustIdNrProtokoll, bestaetigung.protokoll,
              "and the response is stored byte for byte");
    Check(bestaetigt.ustIdNrStatus.find("vies") != std::string::npos &&
          bestaetigt.ustIdNrStatus.find("gueltig") != std::string::npos,
          "with a status that records which authority answered");
    Check(bestaetigt.ustIdNrGeprueftAm == Date(2026, 6, 12), "and when");
    Bestaetigung fehlgeschlagen;
    fehlgeschlagen.ok = false;
    fehlgeschlagen.fehler = "MS_UNAVAILABLE";
    CheckRefused(store.SpeichereUstIdNrBestaetigung(kunde.id, fehlgeschlagen, akteur),
                 "a failed enquiry is not recorded as a confirmation");

    // Searching and filtering.
    // Three customers by now: the two domestic ones and the Slovak business.
    CheckInt(static_cast<int64_t>(
                 store.PartnerListe(mandant.id, PartnerTyp::Kunde).size()), 3,
             "three customers");
    CheckInt(static_cast<int64_t>(
                 store.PartnerListe(mandant.id, PartnerTyp::Lieferant).size()), 1,
             "one supplier");
    CheckInt(static_cast<int64_t>(
                 store.PartnerListe(mandant.id, PartnerTyp::Kunde, "seomatixx").size()), 1,
             "search is case-insensitive");
    CheckInt(static_cast<int64_t>(
                 store.PartnerListe(mandant.id, PartnerTyp::Kunde, "hamburg").size()), 1,
             "and looks at the town too");
    CheckInt(static_cast<int64_t>(
                 store.PartnerListe(mandant.id, PartnerTyp::Beides, "niemand").size()), 0,
             "a search that matches nothing returns nothing");

    // Person-account ranges follow the Sachkontenlaenge rather than being
    // hard-coded for four digits.
    std::string konto;
    CheckStore(store.NextPersonenkonto(mandant.id, PartnerTyp::Kunde, 5, konto),
               "a 5-digit chart allocates a 6-digit Debitor");
    CheckInt(static_cast<int64_t>(konto.size()), 6, "six digits");
    Check(konto[0] == '1', "starting at 100000");
    CheckStore(store.NextPersonenkonto(mandant.id, PartnerTyp::Lieferant, 6, konto),
               "a 6-digit chart allocates a 7-digit Kreditor");
    CheckInt(static_cast<int64_t>(konto.size()), 7, "seven digits");
    Check(konto[0] == '7', "starting at 7000000");
    CheckRefused(store.NextPersonenkonto(mandant.id, PartnerTyp::Kunde, 3, konto),
                 "an impossible account width is refused");

    // --- document numbers: unique, gap-free, allocated in the database ---
    Nummernkreis kreis;
    kreis.mandantId = mandant.id;
    kreis.kreis     = "rechnung";
    kreis.praefix   = "R-{JJJJ}{MM}";
    kreis.stellen   = 3;
    kreis.naechste  = 1;
    CheckStore(store.SaveNummernkreis(kreis, akteur), "a number range can be defined");

    std::string nummer;
    CheckStore(store.NextBelegnummer(mandant.id, "rechnung", Date(2026, 7, 8), nummer),
               "the first invoice number is allocated");
    CheckText(nummer, "R-202607001", "and rendered from the pattern");
    CheckStore(store.NextBelegnummer(mandant.id, "rechnung", Date(2026, 7, 8), nummer),
               "the second");
    CheckText(nummer, "R-202607002", "follows without a gap");
    CheckStore(store.NextBelegnummer(mandant.id, "rechnung", Date(2026, 8, 1), nummer),
               "a new month");
    CheckText(nummer, "R-202608003", "keeps counting - the counter is not monthly");
    CheckRefused(store.NextBelegnummer(mandant.id, "gutschrift", Date(2026, 8, 1), nummer),
                 "an undefined number range is refused rather than invented");
    CheckRefused(store.NextBelegnummer(mandant.id, "rechnung", Date(), nummer),
                 "and so is a number without a date");

    Nummernkreis jaehrlich;
    jaehrlich.mandantId = mandant.id;
    jaehrlich.kreis     = "beleg";
    jaehrlich.praefix   = "B-{JJ}-";
    jaehrlich.stellen   = 4;
    jaehrlich.naechste  = 1;
    jaehrlich.jaehrlichZuruecksetzen = true;
    CheckStore(store.SaveNummernkreis(jaehrlich, akteur), "a yearly range can be defined");
    CheckStore(store.NextBelegnummer(mandant.id, "beleg", Date(2026, 12, 31), nummer), "1/2026");
    CheckText(nummer, "B-26-0001", "counts from one");
    CheckStore(store.NextBelegnummer(mandant.id, "beleg", Date(2026, 12, 31), nummer), "2/2026");
    CheckText(nummer, "B-26-0002", "and on");
    CheckStore(store.NextBelegnummer(mandant.id, "beleg", Date(2027, 1, 2), nummer), "1/2027");
    CheckText(nummer, "B-27-0001", "and starts at one again in the new year");

    // Sequences hand out each value exactly once.
    std::set<int64_t> values;
    bool allUnique = true;
    for (int i = 0; i < 50; ++i) {
        int64_t value = 0;
        if (!store.NextSequenceValue("test", value)) { allUnique = false; break; }
        if (!values.insert(value).second) allUnique = false;
    }
    Check(allUnique && values.size() == 50, "a sequence never hands out the same value twice");

    // --- the audit trail ---
    const std::vector<Store::AuditEintrag> audit = store.AuditListe();
    Check(!audit.empty(), "everything above left an audit trail");
    bool hasAuthor = true, hasFreeze = false, hasRefusal = false;
    for (const Store::AuditEintrag& eintrag : audit) {
        if (eintrag.zeit == 0) hasAuthor = false;
        if (eintrag.aktion == "festschreiben") hasFreeze = true;
        if (eintrag.aktion == "abgelehnt")     hasRefusal = true;
    }
    Check(hasAuthor, "every entry is timestamped");
    Check(hasFreeze, "the Festschreibung is in it");
    Check(hasRefusal, "and so is the attempt to move it backwards");

    store.Close();
    Check(!store.IsOpen(), "the store closes");
}


// ===== BELEGE UND BUCHUNGEN =====
//
// What this covers, and why each one is a requirement rather than an
// implementation detail:
//
//  - **tax is computed per rate, not per position** - three lines of 33,33 EUR
//    at 19 % owe 19,00 EUR, not 18,99, and the per-line shares must add back up
//    to that figure;
//  - a posted invoice **balances**: the sum of every debit equals the sum of
//    every credit, once the automatic tax posting is expanded;
//  - **the journal is append-only**: a posted document refuses to be saved and
//    is corrected by a Storno that reverses Soll and Haben;
//  - **a frozen period refuses everything** - posting, reversing into it, and
//    paying into it;
//  - **the hash chain detects a row that was edited behind the application's
//    back, and a row that was deleted** - the chain is only worth having if
//    something actually checks it;
//  - **roles are enforced in the store**: an Erfasser may write a document and
//    may not post it.

static void TestBelegeUndBuchungen() {
    std::printf("Belege und Buchungen\n");

    Store store;
    if (!CheckStore(store.Open("fibu-beleg", ":memory:"), "a second in-memory database opens")) {
        std::printf("    skipping the document and journal tests\n");
        return;
    }

    // --- the ground the documents stand on ---
    Akteur setup;
    Benutzer admin;
    admin.anmeldename = "chef";
    CheckStore(store.SaveBenutzer(admin, setup), "an administrator exists");
    Akteur akteur;
    akteur.benutzerId  = admin.id;
    akteur.anmeldename = admin.anmeldename;
    akteur.rolle       = admin.rolle;

    Mandant mandant;
    mandant.name = "Beispiel GmbH";
    CheckStore(store.SaveMandant(mandant, akteur), "a company exists");

    // A fiscal year starting 1 April, because that is the one the request named
    // and because a document dated in March belongs to the *previous* one.
    Geschaeftsjahr jahr;
    jahr.mandantId = mandant.id;
    jahr.beginn    = Date(2026, 4, 1);
    jahr.ende      = Date(2027, 3, 31);
    jahr.bezeichnung = jahr.DefaultBezeichnung();
    CheckStore(store.SaveGeschaeftsjahr(jahr, akteur), "a 1 April fiscal year exists");

    std::vector<Konto> konten;
    auto konto = [&](const std::string& nummer, const std::string& text, KontoTyp typ) {
        Konto k;
        k.mandantId   = mandant.id;
        k.nummer      = nummer;
        k.bezeichnung = text;
        k.typ         = typ;
        konten.push_back(k);
    };
    konto("1200", "Bank", KontoTyp::Aktiv);
    konto("1576", "Abziehbare Vorsteuer 19 %", KontoTyp::Aktiv);
    konto("1776", "Umsatzsteuer 19 %", KontoTyp::Passiv);
    konto("1771", "Umsatzsteuer 7 %", KontoTyp::Passiv);
    konto("4930", "Buerobedarf", KontoTyp::Aufwand);
    konto("8300", "Erloese 7 % USt", KontoTyp::Ertrag);
    konto("8400", "Erloese 19 % USt", KontoTyp::Ertrag);
    int geschrieben = 0;
    CheckStore(store.ImportKonten(mandant.id, konten, akteur, geschrieben),
               "the accounts the tests post to exist");

    std::vector<Steuerschluessel> keys;
    auto key = [&](const std::string& name, int satz, const std::string& steuerkonto,
                   bool vorsteuer) {
        Steuerschluessel k;
        k.mandantId   = mandant.id;
        k.schluessel  = name;
        k.bezeichnung = name;
        k.satzPromille = satz;
        k.kontoSteuer = steuerkonto;
        k.vorsteuer   = vorsteuer;
        k.gueltigVon  = Date(2026, 1, 1);
        keys.push_back(k);
    };
    key("USt19", 190, "1776", false);
    key("USt7",   70, "1771", false);
    key("USt0",    0, "",     false);
    key("VSt19", 190, "1576", true);
    CheckStore(store.ImportSteuerschluessel(mandant.id, keys, akteur, geschrieben),
               "the tax keys the tests use exist");

    Nummernkreis kreis;
    kreis.mandantId = mandant.id;
    kreis.kreis     = "rechnung";
    kreis.praefix   = "R-{JJJJ}-";
    kreis.stellen   = 4;
    CheckStore(store.SaveNummernkreis(kreis, akteur), "an invoice number range exists");

    Partner kunde;
    kunde.mandantId = mandant.id;
    kunde.typ       = PartnerTyp::Kunde;
    kunde.name      = "Muster AG";
    kunde.ort       = "Hamburg";
    kunde.zahlungsfristTage = 14;
    CheckStore(store.SavePartner(kunde, akteur), "a customer exists");
    Check(!kunde.konto.empty(), "and was given a Debitorenkonto");

    // --- costing: per rate, not per position ---
    //
    // Three lines of 33,33 EUR at 19 %. Rounded per line the tax is 6,33 three
    // times over, which is 18,99; the amount actually owed on 99,99 is 19,00.
    // Getting this wrong is a cent per invoice that nothing downstream
    // reconciles, and it is why the tax is taken from the group.
    Beleg dreissig;
    dreissig.mandantId = mandant.id;
    dreissig.art       = BelegArt::Ausgangsrechnung;
    dreissig.datum     = Date(2026, 5, 4);
    dreissig.partnerId = kunde.id;
    dreissig.partnerKonto = kunde.konto;
    dreissig.partnerName  = kunde.name;
    for (int i = 0; i < 3; ++i) {
        BelegPosition pos;
        pos.bezeichnung = "Position";
        pos.einzelpreis = Money::FromMinor(3333, "EUR");
        pos.konto       = "8400";
        pos.steuerschluessel = "USt19";
        dreissig.positionen.push_back(pos);
    }
    CheckStore(store.SaveBeleg(dreissig, "rechnung", akteur),
               "an invoice of three equal lines saves");
    CheckInt(dreissig.netto.Minor(),  9999, "its net is 99,99 EUR");
    CheckInt(dreissig.steuer.Minor(),  1900,
             "its tax is 19,00 EUR - taken from the group, not 18,99 from three rounded lines");
    CheckInt(dreissig.brutto.Minor(), 11899, "and its gross is 118,99 EUR");
    int64_t summeAnteile = 0;
    for (const BelegPosition& pos : dreissig.positionen) summeAnteile += pos.steuer.Minor();
    CheckInt(summeAnteile, dreissig.steuer.Minor(),
             "the per-line tax shares add back up to the invoice tax exactly");
    CheckText(dreissig.nummer, "R-2026-0001", "the number came from the Nummernkreis");
    Check(dreissig.faelligAm == Date(2026, 5, 18),
          "and the due date came from the customer's 14-day terms");
    Check(dreissig.status == BelegStatus::Entwurf, "a new document is a draft");

    // --- a mixed-rate invoice, posted ---
    Beleg rechnung;
    rechnung.mandantId    = mandant.id;
    rechnung.art          = BelegArt::Ausgangsrechnung;
    rechnung.datum        = Date(2026, 6, 15);
    rechnung.partnerId    = kunde.id;
    rechnung.partnerKonto = kunde.konto;
    rechnung.partnerName  = kunde.name;
    rechnung.buchungstext = "Beratung und Buch";
    {
        BelegPosition beratung;
        beratung.bezeichnung = "Beratung";
        beratung.mengeTausendstel = 10000;             // 10 hours
        beratung.einheit     = "Std";
        beratung.einzelpreis = Money::FromMinor(10000, "EUR");   // 100,00 / hour
        beratung.konto       = "8400";
        beratung.steuerschluessel = "USt19";
        rechnung.positionen.push_back(beratung);

        BelegPosition buch;
        buch.bezeichnung = "Fachbuch";
        buch.mengeTausendstel = 2000;                  // 2 copies
        buch.einzelpreis = Money::FromMinor(2000, "EUR");         // 20,00 each
        buch.konto       = "8300";
        buch.steuerschluessel = "USt7";
        rechnung.positionen.push_back(buch);
    }
    CheckStore(store.SaveBeleg(rechnung, "rechnung", akteur), "a mixed-rate invoice saves");
    CheckInt(rechnung.netto.Minor(), 104000, "net 1.040,00 EUR");
    CheckInt(rechnung.steuer.Minor(), 19280, "tax 192,80 EUR = 190,00 at 19 % plus 2,80 at 7 %");
    CheckInt(rechnung.brutto.Minor(), 123280, "gross 1.232,80 EUR");

    CheckStore(store.Buchen(rechnung, akteur), "it posts");
    Check(rechnung.status == BelegStatus::Gebucht, "and is then no longer a draft");

    const std::vector<Buchung> buchungen = store.BuchungenZuBeleg(rechnung.id);
    CheckInt(static_cast<int64_t>(buchungen.size()), 2,
             "it produced two postings - one per tax key, not one per line");
    bool sawNineteen = false, sawSeven = false;
    for (const Buchung& b : buchungen) {
        CheckText(b.konto, kunde.konto, "the Debitorenkonto carries the gross side");
        Check(b.sollHaben == SollHaben::Soll,
              "a receivable is a debit on the person account");
        Check(!b.umsatz.IsNegative(), "and the Umsatz is never negative");
        Check(b.steuerSeite == SteuerSeite::Gegenkonto,
              "the revenue account is the net side");
        CheckText(b.belegfeld1, rechnung.nummer, "the document number is in Belegfeld 1");
        CheckInt(b.periode, 3, "June is period 3 of a year starting 1 April");
        Check(b.belegdatum == rechnung.datum,
              "and the Belegdatum is the document's, for the UStVA calendar");
        CheckInt(b.netto.Minor() + b.steuer.Minor(), b.umsatz.Minor(),
                 "net and tax sum to the posting's Umsatz");
        if (b.gegenkonto == "8400") {
            sawNineteen = true;
            CheckInt(b.netto.Minor(),  100000, "19 % leg: net 1.000,00");
            CheckInt(b.steuer.Minor(),  19000, "19 % leg: tax 190,00");
            CheckText(b.steuerkonto, "1776", "and it posts the tax to 1776");
            CheckInt(b.satzPromille, 190, "the rate is stored with the row, not looked up later");
        }
        if (b.gegenkonto == "8300") {
            sawSeven = true;
            CheckInt(b.netto.Minor(), 4000, "7 % leg: net 40,00");
            CheckInt(b.steuer.Minor(), 280, "7 % leg: tax 2,80");
            CheckText(b.steuerkonto, "1771", "and it posts the tax to 1771");
        }
    }
    Check(sawNineteen && sawSeven, "both rates are in the journal");

    // The one property that makes it a ledger.
    Check(store.Buchungskreisdifferenz(mandant.id).IsZero(),
          "every debit has its credit: the ledger balances");

    // The expanded Saldenliste is where the automatic tax posting becomes
    // visible - the tax account is never typed, so if the expansion is wrong
    // nothing else would show it.
    const std::vector<Store::KontoSaldo> salden = store.SummenUndSalden(mandant.id);
    bool sawUst = false, sawDebitor = false;
    for (const Store::KontoSaldo& k : salden) {
        if (k.konto == "1776") {
            sawUst = true;
            CheckInt(k.haben.Minor(), 19000, "1776 carries the 19 % output tax");
            CheckText(k.bezeichnung, "Umsatzsteuer 19 %", "and is named from the chart");
        }
        if (k.konto == kunde.konto) {
            sawDebitor = true;
            CheckInt(k.soll.Minor(), 123280, "the customer owes the gross 1.232,80");
        }
    }
    Check(sawUst, "the tax account appears in the Saldenliste although nobody posted to it");
    Check(sawDebitor, "and so does the customer account");

    // --- a posted document is immutable ---
    rechnung.notiz = "nachtraeglich geaendert";
    CheckRefused(store.SaveBeleg(rechnung, "rechnung", akteur),
                 "a posted document refuses to be changed - a correction is a Storno");
    CheckRefused(store.DeleteBeleg(rechnung.id, akteur),
                 "and refuses to be deleted, because a gap in the numbers is what an audit asks about");
    CheckRefused(store.Buchen(rechnung, akteur), "and refuses to be posted twice");

    // --- an incoming invoice, the other direction ---
    Partner lieferant;
    lieferant.mandantId = mandant.id;
    lieferant.typ       = PartnerTyp::Lieferant;
    lieferant.name      = "Bueroland GmbH";
    CheckStore(store.SavePartner(lieferant, akteur), "a supplier exists");

    Beleg eingang;
    eingang.mandantId    = mandant.id;
    eingang.art          = BelegArt::Eingangsrechnung;
    eingang.datum        = Date(2026, 6, 20);
    eingang.partnerId    = lieferant.id;
    eingang.partnerKonto = lieferant.konto;
    eingang.partnerName  = lieferant.name;
    eingang.externeNummer = "RE-2026-8891";
    {
        BelegPosition pos;
        pos.bezeichnung = "Papier";
        pos.einzelpreis = Money::FromMinor(5000, "EUR");
        pos.konto       = "4930";
        pos.steuerschluessel = "VSt19";
        eingang.positionen.push_back(pos);
    }
    CheckStore(store.SaveBeleg(eingang, "rechnung", akteur), "an incoming invoice saves");
    CheckStore(store.Buchen(eingang, akteur), "and posts");
    const std::vector<Buchung> eingangBuchungen = store.BuchungenZuBeleg(eingang.id);
    CheckInt(static_cast<int64_t>(eingangBuchungen.size()), 1, "as one posting");
    if (!eingangBuchungen.empty()) {
        Check(eingangBuchungen[0].sollHaben == SollHaben::Haben,
              "a payable is a credit on the person account - the opposite of a receivable");
        CheckText(eingangBuchungen[0].steuerkonto, "1576",
                  "and the input tax goes to 1576, not to an output-tax account");
    }
    Check(store.Buchungskreisdifferenz(mandant.id).IsZero(),
          "the ledger still balances with both directions in it");

    // --- payments ---
    CheckRefused(store.ZahlungErfassen(rechnung.id, Date(2026, 7, 1),
                                       Money::FromMinor(200000, "EUR"), "1200", "", akteur),
                 "a payment larger than the invoice is refused - the usual cause is entering it twice");
    CheckStore(store.ZahlungErfassen(rechnung.id, Date(2026, 7, 1),
                                     Money::FromMinor(23280, "EUR"), "1200", "Anzahlung", akteur),
               "a part payment is accepted");
    Beleg nachZahlung;
    Check(store.BelegById(rechnung.id, nachZahlung), "the invoice reads back");
    Check(nachZahlung.status == BelegStatus::TeilweiseBezahlt, "and is partly paid");
    CheckInt(nachZahlung.Offen().Minor(), 100000, "1.000,00 EUR remain open");

    CheckStore(store.ZahlungErfassen(rechnung.id, Date(2026, 7, 20),
                                     Money::FromMinor(100000, "EUR"), "1200", "", akteur),
               "the rest is accepted");
    Check(store.BelegById(rechnung.id, nachZahlung), "the invoice reads back again");
    Check(nachZahlung.status == BelegStatus::Bezahlt, "and is now settled");
    Check(nachZahlung.Offen().IsZero(), "with nothing open");
    CheckInt(static_cast<int64_t>(store.Zahlungen(rechnung.id).size()), 2,
             "both payments are recorded");
    Check(store.Buchungskreisdifferenz(mandant.id).IsZero(),
          "and the ledger balances after the payments");

    // --- Storno ---
    Beleg storno;
    CheckStore(store.StorniereBeleg(eingang.id, Date(2026, 7, 25), "Falsche Menge",
                                    "rechnung", akteur, storno),
               "a posted document can be reversed");
    CheckInt(storno.brutto.Minor(), -eingang.brutto.Minor(),
             "the reversing document carries the negated total");
    Check(storno.stornoVon == eingang.id, "and points at what it reverses");

    Beleg storniert;
    Check(store.BelegById(eingang.id, storniert), "the original reads back");
    Check(storniert.status == BelegStatus::Storniert, "and is marked storniert");
    Check(storniert.storniertDurch == storno.id, "pointing back at the reversal");

    const std::vector<Buchung> stornoBuchungen = store.BuchungenZuBeleg(storno.id);
    CheckInt(static_cast<int64_t>(stornoBuchungen.size()),
             static_cast<int64_t>(eingangBuchungen.size()),
             "the reversal has one posting per original posting");
    if (!stornoBuchungen.empty() && !eingangBuchungen.empty()) {
        CheckInt(stornoBuchungen[0].umsatz.Minor(), eingangBuchungen[0].umsatz.Minor(),
                 "with the same amount, never a negative one");
        Check(stornoBuchungen[0].sollHaben != eingangBuchungen[0].sollHaben,
              "and Soll and Haben exchanged - which is what a Storno is");
        Check(stornoBuchungen[0].belegdatum == Date(2026, 7, 25),
              "dated when the reversal was made, not when the original was");
    }
    Buchung ursprung;
    if (!eingangBuchungen.empty() && store.BuchungById(eingangBuchungen[0].id, ursprung))
        Check(ursprung.IstStorniert(), "the original posting knows it was reversed");
    Check(store.Buchungskreisdifferenz(mandant.id).IsZero(),
          "and the ledger balances after the reversal");
    CheckRefused(store.StorniereBeleg(eingang.id, Date(2026, 7, 26), "", "rechnung",
                                      akteur, storno),
                 "reversing the same document twice is refused");

    // --- reversing a document that has been paid ---
    //
    // The money arrived and is in the bank. Reversing the invoice must not
    // reverse the bank leg, or the bank balance stops agreeing with the bank
    // statement - the one figure in a bookkeeping system that is checked
    // against the outside world. What must remain is a credit on the customer:
    // they paid for an invoice that no longer exists and are owed the money.
    Money bankVorher = Money::Zero("EUR");
    for (const Store::KontoSaldo& k : store.SummenUndSalden(mandant.id))
        if (k.konto == "1200") bankVorher = k.saldo;
    CheckInt(bankVorher.Minor(), 123280, "the bank holds the full payment before the reversal");

    Beleg stornoBezahlt;
    CheckStore(store.StorniereBeleg(rechnung.id, Date(2026, 8, 10), "Falsch berechnet",
                                    "rechnung", akteur, stornoBezahlt),
               "a paid invoice can be reversed");

    Money bankNachher = Money::Zero("EUR");
    Money kundeNachher = Money::Zero("EUR");
    for (const Store::KontoSaldo& k : store.SummenUndSalden(mandant.id)) {
        if (k.konto == "1200")        bankNachher  = k.saldo;
        if (k.konto == kunde.konto)   kundeNachher = k.saldo;
    }
    CheckInt(bankNachher.Minor(), bankVorher.Minor(),
             "the bank is untouched by the reversal - the payment really happened");
    CheckInt(kundeNachher.Minor(), -123280,
             "and the customer is left in credit by what they paid");
    Check(store.Buchungskreisdifferenz(mandant.id).IsZero(),
          "the ledger still balances with an unmatched payment in it");

    bool zahlungGegenbucht = false;
    for (const Buchung& b : store.BuchungenZuBeleg(stornoBezahlt.id))
        if (b.konto == "1200") zahlungGegenbucht = true;
    Check(!zahlungGegenbucht,
          "no reversal posting touched the money account");

    // --- the hash chain, intact ---
    HashKettenPruefung pruefung = store.PruefeHashKette(mandant.id);
    Check(pruefung.ok, "the journal's hash chain verifies");
    Check(pruefung.geprueft > 0, "and it actually looked at rows");

    // --- Festschreibung ---
    CheckStore(store.Festschreiben(jahr.id, Date(2026, 6, 30), akteur),
               "everything up to 30 June can be frozen");
    Beleg gefroren;
    Check(store.BelegById(rechnung.id, gefroren), "the June invoice reads back");
    Check(gefroren.festgeschrieben,
          "and is marked festgeschrieben, as DATEV's own stack would be");
    const std::vector<Buchung> juni = store.Journal(mandant.id, Date(2026, 6, 1),
                                                    Date(2026, 6, 30));
    bool alleGefroren = !juni.empty();
    for (const Buchung& b : juni) if (!b.festgeschrieben) alleGefroren = false;
    Check(alleGefroren, "and so is every posting in the frozen period");

    Beleg zuSpaet;
    zuSpaet.mandantId    = mandant.id;
    zuSpaet.art          = BelegArt::Ausgangsrechnung;
    zuSpaet.datum        = Date(2026, 6, 10);           // inside the frozen period
    zuSpaet.partnerId    = kunde.id;
    zuSpaet.partnerKonto = kunde.konto;
    zuSpaet.partnerName  = kunde.name;
    {
        BelegPosition pos;
        pos.bezeichnung = "Nachtrag";
        pos.einzelpreis = Money::FromMinor(10000, "EUR");
        pos.konto       = "8400";
        pos.steuerschluessel = "USt19";
        zuSpaet.positionen.push_back(pos);
    }
    CheckRefused(store.SaveBeleg(zuSpaet, "rechnung", akteur),
                 "a document dated into a frozen period is refused");

    Buchung freieBuchung;
    freieBuchung.mandantId  = mandant.id;
    freieBuchung.belegdatum = Date(2026, 6, 5);
    freieBuchung.konto      = "4930";
    freieBuchung.gegenkonto = "1200";
    freieBuchung.umsatz     = Money::FromMinor(5000, "EUR");
    freieBuchung.sollHaben  = SollHaben::Soll;
    CheckRefused(store.BuchungErfassen(freieBuchung, akteur),
                 "and so is a posting dated into it");

    // The same posting after the frozen date is fine - freezing closes a
    // period, it does not close the books.
    freieBuchung.belegdatum = Date(2026, 8, 5);
    CheckStore(store.BuchungErfassen(freieBuchung, akteur),
               "a posting after the frozen date is accepted");
    Check(freieBuchung.laufendeNummer > 0, "it took its place in the chain");
    Check(!freieBuchung.hash.empty(), "and carries a hash");
    Check(store.Buchungskreisdifferenz(mandant.id).IsZero(),
          "a free-standing posting balances too");

    Buchung negativ;
    negativ.mandantId  = mandant.id;
    negativ.belegdatum = Date(2026, 8, 6);
    negativ.konto      = "4930";
    negativ.gegenkonto = "1200";
    negativ.umsatz     = Money::FromMinor(-5000, "EUR");
    negativ.sollHaben  = SollHaben::Soll;
    CheckRefused(store.BuchungErfassen(negativ, akteur),
                 "a negative Umsatz is refused - the direction is the Soll/Haben flag");

    Buchung gleich;
    gleich.mandantId  = mandant.id;
    gleich.belegdatum = Date(2026, 8, 6);
    gleich.konto      = "1200";
    gleich.gegenkonto = "1200";
    gleich.umsatz     = Money::FromMinor(5000, "EUR");
    CheckRefused(store.BuchungErfassen(gleich, akteur),
                 "and so is a posting from an account to itself");

    // --- roles ---
    Benutzer erfasser;
    erfasser.anmeldename = "eva";
    erfasser.rolle       = BenutzerRolle::Erfasser;
    CheckStore(store.SaveBenutzer(erfasser, akteur), "an Erfasser exists");
    Akteur nurErfassen;
    nurErfassen.benutzerId  = erfasser.id;
    nurErfassen.anmeldename = erfasser.anmeldename;
    nurErfassen.rolle       = erfasser.rolle;

    Beleg vonErfasser;
    vonErfasser.mandantId    = mandant.id;
    vonErfasser.art          = BelegArt::Ausgangsrechnung;
    vonErfasser.datum        = Date(2026, 8, 3);
    vonErfasser.partnerId    = kunde.id;
    vonErfasser.partnerKonto = kunde.konto;
    vonErfasser.partnerName  = kunde.name;
    {
        BelegPosition pos;
        pos.bezeichnung = "Vorbereitet";
        pos.einzelpreis = Money::FromMinor(25000, "EUR");
        pos.konto       = "8400";
        pos.steuerschluessel = "USt19";
        vonErfasser.positionen.push_back(pos);
    }
    CheckStore(store.SaveBeleg(vonErfasser, "rechnung", nurErfassen),
               "an Erfasser may write a document");
    CheckRefused(store.Buchen(vonErfasser, nurErfassen),
                 "but may not post it - the role is enforced in the store, not in the UI");
    CheckRefused(store.Festschreiben(jahr.id, Date(2026, 7, 31), nurErfassen),
                 "and may not freeze a period");

    // --- an unknown tax key is an error, never a zero rate ---
    Beleg falsch;
    falsch.mandantId    = mandant.id;
    falsch.art          = BelegArt::Ausgangsrechnung;
    falsch.datum        = Date(2026, 8, 4);
    falsch.partnerId    = kunde.id;
    falsch.partnerKonto = kunde.konto;
    falsch.partnerName  = kunde.name;
    {
        BelegPosition pos;
        pos.bezeichnung = "Unbekannt besteuert";
        pos.einzelpreis = Money::FromMinor(10000, "EUR");
        pos.konto       = "8400";
        pos.steuerschluessel = "GIBTESNICHT";
        falsch.positionen.push_back(pos);
    }
    CheckRefused(store.SaveBeleg(falsch, "rechnung", akteur),
                 "an unknown tax key is refused rather than silently taxed at zero");

    // --- searching and filtering, which is what the Rechnungen screen does ---
    Store::BelegFilter filter;
    filter.mandantId = mandant.id;
    filter.nurOffene = true;
    const std::vector<Beleg> offene = store.BelegListe(filter);
    for (const Beleg& b : offene)
        Check(b.status == BelegStatus::Gebucht || b.status == BelegStatus::TeilweiseBezahlt,
              "the open list contains only unsettled posted documents");

    Store::BelegFilter suche;
    suche.mandantId = mandant.id;
    suche.suche     = "muster";                 // lower case, the name is "Muster AG"
    Check(!store.BelegListe(suche).empty(),
          "searching by partner name is case-insensitive");

    Store::BelegFilter zeitraum;
    zeitraum.mandantId = mandant.id;
    zeitraum.von = Date(2026, 6, 1);
    zeitraum.bis = Date(2026, 6, 30);
    const std::vector<Beleg> juniBelege = store.BelegListe(zeitraum);
    bool nurJuni = !juniBelege.empty();
    for (const Beleg& b : juniBelege)
        if (b.datum < zeitraum.von || b.datum > zeitraum.bis) nurJuni = false;
    Check(nurJuni, "and a date range returns only that range");

    // --- the chain catches what SQL did behind the application's back ---
    //
    // This is the test that makes the hash chain worth its cost. The UPDATE
    // below is exactly what somebody with the database file and sqlite3 would
    // do, and it is invisible to every other check in this file.
    pruefung = store.PruefeHashKette(mandant.id);
    Check(pruefung.ok, "the chain still verifies before it is tampered with");

    const std::vector<Buchung> alle = store.Journal(mandant.id);
    Check(!alle.empty(), "there are postings to tamper with");
    if (!alle.empty()) {
        const Buchung ziel = alle[alle.size() / 2];
        const UltraDbResult veraendert = UltraDb_Exec(
            store.ConnectionName(), "UPDATE buchung SET umsatz = umsatz + 100 WHERE id = ?",
            { ziel.id });
        Check(static_cast<bool>(veraendert), "an amount is edited directly in the database");

        pruefung = store.PruefeHashKette(mandant.id);
        Check(!pruefung.ok, "and the chain notices");
        CheckInt(pruefung.ersteFehlerhafteId, ziel.id,
                 "naming the row that was changed");
        Check(!pruefung.fehler.empty(), "with a sentence fit to show a user");

        // Put it back, so the deletion test below starts from a sound chain.
        UltraDb_Exec(store.ConnectionName(),
                     "UPDATE buchung SET umsatz = umsatz - 100 WHERE id = ?", { ziel.id });
        pruefung = store.PruefeHashKette(mandant.id);
        Check(pruefung.ok, "restoring the amount restores the chain");

        const UltraDbResult geloescht = UltraDb_Exec(
            store.ConnectionName(), "DELETE FROM buchung WHERE id = ?", { ziel.id });
        Check(static_cast<bool>(geloescht), "a posting is deleted directly in the database");
        pruefung = store.PruefeHashKette(mandant.id);
        Check(!pruefung.ok, "and the gap in the running numbers is detected too");
    }

    store.Close();
}

int main() {
    std::printf("UltraFIBU engine tests\n");
    TestDate();
    TestGeschaeftsjahr();
    TestSteuerkalender();
    TestUstIdNr();
    TestDatenDateien();
    TestBestaetigung();
    TestStore();
    TestBelegeUndBuchungen();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
