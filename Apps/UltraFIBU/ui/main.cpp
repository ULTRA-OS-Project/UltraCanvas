// Apps/UltraFIBU/ui/main.cpp
// ultrafibu-ui: the German screens over the UltraFIBU engine.
//
// The database is the one optional argument. Without it - or with a path that
// holds no bookkeeping - the start window opens, where one is set up or
// chosen. This used to print "keine Datei angegeben" and end, which from a
// double-click on Windows looked exactly like a crash: the console window
// closed before the sentence could be read.
//
// What still holds from the old rule: a bookkeeping file never appears
// because somebody opened a window. Opening does not create
// (`Store::Open`); only the setup form does, and only when asked.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUApp.h"
#include "UltraFIBUStart.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasUtils.h"   // GetResourcesDir, NormalizePath

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
                        "Aufruf:  ultrafibu-ui [<datei>]\n\n"
                        "Zeigt Belege, Journal, Summen und Salden und die Partner\n"
                        "einer Buchhaltung. Ohne Datei - oder mit einer, die es noch\n"
                        "nicht gibt - oeffnet sich ein Fenster, in dem eine neue\n"
                        "angelegt oder eine bestehende gewaehlt wird.\n",
                        ULTRAFIBU_UI_VERSION);
            return EXIT_SUCCESS;
        }
        if (datenbank.empty()) datenbank = arg;
    }
    UltraCanvas::UltraCanvasApplication app;
    if (!app.Initialize("UltraFIBU")) {
        std::printf("Fehler: Die Anwendung konnte nicht gestartet werden "
                    "(kein Display?).\n");
        return EXIT_FAILURE;
    }

    // One icon, everywhere the app is drawn: the window and the taskbar entry
    // that follows it read this file; the .ico embedded in the Windows binary
    // and the desktop entry's theme icon come from the same
    // media/appicon/UltraFIBU.png (see CMakeLists.txt).
    app.SetDefaultWindowIcon(UltraCanvas::NormalizePath(
        UltraCanvas::GetResourcesDir() + "media/appicon/UltraFIBU.png"));

    UltraFIBU::FibuApp fibu;
    UltraFIBU::StartFenster start;

    // Open a file in the main window. Returns why not, or an empty string.
    // The main window is built and shown here, before anything closes the
    // start window: the application ends when it has no window left.
    auto oeffne = [&fibu](const std::string& pfad) -> std::string {
        std::string fehler;
        if (!fibu.Initialisieren(pfad, fehler)) return fehler;
        fibu.FensterBauen()->Show();
        return std::string();
    };

    // Why the start window is needed, if it is. Each case gets its own
    // sentence: "no file", "no such file" and "a file with nothing in it" call
    // for different things, and a single "could not open" leaves the user to
    // work out which.
    std::string hinweis;
    if (datenbank.empty()) {
        hinweis = "Es ist noch keine Buchhaltung geöffnet. Legen Sie eine neue an "
                  "oder öffnen Sie eine bestehende.";
    } else if (!UltraFIBU::DateiExistiert(datenbank)) {
        hinweis = "Die Datei \"" + datenbank + "\" gibt es nicht. Sie lässt sich "
                  "hier als neue Buchhaltung anlegen.";
    } else if (!UltraFIBU::EnthaeltBuchhaltung(datenbank)) {
        hinweis = "In \"" + datenbank + "\" ist noch keine Buchhaltung angelegt. "
                  "Das lässt sich hier nachholen.";
    } else {
        const std::string fehler = oeffne(datenbank);
        if (fehler.empty()) {
            app.Run();
            return EXIT_SUCCESS;
        }
        // An existing bookkeeping that would not open - written by a newer
        // version, for instance. The start window says so and offers another.
        hinweis = fehler;
    }

    start.onDateiGewaehlt = oeffne;
    start.Bauen(hinweis, datenbank)->Show();
    app.Run();
    return EXIT_SUCCESS;
}
