// Apps/UltraFIBU/engine/UltraFIBUUstIdNr.cpp
// Formats for all member states, check digits for the fifteen whose rule is
// unambiguous. Each algorithm carries the shape of the rule in a comment, so a
// future reader can check it against the member state's own description
// instead of trusting the code.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUUstIdNr.h"

#include <cstdint>

namespace UltraFIBU {

namespace {

bool IsDigit(char c)  { return c >= '0' && c <= '9'; }
bool IsAlpha(char c)  { return c >= 'A' && c <= 'Z'; }
int  DigitAt(const std::string& s, size_t i) { return s[i] - '0'; }

bool AllDigits(const std::string& s, size_t from, size_t count) {
    if (from + count > s.size()) return false;
    for (size_t i = from; i < from + count; ++i) if (!IsDigit(s[i])) return false;
    return true;
}

// The numeric value of a run of digits; caller has checked they are digits.
int64_t NumberAt(const std::string& s, size_t from, size_t count) {
    int64_t value = 0;
    for (size_t i = from; i < from + count; ++i) value = value * 10 + DigitAt(s, i);
    return value;
}

// Sum of the digits of 2*d, i.e. the Luhn doubling step.
int Doubled(int d) { const int x = 2 * d; return x > 9 ? x - 9 : x; }

// ---- national check digits -------------------------------------------------
// `body` is the number without its country prefix in every function below.

// DE - nine digits, "Modulo 11, 10": the procedure the Bundeszentralamt
// publishes for the USt-IdNr. P starts at 10; for each of the first eight
// digits, M = (d + P) mod 10 (0 becomes 10) and P = (2 * M) mod 11; the check
// digit is 11 - P, with 10 collapsing to 0.
bool CheckDE(const std::string& body) {
    int p = 10;
    for (size_t i = 0; i < 8; ++i) {
        int m = (DigitAt(body, i) + p) % 10;
        if (m == 0) m = 10;
        p = (2 * m) % 11;
    }
    int check = 11 - p;
    if (check == 10) check = 0;
    return check == DigitAt(body, 8);
}

// AT - 'U' plus eight digits. The seven leading digits are summed with every
// second one doubled and digit-summed; the check digit is (96 - S) mod 10.
bool CheckAT(const std::string& body) {
    // body: "U" + 8 digits
    int sum = 0;
    for (size_t i = 1; i <= 7; ++i) {
        const int d = DigitAt(body, i);
        sum += (i % 2 == 0) ? Doubled(d) : d;   // digits 2, 4, 6 (1-based within the seven) double
    }
    const int check = (96 - sum) % 10;
    return check == DigitAt(body, 8);
}

// BE - ten digits; the last two are 97 minus the first eight modulo 97.
bool CheckBE(const std::string& body) {
    const int64_t base  = NumberAt(body, 0, 8);
    const int64_t check = NumberAt(body, 8, 2);
    return check == 97 - (base % 97);
}

// DK - eight digits weighted 2,7,6,5,4,3,2,1; the weighted sum is a multiple
// of 11.
bool CheckDK(const std::string& body) {
    static const int kWeights[8] = { 2, 7, 6, 5, 4, 3, 2, 1 };
    int sum = 0;
    for (size_t i = 0; i < 8; ++i) sum += kWeights[i] * DigitAt(body, i);
    return sum % 11 == 0;
}

// FI - eight digits; the first seven weighted 7,9,10,5,8,4,2, the check digit
// is 11 - (sum mod 11), where a remainder of 1 makes the number invalid.
bool CheckFI(const std::string& body) {
    static const int kWeights[7] = { 7, 9, 10, 5, 8, 4, 2 };
    int sum = 0;
    for (size_t i = 0; i < 7; ++i) sum += kWeights[i] * DigitAt(body, i);
    const int rem = sum % 11;
    if (rem == 1) return false;
    const int check = rem == 0 ? 0 : 11 - rem;
    return check == DigitAt(body, 7);
}

// EL (Greece) - nine digits; the first eight weighted by descending powers of
// two from 256, the check digit is (sum mod 11) with 10 collapsing to 0.
bool CheckEL(const std::string& body) {
    static const int kWeights[8] = { 256, 128, 64, 32, 16, 8, 4, 2 };
    int sum = 0;
    for (size_t i = 0; i < 8; ++i) sum += kWeights[i] * DigitAt(body, i);
    int check = sum % 11;
    if (check > 9) check = 0;
    return check == DigitAt(body, 8);
}

// FR - two characters then a nine-digit SIREN. When the two are digits they
// are a key over the SIREN: (12 + 3 * (SIREN mod 97)) mod 97. When they are
// not (the old alphanumeric keys), there is no rule to apply here.
bool CheckFR(const std::string& body, bool& applicable) {
    applicable = AllDigits(body, 0, 2);
    if (!applicable) return true;
    const int64_t key   = NumberAt(body, 0, 2);
    const int64_t siren = NumberAt(body, 2, 9);
    return key == (12 + 3 * (siren % 97)) % 97;
}

// HU - eight digits; the first seven weighted 9,7,3,1,9,7,3 and the check
// digit completes the sum to a multiple of ten.
bool CheckHU(const std::string& body) {
    static const int kWeights[7] = { 9, 7, 3, 1, 9, 7, 3 };
    int sum = 0;
    for (size_t i = 0; i < 7; ++i) sum += kWeights[i] * DigitAt(body, i);
    const int check = (10 - (sum % 10)) % 10;
    return check == DigitAt(body, 7);
}

// IT - eleven digits, Luhn over the first ten (even 1-based positions
// doubled), the check digit completing to a multiple of ten.
bool CheckIT(const std::string& body) {
    int sum = 0;
    for (size_t i = 0; i < 10; ++i) {
        const int d = DigitAt(body, i);
        sum += (i % 2 == 1) ? Doubled(d) : d;
    }
    const int check = (10 - (sum % 10)) % 10;
    return check == DigitAt(body, 10);
}

// LU - eight digits; the last two are the first six modulo 89.
bool CheckLU(const std::string& body) {
    return NumberAt(body, 0, 6) % 89 == NumberAt(body, 6, 2);
}

// PL - ten digits; the first nine weighted 6,5,7,2,3,4,5,6,7, the check digit
// is the sum modulo 11, and a remainder of 10 makes the number invalid.
bool CheckPL(const std::string& body) {
    static const int kWeights[9] = { 6, 5, 7, 2, 3, 4, 5, 6, 7 };
    int sum = 0;
    for (size_t i = 0; i < 9; ++i) sum += kWeights[i] * DigitAt(body, i);
    const int check = sum % 11;
    if (check == 10) return false;
    return check == DigitAt(body, 9);
}

// PT - nine digits; the first eight weighted 9..2, the check digit is
// 11 - (sum mod 11) with 10 and 11 collapsing to 0.
bool CheckPT(const std::string& body) {
    int sum = 0;
    for (size_t i = 0; i < 8; ++i) sum += (9 - static_cast<int>(i)) * DigitAt(body, i);
    int check = 11 - (sum % 11);
    if (check > 9) check = 0;
    return check == DigitAt(body, 8);
}

// SE - twelve digits ending in a two-digit branch number (01 for the VAT
// registration itself); Luhn over the first ten.
bool CheckSE(const std::string& body) {
    int sum = 0;
    for (size_t i = 0; i < 9; ++i) {
        const int d = DigitAt(body, i);
        sum += (i % 2 == 0) ? Doubled(d) : d;
    }
    const int check = (10 - (sum % 10)) % 10;
    return check == DigitAt(body, 9);
}

// SI - eight digits; the first seven weighted 8..2, the check digit is
// 11 - (sum mod 11), where 10 becomes 0 and 11 is invalid.
bool CheckSI(const std::string& body) {
    int sum = 0;
    for (size_t i = 0; i < 7; ++i) sum += (8 - static_cast<int>(i)) * DigitAt(body, i);
    int check = 11 - (sum % 11);
    if (check == 11) return false;
    if (check == 10) check = 0;
    return check == DigitAt(body, 7);
}

// SK - ten digits; the whole number is a multiple of 11.
bool CheckSK(const std::string& body) {
    return NumberAt(body, 0, 10) % 11 == 0;
}

// EE - nine digits weighted 3,7,1,3,7,1,3,7; the check digit completes the sum
// to a multiple of ten.
bool CheckEE(const std::string& body) {
    static const int kWeights[8] = { 3, 7, 1, 3, 7, 1, 3, 7 };
    int sum = 0;
    for (size_t i = 0; i < 8; ++i) sum += kWeights[i] * DigitAt(body, i);
    const int check = (10 - (sum % 10)) % 10;
    return check == DigitAt(body, 8);
}

// ---- formats ---------------------------------------------------------------
// The shape of each member state's number, without the prefix. Where a state
// issues several lengths, all of them are listed.
bool FormatOkFor(const std::string& land, const std::string& body) {
    const size_t n = body.size();
    auto digits = [&](size_t count) { return n == count && AllDigits(body, 0, count); };

    if (land == "AT") return n == 9 && body[0] == 'U' && AllDigits(body, 1, 8);
    if (land == "BE") return digits(10) && (body[0] == '0' || body[0] == '1');
    if (land == "BG") return digits(9) || digits(10);
    if (land == "CY") return n == 9 && AllDigits(body, 0, 8) && IsAlpha(body[8]);
    if (land == "CZ") return digits(8) || digits(9) || digits(10);
    if (land == "DE") return digits(9);
    if (land == "DK") return digits(8);
    if (land == "EE") return digits(9);
    if (land == "EL") return digits(9);
    if (land == "ES") {
        if (n != 9) return false;
        // A letter or a digit at either end, digits in between - the several
        // Spanish schemes all fit that.
        const bool firstOk = IsAlpha(body[0]) || IsDigit(body[0]);
        const bool lastOk  = IsAlpha(body[8]) || IsDigit(body[8]);
        return firstOk && lastOk && AllDigits(body, 1, 7);
    }
    if (land == "FI") return digits(8);
    if (land == "FR") {
        if (n != 11) return false;
        for (size_t i = 0; i < 2; ++i) if (!IsDigit(body[i]) && !IsAlpha(body[i])) return false;
        return AllDigits(body, 2, 9);
    }
    if (land == "HR") return digits(11);
    if (land == "HU") return digits(8);
    if (land == "IE") {
        // 7 digits + 1-2 letters, or digit/letter/5 digits/letter.
        if (n == 8) return (AllDigits(body, 0, 7) && IsAlpha(body[7])) ||
                           (IsDigit(body[0]) && (IsAlpha(body[1]) || body[1] == '+' || body[1] == '*') &&
                            AllDigits(body, 2, 5) && IsAlpha(body[7]));
        if (n == 9) return AllDigits(body, 0, 7) && IsAlpha(body[7]) && IsAlpha(body[8]);
        return false;
    }
    if (land == "IT") return digits(11);
    if (land == "LT") return digits(9) || digits(12);
    if (land == "LU") return digits(8);
    if (land == "LV") return digits(11);
    if (land == "MT") return digits(8);
    if (land == "NL") return n == 12 && AllDigits(body, 0, 9) && body[9] == 'B' &&
                             AllDigits(body, 10, 2);
    if (land == "PL") return digits(10);
    if (land == "PT") return digits(9);
    if (land == "RO") {
        if (n < 2 || n > 10) return false;
        return AllDigits(body, 0, n);
    }
    if (land == "SE") return digits(12);
    if (land == "SI") return digits(8);
    if (land == "SK") return digits(10);
    if (land == "XI") return digits(5) || digits(9) || digits(12);   // Northern Ireland, GB shapes
    return false;
}

} // namespace

std::string UstIdNrStatusToText(UstIdNrStatus status) {
    switch (status) {
        case UstIdNrStatus::Leer:              return "leer";
        case UstIdNrStatus::LandUnbekannt:     return "land-unbekannt";
        case UstIdNrStatus::FormatUngueltig:   return "format-ungueltig";
        case UstIdNrStatus::FormatOk:          return "format-ok";
        case UstIdNrStatus::PruefzifferOk:     return "pruefziffer-ok";
        case UstIdNrStatus::PruefzifferFalsch: return "pruefziffer-falsch";
    }
    return "leer";
}

std::string NormalisiereUstIdNr(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        if (c >= 'a' && c <= 'z') out += static_cast<char>(c - 'a' + 'A');
        else if (IsAlpha(c) || IsDigit(c)) out += c;
    }
    return out;
}

