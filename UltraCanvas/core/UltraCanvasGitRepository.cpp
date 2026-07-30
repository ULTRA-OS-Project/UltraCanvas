// core/UltraCanvasGitRepository.cpp
// On-disk Git repository reader: refs, loose objects, packfiles and delta
// chains. Uses the vendored miniz for zlib inflate.
//
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#include "UltraCanvasGitRepository.h"

#include "miniz.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace UltraCanvas {

namespace {

namespace fs = std::filesystem;

constexpr int kMaxDeltaDepth = 64;

std::string Trim(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string();
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

bool ReadWholeFile(const fs::path& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    return true;
}

std::string ToHex(const uint8_t* bytes, size_t count) {
    static const char* digits = "0123456789abcdef";
    std::string hex;
    hex.reserve(count * 2);
    for (size_t i = 0; i < count; ++i) {
        hex.push_back(digits[bytes[i] >> 4]);
        hex.push_back(digits[bytes[i] & 0x0F]);
    }
    return hex;
}

bool FromHex(const std::string& hex, uint8_t* out, size_t count) {
    if (hex.size() < count * 2) return false;
    auto value = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < count; ++i) {
        const int hi = value(hex[i * 2]);
        const int lo = value(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

uint32_t ReadBigEndian32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |  static_cast<uint32_t>(p[3]);
}

uint64_t ReadBigEndian64(const uint8_t* p) {
    return (static_cast<uint64_t>(ReadBigEndian32(p)) << 32) | ReadBigEndian32(p + 4);
}

// Inflate a complete zlib stream held in memory. `expectedSize` is a hint; 0
// means unknown (loose objects), in which case the buffer grows as needed.
bool InflateBuffer(const uint8_t* data, size_t size, size_t expectedSize, std::string& out) {
    mz_stream stream{};
    if (mz_inflateInit(&stream) != MZ_OK) return false;

    stream.next_in  = data;
    stream.avail_in = static_cast<unsigned int>(size);

    std::string result;
    result.resize(expectedSize > 0 ? expectedSize : std::max<size_t>(size * 4, 1024));

    size_t written = 0;
    int status = MZ_OK;
    while (true) {
        if (written == result.size()) result.resize(result.size() * 2);

        stream.next_out  = reinterpret_cast<unsigned char*>(&result[written]);
        stream.avail_out = static_cast<unsigned int>(result.size() - written);

        status = mz_inflate(&stream, MZ_SYNC_FLUSH);
        written = result.size() - stream.avail_out;

        if (status == MZ_STREAM_END) break;
        if (status != MZ_OK) { mz_inflateEnd(&stream); return false; }
        if (stream.avail_in == 0 && stream.avail_out != 0) {
            mz_inflateEnd(&stream);
            return false;                       // Truncated stream
        }
    }

    mz_inflateEnd(&stream);
    result.resize(written);
    out.swap(result);
    return true;
}

// Applies a git delta (base-size varint, result-size varint, then copy/insert
// instructions) to `base`.
bool ApplyDelta(const std::string& base, const std::string& delta, std::string& out) {
    size_t pos = 0;
    auto readVarint = [&](uint64_t& value) -> bool {
        value = 0;
        int shift = 0;
        while (pos < delta.size()) {
            const uint8_t byte = static_cast<uint8_t>(delta[pos++]);
            value |= static_cast<uint64_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) return true;
            shift += 7;
            if (shift > 63) return false;
        }
        return false;
    };

    uint64_t baseSize = 0, resultSize = 0;
    if (!readVarint(baseSize) || !readVarint(resultSize)) return false;
    if (baseSize != base.size()) return false;

    std::string result;
    result.reserve(static_cast<size_t>(resultSize));

    while (pos < delta.size()) {
        const uint8_t opcode = static_cast<uint8_t>(delta[pos++]);

        if (opcode & 0x80) {
            // Copy from the base: the low bits say which offset/size bytes follow.
            uint64_t offset = 0, size = 0;
            if (opcode & 0x01) offset |= static_cast<uint64_t>(static_cast<uint8_t>(delta[pos++]));
            if (opcode & 0x02) offset |= static_cast<uint64_t>(static_cast<uint8_t>(delta[pos++])) << 8;
            if (opcode & 0x04) offset |= static_cast<uint64_t>(static_cast<uint8_t>(delta[pos++])) << 16;
            if (opcode & 0x08) offset |= static_cast<uint64_t>(static_cast<uint8_t>(delta[pos++])) << 24;
            if (opcode & 0x10) size   |= static_cast<uint64_t>(static_cast<uint8_t>(delta[pos++]));
            if (opcode & 0x20) size   |= static_cast<uint64_t>(static_cast<uint8_t>(delta[pos++])) << 8;
            if (opcode & 0x40) size   |= static_cast<uint64_t>(static_cast<uint8_t>(delta[pos++])) << 16;
            if (size == 0) size = 0x10000;

            if (offset + size > base.size()) return false;
            result.append(base, static_cast<size_t>(offset), static_cast<size_t>(size));
        } else if (opcode != 0) {
            // Insert `opcode` literal bytes.
            if (pos + opcode > delta.size()) return false;
            result.append(delta, pos, opcode);
            pos += opcode;
        } else {
            return false;                       // Opcode 0 is reserved
        }
    }

    if (result.size() != resultSize) return false;
    out.swap(result);
    return true;
}

const char* ObjectTypeName(int type) {
    switch (type) {
        case 1:  return "commit";
        case 2:  return "tree";
        case 3:  return "blob";
        case 4:  return "tag";
        default: return "";
    }
}

// One packfile: the whole .idx in memory (24 bytes per object) plus a handle on
// the .pack, which is read on demand so a multi-gigabyte pack is never
// slurped into RAM.
struct PackFile {
    std::string           idx;
    mutable std::ifstream pack;
    uint32_t              objectCount = 0;

    // Offsets into `idx` of the sections of a v2 index.
    size_t namesOffset       = 0;
    size_t smallOffsetsBase  = 0;
    size_t largeOffsetsBase  = 0;

    bool Load(const fs::path& idxPath, const fs::path& packPath) {
        if (!ReadWholeFile(idxPath, idx)) return false;
        if (idx.size() < 8 + 256 * 4) return false;

        const uint8_t* raw = reinterpret_cast<const uint8_t*>(idx.data());
        static const uint8_t magic[4] = {0xFF, 0x74, 0x4F, 0x63};
        if (std::memcmp(raw, magic, 4) != 0) return false;          // v1 unsupported
        if (ReadBigEndian32(raw + 4) != 2) return false;

        const size_t fanoutBase = 8;
        objectCount = ReadBigEndian32(raw + fanoutBase + 255 * 4);

        namesOffset      = fanoutBase + 256 * 4;
        smallOffsetsBase = namesOffset + static_cast<size_t>(objectCount) * 20
                                       + static_cast<size_t>(objectCount) * 4;   // names + crc
        largeOffsetsBase = smallOffsetsBase + static_cast<size_t>(objectCount) * 4;

        if (idx.size() < largeOffsetsBase) return false;

        pack.open(packPath, std::ios::binary);
        return pack.is_open();
    }

    // Binary search the sorted name table.
    bool FindOffset(const uint8_t* sha, uint64_t& outOffset) const {
        if (objectCount == 0) return false;
        const uint8_t* raw = reinterpret_cast<const uint8_t*>(idx.data());
        const uint8_t* names = raw + namesOffset;

        const size_t bucket = sha[0];
        uint32_t low  = (bucket == 0) ? 0 : ReadBigEndian32(raw + 8 + (bucket - 1) * 4);
        uint32_t high = ReadBigEndian32(raw + 8 + bucket * 4);

        while (low < high) {
            const uint32_t mid = low + (high - low) / 2;
            const int cmp = std::memcmp(sha, names + static_cast<size_t>(mid) * 20, 20);
            if (cmp == 0) {
                const uint32_t small =
                    ReadBigEndian32(raw + smallOffsetsBase + static_cast<size_t>(mid) * 4);
                if (small & 0x80000000u) {
                    const size_t largeIndex = small & 0x7FFFFFFFu;
                    const size_t at = largeOffsetsBase + largeIndex * 8;
                    if (at + 8 > idx.size()) return false;
                    outOffset = ReadBigEndian64(raw + at);
                } else {
                    outOffset = small;
                }
                return true;
            }
            if (cmp < 0) high = mid; else low = mid + 1;
        }
        return false;
    }

    bool ReadRange(uint64_t offset, size_t length, std::string& out) const {
        pack.clear();
        pack.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!pack) return false;
        out.resize(length);
        pack.read(&out[0], static_cast<std::streamsize>(length));
        out.resize(static_cast<size_t>(pack.gcount()));
        return !out.empty();
    }

    uint64_t Size() const {
        pack.clear();
        pack.seekg(0, std::ios::end);
        return static_cast<uint64_t>(pack.tellg());
    }
};

} // namespace

