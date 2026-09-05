// core/UltraCanvasShellLink.cpp
// Reader for the Windows shell link format (MS-SHLLINK) - the ".lnk" files
// that carry a Windows desktop. Pure byte parsing plus std::filesystem, so
// the same code answers on Windows, ULTRA OS, Linux and macOS; nothing here
// calls the shell.
//
// What is parsed, in the order the format stores it: the 76-byte header
// (flags, target attributes, icon index), the target ID list (skipped - it
// describes shell items, not files), the LinkInfo block (which holds the
// target path of every shortcut an installer or a drag-with-Alt creates),
// the five optional strings (comment, relative path, working directory,
// arguments, icon location) and finally the extra-data blocks, of which two
// matter here: the environment-variable block and the icon-environment
// block, the un-expanded forms of the target and the icon path.
// Version: 1.0.0
// Last Modified: 2026-09-05
// Author: UltraCanvas Framework
#include "UltraCanvasShellLink.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace UltraCanvas {

    namespace {

        // ===== FORMAT CONSTANTS =====
        constexpr uint32_t kShellLinkHeaderSize = 0x0000004C;
        // CLSID 00021401-0000-0000-C000-000000000046, stored the way the
        // format stores a GUID: the first three fields little-endian, the
        // last eight bytes in order.
        constexpr std::array<uint8_t, 16> kShellLinkClsid = {
                0x01, 0x14, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
                0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46};

        enum ShellLinkFlags : uint32_t {
            HasLinkTargetIDList = 0x00000001,
            HasLinkInfo         = 0x00000002,
            HasName             = 0x00000004,
            HasRelativePath     = 0x00000008,
            HasWorkingDir       = 0x00000010,
            HasArguments        = 0x00000020,
            HasIconLocation     = 0x00000040,
            IsUnicode           = 0x00000080,
            ForceNoLinkInfo     = 0x00000100,
        };

        constexpr uint32_t kFileAttributeDirectory = 0x00000010;

        // Extra-data block signatures.
        constexpr uint32_t kEnvironmentVariableDataBlock = 0xA0000001;
        constexpr uint32_t kIconEnvironmentDataBlock     = 0xA0000007;

        // A shortcut is a few kilobytes; anything past this is not one, and
        // reading it whole would be the wrong thing to do to a file that
        // merely ends in ".lnk".
        constexpr size_t kMaxShellLinkBytes = 4 * 1024 * 1024;

        // ===== LITTLE-ENDIAN READS, BOUNDS CHECKED =====
        // Every offset in a shell link comes out of the file itself, so all
        // of them are attacker-controlled: a read past the end must produce
        // "no value", never a crash.
        bool ReadU16(const std::vector<uint8_t>& b, size_t at, uint16_t& out) {
            if (at + 2 > b.size()) return false;
            out = static_cast<uint16_t>(b[at] | (b[at + 1] << 8));
            return true;
        }

        bool ReadU32(const std::vector<uint8_t>& b, size_t at, uint32_t& out) {
            if (at + 4 > b.size()) return false;
            out = static_cast<uint32_t>(b[at]) |
                  (static_cast<uint32_t>(b[at + 1]) << 8) |
                  (static_cast<uint32_t>(b[at + 2]) << 16) |
                  (static_cast<uint32_t>(b[at + 3]) << 24);
            return true;
        }

        bool ReadU64(const std::vector<uint8_t>& b, size_t at, uint64_t& out) {
            uint32_t lo = 0, hi = 0;
            if (!ReadU32(b, at, lo) || !ReadU32(b, at + 4, hi)) return false;
            out = (static_cast<uint64_t>(hi) << 32) | lo;
            return true;
        }

        // ===== TEXT =====
        void AppendUtf8(std::string& out, uint32_t cp) {
            if (cp < 0x80) {
                out += static_cast<char>(cp);
            } else if (cp < 0x800) {
                out += static_cast<char>(0xC0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                out += static_cast<char>(0xE0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                out += static_cast<char>(0xF0 | (cp >> 18));
                out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }

        // UTF-16LE -> UTF-8, stopping at the first NUL or after `maxChars`.
        std::string Utf16LeToUtf8(const std::vector<uint8_t>& b, size_t at,
                                  size_t maxChars) {
            std::string out;
            for (size_t i = 0; i < maxChars; ++i) {
                uint16_t unit = 0;
                if (!ReadU16(b, at + i * 2, unit) || unit == 0) break;
                uint32_t cp = unit;
                if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < maxChars) {
                    uint16_t low = 0;
                    if (ReadU16(b, at + (i + 1) * 2, low) && low >= 0xDC00 &&
                        low <= 0xDFFF) {
                        cp = 0x10000 + ((static_cast<uint32_t>(unit - 0xD800) << 10) |
                                        (low - 0xDC00));
                        ++i;
                    }
                }
                AppendUtf8(out, cp);
            }
            return out;
        }

        // The format's non-Unicode strings are in the code page of the
        // machine that wrote the link, which the file does not record.
        // Latin-1 is the reading that never produces invalid UTF-8 and that
        // is right for the ASCII paths almost every such link holds; links
        // written by a modern Windows carry the Unicode strings above.
        std::string AnsiToUtf8(const std::vector<uint8_t>& b, size_t at,
                               size_t maxBytes) {
            std::string out;
            for (size_t i = 0; i < maxBytes && at + i < b.size(); ++i) {
                const uint8_t c = b[at + i];
                if (c == 0) break;
                AppendUtf8(out, c);
            }
            return out;
        }

        // ===== PATH HELPERS =====
        std::string ToLowerAscii(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return s;
        }

        bool EqualsNoCase(const std::string& a, const std::string& b) {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(a[i])) !=
                    std::tolower(static_cast<unsigned char>(b[i])))
                    return false;
            }
            return true;
        }

        std::vector<std::string> SplitWindowsPath(const std::string& path) {
            std::vector<std::string> parts;
            std::string current;
            for (char c : path) {
                if (c == '\\' || c == '/') {
                    if (!current.empty()) parts.push_back(current);
                    current.clear();
                } else {
                    current += c;
                }
            }
            if (!current.empty()) parts.push_back(current);
            return parts;
        }

        // Case-insensitive lookup of one child. The exact name is tried
        // first: on a case-insensitive host (Windows, a default macOS disk)
        // that is the whole cost, and on Linux it is the common case too,
        // because the names in a shortcut were copied from the disk.
        bool FindChildNoCase(const fs::path& dir, const std::string& name,
                             fs::path& out) {
            std::error_code ec;
            fs::path direct = dir / name;
            if (fs::exists(direct, ec) && !ec) { out = direct; return true; }
            for (fs::directory_iterator it(dir, ec), end; it != end;
                 it.increment(ec)) {
                if (ec) break;
                if (EqualsNoCase(it->path().filename().string(), name)) {
                    out = it->path();
                    return true;
                }
            }
            return false;
        }

        // The path as the rest of the system should see it: symlinks
        // resolved and "." / ".." folded away. A drive letter is reached
        // through the dosdevices symlink of a Wine prefix, and an entry that
        // kept that spelling would not compare equal to the same file as the
        // folder tree lists it.
        std::string CanonicalOrSelf(const fs::path& path) {
            std::error_code ec;
            const fs::path canonical = fs::weakly_canonical(path, ec);
            if (ec || canonical.empty()) return path.string();
            return canonical.string();
        }

