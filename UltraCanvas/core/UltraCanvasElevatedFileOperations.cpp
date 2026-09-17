// core/UltraCanvasElevatedFileOperations.cpp
// Platform-independent half of the "retry as administrator" service: the
// host-side entry points, the helper's own work (a delete is a delete on
// every platform), the command-line and report encodings the two processes
// agree on, and the fallbacks for platforms without a backend. Every
// operating-system call lives in OS/<Platform>/.
// Version: 1.0.0
// Last Modified: 2026-09-17
// Author: UltraCanvas Framework
#include "UltraCanvasElevatedFileOperations.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace UltraCanvas {
    namespace ElevatedFileOperations {

        namespace {
            // Set by RunHelperIfRequested: the host has wired the helper mode,
            // so relaunching this executable with the flag does the work
            // instead of opening a second main window.
            bool helperInstalled = false;

            // UTF-8 in, a path the platform opens correctly out. Local so this
            // file links on its own (the standalone test builds it without
            // UltraCanvasUtils).
            fs::path PathFromUtf8(const std::string& utf8) {
                return fs::path(std::u8string(utf8.begin(), utf8.end()));
            }

            // Lift the write protection of `p` and, for a folder, of everything
            // in it: a read-only file cannot be removed on Windows, and the
            // helper's whole point is to get things out of the way. Errors
            // are ignored here - the delete that follows reports the real one.
            void LiftReadOnly(const fs::path& p) {
                std::error_code ec;
                fs::permissions(p, fs::perms::owner_write, fs::perm_options::add, ec);
                if (!fs::is_directory(fs::symlink_status(p, ec))) return;
                // Not following directory symlinks / junctions: the delete
                // removes the link, not what it points at, and so must this.
                fs::recursive_directory_iterator it(
                        p, fs::directory_options::skip_permission_denied, ec);
                if (ec) return;
                for (const fs::recursive_directory_iterator end; it != end; it.increment(ec)) {
                    if (ec) break;
                    std::error_code pec;
                    fs::permissions(it->path(), fs::perms::owner_write,
                                    fs::perm_options::add, pec);
                }
            }
        }

        // ===== NO-BACKEND FALLBACKS =====
#if !defined(_WIN32) && !defined(_WIN64)
        bool NativeProcessIsElevated() { return false; }

        bool NativeIsPermissionFailure(const std::error_code& ec) {
            return ec == std::errc::permission_denied ||
                   ec == std::errc::operation_not_permitted;
        }

        std::vector<std::string> NativeCommandLineArguments() { return {}; }

        bool NativeRunElevatedDelete(const std::vector<std::string>&,
                                     ElevatedDeleteResult&) {
            return false;
        }
#endif

        // ===== HOST SIDE =====
        bool RunHelperIfRequested(int argc, char** argv, int& exitCode) {
            helperInstalled = true;
            std::vector<std::string> args = NativeCommandLineArguments();
            if (args.empty()) {
                for (int i = 0; i < argc; ++i)
                    args.emplace_back(argv[i] ? argv[i] : "");
            }
            if (args.size() < 2 || args[1] != kHelperDeleteFlag) return false;

            // From here on this process IS the helper: whatever happens, the
            // caller returns exitCode instead of starting the application.
            if (args.size() < 3 || args[2].empty()) {
                exitCode = kHelperExitBadArguments;
                return true;
            }
            const std::vector<std::string> paths(args.begin() + 3, args.end());
            std::vector<ElevatedFailure> failures;
            exitCode = RunHelperDelete(paths, failures);

            // The report is the only channel back to the caller: an empty one
            // is written too, so the caller can tell "nothing failed" from
            // "the helper never got this far".
            std::ofstream report(PathFromUtf8(args[2]),
                                 std::ios::binary | std::ios::trunc);
            if (report) report << FormatReport(failures);
            else if (exitCode == kHelperExitOk) exitCode = kHelperExitPartial;
            return true;
        }

        bool ProcessIsElevated() { return NativeProcessIsElevated(); }

        bool IsAvailable() {
#if defined(_WIN32) || defined(_WIN64)
            return helperInstalled && !NativeProcessIsElevated();
#else
            return false;
#endif
        }

        bool IsPermissionFailure(const std::error_code& ec) {
            if (!ec) return false;
            return NativeIsPermissionFailure(ec);
        }

        ElevatedDeleteResult DeleteElevated(const std::vector<std::string>& paths) {
            ElevatedDeleteResult result;
            if (paths.empty()) {
                result.outcome = ElevatedOutcome::Completed;
                return result;
            }
            if (!IsAvailable()) {
                result.outcome = ElevatedOutcome::Unavailable;
                result.error = helperInstalled
                        ? "Retrying with administrator rights is not available here."
                        : "The application has not enabled retrying with "
                          "administrator rights.";
                return result;
            }
            if (!NativeRunElevatedDelete(paths, result)) {
                result.outcome = ElevatedOutcome::Unavailable;
                result.error = "Retrying with administrator rights is not "
                               "available on this platform.";
            }
            return result;
        }

        // ===== THE HELPER'S WORK =====
        int RunHelperDelete(const std::vector<std::string>& paths,
                            std::vector<ElevatedFailure>& failures) {
            if (paths.empty()) return kHelperExitBadArguments;
            for (const std::string& utf8 : paths) {
                const fs::path p = PathFromUtf8(utf8);
                // Only what the caller named in full: a relative path would
                // resolve against whatever the elevated process's working
                // directory happens to be.
                if (utf8.empty() || !p.is_absolute()) {
                    failures.push_back({utf8, "Not an absolute path"});
                    continue;
                }
                LiftReadOnly(p);
                std::error_code ec;
                fs::remove_all(p, ec);   // an entry already gone is a success
                if (ec) failures.push_back({utf8, ec.message()});
            }
            return failures.empty() ? kHelperExitOk : kHelperExitPartial;
        }

        // ===== REPORT ENCODING =====
        std::string FormatReport(const std::vector<ElevatedFailure>& failures) {
            std::string out;
            for (const ElevatedFailure& f : failures) {
                out += f.path;
                out += '\t';
                // A reason must stay on its line; Windows never puts a tab or
                // a newline in a path, and a reason from FormatMessage ends
                // in CR LF, which would otherwise split it.
                for (char c : f.reason) {
                    if (c == '\r') continue;
                    out += (c == '\n' || c == '\t') ? ' ' : c;
                }
                out += '\n';
            }
            return out;
        }

        std::vector<ElevatedFailure> ParseReport(const std::string& report) {
            std::vector<ElevatedFailure> out;
            size_t pos = 0;
            while (pos < report.size()) {
                size_t end = report.find('\n', pos);
                if (end == std::string::npos) end = report.size();
                std::string line = report.substr(pos, end - pos);
                pos = end + 1;
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;
                const size_t tab = line.find('\t');
                ElevatedFailure f;
                f.path = line.substr(0, tab);
                if (tab != std::string::npos) f.reason = line.substr(tab + 1);
                out.push_back(std::move(f));
            }
            return out;
        }

        // ===== COMMAND LINE ENCODING =====
        std::string QuoteCommandLineArgument(const std::string& argument) {
            // The rules CommandLineToArgvW / the C runtime parse by: an
            // argument without spaces, tabs or quotes passes through
            // unchanged (a bare empty one would vanish, so it is quoted);
            // otherwise it is wrapped in quotes, every quote inside becomes
            // \", and the backslashes right before a quote (or the closing
            // one) are doubled so they stay backslashes.
            if (!argument.empty() &&
                argument.find_first_of(" \t\"") == std::string::npos)
                return argument;
            std::string out = "\"";
            size_t backslashes = 0;
            for (char c : argument) {
                if (c == '\\') { ++backslashes; continue; }
                if (c == '"') out.append(backslashes * 2 + 1, '\\');
                else          out.append(backslashes, '\\');
                backslashes = 0;
                out += c;
            }
            out.append(backslashes * 2, '\\');
            out += '"';
            return out;
        }

        std::vector<std::vector<std::string>> SplitIntoCommandLines(
                const std::vector<std::string>& paths,
                size_t prefixLength, size_t maxLength) {
            std::vector<std::vector<std::string>> runs;
            size_t length = prefixLength;
            for (const std::string& path : paths) {
                const size_t cost = QuoteCommandLineArgument(path).size() + 1;   // + separator
                if (!runs.empty() && !runs.back().empty() &&
                    length + cost > maxLength) {
                    runs.emplace_back();
                    length = prefixLength;
                }
                if (runs.empty()) runs.emplace_back();
                runs.back().push_back(path);
                length += cost;
            }
            return runs;
        }

    } // namespace ElevatedFileOperations
} // namespace UltraCanvas
