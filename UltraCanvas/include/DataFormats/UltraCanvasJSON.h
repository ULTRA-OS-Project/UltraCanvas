// include/DataFormats/UltraCanvasJSON.h
// General-purpose JSON parsing and serialization for the UltraCanvas Framework.
//
// This is the first module of the DataFormats section: framework-wide,
// well-defined structured-data facilities that core, plugins and applications
// can all depend on. The public API is entirely UltraCanvas-owned; the parsing
// engine behind it (vendored yyjson, third_party/yyjson) is an implementation
// detail and never leaks into this header, so it can be replaced without
// affecting callers.
//
// Design notes:
// - JSONValue is a value-semantic DOM node (null / boolean / number / string /
//   array / object). Objects preserve insertion order so files round-trip in a
//   stable, diff-friendly way.
// - Read accessors never throw: they return a caller-supplied fallback (or a
//   shared immutable Null value for structural lookups), so parsing code reads
//   linearly without defensive checks. Use the Is*/Contains predicates when the
//   distinction between "absent" and "default" matters.
// - Parse() accepts input produced by any conforming program, not just our own
//   output. Malformed input is reported through JSONParseResult with position,
//   line and column; nesting depth is limited (JSONParseOptions::maxNestingDepth)
//   so hostile input cannot exhaust the stack.
// - Integers are preserved exactly through int64; numbers outside int64 range
//   are held as double.
//
// Version: 1.0.0
// Author: UltraCanvas Framework

#pragma once

#include "../UltraCanvasCommonTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace UltraCanvas {

// ===== VALUE MODEL =====

enum class JSONType : uint8_t {
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object
};

class JSONValue {
public:
    using Member = std::pair<std::string, JSONValue>;

    // ----- Construction -----
    JSONValue() = default;                       // Null
    JSONValue(std::nullptr_t) {}                 // Null
    JSONValue(bool value);
    JSONValue(int value);
    JSONValue(unsigned int value);
    JSONValue(int64_t value);
    JSONValue(double value);
    JSONValue(const char* value);
    JSONValue(const std::string& value);
    JSONValue(std::string&& value);

    static JSONValue MakeArray();                // empty []
    static JSONValue MakeObject();               // empty {}

    // Shared immutable Null value returned by structural lookups that miss.
    static const JSONValue& NullValue();

    // ----- Type inspection -----
    JSONType GetType() const { return type; }
    bool IsNull()    const { return type == JSONType::Null; }
    bool IsBoolean() const { return type == JSONType::Boolean; }
    bool IsNumber()  const { return type == JSONType::Number; }
    bool IsInteger() const { return type == JSONType::Number && isInteger; }
    bool IsString()  const { return type == JSONType::String; }
    bool IsArray()   const { return type == JSONType::Array; }
    bool IsObject()  const { return type == JSONType::Object; }

    // ----- Scalar access (never throws; fallback on type mismatch) -----
    bool GetBoolean(bool fallback = false) const;
    int64_t GetInteger(int64_t fallback = 0) const;   // truncates non-integral numbers
    double GetNumber(double fallback = 0.0) const;
    std::string GetString(const std::string& fallback = std::string()) const;

    // ----- Array access -----
    // Element count of an array, member count of an object, 0 otherwise.
    size_t GetSize() const;
    // Element at index; NullValue() when not an array or out of range.
    const JSONValue& At(size_t index) const;
    // Appends to an array. A Null value silently becomes an array first;
    // appending to any other type is ignored. Returns *this for chaining.
    JSONValue& Append(JSONValue value);
    const std::vector<JSONValue>& GetElements() const { return elements; }

    // ----- Object access -----
    bool Contains(const std::string& key) const;
    // Pointer to the member value, or nullptr when absent / not an object.
    const JSONValue* Find(const std::string& key) const;
    // Member value; NullValue() when absent / not an object.
    const JSONValue& Get(const std::string& key) const;
    // Inserts or replaces a member. A Null value silently becomes an object
    // first; setting on any other type is ignored. Returns *this for chaining.
    JSONValue& Set(const std::string& key, JSONValue value);
    // Removes a member; returns true when it existed.
    bool Remove(const std::string& key);
    // Members in insertion order.
    const std::vector<Member>& GetMembers() const { return members; }

    // ----- Read-only indexing shorthands -----
    const JSONValue& operator[](size_t index) const { return At(index); }
    const JSONValue& operator[](const std::string& key) const { return Get(key); }

private:
    JSONType type = JSONType::Null;
    bool isInteger = false;
    bool booleanValue = false;
    int64_t integerValue = 0;
    double numberValue = 0.0;
    std::string stringValue;
    std::vector<JSONValue> elements;   // Array
    std::vector<Member> members;       // Object (insertion order preserved)
};

// ===== PARSING =====

struct JSONParseOptions {
    bool allowComments = false;        // accept // and /* */ (JSONC)
    bool allowTrailingCommas = false;  // accept [1,2,] and {"a":1,}
    bool allowInfAndNaN = false;       // accept Infinity / NaN literals
    size_t maxNestingDepth = 512;      // guard against stack exhaustion
};

struct JSONParseResult {
    bool success = false;
    std::string errorMessage;          // empty on success
    size_t errorPosition = 0;          // byte offset into the input
    size_t errorLine = 0;              // 1-based; 0 when unknown
    size_t errorColumn = 0;            // 1-based; 0 when unknown
};

// ===== SERIALIZATION =====

struct JSONSerializeOptions {
    bool pretty = false;               // multi-line, indented output
    int indentWidth = 4;               // 2 or 4 spaces (pretty mode only)
    bool escapeNonAscii = false;       // emit \uXXXX for all non-ASCII
    size_t maxNestingDepth = 512;      // guard against stack exhaustion
};

namespace JSON {

    // Parses a complete JSON document. On failure returns Null and, when
    // 'result' is provided, fills in the error details.
    JSONValue Parse(const std::string& text,
                    JSONParseResult* result = nullptr,
                    const JSONParseOptions& options = JSONParseOptions());

    // Reads and parses a file. File-system errors are reported through
    // 'result' the same way as syntax errors (position 0).
    JSONValue ParseFile(const std::string& filePath,
                        JSONParseResult* result = nullptr,
                        const JSONParseOptions& options = JSONParseOptions());

    // Serializes a value to a JSON string. Output is always strictly valid
    // JSON (non-finite numbers are written as null). Returns an empty string
    // only when the value tree exceeds options.maxNestingDepth.
    std::string Serialize(const JSONValue& value,
                          const JSONSerializeOptions& options = JSONSerializeOptions());

    // Serializes and writes to a file; returns false on serialization or I/O
    // failure.
    bool SerializeToFile(const std::string& filePath,
                         const JSONValue& value,
                         const JSONSerializeOptions& options = JSONSerializeOptions());

    // Returns the argument as a quoted, escaped JSON string literal
    // (including the surrounding double quotes).
    std::string EscapeString(const std::string& text);

    // ----- Framework type helpers -----
    // Color <-> [r, g, b, a] (0-255 integers)
    JSONValue FromColor(const Color& color);
    bool ToColor(const JSONValue& value, Color& outColor);
    // Point2D <-> [x, y]
    JSONValue FromPoint(const Point2Dd& point);
    bool ToPoint(const JSONValue& value, Point2Dd& outPoint);
    // Rect2D <-> [x, y, width, height]
    JSONValue FromRect(const Rect2Dd& rect);
    bool ToRect(const JSONValue& value, Rect2Dd& outRect);

} // namespace JSON

} // namespace UltraCanvas
