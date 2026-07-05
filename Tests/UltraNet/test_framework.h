// Tests/UltraNet/test_framework.h
// Tiny zero-dependency test framework for the UltraNet test suite.
// One TU per area (test_url.cpp, test_headers.cpp, ...). Each TEST(name)
// registers itself at static-init time; main() walks the registry and runs
// every test, reporting pass/fail/skip counts.
//
// Macros:
//   TEST(name)           - declares a test body
//   REQUIRE(expr)        - assertion; on failure, marks test failed and returns
//   CHECK(expr)          - non-fatal assertion
//   REQUIRE_EQ(a, b)     - REQUIRE that a == b (prints values on failure)
//   SKIP(reason)         - marks the current test as skipped and returns
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdio>
#include <exception>
#include <functional>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace ultranet_test {

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

inline int      g_passed = 0;
inline int      g_failed = 0;
inline int      g_skipped = 0;
inline bool     g_currentFailed = false;
inline bool     g_currentSkipped = false;
inline std::string g_currentName;

inline void RecordFail(const char* file, int line, const std::string& msg) {
    g_currentFailed = true;
    std::fprintf(stderr, "\n    FAIL %s:%d  %s", file, line, msg.c_str());
}

inline void RecordSkip(const std::string& reason) {
    g_currentSkipped = true;
    std::fprintf(stderr, "\n    SKIP: %s", reason.c_str());
}

template <typename T>
concept Streamable = requires(std::ostream& os, const T& v) {
    { os << v };
};

template <typename T>
void StreamOrPlaceholder(std::ostream& os, const T& v) {
    if constexpr (Streamable<T>) {
        os << v;
    } else if constexpr (std::is_enum_v<T>) {
        os << static_cast<long long>(v);
    } else {
        os << "<value>";
    }
}

template <typename A, typename B>
std::string EqMessage(const char* expr, const A& a, const B& b) {
    std::ostringstream os;
    os << expr << "  (actual: ";
    StreamOrPlaceholder(os, a);
    os << "  vs expected: ";
    StreamOrPlaceholder(os, b);
    os << ")";
    return os.str();
}

inline int RunAllTests() {
    std::fprintf(stdout, "Running %zu UltraNet tests\n", Registry().size());
    for (auto& t : Registry()) {
        g_currentName    = t.name;
        g_currentFailed  = false;
        g_currentSkipped = false;
        std::fprintf(stdout, "  %-40s ", t.name.c_str());
        std::fflush(stdout);
        try {
            t.fn();
        } catch (const std::exception& e) {
            g_currentFailed = true;
            std::fprintf(stderr, "\n    EXCEPTION: %s", e.what());
        }
        if (g_currentSkipped)      { ++g_skipped; std::fprintf(stdout, "\n"); }
        else if (g_currentFailed)  { ++g_failed;  std::fprintf(stdout, "\n  -> FAIL\n"); }
        else                       { ++g_passed;  std::fprintf(stdout, "ok\n"); }
    }
    std::fprintf(stdout, "\n%d passed, %d failed, %d skipped\n",
                 g_passed, g_failed, g_skipped);
    return g_failed == 0 ? 0 : 1;
}

} // namespace ultranet_test

#define TEST(name)                                                         \
    static void ultranet_test_##name();                                    \
    static ultranet_test::Reg ultranet_test_##name##_reg{                  \
        #name, ultranet_test_##name};                                      \
    static void ultranet_test_##name()

#define REQUIRE(expr)                                                      \
    do {                                                                   \
        if (!(expr)) {                                                     \
            ultranet_test::RecordFail(__FILE__, __LINE__, #expr);          \
            return;                                                        \
        }                                                                  \
    } while (0)

#define CHECK(expr)                                                        \
    do {                                                                   \
        if (!(expr)) {                                                     \
            ultranet_test::RecordFail(__FILE__, __LINE__, #expr);          \
        }                                                                  \
    } while (0)

#define REQUIRE_EQ(a, b)                                                   \
    do {                                                                   \
        auto _x = (a);                                                     \
        auto _y = (b);                                                     \
        if (!(_x == _y)) {                                                 \
            ultranet_test::RecordFail(                                     \
                __FILE__, __LINE__,                                        \
                ultranet_test::EqMessage(#a " == " #b, _x, _y));           \
            return;                                                        \
        }                                                                  \
    } while (0)

#define SKIP(reason)                                                       \
    do {                                                                   \
        ultranet_test::RecordSkip(reason);                                 \
        return;                                                            \
    } while (0)
