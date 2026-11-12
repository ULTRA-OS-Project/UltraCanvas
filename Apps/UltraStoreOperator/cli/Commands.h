// Apps/UltraStoreOperator/cli/Commands.h
// The operator's commands, for ui/main.cpp: everything `ultrastoreop
// <command>` does, without a window. Run() returns the process exit code.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace UltraStoreOperator::Cli {

// The usage text (to stdout: exit 0, elsewhere: exit 2).
int Usage(FILE* to);

// args without the program name; `--settings <file>`, `--url <base>` and
// `--token <t>` anywhere, then the command.
int Run(const std::vector<std::string>& args);

} // namespace UltraStoreOperator::Cli
