// Apps/UltraFIBU/report/UltraFIBURechnungPdf.cpp
// Builds the invoice as a VectorDocument and lets the framework's PDF writer
// emit it. See the header for why there is no PDF emitter in this file.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBURechnungPdf.h"

#include "UltraCanvasVectorConverter.h"
#include "DataFormats/UltraCanvasVectorStorage.h"

#include <cstdio>
#include <memory>

namespace UltraFIBU {

using namespace UltraCanvas;
using namespace UltraCanvas::VectorStorage;

namespace {

// ===== FONT METRICS =====
//
// Nothing is embedded, so the widths have to be known rather than measured.
// These are the standard Adobe AFM advance widths for the base-14 Helvetica,
// in 1/1000 em. The ones that matter are the digits: in Helvetica every digit
// is 556 wide, which is what lets a column of amounts line up exactly. An error
// in a letter's width moves a label by a fraction of a point and is cosmetic;
// an error in a digit's width would be visible, and 556 is a value that can be
// checked against any Helvetica AFM in a second.
const short kHelvetica[95] = {
    // 0x20..0x7E
    278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278,
    556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556,
    1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778,
    667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556,
    333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556,
    556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584
};
const short kHelveticaBold[95] = {
    278, 333, 474, 556, 556, 889, 722, 238, 333, 333, 389, 584, 278, 333, 278, 278,
    556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 333, 333, 584, 584, 584, 611,
    975, 722, 722, 722, 722, 667, 611, 778, 722, 278, 556, 722, 611, 833, 722, 778,
    667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 333, 278, 333, 584, 556,
    333, 556, 611, 556, 611, 556, 333, 611, 611, 278, 278, 556, 278, 889, 611, 611,
    611, 611, 389, 556, 333, 611, 556, 778, 556, 556, 500, 389, 280, 389, 584
};

// The German letters and the euro sign, by Unicode code point. Everything else
// outside ASCII falls back to the average below, which is only ever a
// hair's-breadth of alignment.
short BreiteSonderzeichen(uint32_t cp, bool fett) {
    switch (cp) {
        case 0x00E4: return fett ? 556 : 556;   // a-umlaut
        case 0x00F6: return fett ? 611 : 556;   // o-umlaut
        case 0x00FC: return fett ? 611 : 556;   // u-umlaut
        case 0x00C4: return fett ? 722 : 667;   // A-umlaut
        case 0x00D6: return fett ? 778 : 778;   // O-umlaut
        case 0x00DC: return fett ? 722 : 722;   // U-umlaut
        case 0x00DF: return fett ? 611 : 611;   // sharp s
        case 0x20AC: return 556;                // euro
        case 0x00A7: return 556;                // section sign
        case 0x00B0: return fett ? 400 : 400;   // degree
        case 0x2013: return 556;                // en dash
        case 0x00B7: return fett ? 333 : 278;   // middle dot
        default:     return 0;
    }
}

} // namespace

double TextBreite(const std::string& text, double schriftgroesse, bool fett) {
    const short* tabelle = fett ? kHelveticaBold : kHelvetica;
    double tausendstel = 0.0;

    size_t i = 0;
    const size_t n = text.size();
    while (i < n) {
        // Decode one UTF-8 code point; the width table is per character, not
        // per byte, so an umlaut must not count twice.
        uint32_t cp = static_cast<uint8_t>(text[i]);
        size_t extra = 0;
        if (cp >= 0xF0)      { cp &= 0x07; extra = 3; }
        else if (cp >= 0xE0) { cp &= 0x0F; extra = 2; }
        else if (cp >= 0xC0) { cp &= 0x1F; extra = 1; }
        if (i + extra >= n && extra > 0) break;
        for (size_t k = 0; k < extra; ++k)
            cp = (cp << 6) | (static_cast<uint8_t>(text[i + 1 + k]) & 0x3F);
        i += 1 + extra;

        if (cp >= 0x20 && cp <= 0x7E) {
            tausendstel += tabelle[cp - 0x20];
        } else if (short sonder = BreiteSonderzeichen(cp, fett); sonder != 0) {
            tausendstel += sonder;
        } else {
            tausendstel += 556;    // the average; only ever a cosmetic error
        }
    }
    return tausendstel * schriftgroesse / 1000.0;
}

namespace {

// ===== THE PAGE =====
//
// A thin wrapper over the VectorDocument so the layout below reads like a
// layout and not like a tree of shared_ptr. Coordinates are top-down, which is
// what the PDF writer expects and flips for itself.
class Seite {
public:
    Seite(VectorDocument& doc, const RechnungLayout& layout)
        : doc_(doc), layout_(layout) {
        doc_.Size = { layout.seiteBreite, layout.seiteHoehe };
        ebene_ = doc_.AddLayer("rechnung");
    }

