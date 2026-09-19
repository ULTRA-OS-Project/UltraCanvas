// Apps/UltraFIBU/engine/UltraFIBUDatev.cpp
// The EXTF writer and the definition check. See the header for the three
// format rules this file exists to honour.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUDatev.h"

#include "UltraFIBUKontenrahmen.h"

#include <cstdio>
#include <ctime>

namespace UltraFIBU {

namespace {

// ===== CP1252 =====
//
// The file is CP1252. It agrees with Latin-1 from 0xA0 up, and fills
// 0x80..0x9F - Latin-1's unused control block - with 27 printable characters
// that live far away in Unicode. This is the same table the PDF writer needed
// for the euro sign; here it matters for the same reason and one more: a
// Buchungstext that arrives mojibake at a Kanzlei is what they see in their
// ledger for the next ten years.
bool Cp1252Sonderzeichen(uint32_t cp, unsigned char& out) {
    switch (cp) {
        case 0x20AC: out = 0x80; return true;   // euro
        case 0x201A: out = 0x82; return true;
        case 0x0192: out = 0x83; return true;
        case 0x201E: out = 0x84; return true;
        case 0x2026: out = 0x85; return true;
        case 0x2020: out = 0x86; return true;
        case 0x2021: out = 0x87; return true;
        case 0x02C6: out = 0x88; return true;
        case 0x2030: out = 0x89; return true;
        case 0x0160: out = 0x8A; return true;
        case 0x2039: out = 0x8B; return true;
        case 0x0152: out = 0x8C; return true;
        case 0x017D: out = 0x8E; return true;
        case 0x2018: out = 0x91; return true;
        case 0x2019: out = 0x92; return true;
        case 0x201C: out = 0x93; return true;
        case 0x201D: out = 0x94; return true;
        case 0x2022: out = 0x95; return true;
        case 0x2013: out = 0x96; return true;
        case 0x2014: out = 0x97; return true;
        case 0x02DC: out = 0x98; return true;
        case 0x2122: out = 0x99; return true;
        case 0x0161: out = 0x9A; return true;
        case 0x203A: out = 0x9B; return true;
        case 0x0153: out = 0x9C; return true;
        case 0x017E: out = 0x9E; return true;
        case 0x0178: out = 0x9F; return true;
        default: return false;
    }
}

std::string Zahl(int64_t wert) {
    std::string ziffern;
    int64_t v = wert < 0 ? -wert : wert;
    if (v == 0) ziffern = "0";
    while (v > 0) {
        ziffern.insert(ziffern.begin(), static_cast<char>('0' + (v % 10)));
        v /= 10;
    }
    if (wert < 0) ziffern.insert(ziffern.begin(), '-');
    return ziffern;
}

std::string ZahlBreit(int64_t wert, size_t stellen) {
    std::string text = Zahl(wert);
    while (text.size() < stellen) text.insert(text.begin(), '0');
    return text;
}

// A DATEV text field: quoted, and an embedded quote doubled. The separator is
// a semicolon, so a Buchungstext containing one must not break the row - the
// quoting is what prevents that, which is why text fields are quoted at all.
std::string TextFeld(const std::string& roh) {
    std::string out = "\"";
    for (char c : roh) {
        if (c == '"') out += "\"\"";
        else if (c == '\r' || c == '\n') out += ' ';
        else out.push_back(c);
    }
    out += "\"";
    return out;
}

std::string Zeitstempel(int64_t epochSekunden) {
    std::time_t zeit = static_cast<std::time_t>(
        epochSekunden != 0 ? epochSekunden : std::time(nullptr));
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &zeit);
#else
    localtime_r(&zeit, &tm);
#endif
    // YYYYMMDDHHMMSSFFF - the milliseconds are always 000; DATEV only needs
    // the field to be well formed and monotonic enough to tell two exports
    // apart, and inventing sub-second precision it does not have would be
    // worse than a constant.
    return ZahlBreit(tm.tm_year + 1900, 4) + ZahlBreit(tm.tm_mon + 1, 2) +
           ZahlBreit(tm.tm_mday, 2) + ZahlBreit(tm.tm_hour, 2) +
           ZahlBreit(tm.tm_min, 2) + ZahlBreit(tm.tm_sec, 2) + "000";
}

int TageImMonat(int jahr, int monat) { return Date::DaysInMonth(jahr, monat); }

const char* const kMonatsnamen[13] = {
    "", "Januar", "Februar", "März", "April", "Mai", "Juni",
    "Juli", "August", "September", "Oktober", "November", "Dezember"
};

// Split a line on ';' honouring quotes, so a semicolon inside a quoted field
// does not start a new column. Used only by the definition check, which reads
// files somebody else wrote.
std::vector<std::string> ZerlegeCsvZeile(const std::string& zeile) {
    std::vector<std::string> felder;
    std::string aktuell;
    bool inAnfuehrung = false;
    for (size_t i = 0; i < zeile.size(); ++i) {
        const char c = zeile[i];
        if (inAnfuehrung) {
            if (c == '"') {
                if (i + 1 < zeile.size() && zeile[i + 1] == '"') { aktuell += '"'; ++i; }
                else inAnfuehrung = false;
            } else {
                aktuell.push_back(c);
            }
        } else if (c == '"') {
            inAnfuehrung = true;
        } else if (c == ';') {
            felder.push_back(aktuell);
            aktuell.clear();
        } else if (c != '\r') {
            aktuell.push_back(c);
        }
    }
    felder.push_back(aktuell);
    return felder;
}

// CP1252 to UTF-8, for reading a real DATEV file back.
std::string VonCp1252(const std::string& roh) {
    static const uint32_t kHoch[32] = {
        0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
        0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
    };
    std::string out;
    for (unsigned char c : roh) {
        uint32_t cp = c;
        if (c >= 0x80 && c <= 0x9F) cp = kHoch[c - 0x80];
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

} // namespace

// ===== ENCODING =====

std::string NachCp1252(const std::string& utf8, bool& verlust) {
    verlust = false;
    std::string out;
    size_t i = 0;
    const size_t n = utf8.size();
    while (i < n) {
        uint32_t cp = static_cast<uint8_t>(utf8[i]);
        size_t extra = 0;
        if (cp >= 0xF0)      { cp &= 0x07; extra = 3; }
        else if (cp >= 0xE0) { cp &= 0x0F; extra = 2; }
        else if (cp >= 0xC0) { cp &= 0x1F; extra = 1; }
        if (i + extra >= n && extra > 0) break;
        for (size_t k = 0; k < extra; ++k)
            cp = (cp << 6) | (static_cast<uint8_t>(utf8[i + 1 + k]) & 0x3F);
        i += 1 + extra;

        unsigned char sonder = 0;
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (Cp1252Sonderzeichen(cp, sonder)) {
            out.push_back(static_cast<char>(sonder));
        } else if (cp >= 0xA0 && cp <= 0xFF) {
            out.push_back(static_cast<char>(cp));
        } else {
            out.push_back('?');
            verlust = true;
        }
    }
    return out;
}

std::string DatevDefinitionPfad(const std::string& dateiname) {
    return FindeDatenDatei(dateiname);
}

// ===== DEFINITION =====

bool DatevDefinition::Laden(const std::string& dateipfad, std::string& fehler) {
    spalten_.clear();
    if (dateipfad.empty()) {
        fehler = "Die Spaltendefinition wurde nicht gefunden. "
                 "ULTRAFIBU_DATA_DIR setzen oder die Datei neben das Programm legen.";
        return false;
    }
    std::FILE* datei = std::fopen(dateipfad.c_str(), "rb");
    if (datei == nullptr) {
        fehler = "Die Spaltendefinition \"" + dateipfad + "\" ist nicht lesbar.";
        return false;
    }

    std::string inhalt;
    char puffer[4096];
    size_t gelesen = 0;
    while ((gelesen = std::fread(puffer, 1, sizeof(puffer), datei)) > 0)
        inhalt.append(puffer, gelesen);
    std::fclose(datei);

    size_t start = 0;
    int erwartet = 1;
    while (start <= inhalt.size()) {
        size_t ende = inhalt.find('\n', start);
        if (ende == std::string::npos) ende = inhalt.size();
        std::string zeile = inhalt.substr(start, ende - start);
        start = ende + 1;
        if (!zeile.empty() && zeile.back() == '\r') zeile.pop_back();
        if (zeile.empty() || zeile[0] == '#') continue;

        const std::vector<std::string> teile = ZerlegeCsvZeile(zeile);
        if (teile.size() < 3) {
            fehler = "Zeile \"" + zeile + "\" braucht drei Felder: nummer;name;typ.";
            return false;
        }
        // The number is not used for placement - names are - but a gap in it
        // means somebody deleted a line by accident, and a definition with a
        // hole in it writes every later value into the wrong column.
        const int nummer = std::atoi(teile[0].c_str());
        if (nummer != erwartet) {
            fehler = "Die Spaltennummern springen: nach " + Zahl(erwartet - 1) +
                     " folgt " + Zahl(nummer) + ". Eine fehlende Zeile würde "
                     "alle weiteren Werte in die falsche Spalte schreiben.";
            return false;
        }
        ++erwartet;

        DatevSpalte spalte;
        spalte.name = teile[1];
        spalte.typ  = teile[2];
        spalten_.push_back(spalte);
    }

    if (spalten_.empty()) {
        fehler = "Die Spaltendefinition \"" + dateipfad + "\" enthält keine Spalten.";
        return false;
    }
    return true;
}

int DatevDefinition::Index(const std::string& name) const {
    for (size_t i = 0; i < spalten_.size(); ++i)
        if (spalten_[i].name == name) return static_cast<int>(i);
    return -1;
}

// ===== WRITING =====

namespace {

// The preamble and the column line, then the rows. Everything goes through
// NachCp1252 on the way out and the line ending is CRLF, both because that is
// what the format is and because neither is visible in a diff if it is wrong.
bool SchreibeDatei(const std::string& pfad, const DatevKopf& kopf,
                   const DatevDefinition& definition,
                   const std::vector<std::vector<std::string>>& zeilen,
                   std::string& fehler, bool& zeichenverlust) {
    std::string inhalt;
    zeichenverlust = false;

    // ---- line 1: the preamble ----
    // Positions follow the format description; the ones this application must
    // get right are documented in the proposal §4.1 and filled from the
    // Mandant and the Geschaeftsjahr.
    std::vector<std::string> kopfzeile;
    kopfzeile.push_back(TextFeld(kopf.kennzeichen));                 //  1
    kopfzeile.push_back(Zahl(kopf.versionsnummer));                  //  2
    kopfzeile.push_back(Zahl(static_cast<int>(kopf.kategorie)));     //  3
    kopfzeile.push_back(TextFeld(kopf.formatname));                  //  4
    kopfzeile.push_back(Zahl(kopf.formatversion));                   //  5
    kopfzeile.push_back(Zeitstempel(kopf.erzeugtAm));                //  6
    kopfzeile.push_back("");                                         //  7 importiert
    kopfzeile.push_back(TextFeld("UF"));                             //  8 Herkunft
    kopfzeile.push_back(TextFeld(kopf.exportiertVon));               //  9
    kopfzeile.push_back("");                                         // 10 importiert von
    kopfzeile.push_back(Zahl(std::atoll(kopf.beraternummer.c_str())));   // 11
    kopfzeile.push_back(Zahl(std::atoll(kopf.mandantennummer.c_str()))); // 12
    kopfzeile.push_back(FormatDateCompact(kopf.wjBeginn));           // 13 WJ-Beginn
    kopfzeile.push_back(Zahl(kopf.sachkontenlaenge));                // 14
    kopfzeile.push_back(FormatDateCompact(kopf.von));                // 15
    kopfzeile.push_back(FormatDateCompact(kopf.bis));                // 16
    kopfzeile.push_back(TextFeld(kopf.bezeichnung));                 // 17
    kopfzeile.push_back(TextFeld(""));                               // 18 Diktatkürzel
    kopfzeile.push_back(Zahl(kopf.kategorie == DatevKategorie::Buchungsstapel ? 1 : 0));
    kopfzeile.push_back("");                                         // 20 Zahlensperre
    kopfzeile.push_back("");                                         // 21 Sachkontenrahmen
    kopfzeile.push_back("");                                         // 22 ID der Branchenlösung
    kopfzeile.push_back("");                                         // 23
    kopfzeile.push_back("");                                         // 24
    kopfzeile.push_back(TextFeld(kopf.waehrung));                    // 25
    kopfzeile.push_back(Zahl(kopf.festschreibung));                  // 26

    for (size_t i = 0; i < kopfzeile.size(); ++i) {
        if (i) inhalt += ';';
        inhalt += kopfzeile[i];
    }
    inhalt += "\r\n";

    // ---- line 2: the column names ----
    for (size_t i = 0; i < definition.Spalten().size(); ++i) {
        if (i) inhalt += ';';
        inhalt += TextFeld(definition.Spalten()[i].name);
    }
    inhalt += "\r\n";

    // ---- the data ----
    for (const std::vector<std::string>& zeile : zeilen) {
        for (size_t i = 0; i < definition.Spalten().size(); ++i) {
            if (i) inhalt += ';';
            const std::string wert = i < zeile.size() ? zeile[i] : std::string();
            if (wert.empty()) {
                // An empty text column still needs its quotes; an empty number
                // column must stay bare.
                inhalt += definition.Spalten()[i].IstText() ? "\"\"" : "";
            } else {
                inhalt += definition.Spalten()[i].IstText() ? TextFeld(wert) : wert;
            }
        }
        inhalt += "\r\n";
    }

    const std::string kodiert = NachCp1252(inhalt, zeichenverlust);

    std::FILE* datei = std::fopen(pfad.c_str(), "wb");
    if (datei == nullptr) {
        fehler = "Die Datei \"" + pfad + "\" konnte nicht geschrieben werden.";
        return false;
    }
    const size_t geschrieben = std::fwrite(kodiert.data(), 1, kodiert.size(), datei);
    std::fclose(datei);
    if (geschrieben != kodiert.size()) {
        fehler = "Die Datei \"" + pfad + "\" wurde nur unvollständig geschrieben.";
        return false;
    }
    return true;
}

// Berater- and Mandantennummer are assigned by the Kanzlei and an import is
// refused without them, so this refuses first and names what is missing.
bool KopfVollstaendig(const Mandant& mandant, std::string& fehler) {
    if (mandant.beraternummer.empty() || mandant.mandantennummer.empty()) {
        fehler = "Für einen DATEV-Export fehlen die Berater- und die "
                 "Mandantennummer. Beide vergibt die Kanzlei; ohne sie wird der "
                 "Import dort abgelehnt.";
        return false;
    }
    return true;
}

} // namespace

DatevErgebnis SchreibeBuchungsstapel(const Mandant& mandant,
                                     const Geschaeftsjahr& jahr,
                                     const std::vector<Buchung>& buchungen,
                                     const DatevDefinition& definition,
                                     int kalenderJahr, int monat,
                                     const std::string& zielVerzeichnis,
                                     const std::string& exportiertVon) {
    DatevErgebnis ergebnis;
    if (monat < 1 || monat > 12) {
        ergebnis.fehler = "\"" + Zahl(monat) + "\" ist kein Monat.";
        return ergebnis;
    }
    if (!KopfVollstaendig(mandant, ergebnis.fehler)) return ergebnis;

    const Date von(kalenderJahr, monat, 1);
    const Date bis(kalenderJahr, monat, TageImMonat(kalenderJahr, monat));

    // A Buchungsstapel carries no year on its Belegdatum, so the year is read
    // from the Wirtschaftsjahr. A month that is not inside this fiscal year
    // would therefore be booked into the wrong one.
    if (!jahr.Contains(von) || !jahr.Contains(bis)) {
        ergebnis.fehler =
            std::string(kMonatsnamen[monat]) + " " + Zahl(kalenderJahr) +
            " liegt nicht vollständig im Geschäftsjahr \"" + jahr.bezeichnung +
            "\" (" + FormatDateGerman(jahr.beginn) + " - " +
            FormatDateGerman(jahr.ende) + "). Der Belegdatum-Feld eines "
            "Buchungsstapels trägt kein Jahr; DATEV leitet es aus dem "
            "Wirtschaftsjahr ab, und ein Monat außerhalb würde falsch "
            "einsortiert.";
        return ergebnis;
    }

    const int kontoSpalte      = definition.Index("Konto");
    const int gegenkontoSpalte = definition.Index("Gegenkonto (ohne BU-Schlüssel)");
    const int umsatzSpalte     = definition.Index("Umsatz (ohne Soll/Haben-Kz)");
    const int shSpalte         = definition.Index("Soll/Haben-Kennzeichen");
    if (kontoSpalte < 0 || gegenkontoSpalte < 0 || umsatzSpalte < 0 || shSpalte < 0) {
        ergebnis.fehler =
            "Der Spaltendefinition fehlen Pflichtspalten (Umsatz, "
            "Soll/Haben-Kennzeichen, Konto, Gegenkonto). Bitte "
            "data/DATEV-Buchungsstapel-v700.csv prüfen.";
        return ergebnis;
    }

    const int wkzSpalte        = definition.Index("WKZ Umsatz");
    const int buSpalte         = definition.Index("BU-Schlüssel");
    const int belegdatumSpalte = definition.Index("Belegdatum");
    const int beleg1Spalte     = definition.Index("Belegfeld 1");
    const int beleg2Spalte     = definition.Index("Belegfeld 2");
    const int textSpalte       = definition.Index("Buchungstext");
    const int kost1Spalte      = definition.Index("KOST1 - Kostenstelle");
    const int kost2Spalte      = definition.Index("KOST2 - Kostenstelle");
    const int festSpalte       = definition.Index("Festschreibung");
    const int leistungSpalte   = definition.Index("Leistungsdatum");

    std::vector<std::vector<std::string>> zeilen;
    bool alleFestgeschrieben = true;
    int  ohneBuSchluessel = 0;

    for (const Buchung& b : buchungen) {
        if (!b.belegdatum.Valid()) continue;
        if (b.belegdatum < von || b.belegdatum > bis) continue;

        std::vector<std::string> zeile(definition.Anzahl());
        auto setze = [&](int spalte, const std::string& wert) {
            if (spalte >= 0 && spalte < static_cast<int>(zeile.size()))
                zeile[static_cast<size_t>(spalte)] = wert;
        };

        // Umsatz is unsigned; the direction is the Soll/Haben flag. Feeding a
        // signed amount here produces a plausible-looking, wrong ledger.
        const int64_t minor = b.umsatz.Minor() < 0 ? -b.umsatz.Minor() : b.umsatz.Minor();
        setze(umsatzSpalte, Money::FromMinor(minor, b.waehrung)
                                .ToString(UltraCanvas::MoneyStyle::Datev));
        setze(shSpalte, SollHabenToText(b.sollHaben));
        setze(wkzSpalte, b.waehrung);
        setze(kontoSpalte, b.konto);
        setze(gegenkontoSpalte, b.gegenkonto);
        setze(buSpalte, b.buSchluessel);
        setze(belegdatumSpalte, FormatDateDatev(b.belegdatum));
        setze(beleg1Spalte, b.belegfeld1);
        setze(beleg2Spalte, b.belegfeld2);
        setze(textSpalte, b.buchungstext);
        setze(kost1Spalte, b.kost1);
        setze(kost2Spalte, b.kost2);
        setze(festSpalte, b.festgeschrieben ? "1" : "0");
        if (b.belegdatum.Valid()) setze(leistungSpalte, FormatDateCompact(b.belegdatum));

        if (!b.festgeschrieben) alleFestgeschrieben = false;
        if (!b.steuerschluessel.empty() && b.buSchluessel.empty()) ++ohneBuSchluessel;

        zeilen.push_back(std::move(zeile));
    }

    if (zeilen.empty()) {
        ergebnis.fehler = "Im " + std::string(kMonatsnamen[monat]) + " " +
                          Zahl(kalenderJahr) + " gibt es keine Buchungen.";
        return ergebnis;
    }

    DatevKopf kopf;
    kopf.kategorie        = DatevKategorie::Buchungsstapel;
    kopf.formatname       = "Buchungsstapel";
    kopf.formatversion    = 13;
    kopf.exportiertVon    = exportiertVon;
    kopf.beraternummer    = mandant.beraternummer;
    kopf.mandantennummer  = mandant.mandantennummer;
    kopf.wjBeginn         = jahr.beginn;
    kopf.sachkontenlaenge = jahr.sachkontenlaenge;
    kopf.von              = von;
    kopf.bis              = bis;
    kopf.bezeichnung      = std::string(kMonatsnamen[monat]) + " " + Zahl(kalenderJahr);
    kopf.waehrung         = mandant.waehrung;
    kopf.festschreibung   = alleFestgeschrieben ? 1 : 0;

    const std::string pfad = (zielVerzeichnis.empty() ? std::string() :
                              zielVerzeichnis + "/") +
                             "EXTF_Buchungsstapel_" + Zahl(kalenderJahr) +
                             ZahlBreit(monat, 2) + ".csv";

    bool verlust = false;
    if (!SchreibeDatei(pfad, kopf, definition, zeilen, ergebnis.fehler, verlust))
        return ergebnis;

    ergebnis.ok     = true;
    ergebnis.datei  = pfad;
    ergebnis.zeilen = static_cast<int>(zeilen.size());

    if (ohneBuSchluessel > 0) {
        // The most consequential thing that can be wrong in a file that still
        // imports: DATEV books without the tax automatics and the VAT lands
        // nowhere.
        ergebnis.warnungen.push_back(
            Zahl(ohneBuSchluessel) + " Buchung(en) haben einen Steuerschlüssel, "
            "aber keinen DATEV-BU-Schlüssel. DATEV bucht sie dann ohne "
            "Steuerautomatik. Die Zuordnung steht in "
            "data/Steuerschluessel.csv, Spalte datev_bu, und ist dort noch leer.");
    }
    if (verlust) {
        ergebnis.warnungen.push_back(
            "Einzelne Zeichen lassen sich nicht in CP1252 darstellen und wurden "
            "durch \"?\" ersetzt.");
    }
    if (!alleFestgeschrieben) {
        ergebnis.warnungen.push_back(
            "Der Stapel enthält nicht festgeschriebene Buchungen und wird als "
            "nicht festgeschrieben übergeben - die Kanzlei kann ihn dort noch "
            "korrigieren.");
    }
    return ergebnis;
}

DatevErgebnis SchreibeKontenbeschriftungen(const Mandant& mandant,
                                           const Geschaeftsjahr& jahr,
                                           const std::vector<Konto>& konten,
                                           const DatevDefinition& definition,
                                           const std::string& zielVerzeichnis,
                                           const std::string& exportiertVon) {
    DatevErgebnis ergebnis;
    if (!KopfVollstaendig(mandant, ergebnis.fehler)) return ergebnis;

    const int kontoSpalte  = definition.Index("Konto");
    const int textSpalte   = definition.Index("Kontenbeschriftung");
    const int spracheSpalte = definition.Index("Sprach-ID");
    if (kontoSpalte < 0 || textSpalte < 0) {
        ergebnis.fehler = "Der Spaltendefinition fehlen \"Konto\" oder "
                          "\"Kontenbeschriftung\".";
        return ergebnis;
    }

    std::vector<std::vector<std::string>> zeilen;
    for (const Konto& konto : konten) {
        if (!konto.aktiv || konto.nummer.empty()) continue;
        std::vector<std::string> zeile(definition.Anzahl());
        zeile[static_cast<size_t>(kontoSpalte)] = konto.nummer;
        zeile[static_cast<size_t>(textSpalte)]  = konto.bezeichnung;
        if (spracheSpalte >= 0)
            zeile[static_cast<size_t>(spracheSpalte)] = "de-DE";
        zeilen.push_back(std::move(zeile));
    }
    if (zeilen.empty()) {
        ergebnis.fehler = "Es gibt keine aktiven Konten zum Exportieren.";
        return ergebnis;
    }

    DatevKopf kopf;
    kopf.kategorie        = DatevKategorie::Sachkontenbeschriftungen;
    kopf.formatname       = "Kontenbeschriftungen";
    kopf.formatversion    = 3;
    kopf.exportiertVon    = exportiertVon;
    kopf.beraternummer    = mandant.beraternummer;
    kopf.mandantennummer  = mandant.mandantennummer;
    kopf.wjBeginn         = jahr.beginn;
    kopf.sachkontenlaenge = jahr.sachkontenlaenge;
    kopf.von              = jahr.beginn;
    kopf.bis              = jahr.ende;
    kopf.bezeichnung      = "Kontenbeschriftungen " + jahr.bezeichnung;
    kopf.waehrung         = mandant.waehrung;

    const std::string pfad = (zielVerzeichnis.empty() ? std::string() :
                              zielVerzeichnis + "/") +
                             "EXTF_Kontenbeschriftungen_" +
                             FormatDateCompact(jahr.beginn) + ".csv";

    bool verlust = false;
    if (!SchreibeDatei(pfad, kopf, definition, zeilen, ergebnis.fehler, verlust))
        return ergebnis;

    ergebnis.ok     = true;
    ergebnis.datei  = pfad;
    ergebnis.zeilen = static_cast<int>(zeilen.size());
    if (verlust)
        ergebnis.warnungen.push_back(
            "Einzelne Zeichen lassen sich nicht in CP1252 darstellen und wurden "
            "durch \"?\" ersetzt.");
    return ergebnis;
}

// ===== CHECKING A REAL FILE =====

DatevPruefung PruefeDateiGegenDefinition(const std::string& dateipfad,
                                         const DatevDefinition& definition) {
    DatevPruefung pruefung;
    pruefung.spaltenInDefinition = definition.Anzahl();

    std::FILE* datei = std::fopen(dateipfad.c_str(), "rb");
    if (datei == nullptr) {
        pruefung.fehler = "Die Datei \"" + dateipfad + "\" ist nicht lesbar.";
        return pruefung;
    }
    std::string roh;
    char puffer[8192];
    size_t gelesen = 0;
    // The first two lines are all that is needed, but a Buchungsstapel is
    // small enough that reading it whole is simpler than a streaming parser.
    while ((gelesen = std::fread(puffer, 1, sizeof(puffer), datei)) > 0)
        roh.append(puffer, gelesen);
    std::fclose(datei);

    const std::string inhalt = VonCp1252(roh);
    size_t ersteZeileEnde = inhalt.find('\n');
    if (ersteZeileEnde == std::string::npos) {
        pruefung.fehler = "Die Datei hat keine zweite Zeile - eine DATEV-Datei "
                          "besteht aus Kopfzeile, Spaltenzeile und Daten.";
        return pruefung;
    }
    size_t zweiteZeileEnde = inhalt.find('\n', ersteZeileEnde + 1);
    if (zweiteZeileEnde == std::string::npos) zweiteZeileEnde = inhalt.size();

    const std::vector<std::string> kopf =
        ZerlegeCsvZeile(inhalt.substr(0, ersteZeileEnde));
    const std::vector<std::string> spalten = ZerlegeCsvZeile(
        inhalt.substr(ersteZeileEnde + 1, zweiteZeileEnde - ersteZeileEnde - 1));

    if (kopf.size() > 0) pruefung.kennzeichen    = kopf[0];
    if (kopf.size() > 1) pruefung.versionsnummer = std::atoi(kopf[1].c_str());
    if (kopf.size() > 2) pruefung.kategorie      = std::atoi(kopf[2].c_str());
    if (kopf.size() > 4) pruefung.formatversion  = std::atoi(kopf[4].c_str());
    pruefung.spaltenInDatei = spalten.size();

    if (pruefung.kennzeichen != "EXTF" && pruefung.kennzeichen != "DTVF") {
        pruefung.fehler = "Die erste Spalte der Kopfzeile ist \"" +
                          pruefung.kennzeichen +
                          "\" und nicht EXTF oder DTVF - das ist keine "
                          "DATEV-Datei.";
        return pruefung;
    }

    if (spalten.size() != definition.Anzahl()) {
        pruefung.abweichungen.push_back(
            "Die Datei hat " + Zahl(static_cast<int64_t>(spalten.size())) +
            " Spalten, die Definition " +
            Zahl(static_cast<int64_t>(definition.Anzahl())) + ".");
    }

    const size_t gemeinsam = spalten.size() < definition.Anzahl()
                                 ? spalten.size() : definition.Anzahl();
    for (size_t i = 0; i < gemeinsam; ++i) {
        if (spalten[i] == definition.Spalten()[i].name) continue;
        pruefung.abweichungen.push_back(
            "Spalte " + Zahl(static_cast<int64_t>(i + 1)) + ": Datei \"" +
            spalten[i] + "\", Definition \"" + definition.Spalten()[i].name + "\".");
    }
    for (size_t i = gemeinsam; i < spalten.size(); ++i) {
        pruefung.abweichungen.push_back(
            "Spalte " + Zahl(static_cast<int64_t>(i + 1)) + " fehlt in der "
            "Definition: \"" + spalten[i] + "\".");
    }
    for (size_t i = gemeinsam; i < definition.Anzahl(); ++i) {
        pruefung.abweichungen.push_back(
            "Spalte " + Zahl(static_cast<int64_t>(i + 1)) + " steht nur in der "
            "Definition: \"" + definition.Spalten()[i].name + "\".");
    }

    pruefung.ok = pruefung.abweichungen.empty();
    return pruefung;
}

} // namespace UltraFIBU
