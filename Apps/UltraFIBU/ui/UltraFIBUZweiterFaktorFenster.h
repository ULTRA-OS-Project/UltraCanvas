// Apps/UltraFIBU/ui/UltraFIBUZweiterFaktorFenster.h
// Enrol the signed-in user's second factor: a QR code to scan with
// UltraAuthenticator, then a code from the app and the password to confirm.
//
// **Nothing is stored until the code matches.** A QR code that never reached
// the phone must not lock the user out, so the secret is only kept once the
// app has produced a code from it (Store::ZweitenFaktorAktivieren).
//
// **The secret is on screen while this window is open** - that is what a QR
// code is. The window wipes the QR element and the key label when it closes,
// and it offers no way to save or copy the image.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers first (X11 defines Bool/Status as macros; the engine
// headers below use those words as identifiers).
#include "UltraCanvasWindow.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "Plugins/QRCode/UltraCanvasQRCode.h"

#include "UltraFIBUStore.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraFIBU {

class ZweiterFaktorFenster {
public:
    // Called after the second factor was activated, with a sentence for the
    // main window's status line.
    std::function<void(const std::string&)> onFertig;

    // `konto` is the label the authenticator app will show.
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> Bauen(Store& store, const Akteur& akteur,
                                                          const std::string& konto);
    void Schliessen();

private:
    void Aktivieren();
    void Wischen();
    void Melden(const std::string& text, bool fehler);

    Store*      store_ = nullptr;
    Akteur      akteur_;
    Store::ZweiterFaktorEinrichtung einrichtung_;

    std::shared_ptr<UltraCanvas::UltraCanvasWindow>    fenster_;
    std::shared_ptr<UltraCanvas::UltraCanvasQRCode>    qr_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     schluessel_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> passwort_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> code_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    aktivierenKnopf_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     status_;
};

} // namespace UltraFIBU
