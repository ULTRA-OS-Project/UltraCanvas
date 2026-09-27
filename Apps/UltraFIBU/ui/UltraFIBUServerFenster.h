// Apps/UltraFIBU/ui/UltraFIBUServerFenster.h
// Connect to a shared UltraFIBU database and sign in - the window's half of
// the multi-user mode (engine/UltraFIBUServer.h has the rest).
//
// **Two steps, in one window, in the order they happen.** First the server:
// host, port, database and the database role, plus the database password -
// asked for only while this computer's vault does not hold one yet, and then
// stored there. Then the person: login name and password, checked against the
// users in that database. The login fields stay greyed until the connection
// stands, because a login against no database has nothing to check against.
//
// **The personal password is never stored.** The database password belongs
// to the installation and lives in the vault; the personal one says who is
// working, for the audit trail, and is typed at every start.
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

#include "UltraFIBUServer.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraFIBU {

class ServerFenster {
public:
    // Open the connection. `dbPasswort` is empty when the vault already holds
    // one. Returns why not, or an empty string. `leer` is set when the
    // database has no users yet - nobody can sign in, and the window says how
    // to set it up instead of offering a login that cannot work.
    std::function<std::string(const ServerZiel& ziel, const std::string& dbPasswort,
                              bool& leer)> onVerbinden;

    // Sign in and open the main window. Returns why not, or an empty string;
    // on success this window closes itself.
    // `code` is empty until the first attempt reports `codeNoetig` - the user
    // has a second factor - and the code field is then offered.
    std::function<std::string(const std::string& name, const std::string& passwort,
                              const std::string& code, bool& codeNoetig)> onAnmelden;

    // `vorgabe` fills the form: the address given on the command line, or the
    // last server this computer used.
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> Bauen(const ServerZiel& vorgabe,
                                                          const std::string& hinweis);
    void Schliessen();

private:
    bool LiesZiel(ServerZiel& out, std::string& fehler) const;
    void TresorHinweisAktualisieren();
    void Verbinden();
    void Anmelden();
    void AnmeldungFreigeben(bool frei);
    void Melden(const std::string& text, bool fehler);

    bool verbunden_ = false;
    ServerTresor tresor_;

    std::shared_ptr<UltraCanvas::UltraCanvasWindow>    fenster_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> host_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> port_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> datenbank_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> dbBenutzer_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> dbPasswort_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     tresorHinweis_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    verbindenKnopf_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> name_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> passwort_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> code_;
    bool codeGefragt_ = false;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    anmeldenKnopf_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     status_;
};

} // namespace UltraFIBU
