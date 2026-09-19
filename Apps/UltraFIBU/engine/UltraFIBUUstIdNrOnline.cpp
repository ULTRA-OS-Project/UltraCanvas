// Apps/UltraFIBU/engine/UltraFIBUUstIdNrOnline.cpp
// Request building and response reading for VIES and BZSt eVatR. No sockets
// here - the transport is injected (see the header).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUUstIdNrOnline.h"

#include "DataFormats/UltraCanvasJSON.h"

namespace UltraFIBU {

namespace {

using UltraCanvas::JSONValue;

// A JSON string literal with the characters JSON forbids escaped. Written out
// rather than pulled from a writer, because these two requests are the only
// JSON this file produces and a dependency on the serialiser would buy nothing.
std::string JsonString(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) out += ' ';
                else out += c;
        }
    }
    out += "\"";
    return out;
}

// Split a normalised VAT number into its country prefix and the rest.
void SplitVatNumber(const std::string& input, std::string& outLand, std::string& outNummer) {
    const std::string normalised = NormalisiereUstIdNr(input);
    if (normalised.size() < 3) { outLand.clear(); outNummer = normalised; return; }
    outLand   = normalised.substr(0, 2);
    outNummer = normalised.substr(2);
    // VIES and the BZSt both expect Greece as EL, not GR.
    if (outLand == "GR") outLand = "EL";
}

// VIES dates arrive as "2026-06-12+02:00" or "2026-06-12T00:00:00"; only the
// date part is of interest, and Date::TryParseIso wants exactly eight digits.
Date DateFromAuthority(const std::string& text) {
    Date date;
    std::string digits;
    for (char c : text) {
        if (c >= '0' && c <= '9') digits += c;
        else if (c == '-' ) continue;
        else break;                      // stop at 'T', '+', a space or anything else
        if (digits.size() == 8) break;
    }
    if (digits.size() == 8) Date::TryParseIso(digits, date);
    return date;
}

// The BZSt answers each supplied detail with a code; "A" for a match and "B"
// for a mismatch are the codes the retired interface used and the ones its
// successor is described as keeping. [unverified]
Abgleich AbgleichFromCode(const std::string& code) {
    if (code.empty()) return Abgleich::NichtAngefragt;
    if (code == "A")  return Abgleich::Stimmt;
    if (code == "B")  return Abgleich::StimmtNicht;
    if (code == "C" || code == "D") return Abgleich::NichtGeprueft;
    return Abgleich::NichtGeprueft;
}

} // namespace

std::string BestaetigungsQuelleToText(BestaetigungsQuelle quelle) {
    return quelle == BestaetigungsQuelle::BzstEvatr ? "bzst-evatr" : "vies";
}

std::string AbgleichToText(Abgleich abgleich) {
    switch (abgleich) {
        case Abgleich::NichtAngefragt: return "nicht-angefragt";
        case Abgleich::Stimmt:         return "stimmt";
        case Abgleich::StimmtNicht:    return "stimmt-nicht";
        case Abgleich::NichtGeprueft:  return "nicht-geprueft";
    }
    return "nicht-angefragt";
}

std::string Bestaetigung::Zusammenfassung() const {
    if (!ok)
        return "Die Abfrage war nicht erfolgreich" + (fehler.empty() ? "." : ": " + fehler);
    if (!gueltig)
        return "Die USt-IdNr. " + land + nummer + " ist nicht (mehr) gültig.";
    if (IstQualifiziert())
        return "Qualifiziert bestätigt am " + FormatDateGerman(anfrageDatum) +
               " durch das BZSt" + (anfrageId.empty() ? "" : " (Anfrage " + anfrageId + ")") + ".";
    if (quelle == BestaetigungsQuelle::BzstEvatr)
        return "Die USt-IdNr. ist gültig, aber nicht alle angefragten Angaben wurden "
               "bestätigt - für § 6a UStG reicht das nicht.";
    return "Die USt-IdNr. ist laut VIES gültig" +
           (anfrageId.empty()
                ? " (einfache Abfrage ohne Konsultationsnummer)."
                : " - Konsultationsnummer " + anfrageId + ".") +
           " Rechtssicher für innergemeinschaftliche Lieferungen ist nur die "
           "qualifizierte Bestätigung des BZSt.";
}

// ===== VIES =====

std::string ViesUrl(const std::string& land, const std::string& nummer) {
    return "https://ec.europa.eu/taxation_customs/vies/rest-api/ms/" + land +
           "/vat/" + nummer;
}

std::string ViesPostUrl() {
    return "https://ec.europa.eu/taxation_customs/vies/rest-api/check-vat-number";
}

