// Apps/UltraStore/cli/Commands.h
// The store's commands, for ui/main.cpp: everything `ultrastore <command>`
// does, without a window. Run() returns the process exit code.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace UltraStore::Cli {

// The usage text (to stdout: exit 0, elsewhere: exit 2).
int Usage(FILE* to);

// args without the program name; the first is the command.
int Run(const std::vector<std::string>& args);

} // namespace UltraStore::Cli
