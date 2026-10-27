// Apps/UltraStoreAdmin/cli/Commands.h
// The admin backend's commands, for ui/main.cpp: everything
// `ultrastoreadmin <command>` does, without a window. Run() returns the
// process exit code.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace UltraStoreAdmin::Cli {

// The usage text (to stdout: exit 0, elsewhere: exit 2).
int Usage(FILE* to);

// args without the program name; `--store <dir>` anywhere, then the command.
int Run(const std::vector<std::string>& args);

} // namespace UltraStoreAdmin::Cli
