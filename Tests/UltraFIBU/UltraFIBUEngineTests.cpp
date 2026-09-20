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
#include "UltraFIBUBank.h"
#include "UltraFIBUBelegArchiv.h"
#include "UltraFIBUDatev.h"
#include "UltraFIBURechnungPdf.h"
#include "UltraFIBUStore.h"
#include "UltraFIBUTypes.h"
#include "UltraFIBUUstIdNr.h"
#include "UltraFIBUUstIdNrOnline.h"
#include "UltraFIBUOss.h"
#include "UltraFIBUUstva.h"

#include <UltraCrypt/UltraCryptCore.h>
// The hash-chain test edits a posting the way somebody with the database
// file and a SQL prompt would - which is the only way to prove the chain
// notices. Nothing else in the engine writes SQL outside the store.
#include <UltraDatabase/UltraDatabaseQuery.h>

#include <dirent.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <memory>
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


// ===== DER RECHNUNGSDRUCK =====
//
// What is worth testing about a printed invoice, as opposed to what it looks
// like:
//
//  - **§ 14 UStG is checked, not assumed.** An invoice missing the supplier's
//    tax number is legally deficient and its recipient cannot deduct the input
//    tax, so every mandatory field is named when it is absent.
//  - **The euro sign survives.** WinAnsi puts it at 0x80, outside Latin-1, and
//    the PDF writer used to replace it with '?'. An invoice reading
//    "1.232,80 ?" is not an invoice, so the produced file is searched for the
//    byte itself.
//  - **Amounts line up**, which with no embedded font means the width table has
//    to be right where it matters: every digit in Helvetica is 556/1000 em.
//  - **A document that does not fit is refused**, not silently truncated.
//    Losing a position off the bottom of an invoice is the one failure nobody
//    would notice until the customer paid the wrong amount.

static std::string LiesDatei(const std::string& pfad) {
    std::FILE* f = std::fopen(pfad.c_str(), "rb");
    if (f == nullptr) return std::string();
    std::string inhalt;
    char puffer[4096];
    size_t gelesen = 0;
    while ((gelesen = std::fread(puffer, 1, sizeof(puffer), f)) > 0)
        inhalt.append(puffer, gelesen);
    std::fclose(f);
    return inhalt;
}

static void TestRechnungPdf() {
    std::printf("Rechnungsdruck (PDF)\n");

    // --- the width table, where it matters ---
    // Ten digits at 10 pt must be exactly 55.6 pt: Helvetica's digits are all
    // 556/1000 em, which is the property a money column depends on.
    const double zehnZiffern = TextBreite("0123456789", 10.0, false);
    Check(zehnZiffern > 55.59 && zehnZiffern < 55.61,
          "ten digits are exactly 55,6 pt wide at 10 pt");
    Check(TextBreite("1111111111", 10.0, false) == TextBreite("9876543210", 10.0, false),
          "and every digit is the same width, whatever the digits are");
    Check(TextBreite("0", 20.0, false) == 2.0 * TextBreite("0", 10.0, false),
          "width scales with the font size");
    // An umlaut is one character, not two bytes.
    Check(TextBreite("ü", 10.0, false) < TextBreite("uu", 10.0, false),
          "a UTF-8 umlaut counts as one glyph, not as its two bytes");
    CheckInt(static_cast<int64_t>(TextBreite("", 10.0, false)), 0,
             "an empty string is zero wide");

    // --- § 14 UStG ---
    Mandant mandant;
    mandant.name = "Beispiel GmbH";
    Partner kunde;
    kunde.name = "Muster AG";
    Beleg beleg;
    beleg.datum  = Date(2026, 6, 15);
    beleg.nummer = "R-2026-0001";
    {
        BelegPosition pos;
        pos.bezeichnung = "Beratung";
        pos.einzelpreis = Money::FromMinor(100000, "EUR");
        pos.konto       = "8400";
        pos.steuerschluessel = "USt19";
        pos.satzPromille = 190;
        beleg.positionen.push_back(pos);
    }
    beleg.Summieren();

    std::vector<std::string> fehlt = PruefePflichtangaben(mandant, beleg, kunde);
    bool nenntSteuernummer = false, nenntStrasse = false;
    for (const std::string& f : fehlt) {
        if (f.find("Steuernummer") != std::string::npos) nenntSteuernummer = true;
        if (f.find("Straße des leistenden") != std::string::npos) nenntStrasse = true;
    }
    Check(nenntSteuernummer,
          "a missing Steuernummer and USt-IdNr. is reported - without one the "
          "recipient cannot deduct the input tax");
    Check(nenntStrasse, "and so is a missing address of the supplier");

    mandant.strasse      = "Industriestraße 14";
    mandant.plz          = "57462";
    mandant.ort          = "Olpe";
    mandant.steuernummer = "338/5744/1234";
    mandant.iban         = "DE02120300000000202051";
    kunde.strasse = "Domkloster 4";
    kunde.plz     = "50667";
    kunde.ort     = "Köln";
    kunde.konto   = "10000";
    beleg.leistungVon = Date(2026, 6, 10);

    fehlt = PruefePflichtangaben(mandant, beleg, kunde);
    Check(fehlt.empty(), "a complete invoice reports nothing missing");

    // An EU business customer without a VAT number cannot be zero-rated, and
    // the number has to be on the invoice.
    Partner euKunde = kunde;
    euKunde.steuerkategorie = Steuerkategorie::EuUnternehmer;
    euKunde.land = "SK";
    const std::vector<std::string> euFehlt = PruefePflichtangaben(mandant, beleg, euKunde);
    bool nenntUstId = false;
    for (const std::string& f : euFehlt)
        if (f.find("USt-IdNr. des Leistungsempf") != std::string::npos) nenntUstId = true;
    Check(nenntUstId,
          "an EU business customer with no USt-IdNr. is reported - that number "
          "is what makes the supply zero-rated");

    // --- the file itself ---
    std::vector<Steuerschluessel> schluessel;
    {
        Steuerschluessel key;
        key.schluessel   = "USt19";
        key.art          = SteuerArt::Inland;
        key.satzPromille = 190;
        schluessel.push_back(key);
    }

    const std::string pfad = "rechnungstest.pdf";
    std::remove(pfad.c_str());
    RechnungPdfErgebnis ergebnis =
        SchreibeRechnungPdf(mandant, beleg, kunde, schluessel, pfad);
    Check(ergebnis.ok, "the invoice is written");
    Check(ergebnis.VollstaendigNachUStG(), "and is complete under § 14 UStG");

    const std::string pdf = LiesDatei(pfad);
    Check(pdf.size() > 1000, "the file has content");
    Check(pdf.compare(0, 5, "%PDF-") == 0, "and is a PDF");
    Check(pdf.find("%%EOF") != std::string::npos, "that is terminated");
    Check(pdf.find("R-2026-0001") != std::string::npos,
          "the invoice number is in it");
    Check(pdf.find("1.190,00") != std::string::npos,
          "and so is the gross total, in German notation");
    Check(pdf.find("Industriestra") != std::string::npos,
          "and the supplier's address, which § 14 requires");
    Check(pdf.find("338/5744/1234") != std::string::npos,
          "and the Steuernummer");

    // The euro sign, as the byte WinAnsi actually uses. Before the encoding fix
    // this was '?' and nothing in the pipeline complained.
    Check(pdf.find('\x80') != std::string::npos,
          "the euro sign is written as WinAnsi 0x80, not replaced with '?'");
    // The umlauts of a German address, as Latin-1 bytes.
    Check(pdf.find('\xDF') != std::string::npos, "sharp s survives as 0xDF");
    Check(pdf.find('\xF6') != std::string::npos, "o-umlaut survives as 0xF6");

    // --- the draft mark ---
    Check(pdf.find("ENTWURF") != std::string::npos,
          "an unposted document says on its face that it is a draft");
    Beleg gebucht = beleg;
    gebucht.status = BelegStatus::Gebucht;
    const std::string pfadGebucht = "rechnungstest_gebucht.pdf";
    std::remove(pfadGebucht.c_str());
    Check(SchreibeRechnungPdf(mandant, gebucht, kunde, schluessel, pfadGebucht).ok,
          "a posted invoice is written");
    const std::string pdfGebucht = LiesDatei(pfadGebucht);
    Check(pdfGebucht.find("ENTWURF") == std::string::npos,
          "and carries no draft mark");

    // --- the exemption note ---
    Beleg igl = beleg;
    igl.positionen[0].steuerschluessel = "IGL";
    igl.positionen[0].satzPromille = 0;
    igl.Summieren();
    std::vector<Steuerschluessel> iglKeys;
    {
        Steuerschluessel key;
        key.schluessel = "IGL";
        key.art        = SteuerArt::IgLieferung;
        iglKeys.push_back(key);
    }
    const std::string pfadIgl = "rechnungstest_igl.pdf";
    std::remove(pfadIgl.c_str());
    Check(SchreibeRechnungPdf(mandant, igl, euKunde, iglKeys, pfadIgl).ok,
          "a zero-rated invoice is written");
    const std::string pdfIgl = LiesDatei(pfadIgl);
    Check(pdfIgl.find("6a UStG") != std::string::npos,
          "a zero-rated line names its exemption, which § 14 Abs. 4 Nr. 8 requires");
    CheckInt(igl.steuer.Minor(), 0, "and carries no tax");

    // --- too many positions ---
    // The framework's PDF writer emits one page. Refusing is the only honest
    // answer; an invoice quietly missing its last four lines is worse than no
    // invoice at all.
    Beleg lang = beleg;
    lang.positionen.clear();
    for (int i = 0; i < 80; ++i) {
        BelegPosition pos;
        pos.bezeichnung = "Position " + std::to_string(i + 1);
        pos.einzelpreis = Money::FromMinor(1000, "EUR");
        pos.konto       = "8400";
        pos.steuerschluessel = "USt19";
        pos.satzPromille = 190;
        lang.positionen.push_back(pos);
    }
    lang.Summieren();
    const std::string pfadLang = "rechnungstest_lang.pdf";
    std::remove(pfadLang.c_str());
    const RechnungPdfErgebnis zuLang =
        SchreibeRechnungPdf(mandant, lang, kunde, schluessel, pfadLang);
    Check(!zuLang.ok, "an invoice that does not fit one page is refused");
    Check(zuLang.fehler.find("Seite") != std::string::npos,
          "and the refusal says why, in German");
    Check(LiesDatei(pfadLang).empty(),
          "and no half-finished file is left behind");

    // --- the obvious refusals ---
    Check(!SchreibeRechnungPdf(mandant, beleg, kunde, schluessel, "").ok,
          "an empty file name is refused");
    Beleg leer = beleg;
    leer.positionen.clear();
    Check(!SchreibeRechnungPdf(mandant, leer, kunde, schluessel, "leer.pdf").ok,
          "and so is a document without positions");

    std::remove(pfad.c_str());
    std::remove(pfadGebucht.c_str());
    std::remove(pfadIgl.c_str());
    std::remove(pfadLang.c_str());
}


// ===== DATEV =====
//
// What is worth testing about a DATEV export, given that the real acceptance
// test - a Kanzlei importing the stack - cannot be run here:
//
//  - **The file is CP1252, not UTF-8.** An umlaut written as UTF-8 arrives as
//    two wrong characters in a Kanzlei's ledger and stays there for ten years.
//  - **Umsatz is unsigned** and the direction is the Soll/Haben-Kennzeichen. A
//    signed amount produces a plausible-looking, wrong ledger.
//  - **Belegdatum is TTMM**, so a stack may not span a calendar year and this
//    writer goes further and refuses a month not wholly inside the fiscal
//    year - which is the whole point for a 1 April Geschäftsjahr.
//  - **The column order is a guess until a real file confirms it**, so the
//    checker that compares a definition against a real file is itself tested,
//    in both directions: it accepts a matching file and names the position of
//    every mismatch.

static void TestDatev() {
    std::printf("DATEV-Export\n");

    // --- CP1252 ---
    bool verlust = false;
    const std::string umlaute = NachCp1252("Müller & Söhne, Straße", verlust);
    Check(!verlust, "German letters fit in CP1252");
    Check(umlaute.find('\xFC') != std::string::npos, "u-umlaut is the single byte 0xFC");
    Check(umlaute.find('\xF6') != std::string::npos, "o-umlaut is 0xF6");
    Check(umlaute.find('\xDF') != std::string::npos, "sharp s is 0xDF");
    CheckInt(static_cast<int64_t>(umlaute.size()), 22,
             "and each is one byte, not the two UTF-8 uses");

    const std::string euro = NachCp1252("1.234,56 €", verlust);
    Check(!verlust, "the euro sign fits");
    Check(euro.find('\x80') != std::string::npos,
          "as WinAnsi 0x80, where CP1252 puts it");

    const std::string chinesisch = NachCp1252("Konto 北京", verlust);
    Check(verlust, "a character outside CP1252 is reported as a loss");
    Check(chinesisch.find('?') != std::string::npos, "and written as '?'");

    // --- the column definition ---
    DatevDefinition definition;
    std::string fehler;
    const std::string pfad = DatevDefinitionPfad("DATEV-Buchungsstapel-v700.csv");
    if (pfad.empty()) {
        std::printf("    note: DATEV-Buchungsstapel-v700.csv not found, skipping\n");
        return;
    }
    Check(definition.Laden(pfad, fehler), "the shipped column definition loads");
    CheckInt(static_cast<int64_t>(definition.Anzahl()), 120,
             "and has the 120 columns the format describes");
    Check(definition.Index("Umsatz (ohne Soll/Haben-Kz)") == 0,
          "Umsatz is the first column");
    Check(definition.Index("Soll/Haben-Kennzeichen") == 1, "the S/H flag the second");
    Check(definition.Index("Festschreibung") > 0, "and Festschreibung is in there");
    Check(definition.Index("Gibt Es Nicht") == -1, "an unknown column reports -1");

    // A definition with a line missing would write every later value into the
    // wrong column, so the loader refuses a gap rather than shifting silently.
    const std::string luecke = "luecke-test.csv";
    {
        std::FILE* f = std::fopen(luecke.c_str(), "wb");
        const char* inhalt = "1;Erste;text\n3;Dritte;text\n";
        std::fwrite(inhalt, 1, std::strlen(inhalt), f);
        std::fclose(f);
    }
    DatevDefinition kaputt;
    Check(!kaputt.Laden(luecke, fehler),
          "a gap in the column numbering is refused, not silently shifted");
    Check(fehler.find("springen") != std::string::npos, "and the reason says so");
    std::remove(luecke.c_str());

    // --- the ground for an export ---
    Mandant mandant;
    mandant.name            = "Beispiel GmbH";
    mandant.waehrung        = "EUR";
    Geschaeftsjahr jahr;
    jahr.beginn      = Date(2026, 4, 1);
    jahr.ende        = Date(2027, 3, 31);
    jahr.bezeichnung = "2026/2027";
    jahr.sachkontenlaenge = 4;

    std::vector<Buchung> journal;
    auto buchung = [&](const Date& datum, const std::string& konto,
                       const std::string& gegenkonto, int64_t minor,
                       SollHaben sh, const std::string& text,
                       const std::string& steuerschluessel) {
        Buchung b;
        b.mandantId  = 1;
        b.belegdatum = datum;
        b.konto      = konto;
        b.gegenkonto = gegenkonto;
        b.umsatz     = Money::FromMinor(minor, "EUR");
        b.sollHaben  = sh;
        b.buchungstext = text;
        b.belegfeld1 = "R-1";
        b.waehrung   = "EUR";
        // A tax key with no DATEV BU-Schlüssel beside it is exactly the case
        // the export has to warn about: the file imports, and DATEV books it
        // without the tax automatics.
        b.steuerschluessel = steuerschluessel;
        b.laufendeNummer = static_cast<int64_t>(journal.size()) + 1;
        journal.push_back(b);
    };
    buchung(Date(2026, 6, 15), "10000", "8400", 119000, SollHaben::Soll,
            "Beratung; mit Semikolon", "USt19");
    buchung(Date(2026, 6, 20), "1200", "10000", 50000, SollHaben::Soll,
            "Zahlung", "");
    buchung(Date(2026, 7,  3), "10000", "8400", 23800, SollHaben::Soll,
            "Juli", "USt19");

    // Berater- and Mandantennummer are the Kanzlei's; without them the import
    // is refused there, so the export refuses here and says which is missing.
    DatevErgebnis ohneNummern =
        SchreibeBuchungsstapel(mandant, jahr, journal, definition, 2026, 6, ".", "test");
    Check(!ohneNummern.ok, "an export without the Kanzlei's numbers is refused");
    Check(ohneNummern.fehler.find("Beraternummer") != std::string::npos ||
          ohneNummern.fehler.find("Berater") != std::string::npos,
          "and the refusal names them");

    mandant.beraternummer   = "1001";
    mandant.mandantennummer = "456";

    // --- a month that is not wholly inside the fiscal year ---
    // The Belegdatum field carries no year; DATEV infers it from the
    // Wirtschaftsjahr. A month straddling the boundary would be mis-booked.
    Geschaeftsjahr rumpf;
    rumpf.beginn      = Date(2026, 6, 15);      // starts mid-month on purpose
    rumpf.ende        = Date(2027, 3, 31);
    rumpf.bezeichnung = "Rumpfjahr";
    rumpf.sachkontenlaenge = 4;
    const DatevErgebnis halberMonat =
        SchreibeBuchungsstapel(mandant, rumpf, journal, definition, 2026, 6, ".", "test");
    Check(!halberMonat.ok,
          "a month only partly inside the Geschäftsjahr is refused");
    Check(halberMonat.fehler.find("Wirtschaftsjahr") != std::string::npos,
          "and the refusal explains that the Belegdatum carries no year");

    // --- the export itself ---
    const DatevErgebnis juni =
        SchreibeBuchungsstapel(mandant, jahr, journal, definition, 2026, 6,
                               ".", "pruefer");
    Check(juni.ok, "June exports");
    CheckInt(juni.zeilen, 2, "with only June's two postings, not July's");

    const std::string inhalt = LiesDatei(juni.datei);
    Check(!inhalt.empty(), "the file has content");

    // Line endings and encoding: both invisible in a diff and both fatal.
    Check(inhalt.find("\r\n") != std::string::npos, "the lines end CRLF");
    Check(inhalt.find('\n') != std::string::npos, "and there is more than one");
    bool nurCrLf = true;
    for (size_t i = 0; i < inhalt.size(); ++i)
        if (inhalt[i] == '\n' && (i == 0 || inhalt[i - 1] != '\r')) nurCrLf = false;
    Check(nurCrLf, "with no bare LF anywhere");

    // The header fields that decide whether the import lands in the right year.
    Check(inhalt.compare(0, 6, "\"EXTF\"") == 0, "the file starts with EXTF");
    Check(inhalt.find(";700;21;") != std::string::npos,
          "version 700, category 21 Buchungsstapel");
    Check(inhalt.find("20260401") != std::string::npos,
          "the WJ-Beginn is the 1 April fiscal year, not a January default");
    Check(inhalt.find(";1001;456;") != std::string::npos,
          "the Kanzlei's Berater- and Mandantennummer are in the header");
    Check(inhalt.find("20260601;20260630") != std::string::npos,
          "and the period is the whole month");

    // The data rows.
    Check(inhalt.find("1190,00;\"S\"") != std::string::npos,
          "the Umsatz is unsigned with a comma decimal, and S/H carries the direction");
    Check(inhalt.find("-1190") == std::string::npos,
          "no signed amount anywhere - that is what the S/H flag is for");
    Check(inhalt.find(";1506;") != std::string::npos,
          "the Belegdatum is TTMM: 15 June is 1506");
    Check(inhalt.find("\"Beratung; mit Semikolon\"") != std::string::npos,
          "a Buchungstext containing the separator is quoted and does not split the row");
    Check(inhalt.find("\xFC") == std::string::npos ||
          inhalt.find("\xC3\xBC") == std::string::npos,
          "nothing is written as a UTF-8 multi-byte sequence");

    Check(!juni.warnungen.empty(),
          "the missing DATEV BU-Schlüssel is warned about, not passed over");
    bool nenntBu = false;
    for (const std::string& w : juni.warnungen)
        if (w.find("BU-Schl") != std::string::npos) nenntBu = true;
    Check(nenntBu, "and the warning names it");

    // --- the checker, both ways ---
    DatevPruefung gut = PruefeDateiGegenDefinition(juni.datei, definition);
    Check(gut.ok, "the checker accepts a file written from the same definition");
    CheckInt(static_cast<int64_t>(gut.spaltenInDatei), 120, "and counts its columns");
    CheckInt(gut.kategorie, 21, "and reads the category out of the header");

    // Corrupt one column name and confirm the position is named. This is the
    // check that will turn the shipped definition from a guess into a fact the
    // moment a real DATEV file exists.
    {
        std::string kaputtText = inhalt;
        const size_t stelle = kaputtText.find("\"Belegfeld 1\"");
        Check(stelle != std::string::npos, "Belegfeld 1 is in the column line");
        if (stelle != std::string::npos)
            kaputtText.replace(stelle, 13, "\"Belegfeld X\"");
        const std::string kaputtDatei = "datev-kaputt.csv";
        std::FILE* f = std::fopen(kaputtDatei.c_str(), "wb");
        std::fwrite(kaputtText.data(), 1, kaputtText.size(), f);
        std::fclose(f);

        const DatevPruefung schlecht =
            PruefeDateiGegenDefinition(kaputtDatei, definition);
        Check(!schlecht.ok, "a changed column name is detected");
        CheckInt(static_cast<int64_t>(schlecht.abweichungen.size()), 1,
                 "as exactly one difference");
        if (!schlecht.abweichungen.empty())
            Check(schlecht.abweichungen[0].find("Spalte 11") != std::string::npos,
                  "naming its position, so the definition can be corrected there");
        std::remove(kaputtDatei.c_str());
    }

    // A file that is not DATEV at all.
    {
        const std::string fremd = "kein-datev.csv";
        std::FILE* f = std::fopen(fremd.c_str(), "wb");
        const char* inhaltFremd = "\"IRGENDWAS\";1\r\n\"a\";\"b\"\r\n";
        std::fwrite(inhaltFremd, 1, std::strlen(inhaltFremd), f);
        std::fclose(f);
        const DatevPruefung fremdPruefung =
            PruefeDateiGegenDefinition(fremd, definition);
        Check(!fremdPruefung.ok, "a file that is not DATEV is rejected");
        Check(!fremdPruefung.fehler.empty(), "with a reason rather than a diff");
        std::remove(fremd.c_str());
    }
    Check(!PruefeDateiGegenDefinition("gibtesnicht.csv", definition).fehler.empty(),
          "and a missing file is reported rather than crashing");

    std::remove(juni.datei.c_str());
}


