// Apps/UltraFIBU/engine/UltraFIBUKontenrahmen.cpp
// The data-file readers. Columns are looked up by header name; unknown values
// become warnings rather than silent zeros, because a tax key that quietly
// loses its rate is a wrong invoice.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUKontenrahmen.h"

#include "UltraCanvasCSVImport.h"

#include <cstdlib>
#include <map>

namespace UltraFIBU {

namespace {

using UltraCanvas::CSVGrid;
using UltraCanvas::CSVImportOptions;
using UltraCanvas::CSVParse;
using UltraCanvas::CSVReadFileRaw;
using UltraCanvas::CSVRow;

CSVImportOptions DataFileOptions() {
    CSVImportOptions opt;
    opt.encoding = CSVImportOptions::Encoding::UTF8;
    opt.SetSingleSeparator(';');
    opt.textDelimiter = '"';
    // These are structured data files, not spreadsheets: every field is text
    // and is converted by this code, never by the parser's number recognition.
    opt.detectNumbers = false;
    return opt;
}

bool FileExists(const std::string& pfad) {
    if (pfad.empty()) return false;
    std::string ignored;
    std::string error;
    // CSVReadFileRaw is the framework's own "can I read this" answer, so the
    // check and the read agree about what counts as readable.
    return CSVReadFileRaw(pfad, ignored, &error);
}

std::string JoinPath(const std::string& directory, const std::string& name) {
    if (directory.empty()) return name;
    const char last = directory[directory.size() - 1];
    if (last == '/' || last == '\\') return directory + name;
    return directory + "/" + name;
}

int ToInt(const std::string& text, bool& ok) {
    ok = false;
    if (text.empty()) return 0;
    size_t i = 0;
    int sign = 1;
    if (text[0] == '-') { sign = -1; i = 1; }
    int value = 0;
    for (; i < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9') return 0;
        value = value * 10 + (text[i] - '0');
    }
    ok = true;
    return sign * value;
}

std::string Trim(const std::string& text) {
    size_t from = 0, to = text.size();
    while (from < to && (text[from] == ' ' || text[from] == '\t' || text[from] == '\r')) ++from;
    while (to > from && (text[to - 1] == ' ' || text[to - 1] == '\t' || text[to - 1] == '\r')) --to;
    return text.substr(from, to - from);
}

// A row addressed by header name. Missing columns read as empty, which is what
// "not set" means in these files.
class NamedRow {
public:
    NamedRow(const std::map<std::string, size_t>& columns, const CSVRow& row)
        : columns_(columns), row_(row) {}

