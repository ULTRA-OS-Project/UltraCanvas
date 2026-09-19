// Apps/UltraFIBU/report/UltraFIBURechnungPdf.h
// The printed invoice: a Beleg rendered to a PDF a customer can be sent.
//
// **This does not contain a PDF writer.** The framework already has one -
// `UltraCanvas::VectorConverter::PDFVectorConverter`, which writes a
// self-contained PDF 1.4 from a `VectorStorage::VectorDocument`. This file
// builds that document (text, rules, a table) and hands it over. Writing a
// second PDF emitter beside the existing one would be the same mistake as
// building a second data grid beside `UltraCanvasListView`.
//
// The two sources it needs (`UltraCanvasVectorStorage.cpp` and
// `UltraCanvasPDFVectorConverter.cpp`) were checked and carry **no reference to
// the rendering stack**, so this target stays headless like the engine: an
// invoice can be produced on a server with no display, no pango and no vips.
//
// What is deliberately *not* here: ZUGFeRD. That is a PDF/A-3 file with the CII
// XML as an associated embedded file, and it needs embedded subset fonts, an
// output intent and XMP metadata that the writer does not do yet (proposal
// §3.2, §7.2). XRechnung - pure XML, no PDF - is the route to an e-invoice
// until the writer grows up, and it is phase A7.
//
// § 14 UStG decides the content, not taste. An invoice missing the supplier's
// Steuernummer or the tax rate is legally deficient and the recipient cannot
// deduct the input tax from it, so the mandatory fields are checked and what is
// missing is reported rather than quietly left off the page.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraFIBUBeleg.h"
#include "UltraFIBUTypes.h"

#include <string>
#include <vector>

namespace UltraFIBU {

// The few things about the printed page that are a choice rather than a legal
// requirement. Everything else follows from the Mandant and the Beleg.
struct RechnungLayout {
    // A4 in points, which is what the PDF writer measures in.
    double seiteBreite = 595.28;
    double seiteHoehe  = 841.89;
    double randLinks   = 56.7;    // 20 mm
    double randRechts  = 56.7;
    double randOben    = 45.0;
    double randUnten   = 56.7;

    // One of the base-14 families, because nothing is embedded: Helvetica,
    // Times-Roman or Courier. Helvetica is legal on an invoice and is what
    // every German office template uses.
    std::string schrift = "Helvetica";

    // Printed diagonally across an unposted document, so a draft that reaches a
    // customer by accident says what it is.
    bool entwurfKennzeichnen = true;

    // An extra line at the very bottom - a registry court, a managing
    // director, a note about the Verfahrensdokumentation.
    std::string fusszeileZusatz;

    // Payment wording. Empty derives it from the due date and the partner's
    // Skonto terms, which is what almost every invoice wants.
    std::string zahlungshinweis;
};

struct RechnungPdfErgebnis {
    bool        ok = false;
    std::string fehler;

    // § 14 UStG fields that are not filled. The PDF is still written - a draft
    // has to be printable before the master data is complete - but this is what
    // stands between it and a valid invoice, in German, ready to show.
    std::vector<std::string> fehlendePflichtangaben;

    bool VollstaendigNachUStG() const { return fehlendePflichtangaben.empty(); }
};

// Render `beleg` as an invoice and write it to `dateiPfad`.
//
// `empfaenger` supplies the recipient's address: the Beleg stores only the name
// and the account as they were, deliberately, so the address comes from the
// partner record. `schluessel` is the set of tax keys valid on the Belegdatum -
// it decides the wording of the exemption note under the total, which for a
// zero-rated line is itself a § 14 Abs. 4 Nr. 8 requirement.
RechnungPdfErgebnis SchreibeRechnungPdf(const Mandant& mandant,
                                        const Beleg& beleg,
                                        const Partner& empfaenger,
                                        const std::vector<Steuerschluessel>& schluessel,
                                        const std::string& dateiPfad,
                                        const RechnungLayout& layout = RechnungLayout());

// The § 14 UStG check on its own, for a UI that wants to warn before printing.
std::vector<std::string> PruefePflichtangaben(const Mandant& mandant, const Beleg& beleg,
                                              const Partner& empfaenger);

// The width of `text` in points, in the base-14 font this module prints with.
// Exposed because it is the only honest way to right-align a column of amounts
// when no font metrics are embedded: every digit in Helvetica is 556/1000 em
// wide, and so is the euro sign, which is what makes a money column line up.
double TextBreite(const std::string& text, double schriftgroesse, bool fett);

} // namespace UltraFIBU
