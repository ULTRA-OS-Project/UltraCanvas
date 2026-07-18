// core/DataFormats/UltraCanvasJSON.cpp
// General-purpose JSON parsing and serialization for the UltraCanvas Framework.
//
// Implementation is backed by the vendored yyjson engine
// (third_party/yyjson, MIT). yyjson never appears in the public header;
// this file is the only translation unit that touches it, so the engine can
// be swapped without affecting any caller.
//
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "DataFormats/UltraCanvasJSON.h"

#include "yyjson.h"

#include <cstdio>
#include <cstdlib>

namespace UltraCanvas {

// ===== JSONValue =====

JSONValue::JSONValue(bool value)
    : type(JSONType::Boolean), booleanValue(value) {}

JSONValue::JSONValue(int value)
    : JSONValue(static_cast<int64_t>(value)) {}

JSONValue::JSONValue(unsigned int value)
    : JSONValue(static_cast<int64_t>(value)) {}

JSONValue::JSONValue(int64_t value)
    : type(JSONType::Number), isInteger(true), integerValue(value),
      numberValue(static_cast<double>(value)) {}

JSONValue::JSONValue(double value)
    : type(JSONType::Number), numberValue(value) {}

JSONValue::JSONValue(const char* value)
    : type(JSONType::String), stringValue(value ? value : "") {}

JSONValue::JSONValue(const std::string& value)
    : type(JSONType::String), stringValue(value) {}

JSONValue::JSONValue(std::string&& value)
    : type(JSONType::String), stringValue(std::move(value)) {}

JSONValue JSONValue::MakeArray() {
    JSONValue v;
    v.type = JSONType::Array;
    return v;
}

JSONValue JSONValue::MakeObject() {
    JSONValue v;
    v.type = JSONType::Object;
    return v;
}

const JSONValue& JSONValue::NullValue() {
    static const JSONValue nullValue;
    return nullValue;
}

bool JSONValue::GetBoolean(bool fallback) const {
    return IsBoolean() ? booleanValue : fallback;
}

int64_t JSONValue::GetInteger(int64_t fallback) const {
    if (!IsNumber()) return fallback;
    return isInteger ? integerValue : static_cast<int64_t>(numberValue);
}

double JSONValue::GetNumber(double fallback) const {
    return IsNumber() ? numberValue : fallback;
}

std::string JSONValue::GetString(const std::string& fallback) const {
    return IsString() ? stringValue : fallback;
}

size_t JSONValue::GetSize() const {
    if (IsArray()) return elements.size();
    if (IsObject()) return members.size();
    return 0;
}

const JSONValue& JSONValue::At(size_t index) const {
    if (!IsArray() || index >= elements.size()) return NullValue();
    return elements[index];
}

JSONValue& JSONValue::Append(JSONValue value) {
    if (IsNull()) type = JSONType::Array;
    if (IsArray()) elements.push_back(std::move(value));
    return *this;
}

bool JSONValue::Contains(const std::string& key) const {
    return Find(key) != nullptr;
}

const JSONValue* JSONValue::Find(const std::string& key) const {
    if (!IsObject()) return nullptr;
    for (const Member& member : members) {
        if (member.first == key) return &member.second;
    }
    return nullptr;
}

const JSONValue& JSONValue::Get(const std::string& key) const {
    const JSONValue* found = Find(key);
    return found ? *found : NullValue();
}

JSONValue& JSONValue::Set(const std::string& key, JSONValue value) {
    if (IsNull()) type = JSONType::Object;
    if (!IsObject()) return *this;
    for (Member& member : members) {
        if (member.first == key) {
            member.second = std::move(value);
            return *this;
        }
    }
    members.emplace_back(key, std::move(value));
    return *this;
}

bool JSONValue::Remove(const std::string& key) {
    if (!IsObject()) return false;
    for (auto it = members.begin(); it != members.end(); ++it) {
        if (it->first == key) {
            members.erase(it);
            return true;
        }
    }
    return false;
}

namespace JSON {

// ===== Parsing =====

namespace {

void SetError(JSONParseResult* result, const std::string& message, size_t position,
              const std::string& text) {
    if (!result) return;
    result->success = false;
    result->errorMessage = message;
    result->errorPosition = position;
    result->errorLine = 0;
    result->errorColumn = 0;
    if (position <= text.size()) {
        size_t line = 1, column = 1;
        for (size_t i = 0; i < position; ++i) {
            if (text[i] == '\n') { ++line; column = 1; }
            else ++column;
        }
        result->errorLine = line;
        result->errorColumn = column;
    }
}

// Converts a parsed yyjson node into the public DOM. Depth is bounded by
// options.maxNestingDepth (yyjson itself parses iteratively, so this
// recursion is the only stack consumer).
bool ConvertNode(yyjson_val* node, JSONValue& out, size_t depth, size_t maxDepth) {
    if (depth > maxDepth) return false;
    switch (yyjson_get_type(node)) {
        case YYJSON_TYPE_NULL:
            out = JSONValue();
            return true;
        case YYJSON_TYPE_BOOL:
            out = JSONValue(yyjson_get_bool(node));
            return true;
        case YYJSON_TYPE_NUM:
            if (yyjson_is_sint(node)) {
                out = JSONValue(yyjson_get_sint(node));
            } else if (yyjson_is_uint(node)) {
                uint64_t u = yyjson_get_uint(node);
                if (u <= static_cast<uint64_t>(INT64_MAX)) {
                    out = JSONValue(static_cast<int64_t>(u));
                } else {
                    // Above int64 range: preserved as double (documented).
                    out = JSONValue(static_cast<double>(u));
                }
            } else {
                out = JSONValue(yyjson_get_real(node));
            }
            return true;
        case YYJSON_TYPE_STR:
            out = JSONValue(std::string(yyjson_get_str(node), yyjson_get_len(node)));
            return true;
        case YYJSON_TYPE_ARR: {
            out = JSONValue::MakeArray();
            size_t index, count;
            yyjson_val* element;
            yyjson_arr_foreach(node, index, count, element) {
                JSONValue converted;
                if (!ConvertNode(element, converted, depth + 1, maxDepth)) return false;
                out.Append(std::move(converted));
            }
            return true;
        }
        case YYJSON_TYPE_OBJ: {
            out = JSONValue::MakeObject();
            size_t index, count;
            yyjson_val* key;
            yyjson_val* value;
            yyjson_obj_foreach(node, index, count, key, value) {
                JSONValue converted;
                if (!ConvertNode(value, converted, depth + 1, maxDepth)) return false;
                out.Set(std::string(yyjson_get_str(key), yyjson_get_len(key)),
                        std::move(converted));
            }
            return true;
        }
        default:
            return false;
    }
}

} // namespace

JSONValue Parse(const std::string& text, JSONParseResult* result,
                const JSONParseOptions& options) {
    yyjson_read_flag flags = 0;
    if (options.allowComments) flags |= YYJSON_READ_ALLOW_COMMENTS;
    if (options.allowTrailingCommas) flags |= YYJSON_READ_ALLOW_TRAILING_COMMAS;
    if (options.allowInfAndNaN) flags |= YYJSON_READ_ALLOW_INF_AND_NAN;

    yyjson_read_err readError;
    yyjson_doc* doc = yyjson_read_opts(const_cast<char*>(text.data()), text.size(),
                                       flags, nullptr, &readError);
    if (!doc) {
        SetError(result, readError.msg ? readError.msg : "parse error",
                 readError.pos, text);
        return JSONValue();
    }

    JSONValue root;
    bool converted = ConvertNode(yyjson_doc_get_root(doc), root, 1,
                                 options.maxNestingDepth);
    yyjson_doc_free(doc);

    if (!converted) {
        SetError(result, "maximum nesting depth exceeded", 0, text);
        return JSONValue();
    }
    if (result) {
        *result = JSONParseResult();
        result->success = true;
    }
    return root;
}

JSONValue ParseFile(const std::string& filePath, JSONParseResult* result,
                    const JSONParseOptions& options) {
    FILE* file = std::fopen(filePath.c_str(), "rb");
    if (!file) {
        SetError(result, "cannot open file: " + filePath, 0, std::string());
        return JSONValue();
    }
    std::string text;
    char buffer[65536];
    size_t bytesRead;
    while ((bytesRead = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        text.append(buffer, bytesRead);
    }
    bool readFailed = std::ferror(file) != 0;
    std::fclose(file);
    if (readFailed) {
        SetError(result, "cannot read file: " + filePath, 0, std::string());
        return JSONValue();
    }
    return Parse(text, result, options);
}

// ===== Serialization =====

namespace {

// Builds the yyjson mutable tree mirroring the public DOM. Depth-bounded for
// the same reason as ConvertNode.
yyjson_mut_val* BuildNode(yyjson_mut_doc* doc, const JSONValue& value,
                          size_t depth, size_t maxDepth) {
    if (depth > maxDepth) return nullptr;
    switch (value.GetType()) {
        case JSONType::Null:
            return yyjson_mut_null(doc);
        case JSONType::Boolean:
            return yyjson_mut_bool(doc, value.GetBoolean());
        case JSONType::Number:
            if (value.IsInteger()) return yyjson_mut_sint(doc, value.GetInteger());
            return yyjson_mut_real(doc, value.GetNumber());
        case JSONType::String: {
            std::string s = value.GetString();
            return yyjson_mut_strncpy(doc, s.data(), s.size());
        }
        case JSONType::Array: {
            yyjson_mut_val* array = yyjson_mut_arr(doc);
            if (!array) return nullptr;
            for (const JSONValue& element : value.GetElements()) {
                yyjson_mut_val* child = BuildNode(doc, element, depth + 1, maxDepth);
                if (!child || !yyjson_mut_arr_append(array, child)) return nullptr;
            }
            return array;
        }
        case JSONType::Object: {
            yyjson_mut_val* object = yyjson_mut_obj(doc);
            if (!object) return nullptr;
            for (const JSONValue::Member& member : value.GetMembers()) {
                yyjson_mut_val* key = yyjson_mut_strncpy(doc, member.first.data(),
                                                         member.first.size());
                yyjson_mut_val* child = BuildNode(doc, member.second, depth + 1, maxDepth);
                if (!key || !child || !yyjson_mut_obj_add(object, key, child)) return nullptr;
            }
            return object;
        }
    }
    return nullptr;
}

} // namespace

std::string Serialize(const JSONValue& value, const JSONSerializeOptions& options) {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    if (!doc) return std::string();

    yyjson_mut_val* root = BuildNode(doc, value, 1, options.maxNestingDepth);
    if (!root) {
        yyjson_mut_doc_free(doc);
        return std::string();
    }
    yyjson_mut_doc_set_root(doc, root);

    yyjson_write_flag flags = 0;
    if (options.pretty) {
        flags |= (options.indentWidth == 2) ? YYJSON_WRITE_PRETTY_TWO_SPACES
                                            : YYJSON_WRITE_PRETTY;
    }
    if (options.escapeNonAscii) flags |= YYJSON_WRITE_ESCAPE_UNICODE;
    // Keep output strictly valid JSON: non-finite numbers become null.
    flags |= YYJSON_WRITE_INF_AND_NAN_AS_NULL;

    size_t length = 0;
    char* text = yyjson_mut_write(doc, flags, &length);
    std::string output = text ? std::string(text, length) : std::string();
    if (text) std::free(text);
    yyjson_mut_doc_free(doc);
    return output;
}

bool SerializeToFile(const std::string& filePath, const JSONValue& value,
                     const JSONSerializeOptions& options) {
    std::string text = Serialize(value, options);
    if (text.empty() && !value.IsNull()) {
        // Serialize() only returns empty output on failure (any non-null
        // value serializes to at least two characters).
        return false;
    }
    FILE* file = std::fopen(filePath.c_str(), "wb");
    if (!file) return false;
    size_t written = std::fwrite(text.data(), 1, text.size(), file);
    bool ok = (written == text.size());
    ok = (std::fclose(file) == 0) && ok;
    return ok;
}

std::string EscapeString(const std::string& text) {
    return Serialize(JSONValue(text));
}

// ===== Framework type helpers =====

JSONValue FromColor(const Color& color) {
    JSONValue v = JSONValue::MakeArray();
    v.Append(static_cast<int64_t>(color.r));
    v.Append(static_cast<int64_t>(color.g));
    v.Append(static_cast<int64_t>(color.b));
    v.Append(static_cast<int64_t>(color.a));
    return v;
}

bool ToColor(const JSONValue& value, Color& outColor) {
    if (!value.IsArray() || value.GetSize() < 3) return false;
    for (size_t i = 0; i < value.GetSize() && i < 4; ++i) {
        if (!value.At(i).IsNumber()) return false;
    }
    auto channel = [&value](size_t index, int64_t fallback) -> uint8_t {
        int64_t n = value.At(index).GetInteger(fallback);
        if (n < 0) n = 0;
        if (n > 255) n = 255;
        return static_cast<uint8_t>(n);
    };
    outColor.r = channel(0, 0);
    outColor.g = channel(1, 0);
    outColor.b = channel(2, 0);
    outColor.a = value.GetSize() >= 4 ? channel(3, 255) : 255;
    return true;
}

JSONValue FromPoint(const Point2Dd& point) {
    JSONValue v = JSONValue::MakeArray();
    v.Append(point.x);
    v.Append(point.y);
    return v;
}

bool ToPoint(const JSONValue& value, Point2Dd& outPoint) {
    if (!value.IsArray() || value.GetSize() != 2) return false;
    if (!value.At(0).IsNumber() || !value.At(1).IsNumber()) return false;
    outPoint.x = value.At(0).GetNumber();
    outPoint.y = value.At(1).GetNumber();
    return true;
}

JSONValue FromRect(const Rect2Dd& rect) {
    JSONValue v = JSONValue::MakeArray();
    v.Append(rect.x);
    v.Append(rect.y);
    v.Append(rect.width);
    v.Append(rect.height);
    return v;
}

bool ToRect(const JSONValue& value, Rect2Dd& outRect) {
    if (!value.IsArray() || value.GetSize() != 4) return false;
    for (size_t i = 0; i < 4; ++i) {
        if (!value.At(i).IsNumber()) return false;
    }
    outRect.x = value.At(0).GetNumber();
    outRect.y = value.At(1).GetNumber();
    outRect.width = value.At(2).GetNumber();
    outRect.height = value.At(3).GetNumber();
    return true;
}

} // namespace JSON

} // namespace UltraCanvas
