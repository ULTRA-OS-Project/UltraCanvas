// Apps/UltraFIBU/engine/UltraFIBUTypes.cpp
// Enum <-> text for every domain enum (the text form is what goes in the
// database, so it stays readable in a dump and portable between engines), the
// role/permission matrix, and document-number rendering.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUTypes.h"

namespace UltraFIBU {

namespace {

std::string Pad(int64_t value, int width) {
    std::string digits;
    int64_t v = value < 0 ? -value : value;
    if (v == 0) digits = "0";
    while (v > 0) {
        digits.insert(digits.begin(), static_cast<char>('0' + (v % 10)));
        v /= 10;
    }
    while (static_cast<int>(digits.size()) < width) digits.insert(digits.begin(), '0');
    return digits;
}

void ReplaceAll(std::string& text, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t at = 0;
    while ((at = text.find(from, at)) != std::string::npos) {
        text.replace(at, from.size(), to);
        at += to.size();
    }
}

} // namespace

std::string BesteuerungsartToText(Besteuerungsart art) {
    return art == Besteuerungsart::Istversteuerung ? "ist" : "soll";
}
bool BesteuerungsartFromText(const std::string& text, Besteuerungsart& out) {
    if (text == "soll") { out = Besteuerungsart::Sollversteuerung; return true; }
    if (text == "ist")  { out = Besteuerungsart::Istversteuerung;  return true; }
    return false;
}

std::string GewinnermittlungToText(Gewinnermittlung art) {
    return art == Gewinnermittlung::Bilanzierung ? "bilanz" : "euer";
}
bool GewinnermittlungFromText(const std::string& text, Gewinnermittlung& out) {
    if (text == "euer")   { out = Gewinnermittlung::EinnahmenUeberschuss; return true; }
    if (text == "bilanz") { out = Gewinnermittlung::Bilanzierung;         return true; }
    return false;
}

std::string KontoTypToText(KontoTyp typ) {
    switch (typ) {
        case KontoTyp::Aktiv:        return "aktiv";
        case KontoTyp::Passiv:       return "passiv";
        case KontoTyp::Eigenkapital: return "eigenkapital";
        case KontoTyp::Ertrag:       return "ertrag";
        case KontoTyp::Aufwand:      return "aufwand";
        case KontoTyp::Debitor:      return "debitor";
        case KontoTyp::Kreditor:     return "kreditor";
        case KontoTyp::Statistisch:  return "statistisch";
    }
    return "aufwand";
}
bool KontoTypFromText(const std::string& text, KontoTyp& out) {
    if (text == "aktiv")        { out = KontoTyp::Aktiv;        return true; }
    if (text == "passiv")       { out = KontoTyp::Passiv;       return true; }
    if (text == "eigenkapital") { out = KontoTyp::Eigenkapital; return true; }
    if (text == "ertrag")       { out = KontoTyp::Ertrag;       return true; }
    if (text == "aufwand")      { out = KontoTyp::Aufwand;      return true; }
    if (text == "debitor")      { out = KontoTyp::Debitor;      return true; }
    if (text == "kreditor")     { out = KontoTyp::Kreditor;     return true; }
    if (text == "statistisch")  { out = KontoTyp::Statistisch;  return true; }
    return false;
}

std::string LeistungszeitpunktToText(Leistungszeitpunkt art) {
    switch (art) {
        case Leistungszeitpunkt::Lieferdatum:       return "lieferdatum";
        case Leistungszeitpunkt::Leistungsdatum:    return "leistungsdatum";
        case Leistungszeitpunkt::Lieferzeitraum:    return "lieferzeitraum";
        case Leistungszeitpunkt::Leistungszeitraum: return "leistungszeitraum";
        case Leistungszeitpunkt::Keiner:            return "keiner";
    }
    return "leistungsdatum";
}

bool LeistungszeitpunktFromText(const std::string& text, Leistungszeitpunkt& out) {
    if (text == "lieferdatum")       { out = Leistungszeitpunkt::Lieferdatum;       return true; }
    if (text == "leistungsdatum")    { out = Leistungszeitpunkt::Leistungsdatum;    return true; }
    if (text == "lieferzeitraum")    { out = Leistungszeitpunkt::Lieferzeitraum;    return true; }
    if (text == "leistungszeitraum") { out = Leistungszeitpunkt::Leistungszeitraum; return true; }
    if (text == "keiner")            { out = Leistungszeitpunkt::Keiner;            return true; }
    return false;
}

std::string LeistungszeitpunktLabel(Leistungszeitpunkt art) {
    switch (art) {
        case Leistungszeitpunkt::Lieferdatum:       return "Lieferdatum";
        case Leistungszeitpunkt::Leistungsdatum:    return "Leistungsdatum";
        case Leistungszeitpunkt::Lieferzeitraum:    return "Lieferzeitraum";
        case Leistungszeitpunkt::Leistungszeitraum: return "Leistungszeitraum";
        case Leistungszeitpunkt::Keiner:            return "kein Liefer-/Leistungsdatum";
    }
    return "Leistungsdatum";
}

bool LeistungszeitpunktIstZeitraum(Leistungszeitpunkt art) {
    return art == Leistungszeitpunkt::Lieferzeitraum ||
           art == Leistungszeitpunkt::Leistungszeitraum;
}

// Which kinds of key mean "no German VAT on this invoice". OSS is deliberately
// NOT one of them: an OSS invoice does charge tax, the destination country's,
// and its note says exactly that. IgErwerb and Inland are not either - they are
// the ordinary taxed cases.
bool IstNullsatzImAusgang(SteuerArt art) {
    switch (art) {
        case SteuerArt::IgLieferung:
        case SteuerArt::EuSonstigeLeistung:
        case SteuerArt::Drittland:
        case SteuerArt::ReverseCharge13b:
        case SteuerArt::Kleinunternehmer:
        case SteuerArt::NichtSteuerbar:
            return true;
        case SteuerArt::Inland:
        case SteuerArt::IgErwerb:
        case SteuerArt::Oss:
            return false;
    }
    return false;
}

// The two cross-border B2B cases. For both, § 14a UStG requires the customer's
// USt-IdNr. on the invoice, and for both the number is what the exemption rests
// on rather than a formality.
bool BrauchtUstIdNrDesEmpfaengers(SteuerArt art) {
    return art == SteuerArt::IgLieferung || art == SteuerArt::EuSonstigeLeistung;
}

std::string SteuerArtToText(SteuerArt art) {
    switch (art) {
        case SteuerArt::Inland:           return "inland";
        case SteuerArt::IgLieferung:      return "ig-lieferung";
        case SteuerArt::IgErwerb:         return "ig-erwerb";
        case SteuerArt::Drittland:        return "drittland";
        case SteuerArt::EuSonstigeLeistung: return "eu-sonstige-leistung";
        case SteuerArt::ReverseCharge13b: return "reverse-charge-13b";
        case SteuerArt::Oss:              return "oss";
        case SteuerArt::Kleinunternehmer: return "kleinunternehmer";
        case SteuerArt::NichtSteuerbar:   return "nicht-steuerbar";
    }
    return "inland";
}
bool SteuerArtFromText(const std::string& text, SteuerArt& out) {
    if (text == "inland")             { out = SteuerArt::Inland;           return true; }
    if (text == "ig-lieferung")       { out = SteuerArt::IgLieferung;      return true; }
    if (text == "ig-erwerb")          { out = SteuerArt::IgErwerb;         return true; }
    if (text == "drittland")          { out = SteuerArt::Drittland;        return true; }
    if (text == "eu-sonstige-leistung") { out = SteuerArt::EuSonstigeLeistung; return true; }
    if (text == "reverse-charge-13b") { out = SteuerArt::ReverseCharge13b; return true; }
    if (text == "oss")                { out = SteuerArt::Oss;              return true; }
    if (text == "kleinunternehmer")   { out = SteuerArt::Kleinunternehmer; return true; }
    if (text == "nicht-steuerbar")    { out = SteuerArt::NichtSteuerbar;   return true; }
    return false;
}

std::string PartnerTypToText(PartnerTyp typ) {
    switch (typ) {
        case PartnerTyp::Kunde:     return "kunde";
        case PartnerTyp::Lieferant: return "lieferant";
        case PartnerTyp::Beides:    return "beides";
    }
    return "kunde";
}
bool PartnerTypFromText(const std::string& text, PartnerTyp& out) {
    if (text == "kunde")     { out = PartnerTyp::Kunde;     return true; }
    if (text == "lieferant") { out = PartnerTyp::Lieferant; return true; }
    if (text == "beides")    { out = PartnerTyp::Beides;    return true; }
    return false;
}

std::string SteuerkategorieToText(Steuerkategorie kategorie) {
    switch (kategorie) {
        case Steuerkategorie::Inland:        return "inland";
        case Steuerkategorie::EuUnternehmer: return "eu-unternehmer";
        case Steuerkategorie::EuPrivat:      return "eu-privat";
        case Steuerkategorie::Drittland:     return "drittland";
    }
    return "inland";
}
bool SteuerkategorieFromText(const std::string& text, Steuerkategorie& out) {
    if (text == "inland")         { out = Steuerkategorie::Inland;        return true; }
    if (text == "eu-unternehmer") { out = Steuerkategorie::EuUnternehmer; return true; }
    if (text == "eu-privat")      { out = Steuerkategorie::EuPrivat;      return true; }
    if (text == "drittland")      { out = Steuerkategorie::Drittland;     return true; }
    return false;
}

std::string BenutzerRolleToText(BenutzerRolle rolle) {
    switch (rolle) {
        case BenutzerRolle::Administrator: return "administrator";
        case BenutzerRolle::Buchhalter:    return "buchhalter";
        case BenutzerRolle::Erfasser:      return "erfasser";
        case BenutzerRolle::Steuerberater: return "steuerberater";
        case BenutzerRolle::NurLesen:      return "nur-lesen";
    }
    return "nur-lesen";
}
bool BenutzerRolleFromText(const std::string& text, BenutzerRolle& out) {
    if (text == "administrator") { out = BenutzerRolle::Administrator; return true; }
    if (text == "buchhalter")    { out = BenutzerRolle::Buchhalter;    return true; }
    if (text == "erfasser")      { out = BenutzerRolle::Erfasser;      return true; }
    if (text == "steuerberater") { out = BenutzerRolle::Steuerberater; return true; }
    if (text == "nur-lesen")     { out = BenutzerRolle::NurLesen;      return true; }
    return false;
}
std::string BenutzerRolleLabel(BenutzerRolle rolle) {
    switch (rolle) {
        case BenutzerRolle::Administrator: return "Administrator";
        case BenutzerRolle::Buchhalter:    return "Buchhalter";
        case BenutzerRolle::Erfasser:      return "Erfasser";
        case BenutzerRolle::Steuerberater: return "Steuerberater";
        case BenutzerRolle::NurLesen:      return "Nur Lesen";
    }
    return "Nur Lesen";
}

// The permission matrix, written out rather than derived from an ordering:
// Steuerberater reads everything and exports but writes nothing, which no
// simple "level" of access expresses.
bool RolleHatRecht(BenutzerRolle rolle, Recht recht) {
    switch (rolle) {
        case BenutzerRolle::Administrator:
            return true;
        case BenutzerRolle::Buchhalter:
            switch (recht) {
                case Recht::StammdatenLesen:
                case Recht::StammdatenSchreiben:
                case Recht::BelegErfassen:
                case Recht::Buchen:
                case Recht::Festschreiben:
                case Recht::SteuerMelden:
                case Recht::DatevExportieren: return true;
                case Recht::BenutzerVerwalten: return false;
            }
            return false;
        case BenutzerRolle::Erfasser:
            switch (recht) {
                case Recht::StammdatenLesen:
                case Recht::StammdatenSchreiben:
                case Recht::BelegErfassen: return true;
                default: return false;
            }
        case BenutzerRolle::Steuerberater:
            return recht == Recht::StammdatenLesen || recht == Recht::DatevExportieren;
        case BenutzerRolle::NurLesen:
            return recht == Recht::StammdatenLesen;
    }
    return false;
}

std::string FormatNummer(const Nummernkreis& kreis, int64_t wert, const Date& datum) {
    std::string praefix = kreis.praefix;
    if (datum.Valid()) {
        ReplaceAll(praefix, "{JJJJ}", Pad(datum.year, 4));
        ReplaceAll(praefix, "{JJ}",   Pad(datum.year % 100, 2));
        ReplaceAll(praefix, "{MM}",   Pad(datum.month, 2));
    }
    return praefix + Pad(wert, kreis.stellen);
}

} // namespace UltraFIBU
