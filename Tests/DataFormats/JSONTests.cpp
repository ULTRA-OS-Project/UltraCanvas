// Tests/DataFormats/JSONTests.cpp
// Unit tests for the UltraCanvasJSON module (DataFormats section).
//
// Standalone executable (target: JSONTests) that compiles only the JSON
// module and its vendored engine, so it runs headless without the UI stack.
// Exit code 0 = all tests passed.

#include "DataFormats/UltraCanvasJSON.h"

#include <cstdio>
#include <string>

using namespace UltraCanvas;

static int testsRun = 0;
static int testsFailed = 0;

#define CHECK(condition) do { \
    ++testsRun; \
    if (!(condition)) { \
        ++testsFailed; \
        std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition); \
    } \
} while (0)

static void TestParseScalars() {
    JSONParseResult result;
    CHECK(JSON::Parse("null", &result).IsNull() && result.success);
    CHECK(JSON::Parse("true", &result).GetBoolean() == true);
    CHECK(JSON::Parse("false", &result).GetBoolean(true) == false);
    CHECK(JSON::Parse("42", &result).GetInteger() == 42);
    CHECK(JSON::Parse("-7", &result).GetInteger() == -7);
    CHECK(JSON::Parse("42", &result).IsInteger());
    CHECK(JSON::Parse("3.5", &result).GetNumber() == 3.5);
    CHECK(!JSON::Parse("3.5", &result).IsInteger());
    CHECK(JSON::Parse("\"hi\"", &result).GetString() == "hi");
    // int64 boundaries survive exactly
    CHECK(JSON::Parse("9223372036854775807", &result).GetInteger() == INT64_MAX);
    CHECK(JSON::Parse("-9223372036854775808", &result).GetInteger() == INT64_MIN);
}

static void TestParseStructures() {
    JSONParseResult result;
    JSONValue v = JSON::Parse(R"({"name":"Ultra","size":[800,600],"visible":true,"nested":{"a":1}})",
                              &result);
    CHECK(result.success);
    CHECK(v.IsObject() && v.GetSize() == 4);
    CHECK(v["name"].GetString() == "Ultra");
    CHECK(v["size"].IsArray() && v["size"].GetSize() == 2);
    CHECK(v["size"][0].GetInteger() == 800);
    CHECK(v["size"][1].GetInteger() == 600);
    CHECK(v["visible"].GetBoolean() == true);
    CHECK(v["nested"]["a"].GetInteger() == 1);
    // misses are safe, never throw
    CHECK(v["missing"].IsNull());
    CHECK(v["size"][99].IsNull());
    CHECK(v["name"]["not-an-object"].IsNull());
    CHECK(v.Contains("name") && !v.Contains("missing"));
    CHECK(v.Find("missing") == nullptr);
}

static void TestStringEdgeCases() {
    JSONParseResult result;
    // escapes, unicode escapes (surrogate pair), embedded UTF-8
    JSONValue v = JSON::Parse(R"({"s":"a\"b\\c\nd","u":"é😀","raw":"äöü €"})",
                              &result);
    CHECK(result.success);
    CHECK(v["s"].GetString() == "a\"b\\c\nd");
    CHECK(v["u"].GetString() == "\xC3\xA9\xF0\x9F\x98\x80");
    CHECK(v["raw"].GetString() == "\xC3\xA4\xC3\xB6\xC3\xBC \xE2\x82\xAC");
    // NUL inside a string survives (length-preserving handling)
    JSONValue nul = JSON::Parse(R"(["a\u0000b"])", &result);
    CHECK(result.success && nul[0].GetString() == std::string("a\0b", 3));
}

