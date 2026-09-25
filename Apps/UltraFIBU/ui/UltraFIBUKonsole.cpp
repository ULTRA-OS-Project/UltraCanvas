// Apps/UltraFIBU/ui/UltraFIBUKonsole.cpp
// See the header. Kept in a file of its own because <windows.h> defines
// CreateWindow as a macro, which the UltraCanvas window factory is also named.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUKonsole.h"

#if defined(_WIN32)
  #include <windows.h>
#endif

namespace UltraFIBU {

void KonsoleFreigebenWennEigene() {
#if defined(_WIN32)
    DWORD prozesse[2];
    if (::GetConsoleProcessList(prozesse, 2) == 1) ::FreeConsole();
#endif
}

} // namespace UltraFIBU
