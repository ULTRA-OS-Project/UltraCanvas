// Apps/UltraFIBU/engine/UltraFIBUOss.cpp
// One-Stop-Shop. See the header for why the shipped rates are a check rather
// than a source, and why a posting with no destination country stops the
// return.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUOss.h"

#include "UltraFIBUKontenrahmen.h"

#include <UltraCrypt/UltraCryptCore.h>

#include <algorithm>
#include <cstdio>

namespace UltraFIBU {

namespace {

std::string Zahl(int64_t wert) { return std::to_string(wert); }

std::string Trimme(const std::string& text) {
    size_t a = 0, b = text.size();
    auto leer = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (a < b && leer(text[a])) ++a;
    while (b > a && leer(text[b - 1])) --b;
    return text.substr(a, b - a);
}

std::vector<std::string> Teile(const std::string& zeile, char trenner) {
    std::vector<std::string> felder;
    std::string aktuell;
    for (char c : zeile) {
        if (c == trenner) { felder.push_back(Trimme(aktuell)); aktuell.clear(); }
        else aktuell.push_back(c);
    }
    felder.push_back(Trimme(aktuell));
    return felder;
}

// Same rule as everywhere else in the engine: an ordinary sale posts with the
// person account debited, a Storno on the other side, and the other side
// subtracts. A reversed OSS sale that added would declare VAT to another state
// twice.
SollHaben NormaleSeite(const Steuerschluessel& key) {
    return key.vorsteuer ? SollHaben::Haben : SollHaben::Soll;
}

std::string SatzText(int promille) {
    char puffer[32];
    if (promille % 10 == 0)
        std::snprintf(puffer, sizeof(puffer), "%d", promille / 10);
    else
        std::snprintf(puffer, sizeof(puffer), "%d,%d", promille / 10, promille % 10);
    return puffer;
}

std::string Fingerabdruck(const std::string& roh) {
    std::vector<uint8_t> digest;
    if (UltraCrypt_Hash(UltraCryptHashAlgorithm::SHA256, roh.data(), roh.size(), digest))
        return UltraCrypt_ToHex(digest);
    return std::string();
}

// A decimal for a transport file: dot separator, two places, never the
// process locale's comma.
std::string DateiBetrag(const Money& betrag) {
    const int64_t minor = betrag.Minor();
    const int64_t abs = minor < 0 ? -minor : minor;
    char puffer[64];
    std::snprintf(puffer, sizeof(puffer), "%s%lld.%02lld", minor < 0 ? "-" : "",
                  static_cast<long long>(abs / 100),
                  static_cast<long long>(abs % 100));
    return puffer;
}

} // namespace

std::string OssVerfahrenToText(OssVerfahren v) {
    return v == OssVerfahren::Ioss ? "ioss" : "oss";
}

bool OssVerfahrenFromText(const std::string& text, OssVerfahren& out) {
    if (text == "oss"  || text == "OSS")  { out = OssVerfahren::Oss;  return true; }
    if (text == "ioss" || text == "IOSS") { out = OssVerfahren::Ioss; return true; }
    return false;
}

// ===== THE MEMBER STATES' RATES =====

bool EuSteuersatz::GueltigAm(const Date& datum) const {
    if (!datum.Valid()) return false;
    if (gueltigVon.Valid() && datum < gueltigVon) return false;
    if (gueltigBis.Valid() && gueltigBis < datum) return false;
    return true;
}

bool EuSteuersaetze::Laden(const std::string& dateipfad, std::string& fehler) {
    saetze_.clear();
    std::FILE* datei = std::fopen(dateipfad.c_str(), "rb");
    if (datei == nullptr) {
        fehler = "Die Datei \"" + dateipfad + "\" ist nicht lesbar.";
        return false;
    }
    std::string inhalt;
    char puffer[8192];
    size_t n = 0;
    while ((n = std::fread(puffer, 1, sizeof(puffer), datei)) > 0) inhalt.append(puffer, n);
    std::fclose(datei);

    size_t start = 0;
    bool kopfGesehen = false;
    while (start <= inhalt.size()) {
        size_t ende = inhalt.find('\n', start);
        if (ende == std::string::npos) ende = inhalt.size();
        std::string zeile = inhalt.substr(start, ende - start);
        if (!zeile.empty() && zeile.back() == '\r') zeile.pop_back();
        start = (ende == inhalt.size()) ? inhalt.size() + 1 : ende + 1;

        const std::string z = Trimme(zeile);
        if (z.empty() || z[0] == '#') continue;
        if (!kopfGesehen && z.rfind("land;", 0) == 0) { kopfGesehen = true; continue; }

        const std::vector<std::string> felder = Teile(z, ';');
        if (felder.size() < 6) continue;
        EuSteuersatz satz;
        satz.land         = felder[0];
        satz.art          = felder[1];
        satz.satzPromille = std::atoi(felder[2].c_str());
        Date::TryParseIso(felder[3], satz.gueltigVon);
        Date::TryParseIso(felder[4], satz.gueltigBis);
        satz.geprueft     = (felder[5] == "ja");
        if (felder.size() > 6) satz.quelle = felder[6];
        if (satz.land.empty()) continue;
        saetze_.push_back(std::move(satz));
    }
    if (saetze_.empty()) {
        fehler = "Die Datei enthält keinen Steuersatz.";
        return false;
    }
    return true;
}

bool EuSteuersaetze::Standardsatz(const std::string& land, const Date& datum,
                                  int& outPromille) const {
    const EuSteuersatz* beste = nullptr;
    for (const EuSteuersatz& satz : saetze_) {
        if (satz.land != land) continue;
        if (satz.art != "standard") continue;
        // **Unverified rates are not used.** Reporting a correct invoice as
        // wrong because of a guessed rate would train the user to ignore the
        // warning, and then it is worth nothing on the day it is right.
        if (!satz.geprueft) continue;
        if (!satz.GueltigAm(datum)) continue;
        // The newest rule that covers the date. Closing a rate's predecessor is
        // the editor's job and it does it, but a hand-edited file or an import
        // can still leave two rows overlapping, and then "the first one in the
        // list" is not an answer - it depends on the order rows were read.
        if (beste == nullptr || beste->gueltigVon < satz.gueltigVon) beste = &satz;
    }
    if (beste == nullptr) return false;
    outPromille = beste->satzPromille;
    return true;
}

bool EuSteuersaetze::Kennt(const std::string& land) const {
    for (const EuSteuersatz& satz : saetze_)
        if (satz.land == land) return true;
    return false;
}

std::string EuSteuersaetzePfad() { return FindeDatenDatei("EU-Steuersaetze.csv"); }

// ===== PERIODS =====

bool OssZeitraumGrenzen(OssVerfahren verfahren, int jahr, const std::string& zeitraum,
                        Date& outVon, Date& outBis) {
    if (verfahren == OssVerfahren::Ioss) {
        if (zeitraum.size() != 2) return false;
        const int monat = std::atoi(zeitraum.c_str());
        if (monat < 1 || monat > 12) return false;
        outVon = Date(jahr, monat, 1);
        outBis = Date(jahr, monat, Date::DaysInMonth(jahr, monat));
        return outVon.Valid() && outBis.Valid();
    }
    // OSS is quarterly, written the way the portal writes it.
    if (zeitraum.size() != 2 || (zeitraum[0] != 'Q' && zeitraum[0] != 'q')) return false;
    const int quartal = zeitraum[1] - '0';
    if (quartal < 1 || quartal > 4) return false;
    const int ersterMonat = (quartal - 1) * 3 + 1;
    outVon = Date(jahr, ersterMonat, 1);
    const int letzterMonat = ersterMonat + 2;
    outBis = Date(jahr, letzterMonat, Date::DaysInMonth(jahr, letzterMonat));
    return outVon.Valid() && outBis.Valid();
}

// ===== THE RETURN =====

Money OssBerechnung::SteuerFuer(const std::string& land) const {
    int64_t summe = 0;
    for (const OssPosten& p : posten)
        if (p.land == land) summe += p.steuer.Minor();
    return Money::FromMinor(summe, "EUR");
}

OssBerechnung BerechneOss(OssVerfahren verfahren, int64_t mandantId, int jahr,
                          const std::string& zeitraum, const Date& von, const Date& bis,
                          const std::vector<Buchung>& journal,
                          const std::vector<Steuerschluessel>& steuerschluessel,
                          const EuSteuersaetze& saetze) {
    OssBerechnung ergebnis;
    ergebnis.verfahren = verfahren;
    ergebnis.mandantId = mandantId;
    ergebnis.jahr      = jahr;
    ergebnis.zeitraum  = zeitraum;
    ergebnis.von       = von;
    ergebnis.bis       = bis;
    ergebnis.summeBemessung = Money::Zero("EUR");
    ergebnis.summeSteuer    = Money::Zero("EUR");

    if (!von.Valid() || !bis.Valid() || bis < von) {
        ergebnis.fehler = "Der Zeitraum der Meldung ist ungültig.";
        return ergebnis;
    }

    auto findeKey = [&](const std::string& name, const Date& am,
                        Steuerschluessel& out) -> bool {
        for (const Steuerschluessel& k : steuerschluessel) {
            if (k.schluessel != name) continue;
            if (!k.GueltigAm(am)) continue;
            out = k;
            return true;
        }
        return false;
    };

    std::map<std::string, OssLuecke> luecken;
    // Keyed by country and rate, which is how the return is laid out.
    std::map<std::string, OssPosten> posten;

    for (const Buchung& b : journal) {
        if (b.mandantId != 0 && mandantId != 0 && b.mandantId != mandantId) continue;
        if (!b.belegdatum.Valid()) continue;
        if (b.belegdatum < von || bis < b.belegdatum) continue;
        if (b.steuerschluessel.empty()) continue;

        Steuerschluessel key;
        if (!findeKey(b.steuerschluessel, b.belegdatum, key)) continue;
        if (key.art != SteuerArt::Oss) continue;

        const int vorzeichen = (b.sollHaben == NormaleSeite(key)) ? 1 : -1;
        const Money netto  = Money::FromMinor(vorzeichen * b.netto.Minor(), "EUR");
        const Money steuer = Money::FromMinor(vorzeichen * b.steuer.Minor(), "EUR");

        // **Without a destination country there is no return.** "VAT owed
        // somewhere in the EU" cannot be filed, and picking a country would
        // send another state's money to the wrong place.
        if (key.land.empty()) {
            OssLuecke& l = luecken[key.schluessel];
            if (l.steuerschluessel.empty()) {
                l.steuerschluessel = key.schluessel;
                l.bemessung = Money::Zero("EUR");
                l.steuer    = Money::Zero("EUR");
                l.grund = "Der Steuerschlüssel \"" + key.schluessel + "\" nennt kein "
                          "Zielland (Spalte land in data/Steuerschluessel.csv). Für "
                          "OSS braucht jedes Zielland einen eigenen Schlüssel, z. B. "
                          "OSS-AT-20 mit land=AT und satz_promille=200.";
            }
            l.bemessung = l.bemessung + netto;
            l.steuer    = l.steuer + steuer;
            ++l.buchungen;
            continue;
        }

        const std::string schluessel = key.land + "|" + Zahl(key.satzPromille);
        OssPosten& p = posten[schluessel];
        if (p.land.empty()) {
            p.land         = key.land;
            p.satzPromille = key.satzPromille;
            p.bemessung    = Money::Zero("EUR");
            p.steuer       = Money::Zero("EUR");

            // The rate check: what was charged against what the country levies.
            int amtlich = 0;
            if (saetze.Standardsatz(key.land, b.belegdatum, amtlich)) {
                p.satzGeprueft = true;
                if (amtlich != key.satzPromille) {
                    p.satzHinweis =
                        "Berechnet wurden " + SatzText(key.satzPromille) + " %, " +
                        key.land + " erhebt laut data/EU-Steuersaetze.csv aber " +
                        SatzText(amtlich) + " %. Gemeldet wird, was berechnet wurde - "
                        "die Rechnung selbst ist zu prüfen.";
                }
            } else {
                p.satzHinweis =
                    "Für " + key.land + " ist kein geprüfter Steuersatz hinterlegt, "
                    "der berechnete Satz konnte also nicht bestätigt werden.";
            }
        }
        p.bemessung = p.bemessung + netto;
        p.steuer    = p.steuer + steuer;
        ++p.buchungen;
        if (b.id != 0) p.buchungIds.push_back(b.id);
    }

    for (auto& eintrag : posten) {
        ergebnis.summeBemessung = ergebnis.summeBemessung + eintrag.second.bemessung;
        ergebnis.summeSteuer    = ergebnis.summeSteuer + eintrag.second.steuer;
        ergebnis.posten.push_back(std::move(eintrag.second));
    }
    std::sort(ergebnis.posten.begin(), ergebnis.posten.end(),
              [](const OssPosten& a, const OssPosten& b) {
                  if (a.land != b.land) return a.land < b.land;
                  return a.satzPromille < b.satzPromille;
              });
    for (const auto& eintrag : luecken) ergebnis.luecken.push_back(eintrag.second);

    for (const OssPosten& p : ergebnis.posten) {
        if (!p.satzHinweis.empty())
            ergebnis.warnungen.push_back(p.land + ": " + p.satzHinweis);
    }
    if (!ergebnis.luecken.empty()) {
        Money fehlend = Money::Zero("EUR");
        for (const OssLuecke& l : ergebnis.luecken) fehlend = fehlend + l.steuer;
        ergebnis.warnungen.push_back(
            "ACHTUNG: " + fehlend.ToString() + " Steuer aus " +
            Zahl(static_cast<int64_t>(ergebnis.luecken.size())) +
            " Steuerschlüssel(n) hat kein Zielland und kann nicht gemeldet werden. "
            "Das ist Steuer, die einem anderen Mitgliedstaat zusteht.");
    }

    ergebnis.ok = true;
    return ergebnis;
}

// ===== THE § 3c THRESHOLD =====

SchwellenStand PruefeLieferschwelle(int jahr, const std::vector<Buchung>& journal,
                                    const std::vector<Steuerschluessel>& steuerschluessel,
                                    int64_t schwelleMinor) {
    SchwellenStand stand;
    stand.schwelle = Money::FromMinor(schwelleMinor, "EUR");
    stand.summe    = Money::Zero("EUR");

    auto findeKey = [&](const std::string& name, const Date& am,
                        Steuerschluessel& out) -> bool {
        for (const Steuerschluessel& k : steuerschluessel) {
            if (k.schluessel != name || !k.GueltigAm(am)) continue;
            out = k;
            return true;
        }
        return false;
    };

    // In document-date order, because the question is *when* it was crossed -
    // the invoice that crosses it is the first one that must carry the
    // destination country's rate.
    std::vector<const Buchung*> imJahr;
    for (const Buchung& b : journal) {
        if (!b.belegdatum.Valid() || b.belegdatum.year != jahr) continue;
        if (b.steuerschluessel.empty()) continue;
        imJahr.push_back(&b);
    }
    std::sort(imJahr.begin(), imJahr.end(),
              [](const Buchung* a, const Buchung* b) {
                  if (a->belegdatum != b->belegdatum) return a->belegdatum < b->belegdatum;
                  return a->id < b->id;
              });

    int64_t summe = 0;
    for (const Buchung* b : imJahr) {
        Steuerschluessel key;
        if (!findeKey(b->steuerschluessel, b->belegdatum, key)) continue;
        // What counts towards the threshold: cross-border B2C supplies. An OSS
        // key is one by definition; so is a domestic key used for a consumer
        // in another member state, but that case is not distinguishable here
        // without the customer, so only OSS turnover is counted and the
        // caveat is stated rather than hidden.
        if (key.art != SteuerArt::Oss) continue;

        const int vorzeichen = (b->sollHaben == NormaleSeite(key)) ? 1 : -1;
        summe += vorzeichen * b->netto.Minor();
        if (!stand.ueberschritten && summe > schwelleMinor) {
            stand.ueberschritten   = true;
            stand.ueberschrittenAm = b->belegdatum;
        }
    }
    stand.summe = Money::FromMinor(summe, "EUR");

    if (stand.ueberschritten) {
        stand.hinweise.push_back(
            "Die Lieferschwelle von " + stand.schwelle.ToString() + " wurde am " +
            FormatDateGerman(stand.ueberschrittenAm) + " überschritten. Ab dieser "
            "Rechnung ist im Zielland zu versteuern.");
    } else if (summe * 10 >= schwelleMinor * 8) {
        // Within 20 % of the threshold. Warning here rather than at the year
        // end is the entire value of the check: crossing it unnoticed means
        // every later invoice carries the wrong VAT.
        stand.nahe = true;
        stand.hinweise.push_back(
            "Die Lieferschwelle von " + stand.schwelle.ToString() + " ist zu " +
            Zahl(schwelleMinor == 0 ? 0 : (summe * 100) / schwelleMinor) +
            " % ausgeschöpft (" + stand.summe.ToString() + "). Wird sie "
            "überschritten, ist ab der überschreitenden Rechnung im Zielland zu "
            "versteuern - und zwar sofort, nicht ab dem nächsten Quartal.");
    }
    stand.hinweise.push_back(
        "Gezählt wird der Umsatz auf OSS-Steuerschlüsseln. Verkäufe an EU-Privat"
        "kunden, die noch mit einem Inlandsschlüssel gebucht sind, zählen für die "
        "Schwelle mit, erscheinen hier aber nicht.");
    return stand;
}

// ===== RECONCILIATION =====

OssUstvaAbgleich PruefeGegenUstva(const OssBerechnung& oss,
                                  const UstvaBerechnung& ustva) {
    OssUstvaAbgleich abgleich;
    abgleich.ossBemessung = oss.summeBemessung;
    abgleich.ustvaKz45    = ustva.Betrag("45");
    abgleich.differenz =
        Money::FromMinor(abgleich.ossBemessung.Minor() - abgleich.ustvaKz45.Minor(),
                         "EUR");
    abgleich.stimmt = abgleich.differenz.Minor() == 0;
    if (abgleich.stimmt) {
        abgleich.hinweis = "OSS-Bemessungsgrundlage und UStVA-Kennzahl 45 stimmen "
                           "überein.";
    } else {
        abgleich.hinweis =
            "OSS meldet " + abgleich.ossBemessung.ToString() +
            ", die UStVA weist in Kennzahl 45 aber " + abgleich.ustvaKz45.ToString() +
            " aus - Differenz " + abgleich.differenz.ToString() + ". Beide kommen "
            "aus demselben Journal, also ist eine der beiden Meldungen falsch. Das "
            "vor der Abgabe klären, nicht danach. Hinweis: stimmen die Zeiträume "
            "überein? Ein OSS-Quartal umfasst drei UStVA-Monate.";
    }
    return abgleich;
}

// ===== THE TRANSPORT FILE =====

OssDateiErgebnis SchreibeBopDatei(const OssBerechnung& berechnung,
                                  const std::string& ustIdNr,
                                  const std::string& zielVerzeichnis) {
    OssDateiErgebnis ergebnis;
    if (!berechnung.ok) {
        ergebnis.fehler = "Die Berechnung ist fehlgeschlagen: " + berechnung.fehler;
        return ergebnis;
    }
    if (!berechnung.Vollstaendig()) {
        std::string namen;
        for (const OssLuecke& l : berechnung.luecken) {
            if (!namen.empty()) namen += ", ";
            namen += l.steuerschluessel;
        }
        ergebnis.fehler =
            "Die Meldung ist unvollständig und wird nicht geschrieben. Ohne "
            "Zielland sind: " + namen + ". Was hier fehlt, ist Steuer, die einem "
            "anderen Mitgliedstaat zusteht und nicht gemeldet würde.";
        return ergebnis;
    }
    if (ustIdNr.empty()) {
        ergebnis.fehler = "Ohne eigene USt-IdNr. nimmt das BZSt die Meldung nicht an.";
        return ergebnis;
    }
    if (berechnung.posten.empty()) {
        ergebnis.fehler = "Im Zeitraum gibt es keinen OSS-Umsatz zu melden.";
        return ergebnis;
    }

    std::string csv;
    // The layout is a reconstruction: the BZSt publishes the import function
    // but not its specification. Saying so inside the file is the difference
    // between a draft somebody checks and a draft somebody uploads.
    csv += "# UltraFIBU - " + std::string(berechnung.verfahren == OssVerfahren::Ioss
                                              ? "IOSS" : "OSS") +
           "-Meldung " + Zahl(berechnung.jahr) + "/" + berechnung.zeitraum + "\r\n";
    csv += "# ACHTUNG: Der Spaltenaufbau dieser Datei ist NICHT an einem echten\r\n"
           "# BOP-Export geprueft. Das BZSt veroeffentlicht die Importfunktion,\r\n"
           "# aber nicht ihre Spezifikation. Die ZAHLEN stammen unmittelbar aus\r\n"
           "# dem Journal; zu pruefen ist die Anordnung, an einer echten Datei.\r\n";
    csv += "Land;Steuersatz;Steuersatzart;Bemessungsgrundlage;Steuerbetrag\r\n";
    for (const OssPosten& p : berechnung.posten) {
        csv += p.land + ";";
        csv += SatzText(p.satzPromille) + ";";
        csv += "STANDARD;";
        csv += DateiBetrag(p.bemessung) + ";";
        csv += DateiBetrag(p.steuer) + "\r\n";
    }

    const std::string dateiname =
        std::string(berechnung.verfahren == OssVerfahren::Ioss ? "IOSS" : "OSS") +
        "_" + Zahl(berechnung.jahr) + "_" + berechnung.zeitraum + ".csv";
    const std::string pfad =
        zielVerzeichnis.empty() ? dateiname : zielVerzeichnis + "/" + dateiname;
    std::FILE* datei = std::fopen(pfad.c_str(), "wb");
    if (datei == nullptr) {
        ergebnis.fehler = "Die Datei \"" + pfad + "\" ist nicht schreibbar.";
        return ergebnis;
    }
    std::fwrite(csv.data(), 1, csv.size(), datei);
    std::fclose(datei);

    ergebnis.ok    = true;
    ergebnis.datei = pfad;
    ergebnis.hash  = Fingerabdruck(csv);
    ergebnis.warnungen.push_back(
        "Der Spaltenaufbau ist noch nicht an einem echten BOP-Export geprüft. "
        "Vor der ersten Abgabe mit einer echten Exportdatei vergleichen.");
    for (const OssPosten& p : berechnung.posten)
        if (!p.satzHinweis.empty())
            ergebnis.warnungen.push_back(p.land + ": " + p.satzHinweis);
    return ergebnis;
}

} // namespace UltraFIBU
