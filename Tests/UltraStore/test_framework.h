// Tests/UltraStore/test_framework.h
// Tiny zero-dependency test framework for the UltraStore core suite,
// matching the one the UltraCleaner suite uses.
// Version: 0.2.0 - SKIP
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace ultrastore_test {

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

struct Failure {
    std::string message;
};

// Thrown by SKIP: the test cannot run here (no python3, no archive provider,
// Windows) and says so without failing.
struct Skipped {
    std::string reason;
};

inline int RunAll() {
    int passed = 0, failed = 0, skipped = 0;
    for (auto& t : Registry()) {
        try {
            t.fn();
            std::printf("  [PASS] %s\n", t.name.c_str());
            ++passed;
        } catch (const Skipped& s) {
            std::printf("  [SKIP] %s\n         %s\n", t.name.c_str(), s.reason.c_str());
            ++skipped;
        } catch (const Failure& f) {
            std::printf("  [FAIL] %s\n         %s\n", t.name.c_str(), f.message.c_str());
            ++failed;
        } catch (const std::exception& e) {
            std::printf("  [FAIL] %s\n         unexpected exception: %s\n",
                        t.name.c_str(), e.what());
            ++failed;
        }
    }
    std::printf("\nUltraStore core tests: %d passed, %d failed, %d skipped\n", passed, failed, skipped);
    return failed == 0 ? 0 : 1;
}

} // namespace ultrastore_test

#define TEST(name)                                                            \
    static void name();                                                       \
    static ::ultrastore_test::Reg reg_##name(#name, name);                  \
    static void name()

#define SKIP(reason) throw ::ultrastore_test::Skipped{reason}

#define REQUIRE(expr)                                                         \
    do {                                                                      \
        if (!(expr)) {                                                        \
            std::ostringstream oss__;                                         \
            oss__ << "REQUIRE(" #expr ") failed at " << __FILE__ << ":"       \
                  << __LINE__;                                                \
            throw ::ultrastore_test::Failure{oss__.str()};                   \
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
            throw ::ultrastore_test::Failure{oss__.str()};                   \
        }                                                                     \
    } while (0)
