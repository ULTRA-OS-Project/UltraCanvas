// Apps/UltraFIBU/ui/UltraFIBUStart.h
// The window that appears when there is no bookkeeping to show yet: set up a
// new one, or open one that exists.
//
// It used to be that the program had nowhere to go without a file. Started
// with no argument, or with a path that did not exist, it printed one line to
// a console and ended - and on Windows, double-clicked from Explorer, that
// console closed again before anybody could read it, which looked exactly like
// a crash. Worse, a path that did not exist was created on the way, as an
// empty bookkeeping the program then asked the user to set up by hand on the
// command line.
//
// **Only what is needed to start is asked for.** A company name, the first day
// of the fiscal year and the chart of accounts. Address, tax number and bank
// account are needed before the first invoice is printed, not before the first
// document is entered - and "rechnung-pdf" names exactly what is still
// missing at that point. A form that demands all of it up front gets filled
// with placeholders to get past it.
//
// **The fiscal year can only start on the first of a month**, so the calendar
// only lets one pick a first of a month. Refusing the 15th after it was typed
// would be the same rule, told later.
//
// Version: 0.3.1 - SeiteBauen builds the page without a window, for a headless test
// Version: 0.3.0 - on the framework's UltraCanvasStartPage, which UltraPress's start window shares
// Version: 0.2.0 - the bookkeepings opened last, offered first
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers first (X11 defines Bool/Status as macros; the engine
// headers below use those words as identifiers).
#include "UltraCanvasWindow.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasStartPage.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasDatePicker.h"

#include "UltraFIBUEinrichtung.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraFIBU {

class StartFenster {
public:
    // Called with the file the user settled on: one just set up, or an
    // existing one chosen. The caller opens it in the main window and returns
    // an empty string, or returns why it could not - a file written by a newer
    // version, say - which this window then shows instead of closing.
    //
    // This window closes itself only after the call succeeded, so the main
    // window already exists by then: the application ends the moment it has
    // no window left, and closing this one first would end it.
    std::function<std::string(const std::string&)> onDateiGewaehlt;
    // Called with a file of the recent list that is gone, or holds no
    // bookkeeping any more, so the caller drops it from the list.
    std::function<void(const std::string&)> onDateiVergessen;

    // `hinweis` says why this window is showing instead of a bookkeeping, so
    // the user is not left guessing what went wrong. `vorschlag` is a path to
    // offer when setting up - the one that was given and did not exist.
    // `zuletzt` are the files opened last, the last one first; the five
    // newest become buttons above the open button.
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> Bauen(const std::string& hinweis,
                                                          const std::string& vorschlag,
                                                          const std::vector<std::string>& zuletzt = {});
    // The page alone, without a window - what Bauen puts into one. A test
    // builds it without a display and reads it back: the hint, the recent
    // entries, the form, PreferredHeight().
    std::shared_ptr<UltraCanvas::UltraCanvasStartPage> SeiteBauen(const std::string& hinweis,
                                                                  const std::string& vorschlag,
                                                                  const std::vector<std::string>& zuletzt = {});

    void Schliessen();
    // The setup form as it stands, and why it cannot be submitted yet
    // (empty when it can): what the greyed button's reason label says.
    EinrichtungsDaten Daten() const;
    std::string Grund() const;
    // The form's fields, for a test that fills them.
    const std::shared_ptr<UltraCanvas::UltraCanvasTextInput>& Firma() const { return firma_; }
    const std::shared_ptr<UltraCanvas::UltraCanvasDatePicker>& Beginn() const { return beginn_; }
    const std::shared_ptr<UltraCanvas::UltraCanvasButton>& AnlegenKnopf() const { return anlegenKnopf_; }

private:
    void Pruefen();      // live: enable "Anlegen" or say why not
    void Anlegen();
    void Oeffnen();
    void Uebergeben(const std::string& pfad);
    void Melden(const std::string& text, bool fehler);

    std::string vorschlag_;
    std::shared_ptr<UltraCanvas::UltraCanvasWindow>     fenster_;
    std::shared_ptr<UltraCanvas::UltraCanvasStartPage>  seite_;   // the page: title, hint, recent, open, the form, status
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  firma_;
    std::shared_ptr<UltraCanvas::UltraCanvasDatePicker> beginn_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown>   skr_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>      grund_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>     anlegenKnopf_;
};

} // namespace UltraFIBU
