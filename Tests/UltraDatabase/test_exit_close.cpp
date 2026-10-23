// Tests/UltraDatabase/test_exit_close.cpp
// A connection closed from an atexit handler registered before the first
// close of the process - UltraMessage's handler closes its journal so. The
// first close builds the manager's table of retired connections; as a plain
// static it was destroyed before such a handler ran, the handler's close
// wrote into the freed table, and the process aborted in free() as it exited
// (ChatCube, on every quit). Its own program: the fault is in the exit.
// ctest runs it with glibc's strict heap checks, which turn the write into
// an abort; a clean exit with status 0 is the pass.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraDatabase/UltraDatabaseConnection.h"

#include <cstdio>
#include <cstdlib>

namespace {

bool Open(const std::string& name) {
    UltraDbConnectionConfig cfg;
    cfg.name = name;
    cfg.database = ":memory:";
    return UltraDb_RegisterConnection(cfg).success && UltraDb_OpenConnection(name).success;
}

} // namespace

// UltraMessage's order: the manager's connection table touched, the atexit
// handler registered, the journal opened; an application's store closes
// during main; the handler closes the journal at exit.
int main() {
    (void)UltraDb_HasConnection("exit-close.bootstrap");
    std::atexit([] {
        if (!UltraDb_CloseConnection("exit-close.journal").success) {
            std::fprintf(stderr, "UltraDatabaseExitClose: the close at exit failed\n");
            std::_Exit(1);
        }
        std::printf("UltraDatabaseExitClose: closed at exit\n");
    });
    if (!Open("exit-close.journal") || !Open("exit-close.store")) {
        std::fprintf(stderr, "UltraDatabaseExitClose: cannot open the connections\n");
        return 1;
    }
    // The first close of the process.
    if (!UltraDb_CloseConnection("exit-close.store").success) {
        std::fprintf(stderr, "UltraDatabaseExitClose: the close in main failed\n");
        return 1;
    }
    return 0;
}
