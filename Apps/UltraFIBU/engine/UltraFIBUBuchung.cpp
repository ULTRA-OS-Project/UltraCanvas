// Apps/UltraFIBU/engine/UltraFIBUBuchung.cpp
// Enumeration text, the canonical form of a posting, and the chain step.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUBuchung.h"

#include <UltraCrypt/UltraCryptCore.h>

namespace UltraFIBU {

namespace {

// Locale-free digits. The C library's number formatting is exactly what must
// not touch a value that is about to be hashed.
std::string Ziffern(int64_t value) {
    std::string digits;
    int64_t v = value < 0 ? -value : value;
    if (v == 0) digits = "0";
    while (v > 0) {
        digits.insert(digits.begin(), static_cast<char>('0' + (v % 10)));
        v /= 10;
    }
    if (value < 0) digits.insert(digits.begin(), '-');
    return digits;
}

// One length-prefixed field: "12:Buchungstext;". The length is the byte count,
// so a value containing ':' or ';' - or a UTF-8 umlaut - cannot shift the
// parse or collide with a different row.
void Feld(std::string& out, const std::string& value) {
    out += Ziffern(static_cast<int64_t>(value.size()));
    out += ':';
    out += value;
    out += ';';
}

void Feld(std::string& out, int64_t value) {
    Feld(out, Ziffern(value));
}

// An amount contributes its minor units and its currency, never its formatted
// text: "1.234,56 EUR" and "1234.56 EUR" are the same money and must hash the
// same. An invalid amount hashes as the word "ungueltig" rather than as zero,
// so a row that lost its amount cannot silently match one that was always zero.
void Feld(std::string& out, const Money& value) {
    if (!value.Valid()) { Feld(out, std::string("ungueltig")); return; }
    Feld(out, value.Minor());
    Feld(out, value.Currency());
}

} // namespace

// ===== ENUMERATION TEXT =====

std::string SollHabenToText(SollHaben sh) {
    return sh == SollHaben::Soll ? "S" : "H";
}

bool SollHabenFromText(const std::string& text, SollHaben& out) {
    if (text == "S" || text == "s" || text == "Soll" || text == "soll") {
        out = SollHaben::Soll;
        return true;
    }
    if (text == "H" || text == "h" || text == "Haben" || text == "haben") {
        out = SollHaben::Haben;
        return true;
    }
    return false;
}

SollHaben SollHabenUmgekehrt(SollHaben sh) {
    return sh == SollHaben::Soll ? SollHaben::Haben : SollHaben::Soll;
}

std::string SteuerSeiteToText(SteuerSeite seite) {
    switch (seite) {
        case SteuerSeite::Konto:      return "konto";
        case SteuerSeite::Gegenkonto: return "gegenkonto";
        case SteuerSeite::Keine:      break;
    }
    return "keine";
}

bool SteuerSeiteFromText(const std::string& text, SteuerSeite& out) {
    if (text == "konto")      { out = SteuerSeite::Konto;      return true; }
    if (text == "gegenkonto") { out = SteuerSeite::Gegenkonto; return true; }
    if (text == "keine" || text.empty()) { out = SteuerSeite::Keine; return true; }
    return false;
}

// ===== THE TWO SIDES OF A TAXED POSTING =====

std::string Buchung::NettoKonto() const {
    switch (steuerSeite) {
        case SteuerSeite::Konto:      return konto;
        case SteuerSeite::Gegenkonto: return gegenkonto;
        case SteuerSeite::Keine:      break;
    }
    return std::string();
}

std::string Buchung::BruttoKonto() const {
    switch (steuerSeite) {
        case SteuerSeite::Konto:      return gegenkonto;
        case SteuerSeite::Gegenkonto: return konto;
        case SteuerSeite::Keine:      break;
    }
    return std::string();
}

// ===== THE HASH CHAIN =====

std::string KanonischeForm(const Buchung& buchung) {
    // Field order is part of the format: changing it invalidates every stored
    // hash. If a future schema version adds a field, it is appended here and
    // the chain is re-computed under a new version tag - which is why the
    // version leads.
    std::string out;
    Feld(out, std::string("UltraFIBU-Buchung-v1"));
    Feld(out, buchung.mandantId);
    Feld(out, buchung.geschaeftsjahrId);
    Feld(out, static_cast<int64_t>(buchung.periode));
    Feld(out, buchung.belegdatum.ToIso());
    Feld(out, buchung.belegId);
    Feld(out, buchung.belegfeld1);
    Feld(out, buchung.belegfeld2);
    Feld(out, buchung.umsatz);
    Feld(out, SollHabenToText(buchung.sollHaben));
    Feld(out, buchung.konto);
    Feld(out, buchung.gegenkonto);
    Feld(out, buchung.buSchluessel);
    Feld(out, buchung.steuerschluessel);
    Feld(out, SteuerSeiteToText(buchung.steuerSeite));
    Feld(out, static_cast<int64_t>(buchung.satzPromille));
    Feld(out, buchung.netto);
    Feld(out, buchung.steuer);
    Feld(out, buchung.steuerkonto);
    Feld(out, buchung.buchungstext);
    Feld(out, buchung.kost1);
    Feld(out, buchung.kost2);
    Feld(out, buchung.waehrung);
    Feld(out, buchung.stornoVon);
    Feld(out, buchung.erfasstVon);
    Feld(out, buchung.erfasstVonName);
    Feld(out, buchung.erfasstAm);
    return out;
}

std::string BerechneHash(const std::string& prevHash, const Buchung& buchung) {
    std::string payload = prevHash;
    payload += KanonischeForm(buchung);

    std::vector<uint8_t> digest;
    const UltraCryptResult hashed = UltraCrypt_Hash(
        UltraCryptHashAlgorithm::SHA256, payload.data(), payload.size(), digest);
    if (!hashed) return std::string();
    return UltraCrypt_ToHex(digest);
}

} // namespace UltraFIBU
