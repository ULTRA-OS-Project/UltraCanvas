// Tests/UltraDatabase/test_numbers.cpp
// Numbers on the way into and out of a database are dot-decimal, whatever the
// process's locale. The PostgreSQL driver read float8 / numeric with strtod and
// UltraDbValue::AsDouble read Text the same way; both follow LC_NUMERIC, and
// the Linux backend calls setlocale(LC_ALL, "") for XIM, so in a GUI program
// on a comma-decimal desktop "3.14" came back as 3. The driver also sent double
// parameters through std::to_string: "3,140000" there, and six decimals
// everywhere, so 1e-9 reached the server as zero.
//
// Every test here runs under de_DE (or another comma-decimal locale) where one
// is installed - CI installs de_DE.UTF-8 - and says SKIP for the locale half
// where none is.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "test_pg.h"

#include <UltraDatabase/UltraDatabase.h>

// Not a public header: the parser and formatter are driver-internal.
#include "../../UltraCanvas/core/UltraDatabase/UltraDatabaseInternal.h"

#include <clocale>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

// Switches the whole process to a comma-decimal locale for its lifetime, as a
// GUI program on a German desktop runs, and restores the previous one.
class CommaDecimalLocale {
public:
    CommaDecimalLocale() {
        const char* current = std::setlocale(LC_ALL, nullptr);
        saved_ = current != nullptr ? current : "C";
        for (const char* candidate : {"de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "de_DE"}) {
            if (std::setlocale(LC_ALL, candidate) == nullptr) continue;
            // Only a locale that really writes a comma counts.
            char probe[16];
            std::snprintf(probe, sizeof(probe), "%.1f", 1.5);   // locale-ok: probing the locale
            if (std::string(probe) == "1,5") { active_ = true; return; }
        }
        std::setlocale(LC_ALL, saved_.c_str());
        std::printf("         [SKIP] no comma-decimal locale installed; checked under C only\n");
    }
    ~CommaDecimalLocale() { std::setlocale(LC_ALL, saved_.c_str()); }
    bool Active() const { return active_; }

private:
    std::string saved_;
    bool active_ = false;
};

double Parse(const std::string& text, double fallback = -12345.0) {
    double value = fallback;
    ultradb_internal::ParseDoubleClassic(text, value);
    return value;
}

} // namespace

TEST(parse_double_ignores_the_locale) {
    CommaDecimalLocale locale;
    REQUIRE_EQ(Parse("3.14"), 3.14);
    REQUIRE_EQ(Parse("-2.5e3"), -2500.0);
    REQUIRE_EQ(Parse("  0.000000001"), 1e-9);
    REQUIRE_EQ(Parse("12.5 trailing"), 12.5);   // strtod ignored the rest too
    REQUIRE_EQ(Parse("42"), 42.0);
    // PostgreSQL's words for the special float8 values.
    REQUIRE(std::isnan(Parse("NaN")));
    REQUIRE_EQ(Parse("Infinity"), HUGE_VAL);
    REQUIRE_EQ(Parse("-Infinity"), -HUGE_VAL);
    // Not a number: false, and the caller's value is left alone.
    double untouched = 7.0;
    REQUIRE(!ultradb_internal::ParseDoubleClassic("abc", untouched));
    REQUIRE(!ultradb_internal::ParseDoubleClassic("", untouched));
    REQUIRE(!ultradb_internal::ParseDoubleClassic("   ", untouched));
    REQUIRE_EQ(untouched, 7.0);
}

