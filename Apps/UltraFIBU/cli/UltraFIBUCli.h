// Apps/UltraFIBU/cli/UltraFIBUCli.h
// The command-line commands, as a library: `ultrafibu einrichten`,
// `ultrafibu buchen`, `ultrafibu datev-export` and the rest.
//
// There used to be two programs: `ultrafibu` for the commands and
// `ultrafibu-ui` for the window. Which one a user needed depended on what they
// wanted to do, and which one they had double-clicked decided whether
// anything appeared at all. Now there is one `ultrafibu`. Given a command, it
// runs it and prints the result, as before; given nothing, or a file, it
// opens the window.
//
// The commands are kept in this library, apart from the window, because the
// engine must keep working without a display - on a server, on a headless CI
// runner, wherever the UI library cannot be built. There the same commands
// are linked into a `ultrafibu` without a window (cli/main.cpp), so a script
// that calls `ultrafibu info buch.db` works unchanged on both.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>

namespace UltraFIBU {

// True when `wort`, given as the first argument, is a command - or --help,
// -h, hilfe or --version. Anything else is left to the window, which takes
// it for the bookkeeping file to open.
bool IstBefehl(const std::string& wort);

// Run the command in argv[1] and return the program's exit code. `mitFenster`
// says whether this program also has the window, which only changes what the
// help text promises.
int Kommandozeile(int argc, char** argv, bool mitFenster);

} // namespace UltraFIBU
