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

namespace ultracleaner_test {

class TempTree {
public:
    TempTree() {
        static std::atomic<int> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
                ("ultracleaner-test-" + std::to_string(stamp) + "-" +
                 std::to_string(counter++));
        std::error_code ec;
        std::filesystem::create_directories(root_, ec);
    }

    ~TempTree() {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }

    TempTree(const TempTree&) = delete;
    TempTree& operator=(const TempTree&) = delete;

    std::string Path() const { return root_.generic_string(); }
    std::string Path(const std::string& relative) const {
        return (root_ / relative).generic_string();
    }

    // Creates a file (and its parents) with `bytes` bytes of content.
    std::string File(const std::string& relative, size_t bytes = 0) const {
        const std::filesystem::path path = root_ / relative;
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream out(path, std::ios::binary);
        for (size_t i = 0; i < bytes; ++i) out.put('x');
        return path.generic_string();
    }

    std::string Dir(const std::string& relative) const {
        const std::filesystem::path path = root_ / relative;
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        return path.generic_string();
    }

    bool Exists(const std::string& relative) const {
        std::error_code ec;
        return std::filesystem::exists(root_ / relative, ec);
    }

private:
    std::filesystem::path root_;
};

} // namespace ultracleaner_test