std::string ViesPostBody(const Bestaetigungsanfrage& anfrage) {
    std::string land, nummer;
    SplitVatNumber(anfrage.ustIdNr, land, nummer);
    std::string eigenesLand, eigeneNummer;
    SplitVatNumber(anfrage.eigeneUstIdNr, eigenesLand, eigeneNummer);

    std::string body = "{";
    body += "\"countryCode\":" + JsonString(land);
    body += ",\"vatNumber\":" + JsonString(nummer);
    if (!eigenesLand.empty()) {
        // Supplying the enquirer's own number is what makes VIES issue a
        // consultation number, so it is sent whenever it is known.
        body += ",\"requesterMemberStateCode\":" + JsonString(eigenesLand);
        body += ",\"requesterNumber\":" + JsonString(eigeneNummer);
    }
    body += "}";
    return body;
}

Bestaetigung ViesAntwortLesen(const std::string& json, const std::string& land,
                              const std::string& nummer) {
    Bestaetigung result;
    result.quelle = BestaetigungsQuelle::Vies;
    result.land   = land;
    result.nummer = nummer;
    result.protokoll = json;                       // the evidence, verbatim

    UltraCanvas::JSONParseResult parse;
    const JSONValue root = UltraCanvas::JSON::Parse(json, &parse);
    if (!parse.success || !root.IsObject()) {
        result.fehler = "Die Antwort von VIES war nicht lesbar" +
                        (parse.errorMessage.empty() ? "." : ": " + parse.errorMessage);
        return result;
    }

    // VIES reports its own errors in `userError` with "VALID"/"INVALID" in
    // isValid; an error there means the enquiry failed, not that the number is
    // invalid - conflating the two would mark a good customer as bad whenever
    // a member state's service is down (MS_UNAVAILABLE).
    const std::string userError = root["userError"].GetString();
    if (!userError.empty() && userError != "VALID") {
        result.fehler = userError;
        if (userError == "INVALID_INPUT")   result.fehler = "Ungültige Eingabe (INVALID_INPUT).";
        if (userError == "MS_UNAVAILABLE")  result.fehler = "Der Mitgliedstaat antwortet gerade "
                                                            "nicht (MS_UNAVAILABLE) - bitte "
                                                            "später erneut abfragen.";
        if (userError == "TIMEOUT")         result.fehler = "Zeitüberschreitung beim "
                                                            "Mitgliedstaat (TIMEOUT).";
        if (userError == "SERVICE_UNAVAILABLE") result.fehler = "VIES ist gerade nicht "
                                                                "verfügbar.";
        return result;
    }

    result.ok           = true;
    result.gueltig      = root["isValid"].GetBoolean(false);
    result.name         = root["name"].GetString();
    result.adresse      = root["address"].GetString();
    result.anfrageId    = root["requestIdentifier"].GetString();
    result.anfrageDatum = DateFromAuthority(root["requestDate"].GetString());
    if (!root["vatNumber"].GetString().empty()) result.nummer = root["vatNumber"].GetString();
    if (!root["countryCode"].GetString().empty()) result.land = root["countryCode"].GetString();

    // VIES returns the literals "---" and "" for a name or address it will not
    // disclose (several member states do not). Not an error, but not data
    // either, so it is cleared rather than shown as a name.
    if (result.name == "---")    result.name.clear();
    if (result.adresse == "---") result.adresse.clear();
    return result;
}

Bestaetigung PruefeUstIdNrVies(const Bestaetigungsanfrage& anfrage, const HttpAnfrage& http) {
    std::string land, nummer;
    SplitVatNumber(anfrage.ustIdNr, land, nummer);

    Bestaetigung result;
    result.quelle = BestaetigungsQuelle::Vies;
    result.land   = land;
    result.nummer = nummer;

    // The offline check first: a number that cannot be right should not cost a
    // request, and the authority's own answer to nonsense is less helpful than
    // ours.
    const UstIdNrPruefung offline = PruefeUstIdNr(anfrage.ustIdNr);
    if (!offline.Plausibel()) {
        result.fehler = offline.hinweis;
        return result;
    }
    if (!http) {
        result.fehler = "Es ist keine Netzwerkverbindung eingerichtet.";
        return result;
    }

    HttpAntwort antwort;
    const bool erreicht = anfrage.eigeneUstIdNr.empty()
        ? http(ViesUrl(land, nummer), std::string(), antwort)
        : http(ViesPostUrl(), ViesPostBody(anfrage), antwort);
    if (!erreicht) {
        result.fehler = antwort.fehler.empty() ? "VIES war nicht erreichbar." : antwort.fehler;
        result.protokoll = antwort.body;
        return result;
    }
    if (antwort.status >= 500) {
        result.fehler = "VIES antwortet mit einem Serverfehler (" +
                        std::to_string(antwort.status) + ").";
        result.protokoll = antwort.body;
        return result;
    }

    Bestaetigung gelesen = ViesAntwortLesen(antwort.body, land, nummer);
    if (!gelesen.ok && antwort.status >= 400 && gelesen.fehler.empty())
        gelesen.fehler = "VIES hat die Anfrage abgelehnt (" +
                         std::to_string(antwort.status) + ").";
    return gelesen;
}

// ===== BZSt eVatR =====