    double Links()  const { return layout_.randLinks; }
    double Rechts() const { return layout_.seiteBreite - layout_.randRechts; }
    double Unten()  const { return layout_.seiteHoehe - layout_.randUnten; }

    void Text(double x, double y, const std::string& text, double groesse = 10.0,
              bool fett = false, const Color& farbe = Color(0, 0, 0)) {
        if (text.empty()) return;
        auto element = std::make_shared<VectorText>();
        element->Position = { x, y };
        element->BaseStyle.FontFamily = layout_.schrift;
        element->BaseStyle.FontSize   = static_cast<float>(groesse);
        element->BaseStyle.Weight     = fett ? FontWeight::Bold : FontWeight::Normal;
        // Anchor::Start always: the writer approximates centre and right
        // anchoring from an average glyph width, and an invoice's money column
        // cannot be approximate. Right alignment is done here instead, by
        // measuring with TextBreite and moving the start.
        element->BaseStyle.Anchor     = TextAnchor::Start;
        element->SetText(text);
        element->Style.Fill = farbe;
        ebene_->AddChild(element);
    }

    void TextRechts(double xRechts, double y, const std::string& text,
                    double groesse = 10.0, bool fett = false,
                    const Color& farbe = Color(0, 0, 0)) {
        Text(xRechts - TextBreite(text, groesse, fett), y, text, groesse, fett, farbe);
    }

