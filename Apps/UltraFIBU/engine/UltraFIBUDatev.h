// Apps/UltraFIBU/engine/UltraFIBUDatev.h
// The DATEV interface: writing an EXTF Buchungsstapel a Kanzlei can import,
// and checking our column definition against a real DATEV file.
//
// **The column order is data, not code** (`data/DATEV-Buchungsstapel-v700.csv`).
// DATEV's format description fixes ~120 columns in a fixed order; the shipped
// definition is the best reconstruction available without the original, and it
// says so at the top of the file. The exporter writes every value **by column
// name**, so correcting the definition moves the values with it and needs no
// rebuild. `PruefeDateiGegenDefinition` reads a real EXTF file and reports
// every difference with its position, which turns "we think this is right"
// into one command the moment a real file exists.
//
// Three rules from the format that are designed for here rather than
// discovered later (proposal §4.1):
//
//  1. **Belegdatum is TTMM - four digits, no year.** The year comes from the
//     stack's Wirtschaftsjahr. So a stack may never span a calendar year, and
//     this writer goes further: **one file per calendar month**, which is what
//     an accountant expects anyway and makes the year unambiguous. For a
//     1 April fiscal year that is the difference between a correct export and
//     one that silently books December into January.
//  2. **Umsatz is unsigned**; the direction is the Soll/Haben-Kennzeichen. A
//     signed amount produces a plausible-looking, wrong ledger.
//  3. **The file is CP1252**, not UTF-8, and semicolon-separated with CRLF
//     line endings. An umlaut written as UTF-8 arrives as two wrong characters
//     in a Kanzlei's system.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraFIBUBuchung.h"
#include "UltraFIBUTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraFIBU {

// The DATEV format categories. Only the two that are shipped are listed:
// category 16 (Debitoren/Kreditoren) has roughly 240 columns and guessing that
// order would be worse than not offering it, so it waits for a real file.
enum class DatevKategorie {
    Buchungsstapel            = 21,
    Sachkontenbeschriftungen  = 20,
};

// One column of a DATEV file, as the definition file describes it.
struct DatevSpalte {
    std::string name;
    // text -> quoted; zahl/betrag/datum -> bare. The distinction is the
    // format's, not ours: a quoted number is rejected by the import.
    std::string typ = "text";

    bool IstText() const { return typ == "text"; }
};

// The column order for one category, read from `data/DATEV-*.csv`.
class DatevDefinition {
public:
    bool Laden(const std::string& dateipfad, std::string& fehler);

    const std::vector<DatevSpalte>& Spalten() const { return spalten_; }
    size_t Anzahl() const { return spalten_.size(); }
    // The position of a named column, or -1. Every value the exporter writes
    // goes through here, so a corrected definition moves the values with it.
    int Index(const std::string& name) const;

private:
    std::vector<DatevSpalte> spalten_;
};

// The two-line preamble. Beraternummer and Mandantennummer come from the
// Kanzlei and an import is refused without them, so the writer refuses first
// and says which is missing rather than producing a file that fails there.
struct DatevKopf {
    std::string    kennzeichen     = "EXTF";
    int            versionsnummer  = 700;
    DatevKategorie kategorie       = DatevKategorie::Buchungsstapel;
    std::string    formatname      = "Buchungsstapel";
    int            formatversion   = 13;
    int64_t        erzeugtAm       = 0;      // epoch seconds; 0 = now
    std::string    exportiertVon;            // the user who pressed the button
    std::string    beraternummer;
    std::string    mandantennummer;
    Date           wjBeginn;                 // the flexible fiscal year lands here
    int            sachkontenlaenge = 4;
    Date           von;
    Date           bis;
    std::string    bezeichnung;              // the stack's name inside DATEV
    std::string    waehrung         = "EUR";
    // 0 = the stack arrives unfestgeschrieben and the Kanzlei can still
    // correct it; 1 = festgeschrieben. Sent as 1 only when every posting in
    // the file actually is.
    int            festschreibung   = 0;
};

struct DatevErgebnis {
    bool                     ok = false;
    std::string              fehler;
    // Things that do not stop the export but change what the Kanzlei gets -
    // above all a missing BU-Schlüssel, which makes DATEV book without the
    // tax automatics.
    std::vector<std::string> warnungen;
    std::string              datei;
    int                      zeilen = 0;

    bool Sauber() const { return ok && warnungen.empty(); }
};

// Write one calendar month as a Buchungsstapel. `buchungen` may contain
// anything; only the rows whose Belegdatum falls in the month are written, so
// the caller can hand over the whole journal.
DatevErgebnis SchreibeBuchungsstapel(const Mandant& mandant,
                                     const Geschaeftsjahr& jahr,
                                     const std::vector<Buchung>& buchungen,
                                     const DatevDefinition& definition,
                                     int kalenderJahr, int monat,
                                     const std::string& zielVerzeichnis,
                                     const std::string& exportiertVon);

// Write the chart of accounts as Kontenbeschriftungen, so a Kanzlei sees the
// account names rather than bare numbers.
DatevErgebnis SchreibeKontenbeschriftungen(const Mandant& mandant,
                                           const Geschaeftsjahr& jahr,
                                           const std::vector<Konto>& konten,
                                           const DatevDefinition& definition,
                                           const std::string& zielVerzeichnis,
                                           const std::string& exportiertVon);

// What a comparison against a real DATEV file found.
struct DatevPruefung {
    bool        ok = false;
    std::string fehler;                 // the file could not be read at all
    std::string kennzeichen;            // what the file's own header says
    int         versionsnummer = 0;
    int         kategorie = 0;
    int         formatversion = 0;
    size_t      spaltenInDatei = 0;
    size_t      spaltenInDefinition = 0;
    // One sentence per difference, in German, each naming the position.
    std::vector<std::string> abweichungen;
};

// Read a real EXTF file's header and column line and compare them with a
// definition. This is how the shipped column order stops being a guess.
DatevPruefung PruefeDateiGegenDefinition(const std::string& dateipfad,
                                         const DatevDefinition& definition);

// UTF-8 to CP1252, which is the encoding DATEV files are written in. Exposed
// because it is the one transformation that silently corrupts a whole file if
// it is skipped, so the tests check it directly. Characters outside CP1252
// become '?' and set `verlust`.
std::string NachCp1252(const std::string& utf8, bool& verlust);

// Where the shipped definition files live, honouring ULTRAFIBU_DATA_DIR the
// same way the chart of accounts does.
std::string DatevDefinitionPfad(const std::string& dateiname);

} // namespace UltraFIBU