static void TestParseErrors() {
    JSONParseResult result;
    CHECK(JSON::Parse("", &result).IsNull() && !result.success);
    CHECK(!JSON::Parse("{", &result).IsObject() && !result.success);
    JSON::Parse("[1,2,]", &result);
    CHECK(!result.success);                       // strict by default
    JSON::Parse("{\"a\":1} trailing", &result);
    CHECK(!result.success);                       // trailing garbage rejected
    JSON::Parse("[1, 2, oops]", &result);
    CHECK(!result.success && result.errorPosition > 0);
    CHECK(result.errorLine == 1 && result.errorColumn > 1);
    JSON::Parse("{\n  \"a\": bad\n}", &result);
    CHECK(!result.success && result.errorLine == 2);
    // invalid UTF-8 is rejected, not passed through
    JSON::Parse("[\"\xFF\xFE\"]", &result);
    CHECK(!result.success);
}

static void TestParseOptions() {
    JSONParseResult result;
    JSONParseOptions lenient;
    lenient.allowComments = true;
    lenient.allowTrailingCommas = true;
    JSONValue v = JSON::Parse("// comment\n{\"a\": 1, /* x */ \"b\": [1,2,],}",
                              &result, lenient);
    CHECK(result.success && v["a"].GetInteger() == 1 && v["b"].GetSize() == 2);

    JSONParseOptions withNaN;
    withNaN.allowInfAndNaN = true;
    JSONValue inf = JSON::Parse("[Infinity]", &result, withNaN);
    CHECK(result.success && inf[0].GetNumber() > 1e308);

    // nesting depth guard
    JSONParseOptions shallow;
    shallow.maxNestingDepth = 4;
    JSON::Parse("[[[[[[1]]]]]]", &result, shallow);
    CHECK(!result.success);
    JSONValue ok = JSON::Parse("[[[1]]]", &result, shallow);
    CHECK(result.success && ok[0][0][0].GetInteger() == 1);
    // deeply nested hostile input is rejected, not a crash
    std::string bomb(100000, '[');
    JSON::Parse(bomb, &result);
    CHECK(!result.success);
}

static void TestBuildAndSerialize() {
    JSONValue doc = JSONValue::MakeObject();
    doc.Set("title", "Diagram");
    doc.Set("version", static_cast<int64_t>(2));
    doc.Set("scale", 1.5);
    doc.Set("empty", nullptr);
    JSONValue nodes = JSONValue::MakeArray();
    nodes.Append(JSONValue::MakeObject().Set("id", "n1").Set("x", 10.0));
    nodes.Append(JSONValue::MakeObject().Set("id", "n2").Set("x", 20.0));
    doc.Set("nodes", nodes);

    std::string compact = JSON::Serialize(doc);
    CHECK(compact.find("\"title\":\"Diagram\"") != std::string::npos);
    // insertion order is preserved
    CHECK(compact.find("\"title\"") < compact.find("\"version\""));
    CHECK(compact.find("\"version\"") < compact.find("\"nodes\""));

    JSONSerializeOptions pretty;
    pretty.pretty = true;
    std::string formatted = JSON::Serialize(doc, pretty);
    CHECK(formatted.find('\n') != std::string::npos);

    // round trip
    JSONParseResult result;
    JSONValue back = JSON::Parse(formatted, &result);
    CHECK(result.success);
    CHECK(back["title"].GetString() == "Diagram");
    CHECK(back["version"].GetInteger() == 2);
    CHECK(back["scale"].GetNumber() == 1.5);
    CHECK(back["empty"].IsNull() && back.Contains("empty"));
    CHECK(back["nodes"][1]["id"].GetString() == "n2");

    // escaping through the writer
    JSONValue tricky("quote\" slash\\ ctrl\x01 text");
    std::string escaped = JSON::Serialize(tricky);
    CHECK(JSON::Parse(escaped, &result).GetString() == tricky.GetString());
    CHECK(JSON::EscapeString("a\"b").find("\\\"") != std::string::npos);

    // Set replaces, Remove removes
    doc.Set("version", static_cast<int64_t>(3));
    CHECK(doc["version"].GetInteger() == 3 && doc.GetSize() == 5);
    CHECK(doc.Remove("empty") && !doc.Contains("empty"));
    CHECK(!doc.Remove("empty"));

    // serializer depth guard
    JSONSerializeOptions shallow;
    shallow.maxNestingDepth = 2;
    JSONValue deep = JSONValue::MakeArray();
    deep.Append(JSONValue::MakeArray().Append(JSONValue::MakeArray().Append(1)));
    CHECK(JSON::Serialize(deep, shallow).empty());
}