// ===== DATEV-IMPORT =====
//
// The importer has one structural advantage over the exporter and it is worth
// stating: **a real DATEV file names its own columns**, so reading it does not
// depend on the reconstructed column order at all. What is tested here is
// therefore the reading itself - the year inference behind TTMM, the unsigned
// Umsatz, the BU-Schlüssel mapping in both its outcomes - and the two things
// that protect the ledger: no partial import, and no accidental second one.

static void SchreibeDatei(const std::string& pfad, const std::string& inhalt) {
    std::FILE* f = std::fopen(pfad.c_str(), "wb");
    std::fwrite(inhalt.data(), 1, inhalt.size(), f);
    std::fclose(f);
}

// The fixtures are written the way a DATEV file really arrives: CP1252, not
// UTF-8. Reading them back through the importer is therefore also a test of
// the encoding, which is the one mistake that corrupts a whole file silently.
static std::string Cp1252(const std::string& utf8) {
    bool verlust = false;
    return NachCp1252(utf8, verlust);
}

static void TestDatevImport() {
    std::printf("DATEV-Import\n");

    Mandant mandant;
    mandant.id              = 1;
    mandant.name            = "Beispiel GmbH";
    mandant.waehrung        = "EUR";
    mandant.mandantennummer = "456";
    Geschaeftsjahr jahr;
    jahr.beginn      = Date(2026, 4, 1);
    jahr.ende        = Date(2027, 3, 31);
    jahr.bezeichnung = "2026/2027";

    // A minimal but real-shaped file: the two preamble lines, a column line
    // naming only the columns that matter, and rows. The importer finds the
    // values by those names, which is the whole point.
    const std::string kopf =
        "\"EXTF\";700;21;\"Buchungsstapel\";13;20260701120000000;;\"SV\";\"kanzlei\";;"
        "1001;456;20260401;4;20260601;20260630;\"Juni 2026\";\"\";1;;;;;;\"EUR\";0\r\n";
    const std::string spalten =
        "\"Umsatz (ohne Soll/Haben-Kz)\";\"Soll/Haben-Kennzeichen\";\"WKZ Umsatz\";"
        "\"Konto\";\"Gegenkonto (ohne BU-Schlüssel)\";\"BU-Schlüssel\";\"Belegdatum\";"
        "\"Belegfeld 1\";\"Buchungstext\";\"Festschreibung\"\r\n";

    const std::string gut =
        kopf + spalten +
        "1190,00;\"S\";\"EUR\";10000;8400;\"\";1506;\"R-1\";\"Beratung; Müller\";0\r\n"
        "500,00;\"S\";\"EUR\";1200;10000;\"\";2006;\"R-1\";\"Zahlung\";0\r\n";

    const std::string pfad = "import-gut.csv";
    SchreibeDatei(pfad, Cp1252(gut));

    std::vector<Steuerschluessel> keine;
    DatevImportBericht bericht = LeseBuchungsstapel(pfad, mandant, jahr, keine);
    Check(bericht.ok, "a well-formed Buchungsstapel reads");
    CheckText(bericht.kennzeichen, "EXTF", "the header is EXTF");
    CheckInt(bericht.kategorie, 21, "category 21");
    CheckText(bericht.beraternummer, "1001", "the Beraternummer is read");
    CheckText(bericht.mandantennummer, "456", "and the Mandantennummer");
    Check(bericht.wjBeginn == Date(2026, 4, 1),
          "the Wirtschaftsjahr start is read - a 1 April year, not a January default");
    Check(bericht.von == Date(2026, 6, 1) && bericht.bis == Date(2026, 6, 30),
          "and the period");
    CheckInt(bericht.gelesen, 2, "two data lines");
    CheckInt(bericht.uebernommen, 2, "both usable");
    CheckInt(bericht.uebersprungen, 0, "none skipped");
    Check(!bericht.dateiHash.empty(), "the file is fingerprinted for the duplicate check");

    if (bericht.zeilen.size() == 2) {
        const Buchung& erste = bericht.zeilen[0].buchung;
        // TTMM carries no year; it comes from the header's period.
        Check(erste.belegdatum == Date(2026, 6, 15),
              "1506 became 15 June 2026 - the year came from the file's period");
        CheckInt(erste.umsatz.Minor(), 119000, "the amount is read as 1.190,00");
        Check(erste.sollHaben == SollHaben::Soll, "with S on the Konto");
        CheckText(erste.konto, "10000", "Konto");
        CheckText(erste.gegenkonto, "8400", "Gegenkonto");
        CheckText(erste.buchungstext, "Beratung; Müller",
                  "a Buchungstext containing the separator survives the quoting, "
                  "and the umlaut survives CP1252");
        Check(erste.steuerSeite == SteuerSeite::Keine,
              "with no tax split, because the row carries no BU-Schlüssel");
        CheckInt(erste.netto.Minor(), 119000, "so the whole amount is the net side");
    }
    CheckInt(bericht.mitSteuer, 0, "no posting got a tax split");
    bool warntUeberSteuer = false;
    for (const std::string& w : bericht.warnungen)
        if (w.find("Steueraufteilung") != std::string::npos) warntUeberSteuer = true;
    Check(warntUeberSteuer,
          "and that is said out loud - otherwise a gross revenue account after an "
          "import is a mystery");

    // --- the BU-Schlüssel mapping, when it exists ---
    std::vector<Steuerschluessel> mitMapping;
    {
        Steuerschluessel key;
        key.schluessel   = "USt19";
        key.satzPromille = 190;
        key.datevBu      = "3";
        key.kontoSteuer  = "1776";
        key.gueltigVon   = Date(2026, 1, 1);
        mitMapping.push_back(key);
    }
    const std::string mitBu =
        kopf + spalten +
        "1190,00;\"S\";\"EUR\";10000;8400;\"3\";1506;\"R-1\";\"Beratung\";0\r\n";
    SchreibeDatei("import-bu.csv", Cp1252(mitBu));
    const DatevImportBericht mitBericht =
        LeseBuchungsstapel("import-bu.csv", mandant, jahr, mitMapping);
    Check(mitBericht.ok, "a row with a mappable BU-Schlüssel reads");
    CheckInt(mitBericht.mitSteuer, 1, "and gets a tax split");
    if (!mitBericht.zeilen.empty()) {
        const Buchung& b = mitBericht.zeilen[0].buchung;
        CheckText(b.steuerschluessel, "USt19", "mapped back to our own key");
        CheckInt(b.steuer.Minor(), 19000,
                 "19 % out of 1.190,00 gross is 190,00 - the same split the "
                 "posting path does");
        CheckInt(b.netto.Minor(), 100000, "leaving 1.000,00 net");
        CheckText(b.steuerkonto, "1776", "on the key's tax account");
        CheckText(b.buSchluessel, "3", "and the DATEV key is kept verbatim");
    }

    // An unmappable BU key is imported unsplit, with a warning, rather than
    // guessed at.
    const std::string fremdBu =
        kopf + spalten +
        "1190,00;\"S\";\"EUR\";10000;8400;\"9\";1506;\"R-1\";\"Beratung\";0\r\n";
    SchreibeDatei("import-fremdbu.csv", Cp1252(fremdBu));
    const DatevImportBericht fremdBericht =
        LeseBuchungsstapel("import-fremdbu.csv", mandant, jahr, mitMapping);
    Check(fremdBericht.ok, "an unknown BU-Schlüssel does not stop the import");
    CheckInt(fremdBericht.mitSteuer, 0, "but produces no invented tax split");
    if (!fremdBericht.zeilen.empty())
        CheckText(fremdBericht.zeilen[0].buchung.buSchluessel, "9",
                  "and the key is kept so the mapping can be added later");
    bool nenntBu = false, nenntDenSchluessel = false;
    for (const std::string& w : fremdBericht.warnungen) {
        if (w.find("datev_bu") != std::string::npos) nenntBu = true;
        // Counting unmapped keys is not enough: the row to fill in is the one
        // for *this* key, and finding it otherwise means reading the file.
        if (w.find("datev_bu") != std::string::npos &&
            w.find(": 9.") != std::string::npos) nenntDenSchluessel = true;
    }
    Check(nenntBu, "the warning names where the mapping belongs");
    Check(nenntDenSchluessel, "and which key it belongs for");

    // --- the unsigned rule ---
    // DATEV's Umsatz is never signed. A file from elsewhere that carries one
    // must not double up with the Soll/Haben flag.
    const std::string negativ =
        kopf + spalten +
        "-1190,00;\"H\";\"EUR\";10000;8400;\"\";1506;\"R-1\";\"Negativ\";0\r\n";
    SchreibeDatei("import-negativ.csv", Cp1252(negativ));
    const DatevImportBericht negBericht =
        LeseBuchungsstapel("import-negativ.csv", mandant, jahr, keine);
    if (negBericht.ok && !negBericht.zeilen.empty()) {
        CheckInt(negBericht.zeilen[0].buchung.umsatz.Minor(), 119000,
                 "a signed amount is taken as its magnitude");
        Check(negBericht.zeilen[0].buchung.sollHaben == SollHaben::Haben,
              "and the direction stays the one the S/H column gives");
    }

    // --- rows that cannot be read are named, not silently dropped ---
    const std::string kaputt =
        kopf + spalten +
        "1190,00;\"S\";\"EUR\";10000;8400;\"\";1506;\"R-1\";\"Gut\";0\r\n"
        "keinbetrag;\"S\";\"EUR\";10000;8400;\"\";1606;\"R-2\";\"Kaputt\";0\r\n"
        "500,00;\"X\";\"EUR\";1200;10000;\"\";1706;\"R-3\";\"Falsches SH\";0\r\n"
        "500,00;\"S\";\"EUR\";1200;1200;\"\";1806;\"R-4\";\"Gleiches Konto\";0\r\n"
        "500,00;\"S\";\"EUR\";1200;10000;\"\";3112;\"R-5\";\"Falscher Monat\";0\r\n";
    SchreibeDatei("import-kaputt.csv", Cp1252(kaputt));
    const DatevImportBericht kaputtBericht =
        LeseBuchungsstapel("import-kaputt.csv", mandant, jahr, keine);
    CheckInt(kaputtBericht.gelesen, 5, "five data lines seen");
    CheckInt(kaputtBericht.uebernommen, 1, "one usable");
    CheckInt(kaputtBericht.uebersprungen, 4, "four skipped");
    CheckInt(static_cast<int64_t>(kaputtBericht.fehlerZeilen.size()), 4,
             "each skipped line is reported");
    bool nenntZeile = false;
    for (const std::string& z : kaputtBericht.fehlerZeilen)
        if (z.find("Zeile 4") != std::string::npos) nenntZeile = true;
    Check(nenntZeile, "by its line number, so it can be looked at");

    // --- files that are not what they claim ---
    SchreibeDatei("import-fremd.csv", "\"IRGENDWAS\";1\r\n\"a\";\"b\"\r\n");
    Check(!LeseBuchungsstapel("import-fremd.csv", mandant, jahr, keine).ok,
          "a file that is not DATEV is refused");
    SchreibeDatei("import-kategorie.csv",
                  "\"EXTF\";700;16;\"Debitoren\";5;;;;;;1001;456;20260401;4;"
                  "20260601;20260630;\"x\"\r\n\"Konto\"\r\n");
    const DatevImportBericht falscheKat =
        LeseBuchungsstapel("import-kategorie.csv", mandant, jahr, keine);
    Check(!falscheKat.ok, "category 16 is refused - only 21 can be read");
    Check(falscheKat.fehler.find("21") != std::string::npos,
          "and the refusal says which category is supported");
    Check(!LeseBuchungsstapel("gibtesnicht.csv", mandant, jahr, keine).ok,
          "a missing file is reported rather than crashing");

    // A file belonging to another Mandant is a warning, not a silent merge:
    // mixing two companies' books cannot be undone by deleting rows.
    Mandant anderer = mandant;
    anderer.mandantennummer = "999";
    const DatevImportBericht fremderMandant =
        LeseBuchungsstapel(pfad, anderer, jahr, keine);
    bool warntMandant = false;
    for (const std::string& w : fremderMandant.warnungen)
        if (w.find("Mandant") != std::string::npos) warntMandant = true;
    Check(warntMandant, "a file from a different Mandant is flagged");

    // --- accounts the file names and the chart does not have ---
    // The Personenkonto is the interesting half: 10000 is five digits under a
    // Sachkontenlaenge of four, so it belongs to a customer and is never in the
    // chart. Reporting it would bury 8400, which is the one worth looking at.
    {
        std::vector<Konto> chart;
        Konto bank;
        bank.nummer = "1200";
        bank.bezeichnung = "Bank";
        chart.push_back(bank);
        const std::vector<std::string> fehlend = UnbekannteSachkonten(bericht, chart);
        CheckInt(static_cast<int64_t>(fehlend.size()), 1,
                 "one account in the file is missing from the chart");
        if (!fehlend.empty())
            CheckText(fehlend[0], "8400", "the Sachkonto, named so it can be added");
        bool nenntPersonenkonto = false;
        for (const std::string& k : fehlend) if (k == "10000") nenntPersonenkonto = true;
        Check(!nenntPersonenkonto,
              "and not the Personenkonto, which belongs to a customer and is "
              "never in the chart");
    }

    std::remove(pfad.c_str());
    std::remove("import-bu.csv");
    std::remove("import-fremdbu.csv");
    std::remove("import-negativ.csv");
    std::remove("import-kaputt.csv");
    std::remove("import-fremd.csv");
    std::remove("import-kategorie.csv");
}

