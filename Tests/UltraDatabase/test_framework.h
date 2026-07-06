// Tests/UltraDatabase/test_framework.h
// Tiny zero-dependency test framework for the UltraDatabase suite. Each
// TEST(name) registers itself at static-init time; main() runs the registry
// and reports pass/fail counts.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace ultradb_test {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& Registry() {
    static std::vector<TestCase> v;
    return v;
}

struct Reg {
    Reg(const std::string& name, std::function<void()> fn) {
        Registry().push_back({name, std::move(fn)});
    }
};

// Thrown by REQUIRE on failure to abort the current test.
struct Failure {
    std::string message;
};

inline int RunAll() {
    int passed = 0, failed = 0;
    for (auto& t : Registry()) {
        try {
            t.fn();
            std::printf("  [PASS] %s\n", t.name.c_str());
            ++passed;
        } catch (const Failure& f) {
            std::printf("  [FAIL] %s\n         %s\n", t.name.c_str(), f.message.c_str());
            ++failed;
        } catch (const std::exception& e) {
            std::printf("  [FAIL] %s\n         unexpected exception: %s\n",
                        t.name.c_str(), e.what());
            ++failed;
        }
    }
    std::printf("\nUltraDatabase tests: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

} // namespace ultradb_test

#define TEST(name)                                                            \
    static void name();                                                       \
    static ::ultradb_test::Reg reg_##name(#name, name);                       \
    static void name()

#define REQUIRE(expr)                                                         \
    do {                                                                      \
        if (!(expr)) {                                                        \
            std::ostringstream oss__;                                         \
            oss__ << "REQUIRE(" #expr ") failed at " << __FILE__ << ":"       \
                  << __LINE__;                                                \
            throw ::ultradb_test::Failure{oss__.str()};                       \
        }                                                                     \
    } while (0)

#define REQUIRE_EQ(a, b)                                                      \
    do {                                                                      \
        auto va__ = (a);                                                      \
        auto vb__ = (b);                                                      \
        if (!(va__ == vb__)) {                                                \
            std::ostringstream oss__;                                         \
            oss__ << "REQUIRE_EQ(" #a ", " #b ") failed at " << __FILE__      \
                  << ":" << __LINE__ << " (" << va__ << " != " << vb__ << ")";\
            throw ::ultradb_test::Failure{oss__.str()};                       \
        }                                                                     \
    } while (0)
