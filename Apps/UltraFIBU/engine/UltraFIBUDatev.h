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


// ===== IMPORT =====
//
// **The importer does not depend on the guessed column order.** A real DATEV
// file carries its own column line, and this reads the values by the names
// that file gives - so the uncertainty that hangs over the export
// (`data/DATEV-Buchungsstapel-v700.csv`) does not apply here at all. What the
// importer needs is only that the columns it looks for exist under the names
// the format uses; anything else in the file is carried past untouched.

// One posting read out of a file, with where it came from so a problem can be
// pointed at a line rather than described in the abstract.
struct DatevImportZeile {
    int         zeileNr = 0;      // 1-based line number in the file
    Buchung     buchung;
    std::string buSchluessel;     // verbatim, even when it cannot be mapped
    std::string hinweis;          // why this row is doubtful, if it is
};

struct DatevImportBericht {
    bool        ok = false;
    std::string fehler;

    // What the file says about itself.
    std::string kennzeichen;
    int         versionsnummer = 0;
    int         kategorie      = 0;
    int         formatversion  = 0;
    std::string beraternummer;
    std::string mandantennummer;
    Date        wjBeginn;
    Date        von;
    Date        bis;
    int         sachkontenlaenge = 0;
    std::string bezeichnung;
    bool        festgeschrieben = false;

    int gelesen       = 0;    // data lines seen
    int uebernommen   = 0;    // rows that became a posting
    int uebersprungen = 0;    // rows that could not be read
    // How many of the imported postings carry a tax split. The difference
    // between this and `uebernommen` is not an error - a payment has no tax -
    // but it is the number that explains why a revenue account shows its gross
    // amount after an import, so it is reported rather than left to be noticed.
    int mitSteuer     = 0;

    // Anything that changes what the ledger will contain, in German.
    std::vector<std::string> warnungen;
    // One sentence per unreadable line, each naming its line number.
    std::vector<std::string> fehlerZeilen;

    // SHA-256 of the file, so the same stack cannot be imported twice by
    // accident - which would silently double a month.
    std::string dateiHash;

    std::vector<DatevImportZeile> zeilen;
};

// Read a Buchungsstapel without writing anything. Every problem is collected
// rather than thrown, because the first useful thing to do with a Kanzlei's
// file is look at what it contains.
//
// `steuerschluessel` is used only to translate a DATEV BU-Schlüssel back into
// our own key, through the `datevBu` column. Where that mapping is missing the
// posting is still imported - with its BU key preserved verbatim and the whole
// amount unsplit - and a warning says so: inventing a tax split from a key we
// cannot read would be worse than not splitting it.
DatevImportBericht LeseBuchungsstapel(const std::string& dateipfad,
                                      const Mandant& mandant,
                                      const Geschaeftsjahr& jahr,
                                      const std::vector<Steuerschluessel>& steuerschluessel);

// The Sachkonten a read stack posts to that the chart of accounts does not
// have. Personenkonten are deliberately left out: a Debitor or Kreditor belongs
// to a partner and is never in the chart, so listing those would bury the one
// case that matters - a Sachkonto that is in the file and nowhere else, which
// after the import shows up in the Saldenliste as a bare number with no name
// and is usually a wrong account rather than a missing one. The file's own
// Sachkontenlaenge decides which is which, because it is the file's convention
// that produced the numbers.
std::vector<std::string> UnbekannteSachkonten(const DatevImportBericht& bericht,
                                              const std::vector<Konto>& konten);

// UTF-8 to CP1252, which is the encoding DATEV files are written in. Exposed
// because it is the one transformation that silently corrupts a whole file if
// it is skipped, so the tests check it directly. Characters outside CP1252
// become '?' and set `verlust`.
std::string NachCp1252(const std::string& utf8, bool& verlust);

// Where the shipped definition files live, honouring ULTRAFIBU_DATA_DIR the
// same way the chart of accounts does.
std::string DatevDefinitionPfad(const std::string& dateiname);

} // namespace UltraFIBU
