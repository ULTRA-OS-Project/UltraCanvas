// Apps/UltraFIBU/cli/main.cpp
// `ultrafibu` without a window, for where the UI library cannot be built - a
// server, a headless CI runner. Where it can, `ultrafibu` is ui/main.cpp,
// which runs these same commands and opens the window when given none.
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUCli.h"

int main(int argc, char** argv) {
    return UltraFIBU::Kommandozeile(argc, argv, false);
}