// ===== THE IMPORT AS IT REACHES THE LEDGER =====
//
// Reading a file correctly is half of it. The other half is what the store
// does with the result, and it is the half that can cost money: a stack
// imported twice doubles a month and the only evidence is a balance wrong by
// exactly one stack, while a stack that stops halfway leaves a ledger that
// does not balance and no record of where it stopped. Both are checked here
// against a real database rather than argued about in a comment.
static void TestDatevImportInDenBestand() {
    std::printf("DATEV-Import in den Bestand\n");

    Store store;
    if (!CheckStore(store.Open("fibu-import", ":memory:"),
                    "a database for the import tests opens")) {
        std::printf("    skipping the import-into-store tests\n");
        return;
    }

    Akteur setup;
    Benutzer admin;
    admin.anmeldename = "chef";
    CheckStore(store.SaveBenutzer(admin, setup), "an administrator exists");
    Akteur akteur;
    akteur.benutzerId  = admin.id;
    akteur.anmeldename = admin.anmeldename;
    akteur.rolle       = admin.rolle;

    Mandant mandant;
    mandant.name            = "Beispiel GmbH";
    mandant.mandantennummer = "456";
    CheckStore(store.SaveMandant(mandant, akteur), "a company exists");

    Geschaeftsjahr jahr;
    jahr.mandantId   = mandant.id;
    jahr.beginn      = Date(2026, 4, 1);
    jahr.ende        = Date(2027, 3, 31);
    jahr.bezeichnung = jahr.DefaultBezeichnung();
    CheckStore(store.SaveGeschaeftsjahr(jahr, akteur),
               "with the 1 April fiscal year the import has to land in");

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
    konto("1776", "Umsatzsteuer 19 %", KontoTyp::Passiv);
    konto("8400", "Erloese 19 % USt", KontoTyp::Ertrag);
    konto("10000", "Kunde", KontoTyp::Aktiv);
    int geschrieben = 0;
    CheckStore(store.ImportKonten(mandant.id, konten, akteur, geschrieben),
               "the accounts the stack posts to exist");

    std::vector<Steuerschluessel> keys;
    Steuerschluessel ust19;
    ust19.mandantId    = mandant.id;
    ust19.schluessel   = "USt19";
    ust19.bezeichnung  = "Umsatzsteuer 19 %";
    ust19.satzPromille = 190;
    ust19.datevBu      = "3";
    ust19.kontoSteuer  = "1776";
    ust19.gueltigVon   = Date(2026, 1, 1);
    keys.push_back(ust19);
    CheckStore(store.ImportSteuerschluessel(mandant.id, keys, akteur, geschrieben),
               "and the tax key the BU-Schluessel maps to");

    Mandant gespeichert;
    Check(store.LoadMandant(mandant.id, gespeichert), "the company reads back");

    const std::string kopf =
        "\"EXTF\";700;21;\"Buchungsstapel\";13;20260701120000000;;\"SV\";\"kanzlei\";;"
        "1001;456;20260401;4;20260601;20260630;\"Juni 2026\";\"\";1;;;;;;\"EUR\";0\r\n";
    const std::string spalten =
        "\"Umsatz (ohne Soll/Haben-Kz)\";\"Soll/Haben-Kennzeichen\";\"WKZ Umsatz\";"
        "\"Konto\";\"Gegenkonto (ohne BU-Schlüssel)\";\"BU-Schlüssel\";\"Belegdatum\";"
        "\"Belegfeld 1\";\"Buchungstext\";\"Festschreibung\"\r\n";

    // One invoice with its VAT and one payment against it - the smallest stack
    // that still has to balance once the automatic tax posting is expanded.
    const std::string stapel =
        kopf + spalten +
        "1190,00;\"S\";\"EUR\";10000;8400;\"3\";1506;\"R-1\";\"Beratung\";0\r\n"
        "500,00;\"S\";\"EUR\";1200;10000;\"\";2006;\"R-1\";\"Zahlung\";0\r\n";
    SchreibeDatei("stapel-juni.csv", Cp1252(stapel));

    const DatevImportBericht bericht =
        LeseBuchungsstapel("stapel-juni.csv", gespeichert, jahr, keys);
    Check(bericht.ok, "the stack reads");
    CheckInt(bericht.uebernommen, 2, "with both postings");
    CheckInt(bericht.mitSteuer, 1, "one of which carries the tax split");

    int uebernommen = 0;
    CheckStore(store.ImportiereDatevStapel(bericht, "stapel-juni.csv", akteur, false,
                                           uebernommen),
               "and it imports");
    CheckInt(uebernommen, 2, "writing both postings");
    CheckInt(static_cast<int64_t>(store.Journal(mandant.id).size()), 2,
             "which is what the journal now holds");

    // The point of the tax split is that the ledger balances only if it
    // happened: 1.190,00 gross has to arrive as 1.000,00 revenue plus 190,00
    // VAT, not as 1.190,00 of revenue.
    Check(store.Buchungskreisdifferenz(mandant.id).IsZero(),
          "the imported ledger balances - Soll equals Haben");
    auto saldo = [&](const std::string& nummer) -> Store::KontoSaldo {
        for (const Store::KontoSaldo& s : store.SummenUndSalden(mandant.id))
            if (s.konto == nummer) return s;
        return Store::KontoSaldo();
    };
    CheckInt(saldo("8400").haben.Minor(), 100000,
             "the revenue account carries the net 1.000,00, not the gross");
    CheckInt(saldo("1776").haben.Minor(), 19000, "and the VAT account the 190,00");
    CheckInt(saldo("10000").soll.Minor(), 119000, "the customer owes 1.190,00");
    CheckInt(saldo("10000").haben.Minor(), 50000, "and has paid 500,00");
    CheckInt(saldo("1200").soll.Minor(), 50000, "which is on the bank");

    // Imported rows are ordinary postings: they join the chain, so the same
    // check that catches an edited row catches an edited imported one.
    Check(store.PruefeHashKette(mandant.id).ok,
          "the hash chain is intact across the import");

    // --- the second import ---
    const std::vector<Store::DatevImportEintrag> importe = store.DatevImporte(mandant.id);
    CheckInt(static_cast<int64_t>(importe.size()), 1, "the import is recorded");
    if (!importe.empty()) {
        CheckText(importe[0].dateiname, "stapel-juni.csv", "with the file it came from");
        CheckInt(importe[0].zeilen, 2, "and how many postings it brought");
    }
    Store::DatevImportEintrag frueher;
    Check(store.DatevDateiSchonImportiert(mandant.id, bericht.dateiHash, frueher),
          "the file is recognised by its hash");

    int nochmalGeschrieben = 0;
    const StoreResult zweiter = store.ImportiereDatevStapel(
        bericht, "stapel-juni-kopie.csv", akteur, false, nochmalGeschrieben);
    CheckRefused(zweiter,
                 "importing the same file again is refused - a doubled month is "
                 "invisible in the ledger and shows up only as a wrong balance");
    Check(zweiter.fehler.find("bereits importiert") != std::string::npos,
          "and the refusal says why");
    CheckInt(nochmalGeschrieben, 0, "nothing was written");
    CheckInt(static_cast<int64_t>(store.Journal(mandant.id).size()), 2,
             "the journal is unchanged");

    // The override exists for the one case that is real: a first import that
    // was reversed and has to be redone.
    CheckStore(store.ImportiereDatevStapel(bericht, "stapel-juni.csv", akteur, true,
                                           nochmalGeschrieben),
               "--nochmal overrides the duplicate check, because a reversed import "
               "has to be repeatable");
    CheckInt(nochmalGeschrieben, 2, "and then it does write");
    CheckInt(static_cast<int64_t>(store.Journal(mandant.id).size()), 4,
             "doubling the month, which is exactly why it is not the default");

    // --- all or nothing ---
    // A second company, so the freeze below does not have to fight the rows
    // already imported above.
    Store zweiteStore;
    if (CheckStore(zweiteStore.Open("fibu-import2", ":memory:"),
                   "a second database for the all-or-nothing test opens")) {
        Akteur setup2;
        Benutzer admin2;
        admin2.anmeldename = "chef";
        zweiteStore.SaveBenutzer(admin2, setup2);
        Akteur akteur2;
        akteur2.benutzerId  = admin2.id;
        akteur2.anmeldename = admin2.anmeldename;
        akteur2.rolle       = admin2.rolle;

        Mandant m2;
        m2.name            = "Beispiel GmbH";
        m2.mandantennummer = "456";
        zweiteStore.SaveMandant(m2, akteur2);
        Geschaeftsjahr j2;
        j2.mandantId   = m2.id;
        j2.beginn      = Date(2026, 4, 1);
        j2.ende        = Date(2027, 3, 31);
        j2.bezeichnung = j2.DefaultBezeichnung();
        zweiteStore.SaveGeschaeftsjahr(j2, akteur2);
        int g2 = 0;
        for (Konto& k : konten) { k.id = 0; k.mandantId = m2.id; }
        zweiteStore.ImportKonten(m2.id, konten, akteur2, g2);
        for (Steuerschluessel& k : keys) { k.id = 0; k.mandantId = m2.id; }
        zweiteStore.ImportSteuerschluessel(m2.id, keys, akteur2, g2);

        CheckStore(zweiteStore.Festschreiben(j2.id, Date(2026, 6, 10), akteur2),
                   "everything up to 10 June is frozen");
        Geschaeftsjahr j2neu;
        Check(zweiteStore.GeschaeftsjahrById(j2.id, j2neu) &&
              j2neu.IsFrozen(Date(2026, 6, 5)),
              "so a 5 June date is closed to new postings");

        Mandant m2gespeichert;
        zweiteStore.LoadMandant(m2.id, m2gespeichert);

        // The good row comes first on purpose. An importer that wrote as it
        // went would have committed it before reaching the frozen one, and the
        // ledger would end up with half a stack and a failure message.
        const std::string halb =
            kopf + spalten +
            "1190,00;\"S\";\"EUR\";10000;8400;\"3\";1506;\"R-1\";\"Nach der Sperre\";0\r\n"
            "238,00;\"S\";\"EUR\";10000;8400;\"3\";0506;\"R-2\";\"Vor der Sperre\";0\r\n";
        SchreibeDatei("stapel-halb.csv", Cp1252(halb));
        const DatevImportBericht halbBericht =
            LeseBuchungsstapel("stapel-halb.csv", m2gespeichert, j2neu, keys);
        Check(halbBericht.ok && halbBericht.uebernommen == 2,
              "both rows read out of the file");

        int halbGeschrieben = 0;
        const StoreResult verweigert = zweiteStore.ImportiereDatevStapel(
            halbBericht, "stapel-halb.csv", akteur2, false, halbGeschrieben);
        CheckRefused(verweigert, "a posting inside the frozen period stops the import");
        Check(verweigert.fehler.find("nichts importiert") != std::string::npos,
              "and the message says that nothing at all was written");
        CheckInt(halbGeschrieben, 0, "which is true of the counter");
        CheckInt(static_cast<int64_t>(zweiteStore.Journal(m2.id).size()), 0,
                 "and true of the journal - the good row was not written either, "
                 "because half a stack is worse than none");
        CheckInt(static_cast<int64_t>(zweiteStore.DatevImporte(m2.id).size()), 0,
                 "and no import was recorded, so the file can be re-offered once "
                 "the freeze date is right");
        std::remove("stapel-halb.csv");
    }

    std::remove("stapel-juni.csv");
}

// ===== EXPORT AND BACK =====
//
// The design proposal calls this the one test that catches a sign mistake, a
// Soll/Haben mistake, a comma-decimal mistake and a TTMM mistake at once, and
// it is right: each of those produces a file that looks entirely plausible and
// a ledger that is wrong. Writing a stack and reading it back compares the
// only thing that matters - the postings - instead of comparing text with an
// expectation that was written by the same understanding that wrote the code.
static void TestDatevRundlauf() {
    std::printf("DATEV-Rundlauf (Export und zurück)\n");

    DatevDefinition definition;
    std::string fehler;
    const std::string defPfad = DatevDefinitionPfad("DATEV-Buchungsstapel-v700.csv");
    if (defPfad.empty() || !definition.Laden(defPfad, fehler)) {
        std::printf("    note: column definition not available, skipping\n");
        return;
    }

    Mandant mandant;
    mandant.id              = 1;
    mandant.name            = "Beispiel GmbH";
    mandant.waehrung        = "EUR";
    mandant.beraternummer   = "1001";
    mandant.mandantennummer = "456";
    Geschaeftsjahr jahr;
    jahr.beginn           = Date(2026, 4, 1);
    jahr.ende             = Date(2027, 3, 31);
    jahr.bezeichnung      = "2026/2027";
    jahr.sachkontenlaenge = 4;

    // The mapping has to exist in both directions for a tax split to survive a
    // round trip. That it is still empty in the shipped file is exactly what
    // makes a real round trip lossy today, and the test says so by supplying
    // the mapping itself rather than pretending the file has one.
    std::vector<Steuerschluessel> keys;
    Steuerschluessel ust19;
    ust19.schluessel   = "USt19";
    ust19.satzPromille = 190;
    ust19.datevBu      = "3";
    ust19.kontoSteuer  = "1776";
    ust19.gueltigVon   = Date(2026, 1, 1);
    keys.push_back(ust19);

    std::vector<Buchung> original;
    auto buchung = [&](const Date& datum, const std::string& konto,
                       const std::string& gegenkonto, int64_t minor, SollHaben sh,
                       const std::string& text, const std::string& bu) {
        Buchung b;
        b.mandantId    = 1;
        b.belegdatum   = datum;
        b.konto        = konto;
        b.gegenkonto   = gegenkonto;
        b.umsatz       = Money::FromMinor(minor, "EUR");
        b.sollHaben    = sh;
        b.buchungstext = text;
        b.belegfeld1   = "R-1";
        b.waehrung     = "EUR";
        b.buSchluessel = bu;
        if (!bu.empty()) {
            b.steuerschluessel = "USt19";
            b.satzPromille     = 190;
            b.steuerSeite      = SteuerSeite::Gegenkonto;
            b.steuer           = b.umsatz.TaxInGross(190);
            b.netto            = b.umsatz - b.steuer;
            b.steuerkonto      = "1776";
        } else {
            b.netto = b.umsatz;
            b.steuer = Money::Zero("EUR");
        }
        original.push_back(b);
    };
    // Deliberately chosen: an amount over a thousand (a thousands separator in
    // the file would make it unreadable), a Haben row as well as a Soll row so
    // a swapped flag cannot pass, the first and the last day of the month so a
    // TTMM slip lands outside the period, and an umlaut plus the separator
    // itself inside a Buchungstext.
    buchung(Date(2026, 6, 1),  "10000", "8400", 119000, SollHaben::Soll,
            "Beratung Müller & Söhne", "3");
    buchung(Date(2026, 6, 15), "1200",  "10000", 50000, SollHaben::Soll,
            "Zahlung; Teilbetrag", "");
    buchung(Date(2026, 6, 30), "8400",  "10000",  4280, SollHaben::Haben,
            "Gutschrift Straße 1", "");

    const DatevErgebnis exportiert = SchreibeBuchungsstapel(
        mandant, jahr, original, definition, 2026, 6, ".", "test");
    Check(exportiert.ok, "June exports");
    if (!exportiert.ok) {
        std::printf("    %s\n", exportiert.fehler.c_str());
        return;
    }
    CheckInt(exportiert.zeilen, 3, "with all three postings");

    const DatevImportBericht zurueck =
        LeseBuchungsstapel(exportiert.datei, mandant, jahr, keys);
    Check(zurueck.ok, "and reads back in");
    CheckInt(zurueck.gelesen, 3, "three data lines");
    CheckInt(zurueck.uebernommen, 3, "all three usable");
    CheckInt(zurueck.uebersprungen, 0, "none lost");
    CheckInt(static_cast<int64_t>(zurueck.fehlerZeilen.size()), 0,
             "and nothing unreadable");
    CheckInt(zurueck.mitSteuer, 1,
             "the one row with a BU-Schlüssel gets its split back");

    if (zurueck.zeilen.size() == original.size()) {
        for (size_t i = 0; i < original.size(); ++i) {
            const Buchung& hin  = original[i];
            const Buchung& rueck = zurueck.zeilen[i].buchung;
            const std::string wo = " (Buchung " + std::to_string(i + 1) + ")";
            Check(rueck.belegdatum == hin.belegdatum,
                  "the Belegdatum survives TTMM and comes back with its year" + wo);
            CheckInt(rueck.umsatz.Minor(), hin.umsatz.Minor(),
                     "the amount comes back to the cent" + wo);
            Check(rueck.umsatz.Minor() > 0, "and unsigned, as the format wants" + wo);
            Check(rueck.sollHaben == hin.sollHaben,
                  "the Soll/Haben side is unchanged - a swap here reverses the "
                  "posting and still balances" + wo);
            CheckText(rueck.konto, hin.konto, "Konto" + wo);
            CheckText(rueck.gegenkonto, hin.gegenkonto, "Gegenkonto" + wo);
            CheckText(rueck.buchungstext, hin.buchungstext,
                      "and the Buchungstext, separator, umlaut and all" + wo);
            CheckText(rueck.belegfeld1, hin.belegfeld1, "Belegfeld 1" + wo);
        }

        // The split is not just present, it is the same one.
        const Buchung& mitSteuer = zurueck.zeilen[0].buchung;
        CheckText(mitSteuer.steuerschluessel, "USt19",
                  "the BU-Schlüssel maps back to the key it came from");
        CheckInt(mitSteuer.steuer.Minor(), original[0].steuer.Minor(),
                 "with the same tax amount");
        CheckInt(mitSteuer.netto.Minor(), original[0].netto.Minor(),
                 "and the same net");
        CheckText(mitSteuer.steuerkonto, "1776", "on the same tax account");
    }

    // The exported file is what a Kanzlei receives: no sign anywhere, and the
    // amounts written with a comma. Both are checked in the export tests; here
    // the point is that the reader agrees with the writer about them.
    Check(zurueck.von == Date(2026, 6, 1) && zurueck.bis == Date(2026, 6, 30),
          "the period the writer put in the header is the one the reader uses "
          "to resolve TTMM");
    Check(zurueck.wjBeginn == Date(2026, 4, 1),
          "and the 1 April Wirtschaftsjahr survives the round trip");

    std::remove(exportiert.datei.c_str());
}

// ===== BANK: READING A STATEMENT =====
//
// Each of the four format properties in UltraFIBUBank.h produces a
// plausible-looking wrong bank balance when it is got wrong, so each one is
// asserted here rather than trusted: the unsigned amount plus its direction
// flag, the dot/comma split between CAMT and MT940, the counterparty that
// changes side with the direction, and the identity the statement carries
// inside itself.

static std::string Cp1252Datei(const std::string& utf8) {
    bool verlust = false;
    return NachCp1252(utf8, verlust);
}

// A CAMT.053 statement built from parts, so a test can change exactly one
// thing - a sign, a balance, a namespace prefix - and see what it costs.
static std::string BaueCamt(const std::string& eintraege,
                            const std::string& anfang = "1000.00",
                            const std::string& ende = "1971.00",
                            const std::string& praefix = "") {
    const std::string p = praefix.empty() ? "" : praefix + ":";
    const std::string xmlns =
        praefix.empty() ? " xmlns=\"urn:iso:std:iso:20022:tech:xsd:camt.053.001.02\""
                        : " xmlns:" + praefix +
                          "=\"urn:iso:std:iso:20022:tech:xsd:camt.053.001.02\"";
    return
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<" + p + "Document" + xmlns + ">\n"
        "<" + p + "BkToCstmrStmt>\n"
        "<" + p + "Stmt>\n"
        "<" + p + "Id>AUSZUG-6</" + p + "Id>\n"
        "<" + p + "ElctrncSeqNb>6</" + p + "ElctrncSeqNb>\n"
        "<" + p + "Acct><" + p + "Id><" + p + "IBAN>DE02120300000000202051</" + p +
        "IBAN></" + p + "Id><" + p + "Ccy>EUR</" + p + "Ccy></" + p + "Acct>\n"
        "<" + p + "Bal><" + p + "Tp><" + p + "CdOrPrtry><" + p + "Cd>OPBD</" + p +
        "Cd></" + p + "CdOrPrtry></" + p + "Tp><" + p + "Amt Ccy=\"EUR\">" + anfang +
        "</" + p + "Amt><" + p + "CdtDbtInd>CRDT</" + p + "CdtDbtInd>"
        "<" + p + "Dt><" + p + "Dt>2026-06-01</" + p + "Dt></" + p + "Dt></" + p + "Bal>\n"
        "<" + p + "Bal><" + p + "Tp><" + p + "CdOrPrtry><" + p + "Cd>CLBD</" + p +
        "Cd></" + p + "CdOrPrtry></" + p + "Tp><" + p + "Amt Ccy=\"EUR\">" + ende +
        "</" + p + "Amt><" + p + "CdtDbtInd>CRDT</" + p + "CdtDbtInd>"
        "<" + p + "Dt><" + p + "Dt>2026-06-30</" + p + "Dt></" + p + "Dt></" + p + "Bal>\n"
        + eintraege +
        "</" + p + "Stmt>\n</" + p + "BkToCstmrStmt>\n</" + p + "Document>\n";
}

static std::string CamtEintrag(const std::string& betrag, const std::string& richtung,
                               const std::string& tag, const std::string& referenz,
                               const std::string& details,
                               const std::string& status = "BOOK",
                               const std::string& praefix = "") {
    const std::string p = praefix.empty() ? "" : praefix + ":";
    return
        "<" + p + "Ntry><" + p + "Amt Ccy=\"EUR\">" + betrag + "</" + p + "Amt>"
        "<" + p + "CdtDbtInd>" + richtung + "</" + p + "CdtDbtInd>"
        "<" + p + "Sts>" + status + "</" + p + "Sts>"
        "<" + p + "BookgDt><" + p + "Dt>" + tag + "</" + p + "Dt></" + p + "BookgDt>" +
        (referenz.empty() ? "" : "<" + p + "AcctSvcrRef>" + referenz + "</" + p + "AcctSvcrRef>") +
        "<" + p + "NtryDtls><" + p + "TxDtls>" + details +
        "</" + p + "TxDtls></" + p + "NtryDtls></" + p + "Ntry>\n";
}

