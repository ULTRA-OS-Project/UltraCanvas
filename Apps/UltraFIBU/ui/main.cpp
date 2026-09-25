// Apps/UltraFIBU/ui/main.cpp
// ultrafibu: the one UltraFIBU program - the commands and the window.
//
// Given a command (`ultrafibu info buch.db`, `ultrafibu einrichten ...`), it
// runs it and prints the result, exactly as the command-line tool always did.
// Given nothing, or one file, it opens the window. There used to be two
// programs for this, `ultrafibu` and `ultrafibu-ui`, and which one a user had
// in front of them decided whether a double-click showed anything at all.
//
// The database is the one optional argument of the window. Without it - or
// with a path that holds no bookkeeping - the start window opens, where one is
// set up or chosen. This used to print "keine Datei angegeben" and end, which
// from a double-click on Windows looked exactly like a crash: the console
// window closed before the sentence could be read.
//
// What still holds from the old rule: a bookkeeping file never appears
// because somebody opened a window. Opening does not create
// (`Store::Open`); only the setup form does, and only when asked.
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUApp.h"
#include "UltraFIBUStart.h"
#include "UltraFIBUKonsole.h"
#include "UltraFIBUCli.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasUtils.h"   // GetResourcesDir, NormalizePath

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

// Whether these arguments ask for the window rather than a command. The window
// takes at most one argument, the file; a command word, an option, or more
// than one argument goes to the commands - which also means a mistyped
// command ("ultrafibu infoo buch.db") is reported as one instead of being
// taken for a file name and answered with a setup form.
bool WillFenster(int argc, char** argv) {
    if (argc < 2) return true;
    if (argc > 2) return false;
    const std::string arg = argv[1];
    return !arg.empty() && arg[0] != '-' && !UltraFIBU::IstBefehl(arg);
}

} // namespace

int main(int argc, char** argv) {
    if (!WillFenster(argc, argv))
        return UltraFIBU::Kommandozeile(argc, argv, true);

    const std::string datenbank = argc > 1 ? argv[1] : std::string();

    UltraCanvas::UltraCanvasApplication app;
    if (!app.Initialize("UltraFIBU")) {
        std::printf("Fehler: Die Anwendung konnte nicht gestartet werden "
                    "(kein Display?).\n");
        return EXIT_FAILURE;
    }
    // Only once the window can come up: until here, what goes wrong is
    // printed, and needs the console to be read.
    UltraFIBU::KonsoleFreigebenWennEigene();

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
