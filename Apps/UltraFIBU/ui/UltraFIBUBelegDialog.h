// Apps/UltraFIBU/ui/UltraFIBUBelegDialog.h
// Entering a document: the invoice form and the receipt form, which are the
// same form with different words on it.
//
// **The tax key is chosen per position, from a list that depends on the
// partner.** That is the whole reason this screen is not a plain grid of text
// fields. Whether a line to a Vienna customer is zero-rated, and under which
// rule, is decided by four things the program already knows - where they are,
// whether they are a business, whether they gave a VAT number, and whether
// this is a sale or a purchase - and one it does not: goods or a service.
// `SteuerschluesselFuerPartner` answers the first four and refuses to guess the
// fifth, so the dropdown offers the two that fit, says in a sentence what each
// one means, and preselects neither.
//
// The sentence under the dropdown is not decoration. An earlier version of this
// program issued an invoice that charged 19 % VAT and, underneath, told the
// customer they owed the tax - because one tax key covered three different
// reverse-charge rules and nothing said which. The line under the dropdown is
// where that difference becomes visible at the moment somebody picks.
//
// **Everything it refuses, the store refuses too.** This form validates so the
// user finds out now rather than on save, but it is not the guard: `SaveBeleg`
// and `Buchen` check independently, and a document this form would allow but
// the store rejects is a bug in this form, not a way in.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers first (X11 defines Bool/Status as macros; the engine
// headers below use those words as identifiers).
#include "UltraCanvasWindow.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasDropdown.h"

#include "UltraFIBUStore.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraFIBU {

class BelegDialog {
public:
    // `store` outlives the dialog; the dialog holds a reference, not a copy,
    // because the tax keys and the partner list are read on every change of
    // partner and a stale copy would offer a key that no longer exists.
    BelegDialog(Store& store, const Mandant& mandant, const Akteur& akteur);

    // Build the form. `art` decides the wording and, more importantly, the
    // direction: an incoming document offers input-tax keys and an outgoing one
    // never does.
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Bauen(
        const std::string& id, float breite, float hoehe, BelegArt art);

    // Start over with an empty document of `art`.
    void Neu(BelegArt art);

    // Load an existing draft - what the receipt screen does after a PDF has
    // been imported and the header is still empty.
    bool Laden(int64_t belegId);

    // Saved successfully; carries the document as the store wrote it back,
    // with its number and its computed totals.
    std::function<void(const Beleg&)> onGespeichert;
    std::function<void(const std::string&)> onMeldung;

private:
    struct PositionsZeile {
        std::shared_ptr<UltraCanvas::UltraCanvasLabel>     nummer;
        std::shared_ptr<UltraCanvas::UltraCanvasTextInput> bezeichnung;
        std::shared_ptr<UltraCanvas::UltraCanvasTextInput> menge;
        std::shared_ptr<UltraCanvas::UltraCanvasTextInput> einheit;
        std::shared_ptr<UltraCanvas::UltraCanvasTextInput> preis;
        std::shared_ptr<UltraCanvas::UltraCanvasTextInput> konto;
        std::shared_ptr<UltraCanvas::UltraCanvasDropdown>  steuer;
        // The sentence explaining the selected key. See the header comment:
        // this is where "goods" and "service" stop looking alike.
        std::shared_ptr<UltraCanvas::UltraCanvasLabel>     hinweis;
        std::shared_ptr<UltraCanvas::UltraCanvasLabel>     betrag;
        // Parallel to the dropdown's items, so a selection maps back to a key
        // without parsing the label.
        std::vector<SteuerschluesselVorschlag> vorschlaege;
    };

    void PartnerListeFuellen();
    void PartnerGewaehlt(int index);
    void ZeileAnlegen();
    void SteuerlisteFuellen(PositionsZeile& zeile);
    void SteuerHinweisAktualisieren(PositionsZeile& zeile);
    // Recost the document from what is in the fields and update the totals and
    // the findings line. Called on every edit, because a total that lags behind
    // the fields is worse than no total.
    void Neuberechnen();
    bool BelegAusFormular(Beleg& out, std::string& fehler) const;
    void Speichern();
    void FormularLeeren();

    Store&   store_;
    Mandant  mandant_;
    Akteur   akteur_;
    BelegArt art_ = BelegArt::Ausgangsrechnung;
    int64_t  belegId_ = 0;
    std::string belegNummer_;

    std::vector<Partner> partnerListe_;
    Partner              partner_;
    std::vector<Steuerschluessel> schluessel_;

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> wurzel_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     titel_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown>  partnerWahl_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     partnerInfo_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> datum_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> leistungsdatum_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> externeNummer_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> betreff_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> zeilenBereich_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    neueZeile_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     summen_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     befunde_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    speichern_;

    std::vector<PositionsZeile> zeilen_;
    float naechsteZeileY_ = 0.0f;
};

} // namespace UltraFIBU