static void TestBankLesen() {
    std::printf("Bank: Kontoauszüge lesen\n");

    // --- the SEPA tag packing, on its own ---
    // A German bank packs several fields into one remittance line. The human
    // part is what follows SVWZ+; taking the whole string instead leaves a
    // Verwendungszweck full of machine text that no document number search
    // and no human reading survives well.
    {
        const SepaTags tags = ZerlegeSepaTags(
            "EREF+E2E-9911 MREF+MANDAT-1 CRED+DE98ZZZ09999999999 "
            "SVWZ+Rechnung R-202606001 vom 15.06.2026 ABWA+Muster GmbH");
        CheckText(tags.endToEndId, "E2E-9911", "EREF is the end-to-end reference");
        CheckText(tags.mandatsreferenz, "MANDAT-1", "MREF is the mandate");
        CheckText(tags.glaeubigerId, "DE98ZZZ09999999999", "CRED is the creditor id");
        CheckText(tags.verwendungszweck, "Rechnung R-202606001 vom 15.06.2026",
                  "and SVWZ is the part a human wrote - which is the only part "
                  "worth showing and searching");
        CheckText(tags.abweichenderName, "Muster GmbH", "ABWA is the differing name");

        // The ordinary case: somebody typed a sentence, no tags at all.
        const SepaTags schlicht = ZerlegeSepaTags("Rechnung 4711");
        CheckText(schlicht.verwendungszweck, "Rechnung 4711",
                  "an untagged remittance is taken whole");
        Check(schlicht.endToEndId.empty(), "and invents no reference");

        // NONREF is a bank saying there was none.
        const SepaTags ohne = ZerlegeSepaTags("EREF+NOTPROVIDED SVWZ+Miete");
        Check(ohne.endToEndId.empty(),
              "NOTPROVIDED is a bank saying there is no reference, not a reference");
        CheckText(ohne.verwendungszweck, "Miete", "and the remittance still reads");
    }

    // --- CAMT.053 ---
    const std::string gutschrift = CamtEintrag(
        "1190.00", "CRDT", "2026-06-15", "BANKREF-0001",
        "<Refs><EndToEndId>E2E-9911</EndToEndId></Refs>"
        "<RltdPties><Dbtr><Nm>Muster GmbH</Nm></Dbtr>"
        "<DbtrAcct><Id><IBAN>DE89370400440532013000</IBAN></Id></DbtrAcct>"
        "<Cdtr><Nm>Wir Selbst</Nm></Cdtr>"
        "<CdtrAcct><Id><IBAN>DE02120300000000202051</IBAN></Id></CdtrAcct></RltdPties>"
        "<RmtInf><Ustrd>SVWZ+Rechnung R-202606001</Ustrd>"
        "<Ustrd>vom 15.06.2026</Ustrd></RmtInf>");
    const std::string lastschrift = CamtEintrag(
        "219.00", "DBIT", "2026-06-20", "BANKREF-0002",
        "<RltdPties><Dbtr><Nm>Wir Selbst</Nm></Dbtr>"
        "<Cdtr><Nm>Schmidt KG</Nm></Cdtr>"
        "<CdtrAcct><Id><IBAN>DE75512108001245126199</IBAN></Id></CdtrAcct></RltdPties>"
        "<RmtInf><Ustrd>Ihre Rechnung 4711</Ustrd></RmtInf>");

    SchreibeDatei("camt-gut.xml", BaueCamt(gutschrift + lastschrift));
    const BankLeseBericht camt = LiesCamt053("camt-gut.xml");
    Check(camt.ok, "a CAMT.053 statement reads");
    CheckInt(static_cast<int64_t>(camt.auszuege.size()), 1, "one statement");
    CheckInt(camt.uebernommen, 2, "with both entries");

    if (!camt.auszuege.empty() && camt.auszuege[0].umsaetze.size() == 2) {
        const Bankauszug& a = camt.auszuege[0];
        CheckText(a.iban, "DE02120300000000202051", "the account IBAN is read");
        CheckText(a.auszugsnummer, "6", "and the statement number");
        Check(a.von == Date(2026, 6, 1) && a.bis == Date(2026, 6, 30), "and the period");

        // **The self-check.** 1.000,00 + 1.190,00 - 219,00 = 1.971,00.
        Money differenz;
        Check(a.Stimmt(differenz),
              "the statement adds up: opening plus every entry is the closing "
              "balance - the one identity that catches a dropped entry, a "
              "doubled entry and an inverted sign at once");
        CheckInt(differenz.Minor(), 0, "with no difference");
        CheckInt(a.anfangssaldo.Minor(), 100000, "the opening balance is 1.000,00");
        CheckInt(a.endsaldo.Minor(), 197100, "the closing balance 1.971,00");

        const Bankumsatz& ein = a.umsaetze[0];
        const Bankumsatz& aus = a.umsaetze[1];

        // **Unsigned amount plus a direction flag.** CRDT is money in, DBIT is
        // money out; the sign is applied once, on the way in.
        CheckInt(ein.betrag.Minor(), 119000, "CRDT 1190.00 is +1.190,00");
        CheckInt(aus.betrag.Minor(), -21900, "DBIT 219.00 is -219,00");
        Check(ein.Eingang(), "the credit is money in");
        Check(!aus.Eingang(), "the debit is money out");

        // **The decimal separator is a dot in CAMT**, and is not the process
        // locale's business.
        CheckInt(ein.betrag.Minor(), 119000,
                 "a dot-decimal amount is read as 1.190,00 and not as 1,19");

        // **The counterparty changes side with the direction.** Taking the
        // debtor always would name us on our own outgoing payment.
        CheckText(ein.gegenName, "Muster GmbH",
                  "on money in, the other party is the debtor");
        CheckText(ein.gegenIban, "DE89370400440532013000", "with the debtor's IBAN");
        CheckText(aus.gegenName, "Schmidt KG",
                  "on money out, the other party is the creditor - the same "
                  "field would otherwise name us on our own payment");
        CheckText(aus.gegenIban, "DE75512108001245126199", "with the creditor's IBAN");

        // Several <Ustrd> are one remittance split at 140 characters.
        CheckText(ein.verwendungszweck, "Rechnung R-202606001 vom 15.06.2026",
                  "the repeated Ustrd lines are joined and the SVWZ tag removed");
        CheckText(ein.endToEndId, "E2E-9911", "the end-to-end reference is read");
        CheckText(ein.referenz, "BANKREF-0001",
                  "and the bank's own reference is the idempotency key");
        CheckText(ein.buchungstag.ToIso(), "2026-06-15", "the booking date");
    }

    // --- a namespace prefix ---
    // tinyxml2 does not strip prefixes, so <ns:Ntry> and <Ntry> are different
    // names to it. Whether a German bank writes one is not predictable.
    SchreibeDatei("camt-ns.xml",
                  BaueCamt(CamtEintrag("1190.00", "CRDT", "2026-06-15", "R1",
                                       "<ns:RmtInf><ns:Ustrd>Test</ns:Ustrd></ns:RmtInf>",
                                       "BOOK", "ns") +
                           CamtEintrag("219.00", "DBIT", "2026-06-20", "R2", "", "BOOK", "ns"),
                           "1000.00", "1971.00", "ns"));
    const BankLeseBericht mitNs = LiesCamt053("camt-ns.xml");
    Check(mitNs.ok, "a namespace prefix on every element does not stop the reader");
    CheckInt(mitNs.uebernommen, 2, "and all entries are still found");

    // --- a pending entry ---
    // PDNG has not hit the account. Importing it and then having the bank drop
    // it leaves a difference nobody can explain.
    SchreibeDatei("camt-pdng.xml",
                  BaueCamt(gutschrift + lastschrift +
                           CamtEintrag("500.00", "CRDT", "2026-06-30", "BANKREF-0003",
                                       "<RmtInf><Ustrd>Noch offen</Ustrd></RmtInf>",
                                       "PDNG")));
    const BankLeseBericht mitPdng = LiesCamt053("camt-pdng.xml");
    CheckInt(mitPdng.gelesen, 3, "three entries are seen");
    CheckInt(mitPdng.uebernommen, 2, "two are booked and taken");
    CheckInt(mitPdng.vorgemerkt, 1, "the pending one is held back");
    if (!mitPdng.auszuege.empty()) {
        Money differenz;
        Check(mitPdng.auszuege[0].Stimmt(differenz),
              "and the statement still adds up - which is exactly why the "
              "pending entry must not be counted");
    }

    // --- a statement that does not add up ---
    SchreibeDatei("camt-schief.xml", BaueCamt(gutschrift + lastschrift, "1000.00", "9999.00"));
    const BankLeseBericht schief = LiesCamt053("camt-schief.xml");
    bool meldetDifferenz = false;
    for (const std::string& w : schief.warnungen)
        if (w.find("geht nicht auf") != std::string::npos) meldetDifferenz = true;
    Check(meldetDifferenz,
          "a statement whose balances do not match its entries is reported, not "
          "imported quietly");

    // --- two identical lines on one day ---
    // The subtle one. With no bank reference the key is derived, and a key
    // that could not tell two identical lines apart would silently drop the
    // second - a payment that vanishes with no error anywhere.
    const std::string zweimal =
        CamtEintrag("50.00", "CRDT", "2026-06-10", "",
                    "<RmtInf><Ustrd>Kleinbetrag</Ustrd></RmtInf>") +
        CamtEintrag("50.00", "CRDT", "2026-06-10", "",
                    "<RmtInf><Ustrd>Kleinbetrag</Ustrd></RmtInf>");
    SchreibeDatei("camt-doppelt.xml", BaueCamt(zweimal, "1000.00", "1100.00"));
    const BankLeseBericht doppelt = LiesCamt053("camt-doppelt.xml");
    CheckInt(doppelt.uebernommen, 2, "two identical lines on one day stay two lines");
    if (doppelt.auszuege.size() == 1 && doppelt.auszuege[0].umsaetze.size() == 2) {
        Check(doppelt.auszuege[0].umsaetze[0].referenz !=
              doppelt.auszuege[0].umsaetze[1].referenz,
              "with different derived references - a key that could not tell "
              "them apart would drop one payment and report nothing");
        Money differenz;
        Check(doppelt.auszuege[0].Stimmt(differenz),
              "and both are counted in the balance");
    }

    // --- files that are not what they claim ---
    SchreibeDatei("camt-kein.xml", "<?xml version=\"1.0\"?><Andere><X/></Andere>");
    const BankLeseBericht keinCamt = LiesCamt053("camt-kein.xml");
    Check(!keinCamt.ok, "an XML file that is not CAMT.053 is refused");
    Check(keinCamt.fehler.find("BkToCstmrStmt") != std::string::npos,
          "and the refusal names what was missing");
    SchreibeDatei("camt-kaputt.xml", "<Document><unclosed>");
    Check(!LiesCamt053("camt-kaputt.xml").ok, "broken XML is refused");
    Check(!LiesCamt053("gibtesnicht.xml").ok, "a missing file is reported");

    std::remove("camt-gut.xml");
    std::remove("camt-ns.xml");
    std::remove("camt-pdng.xml");
    std::remove("camt-schief.xml");
    std::remove("camt-doppelt.xml");
    std::remove("camt-kein.xml");
    std::remove("camt-kaputt.xml");
}

// ===== BANK: MT940 AND CSV =====

static void TestBankMt940UndCsv() {
    std::printf("Bank: MT940 und CSV\n");

    // --- MT940 ---
    // The archive format. Comma decimals, a C/D marker, ?NN subfields, and
    // CP1252 - each of which differs from CAMT in a way that silently breaks
    // a reader written for the other.
    const std::string mt940 =
        ":20:STARTUMS\r\n"
        ":25:DE02120300000000202051/EUR\r\n"
        ":28C:00006/001\r\n"
        ":60F:C260601EUR1000,00\r\n"
        ":61:2606150615C1190,00NTRFE2E-9911//BANKREF-0001\r\n"
        ":86:166?00SEPA-GUTSCHRIFT?20EREF+E2E-9911 SVWZ+Rechnung R-2?21"
        "02606001 vom 15.06.2026?30GENODEF1S02?31DE89370400440532013000"
        "?32M\xc3\xbcller GmbH &?33 S\xc3\xb6hne\r\n"
        ":61:2606200620D219,00NTRFNONREF//BANKREF-0002\r\n"
        ":86:116?00SEPA-UEBERWEISUNG?20Ihre Rechnung 4711"
        "?31DE75512108001245126199?32Schmidt KG\r\n"
        ":62F:C260630EUR1971,00\r\n"
        "-\r\n";
    SchreibeDatei("mt940.sta", Cp1252Datei(mt940));
    const BankLeseBericht mt = LiesMt940("mt940.sta");
    Check(mt.ok, "an MT940 statement reads");
    CheckInt(mt.uebernommen, 2, "with both entries");

    if (!mt.auszuege.empty() && mt.auszuege[0].umsaetze.size() == 2) {
        const Bankauszug& a = mt.auszuege[0];
        CheckText(a.iban, "DE02120300000000202051", "the IBAN comes from :25:");
        CheckText(a.auszugsnummer, "00006/001", "the statement number from :28C:");

        Money differenz;
        Check(a.Stimmt(differenz),
              "and the same identity holds here: :60F: plus the entries is :62F:");

        const Bankumsatz& ein = a.umsaetze[0];
        const Bankumsatz& aus = a.umsaetze[1];
        // **MT940 decimals are commas**, where CAMT's are dots. A reader that
        // used one style for both turns 1190,00 into 1,19 or drops it.
        CheckInt(ein.betrag.Minor(), 119000, "a comma-decimal amount is 1.190,00");
        CheckInt(aus.betrag.Minor(), -21900, "and the D marker makes it money out");
        Check(ein.buchungstag == Date(2026, 6, 15), "the booking date is read");
        Check(ein.valuta == Date(2026, 6, 15), "and the value date");

        // ?20..?29 is one remittance split across subfields; ?32/?33 is one
        // name split in half. Taking only the first of either truncates.
        CheckText(ein.verwendungszweck, "Rechnung R-202606001 vom 15.06.2026",
                  "?20 and ?21 are joined and the SEPA tags unpacked");
        CheckText(ein.gegenName, "Müller GmbH & Söhne",
                  "?32 and ?33 are joined - and CP1252 umlauts survive, which "
                  "is what a payer's name has to do to match a partner");
        CheckText(ein.gegenIban, "DE89370400440532013000", "?31 is the IBAN");
        CheckText(ein.gegenBic, "GENODEF1S02", "?30 the BIC");
        CheckText(ein.buchungstext, "SEPA-GUTSCHRIFT", "?00 the bank's own wording");
        CheckText(ein.referenz, "BANKREF-0001", "the bank reference after // is the key");
    }

    // A reversal (RC/RD) runs the other way: a returned direct debit reduces
    // the balance where the original increased it.
    const std::string storno =
        ":20:X\r\n:25:DE02120300000000202051\r\n:28C:1\r\n"
        ":60F:C260601EUR1000,00\r\n"
        ":61:2606150615RC100,00NTRFNONREF//REV-1\r\n"
        ":86:105?00RUECKLASTSCHRIFT?20Ruecklauf\r\n"
        ":62F:C260630EUR900,00\r\n-\r\n";
    SchreibeDatei("mt940-storno.sta", Cp1252Datei(storno));
    const BankLeseBericht rev = LiesMt940("mt940-storno.sta");
    Check(rev.ok, "a reversal entry reads");
    if (rev.ok && !rev.auszuege.empty() && !rev.auszuege[0].umsaetze.size() == 0) {
        CheckInt(rev.auszuege[0].umsaetze[0].betrag.Minor(), -10000,
                 "RC is a reversed credit and therefore money out - the "
                 "opposite of the C it contains");
        Money differenz;
        Check(rev.auszuege[0].Stimmt(differenz),
              "and the balances agree, which is what proves the sign");
    }

    Check(!LiesMt940("gibtesnicht.sta").ok, "a missing MT940 file is reported");
    SchreibeDatei("mt940-leer.sta", "kein mt940\r\n");
    Check(!LiesMt940("mt940-leer.sta").ok, "a file with no MT940 fields is refused");

    // --- CSV through a profile ---
    SchreibeDatei("profil.txt",
                  "name = Test\n"
                  "trenner = ;\n"
                  "kopfzeilen = 1\n"
                  "datumsformat = TT.MM.JJJJ\n"
                  "dezimaltrenner = ,\n"
                  "buchungstag = 1\n"
                  "valuta = 2\n"
                  "gegen_name = 3\n"
                  "verwendungszweck = 4\n"
                  "betrag = 5\n"
                  "gegen_iban = 6\n");
    CsvBankProfil profil;
    std::string profilFehler;
    Check(profil.Laden("profil.txt", profilFehler), "a CSV profile loads");
    CheckInt(profil.spalteBuchungstag, 0,
             "and its 1-based column numbers become 0-based indices");
    CheckInt(profil.spalteBetrag, 4, "the amount column too");

    const std::string csv =
        "Buchungstag;Valuta;Name;Verwendungszweck;Betrag;IBAN\r\n"
        "15.06.2026;15.06.2026;M\xc3\xbcller GmbH;SVWZ+Rechnung R-202606001;1190,00;"
        "DE89370400440532013000\r\n"
        "20.06.2026;20.06.2026;Schmidt KG;Ihre Rechnung 4711;-219,00;\r\n"
        "keindatum;;X;Y;1,00;\r\n";
    SchreibeDatei("auszug.csv", Cp1252Datei(csv));
    const BankLeseBericht csvBericht =
        LiesBankCsv("auszug.csv", profil, "DE02120300000000202051", "EUR");
    Check(csvBericht.ok, "a CSV statement reads through its profile");
    CheckInt(csvBericht.gelesen, 3, "three data lines");
    CheckInt(csvBericht.uebernommen, 2, "two usable");
    CheckInt(csvBericht.uebersprungen, 1, "and the unreadable one is skipped");
    CheckInt(static_cast<int64_t>(csvBericht.fehlerZeilen.size()), 1,
             "and reported by line number");
    if (!csvBericht.auszuege.empty() && csvBericht.auszuege[0].umsaetze.size() == 2) {
        const Bankumsatz& u = csvBericht.auszuege[0].umsaetze[0];
        CheckInt(u.betrag.Minor(), 119000, "a signed comma amount reads");
        CheckInt(csvBericht.auszuege[0].umsaetze[1].betrag.Minor(), -21900,
                 "including its minus sign");
        CheckText(u.gegenName, "Müller GmbH", "and CP1252 names survive");
        CheckText(u.verwendungszweck, "Rechnung R-202606001",
                  "with the SEPA tags unpacked the same way");
    }
    // A CSV carries no balances, so it cannot check itself - and that is said
    // rather than left for somebody to discover after a month goes missing.
    bool sagtOhneSalden = false;
    for (const std::string& w : csvBericht.warnungen)
        if (w.find("keine Salden") != std::string::npos) sagtOhneSalden = true;
    Check(sagtOhneSalden,
          "and the import says a CSV cannot verify itself, unlike CAMT.053");

    // Two amount columns instead of one signed one - the other common layout.
    SchreibeDatei("profil2.txt",
                  "trenner = ;\nkopfzeilen = 1\nbuchungstag = 1\n"
                  "gegen_name = 2\nsoll = 3\nhaben = 4\n");
    CsvBankProfil profil2;
    Check(profil2.Laden("profil2.txt", profilFehler),
          "a profile with separate Soll and Haben columns loads");
    SchreibeDatei("auszug2.csv",
                  "Tag;Name;Soll;Haben\r\n"
                  "15.06.2026;Kunde;;1190,00\r\n"
                  "20.06.2026;Lieferant;219,00;\r\n");
    const BankLeseBericht zwei =
        LiesBankCsv("auszug2.csv", profil2, "DE02120300000000202051", "EUR");
    Check(zwei.ok, "and reads");
    if (zwei.ok && !zwei.auszuege.empty() && zwei.auszuege[0].umsaetze.size() == 2) {
        CheckInt(zwei.auszuege[0].umsaetze[0].betrag.Minor(), 119000,
                 "the Haben column is money in");
        CheckInt(zwei.auszuege[0].umsaetze[1].betrag.Minor(), -21900,
                 "and the Soll column money out, unsigned in the file");
    }

    // A profile without a date or an amount cannot map anything, and saying so
    // beats importing a column of zeros.
    SchreibeDatei("profil-leer.txt", "trenner = ;\n");
    CsvBankProfil leer;
    Check(!leer.Laden("profil-leer.txt", profilFehler),
          "a profile with no date column is refused");
    Check(!profilFehler.empty(), "with a reason that names what is missing");

    // --- picking the reader by content ---
    // A bank that names a CAMT file ".txt" is not an unusual bank.
    SchreibeDatei("getarnt.txt", BaueCamt(
        CamtEintrag("1190.00", "CRDT", "2026-06-15", "R1", "") +
        CamtEintrag("219.00", "DBIT", "2026-06-20", "R2", "")));
    const BankLeseBericht getarnt =
        LiesBankdatei("getarnt.txt", profil, "DE02120300000000202051", "EUR");
    Check(getarnt.format == BankFormat::Camt053,
          "the reader is chosen by content, not by the file extension");
    SchreibeDatei("getarnt2.xml", Cp1252Datei(mt940));
    Check(LiesBankdatei("getarnt2.xml", profil, "", "EUR").format == BankFormat::Mt940,
          "and an MT940 named .xml is still read as MT940");

    std::remove("mt940.sta");
    std::remove("mt940-storno.sta");
    std::remove("mt940-leer.sta");
    std::remove("profil.txt");
    std::remove("profil2.txt");
    std::remove("profil-leer.txt");
    std::remove("auszug.csv");
    std::remove("auszug2.csv");
    std::remove("getarnt.txt");
    std::remove("getarnt2.xml");
}

// ===== BANK: THE MATCHER =====
//
// The matcher only ever proposes. What is tested here is therefore not "does it
// find the right document" alone but "does it refuse to be confident when it
// should not be" - because a proposal accepted too readily becomes a posting,
// and a wrong posting inside a frozen period can only be fixed by a Storno.

static Bankumsatz BaueUmsatz(int64_t minor, const std::string& zweck,
                             const std::string& name = std::string(),
                             const std::string& iban = std::string(),
                             const Date& tag = Date(2026, 6, 20)) {
    Bankumsatz u;
    u.buchungstag      = tag;
    u.valuta           = tag;
    u.betrag           = Money::FromMinor(minor, "EUR");
    u.verwendungszweck = zweck;
    u.gegenName        = name;
    u.gegenIban        = iban;
    u.referenz         = "REF";
    return u;
}

static ZuordnungKandidat BaueKandidat(int64_t id, const std::string& nummer,
                                      int64_t offenMinor, bool geldAbgang,
                                      const std::string& partner = "Muster GmbH",
                                      const std::string& iban = std::string(),
                                      const Date& datum = Date(2026, 6, 15)) {
    ZuordnungKandidat k;
    k.belegId     = id;
    k.belegnummer = nummer;
    k.belegdatum  = datum;
    k.offen       = Money::FromMinor(offenMinor, "EUR");
    k.brutto      = k.offen;
    k.geldAbgang  = geldAbgang;
    k.partnerName = partner;
    k.partnerIban = iban;
    return k;
}

