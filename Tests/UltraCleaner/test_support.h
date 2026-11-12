// Tests/UltraCleaner/test_support.h
// A scratch directory tree the filesystem tests build and tear down. Nothing
// here touches a real cache or trash directory — every path the tests hand
// the engine lives under one temporary root.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "UltraCleanerPaths.h"   // WithForwardSlashes
#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"

namespace ultracleaner_test {

// Relative names are UTF-8 and so are the paths handed back, with '/'
// between the parts, so a test can name a folder in Thai or with an emoji.
// They used to go through the narrow path conversions - `root_ / relative`
// and generic_string() - which on Windows read the ANSI code page: such a name
// came out as a different file, or generic_string() threw.
class TempTree {
public:
    TempTree() {
        static std::atomic<int> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
                UltraCanvas::PathFromUtf8("ultracleaner-test-" + std::to_string(stamp) +
                                          "-" + std::to_string(counter++));
        std::error_code ec;
        std::filesystem::create_directories(root_, ec);
    }

    ~TempTree() {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }

    TempTree(const TempTree&) = delete;
    TempTree& operator=(const TempTree&) = delete;

    std::string Path() const { return Text(root_); }
    std::string Path(const std::string& relative) const {
        return Text(Join(relative));
    }

    // Creates a file (and its parents) with `bytes` bytes of content.
    std::string File(const std::string& relative, size_t bytes = 0) const {
        const std::filesystem::path path = Join(relative);
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream out(path, std::ios::binary);
        for (size_t i = 0; i < bytes; ++i) out.put('x');
        return Text(path);
    }

    std::string Dir(const std::string& relative) const {
        const std::filesystem::path path = Join(relative);
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        return Text(path);
    }

    bool Exists(const std::string& relative) const {
        std::error_code ec;
        return std::filesystem::exists(Join(relative), ec);
    }

private:
    std::filesystem::path Join(const std::string& relative) const {
        return root_ / UltraCanvas::PathFromUtf8(relative);
    }
    static std::string Text(const std::filesystem::path& path) {
        return UltraCleaner::WithForwardSlashes(UltraCanvas::PathToUtf8(path));
    }

    std::filesystem::path root_;
};

} // namespace ultracleaner_test