const std::vector<std::string>& EuLaender() {
    static const std::vector<std::string> kLaender = {
        "AT", "BE", "BG", "CY", "CZ", "DE", "DK", "EE", "EL", "ES",
        "FI", "FR", "HR", "HU", "IE", "IT", "LT", "LU", "LV", "MT",
        "NL", "PL", "PT", "RO", "SE", "SI", "SK"
    };
    return kLaender;
}

bool IstEuLand(const std::string& iso2) {
    if (iso2 == "GR") return true;          // the ISO code for Greece; VAT law writes EL
    for (const std::string& land : EuLaender()) if (land == iso2) return true;
    return false;
}

UstIdNrPruefung PruefeUstIdNr(const std::string& input) {
    UstIdNrPruefung result;
    result.normalisiert = NormalisiereUstIdNr(input);

    if (result.normalisiert.empty()) {
        result.status  = UstIdNrStatus::Leer;
        result.hinweis = "Keine USt-IdNr. angegeben.";
        return result;
    }
    if (result.normalisiert.size() < 4) {
        result.status  = UstIdNrStatus::FormatUngueltig;
        result.hinweis = "Die USt-IdNr. ist zu kurz.";
        return result;
    }

    std::string land = result.normalisiert.substr(0, 2);
    if (land == "GR") land = "EL";                       // Greece files as EL
    const std::string body = result.normalisiert.substr(2);

    if (!IstEuLand(land) && !IstNordirland(land)) {
        result.status  = UstIdNrStatus::LandUnbekannt;
        result.hinweis = "Das Länderkürzel \"" + land +
                         "\" gehört nicht zum EU-Umsatzsteuergebiet.";
        return result;
    }
    result.land = land;

    if (!FormatOkFor(land, body)) {
        result.status  = UstIdNrStatus::FormatUngueltig;
        result.hinweis = "Der Aufbau passt nicht zum Format von " + land + ".";
        return result;
    }

    bool hasRule = true;
    bool ok = true;
    if      (land == "DE") ok = CheckDE(body);
    else if (land == "AT") ok = CheckAT(body);
    else if (land == "BE") ok = CheckBE(body);
    else if (land == "DK") ok = CheckDK(body);
    else if (land == "EE") ok = CheckEE(body);
    else if (land == "EL") ok = CheckEL(body);
    else if (land == "FI") ok = CheckFI(body);
    else if (land == "HU") ok = CheckHU(body);
    else if (land == "IT") ok = CheckIT(body);
    else if (land == "LU") ok = CheckLU(body);
    else if (land == "PL") ok = CheckPL(body);
    else if (land == "PT") ok = CheckPT(body);
    else if (land == "SE") ok = CheckSE(body) && body.substr(10) == "01";
    else if (land == "SI") ok = CheckSI(body);
    else if (land == "SK") ok = CheckSK(body);
    else if (land == "FR") ok = CheckFR(body, hasRule);
    else                   hasRule = false;

    if (!hasRule) {
        result.status  = UstIdNrStatus::FormatOk;
        result.hinweis = "Aufbau korrekt. Für " + land +
                         " wird keine Prüfziffer berechnet - die Bestätigung "
                         "erfolgt über das BZSt.";
        return result;
    }
    if (!ok) {
        result.status  = UstIdNrStatus::PruefzifferFalsch;
        result.hinweis = "Die Prüfziffer stimmt nicht - bitte die Nummer prüfen.";
        return result;
    }
    result.status  = UstIdNrStatus::PruefzifferOk;
    result.hinweis = "Aufbau und Prüfziffer sind korrekt. Rechtssicher ist nur die "
                     "qualifizierte Bestätigung des BZSt.";
    return result;
}

} // namespace UltraFIBU
