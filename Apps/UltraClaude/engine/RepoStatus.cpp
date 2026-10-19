// Apps/UltraClaude/engine/RepoStatus.cpp
// See RepoStatus.h.
// Version: 0.1.0
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework / ULTRA OS

#include "RepoStatus.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasUtils.h"   // RunProcessCaptured

#include <charconv>
#include <cstdio>
#include <utility>
#include <vector>

using namespace UltraCanvas;

namespace UltraClaude {

namespace {
    // An untracked file bigger than this is counted only this far: a build
    // artefact someone forgot to ignore must not stall the badge.
    constexpr size_t kMaxUntrackedBytes = 16u * 1024u * 1024u;

    // No prompt may wait for a person who is not there, and a remote that
    // does not answer is given up on: the badge then uses the base as it was
    // last fetched.
    const std::vector<std::pair<std::string, std::string>> kGitEnvironment = {
        {"GIT_TERMINAL_PROMPT", "0"},
        {"GCM_INTERACTIVE", "never"},
        {"GIT_ASKPASS", ""},
        {"SSH_ASKPASS", ""},
        {"GIT_SSH_COMMAND", "ssh -o BatchMode=yes -o ConnectTimeout=10"},
        {"GIT_HTTP_LOW_SPEED_LIMIT", "1000"},
        {"GIT_HTTP_LOW_SPEED_TIME", "10"},
        {"LC_ALL", "C"},   // messages and number formats git does not translate
    };

    struct GitResult {
        bool ok = false;
        std::string out;
        std::string error;
    };

    GitResult Git(const std::string& folder, std::vector<std::string> args) {
        std::vector<std::string> argv = {"git", "-C", folder};
        for (std::string& a : args) argv.push_back(std::move(a));
        const ProcessOutput run = RunProcessCaptured(argv, {}, kGitEnvironment);
        GitResult r;
        r.out.assign(run.standardOutput.begin(), run.standardOutput.end());
        r.ok = run.Succeeded();
        r.error = !run.started ? run.error : run.standardError;
        while (!r.error.empty() && (r.error.back() == '\n' || r.error.back() == '\r')) r.error.pop_back();
        return r;
    }

    std::string FirstLine(const std::string& text) {
        const size_t end = text.find_first_of("\r\n");
        return end == std::string::npos ? text : text.substr(0, end);
    }

    bool RefExists(const std::string& folder, const std::string& ref) {
        return Git(folder, {"rev-parse", "--verify", "--quiet", ref + "^{commit}"}).ok;
    }

    // origin/HEAD's target, else origin/main, else origin/master; "" when
    // the repository has none of them.
    std::string DefaultBase(const std::string& folder) {
        const GitResult head = Git(folder, {"symbolic-ref", "--quiet", "--short",
                                            "refs/remotes/origin/HEAD"});
        if (head.ok) {
            const std::string ref = FirstLine(head.out);
            if (!ref.empty()) return ref;
        }
        for (const char* candidate : {"origin/main", "origin/master"})
            if (RefExists(folder, candidate)) return candidate;
        return std::string();
    }

    // Sums "added<TAB>deleted<TAB>path" lines; binary files ("-") count 0.
    int64_t SumNumstat(const std::string& out) {
        int64_t total = 0;
        size_t pos = 0;
        while (pos < out.size()) {
            size_t end = out.find('\n', pos);
            if (end == std::string::npos) end = out.size();
            const char* p = out.data() + pos;
            const char* lineEnd = out.data() + end;
            for (int field = 0; field < 2 && p < lineEnd; ++field) {
                int64_t value = 0;
                const auto parsed = std::from_chars(p, lineEnd, value);
                if (parsed.ec == std::errc()) total += value;
                p = parsed.ptr;
                while (p < lineEnd && *p != '\t') ++p;   // skip "-" or the rest
                if (p < lineEnd) ++p;
            }
            pos = end + 1;
        }
        return total;
    }

    // Lines in a file as `wc -l` counts them: its newline characters.
    int64_t CountLines(const std::string& folder, const std::string& relative) {
        std::FILE* f = OpenFileUtf8(PathToUtf8(PathFromUtf8(folder) / PathFromUtf8(relative)), "rb");
        if (!f) return 0;
        int64_t lines = 0;
        size_t read = 0;
        char buffer[65536];
        size_t n;
        while (read < kMaxUntrackedBytes && (n = std::fread(buffer, 1, sizeof(buffer), f)) > 0) {
            read += n;
            for (size_t i = 0; i < n; ++i) if (buffer[i] == '\n') ++lines;
        }
        std::fclose(f);
        return lines;
    }
} // namespace

RepoLines MeasureLinesNotMerged(const std::string& folder, bool fetch) {
    RepoLines result;

    const GitResult inside = Git(folder, {"rev-parse", "--is-inside-work-tree"});
    if (!inside.ok) {
        // Either git is missing or this is no repository; only the first is
        // worth an error.
        result.state = inside.error.find("not a git repository") != std::string::npos
                           ? RepoLines::State::NotARepo : RepoLines::State::Failed;
        result.error = inside.error;
        return result;
    }
    if (FirstLine(inside.out) != "true") {   // inside .git, or a bare repository
        result.state = RepoLines::State::NotARepo;
        return result;
    }

    const GitResult branch = Git(folder, {"symbolic-ref", "--quiet", "--short", "HEAD"});
    if (branch.ok) result.branch = FirstLine(branch.out);

    result.base = DefaultBase(folder);
    if (result.base.empty()) {
        result.state = RepoLines::State::NoBase;
        return result;
    }

    if (fetch) {
        // "origin/main" -> fetch "main" from origin, which moves origin/main.
        const size_t slash = result.base.find('/');
        if (slash != std::string::npos) {
            result.fetched = Git(folder, {"fetch", "--quiet", "--no-tags",
                                          result.base.substr(0, slash),
                                          result.base.substr(slash + 1)}).ok;
        }
    }

    const GitResult mergeBase = Git(folder, {"merge-base", result.base, "HEAD"});
    if (!mergeBase.ok) {
        // No common history with the default branch (an orphan branch, or a
        // repository with no commit yet).
        result.state = RepoLines::State::NoBase;
        result.error = mergeBase.error;
        return result;
    }

    // The working tree against the merge base: committed and uncommitted
    // changes to tracked files alike. Renames are counted as the delete and
    // the add they are, as the closing line counts them.
    const GitResult diff = Git(folder, {"diff", "--numstat", "--no-renames",
                                        FirstLine(mergeBase.out)});
    if (!diff.ok) {
        result.state = RepoLines::State::Failed;
        result.error = diff.error;
        return result;
    }
    result.lines = SumNumstat(diff.out);

    const GitResult untracked = Git(folder, {"ls-files", "-z", "--others", "--exclude-standard"});
    if (untracked.ok) {
        size_t pos = 0;
        while (pos < untracked.out.size()) {
            size_t end = untracked.out.find('\0', pos);
            if (end == std::string::npos) end = untracked.out.size();
            if (end > pos) result.lines += CountLines(folder, untracked.out.substr(pos, end - pos));
            pos = end + 1;
        }
    }

    result.state = RepoLines::State::Measured;
    return result;
}

std::string FormatLineCount(int64_t lines) {
    if (lines < 0) lines = 0;
    if (lines < 10000) return std::to_string(lines);
    if (lines < 1000000) return std::to_string(lines / 1000) + "k";
    // One decimal without floating point: 1 234 567 -> "1.2M".
    const int64_t tenths = lines / 100000;
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + "M";   // locale-ok: integer digits only
}

} // namespace UltraClaude