// =============================================================================
// IMPLEMENTATION
// =============================================================================

struct UltraCanvasGitRepository::Impl {
    std::string gitDirectory;
    std::string lastError;
    bool open = false;

    std::vector<std::unique_ptr<PackFile>> packs;
    bool packsLoaded = false;

    // Walk state (newest commit first).
    struct WalkEntry {
        int64_t     date;
        std::string sha;
        bool operator<(const WalkEntry& other) const {
            // std::priority_queue is a max-heap: newest date wins, sha breaks ties.
            if (date != other.date) return date < other.date;
            return sha > other.sha;
        }
    };
    std::priority_queue<WalkEntry> frontier;
    std::unordered_set<std::string> seen;
    bool walkStarted = false;

    // ----- object access -------------------------------------------------

    void LoadPacks() {
        if (packsLoaded) return;
        packsLoaded = true;

        const fs::path packDirectory = fs::path(gitDirectory) / "objects" / "pack";
        std::error_code ec;
        if (!fs::is_directory(packDirectory, ec)) return;

        for (const fs::directory_entry& entry : fs::directory_iterator(packDirectory, ec)) {
            if (ec) break;
            if (entry.path().extension() != ".idx") continue;

            fs::path packPath = entry.path();
            packPath.replace_extension(".pack");
            if (!fs::exists(packPath, ec)) continue;

            auto pack = std::make_unique<PackFile>();
            if (pack->Load(entry.path(), packPath)) packs.push_back(std::move(pack));
        }
    }

