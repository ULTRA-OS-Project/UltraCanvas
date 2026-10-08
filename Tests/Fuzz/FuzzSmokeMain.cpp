// Tests/Fuzz/FuzzSmokeMain.cpp
// A stand-in for libFuzzer's driver, so the fuzz targets also run under any
// compiler as an ordinary ctest: every seed of the corpus once, then a fixed
// number of deterministic mutations of them (bit flips, dictionary tokens,
// deleted, repeated and spliced ranges). A crash, a sanitizer report or a
// hang fails the test. It finds far less than a real fuzzing run - its job is
// to keep the targets building and the seed corpus passing, and to catch the
// shallow cases (deep nesting, huge counts) on every build.
//
//   FuzzSmoke [-runs=N] [-seed=N] [-max_len=N] [-dict=FILE] [-save_last=FILE]
//             FILE_OR_DIR...
//
// The same flags as libFuzzer's, so a corpus directory, a dictionary and a
// crash file given to one work with the other. -save_last writes each input
// to FILE before running it: after a crash it holds the input that crashed.
// Version: 1.0.0
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework

#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

namespace {

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

bool ReadInputFile(const std::string& utf8Path, Bytes& out) {
    FILE* f = UltraCanvas::OpenFileUtf8(utf8Path, "rb");
    if (!f) return false;
    out.clear();
    uint8_t buffer[4096];
    size_t n;
    while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0) out.insert(out.end(), buffer, buffer + n);
    std::fclose(f);
    return true;
}

void WriteInputFile(const std::string& utf8Path, const Bytes& data) {
    FILE* f = UltraCanvas::OpenFileUtf8(utf8Path, "wb");
    if (!f) return;
    if (!data.empty()) std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
}

// A libFuzzer dictionary: one entry per line, name="value" or "value", with
// \\, \" and \xNN escapes; # starts a comment.
std::vector<Bytes> ReadDictionary(const std::string& utf8Path) {
    std::vector<Bytes> entries;
    Bytes text;
    if (!ReadInputFile(utf8Path, text)) return entries;
    std::string all(text.begin(), text.end());
    size_t pos = 0;
    while (pos < all.size()) {
        size_t end = all.find('\n', pos);
        if (end == std::string::npos) end = all.size();
        const std::string line = all.substr(pos, end - pos);
        pos = end + 1;
        const size_t open = line.find('"');
        const size_t close = line.rfind('"');
        if (line.empty() || line[0] == '#' || open == std::string::npos || close <= open) continue;
        Bytes entry;
        for (size_t i = open + 1; i < close; ++i) {
            if (line[i] == '\\' && i + 1 < close) {
                const char c = line[++i];
                if (c == 'x' && i + 2 < close) {
                    entry.push_back(static_cast<uint8_t>(std::strtol(line.substr(i + 1, 2).c_str(), nullptr, 16)));
                    i += 2;
                } else {
                    entry.push_back(static_cast<uint8_t>(c));
                }
            } else {
                entry.push_back(static_cast<uint8_t>(line[i]));
            }
        }
        if (!entry.empty()) entries.push_back(std::move(entry));
    }
    return entries;
}

void CollectInputs(const std::string& utf8Path, std::vector<Bytes>& seeds) {
    const fs::path path = UltraCanvas::PathFromUtf8(utf8Path);
    std::error_code ec;
    if (fs::is_directory(path, ec)) {
        std::vector<std::string> files;
        for (const auto& entry : fs::directory_iterator(path, ec))
            if (entry.is_regular_file(ec)) files.push_back(UltraCanvas::PathToUtf8(entry.path()));
        std::sort(files.begin(), files.end());     // the same order on every system
        for (const auto& file : files) {
            Bytes data;
            if (ReadInputFile(file, data)) seeds.push_back(std::move(data));
        }
        return;
    }
    Bytes data;
    if (ReadInputFile(utf8Path, data)) seeds.push_back(std::move(data));
    else std::fprintf(stderr, "FuzzSmoke: cannot read %s\n", utf8Path.c_str());
}

class Mutator {
public:
    Mutator(uint32_t seed, std::vector<Bytes> dict, size_t maxLen)
        : rng(seed), dictionary(std::move(dict)), maxLength(maxLen) {}

    Bytes Mutate(const std::vector<Bytes>& seeds) {
        Bytes data = seeds.empty() ? Bytes{} : seeds[Below(seeds.size())];
        const int steps = 1 + static_cast<int>(Below(4));
        for (int i = 0; i < steps; ++i) MutateOnce(data, seeds);
        if (data.size() > maxLength) data.resize(maxLength);
        return data;
    }

private:
    std::mt19937 rng;
    std::vector<Bytes> dictionary;
    size_t maxLength;