    void Linie(double x1, double y1, double x2, double y2, double staerke = 0.6,
               const Color& farbe = Color(0, 0, 0)) {
        auto element = std::make_shared<VectorLine>();
        element->Start = { x1, y1 };
        element->End   = { x2, y2 };
        StrokeData stroke;
        stroke.Fill  = farbe;
        stroke.Width = static_cast<float>(staerke);
        element->Style.Stroke = stroke;
        ebene_->AddChild(element);
    }

private:
    VectorDocument&              doc_;
    const RechnungLayout&        layout_;
    std::shared_ptr<VectorLayer> ebene_;
};

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

// A quantity held in thousandths, printed the way an invoice prints it: no
// decimals when it is a whole number, otherwise up to three, trailing zeros
// dropped. Locale-free, like everything else that touches a number here.
std::string Menge(int64_t tausendstel) {
    const bool negativ = tausendstel < 0;
    int64_t v = negativ ? -tausendstel : tausendstel;
    std::string text = Zahl(v / 1000);
    const int64_t rest = v % 1000;
    if (rest != 0) {
        std::string nachkomma = Zahl(rest);
        while (nachkomma.size() < 3) nachkomma.insert(nachkomma.begin(), '0');
        while (!nachkomma.empty() && nachkomma.back() == '0') nachkomma.pop_back();
        text += "," + nachkomma;
    }
    return negativ ? "-" + text : text;
}

// A tax rate in permille as an invoice writes it: "19 %", "7 %", "2,5 %".
std::string Satz(int promille) {
    std::string text = Zahl(promille / 10);
    if (promille % 10 != 0) text += "," + Zahl(promille % 10);
    return text + " %";
}

std::string OrtZeile(const std::string& plz, const std::string& ort) {
    if (plz.empty()) return ort;
    if (ort.empty()) return plz;
    return plz + " " + ort;
}

// Greedy word wrap to `breite` points. Long single words are left to overhang
// rather than broken mid-word, which on an article description is the lesser
// evil.
std::vector<std::string> Umbrechen(const std::string& text, double breite,
                                   double groesse, bool fett) {
    std::vector<std::string> zeilen;
    if (text.empty()) return zeilen;

    std::string zeile;
    size_t i = 0;
    while (i <= text.size()) {
        const size_t leer = text.find(' ', i);
        const std::string wort = text.substr(i, leer == std::string::npos
                                                    ? std::string::npos : leer - i);
        const std::string kandidat = zeile.empty() ? wort : zeile + " " + wort;
        if (!zeile.empty() && TextBreite(kandidat, groesse, fett) > breite) {
            zeilen.push_back(zeile);
            zeile = wort;
        } else {
            zeile = kandidat;
        }
        if (leer == std::string::npos) break;
        i = leer + 1;
    }
    if (!zeile.empty()) zeilen.push_back(zeile);
    return zeilen;
}

// The title on the document, which is not decoration: a Gutschrift must say so
// (§ 14 Abs. 4 Nr. 10 UStG), and a reversal that looks like an invoice is how a
// customer pays twice.
std::string Ueberschrift(const Beleg& beleg) {
    if (beleg.IstStorno()) return "Stornorechnung";
    switch (beleg.art) {
        case BelegArt::Ausgangsrechnung:   return "Rechnung";
        case BelegArt::Ausgangsgutschrift: return "Gutschrift";
        case BelegArt::Eingangsrechnung:   return "Eingangsrechnung";
        case BelegArt::Eingangsgutschrift: return "Gutschrift (Eingang)";
        case BelegArt::Kassenbeleg:        return "Kassenbeleg";
        case BelegArt::Sonstiges:          break;
    }
    return "Beleg";
}

// One line of the tax summary, and the note that goes under the total when a
// rate is zero. § 14 Abs. 4 Nr. 8 UStG: an exemption has to name itself.
struct Steuerzeile {
    std::string schluessel;
    int         satzPromille = 0;
    Money       bemessung;
    Money       steuer;
};

std::string Befreiungshinweis(const Steuerschluessel& key) {
    switch (key.art) {
        case SteuerArt::IgLieferung:
            return "Steuerfreie innergemeinschaftliche Lieferung "
                   "(§ 4 Nr. 1 Buchst. b i. V. m. § 6a UStG).";
        case SteuerArt::Drittland:
            return "Steuerfreie Ausfuhrlieferung (§ 4 Nr. 1 Buchst. a i. V. m. § 6 UStG).";
        case SteuerArt::EuSonstigeLeistung:
            // § 14a Abs. 1 UStG requires this exact phrase, and both parties'
            // VAT numbers, on a service invoiced to a business in another
            // member state. The customer accounts for the tax at their rate,
            // in their country; this invoice carries none.
            return "Steuerschuldnerschaft des Leistungsempfängers "
                   "(Reverse Charge, § 3a Abs. 2 UStG).";
        case SteuerArt::ReverseCharge13b:
            return "Steuerschuldnerschaft des Leistungsempfängers (§ 13b UStG).";
        case SteuerArt::Oss:
            return "Die Umsatzsteuer wird im Bestimmungsland geschuldet und über das "
                   "One-Stop-Shop-Verfahren erklärt.";
        case SteuerArt::Kleinunternehmer:
            return "Gemäß § 19 UStG wird keine Umsatzsteuer berechnet.";
        case SteuerArt::NichtSteuerbar:
            return "Nicht steuerbarer Umsatz (Leistungsort im Ausland).";
        case SteuerArt::Inland:
        case SteuerArt::IgErwerb:
            break;
    }
    return std::string();
}

} // namespace

// ===== § 14 UStG =====

std::vector<std::string> PruefePflichtangaben(const Mandant& mandant, const Beleg& beleg,
                                              const Partner& empfaenger) {
    std::vector<std::string> fehlt;

    // Nr. 1 - the supplier, in full.
    if (mandant.name.empty())    fehlt.push_back("Name des leistenden Unternehmers");
    if (mandant.strasse.empty()) fehlt.push_back("Straße des leistenden Unternehmers");
    if (mandant.ort.empty())     fehlt.push_back("Ort des leistenden Unternehmers");

    // Nr. 2 - the recipient, in full.
    if (empfaenger.name.empty())    fehlt.push_back("Name des Leistungsempfängers");
    if (empfaenger.strasse.empty()) fehlt.push_back("Straße des Leistungsempfängers");
    if (empfaenger.ort.empty())     fehlt.push_back("Ort des Leistungsempfängers");

    // Nr. 2 - one of the two tax numbers. Not both: either satisfies the
    // requirement, and for an intra-community supply the USt-IdNr. is the one
    // that has to be there.
    if (mandant.steuernummer.empty() && mandant.ustIdNr.empty())
        fehlt.push_back("Steuernummer oder USt-IdNr. des leistenden Unternehmers");

    // Nr. 3 and 4 - the date of issue and the number.
    if (!beleg.datum.Valid()) fehlt.push_back("Ausstellungsdatum");
    if (beleg.nummer.empty()) fehlt.push_back("fortlaufende Rechnungsnummer");

    // Nr. 5 - what was supplied.
    if (beleg.positionen.empty()) fehlt.push_back("Menge und Bezeichnung der Leistung");

    // Nr. 6 - when. The Belegdatum stands in only when it is also the day of
    // supply, and an invoice may not simply leave this out.
    if (!beleg.leistungVon.Valid() && !beleg.datum.Valid())
        fehlt.push_back("Zeitpunkt der Lieferung oder sonstigen Leistung");

    // Nr. 7 and 8 - the base per rate and the rate or the exemption. A line
    // without a tax key can be neither.
    for (const BelegPosition& pos : beleg.positionen) {
        if (pos.steuerschluessel.empty()) {
            fehlt.push_back("Steuersatz oder Hinweis auf die Steuerbefreiung "
                            "(Position \"" + pos.bezeichnung + "\")");
            break;
        }
    }

    // An intra-community supply is zero-rated only against a confirmed VAT
    // number, and the recipient's number has to be on the invoice.
    if (empfaenger.steuerkategorie == Steuerkategorie::EuUnternehmer &&
        empfaenger.ustIdNr.empty())
        fehlt.push_back("USt-IdNr. des Leistungsempfängers "
                        "(innergemeinschaftliche Lieferung)");

    return fehlt;
}

// ===== THE PAGE ITSELF =====

RechnungPdfErgebnis SchreibeRechnungPdf(const Mandant& mandant, const Beleg& beleg,
                                        const Partner& empfaenger,
                                        const std::vector<Steuerschluessel>& schluessel,
                                        const std::string& dateiPfad,
                                        const RechnungLayout& layout) {
    RechnungPdfErgebnis ergebnis;
    if (dateiPfad.empty()) {
        ergebnis.fehler = "Es wurde kein Dateiname für die PDF-Datei angegeben.";
        return ergebnis;
    }
    if (beleg.positionen.empty()) {
        ergebnis.fehler = "Ein Beleg ohne Positionen kann nicht gedruckt werden.";
        return ergebnis;
    }

    ergebnis.fehlendePflichtangaben = PruefePflichtangaben(mandant, beleg, empfaenger);

    // **A contradictory invoice is not written.** Unlike a missing address,
    // which leaves a draft incomplete but harmless, a line that charges tax and
    // also declares the customer liable for it is a false statement in the one
    // document the customer books from. Printing it and adding a warning to a
    // console nobody reads is how it reaches them anyway.
    ergebnis.steuerBefunde = PruefeSteuerlicheStimmigkeit(beleg, empfaenger, schluessel);
    if (HatBlockierendenBefund(ergebnis.steuerBefunde)) {
        ergebnis.fehler = "Die Umsatzsteuer des Belegs ist nicht stimmig, die Rechnung "
                          "wird deshalb nicht geschrieben:";
        for (const SteuerBefund& b : ergebnis.steuerBefunde) {
            if (!b.blockierend) continue;
            ergebnis.fehler += "\n  - ";
            if (!b.position.empty()) ergebnis.fehler += "Position \"" + b.position + "\": ";
            ergebnis.fehler += b.text;
        }
        return ergebnis;
    }

    VectorDocument doc;
    doc.Title  = Ueberschrift(beleg) + " " + beleg.nummer;
    doc.Author = mandant.name;
    Seite seite(doc, layout);

    const double links  = seite.Links();
    const double rechts = seite.Rechts();

    // ---- the sender's own block, top right ----
    double y = layout.randOben + 12.0;
    seite.TextRechts(rechts, y, mandant.name, 12.0, true);
    y += 14.0;
    const std::string anschrift[] = {
        mandant.strasse, OrtZeile(mandant.plz, mandant.ort),
        mandant.telefon.empty() ? std::string() : "Tel. " + mandant.telefon,
        mandant.email, mandant.webseite
    };
    for (const std::string& zeile : anschrift) {
        if (zeile.empty()) continue;
        seite.TextRechts(rechts, y, zeile, 8.5);
        y += 10.5;
    }

    // ---- the address field ----
    // The small sender line above the window address, as DIN 5008 has it, so
    // the letter works in a window envelope.
    const double adresseOben = 150.0;
    std::string absender = mandant.name;
    if (!mandant.strasse.empty()) absender += " · " + mandant.strasse;
    if (!mandant.ort.empty())     absender += " · " + OrtZeile(mandant.plz, mandant.ort);
    seite.Text(links, adresseOben, absender, 6.5, false, Color(90, 90, 90));
    seite.Linie(links, adresseOben + 2.0, links + 230.0, adresseOben + 2.0, 0.4,
                Color(150, 150, 150));

    double ya = adresseOben + 18.0;
    const std::string empfaengerZeilen[] = {
        empfaenger.name, empfaenger.name2, empfaenger.kontaktperson,
        empfaenger.strasse, OrtZeile(empfaenger.plz, empfaenger.ort),
        (empfaenger.land == "DE" || empfaenger.land.empty()) ? std::string()
                                                             : empfaenger.land
    };
    for (const std::string& zeile : empfaengerZeilen) {
        if (zeile.empty()) continue;
        seite.Text(links, ya, zeile, 10.5);
        ya += 13.0;
    }

    // ---- the meta block, right of the address ----
    double ym = adresseOben + 18.0;
    const double labelX = 360.0;
    auto meta = [&](const std::string& label, const std::string& wert) {
        if (wert.empty()) return;
        seite.Text(labelX, ym, label, 9.0, false, Color(70, 70, 70));
        // A long value would otherwise be right-aligned straight through its
        // own label. When the two do not both fit, the value takes the next
        // line - which is what the collision looked like before this existed.
        const double platz = rechts - labelX - TextBreite(label, 9.0, false) - 10.0;
        if (TextBreite(wert, 9.0, false) > platz) ym += 11.0;
        seite.TextRechts(rechts, ym, wert, 9.0);
        ym += 12.0;
    };
    meta("Rechnungsnummer", beleg.nummer);
    meta("Rechnungsdatum", FormatDateGerman(beleg.datum));
    if (beleg.leistungVon.Valid()) {
        if (beleg.leistungBis.Valid() && beleg.leistungBis != beleg.leistungVon)
            meta("Leistungszeitraum", FormatDateGerman(beleg.leistungVon) + " - " +
                                      FormatDateGerman(beleg.leistungBis));
        else
            meta("Leistungsdatum", FormatDateGerman(beleg.leistungVon));
    }
    // When there is no separate service date, § 14 Abs. 4 Nr. 6 UStG is
    // answered by the standard sentence under the total rather than by a meta
    // row - see `hinweise` below. It is a sentence, not a field.
    meta("Kundennummer", empfaenger.konto);
    meta("Ihre USt-IdNr.", empfaenger.ustIdNr);
    if (!beleg.externeNummer.empty()) meta("Ihre Bestellnummer", beleg.externeNummer);
    if (beleg.faelligAm.Valid()) meta("Fällig am", FormatDateGerman(beleg.faelligAm));

    // ---- the heading ----
    double yb = (ya > ym ? ya : ym) + 26.0;
    if (yb < 300.0) yb = 300.0;
    seite.Text(links, yb, Ueberschrift(beleg) + " " + beleg.nummer, 15.0, true);
    yb += 20.0;

    // The state of the document, directly under its heading. This used to be
    // drawn across the middle of the page as a watermark and overprinted the
    // opening paragraph - a line of its own says the same thing and cannot
    // collide with anything.
    if (layout.entwurfKennzeichnen && beleg.status == BelegStatus::Entwurf) {
        seite.Text(links, yb, "ENTWURF - noch nicht gebucht", 11.0, true,
                   Color(170, 60, 60));
        yb += 17.0;
    } else if (beleg.status == BelegStatus::Storniert) {
        seite.Text(links, yb, "STORNIERT", 11.0, true, Color(170, 60, 60));
        yb += 17.0;
    }

    if (!beleg.buchungstext.empty()) {
        for (const std::string& zeile :
             Umbrechen(beleg.buchungstext, rechts - links, 10.0, false)) {
            seite.Text(links, yb, zeile, 10.0);
            yb += 13.0;
        }
        yb += 4.0;
    }

    // ---- the table ----
    const double spaltePos     = links;
    const double spalteText    = links + 22.0;
    const double spalteMenge   = 330.0;    // right edge
    const double spalteEinheit = 336.0;
    const double spaltePreis   = 430.0;    // right edge
    const double spalteSatz    = 470.0;    // right edge
    const double spalteGesamt  = rechts;   // right edge
    const double textBreite    = spalteMenge - spalteText - 8.0;

    yb += 8.0;
    seite.Linie(links, yb - 10.0, rechts, yb - 10.0, 0.8);
    seite.Text(spaltePos, yb, "Pos", 8.5, true);
    seite.Text(spalteText, yb, "Bezeichnung", 8.5, true);
    seite.TextRechts(spalteMenge, yb, "Menge", 8.5, true);
    seite.Text(spalteEinheit, yb, "Einheit", 8.5, true);
    seite.TextRechts(spaltePreis, yb, "Einzelpreis", 8.5, true);
    seite.TextRechts(spalteSatz, yb, "USt", 8.5, true);
    seite.TextRechts(spalteGesamt, yb, "Betrag netto", 8.5, true);
    yb += 5.0;
    seite.Linie(links, yb, rechts, yb, 0.8);
    yb += 14.0;

    // How much room the totals, the notes and the footer need below the table.
    // Checked before anything is drawn, because an invoice whose last position
    // fell off the page is worse than one that was not produced.
    const double platzUnten  = 150.0;
    const double letzteZeile = seite.Unten() - platzUnten;

    for (const BelegPosition& pos : beleg.positionen) {
        const std::vector<std::string> zeilen =
            Umbrechen(pos.bezeichnung, textBreite, 9.5, false);
        const double hoehe = 13.0 * (zeilen.empty() ? 1.0
                                                    : static_cast<double>(zeilen.size()));
        if (yb + hoehe > letzteZeile) {
            // The framework's PDF writer emits exactly one page
            // (proposal §3.2), so there is nowhere to continue. Refusing is the
            // only honest answer; silently dropping positions from an invoice
            // is not an option, and shrinking the type until it fits produces a
            // document nobody can read.
            ergebnis.fehler =
                "Der Beleg " + beleg.nummer + " hat mit " +
                Zahl(static_cast<int64_t>(beleg.positionen.size())) +
                " Positionen mehr Zeilen, als auf eine Seite passen. Der "
                "PDF-Writer des Frameworks schreibt derzeit nur einseitige "
                "Dokumente; bis das mehrseitige Schreiben da ist, bitte den "
                "Beleg aufteilen.";
            return ergebnis;
        }

        seite.Text(spaltePos, yb, Zahl(pos.position), 9.5);
        for (size_t z = 0; z < zeilen.size(); ++z)
            seite.Text(spalteText, yb + 13.0 * static_cast<double>(z), zeilen[z], 9.5);
        seite.TextRechts(spalteMenge, yb, Menge(pos.mengeTausendstel), 9.5);
        seite.Text(spalteEinheit, yb, pos.einheit, 9.5);
        seite.TextRechts(spaltePreis, yb, pos.einzelpreis.ToString(), 9.5);
        seite.TextRechts(spalteSatz, yb, Satz(pos.satzPromille), 9.5);
        seite.TextRechts(spalteGesamt, yb, pos.netto.ToString(), 9.5);
        yb += hoehe;
    }

    // ---- the totals ----
    yb += 4.0;
    seite.Linie(spalteEinheit, yb, rechts, yb, 0.6);
    yb += 14.0;

    // One line per tax key, in the order the positions introduced them - which
    // is the order the journal posted them, so the invoice and the ledger read
    // the same way.
    std::vector<Steuerzeile> steuerzeilen;
    for (const BelegPosition& pos : beleg.positionen) {
        Steuerzeile* treffer = nullptr;
        for (Steuerzeile& z : steuerzeilen)
            if (z.schluessel == pos.steuerschluessel) { treffer = &z; break; }
        if (treffer == nullptr) {
            Steuerzeile neu;
            neu.schluessel   = pos.steuerschluessel;
            neu.satzPromille = pos.satzPromille;
            neu.bemessung    = Money::Zero(beleg.waehrung);
            neu.steuer       = Money::Zero(beleg.waehrung);
            steuerzeilen.push_back(neu);
            treffer = &steuerzeilen.back();
        }
        treffer->bemessung = treffer->bemessung + pos.netto;
        treffer->steuer    = treffer->steuer + pos.steuer;
    }

    seite.Text(spalteEinheit, yb, "Zwischensumme netto", 9.5);
    seite.TextRechts(spalteGesamt, yb, beleg.netto.ToString(), 9.5);
    yb += 13.0;

    for (const Steuerzeile& z : steuerzeilen) {
        const std::string label = "zzgl. " + Satz(z.satzPromille) + " USt auf " +
                                  z.bemessung.ToString();
        seite.Text(spalteEinheit, yb, label, 9.5);
        seite.TextRechts(spalteGesamt, yb, z.steuer.ToString(), 9.5);
        yb += 13.0;
    }

    yb += 2.0;
    seite.Linie(spalteEinheit, yb, rechts, yb, 0.8);
    yb += 14.0;
    seite.Text(spalteEinheit, yb, "Gesamtbetrag", 11.0, true);
    // The symbol rather than the ISO code, because that is how a German
    // invoice reads. It is also the one character on this page that WinAnsi
    // puts outside Latin-1 (0x80), which the PDF writer used to replace with
    // '?' - see the 1.1.0 entry for that file.
    seite.TextRechts(spalteGesamt, yb, beleg.brutto.ToStringWithSymbol(), 11.0, true);
    yb += 24.0;

    // ---- payment, and the notes the law wants ----
    std::string zahlung = layout.zahlungshinweis;
    if (zahlung.empty()) {
        if (beleg.faelligAm.Valid())
            zahlung = "Zahlbar ohne Abzug bis zum " +
                      FormatDateGerman(beleg.faelligAm) + ".";
        else
            zahlung = "Zahlbar sofort ohne Abzug.";
        if (empfaenger.skontoPromille > 0 && empfaenger.skontoTage > 0)
            zahlung += " Bei Zahlung innerhalb von " + Zahl(empfaenger.skontoTage) +
                       " Tagen " + Satz(empfaenger.skontoPromille) + " Skonto.";
    }
    seite.Text(links, yb, zahlung, 9.5);
    yb += 13.0;

    if (!mandant.iban.empty()) {
        std::string bank = "Bitte überweisen Sie auf " + mandant.iban;
        if (!mandant.bic.empty())  bank += " (BIC " + mandant.bic + ")";
        if (!mandant.bank.empty()) bank += ", " + mandant.bank;
        bank += " unter Angabe der Rechnungsnummer " + beleg.nummer + ".";
        for (const std::string& zeile : Umbrechen(bank, rechts - links, 9.5, false)) {
            seite.Text(links, yb, zeile, 9.5);
            yb += 12.0;
        }
    }
    yb += 6.0;

    // The exemption notes. One per distinct reason, not one per position.
    std::vector<std::string> hinweise;
    if (!beleg.leistungVon.Valid())
        hinweise.push_back("Das Leistungsdatum entspricht dem Rechnungsdatum.");
    if (mandant.kleinunternehmer)
        hinweise.push_back("Gemäß § 19 UStG wird keine Umsatzsteuer berechnet.");
    for (const Steuerzeile& z : steuerzeilen) {
        for (const Steuerschluessel& key : schluessel) {
            if (key.schluessel != z.schluessel) continue;
            const std::string hinweis = Befreiungshinweis(key);
            if (hinweis.empty()) break;
            bool schonDa = false;
            for (const std::string& h : hinweise)
                if (h == hinweis) { schonDa = true; break; }
            if (!schonDa) hinweise.push_back(hinweis);
            break;
        }
    }
    for (const std::string& hinweis : hinweise) {
        for (const std::string& zeile : Umbrechen(hinweis, rechts - links, 9.0, false)) {
            seite.Text(links, yb, zeile, 9.0);
            yb += 11.5;
        }
    }

    // ---- the footer ----
    const double fussOben = seite.Unten() - 34.0;
    seite.Linie(links, fussOben - 8.0, rechts, fussOben - 8.0, 0.4, Color(150, 150, 150));

    std::string fuss1 = mandant.name;
    if (!mandant.strasse.empty()) fuss1 += ", " + mandant.strasse;
    if (!mandant.ort.empty())     fuss1 += ", " + OrtZeile(mandant.plz, mandant.ort);

    std::string fuss2;
    if (!mandant.steuernummer.empty()) fuss2 += "Steuernummer " + mandant.steuernummer;
    if (!mandant.ustIdNr.empty()) {
        if (!fuss2.empty()) fuss2 += " · ";
        fuss2 += "USt-IdNr. " + mandant.ustIdNr;
    }
    if (!mandant.iban.empty()) {
        if (!fuss2.empty()) fuss2 += " · ";
        fuss2 += "IBAN " + mandant.iban;
    }

    seite.Text(links, fussOben, fuss1, 7.0, false, Color(90, 90, 90));
    seite.Text(links, fussOben + 9.5, fuss2, 7.0, false, Color(90, 90, 90));
    if (!layout.fusszeileZusatz.empty())
        seite.Text(links, fussOben + 19.0, layout.fusszeileZusatz, 7.0, false,
                   Color(90, 90, 90));

    // ---- hand it to the framework's writer ----
    VectorConverter::PDFVectorConverter konverter;
    VectorConverter::ConversionOptions optionen;
    if (!konverter.Export(doc, dateiPfad, optionen)) {
        ergebnis.fehler = "Die PDF-Datei \"" + dateiPfad + "\" konnte nicht "
                          "geschrieben werden.";
        return ergebnis;
    }

    ergebnis.ok = true;
    return ergebnis;
}

} // namespace UltraFIBU