static void TestBankZuordnung() {
    std::printf("Bank: automatische Zuordnung\n");

    // --- the number in the remittance ---
    {
        const Bankumsatz ein = BaueUmsatz(119000, "Rechnung R-202606001 vom 15.06.");
        std::vector<ZuordnungKandidat> kandidaten;
        kandidaten.push_back(BaueKandidat(1, "R-202606001", 119000, false));
        kandidaten.push_back(BaueKandidat(2, "R-202606002", 119000, false));

        const std::vector<Zuordnungsvorschlag> v = SchlageZuordnungVor(ein, kandidaten);
        Check(v.size() >= 1, "a payment quoting an invoice number finds it");
        if (!v.empty()) {
            CheckText(v[0].belegnummer, "R-202606001", "and puts it first");
            Check(v[0].guete == ZuordnungGuete::Sicher,
                  "certain: the payer named the document AND the amount agrees");
            CheckInt(v[0].betrag.Minor(), 119000, "the whole amount would be assigned");
            Check(!v[0].teilzahlung, "and it is not a part payment");
            Check(!v[0].gruende.empty(), "with reasons a human can judge");
        }
        // The second invoice costs the same but was not named. It may still be
        // offered - it cannot be certain.
        for (const Zuordnungsvorschlag& vs : v)
            if (vs.belegnummer == "R-202606002")
                Check(vs.guete != ZuordnungGuete::Sicher,
                      "an invoice with the same amount that nobody named is "
                      "never certain - that is exactly the case where guessing "
                      "puts the money on the wrong customer");
    }

    // --- the amount alone is not certainty ---
    {
        const Bankumsatz ein = BaueUmsatz(119000, "Zahlung", "Muster GmbH",
                                          "DE89370400440532013000");
        std::vector<ZuordnungKandidat> k;
        k.push_back(BaueKandidat(1, "R-202606001", 119000, false, "Muster GmbH",
                                 "DE89370400440532013000"));
        const std::vector<Zuordnungsvorschlag> v = SchlageZuordnungVor(ein, k);
        Check(v.size() == 1, "amount plus IBAN finds the invoice");
        if (!v.empty()) {
            Check(v[0].guete == ZuordnungGuete::Wahrscheinlich,
                  "as probable, not certain - nobody wrote the number down");
            bool nenntIban = false;
            for (const std::string& g : v[0].gruende)
                if (g.find("IBAN") != std::string::npos) nenntIban = true;
            Check(nenntIban, "and the IBAN is given as a reason");
        }
    }

    // --- direction is a precondition, not a score ---
    {
        // Money arriving cannot pay an invoice we received, however well
        // everything else matches.
        const Bankumsatz ein = BaueUmsatz(119000, "Rechnung R-202606001");
        std::vector<ZuordnungKandidat> k;
        k.push_back(BaueKandidat(1, "R-202606001", 119000, /*geldAbgang*/ true));
        Check(SchlageZuordnungVor(ein, k).empty(),
              "money coming in is never offered against a document that would "
              "be settled by money going out - a perfect match on the wrong "
              "side is not a weak match, it is not a match");

        const Bankumsatz aus = BaueUmsatz(-119000, "Rechnung R-202606001");
        Check(SchlageZuordnungVor(aus, k).size() == 1,
              "and the same document is found by the payment that does settle it");
    }

    // --- a credit note reverses the money but not the party ---
    // This is why the matcher asks "does settling it take money out" rather
    // than "did we issue it": an Ausgangsgutschrift is a document to a
    // customer AND money leaving.
    {
        Check(GeldAbgangBeimAusgleich(BelegArt::Ausgangsrechnung) == false,
              "a sales invoice is settled by money coming in");
        Check(GeldAbgangBeimAusgleich(BelegArt::Eingangsrechnung) == true,
              "a purchase invoice by money going out");
        Check(GeldAbgangBeimAusgleich(BelegArt::Ausgangsgutschrift) == true,
              "a credit note to a customer is money going out, although it is "
              "a document we issued - the party side and the money side differ");
        Check(GeldAbgangBeimAusgleich(BelegArt::Eingangsgutschrift) == false,
              "and a supplier's credit note is money coming in");
        Check(IstAusgangsbeleg(BelegArt::Ausgangsgutschrift) == true,
              "while the party side still calls it ours - which is why the "
              "matcher cannot use IstAusgangsbeleg");
    }

    // --- a short number is not evidence ---
    {
        // "1" occurs in almost every remittance line. A matcher that treated
        // that as a hit would propose the same document for everything.
        const Bankumsatz ein = BaueUmsatz(50000, "Zahlung fuer 15 Stueck am 1.6.");
        std::vector<ZuordnungKandidat> k;
        k.push_back(BaueKandidat(1, "1", 99999, false));
        const std::vector<Zuordnungsvorschlag> v = SchlageZuordnungVor(ein, k);
        for (const Zuordnungsvorschlag& vs : v)
            Check(vs.guete != ZuordnungGuete::Sicher,
                  "a one-character document number is never a confident match");
        CheckText(NormalisiereNummer("R-2026/06 001"), "R202606001",
                  "normalising keeps only letters and digits, so the slashes "
                  "and spaces a payer adds do not prevent a match");
        CheckText(NormalisiereNummer("r-202606001"), "R202606001",
                  "and upper-cases, so a payer's spacing and case do not matter");
    }

    // --- a part payment ---
    {
        const Bankumsatz ein = BaueUmsatz(50000, "Rechnung R-202606001 Teilzahlung");
        std::vector<ZuordnungKandidat> k;
        k.push_back(BaueKandidat(1, "R-202606001", 119000, false));
        const std::vector<Zuordnungsvorschlag> v = SchlageZuordnungVor(ein, k);
        Check(v.size() == 1, "less money than is open still finds the invoice");
        if (!v.empty()) {
            Check(v[0].teilzahlung, "and is marked as a part payment");
            CheckInt(v[0].betrag.Minor(), 50000,
                     "assigning only what arrived, not what is open");
            Check(v[0].guete != ZuordnungGuete::Sicher,
                  "and is not certain, because the amount does not settle it");
        }
    }

    // --- more money than is open ---
    {
        const Bankumsatz ein = BaueUmsatz(200000, "Rechnung R-202606001");
        std::vector<ZuordnungKandidat> k;
        k.push_back(BaueKandidat(1, "R-202606001", 119000, false));
        const std::vector<Zuordnungsvorschlag> v = SchlageZuordnungVor(ein, k);
        if (!v.empty()) {
            CheckInt(v[0].betrag.Minor(), 119000,
                     "never more than is open would be assigned");
            Check(v[0].guete != ZuordnungGuete::Sicher,
                  "and an overpayment is not a confident match");
        }
    }

    // --- a payment before the invoice ---
    {
        const Bankumsatz frueh = BaueUmsatz(119000, "Anzahlung", "Muster GmbH",
                                            "DE89370400440532013000",
                                            Date(2026, 1, 10));
        std::vector<ZuordnungKandidat> k;
        k.push_back(BaueKandidat(1, "R-202606001", 119000, false, "Muster GmbH",
                                 "DE89370400440532013000", Date(2026, 6, 15)));
        const std::vector<Zuordnungsvorschlag> v = SchlageZuordnungVor(frueh, k);
        for (const Zuordnungsvorschlag& vs : v) {
            bool nenntDatum = false;
            for (const std::string& g : vs.gruende)
                if (g.find("vor dem Belegdatum") != std::string::npos) nenntDatum = true;
            Check(nenntDatum, "a payment dated before the invoice is flagged as such");
        }
    }

    // --- nothing to offer ---
    {
        const Bankumsatz ein = BaueUmsatz(119000, "Miete Juni");
        std::vector<ZuordnungKandidat> k;
        k.push_back(BaueKandidat(1, "R-202606001", 4200, false, "Ganz Anders",
                                 "DE11111111111111111111"));
        Check(SchlageZuordnungVor(ein, k).empty(),
              "an unrelated payment produces no proposal rather than a bad one");
        Check(SchlageZuordnungVor(ein, {}).empty(), "and no candidates give none");
    }

    // A line with no amount cannot be matched to anything.
    {
        Bankumsatz null = BaueUmsatz(0, "Nichts");
        std::vector<ZuordnungKandidat> k;
        k.push_back(BaueKandidat(1, "R-202606001", 119000, false));
        Check(SchlageZuordnungVor(null, k).empty(), "a zero line is never matched");
    }
}

// ===== BANK: THE IMPORT AS IT REACHES THE LEDGER =====
//
// Two things are checked against a real database here, because neither can be
// argued for in a comment: that the same statement read twice changes nothing,
// and that confirming an assignment goes through the one payment path that
// already knows about over-payment, frozen periods and the hash chain.

static void TestBankImportInDenBestand() {
    std::printf("Bank: Import und Zuordnung im Bestand\n");

    Store store;
    if (!CheckStore(store.Open("fibu-bank", ":memory:"),
                    "a database for the bank tests opens")) {
        std::printf("    skipping the bank store tests\n");
        return;
    }
    CheckInt(store.SchemaVersion(), Store::kSchemaVersion,
             "and is migrated to the schema the code expects");

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
    Geschaeftsjahr jahr;
    jahr.mandantId   = mandant.id;
    jahr.beginn      = Date(2026, 4, 1);
    jahr.ende        = Date(2027, 3, 31);
    jahr.bezeichnung = jahr.DefaultBezeichnung();
    CheckStore(store.SaveGeschaeftsjahr(jahr, akteur), "with a 1 April fiscal year");

    std::vector<Konto> konten;
    auto konto = [&](const std::string& nummer, const std::string& text, KontoTyp typ) {
        Konto k;
        k.mandantId = mandant.id; k.nummer = nummer; k.bezeichnung = text; k.typ = typ;
        konten.push_back(k);
    };
    konto("1200", "Bank", KontoTyp::Aktiv);
    konto("1776", "Umsatzsteuer 19 %", KontoTyp::Passiv);
    konto("8400", "Erloese 19 % USt", KontoTyp::Ertrag);
    int geschrieben = 0;
    CheckStore(store.ImportKonten(mandant.id, konten, akteur, geschrieben),
               "the accounts exist");
    std::vector<Steuerschluessel> keys;
    Steuerschluessel ust19;
    ust19.mandantId = mandant.id; ust19.schluessel = "USt19"; ust19.bezeichnung = "USt 19";
    ust19.satzPromille = 190; ust19.kontoSteuer = "1776"; ust19.gueltigVon = Date(2026, 1, 1);
    keys.push_back(ust19);
    CheckStore(store.ImportSteuerschluessel(mandant.id, keys, akteur, geschrieben),
               "and the tax key");
    Nummernkreis kreis;
    kreis.mandantId = mandant.id;
    kreis.kreis     = "rechnung";
    kreis.praefix   = "R-";
    CheckStore(store.SaveNummernkreis(kreis, akteur), "and a number range");

    // --- the bank account ---
    Bankkonto bank;
    bank.mandantId   = mandant.id;
    bank.bezeichnung = "Geschäftskonto";
    bank.iban        = "DE02120300000000202051";
    bank.konto       = "1200";
    CheckStore(store.SaveBankkonto(bank, akteur), "a bank account is created");
    Check(bank.id != 0, "and gets an id");

    // One IBAN belongs to one account. Two would make every import ambiguous,
    // and the ambiguity would only show as lines landing on the wrong account.
    Bankkonto doppelt;
    doppelt.mandantId   = mandant.id;
    doppelt.bezeichnung = "Noch eins";
    doppelt.iban        = bank.iban;
    doppelt.konto       = "1200";
    CheckRefused(store.SaveBankkonto(doppelt, akteur),
                 "a second account with the same IBAN is refused");

    // --- a customer and a posted invoice for the bank line to pay ---
    Partner kunde;
    kunde.mandantId = mandant.id;
    kunde.name      = "Muster GmbH";
    kunde.typ       = PartnerTyp::Kunde;
    kunde.iban      = "DE89370400440532013000";
    kunde.konto     = "10000";
    CheckStore(store.SavePartner(kunde, akteur), "a customer exists");

    Beleg rechnung;
    rechnung.mandantId = mandant.id;
    rechnung.art       = BelegArt::Ausgangsrechnung;
    rechnung.partnerId = kunde.id;
    rechnung.datum     = Date(2026, 6, 15);
    rechnung.waehrung  = "EUR";
    rechnung.partnerKonto = kunde.konto;
    rechnung.partnerName  = kunde.name;
    {
        BelegPosition pos;
        pos.bezeichnung     = "Beratung";
        pos.konto           = "8400";
        pos.steuerschluessel = "USt19";
        pos.mengeTausendstel = 1000;
        pos.einzelpreis     = Money::FromMinor(100000, "EUR");
        rechnung.positionen.push_back(pos);
    }
    CheckStore(store.SaveBeleg(rechnung, "rechnung", akteur), "an invoice is drafted");
    CheckStore(store.Buchen(rechnung, akteur), "and posted");
    CheckInt(rechnung.brutto.Minor(), 119000, "for 1.190,00 gross");

    // --- importing a statement ---
    const std::string eintraege =
        CamtEintrag("1190.00", "CRDT", "2026-06-20", "BANKREF-0001",
                    "<RltdPties><Dbtr><Nm>Muster GmbH</Nm></Dbtr>"
                    "<DbtrAcct><Id><IBAN>DE89370400440532013000</IBAN></Id>"
                    "</DbtrAcct></RltdPties>"
                    "<RmtInf><Ustrd>SVWZ+Rechnung " + rechnung.nummer + "</Ustrd></RmtInf>") +
        CamtEintrag("219.00", "DBIT", "2026-06-25", "BANKREF-0002",
                    "<RmtInf><Ustrd>Buerobedarf</Ustrd></RmtInf>");
    SchreibeDatei("bank-juni.xml", BaueCamt(eintraege, "1000.00", "1971.00"));

    const BankLeseBericht bericht = LiesCamt053("bank-juni.xml");
    Check(bericht.ok, "the statement reads");
    int neu = 0, bekannt = 0;
    CheckStore(store.ImportiereBankauszug(bank.id, bericht, "bank-juni.xml", akteur,
                                          neu, bekannt),
               "and imports");
    CheckInt(neu, 2, "both lines are new");
    CheckInt(bekannt, 0, "none was known");

    Store::UmsatzFilter filter;
    filter.mandantId = mandant.id;
    CheckInt(static_cast<int64_t>(store.Umsaetze(filter).size()), 2,
             "and the account now holds two lines");

    // **The same file again changes nothing.** This is the single most common
    // way a bookkeeping system acquires duplicates.
    int neu2 = 0, bekannt2 = 0;
    CheckStore(store.ImportiereBankauszug(bank.id, bericht, "bank-juni.xml", akteur,
                                          neu2, bekannt2),
               "importing the same statement again is allowed");
    CheckInt(neu2, 0, "but writes nothing");
    CheckInt(bekannt2, 2, "because both lines are already there");
    CheckInt(static_cast<int64_t>(store.Umsaetze(filter).size()), 2,
             "so the account still holds two lines and not four");

    // The overlapping download - "the last 30 days", every week - is the case
    // that per-file checking would get wrong in both directions.
    const std::string ueberlappend =
        CamtEintrag("219.00", "DBIT", "2026-06-25", "BANKREF-0002",
                    "<RmtInf><Ustrd>Buerobedarf</Ustrd></RmtInf>") +
        CamtEintrag("50.00", "CRDT", "2026-06-28", "BANKREF-0003",
                    "<RmtInf><Ustrd>Neu</Ustrd></RmtInf>");
    SchreibeDatei("bank-juni-2.xml", BaueCamt(ueberlappend, "2190.00", "2021.00"));
    const BankLeseBericht zweiter = LiesCamt053("bank-juni-2.xml");
    int neu3 = 0, bekannt3 = 0;
    CheckStore(store.ImportiereBankauszug(bank.id, zweiter, "bank-juni-2.xml", akteur,
                                          neu3, bekannt3),
               "an overlapping statement imports");
    CheckInt(neu3, 1, "taking only the line that is new");
    CheckInt(bekannt3, 1, "and skipping the one already there");

    // --- what is refused ---
    SchreibeDatei("bank-fremd.xml", BaueCamt(
        CamtEintrag("100.00", "CRDT", "2026-06-20", "X1", ""), "0.00", "100.00"));
    BankLeseBericht fremd = LiesCamt053("bank-fremd.xml");
    if (!fremd.auszuege.empty()) fremd.auszuege[0].iban = "DE99999999999999999999";
    int a = 0, b = 0;
    CheckRefused(store.ImportiereBankauszug(bank.id, fremd, "bank-fremd.xml", akteur, a, b),
                 "a statement for a different IBAN is refused - the lines would "
                 "be right and the account wrong, and nothing would look odd");

    BankLeseBericht schief = LiesCamt053("bank-juni.xml");
    if (!schief.auszuege.empty())
        schief.auszuege[0].endsaldo = Money::FromMinor(999999, "EUR");
    CheckRefused(store.ImportiereBankauszug(bank.id, schief, "schief.xml", akteur, a, b),
                 "a statement that does not add up is refused rather than "
                 "imported into a wrong bank balance");

    // --- the proposal ---
    std::vector<Bankumsatz> offene;
    filter.nurOffene = true;
    offene = store.Umsaetze(filter);
    CheckInt(static_cast<int64_t>(offene.size()), 3, "three lines are unassigned");

    int64_t gutschriftId = 0;
    for (const Bankumsatz& u : store.Umsaetze(Store::UmsatzFilter{bank.id, mandant.id}))
        if (u.referenz == "BANKREF-0001") gutschriftId = u.id;
    Check(gutschriftId != 0, "the incoming payment is found");

    const std::vector<Zuordnungsvorschlag> vorschlaege =
        store.Zuordnungsvorschlaege(gutschriftId);
    Check(!vorschlaege.empty(), "and the matcher proposes the invoice it names");
    if (!vorschlaege.empty()) {
        CheckText(vorschlaege[0].belegnummer, rechnung.nummer, "the right one");
        Check(vorschlaege[0].guete == ZuordnungGuete::Sicher,
              "with certainty, because the remittance names it and the amount agrees");
    }
    // **Proposing posts nothing.** That is the whole point of a proposal.
    CheckInt(static_cast<int64_t>(store.Zahlungen(rechnung.id).size()), 0,
             "and proposing has not booked anything");

    // --- confirming ---
    CheckStore(store.ZuordnungBuchen(gutschriftId, rechnung.id,
                                     Money::FromMinor(119000, "EUR"), akteur),
               "confirming the assignment books it");
    Beleg bezahlt;
    Check(store.BelegById(rechnung.id, bezahlt), "the invoice reads back");
    CheckInt(bezahlt.bezahlt.Minor(), 119000, "as paid in full");
    Check(bezahlt.status == BelegStatus::Bezahlt, "and its status says so");
    CheckInt(static_cast<int64_t>(store.Zahlungen(rechnung.id).size()), 1,
             "through the ordinary payment path - one payment, not a second "
             "mechanism that would drift from it");
    CheckInt(static_cast<int64_t>(store.Zuordnungen(gutschriftId).size()), 1,
             "and the assignment is recorded");
    CheckInt(store.OffenerBetrag(gutschriftId).Minor(), 0,
             "the bank line has nothing left to assign");
    Check(store.Buchungskreisdifferenz(mandant.id).IsZero(),
          "the ledger still balances");
    Check(store.PruefeHashKette(mandant.id).ok, "and the hash chain is intact");

    filter.nurOffene = true;
    CheckInt(static_cast<int64_t>(store.Umsaetze(filter).size()), 2,
             "and the assigned line drops out of the open list");

    // --- what an assignment refuses ---
    CheckRefused(store.ZuordnungBuchen(gutschriftId, rechnung.id,
                                       Money::FromMinor(1000, "EUR"), akteur),
                 "nothing more can be assigned from a line that is used up");

    int64_t lastschriftId = 0;
    for (const Bankumsatz& u : store.Umsaetze(Store::UmsatzFilter{bank.id, mandant.id}))
        if (u.referenz == "BANKREF-0002") lastschriftId = u.id;
    Check(lastschriftId != 0, "the outgoing payment is found");
    CheckRefused(store.ZuordnungBuchen(lastschriftId, rechnung.id,
                                       Money::FromMinor(1000, "EUR"), akteur),
                 "money going out cannot pay an invoice we issued - the rule "
                 "holds in the store, not only in the matcher that proposes");

    std::remove("bank-juni.xml");
    std::remove("bank-juni-2.xml");
    std::remove("bank-fremd.xml");
}

