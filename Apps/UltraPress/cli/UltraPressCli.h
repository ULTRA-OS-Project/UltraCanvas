// Apps/UltraPress/cli/UltraPressCli.h
// `ultrapress <command> ...`: the engine from the command line - a starter
// site, a check, a page's HTML, a static publish. No window; the editor
// application comes with the element renderer.
// Version: 0.1.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

namespace UltraPress {

// Runs one command; returns the process exit code. Prints to stdout and
// stderr. `ultrapress help` lists the commands.
int CommandLine(int argc, char** argv);

} // namespace UltraPress