    bool ReadLooseObject(const std::string& sha, std::string& type, std::string& body) {
        if (sha.size() < 3) return false;
        const fs::path path = fs::path(gitDirectory) / "objects" / sha.substr(0, 2)
                                                     / sha.substr(2);
        std::error_code ec;
        if (!fs::exists(path, ec)) return false;

        std::string raw;
        if (!ReadWholeFile(path, raw)) return false;

        std::string inflated;
        if (!InflateBuffer(reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), 0,
                           inflated)) {
            return false;
        }

        // "<type> <size>\0<body>"
        const size_t nul = inflated.find('\0');
        if (nul == std::string::npos) return false;
        const std::string header = inflated.substr(0, nul);
        const size_t space = header.find(' ');
        if (space == std::string::npos) return false;

        type = header.substr(0, space);
        body = inflated.substr(nul + 1);
        return true;
    }

    // Reads one object out of a pack, resolving OFS_DELTA / REF_DELTA chains.
    bool ReadPackedObjectAt(const PackFile& pack, uint64_t offset, int depth,
                            std::string& type, std::string& body) {
        if (depth > kMaxDeltaDepth) return false;

        std::string header;
        if (!pack.ReadRange(offset, 32, header)) return false;

        size_t pos = 0;
        uint8_t byte = static_cast<uint8_t>(header[pos++]);
        int objectType = (byte >> 4) & 0x07;
        uint64_t size = byte & 0x0F;
        int shift = 4;
        while (byte & 0x80) {
            if (pos >= header.size()) return false;
            byte = static_cast<uint8_t>(header[pos++]);
            size |= static_cast<uint64_t>(byte & 0x7F) << shift;
            shift += 7;
        }

        std::string baseType, baseBody;
        uint64_t dataOffset = offset + pos;

        if (objectType == 6) {                  // OFS_DELTA
            if (pos >= header.size()) return false;
            byte = static_cast<uint8_t>(header[pos++]);
            uint64_t relative = byte & 0x7F;
            while (byte & 0x80) {
                if (pos >= header.size()) return false;
                byte = static_cast<uint8_t>(header[pos++]);
                relative = ((relative + 1) << 7) | (byte & 0x7F);
            }
            if (relative > offset) return false;
            if (!ReadPackedObjectAt(pack, offset - relative, depth + 1, baseType, baseBody)) {
                return false;
            }
            dataOffset = offset + pos;
        } else if (objectType == 7) {           // REF_DELTA
            if (pos + 20 > header.size()) return false;
            const std::string baseSha =
                ToHex(reinterpret_cast<const uint8_t*>(header.data()) + pos, 20);
            pos += 20;
            dataOffset = offset + pos;
            if (!ReadAnyObject(baseSha, baseType, baseBody, depth + 1)) return false;
        }

        // Inflate the entry payload. The compressed length is unknown, so feed
        // the inflater a window that certainly covers it.
        const uint64_t packSize = pack.Size();
        const uint64_t available = (packSize > dataOffset) ? (packSize - dataOffset) : 0;
        const size_t window = static_cast<size_t>(
            std::min<uint64_t>(available, std::max<uint64_t>(size * 2 + 4096, 65536)));

        std::string compressed;
        if (!pack.ReadRange(dataOffset, window, compressed)) return false;

        std::string payload;
        if (!InflateBuffer(reinterpret_cast<const uint8_t*>(compressed.data()),
                           compressed.size(), static_cast<size_t>(size), payload)) {
            // Retry with everything left in the pack for pathological ratios.
            if (window >= available ||
                !pack.ReadRange(dataOffset, static_cast<size_t>(available), compressed) ||
                !InflateBuffer(reinterpret_cast<const uint8_t*>(compressed.data()),
                               compressed.size(), static_cast<size_t>(size), payload)) {
                return false;
            }
        }

        if (objectType == 6 || objectType == 7) {
            std::string resolved;
            if (!ApplyDelta(baseBody, payload, resolved)) return false;
            type = baseType;
            body.swap(resolved);
            return true;
        }

        const char* name = ObjectTypeName(objectType);
        if (!*name) return false;
        type = name;
        body.swap(payload);
        return true;
    }

    bool ReadAnyObject(const std::string& sha, std::string& type, std::string& body,
                       int depth = 0) {
        if (sha.size() != 40) return false;
        if (ReadLooseObject(sha, type, body)) return true;

        LoadPacks();
        uint8_t raw[20];
        if (!FromHex(sha, raw, 20)) return false;

        for (const std::unique_ptr<PackFile>& pack : packs) {
            uint64_t offset = 0;
            if (!pack->FindOffset(raw, offset)) continue;
            if (ReadPackedObjectAt(*pack, offset, depth, type, body)) return true;
        }
        return false;
    }

    // Follows tag objects down to the commit they point at.
    std::string PeelToCommit(const std::string& sha, int depth = 0) {
        if (depth > 16) return std::string();
        std::string type, body;
        if (!ReadAnyObject(sha, type, body)) return std::string();
        if (type == "commit") return sha;
        if (type != "tag") return std::string();

        std::istringstream stream(body);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.rfind("object ", 0) == 0) return PeelToCommit(Trim(line.substr(7)), depth + 1);
            if (line.empty()) break;
        }
        return std::string();
    }

    // ----- commit parsing -------------------------------------------------

    static void ParseIdentity(const std::string& line, std::string& name,
                              std::string& email, int64_t& timestamp) {
        const size_t open = line.find('<');
        const size_t close = line.find('>', open == std::string::npos ? 0 : open);
        if (open != std::string::npos) name = Trim(line.substr(0, open));
        if (open != std::string::npos && close != std::string::npos) {
            email = line.substr(open + 1, close - open - 1);
        }
        if (close != std::string::npos) {
            timestamp = std::strtoll(Trim(line.substr(close + 1)).c_str(), nullptr, 10);
        }
    }

    static bool ParseCommitBody(const std::string& sha, const std::string& body,
                                GitGraphCommit& commit) {
        commit = GitGraphCommit();
        commit.sha = sha;

        size_t pos = 0;
        bool inHeader = true;
        std::string message;

        while (pos <= body.size()) {
            const size_t end = body.find('\n', pos);
            const std::string line = body.substr(pos, (end == std::string::npos)
                                                          ? std::string::npos
                                                          : end - pos);
            if (inHeader) {
                if (line.empty()) {
                    inHeader = false;
                } else if (line.rfind("parent ", 0) == 0) {
                    commit.parents.push_back(Trim(line.substr(7)));
                } else if (line.rfind("author ", 0) == 0) {
                    ParseIdentity(line.substr(7), commit.authorName, commit.authorEmail,
                                  commit.authorDate);
                } else if (line.rfind("committer ", 0) == 0) {
                    std::string email;
                    ParseIdentity(line.substr(10), commit.committerName, email,
                                  commit.commitDate);
                }
                // tree, gpgsig continuation lines and anything else are ignored.
            } else {
                if (!message.empty()) message.push_back('\n');
                message += line;
            }

            if (end == std::string::npos) break;
            pos = end + 1;
        }

        const size_t firstBreak = message.find('\n');
        commit.subject = (firstBreak == std::string::npos) ? message
                                                           : message.substr(0, firstBreak);
        if (firstBreak != std::string::npos) {
            commit.body = Trim(message.substr(firstBreak + 1));
        }
        if (commit.commitDate == 0) commit.commitDate = commit.authorDate;
        return true;
    }

    bool LoadCommit(const std::string& sha, GitGraphCommit& commit) {
        std::string type, body;
        if (!ReadAnyObject(sha, type, body)) return false;
        if (type != "commit") return false;
        return ParseCommitBody(sha, body, commit);
    }

    // ----- refs -----------------------------------------------------------

    void CollectRefsFromDirectory(const fs::path& base, const std::string& prefix,
                                  std::unordered_map<std::string, std::string>& refs) {
        std::error_code ec;
        const fs::path directory = fs::path(gitDirectory) / base;
        if (!fs::is_directory(directory, ec)) return;

        for (const fs::directory_entry& entry :
             fs::recursive_directory_iterator(directory, ec)) {
            if (ec) break;
            if (!entry.is_regular_file(ec)) continue;

            std::string content;
            if (!ReadWholeFile(entry.path(), content)) continue;
            content = Trim(content);
            if (content.empty()) continue;

            const std::string relative =
                fs::relative(entry.path(), directory, ec).generic_string();
            if (relative.empty()) continue;

            if (content.rfind("ref: ", 0) == 0) continue;      // Symbolic, skip
            refs[prefix + relative] = content;
        }
    }

    void CollectPackedRefs(std::unordered_map<std::string, std::string>& refs,
                           std::unordered_map<std::string, std::string>& peeled) {
        std::string content;
        if (!ReadWholeFile(fs::path(gitDirectory) / "packed-refs", content)) return;

        std::istringstream stream(content);
        std::string line;
        std::string previousRef;
        while (std::getline(stream, line)) {
            line = Trim(line);
            if (line.empty() || line[0] == '#') continue;

            if (line[0] == '^') {                              // Peeled tag target
                if (!previousRef.empty()) peeled[previousRef] = Trim(line.substr(1));
                continue;
            }
            const size_t space = line.find(' ');
            if (space == std::string::npos) continue;

            const std::string sha  = line.substr(0, space);
            const std::string name = Trim(line.substr(space + 1));
            if (refs.find(name) == refs.end()) refs[name] = sha;   // Loose wins
            previousRef = name;
        }
    }
};

