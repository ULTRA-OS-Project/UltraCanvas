// Apps/UltraFIBU/engine/UltraFIBUBank.cpp
// The three bank-statement readers. See the header for the four format
// properties that shape all of this.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUBank.h"

#include "UltraFIBUKontenrahmen.h"

#include <UltraCrypt/UltraCryptCore.h>
#include <tinyxml2.h>

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

// CP1252 to UTF-8. MT940 and most German bank CSVs are CP1252, and an umlaut
// read as UTF-8 becomes two wrong characters in a payer's name - which then
// fails to match the partner it belongs to.
std::string VonCp1252(const std::string& roh) {
    static const uint16_t kHoch[32] = {
        0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
        0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
    };
    std::string aus;
    aus.reserve(roh.size());
    for (unsigned char c : roh) {
        uint32_t cp = c;
        if (c >= 0x80 && c <= 0x9F) cp = kHoch[c - 0x80];
        if (cp < 0x80) {
            aus.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            aus.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            aus.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            aus.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            aus.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            aus.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return aus;
}

bool SiehtWieUtf8Aus(const std::string& text) {
    int folge = 0;
    for (unsigned char c : text) {
        if (folge > 0) {
            if ((c & 0xC0) != 0x80) return false;
            --folge;
        } else if (c >= 0xF0) { folge = 3; }
        else if (c >= 0xE0)   { folge = 2; }
        else if (c >= 0xC0)   { folge = 1; }
        else if (c >= 0x80)   { return false; }
    }
    return folge == 0;
}

bool LiesDatei(const std::string& pfad, std::string& roh, std::string& fehler) {
    std::FILE* datei = std::fopen(pfad.c_str(), "rb");
    if (datei == nullptr) {
        fehler = "Die Datei \"" + pfad + "\" ist nicht lesbar.";
        return false;
    }
    char puffer[8192];
    size_t n = 0;
    while ((n = std::fread(puffer, 1, sizeof(puffer), datei)) > 0) roh.append(puffer, n);
    std::fclose(datei);
    if (roh.empty()) {
        fehler = "Die Datei \"" + pfad + "\" ist leer.";
        return false;
    }
    return true;
}

std::string Fingerabdruck(const std::string& roh) {
    std::vector<uint8_t> digest;
    if (UltraCrypt_Hash(UltraCryptHashAlgorithm::SHA256, roh.data(), roh.size(), digest))
        return UltraCrypt_ToHex(digest);
    return std::string();
}

std::vector<std::string> Zeilen(const std::string& inhalt) {
    std::vector<std::string> zeilen;
    size_t start = 0;
    while (start <= inhalt.size()) {
        size_t ende = inhalt.find('\n', start);
        if (ende == std::string::npos) ende = inhalt.size();
        std::string zeile = inhalt.substr(start, ende - start);
        if (!zeile.empty() && zeile.back() == '\r') zeile.pop_back();
        zeilen.push_back(zeile);
        if (ende == inhalt.size()) break;
        start = ende + 1;
    }
    return zeilen;
}

// ===== TINYXML2 WITHOUT NAMESPACE SURPRISES =====
//
// tinyxml2 does not strip namespace prefixes, so an element written as
// <Ntry> in one bank's file and <ns:Ntry> in another's has two different
// names. Every lookup here compares the part after the colon, which makes the
// reader indifferent to a prefix it cannot predict.

const char* OhnePraefix(const char* name) {
    if (name == nullptr) return "";
    const char* doppelpunkt = std::strrchr(name, ':');
    return doppelpunkt ? doppelpunkt + 1 : name;
}

const tinyxml2::XMLElement* ErstesKind(const tinyxml2::XMLElement* eltern,
                                       const char* name) {
    if (eltern == nullptr) return nullptr;
    for (const tinyxml2::XMLElement* k = eltern->FirstChildElement(); k != nullptr;
         k = k->NextSiblingElement())
        if (std::strcmp(OhnePraefix(k->Name()), name) == 0) return k;
    return nullptr;
}

const tinyxml2::XMLElement* NaechstesGeschwister(const tinyxml2::XMLElement* knoten,
                                                 const char* name) {
    if (knoten == nullptr) return nullptr;
    for (const tinyxml2::XMLElement* k = knoten->NextSiblingElement(); k != nullptr;
         k = k->NextSiblingElement())
        if (std::strcmp(OhnePraefix(k->Name()), name) == 0) return k;
    return nullptr;
}

// A path of element names, each looked up without its prefix.
const tinyxml2::XMLElement* Pfad(const tinyxml2::XMLElement* start,
                                 std::initializer_list<const char*> namen) {
    const tinyxml2::XMLElement* knoten = start;
    for (const char* name : namen) {
        knoten = ErstesKind(knoten, name);
        if (knoten == nullptr) return nullptr;
    }
    return knoten;
}

std::string Text(const tinyxml2::XMLElement* knoten) {
    if (knoten == nullptr) return std::string();
    const char* t = knoten->GetText();
    return t ? Trimme(t) : std::string();
}

std::string TextAn(const tinyxml2::XMLElement* start,
                   std::initializer_list<const char*> namen) {
    return Text(Pfad(start, namen));
}

// A CAMT date is either <Dt>2026-06-15</Dt> or <DtTm>2026-06-15T...</DtTm>.
bool LiesCamtDatum(const tinyxml2::XMLElement* knoten, Date& out) {
    if (knoten == nullptr) return false;
    std::string text = TextAn(knoten, {"Dt"});
    if (text.empty()) text = TextAn(knoten, {"DtTm"});
    if (text.empty()) text = Text(knoten);
    if (text.size() > 10) text = text.substr(0, 10);
    return Date::TryParseIso(text, out);
}

// CAMT amounts are dot-decimal, always. Never the process locale's business.
bool LiesCamtBetrag(const tinyxml2::XMLElement* knoten, const std::string& fallbackWaehrung,
                    Money& out) {
    if (knoten == nullptr) return false;
    const char* ccy = knoten->Attribute("Ccy");
    const std::string waehrung = (ccy && *ccy) ? ccy : fallbackWaehrung;
    return Money::TryParse(Text(knoten), out, waehrung, UltraCanvas::MoneyStyle::Plain);
}

// CRDT is money in, DBIT is money out. Anything else is not a direction and is
// reported rather than assumed, because assuming makes half a statement wrong
// while the totals still look plausible.
bool LiesRichtung(const std::string& text, bool& eingang) {
    if (text == "CRDT") { eingang = true;  return true; }
    if (text == "DBIT") { eingang = false; return true; }
    return false;
}

Money MitVorzeichen(const Money& betrag, bool eingang) {
    const int64_t m = betrag.Minor() < 0 ? -betrag.Minor() : betrag.Minor();
    return Money::FromMinor(eingang ? m : -m, betrag.Currency());
}

} // namespace

// ===== SHARED =====

std::string BankFormatToText(BankFormat format) {
    switch (format) {
        case BankFormat::Camt053: return "camt053";
        case BankFormat::Mt940:   return "mt940";
        case BankFormat::Csv:     return "csv";
    }
    return "camt053";
}

bool BankFormatFromText(const std::string& text, BankFormat& out) {
    if (text == "camt053" || text == "camt" || text == "CAMT053") { out = BankFormat::Camt053; return true; }
    if (text == "mt940"   || text == "MT940")                     { out = BankFormat::Mt940;   return true; }
    if (text == "csv"     || text == "CSV")                       { out = BankFormat::Csv;     return true; }
    return false;
}

bool Bankauszug::Stimmt(Money& outDifferenz) const {
    outDifferenz = Money::Zero(waehrung);
    if (!saldenGelesen) return true;      // nothing to check against
    if (!anfangssaldo.Valid() || !endsaldo.Valid()) return true;

    int64_t summe = anfangssaldo.Minor();
    for (const Bankumsatz& u : umsaetze) summe += u.betrag.Minor();
    outDifferenz = Money::FromMinor(summe - endsaldo.Minor(), waehrung);
    return outDifferenz.Minor() == 0;
}

// ===== SEPA REMITTANCE TAGS =====

SepaTags ZerlegeSepaTags(const std::string& text) {
    SepaTags tags;
    // The tags a German bank packs into one remittance line. They appear in
    // this order but not all of them appear, so the parser finds each one's
    // position and each value runs to whichever tag starts next.
    static const char* const kTags[] = {
        "EREF+", "KREF+", "MREF+", "CRED+", "DEBT+", "COAM+", "OAMT+",
        "SVWZ+", "ABWA+", "ABWE+", "IBAN+", "BIC+"
    };

    struct Fund { size_t pos; const char* tag; };
    std::vector<Fund> funde;
    for (const char* tag : kTags) {
        size_t pos = text.find(tag);
        while (pos != std::string::npos) {
            funde.push_back({pos, tag});
            pos = text.find(tag, pos + 1);
        }
    }
    if (funde.empty()) {
        // No tags at all: the whole string is the remittance information, which
        // is the ordinary case for a hand-typed transfer.
        tags.verwendungszweck = Trimme(text);
        return tags;
    }
    std::sort(funde.begin(), funde.end(),
              [](const Fund& a, const Fund& b) { return a.pos < b.pos; });

    for (size_t i = 0; i < funde.size(); ++i) {
        const size_t start = funde[i].pos + std::strlen(funde[i].tag);
        const size_t ende  = (i + 1 < funde.size()) ? funde[i + 1].pos : text.size();
        if (start > ende) continue;
        const std::string wert = Trimme(text.substr(start, ende - start));
        const std::string tag(funde[i].tag);
        if      (tag == "EREF+") tags.endToEndId       = wert;
        else if (tag == "KREF+") tags.kundenreferenz   = wert;
        else if (tag == "MREF+") tags.mandatsreferenz  = wert;
        else if (tag == "CRED+") tags.glaeubigerId     = wert;
        else if (tag == "SVWZ+") tags.verwendungszweck = wert;
        else if (tag == "ABWA+") tags.abweichenderName = wert;
    }
    // Text before the first tag is remittance information too, on the banks
    // that put the human part first.
    if (tags.verwendungszweck.empty()) {
        const std::string vorne = Trimme(text.substr(0, funde.front().pos));
        tags.verwendungszweck = vorne;
    }
    // NOTREF and NONREF are a bank saying "there was none", not a reference.
    if (tags.endToEndId == "NOTPROVIDED" || tags.endToEndId == "NONREF")
        tags.endToEndId.clear();
    return tags;
}

// ===== CAMT.053 =====

BankLeseBericht LiesCamt053(const std::string& dateipfad) {
    BankLeseBericht bericht;
    bericht.format = BankFormat::Camt053;

    std::string roh;
    if (!LiesDatei(dateipfad, roh, bericht.fehler)) return bericht;
    bericht.dateiHash = Fingerabdruck(roh);

    tinyxml2::XMLDocument dokument;
    if (dokument.Parse(roh.data(), roh.size()) != tinyxml2::XML_SUCCESS) {
        bericht.fehler = std::string("Die Datei ist kein gültiges XML: ") +
                         (dokument.ErrorStr() ? dokument.ErrorStr() : "unbekannter Fehler");
        return bericht;
    }

    const tinyxml2::XMLElement* wurzel = dokument.RootElement();
    if (wurzel == nullptr) {
        bericht.fehler = "Die XML-Datei hat kein Wurzelelement.";
        return bericht;
    }
    // <Document><BkToCstmrStmt>. Some banks wrap it in a message envelope, so
    // the statement block is looked for one level deeper as well.
    const tinyxml2::XMLElement* stmtBlock = ErstesKind(wurzel, "BkToCstmrStmt");
    if (stmtBlock == nullptr) {
        for (const tinyxml2::XMLElement* k = wurzel->FirstChildElement();
             k != nullptr && stmtBlock == nullptr; k = k->NextSiblingElement())
            stmtBlock = ErstesKind(k, "BkToCstmrStmt");
    }
    if (stmtBlock == nullptr) {
        bericht.fehler =
            "In der Datei steht kein <BkToCstmrStmt> - das ist kein CAMT.053 "
            "(Kontoauszug). CAMT.052 (Vormerkungen) und CAMT.054 "
            "(Einzelbuchungen) haben andere Wurzelelemente.";
        return bericht;
    }

    for (const tinyxml2::XMLElement* stmt = ErstesKind(stmtBlock, "Stmt"); stmt != nullptr;
         stmt = NaechstesGeschwister(stmt, "Stmt")) {
        Bankauszug auszug;
        auszug.auszugsnummer = TextAn(stmt, {"LglSeqNb"});
        if (auszug.auszugsnummer.empty()) auszug.auszugsnummer = TextAn(stmt, {"ElctrncSeqNb"});
        if (auszug.auszugsnummer.empty()) auszug.auszugsnummer = TextAn(stmt, {"Id"});

        auszug.iban = TextAn(stmt, {"Acct", "Id", "IBAN"});
        if (auszug.iban.empty()) auszug.iban = TextAn(stmt, {"Acct", "Id", "Othr", "Id"});
        auszug.bic = TextAn(stmt, {"Acct", "Svcr", "FinInstnId", "BIC"});
        if (auszug.bic.empty())
            auszug.bic = TextAn(stmt, {"Acct", "Svcr", "FinInstnId", "BICFI"});
        const std::string ccy = TextAn(stmt, {"Acct", "Ccy"});
        if (!ccy.empty()) auszug.waehrung = ccy;

        if (const tinyxml2::XMLElement* zeitraum = ErstesKind(stmt, "FrToDt")) {
            Date von, bis;
            std::string vonText = TextAn(zeitraum, {"FrDtTm"});
            std::string bisText = TextAn(zeitraum, {"ToDtTm"});
            if (vonText.size() > 10) vonText = vonText.substr(0, 10);
            if (bisText.size() > 10) bisText = bisText.substr(0, 10);
            if (Date::TryParseIso(vonText, von)) auszug.von = von;
            if (Date::TryParseIso(bisText, bis)) auszug.bis = bis;
        }

        // ---- the balances ----
        // OPBD/PRCD is the opening balance, CLBD the closing one. They are
        // what makes the statement check itself, so a file without them is
        // still imported but says so.
        for (const tinyxml2::XMLElement* bal = ErstesKind(stmt, "Bal"); bal != nullptr;
             bal = NaechstesGeschwister(bal, "Bal")) {
            const std::string code = TextAn(bal, {"Tp", "CdOrPrtry", "Cd"});
            Money betrag;
            if (!LiesCamtBetrag(ErstesKind(bal, "Amt"), auszug.waehrung, betrag)) continue;
            bool eingang = true;
            // A balance's CdtDbtInd says whether it is positive or overdrawn.
            if (!LiesRichtung(TextAn(bal, {"CdtDbtInd"}), eingang)) eingang = true;
            const Money vorzeichen = MitVorzeichen(betrag, eingang);

            Date datum;
            LiesCamtDatum(ErstesKind(bal, "Dt"), datum);
            if (code == "OPBD" || code == "PRCD") {
                auszug.anfangssaldo = vorzeichen;
                auszug.saldenGelesen = true;
                if (datum.Valid() && !auszug.von.Valid()) auszug.von = datum;
            } else if (code == "CLBD") {
                auszug.endsaldo = vorzeichen;
                if (datum.Valid() && !auszug.bis.Valid()) auszug.bis = datum;
            }
        }
        if (!auszug.endsaldo.Valid()) auszug.saldenGelesen = false;

        // ---- the entries ----
        int ordnung = 0;
        for (const tinyxml2::XMLElement* ntry = ErstesKind(stmt, "Ntry"); ntry != nullptr;
             ntry = NaechstesGeschwister(ntry, "Ntry")) {
            ++ordnung;
            ++bericht.gelesen;

            // A pending entry has not hit the account yet and can still change
            // or vanish. Importing one produces a ledger difference that
            // nobody can explain once the bank drops it.
            const std::string status = TextAn(ntry, {"Sts"});
            const std::string statusCode = status.empty() ? TextAn(ntry, {"Sts", "Cd"}) : status;
            if (!statusCode.empty() && statusCode != "BOOK") {
                ++bericht.vorgemerkt;
                continue;
            }

            Money betrag;
            if (!LiesCamtBetrag(ErstesKind(ntry, "Amt"), auszug.waehrung, betrag)) {
                bericht.fehlerZeilen.push_back(
                    "Buchung " + Zahl(ordnung) + " in Auszug " + auszug.auszugsnummer +
                    ": der Betrag ist nicht lesbar.");
                ++bericht.uebersprungen;
                continue;
            }
            bool eingang = true;
            if (!LiesRichtung(TextAn(ntry, {"CdtDbtInd"}), eingang)) {
                bericht.fehlerZeilen.push_back(
                    "Buchung " + Zahl(ordnung) + " in Auszug " + auszug.auszugsnummer +
                    ": <CdtDbtInd> ist weder CRDT noch DBIT, die Richtung ist damit "
                    "unbekannt.");
                ++bericht.uebersprungen;
                continue;
            }

            Bankumsatz umsatz;
            umsatz.betrag = MitVorzeichen(betrag, eingang);
            if (!LiesCamtDatum(ErstesKind(ntry, "BookgDt"), umsatz.buchungstag)) {
                bericht.fehlerZeilen.push_back(
                    "Buchung " + Zahl(ordnung) + " in Auszug " + auszug.auszugsnummer +
                    ": kein lesbares Buchungsdatum.");
                ++bericht.uebersprungen;
                continue;
            }
            if (!LiesCamtDatum(ErstesKind(ntry, "ValDt"), umsatz.valuta))
                umsatz.valuta = umsatz.buchungstag;

            umsatz.buchungstext = TextAn(ntry, {"AddtlNtryInf"});
            if (umsatz.buchungstext.empty())
                umsatz.buchungstext = TextAn(ntry, {"BkTxCd", "Prtry", "Cd"});

            // ---- the transaction details ----
            const tinyxml2::XMLElement* dtls = ErstesKind(ntry, "NtryDtls");
            std::string zweckRoh;
            const tinyxml2::XMLElement* ersteTx = nullptr;
            if (dtls != nullptr) {
                for (const tinyxml2::XMLElement* tx = ErstesKind(dtls, "TxDtls"); tx != nullptr;
                     tx = NaechstesGeschwister(tx, "TxDtls")) {
                    ++umsatz.teilbuchungen;
                    if (ersteTx == nullptr) ersteTx = tx;
                }
            }
            if (ersteTx != nullptr) {
                // <Ustrd> repeats: a remittance longer than 140 characters is
                // split across several, and joining them is not optional -
                // taking only the first silently truncates every long one.
                if (const tinyxml2::XMLElement* rmt = ErstesKind(ersteTx, "RmtInf")) {
                    for (const tinyxml2::XMLElement* ustrd = ErstesKind(rmt, "Ustrd");
                         ustrd != nullptr; ustrd = NaechstesGeschwister(ustrd, "Ustrd")) {
                        if (!zweckRoh.empty()) zweckRoh += " ";
                        zweckRoh += Text(ustrd);
                    }
                }
                umsatz.endToEndId      = TextAn(ersteTx, {"Refs", "EndToEndId"});
                umsatz.mandatsreferenz = TextAn(ersteTx, {"Refs", "MndtId"});

                // **The counterparty depends on the direction.** On money in
                // the other party is the debtor; on money out, the creditor.
                const tinyxml2::XMLElement* parteien = ErstesKind(ersteTx, "RltdPties");
                const tinyxml2::XMLElement* agenten  = ErstesKind(ersteTx, "RltdAgts");
                if (eingang) {
                    umsatz.gegenName = TextAn(parteien, {"Dbtr", "Nm"});
                    umsatz.gegenIban = TextAn(parteien, {"DbtrAcct", "Id", "IBAN"});
                    umsatz.gegenBic  = TextAn(agenten,  {"DbtrAgt", "FinInstnId", "BIC"});
                    if (umsatz.gegenBic.empty())
                        umsatz.gegenBic = TextAn(agenten, {"DbtrAgt", "FinInstnId", "BICFI"});
                } else {
                    umsatz.gegenName = TextAn(parteien, {"Cdtr", "Nm"});
                    umsatz.gegenIban = TextAn(parteien, {"CdtrAcct", "Id", "IBAN"});
                    umsatz.gegenBic  = TextAn(agenten,  {"CdtrAgt", "FinInstnId", "BIC"});
                    if (umsatz.gegenBic.empty())
                        umsatz.gegenBic = TextAn(agenten, {"CdtrAgt", "FinInstnId", "BICFI"});
                }
            }

            const SepaTags tags = ZerlegeSepaTags(zweckRoh);
            umsatz.verwendungszweck = tags.verwendungszweck;
            if (umsatz.endToEndId.empty()) umsatz.endToEndId = tags.endToEndId;
            if (umsatz.mandatsreferenz.empty()) umsatz.mandatsreferenz = tags.mandatsreferenz;
            umsatz.glaeubigerId = tags.glaeubigerId;
            if (umsatz.gegenName.empty()) umsatz.gegenName = tags.abweichenderName;
            if (umsatz.endToEndId == "NOTPROVIDED" || umsatz.endToEndId == "NONREF")
                umsatz.endToEndId.clear();

            // ---- the idempotency key ----
            // The bank's own reference when it gives one. Otherwise a digest of
            // the statement, the entry's content **and its position**: two
            // identical lines on one day are possible, and a key that could not
            // tell them apart would silently drop the second.
            umsatz.referenz = TextAn(ntry, {"AcctSvcrRef"});
            if (umsatz.referenz.empty()) umsatz.referenz = TextAn(ntry, {"NtryRef"});
            if (umsatz.referenz.empty()) {
                const std::string material =
                    auszug.iban + "|" + auszug.auszugsnummer + "|" + Zahl(ordnung) + "|" +
                    umsatz.buchungstag.ToIso() + "|" +
                    Zahl(umsatz.betrag.Minor()) + "|" + umsatz.gegenIban + "|" +
                    umsatz.verwendungszweck;
                const std::string digest = Fingerabdruck(material);
                umsatz.referenz = "abgeleitet:" + digest.substr(0, 32);
            }

            auszug.umsaetze.push_back(std::move(umsatz));
            ++bericht.uebernommen;
        }

        // The self-check. A statement that carries balances and does not add up
        // has been read wrong; saying so is worth more than importing it.
        Money differenz;
        if (auszug.saldenGelesen && !auszug.Stimmt(differenz)) {
            bericht.warnungen.push_back(
                "Auszug " + auszug.auszugsnummer + " geht nicht auf: Anfangssaldo " +
                auszug.anfangssaldo.ToString() + " plus alle Buchungen ergibt nicht den "
                "Endsaldo " + auszug.endsaldo.ToString() + ", es fehlen " +
                differenz.ToString() + ". Die Datei wurde falsch gelesen oder ist "
                "unvollständig.");
        }
        bericht.auszuege.push_back(std::move(auszug));
    }

    if (bericht.auszuege.empty()) {
        bericht.fehler = "Die Datei enthält keinen Auszug (<Stmt>).";
        return bericht;
    }
    if (bericht.vorgemerkt > 0) {
        bericht.warnungen.push_back(
            Zahl(bericht.vorgemerkt) + " Buchung(en) sind in der Datei als noch nicht "
            "gebucht gekennzeichnet und werden nicht übernommen. Sie erscheinen im "
            "nächsten Auszug, sobald die Bank sie gebucht hat.");
    }
    bericht.ok = bericht.uebernommen > 0;
    if (!bericht.ok && bericht.fehler.empty())
        bericht.fehler = "Die Datei enthält keine übernehmbare Buchung.";
    return bericht;
}

// ===== MT940 =====

namespace {

// :61:  YYMMDD [MMDD] C|D|RC|RD [funds code] amount N type ref //bankref
// The amount is comma-decimal and the C/D marker carries the direction. RC and
// RD are reversals - a returned direct debit - and invert the sign, which is
// the difference between a chargeback that reduces the balance and one that
// appears to increase it.
bool LiesMt940Zeile61(const std::string& feld, const std::string& waehrung,
                      Bankumsatz& out, std::string& fehler) {
    size_t i = 0;
    auto ziffern = [&](size_t anzahl, int& wert) -> bool {
        if (i + anzahl > feld.size()) return false;
        wert = 0;
        for (size_t k = 0; k < anzahl; ++k) {
            const char c = feld[i + k];
            if (c < '0' || c > '9') return false;
            wert = wert * 10 + (c - '0');
        }
        i += anzahl;
        return true;
    };

    int jj = 0, mm = 0, tt = 0;
    if (!ziffern(2, jj) || !ziffern(2, mm) || !ziffern(2, tt)) {
        fehler = "das Valutadatum ist nicht lesbar";
        return false;
    }
    // A two-digit year in a bank file means 20xx; MT940 predates none of the
    // dates this program will ever see.
    out.valuta = Date(2000 + jj, mm, tt);
    if (!out.valuta.Valid()) { fehler = "das Valutadatum ist kein gültiges Datum"; return false; }

    // An optional booking date, MMDD, taking its year from the value date -
    // except across a year boundary, where a December value date with a
    // January booking date belongs to the next year.
    out.buchungstag = out.valuta;
    if (i + 4 <= feld.size() && std::isdigit(static_cast<unsigned char>(feld[i])) &&
        std::isdigit(static_cast<unsigned char>(feld[i + 1])) &&
        std::isdigit(static_cast<unsigned char>(feld[i + 2])) &&
        std::isdigit(static_cast<unsigned char>(feld[i + 3]))) {
        int bmm = 0, btt = 0;
        const size_t merk = i;
        if (ziffern(2, bmm) && ziffern(2, btt)) {
            int jahr = out.valuta.year;
            if (out.valuta.month == 12 && bmm == 1) ++jahr;
            else if (out.valuta.month == 1 && bmm == 12) --jahr;
            const Date kandidat(jahr, bmm, btt);
            if (kandidat.Valid()) out.buchungstag = kandidat;
            else i = merk;
        } else {
            i = merk;
        }
    }

    bool eingang = true;
    if (i < feld.size() && (feld[i] == 'R' || feld[i] == 'r')) {
        // RC / RD: a reversal. The letter after R gives the original
        // direction, and a reversal runs the other way.
        ++i;
        if (i >= feld.size()) { fehler = "nach R fehlt die Richtung"; return false; }
        eingang = (feld[i] == 'D' || feld[i] == 'd');
        ++i;
    } else if (i < feld.size() && (feld[i] == 'C' || feld[i] == 'c')) {
        eingang = true;  ++i;
    } else if (i < feld.size() && (feld[i] == 'D' || feld[i] == 'd')) {
        eingang = false; ++i;
    } else {
        fehler = "es fehlt das Soll-/Haben-Kennzeichen (C oder D)";
        return false;
    }

    // An optional one-letter funds code sits between the marker and the amount.
    if (i < feld.size() && std::isalpha(static_cast<unsigned char>(feld[i])) &&
        i + 1 < feld.size() &&
        (std::isdigit(static_cast<unsigned char>(feld[i + 1])) || feld[i + 1] == ','))
        ++i;

    std::string betragText;
    while (i < feld.size() &&
           (std::isdigit(static_cast<unsigned char>(feld[i])) || feld[i] == ','))
        betragText.push_back(feld[i++]);
    Money betrag;
    if (betragText.empty() ||
        !Money::TryParse(betragText, betrag, waehrung, UltraCanvas::MoneyStyle::Datev)) {
        fehler = "der Betrag \"" + betragText + "\" ist nicht lesbar";
        return false;
    }
    out.betrag = MitVorzeichen(betrag, eingang);

    // What is left is the transaction type (N + three characters) and the
    // references; the bank reference after "//" is the stable one.
    const std::string rest = feld.substr(i);
    const size_t doppel = rest.find("//");
    if (doppel != std::string::npos)
        out.referenz = Trimme(rest.substr(doppel + 2));
    if (out.referenz.empty() || out.referenz == "NONREF") {
        const size_t n = rest.find('N');
        if (n != std::string::npos && n + 4 <= rest.size()) {
            std::string kundenref = rest.substr(n + 4);
            const size_t schnitt = kundenref.find("//");
            if (schnitt != std::string::npos) kundenref = kundenref.substr(0, schnitt);
            kundenref = Trimme(kundenref);
            if (!kundenref.empty() && kundenref != "NONREF") out.referenz = kundenref;
        }
    }
    return true;
}

// :86:  166?00SEPA-GUTSCHRIFT?20EREF+...?30BIC?31IBAN?32Name
// Subfields 20..29 and 60..63 are the remittance information and have to be
// joined; 32 and 33 are the name in two halves and have to be joined too.
void LiesMt940Zeile86(const std::string& feld, Bankumsatz& out) {
    if (feld.find('?') == std::string::npos) {
        // Some banks write :86: as free text with no subfields at all.
        const SepaTags tags = ZerlegeSepaTags(feld);
        out.verwendungszweck = tags.verwendungszweck;
        if (out.endToEndId.empty())      out.endToEndId      = tags.endToEndId;
        if (out.mandatsreferenz.empty()) out.mandatsreferenz = tags.mandatsreferenz;
        if (out.glaeubigerId.empty())    out.glaeubigerId    = tags.glaeubigerId;
        return;
    }

    std::string zweck, name;
    size_t pos = feld.find('?');
    // Anything before the first '?' is the GVC (business transaction code).
    while (pos != std::string::npos) {
        const size_t naechste = feld.find('?', pos + 1);
        const std::string stueck =
            feld.substr(pos + 1, (naechste == std::string::npos ? feld.size() : naechste)
                                     - pos - 1);
        pos = naechste;
        if (stueck.size() < 2) continue;
        const std::string schluessel = stueck.substr(0, 2);
        const std::string wert = stueck.substr(2);
        if (schluessel == "00")                         out.buchungstext = Trimme(wert);
        else if (schluessel >= "20" && schluessel <= "29") zweck += wert;
        else if (schluessel >= "60" && schluessel <= "63") zweck += wert;
        else if (schluessel == "30")                    out.gegenBic  = Trimme(wert);
        else if (schluessel == "31")                    out.gegenIban = Trimme(wert);
        else if (schluessel == "32" || schluessel == "33") name += wert;
    }
    out.gegenName = Trimme(name);

    const SepaTags tags = ZerlegeSepaTags(zweck);
    out.verwendungszweck = tags.verwendungszweck;
    if (out.endToEndId.empty())      out.endToEndId      = tags.endToEndId;
    if (out.mandatsreferenz.empty()) out.mandatsreferenz = tags.mandatsreferenz;
    if (out.glaeubigerId.empty())    out.glaeubigerId    = tags.glaeubigerId;
    if (out.gegenName.empty())       out.gegenName       = tags.abweichenderName;
}

// :60F:C260601EUR1234,56  - mark, date, currency, comma-decimal amount.
bool LiesMt940Saldo(const std::string& feld, Money& out, Date& datum,
                    std::string& waehrung) {
    if (feld.size() < 10) return false;
    size_t i = 0;
    bool eingang = true;
    if (feld[i] == 'C' || feld[i] == 'c') eingang = true;
    else if (feld[i] == 'D' || feld[i] == 'd') eingang = false;
    else return false;
    ++i;
    if (i + 6 > feld.size()) return false;
    const int jj = (feld[i] - '0') * 10 + (feld[i + 1] - '0');
    const int mm = (feld[i + 2] - '0') * 10 + (feld[i + 3] - '0');
    const int tt = (feld[i + 4] - '0') * 10 + (feld[i + 5] - '0');
    i += 6;
    datum = Date(2000 + jj, mm, tt);
    if (i + 3 > feld.size()) return false;
    waehrung = feld.substr(i, 3);
    i += 3;
    Money betrag;
    if (!Money::TryParse(feld.substr(i), betrag, waehrung, UltraCanvas::MoneyStyle::Datev))
        return false;
    out = MitVorzeichen(betrag, eingang);
    return true;
}

} // namespace

BankLeseBericht LiesMt940(const std::string& dateipfad) {
    BankLeseBericht bericht;
    bericht.format = BankFormat::Mt940;

    std::string roh;
    if (!LiesDatei(dateipfad, roh, bericht.fehler)) return bericht;
    bericht.dateiHash = Fingerabdruck(roh);
    // MT940 is CP1252 in practice. A file that is already UTF-8 is left alone,
    // because converting it twice would corrupt exactly the umlauts that
    // matter for matching a payer's name.
    const std::string inhalt = SiehtWieUtf8Aus(roh) ? roh : VonCp1252(roh);

    // Fields continue onto following lines until the next line beginning ":".
    std::vector<std::pair<std::string, std::string>> felder;
    for (const std::string& zeile : Zeilen(inhalt)) {
        if (zeile.empty()) continue;
        if (zeile[0] == '-' && zeile.size() == 1) continue;     // end-of-message
        if (zeile[0] == ':') {
            const size_t ende = zeile.find(':', 1);
            if (ende == std::string::npos) continue;
            felder.emplace_back(zeile.substr(1, ende - 1), zeile.substr(ende + 1));
        } else if (!felder.empty()) {
            felder.back().second += zeile;
        }
    }
    if (felder.empty()) {
        bericht.fehler = "Die Datei enthält keine MT940-Felder (:20:, :25:, :61: ...).";
        return bericht;
    }

    Bankauszug auszug;
    bool imAuszug = false;
    Bankumsatz offen;
    bool offenGueltig = false;
    int ordnung = 0;

    auto umsatzAbschliessen = [&]() {
        if (!offenGueltig) return;
        if (offen.referenz.empty() || offen.referenz == "NONREF") {
            const std::string material =
                auszug.iban + "|" + auszug.auszugsnummer + "|" + Zahl(ordnung) + "|" +
                offen.buchungstag.ToIso() + "|" + Zahl(offen.betrag.Minor()) + "|" +
                offen.gegenIban + "|" + offen.verwendungszweck;
            offen.referenz = "abgeleitet:" + Fingerabdruck(material).substr(0, 32);
        }
        auszug.umsaetze.push_back(offen);
        ++bericht.uebernommen;
        offen = Bankumsatz();
        offenGueltig = false;
    };
    auto auszugAbschliessen = [&]() {
        umsatzAbschliessen();
        if (!imAuszug) return;
        Money differenz;
        if (auszug.saldenGelesen && !auszug.Stimmt(differenz)) {
            bericht.warnungen.push_back(
                "Auszug " + auszug.auszugsnummer + " geht nicht auf: Anfangssaldo plus "
                "alle Buchungen ergibt nicht den Endsaldo, es fehlen " +
                differenz.ToString() + ".");
        }
        bericht.auszuege.push_back(auszug);
        auszug = Bankauszug();
        imAuszug = false;
    };

    for (const auto& feld : felder) {
        const std::string& tag  = feld.first;
        const std::string  wert = Trimme(feld.second);

        if (tag == "20") {                    // a new message starts here
            auszugAbschliessen();
            imAuszug = true;
            ordnung = 0;
        } else if (tag == "25") {             // account
            imAuszug = true;
            // "DE02.../EUR" or "BLZ/Kontonummer". Only an IBAN is useful here.
            std::string konto = wert;
            const size_t schraeg = konto.find('/');
            if (schraeg != std::string::npos) konto = konto.substr(0, schraeg);
            konto = Trimme(konto);
            // ":25:" is an IBAN on a modern file and a BLZ/account number on an
            // old one. Only an IBAN is carried forward; a BLZ/account pair is
            // not one and inventing an IBAN from it would be a guess.
            if (konto.size() >= 15 && std::isalpha(static_cast<unsigned char>(konto[0])))
                auszug.iban = konto;
        } else if (tag == "28C" || tag == "28") {
            auszug.auszugsnummer = wert;
        } else if (tag == "60F" || tag == "60M") {
            Money saldo; Date datum; std::string waehrung;
            if (LiesMt940Saldo(wert, saldo, datum, waehrung)) {
                if (tag == "60F" || !auszug.saldenGelesen) {
                    auszug.anfangssaldo = saldo;
                    auszug.saldenGelesen = true;
                    auszug.waehrung = waehrung;
                    if (datum.Valid()) auszug.von = datum;
                }
            }
        } else if (tag == "62F" || tag == "62M") {
            Money saldo; Date datum; std::string waehrung;
            if (LiesMt940Saldo(wert, saldo, datum, waehrung)) {
                auszug.endsaldo = saldo;
                if (datum.Valid()) auszug.bis = datum;
            }
        } else if (tag == "61") {
            umsatzAbschliessen();
            ++bericht.gelesen;
            ++ordnung;
            Bankumsatz umsatz;
            std::string warum;
            if (!LiesMt940Zeile61(wert, auszug.waehrung, umsatz, warum)) {
                bericht.fehlerZeilen.push_back(
                    "Buchung " + Zahl(ordnung) + " (:61:): " + warum + ".");
                ++bericht.uebersprungen;
                continue;
            }
            offen = umsatz;
            offenGueltig = true;
        } else if (tag == "86") {
            if (offenGueltig) LiesMt940Zeile86(wert, offen);
        }
    }
    auszugAbschliessen();

    if (bericht.auszuege.empty()) {
        bericht.fehler = "Die Datei enthält keinen vollständigen Auszug.";
        return bericht;
    }
    if (!bericht.fehlerZeilen.empty()) {
        bericht.warnungen.push_back(
            Zahl(static_cast<int64_t>(bericht.fehlerZeilen.size())) +
            " Buchung(en) konnten nicht gelesen werden und fehlen im Import.");
    }
    bericht.ok = bericht.uebernommen > 0;
    if (!bericht.ok && bericht.fehler.empty())
        bericht.fehler = "Die Datei enthält keine übernehmbare Buchung.";
    return bericht;
}

// ===== CSV =====

std::string BankProfilPfad(const std::string& dateiname) {
    return FindeDatenDatei(dateiname);
}

bool CsvBankProfil::Laden(const std::string& dateipfad, std::string& fehler) {
    std::string roh;
    if (!LiesDatei(dateipfad, roh, fehler)) return false;
    const std::string inhalt = SiehtWieUtf8Aus(roh) ? roh : VonCp1252(roh);

    auto spalte = [&](const std::string& wert) -> int {
        if (wert.empty() || wert == "-") return -1;
        return std::atoi(wert.c_str()) - 1;      // the file is 1-based
    };

    for (const std::string& zeile : Zeilen(inhalt)) {
        const std::string z = Trimme(zeile);
        if (z.empty() || z[0] == '#') continue;
        const size_t gleich = z.find('=');
        if (gleich == std::string::npos) continue;
        const std::string schluessel = Trimme(z.substr(0, gleich));
        const std::string wert       = Trimme(z.substr(gleich + 1));
        if      (schluessel == "name")            name = wert;
        else if (schluessel == "trenner")         trenner = wert.empty() ? ';' : wert[0];
        else if (schluessel == "kopfzeilen")      kopfzeilen = std::atoi(wert.c_str());
        else if (schluessel == "datumsformat")    datumsformat = wert;
        else if (schluessel == "dezimaltrenner")  dezimaltrenner = wert;
        else if (schluessel == "encoding")        encoding = wert;
        else if (schluessel == "buchungstag")     spalteBuchungstag = spalte(wert);
        else if (schluessel == "valuta")          spalteValuta = spalte(wert);
        else if (schluessel == "betrag")          spalteBetrag = spalte(wert);
        else if (schluessel == "waehrung")        spalteWaehrung = spalte(wert);
        else if (schluessel == "gegen_name")      spalteGegenName = spalte(wert);
        else if (schluessel == "gegen_iban")      spalteGegenIban = spalte(wert);
        else if (schluessel == "gegen_bic")       spalteGegenBic = spalte(wert);
        else if (schluessel == "verwendungszweck") spalteVerwendungszweck = spalte(wert);
        else if (schluessel == "buchungstext")    spalteBuchungstext = spalte(wert);
        else if (schluessel == "soll")            spalteSoll = spalte(wert);
        else if (schluessel == "haben")           spalteHaben = spalte(wert);
        else if (schluessel == "richtung")        spalteRichtung = spalte(wert);
        else if (schluessel == "richtung_eingang") richtungEingang = wert;
    }

    if (spalteBuchungstag < 0) {
        fehler = "Dem Profil fehlt die Spalte \"buchungstag\".";
        return false;
    }
    if (spalteBetrag < 0 && spalteSoll < 0 && spalteHaben < 0) {
        fehler = "Dem Profil fehlt der Betrag: entweder \"betrag\" oder \"soll\" "
                 "und \"haben\".";
        return false;
    }
    return true;
}

namespace {

std::vector<std::string> ZerlegeCsv(const std::string& zeile, char trenner) {
    std::vector<std::string> felder;
    std::string aktuell;
    bool inAnfuehrung = false;
    for (size_t i = 0; i < zeile.size(); ++i) {
        const char c = zeile[i];
        if (inAnfuehrung) {
            if (c == '"') {
                if (i + 1 < zeile.size() && zeile[i + 1] == '"') { aktuell.push_back('"'); ++i; }
                else inAnfuehrung = false;
            } else {
                aktuell.push_back(c);
            }
        } else if (c == '"') {
            inAnfuehrung = true;
        } else if (c == trenner) {
            felder.push_back(aktuell);
            aktuell.clear();
        } else {
            aktuell.push_back(c);
        }
    }
    felder.push_back(aktuell);
    return felder;
}

bool LiesProfilDatum(const std::string& text, const std::string& format, Date& out) {
    if (text.empty()) return false;
    if (format == "JJJJ-MM-TT") return Date::TryParseIso(text, out);
    if (format == "TT.MM.JJ") {
        // A two-digit year in a bank export is this century.
        std::string erweitert = text;
        if (erweitert.size() == 8) erweitert.insert(6, "20");
        return TryParseDateGerman(erweitert, out);
    }
    return TryParseDateGerman(text, out);
}

} // namespace

BankLeseBericht LiesBankCsv(const std::string& dateipfad, const CsvBankProfil& profil,
                            const std::string& iban, const std::string& waehrung) {
    BankLeseBericht bericht;
    bericht.format = BankFormat::Csv;

    std::string roh;
    if (!LiesDatei(dateipfad, roh, bericht.fehler)) return bericht;
    bericht.dateiHash = Fingerabdruck(roh);
    const std::string inhalt =
        (profil.encoding == "utf8" || SiehtWieUtf8Aus(roh)) ? roh : VonCp1252(roh);

    const UltraCanvas::MoneyStyle stil = profil.dezimaltrenner == "."
                                             ? UltraCanvas::MoneyStyle::Plain
                                             : UltraCanvas::MoneyStyle::Datev;

    Bankauszug auszug;
    auszug.iban     = iban;
    auszug.waehrung = waehrung;
    // A CSV export carries no balances, so the statement cannot check itself.
    // That is a property of the format, and it is said rather than left for
    // somebody to notice that one import was never verified.
    auszug.saldenGelesen = false;

    const std::vector<std::string> zeilen = Zeilen(inhalt);
    int ordnung = 0;
    for (size_t nr = 0; nr < zeilen.size(); ++nr) {
        if (static_cast<int>(nr) < profil.kopfzeilen) continue;
        const std::string& zeile = zeilen[nr];
        if (Trimme(zeile).empty()) continue;
        ++bericht.gelesen;
        ++ordnung;
        const int zeilenNummer = static_cast<int>(nr) + 1;

        const std::vector<std::string> felder = ZerlegeCsv(zeile, profil.trenner);
        auto feld = [&](int index) -> std::string {
            return (index >= 0 && index < static_cast<int>(felder.size()))
                       ? Trimme(felder[static_cast<size_t>(index)]) : std::string();
        };

        Bankumsatz umsatz;
        if (!LiesProfilDatum(feld(profil.spalteBuchungstag), profil.datumsformat,
                             umsatz.buchungstag)) {
            bericht.fehlerZeilen.push_back(
                "Zeile " + Zahl(zeilenNummer) + ": \"" + feld(profil.spalteBuchungstag) +
                "\" ist kein Datum im Format " + profil.datumsformat + ".");
            ++bericht.uebersprungen;
            continue;
        }
        if (!LiesProfilDatum(feld(profil.spalteValuta), profil.datumsformat, umsatz.valuta))
            umsatz.valuta = umsatz.buchungstag;

        const std::string zeilenWaehrung =
            profil.spalteWaehrung >= 0 && !feld(profil.spalteWaehrung).empty()
                ? feld(profil.spalteWaehrung) : waehrung;

        bool gelesen = false;
        if (profil.spalteBetrag >= 0) {
            Money betrag;
            if (Money::TryParse(feld(profil.spalteBetrag), betrag, zeilenWaehrung, stil)) {
                umsatz.betrag = betrag;
                gelesen = true;
                // A separate direction column overrides the sign in the amount;
                // a bank that gives both writes the amount unsigned.
                if (profil.spalteRichtung >= 0) {
                    const std::string richtung = feld(profil.spalteRichtung);
                    if (!richtung.empty())
                        umsatz.betrag = MitVorzeichen(betrag, richtung == profil.richtungEingang);
                }
            }
        } else {
            // Two columns: Soll (out) and Haben (in). Exactly one is filled.
            Money soll, haben;
            const bool hatSoll  = profil.spalteSoll >= 0 &&
                Money::TryParse(feld(profil.spalteSoll), soll, zeilenWaehrung, stil);
            const bool hatHaben = profil.spalteHaben >= 0 &&
                Money::TryParse(feld(profil.spalteHaben), haben, zeilenWaehrung, stil);
            if (hatHaben && haben.Minor() != 0)     { umsatz.betrag = MitVorzeichen(haben, true);  gelesen = true; }
            else if (hatSoll && soll.Minor() != 0)  { umsatz.betrag = MitVorzeichen(soll, false); gelesen = true; }
        }
        if (!gelesen) {
            bericht.fehlerZeilen.push_back(
                "Zeile " + Zahl(zeilenNummer) + ": der Betrag ist nicht lesbar.");
            ++bericht.uebersprungen;
            continue;
        }

        umsatz.gegenName    = feld(profil.spalteGegenName);
        umsatz.gegenIban    = feld(profil.spalteGegenIban);
        umsatz.gegenBic     = feld(profil.spalteGegenBic);
        umsatz.buchungstext = feld(profil.spalteBuchungstext);

        const SepaTags tags = ZerlegeSepaTags(feld(profil.spalteVerwendungszweck));
        umsatz.verwendungszweck = tags.verwendungszweck;
        umsatz.endToEndId       = tags.endToEndId;
        umsatz.mandatsreferenz  = tags.mandatsreferenz;
        umsatz.glaeubigerId     = tags.glaeubigerId;
        if (umsatz.gegenName.empty()) umsatz.gegenName = tags.abweichenderName;

        // A CSV never carries the bank's own reference, so the key is always
        // derived - and it has to include the position, because a bank export
        // can legitimately hold two identical rows on one day.
        const std::string material =
            iban + "|" + Zahl(ordnung) + "|" + umsatz.buchungstag.ToIso() + "|" +
            Zahl(umsatz.betrag.Minor()) + "|" + umsatz.gegenIban + "|" +
            umsatz.gegenName + "|" + umsatz.verwendungszweck;
        umsatz.referenz = "abgeleitet:" + Fingerabdruck(material).substr(0, 32);

        auszug.umsaetze.push_back(std::move(umsatz));
        ++bericht.uebernommen;
    }

    if (!auszug.umsaetze.empty()) {
        auszug.von = auszug.umsaetze.front().buchungstag;
        auszug.bis = auszug.umsaetze.front().buchungstag;
        for (const Bankumsatz& u : auszug.umsaetze) {
            if (u.buchungstag < auszug.von) auszug.von = u.buchungstag;
            if (auszug.bis < u.buchungstag) auszug.bis = u.buchungstag;
        }
    }
    bericht.auszuege.push_back(std::move(auszug));

    bericht.warnungen.push_back(
        "Eine CSV-Datei enthält keine Salden, deshalb kann dieser Import sich nicht "
        "selbst prüfen. Bei CAMT.053 rechnet der Import Anfangssaldo plus alle "
        "Buchungen gegen den Endsaldo; hier ist das nicht möglich.");
    if (!bericht.fehlerZeilen.empty()) {
        bericht.warnungen.push_back(
            Zahl(static_cast<int64_t>(bericht.fehlerZeilen.size())) +
            " Zeile(n) konnten nicht gelesen werden und fehlen im Import.");
    }
    bericht.ok = bericht.uebernommen > 0;
    if (!bericht.ok && bericht.fehler.empty())
        bericht.fehler = "Die Datei enthält keine lesbare Buchung.";
    return bericht;
}

// ===== PICKING A READER =====

BankLeseBericht LiesBankdatei(const std::string& dateipfad, const CsvBankProfil& csvProfil,
                              const std::string& iban, const std::string& waehrung) {
    // By content, not by extension: a bank that names a CAMT file ".txt" is
    // not an unusual bank, and a wrong reader gives a confusing error rather
    // than the obvious one.
    std::string roh;
    BankLeseBericht bericht;
    if (!LiesDatei(dateipfad, roh, bericht.fehler)) return bericht;

    const std::string anfang = roh.substr(0, std::min<size_t>(roh.size(), 4096));
    if (anfang.find("<?xml") != std::string::npos ||
        anfang.find("BkToCstmrStmt") != std::string::npos ||
        anfang.find("Document") != std::string::npos)
        return LiesCamt053(dateipfad);
    if (anfang.find(":20:") != std::string::npos || anfang.find(":61:") != std::string::npos ||
        anfang.find(":25:") != std::string::npos)
        return LiesMt940(dateipfad);
    return LiesBankCsv(dateipfad, csvProfil, iban, waehrung);
}

// ===== AUTOMATIC ASSIGNMENT =====

std::string ZuordnungGueteToText(ZuordnungGuete guete) {
    switch (guete) {
        case ZuordnungGuete::Sicher:         return "sicher";
        case ZuordnungGuete::Wahrscheinlich: return "wahrscheinlich";
        case ZuordnungGuete::Moeglich:       return "moeglich";
    }
    return "moeglich";
}

std::string NormalisiereNummer(const std::string& text) {
    std::string aus;
    aus.reserve(text.size());
    for (unsigned char c : text) {
        if (c >= 'a' && c <= 'z')      aus.push_back(static_cast<char>(c - 'a' + 'A'));
        else if (c >= 'A' && c <= 'Z') aus.push_back(static_cast<char>(c));
        else if (c >= '0' && c <= '9') aus.push_back(static_cast<char>(c));
    }
    return aus;
}

namespace {

// A number short enough to appear by accident is not evidence. Four characters
// is the shortest that carries any: "1" is in nearly every remittance line,
// and a matcher that treated it as a hit would propose the same document for
// everything and be trusted exactly once.
constexpr size_t kMindestNummernlaenge = 4;

bool NummerKommtVor(const std::string& nummer, const std::string& heuhaufen) {
    const std::string n = NormalisiereNummer(nummer);
    if (n.size() < kMindestNummernlaenge) return false;
    return heuhaufen.find(n) != std::string::npos;
}

int64_t Betrag(const Money& m) { return m.Minor() < 0 ? -m.Minor() : m.Minor(); }

} // namespace

std::vector<Zuordnungsvorschlag> SchlageZuordnungVor(
        const Bankumsatz& umsatz, const std::vector<ZuordnungKandidat>& kandidaten) {
    std::vector<Zuordnungsvorschlag> vorschlaege;
    if (!umsatz.betrag.Valid() || umsatz.betrag.Minor() == 0) return vorschlaege;

    const bool geldEin = umsatz.betrag.Minor() > 0;
    const int64_t bankbetrag = Betrag(umsatz.betrag);

    // Everything the payer wrote, in one normalised haystack: the remittance,
    // the end-to-end reference and the bank's own text. A document number can
    // arrive in any of them.
    const std::string heuhaufen = NormalisiereNummer(
        umsatz.verwendungszweck + " " + umsatz.endToEndId + " " + umsatz.buchungstext);
    const std::string gegenIban = NormalisiereNummer(umsatz.gegenIban);
    const std::string gegenName = NormalisiereNummer(umsatz.gegenName);

    for (const ZuordnungKandidat& k : kandidaten) {
        // **The direction is a precondition, not a score.** Money coming in
        // cannot pay an invoice we received; money going out cannot settle one
        // we sent. A candidate on the wrong side is not a weak match.
        // Money in settles a document that takes no money out (a sales
        // invoice); money out settles one that does (a purchase invoice, or a
        // credit note we owe). So the two flags being *equal* is the
        // mismatch: money arrived AND settling it would take money out.
        if (k.geldAbgang == geldEin) continue;
        if (!k.offen.Valid() || k.offen.Minor() <= 0) continue;

        Zuordnungsvorschlag v;
        v.belegId     = k.belegId;
        v.belegnummer = k.belegnummer;

        const int64_t offen = Betrag(k.offen);
        const bool exakt  = bankbetrag == offen;
        const bool passt  = bankbetrag <= offen;      // a part payment fits too

        // The document number in the remittance is the strongest signal there
        // is: it is what the payer's own system put there on purpose.
        const bool nummerGefunden = NummerKommtVor(k.belegnummer, heuhaufen);
        const bool externGefunden = !k.externeNummer.empty() &&
                                    NummerKommtVor(k.externeNummer, heuhaufen);
        if (nummerGefunden) {
            v.punkte += 50;
            v.gruende.push_back("Die Belegnummer " + k.belegnummer +
                                " steht im Verwendungszweck.");
        } else if (externGefunden) {
            v.punkte += 45;
            v.gruende.push_back("Die Rechnungsnummer des Partners (" + k.externeNummer +
                                ") steht im Verwendungszweck.");
        }

        if (exakt) {
            v.punkte += 30;
            v.gruende.push_back("Der Betrag entspricht genau dem offenen Betrag " +
                                k.offen.ToString() + ".");
        } else if (passt) {
            v.punkte += 10;
            v.teilzahlung = true;
            v.gruende.push_back("Der Betrag ist kleiner als der offene Betrag " +
                                k.offen.ToString() + " - das wäre eine Teilzahlung.");
        } else {
            // More money than is open. Not impossible (an overpayment), but it
            // cannot be assigned in full and is never a confident proposal.
            v.punkte -= 15;
            v.gruende.push_back("Der Betrag ist größer als der offene Betrag " +
                                k.offen.ToString() + ".");
        }

        if (!gegenIban.empty() && !k.partnerIban.empty() &&
            gegenIban == NormalisiereNummer(k.partnerIban)) {
            v.punkte += 25;
            v.gruende.push_back("Die IBAN gehört zu " + k.partnerName + ".");
        } else if (!gegenName.empty() && !k.partnerName.empty()) {
            const std::string partner = NormalisiereNummer(k.partnerName);
            if (partner.size() >= 4 && gegenName.find(partner) != std::string::npos) {
                v.punkte += 15;
                v.gruende.push_back("Der Name auf dem Kontoauszug enthält " +
                                    k.partnerName + ".");
            }
        }

        // A payment before the invoice exists is possible (a deposit) but rare;
        // one long after it is normal. The window is generous on the late side
        // and narrow on the early one.
        if (k.belegdatum.Valid() && umsatz.buchungstag.Valid()) {
            const int64_t tage = k.belegdatum.DaysUntil(umsatz.buchungstag);
            if (tage >= 0 && tage <= 90) {
                v.punkte += 10;
            } else if (tage < -5) {
                v.punkte -= 20;
                v.gruende.push_back("Die Zahlung liegt vor dem Belegdatum " +
                                    FormatDateGerman(k.belegdatum) + ".");
            } else if (tage > 365) {
                v.punkte -= 10;
                v.gruende.push_back("Zwischen Beleg und Zahlung liegt mehr als ein Jahr.");
            }
        }

        // Certain means: the payer named the document **and** the amount is
        // right. Either alone is not enough - a correct amount with no
        // reference is guesswork when two invoices happen to cost the same.
        if ((nummerGefunden || externGefunden) && exakt)
            v.guete = ZuordnungGuete::Sicher;
        else if (v.punkte >= 45)
            v.guete = ZuordnungGuete::Wahrscheinlich;
        else
            v.guete = ZuordnungGuete::Moeglich;

        // What would actually be assigned: never more than is open, and never
        // more than arrived.
        v.betrag = Money::FromMinor(std::min(bankbetrag, offen), umsatz.betrag.Currency());

        if (v.punkte >= 25) vorschlaege.push_back(std::move(v));
    }

    std::stable_sort(vorschlaege.begin(), vorschlaege.end(),
                     [](const Zuordnungsvorschlag& a, const Zuordnungsvorschlag& b) {
                         return a.punkte > b.punkte;
                     });
    return vorschlaege;
}

} // namespace UltraFIBU
