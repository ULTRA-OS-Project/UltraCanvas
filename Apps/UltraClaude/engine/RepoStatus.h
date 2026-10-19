// Apps/UltraClaude/engine/RepoStatus.h
// How many lines a chat's folder holds that its repository's default branch
// does not - the "lines not PRed" badge in the chat list. The count is the
// one this repository's closing line uses (AGENTS.md, "The closing line"):
//
//   git fetch origin <default>
//   git diff --numstat $(git merge-base origin/<default> HEAD)   committed + uncommitted
//   + every line of the untracked, non-ignored files
//
// The default branch is what origin/HEAD names, else origin/main, else
// origin/master. Git runs with its prompts off and with network timeouts,
// so a remote that wants a password or does not answer costs seconds and an
// unfetched base, never a hang; the count then uses the base as last fetched.
//
// Blocking: run it off the UI thread.
//
// Version: 0.1.0
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <string>

namespace UltraClaude {

struct RepoLines {
    enum class State {
        Measured,    // lines is the count
        NotARepo,    // the folder is not in a git work tree: no badge
        NoBase,      // a repository without origin/<default>: nothing to compare with
        Failed       // git could not be run, or answered nonsense; error says why
    };
    State state = State::Failed;
    int64_t lines = 0;
    std::string base;    // "origin/main"
    std::string branch;  // the checked-out branch, "" when detached
    bool fetched = false;
    std::string error;
};

// git is looked up on PATH. fetch=false skips the network and compares with
// the default branch as last fetched.
RepoLines MeasureLinesNotMerged(const std::string& folder, bool fetch = true);

// "344", "12k", "1.2M" - short enough for a badge.
std::string FormatLineCount(int64_t lines);

} // namespace UltraClaude