static void TestFileRoundTrip() {
    const char* path = "ultracanvas_json_test_tmp.json";
    JSONValue doc = JSONValue::MakeObject();
    doc.Set("hello", "wörld");
    doc.Set("n", static_cast<int64_t>(123));
    CHECK(JSON::SerializeToFile(path, doc));

    JSONParseResult result;
    JSONValue back = JSON::ParseFile(path, &result);
    CHECK(result.success);
    CHECK(back["hello"].GetString() == "w\xC3\xB6rld");
    CHECK(back["n"].GetInteger() == 123);
    std::remove(path);

    JSON::ParseFile("does/not/exist.json", &result);
    CHECK(!result.success && !result.errorMessage.empty());
}

static void TestFrameworkTypes() {
    Color c(10, 20, 30, 40);
    JSONValue jc = JSON::FromColor(c);
    CHECK(JSON::Serialize(jc) == "[10,20,30,40]");
    Color c2;
    CHECK(JSON::ToColor(jc, c2) && c2.r == 10 && c2.g == 20 && c2.b == 30 && c2.a == 40);
    // 3-element form defaults alpha to 255; out-of-range clamps
    JSONParseResult result;
    CHECK(JSON::ToColor(JSON::Parse("[1,2,3]", &result), c2) && c2.a == 255);
    CHECK(JSON::ToColor(JSON::Parse("[999,-5,3]", &result), c2) && c2.r == 255 && c2.g == 0);
    CHECK(!JSON::ToColor(JSON::Parse("[1,2]", &result), c2));
    CHECK(!JSON::ToColor(JSON::Parse("[1,\"x\",3]", &result), c2));

    Point2Dd p(1.5, -2.5);
    Point2Dd p2;
    CHECK(JSON::ToPoint(JSON::FromPoint(p), p2) && p2.x == 1.5 && p2.y == -2.5);
    CHECK(!JSON::ToPoint(JSON::Parse("[1]", &result), p2));

    Rect2Dd r(0, 1, 800, 600);
    Rect2Dd r2;
    CHECK(JSON::ToRect(JSON::FromRect(r), r2) && r2.width == 800 && r2.height == 600);
    CHECK(!JSON::ToRect(JSON::Parse("[1,2,3]", &result), r2));
}

static void TestNullPromotion() {
    // Append/Set promote a Null value, but never mutate other types
    JSONValue a;
    a.Append(1).Append(2);
    CHECK(a.IsArray() && a.GetSize() == 2);
    JSONValue o;
    o.Set("k", "v");
    CHECK(o.IsObject() && o["k"].GetString() == "v");
    JSONValue s("text");
    s.Append(1);
    s.Set("k", 1);
    CHECK(s.IsString() && s.GetString() == "text");
    // scalar accessors fall back on mismatch
    CHECK(s.GetInteger(-1) == -1 && s.GetBoolean(true) == true);
    CHECK(JSONValue(1.9).GetInteger() == 1);   // truncation documented
}

int main() {
    TestParseScalars();
    TestParseStructures();
    TestStringEdgeCases();
    TestParseErrors();
    TestParseOptions();
    TestBuildAndSerialize();
    TestFileRoundTrip();
    TestFrameworkTypes();
    TestNullPromotion();

    std::printf("JSONTests: %d checks, %d failed\n", testsRun, testsFailed);
    return testsFailed == 0 ? 0 : 1;
}
