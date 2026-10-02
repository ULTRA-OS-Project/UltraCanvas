// core/UltraCanvasCoderModuleRepair.cpp
// Platform-free half of UltraCanvasCoderModuleRepair: the names, the .la
// rewrite and the repair of one folder. The Windows backend
// (OS/MSWindows/UltraCanvasWindowsCoderModuleRepair.cpp) answers which
// names System32 holds; every other platform's answer is in here.
// Version: 1.0.0
// Last Modified: 2026-10-01
// Author: UltraCanvas Framework
#include "UltraCanvasCoderModuleRepair.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace fs = std::filesystem;

namespace UltraCanvas {
    namespace CoderModuleRepair {

        std::string LowerCaseName(const std::string& name) {
            std::string out = name;
            for (char& c : out)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return out;
        }

        bool IsKnownSystemDllName(const std::string& lowerName) {
            return lowerName == "mpr.dll" || lowerName == "url.dll" ||
                   lowerName == "dpx.dll" || lowerName == "vid.dll";
        }

        bool IsDroppableCoder(const std::string& lowerName) {
            return lowerName == "mpr.dll" || lowerName == "url.dll";
        }

        std::string RenamedCoderFileName(const std::string& dllName) {
            const size_t dot = dllName.rfind('.');
            const std::string stem = dot == std::string::npos ? dllName : dllName.substr(0, dot);
            return stem + "-coder.dll";
        }

        std::string RewriteLibtoolArchive(const std::string& laText,
                                          const std::string& dlName) {
            std::string out;
            out.reserve(laText.size() + 16);
            size_t pos = 0;
            while (pos < laText.size()) {
                size_t end = laText.find('\n', pos);
                const bool last = end == std::string::npos;
                if (last) end = laText.size();
                std::string line = laText.substr(pos, end - pos);
                std::string eol;
                if (!line.empty() && line.back() == '\r') { line.pop_back(); eol = "\r"; }
                for (const char* key : {"dlname=", "library_names="}) {
                    const std::string k = key;
                    if (line.compare(0, k.size(), k) == 0) {
                        line = k + "'" + dlName + "'";
                        break;
                    }
                }
                out += line + eol;
                if (!last) out += '\n';
                pos = end + 1;
            }
            return out;
        }

        namespace {

            bool HasDllExtension(const std::string& lowerName) {
                return lowerName.size() > 4 &&
                       lowerName.compare(lowerName.size() - 4, 4, ".dll") == 0;
            }

            std::string ReadWholeFile(const fs::path& p, bool& ok) {
                std::ifstream in(p, std::ios::binary);
                ok = static_cast<bool>(in);
                if (!ok) return {};
                return std::string(std::istreambuf_iterator<char>(in),
                                   std::istreambuf_iterator<char>());
            }

            bool WriteWholeFile(const fs::path& p, const std::string& text) {
                std::ofstream out(p, std::ios::binary | std::ios::trunc);
                if (!out) return false;
                out << text;
                return static_cast<bool>(out);
            }

            std::string Reason(const std::error_code& ec) {
                return ec ? ec.message() : std::string("unknown error");
            }

        } // namespace

        CoderModuleRepairResult RepairCoderModules(
                const std::string& codersDirUtf8,
                const std::function<bool(const std::string&)>& isSystemDllName) {
            CoderModuleRepairResult result;
            const fs::path dir = PathFromUtf8(codersDirUtf8);
            std::error_code ec;
            if (!fs::is_directory(dir, ec)) return result;

            // The names first, the changes after: renaming while iterating
            // the same folder is undefined on some implementations.
            std::vector<std::string> names;
            for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
                std::error_code fec;
                if (!entry.is_regular_file(fec)) continue;
                const std::string name = PathToUtf8(entry.path().filename());
                if (HasDllExtension(LowerCaseName(name))) names.push_back(name);
            }

            for (const std::string& name : names) {
                const std::string lower = LowerCaseName(name);
                if (!isSystemDllName(lower)) continue;
                const fs::path dll = dir / PathFromUtf8(name);
                const fs::path la = dir / PathFromUtf8(name.substr(0, name.size() - 4) + ".la");
                const std::string laName = PathToUtf8(la.filename());
                const bool hasLa = fs::is_regular_file(la, ec);

                if (IsDroppableCoder(lower) || !hasLa) {
                    // Nothing could point ImageMagick at a renamed file
                    // without a .la, so a coder lacking one goes the same
                    // way as a pseudo-format: out.
                    if (fs::remove(dll, ec) || !ec) result.removed.push_back(name);
                    else { result.failed.push_back(name + ": " + Reason(ec)); continue; }
                    if (hasLa) {
                        if (fs::remove(la, ec) || !ec) result.removed.push_back(laName);
                        else result.failed.push_back(laName + ": " + Reason(ec));
                    }
                    continue;
                }

                // The DLL first: the shadowing name is what must go, and a
                // .la left pointing at the old name only costs one format
                // until the next start repairs the rest.
                const std::string renamed = RenamedCoderFileName(name);
                const fs::path target = dir / PathFromUtf8(renamed);
                if (fs::exists(target, ec)) {
                    // An earlier repair got this far: the stale original is
                    // all that is left to remove.
                    if (!fs::remove(dll, ec) && ec) {
                        result.failed.push_back(name + ": " + Reason(ec));
                        continue;
                    }
                } else {
                    fs::rename(dll, target, ec);
                    if (ec) { result.failed.push_back(name + ": " + Reason(ec)); continue; }
                }
                result.renamed.push_back(name + " -> " + renamed);

                bool readOk = false;
                const std::string laText = ReadWholeFile(la, readOk);
                if (!readOk) { result.failed.push_back(laName + ": could not be read"); continue; }
                const std::string rewritten = RewriteLibtoolArchive(laText, renamed);
                if (rewritten != laText && !WriteWholeFile(la, rewritten))
                    result.failed.push_back(laName + ": could not be written");
            }
            return result;
        }

        CoderModuleRepairResult RepairPackagedCoderModules(const std::string& exeDirUtf8) {
            CoderModuleRepairResult result;
#if defined(_WIN32) || defined(_WIN64)
            const fs::path lib = PathFromUtf8(exeDirUtf8) / "lib";
            std::error_code ec;
            if (!fs::is_directory(lib, ec)) return result;
            auto isSystem = [](const std::string& lowerName) {
                return IsKnownSystemDllName(lowerName) || NativeIsSystemDllName(lowerName);
            };
            for (const fs::directory_entry& entry : fs::directory_iterator(lib, ec)) {
                const std::string name = PathToUtf8(entry.path().filename());
                if (name.compare(0, 12, "ImageMagick-") != 0) continue;
                const fs::path coders = entry.path() / "modules-Q16HDRI" / "coders";
                CoderModuleRepairResult one =
                        RepairCoderModules(PathToUtf8(coders), isSystem);
                result.removed.insert(result.removed.end(), one.removed.begin(), one.removed.end());
                result.renamed.insert(result.renamed.end(), one.renamed.begin(), one.renamed.end());
                result.failed.insert(result.failed.end(), one.failed.begin(), one.failed.end());
            }
#else
            (void)exeDirUtf8;
#endif
            return result;
        }

#if !defined(_WIN32) && !defined(_WIN64)
        bool NativeIsSystemDllName(const std::string&) { return false; }
#endif

    } // namespace CoderModuleRepair
} // namespace UltraCanvas
