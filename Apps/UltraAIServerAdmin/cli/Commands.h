// Apps/UltraAIServerAdmin/cli/Commands.h
// UltraAI ServerAdmin's commands, for ui/main.cpp: everything
// `ultraai-serveradmin <command>` does without a window - list, create and
// publish models, upload their files, set the topics, manage the accounts.
// The server and the token come from --server / --token or the environment
// (ULTRAAI_SERVER_URL, ULTRAAI_SERVER_TOKEN). Run() returns the exit code.
// Version: 0.1.0
// Author: UltraAI Module
#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace UltraAIServerAdmin::Cli {

// The usage text (to stdout: exit 0, elsewhere: exit 2).
int Usage(FILE* to);

// args without the program name.
int Run(const std::vector<std::string>& args);

} // namespace UltraAIServerAdmin::Cli
