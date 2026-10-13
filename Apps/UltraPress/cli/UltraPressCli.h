// Apps/UltraPress/cli/UltraPressCli.h
// `ultrapress <command> ...`: the engine from the command line - a starter
// site, a check, a page's HTML, a static publish, a WordPress import. No
// window here; ui/main.cpp is the one program that runs these commands
// and, given a site folder instead of a command, opens the editor.
// Version: 0.4.1
// Version: 0.1.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

namespace UltraPress {

// Runs one command; returns the process exit code. Prints to stdout and
// stderr. `ultrapress help` lists the commands.
int CommandLine(int argc, char** argv);

} // namespace UltraPress