TEST(format_double_is_dot_decimal_and_round_trips) {
    CommaDecimalLocale locale;
    REQUIRE_EQ(ultradb_internal::FormatDoubleClassic(3.14), std::string("3.14"));
    REQUIRE_EQ(ultradb_internal::FormatDoubleClassic(0.1), std::string("0.1"));
    REQUIRE_EQ(ultradb_internal::FormatDoubleClassic(-2500.0), std::string("-2500"));
    REQUIRE_EQ(ultradb_internal::FormatDoubleClassic(1e-9), std::string("1e-09"));
    REQUIRE_EQ(ultradb_internal::FormatDoubleClassic(HUGE_VAL), std::string("Infinity"));
    REQUIRE_EQ(ultradb_internal::FormatDoubleClassic(-HUGE_VAL), std::string("-Infinity"));
    REQUIRE_EQ(ultradb_internal::FormatDoubleClassic(std::nan("")), std::string("NaN"));
    // A value that needs all 17 digits still reads back exactly.
    const double third = 1.0 / 3.0;
    REQUIRE_EQ(Parse(ultradb_internal::FormatDoubleClassic(third)), third);
}

TEST(text_as_double_ignores_the_locale) {
    CommaDecimalLocale locale;
    REQUIRE_EQ(UltraDbValue("3.14").AsDouble(), 3.14);
    REQUIRE_EQ(UltraDbValue("  -0.5").AsDouble(), -0.5);
    REQUIRE_EQ(UltraDbValue("not a number").AsDouble(), 0.0);
    REQUIRE_EQ(UltraDbValue("").AsDouble(), 0.0);
}

TEST(sqlite_text_number_reads_back_under_a_comma_locale) {
    CommaDecimalLocale locale;
    UltraDbConnectionConfig cfg;
    cfg.name = "test-numbers-sqlite";
    cfg.database = ":memory:";
    UltraDb_CloseConnection(cfg.name);
    REQUIRE(UltraDb_RegisterConnection(cfg).success);

    // A number stored as text - as a CSV import or a legacy column leaves it.
    UltraDbResultSet rs;
    REQUIRE(UltraDb_Query(cfg.name, "SELECT '3.14' AS t, 2.75 AS r", rs).success);
    REQUIRE_EQ(rs.Row(0)["t"].AsDouble(), 3.14);
    REQUIRE_EQ(rs.Row(0)["r"].AsDouble(), 2.75);
    UltraDb_CloseConnection(cfg.name);
}

TEST(pg_numbers_round_trip_under_a_comma_locale) {
    if (!ultradb_test::OpenPgTestConnection("test-numbers-pg")) return;
    CommaDecimalLocale locale;
    const std::string c = "test-numbers-pg";

    // Out of the server: float8, numeric and the special values.
    UltraDbResultSet rs;
    REQUIRE(UltraDb_Query(c,
        "SELECT 3.14::float8 AS f, 2.5::numeric AS n, 'NaN'::float8 AS nan_value,"
        " '-Infinity'::float8 AS neg_inf", rs).success);
    REQUIRE_EQ(rs.Row(0)["f"].AsDouble(), 3.14);
    REQUIRE_EQ(rs.Row(0)["n"].AsDouble(), 2.5);
    REQUIRE(std::isnan(rs.Row(0)["nan_value"].AsDouble()));
    REQUIRE_EQ(rs.Row(0)["neg_inf"].AsDouble(), -HUGE_VAL);

    // Into the server as parameters, and back: the server must receive the
    // value itself, not "3,140000" (refused) or "0.000000" (a different value).
    UltraDbResultSet back;
    REQUIRE(UltraDb_Query(c, "SELECT ?::float8 AS a, ?::float8 AS b, ?::float8 AS third",
                          { 3.14, 1e-9, 1.0 / 3.0 }, back).success);
    REQUIRE_EQ(back.Row(0)["a"].AsDouble(), 3.14);
    REQUIRE_EQ(back.Row(0)["b"].AsDouble(), 1e-9);
    REQUIRE_EQ(back.Row(0)["third"].AsDouble(), 1.0 / 3.0);

    // And compared on the server, where a wrong text form would not match.
    UltraDbResultSet compared;
    REQUIRE(UltraDb_Query(c, "SELECT ?::float8 = 3.14::float8 AS same", { 3.14 },
                          compared).success);
    REQUIRE(compared.Row(0)["same"].AsBool());
    UltraDb_CloseConnection(c);
}
