// Apps/UltraClaude/main.cpp
// UltraClaude - a desktop chat window for Claude that runs on a Claude Pro or
// Max subscription. It does not talk to Anthropic's servers itself: it starts
// the official Claude Code CLI (`claude`), which is signed in with the
// user's own account, and shows what the CLI streams back
// (ClaudeChatSession). No API key, no per-token bill, no credential in this
// program.
//
// Without arguments it opens the window, on the sign-in page. With
// --print "<prompt>" it sends one prompt and writes the streamed reply to
// standard output - the same engine, headless, so it can be checked from a
// terminal or over ssh.
//
// Version: 0.1.0
// Last Modified: 2026-10-02
// Author: UltraCanvas Framework / ULTRA OS

#include "ClaudeChatSession.h"
#include "ui/UltraClaudeWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"

#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

// ULTRACLAUDE_VERSION comes from the build alone: CMake reads the first line of
// Docs/UltraClaude/CHANGELOG.md (cmake/UltraCanvasVersion.cmake) and passes it
// as a compile definition. No fallback, so a build that lost it fails instead
// of reporting a wrong number.
#ifndef ULTRACLAUDE_VERSION
#error "ULTRACLAUDE_VERSION is not defined: build through CMake, which reads it from Docs/UltraClaude/CHANGELOG.md"
#endif

using namespace UltraClaude;

namespace {

void PrintUsage(const char* programName) {
    std::printf(
        "UltraClaude %s - chat with Claude on your Claude subscription\n"
        "Powered by the UltraCanvas framework and the Claude Code CLI\n"
        "\n"
        "Usage: %s [options]\n"
        "\n"
        "  (no options)          Open the UltraClaude window\n"
        "  --print <prompt>      Send one prompt and print the streamed reply\n"
        "      --model <name>    Model alias or name (opus, sonnet, haiku, ...)\n"
        "      --cwd <folder>    The folder Claude works in (default: this one)\n"
        "      --permission-mode <mode>\n"
        "                        default, acceptEdits, plan or bypassPermissions\n"
        "  --claude <path>       The claude program to run (default: claude on PATH)\n"
        "  --version             Print the version and exit\n"
        "  --help                This text\n"
        "\n"
        "Install Claude Code and sign in once (claude auth login) - UltraClaude\n"
        "uses that sign-in and never asks for a password or an API key.\n",
        ULTRACLAUDE_VERSION, programName);
}

// --print: one prompt, the reply streamed to stdout, tool calls and errors
// to stderr. Returns the process exit code.
int RunPrint(const std::string& prompt, const ClaudeChatOptions& options) {
    ClaudeChatSession session;
    std::mutex mutex;
    std::condition_variable finished;
    bool done = false;
    bool failed = false;

    auto onEvent = [&](const ClaudeStreamEvent& e) {
        switch (e.kind) {
            case ClaudeEventKind::TextDelta:
            case ClaudeEventKind::AssistantText:
                std::fwrite(e.text.data(), 1, e.text.size(), stdout);
                std::fflush(stdout);
                break;
            case ClaudeEventKind::ToolUse:
                std::fprintf(stderr, "\n[%s] %s\n", e.toolName.c_str(), e.text.c_str());
                break;
            case ClaudeEventKind::ToolResult:
                if (e.isError) std::fprintf(stderr, "[tool error] %s\n", e.text.c_str());
                break;
            case ClaudeEventKind::TurnFinished:
                std::fputc('\n', stdout);
                if (e.isError) {
                    if (!e.textAlreadyShown) std::fprintf(stderr, "Error: %s\n", e.text.c_str());
                    failed = true;
                }
                break;
            case ClaudeEventKind::ProcessExited: {
                if (e.isError) {
                    std::fprintf(stderr, "Claude Code exited with code %d%s%s\n", e.exitCode,
                                 e.text.empty() ? "" : ": ", e.text.c_str());
                    failed = true;
                }
                std::lock_guard<std::mutex> lock(mutex);
                done = true;
                finished.notify_all();
                break;
            }
            case ClaudeEventKind::ProcessError: {
                std::fprintf(stderr, "%s\n", e.text.c_str());
                std::lock_guard<std::mutex> lock(mutex);
                failed = true;
                done = true;
                finished.notify_all();
                break;
            }
            case ClaudeEventKind::SessionStarted:
                break;
        }
    };

    std::string error;
    if (!session.SendPrompt(prompt, options, onEvent, error)) return EXIT_FAILURE;
    std::unique_lock<std::mutex> lock(mutex);
    finished.wait(lock, [&]() { return done; });
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv) {
    std::string printPrompt;
    bool print = false;
    ClaudeChatOptions options;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "UltraClaude: %s needs a value\n", name);
                std::exit(EXIT_FAILURE);
            }
            return argv[++i];
        };
        if (std::strcmp(arg, "--version") == 0) {
            std::printf("UltraClaude %s\n", ULTRACLAUDE_VERSION);
            return EXIT_SUCCESS;
        } else if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            PrintUsage(argv[0]);
            return EXIT_SUCCESS;
        } else if (std::strcmp(arg, "--print") == 0) {
            print = true;
            printPrompt = next("--print");
        } else if (std::strcmp(arg, "--model") == 0) {
            options.model = next("--model");
        } else if (std::strcmp(arg, "--cwd") == 0) {
            options.workingDirectory = next("--cwd");
        } else if (std::strcmp(arg, "--permission-mode") == 0) {
            options.permissionMode = next("--permission-mode");
        } else if (std::strcmp(arg, "--claude") == 0) {
            options.executable = next("--claude");
        } else {
            std::fprintf(stderr, "UltraClaude: unknown option %s\n\n", arg);
            PrintUsage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (print) return RunPrint(printPrompt, options);

    UltraCanvas::UltraCanvasApplication app;
    if (!app.Initialize("UltraClaude")) {
        std::fprintf(stderr, "UltraClaude: the UltraCanvas application could not be "
                             "initialised (no display?). Use --print for the terminal.\n");
        return EXIT_FAILURE;
    }
    app.SetDefaultWindowIcon(UltraCanvas::NormalizePath(
            UltraCanvas::GetResourcesDir() + "media/appicon/UltraClaude.png"));

    UltraClaudeWindow window(ULTRACLAUDE_VERSION, options.executable);
    if (!window.Create()) {
        std::fprintf(stderr, "UltraClaude: the window could not be created.\n");
        return EXIT_FAILURE;
    }
    window.onClosed = [&app]() { app.Exit(); };
    window.Show();
    app.Run();
    return EXIT_SUCCESS;
}
