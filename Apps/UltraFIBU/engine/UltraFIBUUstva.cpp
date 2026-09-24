// Apps/UltraFIBU/engine/UltraFIBUUstva.cpp
// Computing the UStVA and writing it for ELSTER. See the header for why the
// mapping is data, why an unmapped amount stops the return, and why the return
// checks itself against the ledger.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUUstva.h"

#include "UltraFIBUKontenrahmen.h"

#include <UltraCrypt/UltraCryptCore.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

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

// The side a tax key posts on when the transaction is an ordinary one rather
// than a reversal. Output tax rides an Ausgangsrechnung, whose person account
// is debited; input tax rides an Eingangsrechnung, whose person account is
// credited. A posting on the other side is a Storno and subtracts.
SollHaben NormaleSeite(const Steuerschluessel& key) {
    return key.vorsteuer ? SollHaben::Haben : SollHaben::Soll;
}

// ELSTER writes a Bemessungsgrundlage in whole euros and a tax amount to the
// cent. Truncation, not rounding: the form takes the full euro amount of the
// base, and rounding it up would declare turnover that did not happen.
std::string ElsterBetrag(const Money& betrag, KennzahlArt art) {
    const int64_t minor = betrag.Minor();
    if (art == KennzahlArt::Bemessung || art == KennzahlArt::Frei) {
        const int64_t euro = minor / 100;     // toward zero on both signs
        return Zahl(euro);
    }
    char puffer[64];
    const int64_t abs = minor < 0 ? -minor : minor;
    std::snprintf(puffer, sizeof(puffer), "%s%lld.%02lld", minor < 0 ? "-" : "",
                  static_cast<long long>(abs / 100),
                  static_cast<long long>(abs % 100));
    return puffer;
}

std::string XmlEscape(const std::string& text) {
    std::string aus;
    aus.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&':  aus += "&amp;";  break;
            case '<':  aus += "&lt;";   break;
            case '>':  aus += "&gt;";   break;
            case '"':  aus += "&quot;"; break;
            case '\'': aus += "&apos;"; break;
            default:   aus.push_back(c);
        }
    }
    return aus;
}

std::string Fingerabdruck(const std::string& roh) {
    std::vector<uint8_t> digest;
    if (UltraCrypt_Hash(UltraCryptHashAlgorithm::SHA256, roh.data(), roh.size(), digest))
        return UltraCrypt_ToHex(digest);
    return std::string();
}

} // namespace

// ===== THE MAPPING =====

std::string KennzahlArtToText(KennzahlArt art) {
    switch (art) {
        case KennzahlArt::Bemessung: return "bemessung";
        case KennzahlArt::Steuer:    return "steuer";
        case KennzahlArt::Vorsteuer: return "vorsteuer";
        case KennzahlArt::Frei:      return "frei";
        case KennzahlArt::Berechnet: return "berechnet";
    }
    return "bemessung";
}

bool KennzahlArtFromText(const std::string& text, KennzahlArt& out) {
    if (text == "bemessung") { out = KennzahlArt::Bemessung; return true; }
    if (text == "steuer")    { out = KennzahlArt::Steuer;    return true; }
    if (text == "vorsteuer") { out = KennzahlArt::Vorsteuer; return true; }
    if (text == "frei")      { out = KennzahlArt::Frei;      return true; }
    if (text == "berechnet") { out = KennzahlArt::Berechnet; return true; }
    return false;
}