// =============================================================================
// PUBLIC API
// =============================================================================

UltraCanvasGitRepository::UltraCanvasGitRepository() : impl(std::make_unique<Impl>()) {}
UltraCanvasGitRepository::~UltraCanvasGitRepository() = default;

bool UltraCanvasGitRepository::Open(const std::string& path) {
    Close();

    std::error_code ec;
    fs::path candidate = fs::absolute(fs::path(path), ec);
    if (ec) {
        impl->lastError = "cannot resolve '" + path + "'";
        return false;
    }

    // Accept a .git directory, a working tree, a .git file (worktree or
    // submodule), or any directory inside a working tree.
    for (int depth = 0; depth < 64; ++depth) {
        if (candidate.filename() == ".git" && fs::is_directory(candidate, ec)) break;

        const fs::path dotGit = candidate / ".git";
        if (fs::is_directory(dotGit, ec)) {
            candidate = dotGit;
            break;
        }
        if (fs::is_regular_file(dotGit, ec)) {
            std::string content;
            if (ReadWholeFile(dotGit, content) && content.rfind("gitdir:", 0) == 0) {
                fs::path target = Trim(content.substr(7));
                if (target.is_relative()) target = candidate / target;
                candidate = fs::weakly_canonical(target, ec);
                break;
            }
        }
        // HEAD + objects means this already is a git directory.
        if (fs::exists(candidate / "HEAD", ec) && fs::exists(candidate / "objects", ec)) break;

        const fs::path parent = candidate.parent_path();
        if (parent.empty() || parent == candidate) {
            impl->lastError = "no git repository found at or above '" + path + "'";
            return false;
        }
        candidate = parent;
    }

    if (!fs::exists(candidate / "HEAD", ec)) {
        impl->lastError = "'" + candidate.string() + "' is not a git directory (no HEAD)";
        return false;
    }

    impl->gitDirectory = candidate.string();
    impl->open = true;
    impl->lastError.clear();
    return true;
}