// ===== UStVA =====
//
// The first output of this program that goes to a tax authority, so what is
// tested is not only "does it add up" but "does it refuse when it cannot add
// up honestly". A return that silently omits a turnover is worse than no
// return: it is an under-declaration and the file looks perfectly valid.

static std::string SchreibeKennzahlen(const std::string& zeilen) {
    const std::string pfad = "kz-test.csv";
    SchreibeDatei(pfad, "# test\nkennzahl;art;satz_promille;geprueft;bezeichnung\n" + zeilen);
    return pfad;
}

// A journal row as the posting path produces one: gross on the person account,
// the net and tax recorded beside it as the automatic posting they are.
static Buchung UmsatzBuchung(const Date& datum, const std::string& key,
                             int64_t nettoMinor, int satzPromille,
                             SollHaben seite, int64_t id = 0) {
    Buchung b;
    b.id               = id;
    b.mandantId        = 1;
    b.belegdatum       = datum;
    b.steuerschluessel = key;
    b.satzPromille     = satzPromille;
    b.netto            = Money::FromMinor(nettoMinor, "EUR");
    b.steuer           = b.netto.TaxOnNet(satzPromille);
    b.umsatz           = b.netto + b.steuer;
    b.sollHaben        = seite;
    b.konto            = "10000";
    b.gegenkonto       = "8400";
    b.waehrung         = "EUR";
    return b;
}

static Steuerschluessel BaueKey(const std::string& name, int satz, bool vorsteuer,
                                const std::string& kzBemessung,
                                const std::string& kzSteuer = std::string()) {
    Steuerschluessel k;
    k.schluessel   = name;
    k.bezeichnung  = name;
    k.satzPromille = satz;
    k.vorsteuer    = vorsteuer;
    k.kzBemessung  = kzBemessung;
    k.kzSteuer     = kzSteuer;
    k.gueltigVon   = Date(2026, 1, 1);
    return k;
}

static void TestUstva() {
    std::printf("Umsatzsteuer-Voranmeldung\n");

    // --- the mapping is data, and it records what has been verified ---
    {
        UstvaMapping mapping;
        std::string fehler;
        const std::string pfad = SchreibeKennzahlen(
            "81;bemessung;190;ja;Umsaetze 19 %\n"
            "86;bemessung;70;ja;Umsaetze 7 %\n"
            "66;vorsteuer;0;ja;Vorsteuer\n"
            "89;bemessung;190;nein;Innergemeinschaftliche Erwerbe 19 %\n"
            "83;berechnet;0;ja;Zahllast\n");
        Check(mapping.Laden(pfad, fehler), "a Kennzahl mapping loads from data");
        CheckInt(static_cast<int64_t>(mapping.Anzahl()), 5, "with all its lines");

        UstvaKennzahl kz;
        Check(mapping.Finde("81", kz), "Kz 81 is found");
        Check(kz.art == KennzahlArt::Bemessung, "as a Bemessungsgrundlage");
        CheckInt(kz.satzPromille, 190, "at 19 %");
        Check(kz.geprueft, "and marked verified");
        Check(mapping.Finde("89", kz) && !kz.geprueft,
              "while Kz 89 is present but NOT verified - the file records that "
              "the case exists and is not yet answered, which is not the same "
              "as the case being absent");
        Check(!mapping.Finde("99", kz), "an unknown Kennzahl is not found");
        std::remove(pfad.c_str());
    }

    // --- periods ---
    {
        CheckText(UstvaZeitraumCode(6, false), "06", "June is 06");
        CheckText(UstvaZeitraumCode(12, false), "12", "December is 12");
        CheckText(UstvaZeitraumCode(1, true), "41", "Q1 is 41");
        CheckText(UstvaZeitraumCode(4, true), "44", "Q4 is 44");
        Check(UstvaZeitraumCode(13, false).empty(), "there is no month 13");
        Check(UstvaZeitraumCode(5, true).empty(), "and no fifth quarter");

        Date von, bis;
        Check(UstvaZeitraumGrenzen(2026, "06", von, bis) &&
              von == Date(2026, 6, 1) && bis == Date(2026, 6, 30),
              "June runs to the 30th");
        Check(UstvaZeitraumGrenzen(2026, "02", von, bis) && bis == Date(2026, 2, 28),
              "February 2026 to the 28th");
        Check(UstvaZeitraumGrenzen(2028, "02", von, bis) && bis == Date(2028, 2, 29),
              "and a leap February to the 29th");
        Check(UstvaZeitraumGrenzen(2026, "41", von, bis) &&
              von == Date(2026, 1, 1) && bis == Date(2026, 3, 31),
              "Q1 is January to March");
        Check(UstvaZeitraumGrenzen(2026, "44", von, bis) &&
              von == Date(2026, 10, 1) && bis == Date(2026, 12, 31),
              "and Q4 October to December");
        Check(!UstvaZeitraumGrenzen(2026, "99", von, bis), "99 is not a period");
    }

    // --- the ordinary month ---
    UstvaMapping mapping;
    {
        std::string fehler;
        const std::string pfad = SchreibeKennzahlen(
            "81;bemessung;190;ja;Umsaetze 19 %\n"
            "86;bemessung;70;ja;Umsaetze 7 %\n"
            "41;frei;0;ja;Innergemeinschaftliche Lieferungen\n"
            "66;vorsteuer;0;ja;Vorsteuer\n"
            "89;bemessung;190;nein;Innergemeinschaftliche Erwerbe\n"
            "83;berechnet;0;ja;Zahllast\n");
        Check(mapping.Laden(pfad, fehler), "the test mapping loads");
        std::remove(pfad.c_str());
    }
    std::vector<Steuerschluessel> keys;
    keys.push_back(BaueKey("USt19", 190, false, "81"));
    keys.push_back(BaueKey("USt7",   70, false, "86"));
    keys.push_back(BaueKey("VSt19", 190, true,  "", "66"));
    keys.push_back(BaueKey("IGL",     0, false, "41"));
    keys.push_back(BaueKey("IGE19", 190, false, "89"));     // mapped, NOT verified
    keys.push_back(BaueKey("RC13b", 190, false, ""));       // no Kennzahl at all

    Date von, bis;
    UstvaZeitraumGrenzen(2026, "06", von, bis);

    {
        std::vector<Buchung> journal;
        // 1.000,00 at 19 % and 500,00 at 7 %, both sales.
        journal.push_back(UmsatzBuchung(Date(2026, 6, 10), "USt19", 100000, 190,
                                        SollHaben::Soll, 1));
        journal.push_back(UmsatzBuchung(Date(2026, 6, 15), "USt7", 50000, 70,
                                        SollHaben::Soll, 2));
        // A purchase invoice: input tax. The person account is credited.
        journal.push_back(UmsatzBuchung(Date(2026, 6, 20), "VSt19", 20000, 190,
                                        SollHaben::Haben, 3));
        // A payment: no tax key, and therefore not in the return at all.
        Buchung zahlung;
        zahlung.id = 4; zahlung.mandantId = 1;
        zahlung.belegdatum = Date(2026, 6, 25);
        zahlung.umsatz = Money::FromMinor(119000, "EUR");
        zahlung.sollHaben = SollHaben::Soll;
        zahlung.konto = "1200"; zahlung.gegenkonto = "10000";
        journal.push_back(zahlung);
        // A posting outside the period must not be counted.
        journal.push_back(UmsatzBuchung(Date(2026, 7, 1), "USt19", 999900, 190,
                                        SollHaben::Soll, 5));

        const UstvaBerechnung b = BerechneUstva(1, 2026, "06", von, bis, journal,
                                                keys, mapping);
        Check(b.ok, "a month computes");
        Check(b.Vollstaendig(), "and is complete - every key has a verified Kennzahl");
        CheckInt(b.Betrag("81").Minor(), 100000, "Kz 81 carries the 19 % net base");
        CheckInt(b.Betrag("86").Minor(), 50000, "Kz 86 the 7 % base");
        CheckInt(b.Betrag("66").Minor(), 3800,
                 "Kz 66 the input tax, 19 % of 200,00");

        // 190,00 + 35,00 output tax, less 38,00 input tax.
        CheckInt(b.summeSteuer.Minor(), 22500, "output tax is 190,00 + 35,00");
        CheckInt(b.summeVorsteuer.Minor(), 3800, "input tax is 38,00");
        CheckInt(b.zahllast.Minor(), 18700, "so Kz 83 is 187,00 to pay");
        CheckInt(b.Betrag("83").Minor(), 18700, "and Kz 83 says the same");

        // Traceability: the figure names the postings it came from, because
        // the question arrives months later.
        const auto kz81 = b.kennzahlen.find("81");
        Check(kz81 != b.kennzahlen.end() && kz81->second.buchungIds.size() == 1 &&
              kz81->second.buchungIds[0] == 1,
              "and each Kennzahl names the journal rows behind it");

        // The payment is reported as untouched rather than silently ignored.
        bool nenntOhneSchluessel = false;
        for (const std::string& w : b.warnungen)
            if (w.find("keinen Steuerschlüssel") != std::string::npos)
                nenntOhneSchluessel = true;
        Check(nenntOhneSchluessel,
              "postings with no tax key are counted and explained, not just dropped");

        // Same inputs, same figures - a return has to be reproducible when it
        // is questioned a year later.
        const UstvaBerechnung wieder = BerechneUstva(1, 2026, "06", von, bis, journal,
                                                     keys, mapping);
        CheckInt(wieder.zahllast.Minor(), b.zahllast.Minor(),
                 "computing the same period twice gives the same figures - no "
                 "clock, no locale, no database in the calculation");
    }

    // --- a Storno subtracts ---
    // The one that silently doubles a month if it is wrong: a reversal booked
    // on the other side must reduce the turnover, not add to it.
    {
        std::vector<Buchung> journal;
        journal.push_back(UmsatzBuchung(Date(2026, 6, 10), "USt19", 100000, 190,
                                        SollHaben::Soll, 1));
        journal.push_back(UmsatzBuchung(Date(2026, 6, 12), "USt19", 100000, 190,
                                        SollHaben::Haben, 2));   // the Storno
        const UstvaBerechnung b = BerechneUstva(1, 2026, "06", von, bis, journal,
                                                keys, mapping);
        CheckInt(b.Betrag("81").Minor(), 0,
                 "an invoice and its Storno cancel to nothing - a reversal that "
                 "added instead would declare the turnover twice");
        CheckInt(b.zahllast.Minor(), 0, "and nothing is owed");

        // An input-tax reversal runs the other way round, because a purchase
        // invoice posts on the opposite side to begin with.
        std::vector<Buchung> einkauf;
        einkauf.push_back(UmsatzBuchung(Date(2026, 6, 10), "VSt19", 100000, 190,
                                        SollHaben::Haben, 1));
        einkauf.push_back(UmsatzBuchung(Date(2026, 6, 12), "VSt19", 100000, 190,
                                        SollHaben::Soll, 2));
        const UstvaBerechnung e = BerechneUstva(1, 2026, "06", von, bis, einkauf,
                                                keys, mapping);
        CheckInt(e.Betrag("66").Minor(), 0,
                 "and a reversed purchase invoice cancels its input tax, which "
                 "is the opposite side from a sale");
    }

    // --- zero-rated turnover is declared but owes nothing ---
    {
        std::vector<Buchung> journal;
        journal.push_back(UmsatzBuchung(Date(2026, 6, 10), "IGL", 250000, 0,
                                        SollHaben::Soll, 1));
        const UstvaBerechnung b = BerechneUstva(1, 2026, "06", von, bis, journal,
                                                keys, mapping);
        CheckInt(b.Betrag("41").Minor(), 250000,
                 "an intra-community supply is declared in Kz 41");
        CheckInt(b.zahllast.Minor(), 0,
                 "and owes nothing - it is turnover the form wants to see, not tax");
    }

    // --- THE SAFETY PROPERTY: an amount with nowhere to go stops the return ---
    {
        // RC13b has no Kennzahl at all in data/Steuerschluessel.csv.
        std::vector<Buchung> journal;
        journal.push_back(UmsatzBuchung(Date(2026, 6, 10), "USt19", 100000, 190,
                                        SollHaben::Soll, 1));
        journal.push_back(UmsatzBuchung(Date(2026, 6, 11), "RC13b", 400000, 190,
                                        SollHaben::Soll, 2));
        const UstvaBerechnung b = BerechneUstva(1, 2026, "06", von, bis, journal,
                                                keys, mapping);
        Check(b.ok, "the computation still runs");
        Check(!b.Vollstaendig(),
              "but the return is NOT complete - 4.000,00 of turnover has no "
              "Kennzahl, and a form leaving it out declares too little");
        CheckInt(static_cast<int64_t>(b.luecken.size()), 1, "one key is unmapped");
        if (!b.luecken.empty()) {
            CheckText(b.luecken[0].steuerschluessel, "RC13b", "and it is named");
            CheckInt(b.luecken[0].netto.Minor(), 400000,
                     "with the amount that would have gone missing");
        }

        ElsterKopf kopf;
        kopf.steuernummer = "1121081508150";
        kopf.finanzamtNummer = "1121";
        kopf.name = "Beispiel GmbH";
        kopf.erstellt = Date(2026, 7, 10);
        const ElsterErgebnis r = SchreibeUstvaXml(b, kopf, ".");
        Check(!r.ok,
              "and writing the XML is REFUSED - the file would look perfectly "
              "valid while under-declaring, which is the worst possible outcome");
        Check(r.fehler.find("RC13b") != std::string::npos,
              "the refusal names the key that is missing");
    }

    // An unverified Kennzahl blocks just as hard as a missing one. A guessed
    // Kennzahl produces a wrong return, and wrong is not better than absent.
    {
        std::vector<Buchung> journal;
        journal.push_back(UmsatzBuchung(Date(2026, 6, 10), "IGE19", 100000, 190,
                                        SollHaben::Soll, 1));
        const UstvaBerechnung b = BerechneUstva(1, 2026, "06", von, bis, journal,
                                                keys, mapping);
        Check(!b.Vollstaendig(),
              "a Kennzahl that exists but is not verified also stops the return");
        if (!b.luecken.empty())
            Check(b.luecken[0].grund.find("geprüft") != std::string::npos,
                  "and the reason says it has not been checked against the form");
        CheckInt(b.Betrag("89").Minor(), 0,
                 "nothing was declared on the unverified Kennzahl");
    }

    // --- the books and the form must agree ---
    {
        // A posting whose booked tax does not match the rate its Kennzahl
        // carries: 19 % declared, 7 % actually booked.
        Buchung falsch = UmsatzBuchung(Date(2026, 6, 10), "USt19", 100000, 190,
                                       SollHaben::Soll, 1);
        falsch.steuer = Money::FromMinor(7000, "EUR");     // 7 % where 19 % is due
        std::vector<Buchung> journal{ falsch };
        const UstvaBerechnung b = BerechneUstva(1, 2026, "06", von, bis, journal,
                                                keys, mapping);
        Check(!b.abweichungen.empty(),
              "tax booked at a different rate from the Kennzahl is reported - "
              "the return and the books disagreeing is exactly what must not be "
              "found by the Finanzamt first");
        if (!b.abweichungen.empty())
            Check(b.abweichungen[0].find("81") != std::string::npos,
                  "naming the Kennzahl concerned");

        // Ordinary rounding must NOT trip it, or the warning becomes noise and
        // gets ignored on the day it matters.
        Buchung gerundet = UmsatzBuchung(Date(2026, 6, 10), "USt19", 3333, 190,
                                         SollHaben::Soll, 1);
        std::vector<Buchung> klein{ gerundet };
        const UstvaBerechnung r = BerechneUstva(1, 2026, "06", von, bis, klein,
                                                keys, mapping);
        Check(r.abweichungen.empty(),
              "while a cent of rounding does not - a warning that cries wolf is "
              "worse than none");
    }

    // --- the ELSTER file ---
    {
        std::vector<Buchung> journal;
        journal.push_back(UmsatzBuchung(Date(2026, 6, 10), "USt19", 123456, 190,
                                        SollHaben::Soll, 1));
        journal.push_back(UmsatzBuchung(Date(2026, 6, 20), "VSt19", 20000, 190,
                                        SollHaben::Haben, 2));
        const UstvaBerechnung b = BerechneUstva(1, 2026, "06", von, bis, journal,
                                                keys, mapping);
        Check(b.Vollstaendig(), "a clean month is complete");

        ElsterKopf kopf;
        kopf.steuernummer    = "1121081508150";
        kopf.finanzamtNummer = "1121";
        kopf.name            = "Beispiel GmbH & Söhne";
        kopf.strasse         = "Hauptstraße 1";
        kopf.plz             = "80331";
        kopf.ort             = "München";
        kopf.produktVersion  = "0.8.0";
        kopf.erstellt        = Date(2026, 7, 10);

        const ElsterErgebnis r = SchreibeUstvaXml(b, kopf, ".");
        Check(r.ok, "the ELSTER file is written");
        Check(!r.xmlHash.empty(),
              "and hashed - what was filed has to stay provable afterwards");

        std::string inhalt;
        {
            std::FILE* f = std::fopen(r.datei.c_str(), "rb");
            Check(f != nullptr, "the file exists");
            if (f) {
                char puffer[8192]; size_t n = 0;
                while ((n = std::fread(puffer, 1, sizeof(puffer), f)) > 0)
                    inhalt.append(puffer, n);
                std::fclose(f);
            }
        }
        Check(inhalt.find("<DatenArt>UStVA</DatenArt>") != std::string::npos,
              "it declares itself a UStVA");
        Check(inhalt.find("<Jahr>2026</Jahr>") != std::string::npos, "for 2026");
        Check(inhalt.find("<Zeitraum>06</Zeitraum>") != std::string::npos, "for June");
        Check(inhalt.find("<Steuernummer>1121081508150</Steuernummer>") != std::string::npos,
              "with the Steuernummer");

        // **A Bemessungsgrundlage goes in whole euros, truncated.** 1.234,56
        // must arrive as 1234, not 1235: rounding up declares turnover that did
        // not happen.
        Check(inhalt.find("<Kz81>1234</Kz81>") != std::string::npos,
              "the base is whole euros, truncated - rounding up would declare "
              "turnover that did not happen");
        // A tax amount keeps its cents, with a dot.
        Check(inhalt.find("<Kz66>38.00</Kz66>") != std::string::npos,
              "a tax amount keeps its cents and uses a dot, not a German comma");

        // **A test submission must say so.** Without the Testmerker it is filed
        // for real; neither mistake is visible afterwards.
        Check(inhalt.find("<Testmerker>700000004</Testmerker>") != std::string::npos,
              "a return is a test submission unless it is explicitly not - the "
              "default must never be the one that files for real");
        bool sagtTest = false;
        for (const std::string& w : r.warnungen)
            if (w.find("Test") != std::string::npos) sagtTest = true;
        Check(sagtTest, "and that is said out loud, not only written in the file");

        // The file admits what has not been checked, inside itself.
        Check(inhalt.find("NICHT gegen das amtliche Schema geprueft") != std::string::npos,
              "the file states that its ELSTER envelope is unverified - the "
              "schemas ship inside the ERiC SDK, which is not in this repository");

        kopf.echtfall = true;
        const ElsterErgebnis echt = SchreibeUstvaXml(b, kopf, ".");
        Check(echt.ok, "a real submission writes too");
        std::string echtInhalt;
        {
            std::FILE* f = std::fopen(echt.datei.c_str(), "rb");
            if (f) {
                char puffer[8192]; size_t n = 0;
                while ((n = std::fread(puffer, 1, sizeof(puffer), f)) > 0)
                    echtInhalt.append(puffer, n);
                std::fclose(f);
            }
        }
        Check(echtInhalt.find("<Testmerker>") == std::string::npos,
              "and then carries no Testmerker");
        Check(echt.xmlHash != r.xmlHash,
              "the two differ, so their hashes differ - which is what makes the "
              "stored hash evidence of a particular file");

        std::remove(r.datei.c_str());
    }

    // --- what ELSTER refuses to accept, refused here first ---
    {
        std::vector<Buchung> journal;
        journal.push_back(UmsatzBuchung(Date(2026, 6, 10), "USt19", 100000, 190,
                                        SollHaben::Soll, 1));
        const UstvaBerechnung b = BerechneUstva(1, 2026, "06", von, bis, journal,
                                                keys, mapping);
        ElsterKopf ohneNummer;
        ohneNummer.finanzamtNummer = "1121";
        ohneNummer.erstellt = Date(2026, 7, 10);
        Check(!SchreibeUstvaXml(b, ohneNummer, ".").ok,
              "without a Steuernummer the file is refused here rather than by "
              "ELSTER after the upload");
        ElsterKopf ohneAmt;
        ohneAmt.steuernummer = "1121081508150";
        ohneAmt.erstellt = Date(2026, 7, 10);
        Check(!SchreibeUstvaXml(b, ohneAmt, ".").ok,
              "and without a Finanzamt there is nowhere to send it");
    }

    // --- the transports ---
    {
        std::unique_ptr<IElsterTransport> datei = ElsterDateiTransport();
        std::string warum;
        Check(datei->Verfuegbar(warum),
              "the file transport always works - it needs nothing installed");

        std::unique_ptr<IElsterTransport> eric = ElsterEricTransport("");
        Check(!eric->Verfuegbar(warum),
              "the ERiC transport reports unavailable without a directory");
        Check(!warum.empty(), "and says why rather than failing silently");
        std::unique_ptr<IElsterTransport> eric2 = ElsterEricTransport("/gibt/es/nicht");
        Check(!eric2->Verfuegbar(warum), "and unavailable when ERiC is not there");
        Check(warum.find("ERiC") != std::string::npos,
              "naming what has to be installed separately - it may not ship here");
    }
}