bool UstvaMapping::Laden(const std::string& dateipfad, std::string& fehler) {
    kennzahlen_.clear();
    std::FILE* datei = std::fopen(dateipfad.c_str(), "rb");
    if (datei == nullptr) {
        fehler = "Die Kennzahlen-Datei \"" + dateipfad + "\" ist nicht lesbar.";
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
        if (!kopfGesehen && z.rfind("kennzahl;", 0) == 0) { kopfGesehen = true; continue; }

        const std::vector<std::string> felder = Teile(z, ';');
        if (felder.size() < 5) continue;

        UstvaKennzahl kz;
        kz.code = felder[0];
        if (!KennzahlArtFromText(felder[1], kz.art)) {
            fehler = "Kennzahl " + kz.code + ": \"" + felder[1] +
                     "\" ist keine bekannte Art (bemessung, steuer, vorsteuer, "
                     "frei, berechnet).";
            return false;
        }
        kz.satzPromille = std::atoi(felder[2].c_str());
        kz.geprueft     = (felder[3] == "ja");
        kz.bezeichnung  = felder[4];
        if (kz.code.empty()) continue;
        kennzahlen_.push_back(std::move(kz));
    }

    if (kennzahlen_.empty()) {
        fehler = "Die Kennzahlen-Datei enthält keine Kennzahl.";
        return false;
    }
    return true;
}

bool UstvaMapping::Finde(const std::string& code, UstvaKennzahl& out) const {
    for (const UstvaKennzahl& kz : kennzahlen_)
        if (kz.code == code) { out = kz; return true; }
    return false;
}

std::string UstvaMappingPfad(int jahr) {
    return FindeDatenDatei("UStVA-Kennzahlen-" + Zahl(jahr) + ".csv");
}

std::string KennzahlenJson(const UstvaBerechnung& berechnung) {
    std::string json = "{";
    bool erstes = true;
    for (const auto& eintrag : berechnung.kennzahlen) {
        if (eintrag.second.code.empty()) continue;
        if (!erstes) json += ",";
        erstes = false;
        json += "\"" + eintrag.second.code + "\":" + Zahl(eintrag.second.betrag.Minor());
    }
    json += "}";
    return json;
}

// ===== PERIODS =====

std::string UstvaZeitraumCode(int monatOderQuartal, bool vierteljaehrlich) {
    if (vierteljaehrlich) {
        if (monatOderQuartal < 1 || monatOderQuartal > 4) return std::string();
        return Zahl(40 + monatOderQuartal);           // 41..44
    }
    if (monatOderQuartal < 1 || monatOderQuartal > 12) return std::string();
    char puffer[8];
    std::snprintf(puffer, sizeof(puffer), "%02d", monatOderQuartal);
    return puffer;
}

bool UstvaZeitraumGrenzen(int jahr, const std::string& zeitraum, Date& outVon,
                          Date& outBis) {
    if (zeitraum.size() != 2) return false;
    const int wert = std::atoi(zeitraum.c_str());
    if (wert >= 41 && wert <= 44) {
        const int ersterMonat = (wert - 41) * 3 + 1;
        outVon = Date(jahr, ersterMonat, 1);
        const int letzterMonat = ersterMonat + 2;
        outBis = Date(jahr, letzterMonat, Date::DaysInMonth(jahr, letzterMonat));
        return outVon.Valid() && outBis.Valid();
    }
    if (wert >= 1 && wert <= 12) {
        outVon = Date(jahr, wert, 1);
        outBis = Date(jahr, wert, Date::DaysInMonth(jahr, wert));
        return outVon.Valid() && outBis.Valid();
    }
    return false;
}

// ===== THE COMPUTATION =====

Money UstvaBerechnung::Betrag(const std::string& code) const {
    const auto treffer = kennzahlen.find(code);
    if (treffer == kennzahlen.end()) return Money::Zero("EUR");
    return treffer->second.betrag;
}

