# UltraCanvasJSON — General-Purpose JSON Support

`UltraCanvasJSON` is the first module of the **DataFormats** section
(`UltraCanvas/{include,core}/DataFormats/`): framework-wide structured-data
facilities that core, plugins and applications can all depend on.

It provides parsing and serialization of arbitrary JSON — including files and
payloads produced by *other programs*, not just UltraCanvas's own output. The
implementation is backed by the vendored [yyjson](https://github.com/ibireme/yyjson)
engine (`UltraCanvas/third_party/yyjson`, MIT), which is wrapped completely:
yyjson types never appear in public headers, so the engine can be replaced
without affecting any caller.

```cpp
#include "DataFormats/UltraCanvasJSON.h"
using namespace UltraCanvas;
```

## The value model

`JSONValue` is a value-semantic DOM node with type
`JSONType::{Null, Boolean, Number, String, Array, Object}`.
Objects preserve **insertion order**, so files round-trip in a stable,
diff-friendly way. Integers are preserved exactly through `int64_t`.

Read accessors never throw. Scalar getters take a fallback; structural
lookups that miss return a shared immutable Null value, so chained access is
always safe:

```cpp
JSONParseResult result;
JSONValue doc = JSON::Parse(text, &result);
if (!result.success) {
    // result.errorMessage, result.errorPosition, result.errorLine/errorColumn
    return;
}

std::string title = doc["title"].GetString("Untitled");
int64_t version   = doc["version"].GetInteger(1);
double x          = doc["nodes"][0]["x"].GetNumber();   // safe even if absent
if (doc.Contains("legacyField")) { /* absent vs default, when it matters */ }
```

## Building and writing documents

```cpp
JSONValue doc = JSONValue::MakeObject();
doc.Set("title", "Diagram");
doc.Set("version", static_cast<int64_t>(2));

JSONValue nodes = JSONValue::MakeArray();
nodes.Append(JSONValue::MakeObject().Set("id", "n1").Set("x", 10.0));
doc.Set("nodes", nodes);

std::string compact = JSON::Serialize(doc);

JSONSerializeOptions pretty;
pretty.pretty = true;          // indentWidth: 4 (default) or 2
std::string formatted = JSON::Serialize(doc, pretty);

JSON::SerializeToFile("diagram.json", doc, pretty);
```

Serialized output is always strictly valid JSON (non-finite numbers are
written as `null`).

## Files and lenient input

```cpp
JSONParseResult result;
JSONValue config = JSON::ParseFile(configPath, &result);

// JSONC-style config files:
JSONParseOptions lenient;
lenient.allowComments = true;        // // and /* */
lenient.allowTrailingCommas = true;  // [1, 2,] and {"a": 1,}
JSONValue jsonc = JSON::ParseFile(configPath, &result, lenient);
```

## Safety properties

- **Strict by default:** RFC 8259 conformance; trailing garbage, trailing
  commas, comments and invalid UTF-8 are rejected unless explicitly allowed.
- **Depth-limited:** `JSONParseOptions::maxNestingDepth` (default 512) bounds
  recursion, so hostile input such as one hundred thousand nested `[` cannot
  exhaust the stack. The serializer applies the same guard.
- **Error reporting:** `JSONParseResult` carries a message plus byte offset
  and 1-based line/column of the failure.

## Framework type helpers

Shared conventions so modules stop reinventing them:

| Type | JSON form | Functions |
|---|---|---|
| `Color` | `[r, g, b, a]` (0–255; 3-element form implies a = 255) | `JSON::FromColor` / `JSON::ToColor` |
| `Point2Dd` | `[x, y]` | `JSON::FromPoint` / `JSON::ToPoint` |
| `Rect2Dd` | `[x, y, width, height]` | `JSON::FromRect` / `JSON::ToRect` |

`JSON::EscapeString(text)` returns `text` as a quoted, escaped JSON string
literal — the replacement for the hand-rolled `EscapeJsonString` helpers.

## Tests

`Tests/DataFormats/JSONTests.cpp` (CMake target `JSONTests`, built with
`BUILD_TESTS=ON`) is a standalone headless executable covering scalars,
structures, string edge cases (escapes, surrogate pairs, embedded NUL,
invalid UTF-8), error positions, lenient options, depth guards, round-trips,
file I/O and the framework-type helpers.