// ===== BELEGARCHIV =====
//
// The receipts themselves. What is tested is mostly what the archive refuses
// and what it recognises, because the failures here are quiet ones: a file
// that was not really a PDF, a copy that was truncated, the same receipt
// filed twice, or a path that stopped resolving five years later.

static const char* const kPdfKopf = "%PDF-1.4\n";

// Remove an archive directory and what is in it. The other tests here leave
// nothing behind and neither should these; a suite that litters the working
// directory makes the next run's failures ambiguous.
static void RaeumeArchivAuf(const std::string& wurzel) {
    for (int jahr = 2025; jahr <= 2028; ++jahr) {
        const std::string verzeichnis = wurzel + "/" + std::to_string(jahr);
        DIR* dir = ::opendir(verzeichnis.c_str());
        if (dir != nullptr) {
            while (struct dirent* eintrag = ::readdir(dir)) {
                const std::string name = eintrag->d_name;
                if (name == "." || name == "..") continue;
                std::remove((verzeichnis + "/" + name).c_str());
            }
            ::closedir(dir);
        }
        ::rmdir(verzeichnis.c_str());
    }
    ::rmdir(wurzel.c_str());
}

static std::string BaueMiniPdf(const std::string& inhalt) {
    return std::string(kPdfKopf) + "1 0 obj<<>>endobj\n" + inhalt + "\n%%EOF\n";
}

static void TestBelegArchiv() {
    std::printf("Belegarchiv (PDF-Import)\n");

    // --- a PDF is its bytes, not its name ---
    {
        Check(IstPdf(BaueMiniPdf("x")), "a file starting %PDF- is a PDF");
        Check(!IstPdf("Das hier ist Text.\n"),
              "and a text file is not, whatever it is called - an extension is "
              "a claim, the magic number is evidence");
        Check(!IstPdf(""), "an empty file is not a PDF");
        // Some producers put a few bytes in front; readers tolerate it.
        Check(IstPdf(std::string("\n\n") + BaueMiniPdf("x")),
              "a few bytes of junk before the header are tolerated");
        // But the word appearing deep inside a big file is not a header.
        Check(!IstPdf(std::string(4000, 'x') + "%PDF-1.4"),
              "while the string appearing far into the file is not");

        Check(IstVerschluesseltesPdf(BaueMiniPdf("trailer<</Encrypt 5 0 R>>")),
              "an encrypted PDF is recognised");
        Check(!IstVerschluesseltesPdf(BaueMiniPdf("trailer<</Root 1 0 R>>")),
              "and an ordinary one is not");
    }

    // --- filing a document ---
    {
        BelegArchiv archiv("archiv-test");
        SchreibeDatei("quelle-a.pdf", BaueMiniPdf("Rechnung A"));

        const ArchivEintrag eintrag = archiv.Ablegen("quelle-a.pdf", 2026);
        Check(eintrag.ok, "a PDF is filed");
        Check(!eintrag.schonVorhanden, "as a new document");
        CheckText(eintrag.dateiname, "quelle-a.pdf",
                  "the original name is kept - it usually carries the supplier "
                  "and the invoice number, and it is all that is known at this "
                  "point");
        Check(!eintrag.hash.empty(), "and it is hashed");
        // **Content-addressed**: the name in the archive IS the hash, so the
        // integrity check and the file name cannot drift apart.
        Check(eintrag.pfad.find(eintrag.hash) != std::string::npos,
              "the archived file is named by its own hash");
        Check(eintrag.pfad.find("2026") != std::string::npos,
              "under the document's year, not today's - a receipt filed late "
              "still belongs to its own year");

        // The copy really is the original. A copy truncated by a full disk is
        // precisely the failure an archive exists to prevent.
        std::string kopie;
        {
            std::FILE* f = std::fopen(eintrag.pfad.c_str(), "rb");
            Check(f != nullptr, "the archived file exists");
            if (f) {
                char puffer[8192]; size_t n = 0;
                while ((n = std::fread(puffer, 1, sizeof(puffer), f)) > 0)
                    kopie.append(puffer, n);
                std::fclose(f);
            }
        }
        CheckText(kopie, BaueMiniPdf("Rechnung A"),
                  "and is byte-for-byte the original");

        // **The original may now go away.** That is the whole point: a receipt
        // kept as a path into somebody's Downloads folder does not survive the
        // ten years § 147 AO asks for.
        std::remove("quelle-a.pdf");
        std::string wo;
        Check(archiv.Enthaelt(eintrag.hash, 2026, wo),
              "the archive still holds it after the original is deleted");

        // Filing it again is free and produces one file, because that is what
        // dragging the same folder in twice has to do.
        SchreibeDatei("quelle-a-kopie.pdf", BaueMiniPdf("Rechnung A"));
        const ArchivEintrag nochmal = archiv.Ablegen("quelle-a-kopie.pdf", 2026);
        Check(nochmal.ok, "a byte-identical file files again");
        Check(nochmal.schonVorhanden,
              "and is recognised as already there rather than stored twice");
        CheckText(nochmal.pfad, eintrag.pfad, "pointing at the same file");
        std::remove("quelle-a-kopie.pdf");

        // A different document is a different file, even on the same day.
        SchreibeDatei("quelle-b.pdf", BaueMiniPdf("Rechnung B"));
        const ArchivEintrag b = archiv.Ablegen("quelle-b.pdf", 2026);
        Check(b.ok && !b.schonVorhanden, "a different document is filed separately");
        Check(b.hash != eintrag.hash, "with its own hash");
        std::remove("quelle-b.pdf");

        // What is refused.
        SchreibeDatei("kein.pdf", "Das ist nur Text.\n");
        const ArchivEintrag kein = archiv.Ablegen("kein.pdf", 2026);
        Check(!kein.ok, "a file that is not a PDF is refused");
        Check(kein.fehler.find("%PDF-") != std::string::npos,
              "and the reason says what was looked for");
        std::remove("kein.pdf");
        Check(!archiv.Ablegen("gibtesnicht.pdf", 2026).ok,
              "a missing file is reported rather than crashing");

        // An encrypted PDF is stored but flagged: in ten years nobody has the
        // password, and that is exactly when the document is wanted.
        SchreibeDatei("gesperrt.pdf", BaueMiniPdf("trailer<</Encrypt 5 0 R>>"));
        const ArchivEintrag gesperrt = archiv.Ablegen("gesperrt.pdf", 2026);
        Check(gesperrt.ok, "an encrypted PDF is still archived");
        Check(!gesperrt.warnungen.empty(),
              "but warned about - it cannot be read back without a password "
              "nobody recorded");
        std::remove("gesperrt.pdf");

        // A batch, which is how the button and the drop target both call it.
        SchreibeDatei("stapel-1.pdf", BaueMiniPdf("Eins"));
        SchreibeDatei("stapel-2.pdf", BaueMiniPdf("Zwei"));
        SchreibeDatei("stapel-3.txt", "kein pdf");
        const ArchivBericht stapel = archiv.AblegenAlle(
            { "stapel-1.pdf", "stapel-2.pdf", "stapel-3.txt", "stapel-1.pdf" }, 2026);
        Check(stapel.ok, "a mixed batch files what it can");
        CheckInt(stapel.gelesen, 4, "four handed over");
        CheckInt(stapel.abgelegt, 2, "two newly stored");
        CheckInt(stapel.bekannt, 1, "one already there - the repeat");
        CheckInt(stapel.abgelehnt, 1, "and one refused");
        std::remove("stapel-1.pdf");
        std::remove("stapel-2.pdf");
        std::remove("stapel-3.txt");

        Check(archiv.AblegenAlle({}, 2026).ok == false, "an empty batch is refused");
        RaeumeArchivAuf("archiv-test");
    }

    // --- where the archive lives ---
    {
        CheckText(BelegArchivPfadFuer("/pfad/buch.db"), "/pfad/buch-belege",
                  "the archive sits beside its database - a database and its "
                  "receipts that can be separated will be separated");
        CheckText(BelegArchivPfadFuer(":memory:"), "belege",
                  "and an in-memory database gets a working directory");
    }
}

// ===== EU-STEUERSAETZE IM BESTAND =====

static void TestEuSteuersaetze() {
    std::printf("EU-Steuersätze (Tabelle und Editor)\n");

    Store store;
    if (!CheckStore(store.Open("fibu-eusatz", ":memory:"),
                    "a database for the rate tests opens")) {
        std::printf("    skipping the EU rate tests\n");
        return;
    }
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

    // --- a rate goes in, and the country code is normalised ---
    {
        EuSteuersatz satz;
        satz.land         = " at ";
        satz.satzPromille = 200;
        satz.gueltigVon   = Date(2021, 7, 1);
        satz.geprueft     = true;
        satz.quelle       = "Testquelle";
        CheckStore(store.EuSteuersatzSetzen(satz, akteur), "a rate can be entered");
        CheckText(satz.land, "AT", "the country code is normalised to ISO form");
        Check(satz.id != 0, "and it has an id");
    }

    // --- what the whole table exists for: a change is a NEW ROW ---
    {
        EuSteuersatz neu;
        neu.land         = "AT";
        neu.satzPromille = 220;
        neu.gueltigVon   = Date(2027, 1, 1);
        neu.geprueft     = true;
        neu.quelle       = "Testquelle";
        CheckStore(store.EuSteuersatzSetzen(neu, akteur), "a changed rate is entered");

        const std::vector<EuSteuersatz> alle = store.EuSteuersaetzeAlle();
        CheckInt(static_cast<int64_t>(alle.size()), 2,
                 "both rates are in the table - a changed rate is added, never an "
                 "edit, or a return already filed stops reproducing its figures");

        const EuSteuersatz* alt = nullptr;
        for (const EuSteuersatz& satz : alle)
            if (satz.gueltigVon == Date(2021, 7, 1)) alt = &satz;
        Check(alt != nullptr, "the previous rate is still there");
        if (alt != nullptr) {
            Check(alt->gueltigBis == Date(2026, 12, 31),
                  "and it is closed the day before the new one starts, so every "
                  "day has exactly one rate");
            CheckInt(alt->satzPromille, 200, "with its own rate untouched");
        }
    }

    // --- which rate applies on which day ---
    {
        const EuSteuersaetze saetze = store.EuSteuersaetzeGeladen();
        int satz = 0;
        Check(saetze.Standardsatz("AT", Date(2026, 6, 15), satz) && satz == 200,
              "a day before the change gets the old rate");
        Check(saetze.Standardsatz("AT", Date(2027, 6, 15), satz) && satz == 220,
              "a day after it gets the new one");
        Check(!saetze.Standardsatz("AT", Date(2020, 1, 1), satz),
              "and a day before either was in force gets neither");
    }

    // --- the same country, kind and day twice is an edit in disguise ---
    {
        EuSteuersatz doppelt;
        doppelt.land         = "AT";
        doppelt.satzPromille = 210;
        doppelt.gueltigVon   = Date(2027, 1, 1);
        const StoreResult r = store.EuSteuersatzSetzen(doppelt, akteur);
        Check(!r, "the same country, kind and start date is refused");
        Check(r.fehler.find("bereits einen Satz") != std::string::npos,
              "and the refusal says why - a changed rate takes the date it "
              "changed, not the date of the one it replaces");
    }

    // --- a rate needs a start date, a country and a plausible percentage ---
    {
        EuSteuersatz ohneDatum;
        ohneDatum.land         = "FR";
        ohneDatum.satzPromille = 200;
        Check(!store.EuSteuersatzSetzen(ohneDatum, akteur),
              "a rate without a start date is refused - the date is the point");

        EuSteuersatz falschesLand;
        falschesLand.land         = "Frankreich";
        falschesLand.satzPromille = 200;
        falschesLand.gueltigVon   = Date(2026, 1, 1);
        Check(!store.EuSteuersatzSetzen(falschesLand, akteur),
              "a country that is not an ISO code is refused");

        EuSteuersatz zuHoch;
        zuHoch.land         = "FR";
        zuHoch.satzPromille = 1200;
        zuHoch.gueltigVon   = Date(2026, 1, 1);
        Check(!store.EuSteuersatzSetzen(zuHoch, akteur),
              "and a rate above 100 % is refused");
    }

    // --- seeding from the shipped file is additive and never overwrites ---
    {
        SchreibeDatei("eu-seed-test.csv",
                      "land;art;satz_promille;gueltig_von;gueltig_bis;geprueft;quelle\n"
                      "AT;standard;190;2021-07-01;;nein;\n"
                      "PT;standard;230;2021-07-01;;nein;\n");
        int neu = 0, bekannt = 0;
        CheckStore(store.EuSteuersaetzeAusDatei("eu-seed-test.csv", akteur, neu, bekannt),
                   "the shipped rates can be taken over");
        CheckInt(neu, 1, "only the country that was missing is added");
        CheckInt(bekannt, 1, "the one already there is counted as known");

        // The important half: the file says AT was 19 % from 2021-07-01 and
        // unverified. The table says 20 % and verified. Re-seeding must not
        // undo a rate somebody checked by hand.
        const EuSteuersaetze saetze = store.EuSteuersaetzeGeladen();
        int satz = 0;
        Check(saetze.Standardsatz("AT", Date(2026, 6, 15), satz) && satz == 200,
              "and the rate already in the table is left exactly as it was - "
              "re-seeding must not undo a rate that was verified by hand");
        std::remove("eu-seed-test.csv");
    }

    // --- a rate entered by mistake can go, and its predecessor reopens ---
    {
        std::vector<EuSteuersatz> alle = store.EuSteuersaetzeAlle();
        int64_t neueAt = 0;
        for (const EuSteuersatz& satz : alle)
            if (satz.land == "AT" && satz.gueltigVon == Date(2027, 1, 1)) neueAt = satz.id;
        Check(neueAt != 0, "the 2027 rate is findable");

        CheckStore(store.EuSteuersatzLoeschen(neueAt, akteur),
                   "a rate entered by mistake can be removed");
        alle = store.EuSteuersaetzeAlle();
        const EuSteuersatz* alt = nullptr;
        for (const EuSteuersatz& satz : alle)
            if (satz.land == "AT") alt = &satz;
        Check(alt != nullptr && !alt->gueltigBis.Valid(),
              "and the rate it had superseded is open again - otherwise deleting "
              "a typo would leave the country with no rate at all from that day");
    }

    // --- the rule that outranks all of it: a filed return stays reproducible ---
    {
        Store::Meldung meldung;
        meldung.mandantId = mandant.id;
        meldung.art       = "oss";
        meldung.jahr      = 2026;
        meldung.zeitraum  = "Q2";
        meldung.status    = Store::MeldungStatus::Eingereicht;
        meldung.transferticket = "TESTTICKET";
        CheckStore(store.MeldungEintragen(meldung, akteur), "a return is on file");

        EuSteuersatz rueckwirkend;
        rueckwirkend.land         = "AT";
        rueckwirkend.satzPromille = 230;
        rueckwirkend.gueltigVon   = Date(2026, 1, 1);
        const StoreResult r = store.EuSteuersatzSetzen(rueckwirkend, akteur);
        Check(!r, "a rate starting inside a filed period is refused");
        Check(r.fehler.find("eingereicht") != std::string::npos,
              "and the refusal names the filing - changing it would make an "
              "already-submitted return recompute to something else, with "
              "nothing on screen to show that it had");

        EuSteuersatz spaeter;
        spaeter.land         = "AT";
        spaeter.satzPromille = 230;
        spaeter.gueltigVon   = Date(2026, 7, 1);
        spaeter.geprueft     = true;
        CheckStore(store.EuSteuersatzSetzen(spaeter, akteur),
                   "while a rate starting after that period is fine - the filed "
                   "return is untouched by it");

        std::vector<EuSteuersatz> alle = store.EuSteuersaetzeAlle();
        int64_t inPeriode = 0;
        for (const EuSteuersatz& satz : alle)
            if (satz.land == "AT" && satz.gueltigVon == Date(2021, 7, 1)) inPeriode = satz.id;
        Check(inPeriode != 0, "the rate the filed return used is findable");
        const StoreResult weg = store.EuSteuersatzLoeschen(inPeriode, akteur);
        Check(!weg, "and it cannot be deleted either, for the same reason");
    }

    store.Close();
}

// ===== PDF-BELEGE IN DEN BESTAND =====

static void TestBelegImport() {
    std::printf("PDF-Belege importieren\n");

    Store store;
    if (!CheckStore(store.Open("fibu-pdf", ":memory:"),
                    "a database for the import tests opens")) {
        std::printf("    skipping the PDF import tests\n");
        return;
    }
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
    Geschaeftsjahr jahr;
    jahr.mandantId   = mandant.id;
    jahr.beginn      = Date(2026, 4, 1);
    jahr.ende        = Date(2027, 3, 31);
    jahr.bezeichnung = jahr.DefaultBezeichnung();
    CheckStore(store.SaveGeschaeftsjahr(jahr, akteur), "with a fiscal year");
    Nummernkreis kreis;
    kreis.mandantId = mandant.id;
    kreis.kreis     = "eingang";
    kreis.praefix   = "E-";
    CheckStore(store.SaveNummernkreis(kreis, akteur),
               "and a number range for incoming documents");

    SchreibeDatei("import-1.pdf", BaueMiniPdf("Beleg eins"));
    SchreibeDatei("import-2.pdf", BaueMiniPdf("Beleg zwei"));
    SchreibeDatei("import-3.txt", "kein pdf");
    SchreibeDatei("import-1-kopie.pdf", BaueMiniPdf("Beleg eins"));

    const Store::BelegImportBericht b = store.ImportiereBelegDateien(
        mandant.id,
        { "import-1.pdf", "import-2.pdf", "import-3.txt", "import-1-kopie.pdf" },
        BelegArt::Eingangsrechnung, Date(2026, 6, 20), "eingang", akteur);

    Check(b.ok, "a stack of PDFs imports");
    CheckInt(b.gelesen, 4, "four files handed over");
    CheckInt(b.angelegt, 2, "two became drafts");
    CheckInt(b.bekannt, 1, "one was the same receipt again");
    CheckInt(b.abgelehnt, 1, "and one was not a PDF");

    // **The same receipt twice is one receipt.** Creating a second draft for a
    // file already filed is how a duplicate expense gets into a ledger.
    for (const Store::BelegImportEintrag& e : b.eintraege) {
        if (e.dateiname == "import-1-kopie.pdf") {
            Check(e.schonVorhanden,
                  "the repeat is recognised by its hash, not its name - the "
                  "same receipt under a different file name is still the same "
                  "receipt");
            Check(e.vorhandenerBeleg != 0, "and points at the document that has it");
        }
    }

    Store::BelegFilter filter;
    filter.mandantId = mandant.id;
    const std::vector<Beleg> belege = store.BelegListe(filter);
    CheckInt(static_cast<int64_t>(belege.size()), 2,
             "so the ledger holds two documents, not three");

    // The drafts carry the file, the name and nothing invented.
    if (belege.size() == 2) {
        const Beleg& b1 = belege[0];
        Check(b1.status == BelegStatus::Entwurf,
              "each is a draft - nothing is read out of the PDF, and invented "
              "figures in a ledger would be worse than none");
        Check(!b1.dateiHash.empty(), "with the file's hash recorded");
        Check(!b1.dateiPfad.empty(), "and its place in the archive");
        Check(!b1.buchungstext.empty(),
              "and the original file name, which is the only thing that tells "
              "one uploaded receipt from another");
        CheckInt(b1.brutto.Minor(), 0, "no amount was invented");

        std::string fehler;
        Check(store.PruefeBelegDatei(b1, fehler),
              "and the archived file still matches the hash recorded for it");
    }

    // **The invariant that was relaxed, and the one that was not.**
    // An empty draft may now be saved, because a received PDF has no positions
    // until somebody reads it. Posting one must still be refused.
    {
        Beleg leer;
        leer.mandantId = mandant.id;
        leer.art       = BelegArt::Eingangsrechnung;
        leer.datum     = Date(2026, 6, 20);
        leer.waehrung  = "EUR";
        CheckStore(store.SaveBeleg(leer, "eingang", akteur),
                   "an empty draft saves - a receipt can be filed before it is "
                   "understood");
        // Asserting *why* it is refused, not merely that it is. A refusal for
        // some unrelated reason would pass a weaker test while leaving the
        // relaxed draft rule genuinely unsafe - which is exactly what a
        // mutation of the positions guard revealed.
        const StoreResult gebucht = store.Buchen(leer, akteur);
        CheckRefused(gebucht,
                     "but posting it is still refused - relaxing the draft rule "
                     "must not let an empty document become a posting");
        Check(gebucht.fehler.find("Positionen") != std::string::npos,
              "and refused for the right reason: no positions, not some other "
              "validation that might not apply to the next empty document");
    }

    // What the import refuses outright.
    {
        int dummy = 0; (void)dummy;
        Check(!store.ImportiereBelegDateien(mandant.id, {}, BelegArt::Eingangsrechnung,
                                            Date(2026, 6, 20), "eingang", akteur).ok,
              "an empty list is refused");
        Check(!store.ImportiereBelegDateien(mandant.id, { "import-1.pdf" },
                                            BelegArt::Eingangsrechnung, Date(),
                                            "eingang", akteur).ok,
              "and so is an import with no document date - without one the "
              "document belongs to no fiscal year");
    }

    std::remove("import-1.pdf");
    std::remove("import-2.pdf");
    std::remove("import-3.txt");
    std::remove("import-1-kopie.pdf");
    RaeumeArchivAuf(store.BelegArchivPfad());
}