#ifndef _WIN32
        // ===== EVERYTHING BELOW MAPS A WINDOWS PATH ONTO A HOST THAT IS NOT
        // WINDOWS =====
        // On Windows the path already names a file the system can open, so
        // none of this is compiled there.

        // "C:\dir\file" -> letter 'c' and the components after it. False for
        // anything that is not a drive-letter path (a UNC name, a relative
        // path, a shell namespace string).
        bool SplitDrivePath(const std::string& winPath, char& outLetter,
                            std::vector<std::string>& outParts) {
            if (winPath.size() < 2 || winPath[1] != ':' ||
                !std::isalpha(static_cast<unsigned char>(winPath[0])))
                return false;
            outLetter = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(winPath[0])));
            outParts = SplitWindowsPath(winPath.substr(2));
            return true;
        }

        // Walk `parts` down from `root`, matching each component
        // case-insensitively. "" when any of them is missing.
        std::string DescendPath(const fs::path& root,
                                const std::vector<std::string>& parts) {
            std::error_code ec;
            if (!fs::is_directory(root, ec) || ec) return {};
            fs::path current = root;
            for (const std::string& part : parts) {
                if (part.empty() || part == ".") continue;
                if (part == "..") { current = current.parent_path(); continue; }
                fs::path next;
                if (!FindChildNoCase(current, part, next)) return {};
                current = next;
            }
            return CanonicalOrSelf(current);
        }

        // ===== WHERE A WINDOWS DRIVE LIVES ON THIS HOST =====
        // Only ever asked off Windows. Three layouts answer it, in the order
        // a shortcut is most likely to be sitting in one: inside a Wine
        // prefix, on a mounted Windows disk, or - for a shortcut that is
        // somewhere else entirely - in the prefix this session is configured
        // for.
        // Does `dir` hold this child? Only the spellings actually used on
        // disk are tried, and each is one stat: this runs for every ancestor
        // of a shortcut, and a case-insensitive directory scan per ancestor
        // would make listing a folder of shortcuts a directory walk of the
        // whole path above it. A case-insensitive host matches on the first
        // spelling anyway.
        bool HasChildNamed(const fs::path& dir,
                           std::initializer_list<const char*> names) {
            std::error_code ec;
            for (const char* name : names) {
                if (fs::exists(dir / name, ec) && !ec) return true;
            }
            return false;
        }

        void AddCandidate(std::vector<fs::path>& out, const fs::path& p) {
            if (p.empty()) return;
            std::error_code ec;
            if (!fs::is_directory(p, ec) || ec) return;
            for (const fs::path& existing : out)
                if (existing == p) return;
            out.push_back(p);
        }

        // The drive root a Wine prefix serves for `letter`: dosdevices holds
        // one symlink per drive ("c:" -> "../drive_c", "z:" -> "/"), which is
        // exactly the mapping wanted here.
        void AddPrefixCandidate(std::vector<fs::path>& out,
                                const fs::path& prefix, char letter) {
            AddCandidate(out, prefix / "dosdevices" / (std::string(1, letter) + ":"));
            if (letter == 'c') AddCandidate(out, prefix / "drive_c");
        }

        std::vector<fs::path> DriveRootCandidates(char letter,
                                                  const std::string& contextPath) {
            std::vector<fs::path> roots;
            std::error_code ec;
            fs::path dir = fs::absolute(fs::path(contextPath), ec);
            if (ec) dir = fs::path(contextPath);
            if (!contextPath.empty()) {
                if (!fs::is_directory(dir, ec) || ec) dir = dir.parent_path();
                for (; !dir.empty() && dir != dir.root_path();
                     dir = dir.parent_path()) {
                    // Inside a Wine prefix: .../<prefix>/drive_c/...
                    if (EqualsNoCase(dir.filename().string(), "drive_c"))
                        AddPrefixCandidate(roots, dir.parent_path(), letter);
                    // The prefix itself.
                    if (HasChildNamed(dir, {"dosdevices"}))
                        AddPrefixCandidate(roots, dir, letter);
                    // The root of a mounted Windows system disk. Only C: is
                    // recognisable this way, which is the letter that
                    // matters: it is where the programs a shortcut points at
                    // are installed.
                    if (letter == 'c' &&
                        HasChildNamed(dir, {"Windows", "windows", "WINDOWS"}) &&
                        HasChildNamed(dir, {"Users", "users", "USERS"}))
                        AddCandidate(roots, dir);
                }
            }
            if (const char* prefix = std::getenv("WINEPREFIX"))
                AddPrefixCandidate(roots, fs::path(prefix), letter);
            if (const char* home = std::getenv("HOME"))
                AddPrefixCandidate(roots, fs::path(home) / ".wine", letter);
            return roots;
        }