void UltraCanvasGitRepository::Close() {
    impl->gitDirectory.clear();
    impl->open = false;
    impl->packs.clear();
    impl->packsLoaded = false;
    impl->seen.clear();
    impl->walkStarted = false;
    while (!impl->frontier.empty()) impl->frontier.pop();
}

bool UltraCanvasGitRepository::IsOpen() const { return impl->open; }
const std::string& UltraCanvasGitRepository::GetGitDirectory() const { return impl->gitDirectory; }
const std::string& UltraCanvasGitRepository::GetLastError() const { return impl->lastError; }

std::string UltraCanvasGitRepository::ReadCurrentBranch() {
    if (!impl->open) return std::string();

    std::string content;
    if (!ReadWholeFile(fs::path(impl->gitDirectory) / "HEAD", content)) return std::string();
    content = Trim(content);
    if (content.rfind("ref: ", 0) != 0) return std::string();       // Detached

    std::string ref = Trim(content.substr(5));
    const std::string prefix = "refs/heads/";
    return (ref.rfind(prefix, 0) == 0) ? ref.substr(prefix.size()) : ref;
}

std::string UltraCanvasGitRepository::ReadHeadSha() {
    if (!impl->open) return std::string();

    std::string content;
    if (!ReadWholeFile(fs::path(impl->gitDirectory) / "HEAD", content)) return std::string();
    content = Trim(content);

    if (content.rfind("ref: ", 0) != 0) return content;             // Detached HEAD

    const std::string ref = Trim(content.substr(5));
    std::string target;
    if (ReadWholeFile(fs::path(impl->gitDirectory) / ref, target)) return Trim(target);

    // Fall back to packed-refs.
    std::unordered_map<std::string, std::string> refs, peeled;
    impl->CollectPackedRefs(refs, peeled);
    auto it = refs.find(ref);
    return (it == refs.end()) ? std::string() : it->second;
}