    std::string operator[](const std::string& name) const {
        const auto found = columns_.find(name);
        if (found == columns_.end() || found->second >= row_.size()) return std::string();
        return Trim(row_[found->second].text);
    }
    bool Has(const std::string& name) const { return columns_.count(name) > 0; }

private:
    const std::map<std::string, size_t>& columns_;
    const CSVRow& row_;
};

// Read a data file into a header map plus its data rows, skipping '#' comments
// and blank lines.
bool ReadDataFile(const std::string& pfad, std::map<std::string, size_t>& outColumns,
                  std::vector<CSVRow>& outRows, std::string& outFehler) {
    std::string raw;
    if (!CSVReadFileRaw(pfad, raw, &outFehler)) {
        if (outFehler.empty()) outFehler = "Die Datei konnte nicht gelesen werden.";
        return false;
    }
    const CSVGrid grid = CSVParse(raw, DataFileOptions());

    bool headerSeen = false;
    for (const CSVRow& row : grid) {
        if (row.empty()) continue;
        const std::string first = Trim(row[0].text);
        if (first.empty() && row.size() == 1) continue;
        if (!first.empty() && first[0] == '#') continue;

        if (!headerSeen) {
            for (size_t i = 0; i < row.size(); ++i) outColumns[Trim(row[i].text)] = i;
            headerSeen = true;
            continue;
        }
        outRows.push_back(row);
    }
    if (!headerSeen) {
        outFehler = "Die Datei enthält keine Kopfzeile.";
        return false;
    }
    return true;
}

} // namespace

std::string FindeDatenDatei(const std::string& dateiname) {
    if (const char* override = std::getenv("ULTRAFIBU_DATA_DIR")) {
        const std::string candidate = JoinPath(override, dateiname);
        if (FileExists(candidate)) return candidate;
    }
    static const char* const kCandidates[] = {
        "data/",
        "Apps/UltraFIBU/data/",
        "../Apps/UltraFIBU/data/",
        "../../Apps/UltraFIBU/data/",
        "../data/",
        nullptr
    };
    for (const char* const* dir = kCandidates; *dir; ++dir) {
        const std::string candidate = std::string(*dir) + dateiname;
        if (FileExists(candidate)) return candidate;
    }
    return std::string();
}

LadeErgebnis LadeKontenrahmen(const std::string& pfad, const std::string& skr,
                              std::vector<Konto>& out) {
    LadeErgebnis ergebnis;
    std::map<std::string, size_t> columns;
    std::vector<CSVRow> rows;
    if (!ReadDataFile(pfad, columns, rows, ergebnis.fehler)) return ergebnis;

    if (!columns.count("nummer") || !columns.count("bezeichnung")) {
        ergebnis.fehler = "Die Kopfzeile braucht mindestens die Spalten \"nummer\" "
                          "und \"bezeichnung\".";
        return ergebnis;
    }

    int lineNumber = 1;
    for (const CSVRow& row : rows) {
        ++lineNumber;
        const NamedRow named(columns, row);
        Konto konto;
        konto.nummer      = named["nummer"];
        konto.bezeichnung = named["bezeichnung"];
        if (konto.nummer.empty() || konto.bezeichnung.empty()) {
            ergebnis.warnungen.push_back("Zeile " + std::to_string(lineNumber) +
                                         ": Nummer oder Bezeichnung fehlt - übersprungen.");
            continue;
        }
        const std::string typ = named["typ"];
        if (!typ.empty() && !KontoTypFromText(typ, konto.typ))
            ergebnis.warnungen.push_back("Zeile " + std::to_string(lineNumber) + ": Kontotyp \"" +
                                         typ + "\" ist unbekannt - \"aufwand\" angenommen.");
        konto.skr              = skr;
        konto.steuerschluessel = named["steuerschluessel"];
        konto.eurZeile         = named["euer_zeile"];
        konto.bwaPosition      = named["bwa_position"];
        konto.bilanzPosition   = named["bilanz_position"];
        konto.aktiv            = true;
        out.push_back(konto);
        ++ergebnis.zeilen;
    }
    ergebnis.ok = ergebnis.zeilen > 0;
    if (!ergebnis.ok && ergebnis.fehler.empty())
        ergebnis.fehler = "Die Datei enthält keine verwertbaren Konten.";
    return ergebnis;
}

LadeErgebnis LadeSteuerschluesselDatei(const std::string& pfad,
                                       std::vector<Steuerschluessel>& out) {
    LadeErgebnis ergebnis;
    std::map<std::string, size_t> columns;
    std::vector<CSVRow> rows;
    if (!ReadDataFile(pfad, columns, rows, ergebnis.fehler)) return ergebnis;

    if (!columns.count("schluessel") || !columns.count("art")) {
        ergebnis.fehler = "Die Kopfzeile braucht mindestens die Spalten \"schluessel\" "
                          "und \"art\".";
        return ergebnis;
    }

    int lineNumber = 1;
    for (const CSVRow& row : rows) {
        ++lineNumber;
        const NamedRow named(columns, row);
        Steuerschluessel key;
        key.schluessel  = named["schluessel"];
        key.bezeichnung = named["bezeichnung"];
        if (key.schluessel.empty()) {
            ergebnis.warnungen.push_back("Zeile " + std::to_string(lineNumber) +
                                         ": Schlüssel fehlt - übersprungen.");
            continue;
        }
        const std::string art = named["art"];
        if (!SteuerArtFromText(art, key.art)) {
            ergebnis.warnungen.push_back("Zeile " + std::to_string(lineNumber) + ": Steuerart \"" +
                                         art + "\" ist unbekannt - Zeile übersprungen.");
            continue;
        }
        // A missing or unreadable rate is refused rather than defaulted to
        // zero: 0 % is a legitimate rate, so a silent zero would be
        // indistinguishable from a correct one.
        const std::string satz = named["satz_promille"];
        bool satzOk = false;
        key.satzPromille = ToInt(satz, satzOk);
        if (!satzOk) {
            ergebnis.warnungen.push_back("Zeile " + std::to_string(lineNumber) +
                                         ": Steuersatz \"" + satz +
                                         "\" ist keine Zahl - Zeile übersprungen.");
            continue;
        }
        key.land        = named["land"];
        key.vorsteuer   = named["vorsteuer"] == "1";
        key.datevBu     = named["datev_bu"];
        key.kzBemessung = named["kz_bemessung"];
        key.kzSteuer    = named["kz_steuer"];
        key.kontoUmsatz = named["konto_umsatz"];
        key.kontoSteuer = named["konto_steuer"];

        const std::string von = named["gueltig_von"];
        if (!von.empty() && !Date::TryParseIso(von, key.gueltigVon))
            ergebnis.warnungen.push_back("Zeile " + std::to_string(lineNumber) +
                                         ": \"gueltig_von\" ist kein ISO-Datum (" + von + ").");
        const std::string bis = named["gueltig_bis"];
        if (!bis.empty() && !Date::TryParseIso(bis, key.gueltigBis))
            ergebnis.warnungen.push_back("Zeile " + std::to_string(lineNumber) +
                                         ": \"gueltig_bis\" ist kein ISO-Datum (" + bis + ").");

        out.push_back(key);
        ++ergebnis.zeilen;
    }
    ergebnis.ok = ergebnis.zeilen > 0;
    if (!ergebnis.ok && ergebnis.fehler.empty())
        ergebnis.fehler = "Die Datei enthält keine verwertbaren Steuerschlüssel.";
    return ergebnis;
}

} // namespace UltraFIBU