#endif // !_WIN32

        // ===== ENVIRONMENT VARIABLES =====
        // Off Windows there is no Windows environment to read, so the
        // well-known variables are answered from the fixed layout of a
        // Windows disk. The per-user ones need a profile, which the caller
        // derives from the shortcut's own location when it can.
        std::string WindowsEnvValue(const std::string& name,
                                    const std::string& userProfile) {
#ifdef _WIN32
            if (const char* v = std::getenv(name.c_str())) return v;
#endif
            const std::string key = ToLowerAscii(name);
            if (key == "systemdrive" || key == "homedrive")   return "C:";
            if (key == "systemroot" || key == "windir")       return "C:\\Windows";
            if (key == "programfiles" || key == "programw6432")
                return "C:\\Program Files";
            if (key == "programfiles(x86)")                   return "C:\\Program Files (x86)";
            if (key == "commonprogramfiles")                  return "C:\\Program Files\\Common Files";
            if (key == "commonprogramfiles(x86)")             return "C:\\Program Files (x86)\\Common Files";
            if (key == "programdata" || key == "allusersprofile")
                return "C:\\ProgramData";
            if (key == "public")                              return "C:\\Users\\Public";
            if (!userProfile.empty()) {
                if (key == "userprofile" || key == "homepath") return userProfile;
                if (key == "appdata")      return userProfile + "\\AppData\\Roaming";
                if (key == "localappdata") return userProfile + "\\AppData\\Local";
                if (key == "temp" || key == "tmp")
                    return userProfile + "\\AppData\\Local\\Temp";
            }
            return {};
        }

        std::string ExpandWithProfile(const std::string& path,
                                      const std::string& userProfile) {
            std::string out;
            size_t at = 0;
            while (at < path.size()) {
                const size_t open = path.find('%', at);
                if (open == std::string::npos) { out += path.substr(at); break; }
                const size_t close = path.find('%', open + 1);
                if (close == std::string::npos) { out += path.substr(at); break; }
                out += path.substr(at, open - at);
                const std::string name = path.substr(open + 1, close - open - 1);
                const std::string value = WindowsEnvValue(name, userProfile);
                // An unknown variable stays as it was written: a path with a
                // visible "%FOO%" in it is a readable failure, while one with
                // the reference silently dropped looks valid and is not.
                out += value.empty() ? path.substr(open, close - open + 1) : value;
                at = close + 1;
            }
            return out;
        }

        // "C:\Users\Someone" for a host path that sits inside a Windows user
        // profile - the shortcut's own folder, in practice. Empty when it
        // does not, and then the per-user variables stay unexpanded.
        std::string UserProfileFromContext(const std::string& contextPath) {
            if (contextPath.empty()) return {};
            std::error_code ec;
            fs::path dir = fs::absolute(fs::path(contextPath), ec);
            if (ec) dir = fs::path(contextPath);
            std::string user;
            for (fs::path p = dir; !p.empty() && p != p.root_path();
                 p = p.parent_path()) {
                if (EqualsNoCase(p.filename().string(), "users") &&
                    !user.empty())
                    return "C:\\Users\\" + user;
                user = p.filename().string();
            }
            return {};
        }

        // ===== READING THE FILE =====
        bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out) {
            std::error_code ec;
            if (!fs::is_regular_file(path, ec) || ec) return false;
            const uintmax_t size = fs::file_size(path, ec);
            if (ec || size < kShellLinkHeaderSize || size > kMaxShellLinkBytes)
                return false;
            std::ifstream in(path, std::ios::binary);
            if (!in) return false;
            out.resize(static_cast<size_t>(size));
            in.read(reinterpret_cast<char*>(out.data()),
                    static_cast<std::streamsize>(out.size()));
            return static_cast<size_t>(in.gcount()) == out.size();
        }

        std::time_t FileTimeToTimeT(uint64_t fileTime) {
            if (fileTime == 0) return 0;
            // FILETIME counts 100-nanosecond ticks since 1601-01-01.
            constexpr uint64_t kTicksPerSecond = 10000000ULL;
            constexpr uint64_t kEpochDifference = 11644473600ULL;
            const uint64_t seconds = fileTime / kTicksPerSecond;
            if (seconds < kEpochDifference) return 0;
            return static_cast<std::time_t>(seconds - kEpochDifference);
        }

        // The LinkInfo structure: the target as a drive-letter path
        // (VolumeIDAndLocalBasePath) or as a UNC name
        // (CommonNetworkRelativeLink), either of them completed by the
        // common path suffix.
        std::string ParseLinkInfo(const std::vector<uint8_t>& b, size_t at,
                                  bool& outIsNetwork) {
            outIsNetwork = false;
            uint32_t size = 0, headerSize = 0, flags = 0;
            if (!ReadU32(b, at, size) || !ReadU32(b, at + 4, headerSize) ||
                !ReadU32(b, at + 8, flags))
                return {};
            if (size < 0x1C || at + size > b.size() || headerSize < 0x1C)
                return {};
            uint32_t localBasePathOffset = 0, networkOffset = 0, suffixOffset = 0;
            if (!ReadU32(b, at + 16, localBasePathOffset) ||
                !ReadU32(b, at + 20, networkOffset) ||
                !ReadU32(b, at + 24, suffixOffset))
                return {};
            uint32_t localBasePathOffsetUnicode = 0, suffixOffsetUnicode = 0;
            const bool hasUnicode = headerSize >= 0x24;
            if (hasUnicode &&
                (!ReadU32(b, at + 28, localBasePathOffsetUnicode) ||
                 !ReadU32(b, at + 32, suffixOffsetUnicode)))
                return {};

            const size_t maxChars = size;   // the block bounds every string
            std::string suffix;
            if (hasUnicode && suffixOffsetUnicode > 0 &&
                suffixOffsetUnicode < size)
                suffix = Utf16LeToUtf8(b, at + suffixOffsetUnicode, maxChars);
            if (suffix.empty() && suffixOffset > 0 && suffixOffset < size)
                suffix = AnsiToUtf8(b, at + suffixOffset, maxChars);

            std::string base;
            if (flags & 0x1) {   // VolumeIDAndLocalBasePath
                if (hasUnicode && localBasePathOffsetUnicode > 0 &&
                    localBasePathOffsetUnicode < size)
                    base = Utf16LeToUtf8(b, at + localBasePathOffsetUnicode,
                                         maxChars);
                if (base.empty() && localBasePathOffset > 0 &&
                    localBasePathOffset < size)
                    base = AnsiToUtf8(b, at + localBasePathOffset, maxChars);
            } else if ((flags & 0x2) && networkOffset > 0 &&
                       networkOffset < size) {
                // CommonNetworkRelativeLink: the share the target lives on.
                const size_t net = at + networkOffset;
                uint32_t netNameOffset = 0;
                if (ReadU32(b, net + 8, netNameOffset) && netNameOffset >= 0x14) {
                    uint32_t netNameOffsetUnicode = 0;
                    if (netNameOffset > 0x14 &&
                        ReadU32(b, net + 20, netNameOffsetUnicode) &&
                        netNameOffsetUnicode > 0)
                        base = Utf16LeToUtf8(b, net + netNameOffsetUnicode,
                                             maxChars);
                    if (base.empty())
                        base = AnsiToUtf8(b, net + netNameOffset, maxChars);
                    if (!base.empty()) outIsNetwork = true;
                }
            }
            if (base.empty()) return {};
            if (suffix.empty()) return base;
            if (base.back() != '\\' && suffix.front() != '\\') base += '\\';
            return base + suffix;
        }

        // One StringData entry: a character count followed by that many
        // characters, without a terminator. `at` is advanced past it.
        bool ReadSizedString(const std::vector<uint8_t>& b, size_t& at,
                             bool unicode, std::string& out) {
            uint16_t count = 0;
            if (!ReadU16(b, at, count)) return false;
            at += 2;
            const size_t bytes = unicode ? count * 2u : count;
            if (at + bytes > b.size()) return false;
            out = unicode ? Utf16LeToUtf8(b, at, count) : AnsiToUtf8(b, at, count);
            at += bytes;
            return true;
        }

        // Extra-data blocks. Two of them hold a path in its un-expanded
        // form ("%ProgramFiles%\App\App.exe"), which is what makes a
        // shortcut written on another machine resolve on this one.
        void ParseExtraData(const std::vector<uint8_t>& b, size_t at,
                            std::string& outEnvTarget, std::string& outEnvIcon) {
            while (at + 8 <= b.size()) {
                uint32_t size = 0, signature = 0;
                if (!ReadU32(b, at, size) || !ReadU32(b, at + 4, signature)) return;
                if (size < 0x04) return;             // terminal block
                if (size < 8 || at + size > b.size()) return;
                if (signature == kEnvironmentVariableDataBlock ||
                    signature == kIconEnvironmentDataBlock) {
                    // TargetAnsi: 260 bytes at +8. TargetUnicode: 520 bytes
                    // (260 characters) at +268, present when the block is the
                    // full 0x314 bytes.
                    std::string value;
                    if (size >= 0x314) value = Utf16LeToUtf8(b, at + 268, 260);
                    if (value.empty()) value = AnsiToUtf8(b, at + 8, 260);
                    if (!value.empty()) {
                        if (signature == kEnvironmentVariableDataBlock)
                            outEnvTarget = value;
                        else
                            outEnvIcon = value;
                    }
                }
                at += size;
            }
        }

    } // namespace

    bool IsShellLinkPath(const std::string& path) {
        const size_t dot = path.find_last_of('.');
        if (dot == std::string::npos) return false;
        return EqualsNoCase(path.substr(dot + 1), "lnk");
    }

    std::string ExpandWindowsEnvironmentPath(const std::string& path) {
        return ExpandWithProfile(path, {});
    }

    std::string ResolveWindowsPathOnHost(const std::string& windowsPath,
                                         const std::string& contextPath) {
        if (windowsPath.empty()) return {};
        const std::string expanded =
                ExpandWithProfile(windowsPath, UserProfileFromContext(contextPath));
        std::error_code ec;
#ifdef _WIN32
        // The path is already a path this system understands.
        std::string native = expanded;
        std::replace(native.begin(), native.end(), '/', '\\');
        if (fs::exists(native, ec) && !ec) return CanonicalOrSelf(native);
        return {};
#else
        // A path that is already a host path (a shortcut whose target was
        // written by Wine's own tooling, or a caller passing one through).
        if (!expanded.empty() && expanded.front() == '/') {
            if (fs::exists(expanded, ec) && !ec) return CanonicalOrSelf(expanded);
            return {};
        }
        char letter = 0;
        std::vector<std::string> parts;
        if (!SplitDrivePath(expanded, letter, parts)) return {};
        for (const fs::path& root : DriveRootCandidates(letter, contextPath)) {
            const std::string resolved = DescendPath(root, parts);
            if (!resolved.empty()) return resolved;
        }
        return {};
#endif
    }

    bool ReadShellLink(const std::string& linkPath, UCShellLink& out) {
        std::vector<uint8_t> b;
        if (!ReadWholeFile(linkPath, b)) return false;

        uint32_t headerSize = 0;
        if (!ReadU32(b, 0, headerSize) || headerSize != kShellLinkHeaderSize)
            return false;
        if (b.size() < kShellLinkHeaderSize) return false;
        if (!std::equal(kShellLinkClsid.begin(), kShellLinkClsid.end(),
                        b.begin() + 4))
            return false;

        UCShellLink link;
        uint32_t flags = 0, attributes = 0, fileSize = 0;
        uint32_t iconIndex = 0;
        uint64_t writeTime = 0;
        ReadU32(b, 0x14, flags);
        ReadU32(b, 0x18, attributes);
        ReadU64(b, 0x2C, writeTime);
        ReadU32(b, 0x34, fileSize);
        ReadU32(b, 0x38, iconIndex);
        link.targetAttributes = attributes;
        link.targetIsDirectory = (attributes & kFileAttributeDirectory) != 0;
        link.targetSize = fileSize;
        link.targetModifiedTime = FileTimeToTimeT(writeTime);
        link.iconIndex = static_cast<int32_t>(iconIndex);

        size_t at = kShellLinkHeaderSize;
        if (flags & HasLinkTargetIDList) {
            // The ID list describes shell items rather than files; a
            // shortcut to a file repeats the path in the LinkInfo block
            // below, and one to a virtual item (a Control Panel page, a
            // packaged app) has no file path at all. Skipped, but its size
            // is what says where LinkInfo starts.
            uint16_t idListSize = 0;
            if (!ReadU16(b, at, idListSize)) return false;
            at += 2 + idListSize;
            if (at > b.size()) return false;
        }

        std::string linkInfoTarget;
        if ((flags & HasLinkInfo) && !(flags & ForceNoLinkInfo)) {
            bool isNetwork = false;
            linkInfoTarget = ParseLinkInfo(b, at, isNetwork);
            link.targetIsNetworkPath = isNetwork;
            uint32_t linkInfoSize = 0;
            if (!ReadU32(b, at, linkInfoSize) || linkInfoSize < 4) return false;
            at += linkInfoSize;
            if (at > b.size()) return false;
        }

        const bool unicode = (flags & IsUnicode) != 0;
        // StringData, always in this order, each one present only when its
        // flag is set.
        if (flags & HasName)
            if (!ReadSizedString(b, at, unicode, link.description)) return false;
        if (flags & HasRelativePath)
            if (!ReadSizedString(b, at, unicode, link.relativePath)) return false;
        if (flags & HasWorkingDir)
            if (!ReadSizedString(b, at, unicode, link.workingDirectory)) return false;
        if (flags & HasArguments)
            if (!ReadSizedString(b, at, unicode, link.arguments)) return false;
        if (flags & HasIconLocation)
            if (!ReadSizedString(b, at, unicode, link.iconLocation)) return false;

        std::string envTarget, envIcon;
        ParseExtraData(b, at, envTarget, envIcon);

        // The target, from the most concrete statement of it to the least.
        // Whichever of them this host can actually find wins - a link
        // written on another machine often has an absolute path that is
        // wrong here and an environment form that is right.
        std::vector<std::string> candidates;
        if (!linkInfoTarget.empty()) candidates.push_back(linkInfoTarget);
        if (!envTarget.empty()) candidates.push_back(envTarget);
        const fs::path linkDir = fs::path(linkPath).parent_path();
        for (const std::string& candidate : candidates) {
            const std::string host = ResolveWindowsPathOnHost(candidate, linkPath);
            if (!host.empty()) {
                link.targetPath = candidate;
                link.hostTargetPath = host;
                break;
            }
        }
        if (link.targetPath.empty() && !candidates.empty())
            link.targetPath = candidates.front();
        if (link.hostTargetPath.empty() && !link.relativePath.empty()) {
            // ".\\App.exe" beside the shortcut: the one target form that
            // needs no drive mapping at all, and the one a portable
            // installation relies on.
            std::error_code ec;
            fs::path relative = linkDir;
            for (const std::string& part : SplitWindowsPath(link.relativePath)) {
                if (part == ".") continue;
                if (part == "..") { relative = relative.parent_path(); continue; }
                fs::path next;
                if (!FindChildNoCase(relative, part, next)) { relative.clear(); break; }
                relative = next;
            }
            if (!relative.empty() && fs::exists(relative, ec) && !ec) {
                link.hostTargetPath = CanonicalOrSelf(relative);
                if (link.targetPath.empty()) link.targetPath = link.relativePath;
            }
        }

        // The icon: the link's own icon location, else the one its
        // environment block names, else the target itself.
        if (link.iconLocation.empty() && !envIcon.empty())
            link.iconLocation = envIcon;
        if (!link.iconLocation.empty()) {
            link.hostIconLocation =
                    ResolveWindowsPathOnHost(link.iconLocation, linkPath);
            if (link.hostIconLocation.empty() && !envIcon.empty())
                link.hostIconLocation =
                        ResolveWindowsPathOnHost(envIcon, linkPath);
        }

        out = std::move(link);
        return true;
    }

} // namespace UltraCanvas