std::vector<GitGraphRef> UltraCanvasGitRepository::ReadRefs() {
    std::vector<GitGraphRef> result;
    if (!impl->open) return result;

    std::unordered_map<std::string, std::string> refs, peeled;
    impl->CollectRefsFromDirectory("refs/heads",   "refs/heads/",   refs);
    impl->CollectRefsFromDirectory("refs/tags",    "refs/tags/",    refs);
    impl->CollectRefsFromDirectory("refs/remotes", "refs/remotes/", refs);
    impl->CollectPackedRefs(refs, peeled);

    const std::string currentBranch = ReadCurrentBranch();

    for (const auto& entry : refs) {
        const std::string& fullName = entry.first;
        const std::string& sha = entry.second;

        GitGraphRef ref;
        ref.sha = sha;

        if (fullName.rfind("refs/heads/", 0) == 0) {
            ref.name = fullName.substr(11);
            ref.type = GitGraphRefType::LocalBranch;
            ref.isCurrent = (!currentBranch.empty() && ref.name == currentBranch);
        } else if (fullName.rfind("refs/tags/", 0) == 0) {
            ref.name = fullName.substr(10);
            ref.type = GitGraphRefType::Tag;

            // Annotated tags point at a tag object - peel it to the commit.
            auto peel = peeled.find(fullName);
            if (peel != peeled.end()) {
                ref.sha = peel->second;
                ref.annotated = true;
            } else {
                const std::string commitSha = impl->PeelToCommit(sha);
                if (!commitSha.empty() && commitSha != sha) {
                    ref.sha = commitSha;
                    ref.annotated = true;
                }
            }
        } else if (fullName.rfind("refs/remotes/", 0) == 0) {
            const std::string rest = fullName.substr(13);
            const size_t slash = rest.find('/');
            if (slash == std::string::npos) continue;
            ref.remote = rest.substr(0, slash);
            ref.name   = rest.substr(slash + 1);
            if (ref.name == "HEAD") continue;              // Symbolic remote head
            ref.type = GitGraphRefType::RemoteBranch;
        } else {
            continue;
        }

        result.push_back(ref);
    }

    std::sort(result.begin(), result.end(),
              [](const GitGraphRef& a, const GitGraphRef& b) {
                  if (a.type != b.type) return static_cast<int>(a.type) < static_cast<int>(b.type);
                  return a.DisplayName() < b.DisplayName();
              });
    return result;
}

