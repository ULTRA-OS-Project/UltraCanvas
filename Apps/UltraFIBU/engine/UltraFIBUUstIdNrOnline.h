// Apps/UltraFIBU/engine/UltraFIBUUstIdNrOnline.h
// Confirming a VAT number against an authority, and keeping the answer as
// evidence.
//
// Two services, and the difference between them is legal, not technical:
//
//  - **VIES** (European Commission) answers for every member state, needs no
//    registration, and issues a *consultation number* when the enquirer gives
//    their own VAT number. That consultation number is the EU-level record that
//    the enquiry was made.
//        GET  /taxation_customs/vies/rest-api/ms/{LAND}/vat/{NUMMER}
//        POST /taxation_customs/vies/rest-api/check-vat-number   (with requester)
//    Response: isValid, name, address, requestDate, requestIdentifier,
//    userError, viesApproximate.
//
//  - **BZSt eVatR** (Bundeszentralamt fuer Steuern) is what German law means by
//    a *qualified* confirmation (§ 18e UStG): the enquirer supplies name,
//    street, postcode and town, and the answer confirms each of them
//    separately. **For a German business this is the evidence that protects a
//    zero-rated intra-community supply** - a VIES "valid" alone is not. The
//    BZSt retired its XML-RPC interface on 30 November 2025; the successor is a
//    REST API (api.evatr.vies.bzst.de), whose exact fields must be read from its
//    OpenAPI document before this is wired up - hence the [unverified] marks on
//    the eVatR side below.
//
// **Whatever comes back is stored verbatim.** The proof is the authority's own
// data set, not a boolean derived from it, so `protokoll` carries the raw
// response body and is what goes into the partner record.
//
// The transport is **injected**, not called: this file builds requests and
// reads answers, and knows nothing about sockets. That keeps the engine
// headless and testable (the tests feed it canned authority responses), and it
// keeps every HTTPS setting - TLS verification, timeouts, proxies - in the one
// place that owns networking.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraFIBUDate.h"
#include "UltraFIBUUstIdNr.h"

#include <functional>
#include <string>

namespace UltraFIBU {

enum class BestaetigungsQuelle {
    Vies,        // European Commission, EU-wide, simple confirmation
    BzstEvatr    // BZSt, German qualified confirmation (§ 18e UStG)
};

std::string BestaetigungsQuelleToText(BestaetigungsQuelle quelle);

// How each supplied detail was answered in a qualified confirmation. The BZSt
// answers per field, and "not enquired" is not the same as "does not match".
enum class Abgleich { NichtAngefragt, Stimmt, StimmtNicht, NichtGeprueft };

std::string AbgleichToText(Abgleich abgleich);

// What the caller wants confirmed. Name and address are only needed for a
// qualified enquiry; `eigeneUstIdNr` is what earns a VIES consultation number.
struct Bestaetigungsanfrage {
    std::string ustIdNr;          // the number to confirm, any spelling
    std::string eigeneUstIdNr;    // the enquirer's own number
    std::string name;             // qualified enquiry only
    std::string strasse;
    std::string plz;
    std::string ort;
};

// The answer, and the evidence.
struct Bestaetigung {
    bool                ok = false;        // the enquiry itself succeeded
    bool                gueltig = false;   // the number is registered
    BestaetigungsQuelle quelle = BestaetigungsQuelle::Vies;
    std::string         land;
    std::string         nummer;
    std::string         name;              // as the authority holds it
    std::string         adresse;
    std::string         anfrageId;         // VIES consultation number / BZSt reference
    Date                anfrageDatum;
    std::string         fehler;            // userError, or the transport's message

    // Per-field answers of a qualified confirmation.
    Abgleich nameAbgleich    = Abgleich::NichtAngefragt;
    Abgleich strasseAbgleich = Abgleich::NichtAngefragt;
    Abgleich plzAbgleich     = Abgleich::NichtAngefragt;
    Abgleich ortAbgleich     = Abgleich::NichtAngefragt;

    // The authority's response, byte for byte. This is the evidence; never
    // replace it with a summary.
    std::string protokoll;

    // A qualified confirmation is one where every detail supplied was
    // confirmed - which is what § 18e asks for.
    bool IstQualifiziert() const {
        if (quelle != BestaetigungsQuelle::BzstEvatr || !gueltig) return false;
        const Abgleich felder[] = { nameAbgleich, strasseAbgleich, plzAbgleich, ortAbgleich };
        bool eines = false;
        for (Abgleich feld : felder) {
            if (feld == Abgleich::StimmtNicht) return false;
            if (feld == Abgleich::Stimmt) eines = true;
        }
        return eines;
    }

    // One German sentence for the UI and for the audit trail.
    std::string Zusammenfassung() const;
};

// The injected transport. `status` is the HTTP status code; `body` the response
// body whatever the status, because an authority's error text is part of the
// evidence too. Returning false means the request never reached anybody.
struct HttpAntwort {
    int         status = 0;
    std::string body;
    std::string fehler;
};
using HttpAnfrage = std::function<bool(const std::string& url, const std::string& postBody,
                                       HttpAntwort& out)>;

// ---- VIES ------------------------------------------------------------------

// The URL of a simple VIES enquiry, so a caller can also just open it.
std::string ViesUrl(const std::string& land, const std::string& nummer);

// The POST endpoint and its JSON body for an enquiry that carries the
// enquirer's own number and therefore returns a consultation number.
std::string ViesPostUrl();
std::string ViesPostBody(const Bestaetigungsanfrage& anfrage);

// Parse a VIES JSON response. Kept separate from the request so the tests can
// feed it recorded answers - including the malformed ones.
Bestaetigung ViesAntwortLesen(const std::string& json, const std::string& land,
                              const std::string& nummer);

// The whole enquiry, through an injected transport.
Bestaetigung PruefeUstIdNrVies(const Bestaetigungsanfrage& anfrage, const HttpAnfrage& http);

// ---- BZSt eVatR ------------------------------------------------------------
// [unverified] Endpoint and field names must be taken from the BZSt's OpenAPI
// document (api.evatr.vies.bzst.de/api-docs) before this is used against the
// live service; the shape below follows the published description of the
// service that replaced the XML-RPC interface on 30 November 2025.
std::string EvatrUrl();
std::string EvatrBody(const Bestaetigungsanfrage& anfrage);
Bestaetigung EvatrAntwortLesen(const std::string& json, const std::string& land,
                               const std::string& nummer);
Bestaetigung PruefeUstIdNrBzst(const Bestaetigungsanfrage& anfrage, const HttpAnfrage& http);

} // namespace UltraFIBU