    size_t Below(size_t n) { return n == 0 ? 0 : std::uniform_int_distribution<size_t>(0, n - 1)(rng); }

    void MutateOnce(Bytes& data, const std::vector<Bytes>& seeds) {
        const size_t at = Below(data.size() + 1);
        switch (Below(8)) {
            case 0:   // flip a bit
                if (!data.empty()) data[Below(data.size())] ^= static_cast<uint8_t>(1u << Below(8));
                break;
            case 1:   // a random byte, often a structural one
                if (!data.empty()) {
                    static const char kSpecial[] = "<>/=\"'{}();:,#.[]*!@%-\\&\0\n ";
                    data[Below(data.size())] = Below(2) ? static_cast<uint8_t>(Below(256))
                                                        : static_cast<uint8_t>(kSpecial[Below(sizeof(kSpecial) - 1)]);
                }
                break;
            case 2:   // insert a dictionary token
            case 3:
                if (!dictionary.empty()) {
                    const Bytes& token = dictionary[Below(dictionary.size())];
                    data.insert(data.begin() + static_cast<std::ptrdiff_t>(at), token.begin(), token.end());
                }
                break;
            case 4: { // delete a range
                if (data.empty()) break;
                const size_t from = Below(data.size());
                const size_t len = 1 + Below(std::min<size_t>(64, data.size() - from));
                data.erase(data.begin() + static_cast<std::ptrdiff_t>(from),
                           data.begin() + static_cast<std::ptrdiff_t>(from + len));
                break;
            }
            case 5: { // repeat a range many times (deep nesting, long lists)
                if (data.empty()) break;
                const size_t from = Below(data.size());
                const size_t len = 1 + Below(std::min<size_t>(16, data.size() - from));
                const Bytes slice(data.begin() + static_cast<std::ptrdiff_t>(from),
                                  data.begin() + static_cast<std::ptrdiff_t>(from + len));
                const size_t times = 1 + Below(Below(2) ? 8 : 4000);
                Bytes repeated;
                for (size_t t = 0; t < times && repeated.size() < maxLength; ++t)
                    repeated.insert(repeated.end(), slice.begin(), slice.end());
                data.insert(data.begin() + static_cast<std::ptrdiff_t>(from), repeated.begin(), repeated.end());
                break;
            }
            case 6: { // splice in part of another seed
                if (seeds.empty()) break;
                const Bytes& other = seeds[Below(seeds.size())];
                if (other.empty()) break;
                const size_t from = Below(other.size());
                const size_t len = 1 + Below(other.size() - from);
                data.insert(data.begin() + static_cast<std::ptrdiff_t>(at),
                            other.begin() + static_cast<std::ptrdiff_t>(from),
                            other.begin() + static_cast<std::ptrdiff_t>(from + len));
                break;
            }
            default:  // cut it short
                if (!data.empty()) data.resize(Below(data.size()));
                break;
        }
    }
};

bool FlagValue(const char* arg, const char* name, std::string& value) {
    const size_t n = std::strlen(name);
    if (std::strncmp(arg, name, n) != 0) return false;
    value = arg + n;
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    long runs = 10000;
    uint32_t seed = 1;
    size_t maxLen = 64 * 1024;
    std::string dictPath, saveLast;
    std::vector<Bytes> seeds;
    for (int i = 1; i < argc; ++i) {
        std::string v;
        if (FlagValue(argv[i], "-runs=", v)) runs = std::atol(v.c_str());
        else if (FlagValue(argv[i], "-seed=", v)) seed = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
        else if (FlagValue(argv[i], "-max_len=", v)) maxLen = static_cast<size_t>(std::atol(v.c_str()));
        else if (FlagValue(argv[i], "-dict=", v)) dictPath = v;
        else if (FlagValue(argv[i], "-save_last=", v)) saveLast = v;
        else if (argv[i][0] == '-') std::fprintf(stderr, "FuzzSmoke: ignoring %s\n", argv[i]);
        else CollectInputs(argv[i], seeds);
    }

    const auto start = std::chrono::steady_clock::now();
    auto run = [&](const Bytes& input) {
        if (!saveLast.empty()) WriteInputFile(saveLast, input);
        LLVMFuzzerTestOneInput(input.data(), input.size());
    };
    for (const Bytes& s : seeds) run(s);
    run(Bytes{});

    Mutator mutator(seed, dictPath.empty() ? std::vector<Bytes>{} : ReadDictionary(dictPath), maxLen);
    for (long i = 0; i < runs; ++i) run(mutator.Mutate(seeds));

    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("FuzzSmoke: %zu seeds, %ld mutations (seed %u) in %.1f s - no crash\n",
                seeds.size(), runs, seed, seconds);
    return 0;
}