bool UltraCanvasGitRepository::ReadObject(const std::string& sha,
                                          const std::string& expectedType,
                                          std::string& outData, std::string& outType) {
    if (!impl->open) return false;
    if (!impl->ReadAnyObject(sha, outType, outData)) return false;
    return expectedType.empty() || outType == expectedType;
}

bool UltraCanvasGitRepository::ReadCommit(const std::string& sha, GitGraphCommit& outCommit) {
    if (!impl->open) return false;
    return impl->LoadCommit(sha, outCommit);
}

void UltraCanvasGitRepository::RestartWalk() {
    while (!impl->frontier.empty()) impl->frontier.pop();
    impl->seen.clear();
    impl->walkStarted = false;
}

bool UltraCanvasGitRepository::IsWalkComplete() const {
    return impl->walkStarted && impl->frontier.empty();
}

std::vector<GitGraphCommit> UltraCanvasGitRepository::WalkMore(size_t count) {
    std::vector<GitGraphCommit> result;
    if (!impl->open || count == 0) return result;

    if (!impl->walkStarted) {
        impl->walkStarted = true;
        // Seed from every ref tip plus HEAD, so detached heads are not lost.
        std::vector<std::string> tips;
        for (const GitGraphRef& ref : ReadRefs()) tips.push_back(ref.sha);
        const std::string head = ReadHeadSha();
        if (!head.empty()) tips.push_back(head);

        for (const std::string& tip : tips) {
            if (tip.size() != 40) continue;
            if (!impl->seen.insert(tip).second) continue;
            GitGraphCommit commit;
            if (!impl->LoadCommit(tip, commit)) {
                impl->seen.erase(tip);
                continue;
            }
            impl->frontier.push({commit.commitDate, tip});
        }
    }

    while (result.size() < count && !impl->frontier.empty()) {
        const Impl::WalkEntry entry = impl->frontier.top();
        impl->frontier.pop();

        GitGraphCommit commit;
        if (!impl->LoadCommit(entry.sha, commit)) continue;
        result.push_back(commit);

        for (const std::string& parent : commit.parents) {
            if (parent.size() != 40) continue;
            if (!impl->seen.insert(parent).second) continue;

            GitGraphCommit parentCommit;
            if (!impl->LoadCommit(parent, parentCommit)) {
                impl->seen.erase(parent);
                continue;
            }
            impl->frontier.push({parentCommit.commitDate, parent});
        }
    }

    return result;
}

std::vector<GitGraphCommit> UltraCanvasGitRepository::ReadCommits(size_t limit) {
    RestartWalk();
    if (limit > 0) return WalkMore(limit);

    std::vector<GitGraphCommit> all;
    while (true) {
        std::vector<GitGraphCommit> chunk = WalkMore(4096);
        if (chunk.empty()) break;
        all.insert(all.end(), chunk.begin(), chunk.end());
    }
    return all;
}

// =============================================================================
// DATA SOURCE ADAPTER
// =============================================================================

UltraCanvasGitRepositorySource::UltraCanvasGitRepositorySource(
        std::shared_ptr<UltraCanvasGitRepository> repository)
    : repository(std::move(repository)) {}

std::vector<GitGraphCommit> UltraCanvasGitRepositorySource::FetchCommits(size_t offset,
                                                                        size_t count) {
    if (!repository) return {};

    // The walker only moves forward, so keep everything it has produced and
    // serve any offset from that cache.
    while (walked.size() < offset + count) {
        std::vector<GitGraphCommit> chunk =
            repository->WalkMore(offset + count - walked.size());
        if (chunk.empty()) break;
        walked.insert(walked.end(), chunk.begin(), chunk.end());
    }

    if (offset >= walked.size()) return {};
    const size_t end = std::min(walked.size(), offset + count);
    return std::vector<GitGraphCommit>(walked.begin() + static_cast<long>(offset),
                                       walked.begin() + static_cast<long>(end));
}

std::vector<GitGraphRef> UltraCanvasGitRepositorySource::FetchRefs() {
    return repository ? repository->ReadRefs() : std::vector<GitGraphRef>();
}

} // namespace UltraCanvas
