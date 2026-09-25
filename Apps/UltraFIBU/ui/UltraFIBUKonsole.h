// Apps/UltraFIBU/ui/UltraFIBUKonsole.h
// Letting go of a console window nobody asked for.
//
// `ultrafibu` is one program for the commands and the window, so it is built
// as a console program: that is what lets `ultrafibu info buch.db` print into
// the prompt it was typed at, and makes the prompt wait for it. The price is
// on Windows - started by a double-click, a console program gets a console
// window of its own, which then stands empty behind the bookkeeping.
//
// A GUI program would avoid that window but could not print to the prompt at
// all; attaching to the parent console afterwards writes the output after the
// prompt has already come back. So the console stays, and is let go of when
// it is nobody else's.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

namespace UltraFIBU {

// On Windows: close the console window if this process is the only one
// attached to it - which is the case exactly when Windows opened it for this
// program, i.e. after a double-click. Started from a prompt, the prompt's own
// console is shared and is kept, so errors printed while starting stay
// readable. Everywhere else, nothing: there a console appears only when the
// program was started from one.
void KonsoleFreigebenWennEigene();

} // namespace UltraFIBU