UstvaBerechnung BerechneUstva(int64_t mandantId, int jahr, const std::string& zeitraum,
                              const Date& von, const Date& bis,
                              const std::vector<Buchung>& journal,
                              const std::vector<Steuerschluessel>& steuerschluessel,
                              const UstvaMapping& mapping) {
    UstvaBerechnung ergebnis;
    ergebnis.mandantId = mandantId;
    ergebnis.jahr      = jahr;
    ergebnis.zeitraum  = zeitraum;
    ergebnis.von       = von;
    ergebnis.bis       = bis;

    if (!von.Valid() || !bis.Valid() || bis < von) {
        ergebnis.fehler = "Der Zeitraum der Voranmeldung ist ungültig.";
        return ergebnis;
    }
    if (mapping.Anzahl() == 0) {
        ergebnis.fehler = "Es ist keine Kennzahlen-Zuordnung geladen.";
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

    // Gaps are collected per tax key rather than per posting: "USt19 has no
    // Kennzahl" said once with the total is actionable, the same sentence two
    // hundred times is not.
    std::map<std::string, UstvaLuecke> luecken;
    auto luecke = [&](const Steuerschluessel& key, const Money& netto,
                      const Money& steuer, const std::string& grund) {
        UstvaLuecke& l = luecken[key.schluessel];
        if (l.steuerschluessel.empty()) {
            l.steuerschluessel = key.schluessel;
            l.bezeichnung      = key.bezeichnung;
            l.netto            = Money::Zero("EUR");
            l.steuer           = Money::Zero("EUR");
            l.grund            = grund;
        }
        l.netto  = l.netto + netto;
        l.steuer = l.steuer + steuer;
        ++l.buchungen;
    };

    auto addiere = [&](const std::string& code, const Money& betrag,
                       const Money& steuerGebucht, int64_t buchungId) {
        UstvaKennzahl kz;
        if (!mapping.Finde(code, kz)) return;
        KennzahlBetrag& eintrag = ergebnis.kennzahlen[code];
        if (eintrag.code.empty()) {
            eintrag.code  = code;
            eintrag.art   = kz.art;
            eintrag.betrag        = Money::Zero("EUR");
            eintrag.steuerGebucht = Money::Zero("EUR");
        }
        eintrag.betrag        = eintrag.betrag + betrag;
        eintrag.steuerGebucht = eintrag.steuerGebucht + steuerGebucht;
        ++eintrag.buchungen;
        if (buchungId != 0) eintrag.buchungIds.push_back(buchungId);
    };

    int ohneSchluessel = 0;
    for (const Buchung& b : journal) {
        if (b.mandantId != 0 && mandantId != 0 && b.mandantId != mandantId) continue;
        if (!b.belegdatum.Valid()) continue;
        if (b.belegdatum < von || bis < b.belegdatum) continue;

        // A posting with no tax key carries no turnover and no input tax: a
        // bank transfer, an accrual, an opening balance. It is counted so the
        // report can say how much of the journal the return did not touch.
        if (b.steuerschluessel.empty()) { ++ohneSchluessel; continue; }

        Steuerschluessel key;
        if (!findeKey(b.steuerschluessel, b.belegdatum, key)) {
            UstvaLuecke& l = luecken[b.steuerschluessel];
            if (l.steuerschluessel.empty()) {
                l.steuerschluessel = b.steuerschluessel;
                l.bezeichnung      = "(unbekannt)";
                l.netto            = Money::Zero("EUR");
                l.steuer           = Money::Zero("EUR");
                l.grund = "Diesen Steuerschlüssel gibt es nicht (mehr) oder er "
                          "war am Belegdatum nicht gültig.";
            }
            l.netto  = l.netto + b.netto;
            l.steuer = l.steuer + b.steuer;
            ++l.buchungen;
            continue;
        }

        // A Storno runs the other way and must subtract, or a corrected month
        // declares the turnover twice.
        const int vorzeichen = (b.sollHaben == NormaleSeite(key)) ? 1 : -1;
        const Money netto  = Money::FromMinor(vorzeichen * b.netto.Minor(), "EUR");
        const Money steuer = Money::FromMinor(vorzeichen * b.steuer.Minor(), "EUR");

        // **The base.** Without a Kennzahl the turnover has nowhere to go, and
        // leaving it out silently is an under-declaration.
        bool etwasGemeldet = false;
        if (!key.kzBemessung.empty()) {
            UstvaKennzahl kz;
            if (!mapping.Finde(key.kzBemessung, kz)) {
                luecke(key, netto, steuer,
                       "Kennzahl " + key.kzBemessung + " steht nicht in der "
                       "Kennzahlen-Datei des Jahres.");
            } else if (!kz.geprueft) {
                luecke(key, netto, steuer,
                       "Kennzahl " + key.kzBemessung + " (" + kz.bezeichnung +
                       ") ist noch nicht am BMF-Vordruckmuster geprüft.");
            } else {
                addiere(key.kzBemessung, netto, steuer, b.id);
                etwasGemeldet = true;
            }
        }

        // **The tax**, where it is declared separately rather than derived by
        // ELSTER from the rate.
        if (!key.kzSteuer.empty()) {
            UstvaKennzahl kz;
            if (!mapping.Finde(key.kzSteuer, kz)) {
                luecke(key, Money::Zero("EUR"), steuer,
                       "Kennzahl " + key.kzSteuer + " steht nicht in der "
                       "Kennzahlen-Datei des Jahres.");
            } else if (!kz.geprueft) {
                luecke(key, Money::Zero("EUR"), steuer,
                       "Kennzahl " + key.kzSteuer + " (" + kz.bezeichnung +
                       ") ist noch nicht am BMF-Vordruckmuster geprüft.");
            } else {
                // The declared amount on a tax line is the tax, not the base.
                addiere(key.kzSteuer, steuer, steuer, b.id);
                etwasGemeldet = true;
            }
        }

        if (key.kzBemessung.empty() && key.kzSteuer.empty()) {
            luecke(key, netto, steuer,
                   "Für diesen Steuerschlüssel ist in data/Steuerschluessel.csv "
                   "keine UStVA-Kennzahl eingetragen (Spalten kz_bemessung / "
                   "kz_steuer).");
        }
        (void)etwasGemeldet;
    }

    for (const auto& eintrag : luecken) ergebnis.luecken.push_back(eintrag.second);
    std::sort(ergebnis.luecken.begin(), ergebnis.luecken.end(),
              [](const UstvaLuecke& a, const UstvaLuecke& b) {
                  return a.steuerschluessel < b.steuerschluessel;
              });

    // ---- Kz 83: what is owed ----
    // Declared tax minus declared input tax. On a Bemessung line ELSTER
    // derives the tax from the rate, so the same derivation happens here -
    // and it is compared against what the journal actually booked.
    int64_t steuerSumme = 0, vorsteuerSumme = 0;
    for (auto& eintrag : ergebnis.kennzahlen) {
        KennzahlBetrag& kb = eintrag.second;
        UstvaKennzahl kz;
        if (!mapping.Finde(kb.code, kz)) continue;

        if (kz.art == KennzahlArt::Bemessung) {
            // ELSTER computes the tax from the declared base, and the declared
            // base is whole euros. Comparing that against the booked tax is
            // the check that the books and the form agree.
            const int64_t basisMinor = kb.betrag.Minor();
            const int64_t abgeleitet =
                Money::FromMinor(basisMinor, "EUR").TaxOnNet(kz.satzPromille).Minor();
            steuerSumme += abgeleitet;

            const int64_t differenz = abgeleitet - kb.steuerGebucht.Minor();
            const int64_t abs = differenz < 0 ? -differenz : differenz;
            // A cent or two per posting is ordinary rounding. More than that
            // means a rate in the books that the form does not agree with.
            const int64_t erlaubt = static_cast<int64_t>(kb.buchungen) + 1;
            if (abs > erlaubt) {
                ergebnis.abweichungen.push_back(
                    "Kennzahl " + kb.code + ": im Journal sind " +
                    kb.steuerGebucht.ToString() + " Steuer gebucht, aus der "
                    "Bemessungsgrundlage " + kb.betrag.ToString() + " und dem Satz "
                    "der Kennzahl ergeben sich aber " +
                    Money::FromMinor(abgeleitet, "EUR").ToString() +
                    ". Differenz " + Money::FromMinor(differenz, "EUR").ToString() +
                    " - das ist mehr als Rundung und deutet auf einen falschen "
                    "Steuersatz in den Büchern hin.");
            }
        } else if (kz.art == KennzahlArt::Steuer) {
            steuerSumme += kb.betrag.Minor();
        } else if (kz.art == KennzahlArt::Vorsteuer) {
            vorsteuerSumme += kb.betrag.Minor();
        }
    }
    ergebnis.summeSteuer    = Money::FromMinor(steuerSumme, "EUR");
    ergebnis.summeVorsteuer = Money::FromMinor(vorsteuerSumme, "EUR");
    ergebnis.zahllast       = Money::FromMinor(steuerSumme - vorsteuerSumme, "EUR");

    {
        KennzahlBetrag& kz83 = ergebnis.kennzahlen["83"];
        kz83.code   = "83";
        kz83.art    = KennzahlArt::Berechnet;
        kz83.betrag = ergebnis.zahllast;
    }

    if (ohneSchluessel > 0) {
        ergebnis.warnungen.push_back(
            Zahl(ohneSchluessel) + " Buchung(en) im Zeitraum tragen keinen "
            "Steuerschlüssel und gehen deshalb nicht in die Voranmeldung ein "
            "(Zahlungen, Umbuchungen, Abgrenzungen). Das ist der Normalfall.");
    }
    if (!ergebnis.luecken.empty()) {
        Money fehlendNetto = Money::Zero("EUR");
        for (const UstvaLuecke& l : ergebnis.luecken) fehlendNetto = fehlendNetto + l.netto;
        ergebnis.warnungen.push_back(
            "ACHTUNG: " + fehlendNetto.ToString() + " Umsatz aus " +
            Zahl(static_cast<int64_t>(ergebnis.luecken.size())) +
            " Steuerschlüssel(n) konnte keiner geprüften Kennzahl zugeordnet "
            "werden. Die Voranmeldung wäre damit unvollständig und würde zu "
            "wenig Umsatz ausweisen - sie kann so nicht abgegeben werden.");
    }

    ergebnis.ok = true;
    return ergebnis;
}

// ===== ELSTER XML =====

ElsterErgebnis SchreibeUstvaXml(const UstvaBerechnung& berechnung,
                                const ElsterKopf& kopf,
                                const std::string& zielVerzeichnis) {
    ElsterErgebnis ergebnis;

    if (!berechnung.ok) {
        ergebnis.fehler = "Die Berechnung ist fehlgeschlagen: " + berechnung.fehler;
        return ergebnis;
    }
    // **The refusal that matters.** A return missing turnover because its tax
    // key has no verified Kennzahl is not a return with a small gap - it is an
    // under-declaration, and the file would look perfectly valid.
    if (!berechnung.Vollstaendig()) {
        std::string namen;
        for (const UstvaLuecke& l : berechnung.luecken) {
            if (!namen.empty()) namen += ", ";
            namen += l.steuerschluessel;
        }
        ergebnis.fehler =
            "Die Voranmeldung ist unvollständig und wird nicht geschrieben. "
            "Ohne geprüfte Kennzahl sind: " + namen +
            ". Eine Datei, die diese Umsätze weglässt, meldet zu wenig Umsatz "
            "und sieht dabei völlig in Ordnung aus. Zuerst die Kennzahlen in "
            "data/Steuerschluessel.csv und data/UStVA-Kennzahlen-<Jahr>.csv "
            "ergänzen und auf \"geprueft = ja\" setzen.";
        return ergebnis;
    }
    if (kopf.steuernummer.empty()) {
        ergebnis.fehler = "Ohne Steuernummer nimmt ELSTER die Anmeldung nicht an.";
        return ergebnis;
    }
    if (kopf.finanzamtNummer.empty()) {
        ergebnis.fehler = "Ohne Finanzamtsnummer weiß ELSTER nicht, wohin die "
                          "Anmeldung geht.";
        return ergebnis;
    }

    const std::string datenLieferant =
        kopf.name + "\n" + kopf.strasse + "\n" + kopf.plz + " " + kopf.ort;

    std::string xml;
    xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    // The envelope is reconstructed from secondary sources: the official
    // schemas ship inside the ERiC SDK, which this repository does not have.
    // Saying so inside the file itself is the difference between a draft
    // somebody checks and a draft somebody trusts.
    xml += "<!--\n"
           "  UltraFIBU - Umsatzsteuer-Voranmeldung\n"
           "\n"
           "  ACHTUNG: Der ELSTER-Rahmen (TransferHeader / NutzdatenHeader)\n"
           "  dieser Datei ist NICHT gegen das amtliche Schema geprueft. Die\n"
           "  Schemata liegen im ERiC-SDK, das nicht in diesem Repository ist\n"
           "  und dort auch nicht liegen darf. Geprueft wird diese Datei mit\n"
           "  EricCheckXML, sobald das SDK vorliegt.\n"
           "\n"
           "  Die ZAHLEN in <Umsatzsteuervoranmeldung> stammen unmittelbar aus\n"
           "  dem Journal und sind nachvollziehbar: jede Kennzahl nennt die\n"
           "  Buchungen, aus denen sie entstanden ist.\n"
           "-->\n";
    xml += "<Elster xmlns=\"http://www.elster.de/elsterxml/schema/v11\">\n";
    xml += "  <TransferHeader version=\"11\">\n";
    xml += "    <Verfahren>ElsterAnmeldung</Verfahren>\n";
    xml += "    <DatenArt>UStVA</DatenArt>\n";
    xml += "    <Vorgang>send-Auth</Vorgang>\n";
    // Without the Testmerker a test submission is filed for real; with it, a
    // real one never arrives. Neither is visible afterwards, so it is written
    // explicitly either way rather than defaulted.
    if (!kopf.echtfall)
        xml += "    <Testmerker>700000004</Testmerker>\n";
    xml += "    <HerstellerID>" + XmlEscape(kopf.herstellerId.empty() ? "00000"
                                                                     : kopf.herstellerId) +
           "</HerstellerID>\n";
    xml += "    <DatenLieferant>" + XmlEscape(datenLieferant) + "</DatenLieferant>\n";
    xml += "    <VersionClient>" + XmlEscape(kopf.produktVersion) + "</VersionClient>\n";
    xml += "  </TransferHeader>\n";
    xml += "  <DatenTeil>\n";
    xml += "    <Nutzdatenblock>\n";
    xml += "      <NutzdatenHeader version=\"11\">\n";
    xml += "        <NutzdatenTicket>1</NutzdatenTicket>\n";
    xml += "        <Empfaenger id=\"F\"><Ziel>" + XmlEscape(kopf.finanzamtNummer) +
           "</Ziel></Empfaenger>\n";
    xml += "        <Hersteller>\n";
    xml += "          <ProduktName>" + XmlEscape(kopf.produktName) + "</ProduktName>\n";
    xml += "          <ProduktVersion>" + XmlEscape(kopf.produktVersion) +
           "</ProduktVersion>\n";
    xml += "        </Hersteller>\n";
    xml += "        <DatenLieferant>" + XmlEscape(kopf.name) + "</DatenLieferant>\n";
    xml += "      </NutzdatenHeader>\n";
    xml += "      <Nutzdaten>\n";
    xml += "        <Anmeldungssteuern art=\"UStVA\" version=\"" +
           Zahl(berechnung.jahr) + "\">\n";
    xml += "          <DatenLieferant>" + XmlEscape(datenLieferant) +
           "</DatenLieferant>\n";
    xml += "          <Erstellungsdatum>" +
           (kopf.erstellt.Valid() ? FormatDateCompact(kopf.erstellt) : std::string()) +
           "</Erstellungsdatum>\n";
    xml += "          <Steuerfall>\n";
    xml += "            <Umsatzsteuervoranmeldung>\n";
    xml += "              <Jahr>" + Zahl(berechnung.jahr) + "</Jahr>\n";
    xml += "              <Zeitraum>" + XmlEscape(berechnung.zeitraum) + "</Zeitraum>\n";
    xml += "              <Steuernummer>" + XmlEscape(kopf.steuernummer) +
           "</Steuernummer>\n";
    if (kopf.berichtigt)
        xml += "              <Kz10>1</Kz10>\n";

    // Kennzahlen in form order, which is numeric order, so a human comparing
    // the file against the paper form reads down both at once.
    std::vector<const KennzahlBetrag*> zeilen;
    for (const auto& eintrag : berechnung.kennzahlen) {
        if (eintrag.second.code.empty()) continue;
        // A zero line is left out: ELSTER treats an absent Kennzahl as zero,
        // and a form full of zeros hides the figures that matter.
        if (eintrag.second.betrag.Minor() == 0 && eintrag.second.code != "83") continue;
        zeilen.push_back(&eintrag.second);
    }
    std::sort(zeilen.begin(), zeilen.end(),
              [](const KennzahlBetrag* a, const KennzahlBetrag* b) {
                  return std::atoi(a->code.c_str()) < std::atoi(b->code.c_str());
              });
    for (const KennzahlBetrag* kb : zeilen) {
        xml += "              <Kz" + kb->code + ">" +
               ElsterBetrag(kb->betrag, kb->art) + "</Kz" + kb->code + ">\n";
    }

    xml += "            </Umsatzsteuervoranmeldung>\n";
    xml += "          </Steuerfall>\n";
    xml += "        </Anmeldungssteuern>\n";
    xml += "      </Nutzdaten>\n";
    xml += "    </Nutzdatenblock>\n";
    xml += "  </DatenTeil>\n";
    xml += "</Elster>\n";

    const std::string dateiname =
        "UStVA_" + Zahl(berechnung.jahr) + "_" + berechnung.zeitraum + ".xml";
    const std::string pfad =
        zielVerzeichnis.empty() ? dateiname : zielVerzeichnis + "/" + dateiname;
    std::FILE* datei = std::fopen(pfad.c_str(), "wb");
    if (datei == nullptr) {
        ergebnis.fehler = "Die Datei \"" + pfad + "\" ist nicht schreibbar.";
        return ergebnis;
    }
    std::fwrite(xml.data(), 1, xml.size(), datei);
    std::fclose(datei);

    ergebnis.ok      = true;
    ergebnis.datei   = pfad;
    // The hash of exactly what was written. What is filed has to be storable
    // unaltered and provable later; a figure that is disputed two years on is
    // answered with the file and its hash, not with a recomputation.
    ergebnis.xmlHash = Fingerabdruck(xml);

    if (!kopf.echtfall)
        ergebnis.warnungen.push_back(
            "Die Datei trägt den Testmerker 700000004 und ist damit eine "
            "Testübermittlung. Für eine echte Abgabe mit --echtfall erzeugen.");
    if (kopf.herstellerId.empty())
        ergebnis.warnungen.push_back(
            "Es ist keine Hersteller-ID eingetragen. Die bekommt man bei der "
            "Registrierung als Softwarehersteller; ohne sie weist ELSTER die "
            "Übermittlung zurück.");
    if (!berechnung.abweichungen.empty())
        ergebnis.warnungen.push_back(
            "Die Datei wurde geschrieben, aber die gebuchte Steuer weicht von "
            "der aus den Kennzahlsätzen berechneten ab - siehe die Meldung der "
            "Berechnung. Vor dem Hochladen klären.");
    return ergebnis;
}

// ===== TRANSPORTS =====

namespace {

class DateiTransport : public IElsterTransport {
public:
    std::string Name() const override { return "Datei (manueller Upload in Mein ELSTER)"; }
    bool Verfuegbar(std::string& warum) const override {
        warum.clear();
        return true;
    }
    ElsterErgebnis Senden(const UstvaBerechnung& berechnung, const ElsterKopf& kopf,
                          const std::string& zielVerzeichnis) override {
        ElsterErgebnis r = SchreibeUstvaXml(berechnung, kopf, zielVerzeichnis);
        if (r.ok)
            r.warnungen.push_back(
                "Die Datei wurde geschrieben, aber nichts wurde übermittelt. "
                "Hochladen in Mein ELSTER; das Transferticket danach mit "
                "\"ultrafibu meldung-quittung\" eintragen.");
        return r;
    }
};

// ERiC, when somebody supplies it.
//
// This deliberately does not call ERiC yet. Its C API is known here only from
// secondary sources, and the official handbook ships inside the SDK; guessing
// at `EricBearbeiteVorgang`'s flag vocabulary would produce a transport that
// compiles, links against nothing, and fails in front of a tax authority. So
// it reports what it needs and stops, which is a true answer.
class EricTransport : public IElsterTransport {
public:
    explicit EricTransport(std::string verzeichnis)
        : verzeichnis_(std::move(verzeichnis)) {}

    std::string Name() const override { return "ERiC"; }

    bool Verfuegbar(std::string& warum) const override {
        if (verzeichnis_.empty()) {
            warum = "Es ist kein ERiC-Verzeichnis konfiguriert.";
            return false;
        }
        // The shared library under its platform name. Present or not, the
        // answer is checkable rather than assumed.
        static const char* const kNamen[] = { "libericapi.so", "ericapi.dll",
                                              "libericapi.dylib" };
        for (const char* name : kNamen) {
            const std::string pfad = verzeichnis_ + "/" + name;
            std::FILE* f = std::fopen(pfad.c_str(), "rb");
            if (f != nullptr) {
                std::fclose(f);
                warum = "ERiC liegt unter " + pfad +
                        ", die Anbindung ist aber noch nicht implementiert: die "
                        "C-Schnittstelle muss zuerst am amtlichen Handbuch aus "
                        "dem SDK geprüft werden. Bis dahin die XML-Datei "
                        "erzeugen und in Mein ELSTER hochladen.";
                return false;
            }
        }
        warum = "In \"" + verzeichnis_ + "\" liegt keine ERiC-Bibliothek "
                "(libericapi.so / ericapi.dll / libericapi.dylib). ERiC wird "
                "vom Bundeszentralamt für Steuern bereitgestellt und darf "
                "diesem Programm nicht beiliegen; es muss getrennt installiert "
                "werden.";
        return false;
    }

    ElsterErgebnis Senden(const UstvaBerechnung& berechnung, const ElsterKopf& kopf,
                          const std::string& zielVerzeichnis) override {
        ElsterErgebnis r;
        std::string warum;
        if (!Verfuegbar(warum)) { r.fehler = warum; return r; }
        r.fehler = "Die ERiC-Übermittlung ist noch nicht implementiert.";
        (void)berechnung; (void)kopf; (void)zielVerzeichnis;
        return r;
    }

private:
    std::string verzeichnis_;
};

} // namespace

std::unique_ptr<IElsterTransport> ElsterDateiTransport() {
    return std::unique_ptr<IElsterTransport>(new DateiTransport());
}

std::unique_ptr<IElsterTransport> ElsterEricTransport(const std::string& ericVerzeichnis) {
    return std::unique_ptr<IElsterTransport>(new EricTransport(ericVerzeichnis));
}

} // namespace UltraFIBU