std::string EvatrUrl() {
    // [unverified] - the host is published; the path must be confirmed against
    // the BZSt's OpenAPI document before the first live call.
    return "https://api.evatr.vies.bzst.de/api/v1/abfrage";
}

std::string EvatrBody(const Bestaetigungsanfrage& anfrage) {
    std::string land, nummer;
    SplitVatNumber(anfrage.ustIdNr, land, nummer);
    std::string eigenesLand, eigeneNummer;
    SplitVatNumber(anfrage.eigeneUstIdNr, eigenesLand, eigeneNummer);

    std::string body = "{";
    body += "\"anfragendeUstid\":" + JsonString(eigenesLand + eigeneNummer);
    body += ",\"auslaendischeUstid\":" + JsonString(land + nummer);
    // Supplying these four is what turns a simple enquiry into a qualified one.
    if (!anfrage.name.empty())    body += ",\"firmenname\":"  + JsonString(anfrage.name);
    if (!anfrage.strasse.empty()) body += ",\"strasse\":"     + JsonString(anfrage.strasse);
    if (!anfrage.plz.empty())     body += ",\"plz\":"         + JsonString(anfrage.plz);
    if (!anfrage.ort.empty())     body += ",\"ort\":"         + JsonString(anfrage.ort);
    body += "}";
    return body;
}

Bestaetigung EvatrAntwortLesen(const std::string& json, const std::string& land,
                               const std::string& nummer) {
    Bestaetigung result;
    result.quelle    = BestaetigungsQuelle::BzstEvatr;
    result.land      = land;
    result.nummer    = nummer;
    result.protokoll = json;

    UltraCanvas::JSONParseResult parse;
    const JSONValue root = UltraCanvas::JSON::Parse(json, &parse);
    if (!parse.success || !root.IsObject()) {
        result.fehler = "Die Antwort des BZSt war nicht lesbar" +
                        (parse.errorMessage.empty() ? "." : ": " + parse.errorMessage);
        return result;
    }

    // The status code carries the verdict; anything but "200" is a refusal
    // whose text the BZSt supplies. [unverified]
    const std::string status = root["statusCode"].GetString();
    const std::string meldung = root["statusMeldung"].GetString();
    if (!status.empty() && status != "200") {
        result.fehler = meldung.empty() ? ("Das BZSt meldet Status " + status) : meldung;
        return result;
    }

    result.ok           = true;
    result.gueltig      = true;
    result.name         = root["firmenname"].GetString();
    result.adresse      = root["strasse"].GetString();
    result.anfrageId    = root["anfrageId"].GetString();
    result.anfrageDatum = DateFromAuthority(root["datum"].GetString());

    result.nameAbgleich    = AbgleichFromCode(root["ergFirmenname"].GetString());
    result.strasseAbgleich = AbgleichFromCode(root["ergStrasse"].GetString());
    result.plzAbgleich     = AbgleichFromCode(root["ergPlz"].GetString());
    result.ortAbgleich     = AbgleichFromCode(root["ergOrt"].GetString());
    return result;
}

Bestaetigung PruefeUstIdNrBzst(const Bestaetigungsanfrage& anfrage, const HttpAnfrage& http) {
    std::string land, nummer;
    SplitVatNumber(anfrage.ustIdNr, land, nummer);

    Bestaetigung result;
    result.quelle = BestaetigungsQuelle::BzstEvatr;
    result.land   = land;
    result.nummer = nummer;

    const UstIdNrPruefung offline = PruefeUstIdNr(anfrage.ustIdNr);
    if (!offline.Plausibel()) {
        result.fehler = offline.hinweis;
        return result;
    }
    // The BZSt confirms *foreign* numbers for a German enquirer; asking it
    // about a German number, or asking without one's own number, cannot
    // produce a usable answer.
    if (land == "DE") {
        result.fehler = "Das BZSt bestätigt ausländische USt-IdNrn.; für eine deutsche "
                        "Nummer ist es nicht zuständig.";
        return result;
    }
    if (anfrage.eigeneUstIdNr.empty()) {
        result.fehler = "Für eine Bestätigungsanfrage muss die eigene USt-IdNr. in den "
                        "Mandantendaten hinterlegt sein.";
        return result;
    }
    if (!http) {
        result.fehler = "Es ist keine Netzwerkverbindung eingerichtet.";
        return result;
    }

    HttpAntwort antwort;
    if (!http(EvatrUrl(), EvatrBody(anfrage), antwort)) {
        result.fehler = antwort.fehler.empty() ? "Das BZSt war nicht erreichbar."
                                               : antwort.fehler;
        result.protokoll = antwort.body;
        return result;
    }
    if (antwort.status >= 500) {
        result.fehler = "Das BZSt antwortet mit einem Serverfehler (" +
                        std::to_string(antwort.status) + ").";
        result.protokoll = antwort.body;
        return result;
    }
    return EvatrAntwortLesen(antwort.body, land, nummer);
}

} // namespace UltraFIBU