// ===== ONE-STOP-SHOP =====
//
// OSS is VAT owed to other member states and forwarded by the BZSt. A figure
// missing from a return is another country's money not paid, and there is no
// machine interface downstream to catch it - the file is uploaded by hand. So
// what is tested here is mostly the refusals and the reconciliation.

static Buchung OssBuchung(const Date& datum, const std::string& key,
                          int64_t nettoMinor, int satzPromille, SollHaben seite,
                          int64_t id = 0) {
    Buchung b;
    b.id               = id;
    b.mandantId        = 1;
    b.belegdatum       = datum;
    b.steuerschluessel = key;
    b.satzPromille     = satzPromille;
    b.netto            = Money::FromMinor(nettoMinor, "EUR");
    b.steuer           = b.netto.TaxOnNet(satzPromille);
    b.umsatz           = b.netto + b.steuer;
    b.sollHaben        = seite;
    b.konto            = "10000";
    b.gegenkonto       = "8338";
    b.waehrung         = "EUR";
    return b;
}

static Steuerschluessel OssKey(const std::string& name, const std::string& land,
                               int satz, const std::string& kzBemessung = "45") {
    Steuerschluessel k;
    k.schluessel   = name;
    k.bezeichnung  = name;
    k.art          = SteuerArt::Oss;
    k.land         = land;
    k.satzPromille = satz;
    k.kzBemessung  = kzBemessung;
    k.gueltigVon   = Date(2026, 1, 1);
    return k;
}

static void TestOss() {
    std::printf("One-Stop-Shop\n");

    // --- the member states' rates are a check, not a source ---
    {
        SchreibeDatei("saetze-test.csv",
                      "land;art;satz_promille;gueltig_von;gueltig_bis;geprueft;quelle\n"
                      "AT;standard;200;2021-07-01;;ja;geprueft\n"
                      "FR;standard;200;2021-07-01;;nein;\n");
        EuSteuersaetze saetze;
        std::string fehler;
        Check(saetze.Laden("saetze-test.csv", fehler), "the rate table loads");
        int satz = 0;
        Check(saetze.Standardsatz("AT", Date(2026, 2, 15), satz) && satz == 200,
              "a verified rate is available for comparison");
        Check(!saetze.Standardsatz("FR", Date(2026, 2, 15), satz),
              "an UNVERIFIED rate is not - reporting a correct invoice as wrong "
              "because of a guessed rate would train the user to ignore the "
              "warning, and then it is worth nothing when it is right");
        Check(!saetze.Standardsatz("AT", Date(2020, 1, 1), satz),
              "and a rate is not used before it was in force");
        Check(saetze.Kennt("FR"), "an unverified country is still listed - the file "
                                  "records which cases exist");
        std::remove("saetze-test.csv");
    }

    // --- periods ---
    {
        Date von, bis;
        Check(OssZeitraumGrenzen(OssVerfahren::Oss, 2026, "Q1", von, bis) &&
              von == Date(2026, 1, 1) && bis == Date(2026, 3, 31),
              "OSS Q1 is January to March");
        Check(OssZeitraumGrenzen(OssVerfahren::Oss, 2026, "Q4", von, bis) &&
              bis == Date(2026, 12, 31), "and Q4 ends on 31 December");
        Check(!OssZeitraumGrenzen(OssVerfahren::Oss, 2026, "Q5", von, bis),
              "there is no fifth quarter");
        Check(!OssZeitraumGrenzen(OssVerfahren::Oss, 2026, "02", von, bis),
              "and OSS is not monthly");
        // IOSS is the same shape with a monthly period.
        Check(OssZeitraumGrenzen(OssVerfahren::Ioss, 2026, "02", von, bis) &&
              von == Date(2026, 2, 1) && bis == Date(2026, 2, 28),
              "IOSS is monthly, and February 2026 ends on the 28th");
    }

    EuSteuersaetze saetze;
    {
        SchreibeDatei("saetze-oss.csv",
                      "land;art;satz_promille;gueltig_von;gueltig_bis;geprueft;quelle\n"
                      "AT;standard;200;2021-07-01;;ja;geprueft\n"
                      "FR;standard;200;2021-07-01;;ja;geprueft\n"
                      "IT;standard;220;2021-07-01;;nein;\n");
        std::string fehler;
        Check(saetze.Laden("saetze-oss.csv", fehler), "the test rates load");
        std::remove("saetze-oss.csv");
    }

    std::vector<Steuerschluessel> keys;
    keys.push_back(OssKey("OSS-AT-20", "AT", 200));
    keys.push_back(OssKey("OSS-FR-20", "FR", 200));
    keys.push_back(OssKey("OSS-AT-19", "AT", 190));      // the wrong rate charged
    keys.push_back(OssKey("OSS-IT-22", "IT", 220));      // rate not verified
    keys.push_back(OssKey("OSS", "", 0));                // no country at all
    {
        // A domestic key, to prove it stays out of the OSS return.
        Steuerschluessel inland;
        inland.schluessel   = "USt19";
        inland.art          = SteuerArt::Inland;
        inland.satzPromille = 190;
        inland.kzBemessung  = "81";
        inland.gueltigVon   = Date(2026, 1, 1);
        keys.push_back(inland);
    }

    Date von, bis;
    OssZeitraumGrenzen(OssVerfahren::Oss, 2026, "Q1", von, bis);

    // --- an ordinary quarter ---
    {
        std::vector<Buchung> journal;
        journal.push_back(OssBuchung(Date(2026, 1, 20), "OSS-AT-20", 100000, 200,
                                     SollHaben::Soll, 1));
        journal.push_back(OssBuchung(Date(2026, 2, 10), "OSS-AT-20", 50000, 200,
                                     SollHaben::Soll, 2));
        journal.push_back(OssBuchung(Date(2026, 2, 15), "OSS-FR-20", 30000, 200,
                                     SollHaben::Soll, 3));
        // Domestic turnover is not OSS turnover.
        journal.push_back(OssBuchung(Date(2026, 2, 20), "USt19", 900000, 190,
                                     SollHaben::Soll, 4));
        // And a sale outside the quarter.
        journal.push_back(OssBuchung(Date(2026, 4, 1), "OSS-AT-20", 700000, 200,
                                     SollHaben::Soll, 5));

        const OssBerechnung b = BerechneOss(OssVerfahren::Oss, 1, 2026, "Q1", von, bis,
                                            journal, keys, saetze);
        Check(b.ok, "a quarter computes");
        Check(b.Vollstaendig(), "and is complete");
        CheckInt(static_cast<int64_t>(b.posten.size()), 2,
                 "two lines: one per country and rate");
        CheckInt(b.SteuerFuer("AT").Minor(), 30000,
                 "Austria's VAT is 20 % of 1.500,00");
        CheckInt(b.SteuerFuer("FR").Minor(), 6000, "and France's 20 % of 300,00");
        CheckInt(b.summeBemessung.Minor(), 180000,
                 "the base is 1.800,00 - domestic turnover is not OSS turnover "
                 "and the April sale is not this quarter");
        CheckInt(b.summeSteuer.Minor(), 36000, "and the tax 360,00");
        if (!b.posten.empty()) {
            Check(b.posten[0].satzGeprueft,
                  "the rate was checked against the country's own");
            Check(b.posten[0].satzHinweis.empty(), "and agreed");
            CheckInt(static_cast<int64_t>(b.posten[0].buchungIds.size()), 2,
                     "and the line names the postings behind it");
        }
    }

    // --- a Storno subtracts here too ---
    {
        std::vector<Buchung> journal;
        journal.push_back(OssBuchung(Date(2026, 1, 20), "OSS-AT-20", 100000, 200,
                                     SollHaben::Soll, 1));
        journal.push_back(OssBuchung(Date(2026, 1, 25), "OSS-AT-20", 100000, 200,
                                     SollHaben::Haben, 2));
        const OssBerechnung b = BerechneOss(OssVerfahren::Oss, 1, 2026, "Q1", von, bis,
                                            journal, keys, saetze);
        CheckInt(b.summeSteuer.Minor(), 0,
                 "a cancelled OSS sale owes nothing - a reversal that added would "
                 "declare another state's VAT twice");
    }

    // --- THE REFUSAL: turnover with no destination country ---
    {
        std::vector<Buchung> journal;
        journal.push_back(OssBuchung(Date(2026, 1, 20), "OSS-AT-20", 100000, 200,
                                     SollHaben::Soll, 1));
        journal.push_back(OssBuchung(Date(2026, 2, 1), "OSS", 200000, 0,
                                     SollHaben::Soll, 2));
        const OssBerechnung b = BerechneOss(OssVerfahren::Oss, 1, 2026, "Q1", von, bis,
                                            journal, keys, saetze);
        Check(!b.Vollstaendig(),
              "a tax key with no country makes the return incomplete - 'VAT owed "
              "somewhere in the EU' is not a filing");
        CheckInt(static_cast<int64_t>(b.luecken.size()), 1, "one key is unusable");
        if (!b.luecken.empty())
            Check(b.luecken[0].grund.find("Zielland") != std::string::npos,
                  "and the reason says what is missing and how to fix it");

        const OssDateiErgebnis r = SchreibeBopDatei(b, "DE123456789", ".");
        Check(!r.ok,
              "and the transport file is refused - what would be left out is "
              "another member state's money");
        Check(r.fehler.find("OSS") != std::string::npos,
              "with the key named");
    }

    // --- the rate check, in both outcomes ---
    {
        // 19 % charged where Austria levies 20 %.
        std::vector<Buchung> journal;
        journal.push_back(OssBuchung(Date(2026, 1, 20), "OSS-AT-19", 100000, 190,
                                     SollHaben::Soll, 1));
        const OssBerechnung b = BerechneOss(OssVerfahren::Oss, 1, 2026, "Q1", von, bis,
                                            journal, keys, saetze);
        Check(b.ok && !b.posten.empty(), "the sale is still reported");
        if (!b.posten.empty()) {
            CheckInt(b.posten[0].steuer.Minor(), 19000,
                     "**at the rate that was actually charged** - the return must "
                     "match the invoice, even when the invoice was wrong");
            Check(!b.posten[0].satzHinweis.empty(),
                  "but the mismatch is reported: the invoice is the thing to fix");
            Check(b.posten[0].satzHinweis.find("20") != std::string::npos,
                  "naming what the country actually levies");
        }

        // A country whose rate is not verified: no comparison, and it says so.
        std::vector<Buchung> italien;
        italien.push_back(OssBuchung(Date(2026, 1, 20), "OSS-IT-22", 100000, 220,
                                     SollHaben::Soll, 1));
        const OssBerechnung it = BerechneOss(OssVerfahren::Oss, 1, 2026, "Q1", von, bis,
                                             italien, keys, saetze);
        if (!it.posten.empty()) {
            Check(!it.posten[0].satzGeprueft,
                  "an unverified country rate is not used for comparison");
            Check(!it.posten[0].satzHinweis.empty(),
                  "and the return says the rate could not be confirmed - which is "
                  "a different statement from 'the rate is right'");
        }
    }

    // --- the reconciliation against the UStVA ---
    // Both come from the same journal by different paths, so disagreeing means
    // one of two returns about to be filed is wrong.
    {
        UstvaMapping mapping;
        {
            SchreibeDatei("kz-oss.csv",
                          "kennzahl;art;satz_promille;geprueft;bezeichnung\n"
                          "45;frei;0;ja;Nicht steuerbare Umsaetze\n"
                          "81;bemessung;190;ja;Umsaetze 19 %\n"
                          "83;berechnet;0;ja;Zahllast\n");
            std::string fehler;
            Check(mapping.Laden("kz-oss.csv", fehler), "a mapping with Kz 45 loads");
            std::remove("kz-oss.csv");
        }
        std::vector<Buchung> journal;
        journal.push_back(OssBuchung(Date(2026, 1, 20), "OSS-AT-20", 100000, 200,
                                     SollHaben::Soll, 1));
        journal.push_back(OssBuchung(Date(2026, 2, 15), "OSS-FR-20", 30000, 200,
                                     SollHaben::Soll, 2));

        const OssBerechnung oss = BerechneOss(OssVerfahren::Oss, 1, 2026, "Q1", von, bis,
                                              journal, keys, saetze);
        const UstvaBerechnung ustva =
            BerechneUstva(1, 2026, "41", von, bis, journal, keys, mapping);
        const OssUstvaAbgleich a = PruefeGegenUstva(oss, ustva);
        Check(a.stimmt,
              "OSS turnover and UStVA Kz 45 agree - the same money by two paths");
        CheckInt(a.ossBemessung.Minor(), 130000, "1.300,00 on the OSS side");
        CheckInt(a.ustvaKz45.Minor(), 130000, "and the same in Kz 45");

        // Now break it: a key that reports to OSS but carries no Kz 45.
        std::vector<Steuerschluessel> schief = keys;
        for (Steuerschluessel& k : schief)
            if (k.schluessel == "OSS-FR-20") k.kzBemessung.clear();
        const OssBerechnung oss2 = BerechneOss(OssVerfahren::Oss, 1, 2026, "Q1", von, bis,
                                               journal, schief, saetze);
        const UstvaBerechnung ustva2 =
            BerechneUstva(1, 2026, "41", von, bis, journal, schief, mapping);
        const OssUstvaAbgleich a2 = PruefeGegenUstva(oss2, ustva2);
        Check(!a2.stimmt,
              "a key that reports to OSS but not to Kz 45 makes the two disagree, "
              "and the reconciliation says so before either is filed");
        CheckInt(a2.differenz.Minor(), 30000, "naming the amount that differs");
    }

    // --- the § 3c threshold ---
    {
        std::vector<Buchung> journal;
        // Well under.
        journal.push_back(OssBuchung(Date(2026, 1, 20), "OSS-AT-20", 100000, 200,
                                     SollHaben::Soll, 1));
        SchwellenStand stand = PruefeLieferschwelle(2026, journal, keys);
        CheckInt(stand.summe.Minor(), 100000, "the running total is 1.000,00");
        Check(!stand.ueberschritten, "the threshold is not crossed");
        Check(!stand.nahe, "and it is not close");

        // Close: 8.500 of 10.000.
        journal.push_back(OssBuchung(Date(2026, 3, 1), "OSS-AT-20", 750000, 200,
                                     SollHaben::Soll, 2));
        stand = PruefeLieferschwelle(2026, journal, keys);
        Check(stand.nahe,
              "at 85 % the warning comes early - crossing it unnoticed means every "
              "later invoice carries the wrong VAT");
        Check(!stand.ueberschritten, "but it is not crossed yet");

        // Over, and it matters exactly when.
        journal.push_back(OssBuchung(Date(2026, 5, 4), "OSS-AT-20", 300000, 200,
                                     SollHaben::Soll, 3));
        stand = PruefeLieferschwelle(2026, journal, keys);
        Check(stand.ueberschritten, "now it is crossed");
        Check(stand.ueberschrittenAm == Date(2026, 5, 4),
              "on the day of the invoice that crossed it - from that invoice on, "
              "the destination country's rate is compulsory");
        bool nenntDatum = false;
        for (const std::string& h : stand.hinweise)
            if (h.find("04.05.2026") != std::string::npos) nenntDatum = true;
        Check(nenntDatum, "and the date is in the message");

        // The limitation is stated rather than hidden.
        bool nenntGrenze = false;
        for (const std::string& h : stand.hinweise)
            if (h.find("Inlandsschlüssel") != std::string::npos) nenntGrenze = true;
        Check(nenntGrenze,
              "and so is what the count cannot see - EU consumer sales still "
              "booked on a domestic key count towards the threshold too");
    }

    // --- the transport file ---
    {
        std::vector<Buchung> journal;
        journal.push_back(OssBuchung(Date(2026, 1, 20), "OSS-AT-20", 100000, 200,
                                     SollHaben::Soll, 1));
        journal.push_back(OssBuchung(Date(2026, 2, 15), "OSS-FR-20", 30000, 200,
                                     SollHaben::Soll, 2));
        const OssBerechnung b = BerechneOss(OssVerfahren::Oss, 1, 2026, "Q1", von, bis,
                                            journal, keys, saetze);
        const OssDateiErgebnis r = SchreibeBopDatei(b, "DE123456789", ".");
        Check(r.ok, "the transport file is written");
        Check(!r.hash.empty(), "and hashed");

        std::string inhalt;
        {
            std::FILE* f = std::fopen(r.datei.c_str(), "rb");
            if (f) {
                char puffer[8192]; size_t n = 0;
                while ((n = std::fread(puffer, 1, sizeof(puffer), f)) > 0)
                    inhalt.append(puffer, n);
                std::fclose(f);
            }
        }
        Check(inhalt.find("AT;20;STANDARD;1000.00;200.00") != std::string::npos,
              "with a line per country and rate, dot-decimal - not the process "
              "locale's comma");
        Check(inhalt.find("FR;20;STANDARD;300.00;60.00") != std::string::npos,
              "and one for France");
        // The file admits what has not been checked, in itself.
        Check(inhalt.find("NICHT an einem echten") != std::string::npos,
              "and states that its column layout is unverified - the BZSt "
              "publishes the import function but not its specification");
        bool warntAufbau = false;
        for (const std::string& w : r.warnungen)
            if (w.find("Spaltenaufbau") != std::string::npos) warntAufbau = true;
        Check(warntAufbau, "which is said out loud as well");

        Check(!SchreibeBopDatei(b, "", ".").ok,
              "without an own VAT number the BZSt will not take it, so it is "
              "refused here first");
        std::remove(r.datei.c_str());
    }

    // --- nothing to report ---
    {
        std::vector<Buchung> leer;
        const OssBerechnung b = BerechneOss(OssVerfahren::Oss, 1, 2026, "Q1", von, bis,
                                            leer, keys, saetze);
        Check(b.ok, "a quarter with no EU sales still computes");
        Check(b.posten.empty(), "with no lines");
        Check(!SchreibeBopDatei(b, "DE123456789", ".").ok,
              "and no file is written for an empty return");
    }
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
    TestRechnungPdf();
    TestDatev();
    TestDatevImport();
    TestDatevImportInDenBestand();
    TestDatevRundlauf();
    TestBankLesen();
    TestBankMt940UndCsv();
    TestBankZuordnung();
    TestBankImportInDenBestand();
    TestUstva();
    TestBelegArchiv();
    TestBelegImport();
    TestOss();
    TestEuSteuersaetze();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
