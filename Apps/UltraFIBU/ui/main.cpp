// Apps/UltraFIBU/ui/main.cpp
// ultrafibu-ui: the German screens over the UltraFIBU engine.
//
// The database is the one argument. It is not created here: setting up a
// company, its fiscal year and its chart of accounts is `ultrafibu einrichten`,
// and a bookkeeping file that appears because somebody opened a window by
// mistake is not a thing that should be possible.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUApp.h"

#include "UltraCanvasApplication.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#ifndef ULTRAFIBU_UI_VERSION
#define ULTRAFIBU_UI_VERSION "0.0.0"
#endif

int main(int argc, char** argv) {
    std::string datenbank;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--version") {
            std::printf("%s\n", ULTRAFIBU_UI_VERSION);
            return EXIT_SUCCESS;
        }
        if (arg == "--help" || arg == "-h") {
            std::printf("ultrafibu-ui - Buchhaltung (UltraFIBU %s)\n\n"
                        "Aufruf:  ultrafibu-ui <datei>\n\n"
                        "Zeigt Belege, Journal, Summen und Salden und die Partner\n"
                        "einer bestehenden Buchhaltung. Angelegt wird eine solche\n"
                        "Datei mit \"ultrafibu einrichten\".\n",
                        ULTRAFIBU_UI_VERSION);
            return EXIT_SUCCESS;
        }
        if (datenbank.empty()) datenbank = arg;
    }
    if (datenbank.empty()) {
        std::printf("Fehler: Keine Datei angegeben.\n"
                    "Aufruf: ultrafibu-ui <datei>\n");
        return EXIT_FAILURE;
    }

    UltraCanvas::UltraCanvasApplication app;
    if (!app.Initialize("UltraFIBU")) {
        std::printf("Fehler: Die Anwendung konnte nicht gestartet werden "
                    "(kein Display?).\n");
        return EXIT_FAILURE;
    }

    UltraFIBU::FibuApp fibu;
    std::string fehler;
    if (!fibu.Initialisieren(datenbank, fehler)) {
        // Reported on the terminal rather than in a dialog: at this point
        // there is no window yet, and the usual cause is a wrong path typed on
        // that same terminal.
        std::printf("Fehler: %s\n", fehler.c_str());
        return EXIT_FAILURE;
    }

    auto fenster = fibu.FensterBauen();
    fenster->Show();
    app.Run();
    return EXIT_SUCCESS;
}
