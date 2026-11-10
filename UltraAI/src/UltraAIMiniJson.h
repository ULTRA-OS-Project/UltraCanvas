// UltraAI/src/UltraAIMiniJson.h
// The module's own small JSON reader and string writer, private to
// UltraAI_Core. The core builds without the vendored nlohmann/json (the
// module is standalone), yet two parts of it read JSON a model wrote: the
// text-LLM translator (its reply) and the agent (tool-call arguments).
// Not installed and not a public header.
// Version: 0.1.0 - moved out of TextLLMTranslator.cpp for the agent
// Last Modified: 2026-11-09
// Author: UltraAI Module
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UltraAI {
namespace detail {

// =====================================================================
// A JSON value and a reader for the model's reply
// =====================================================================

// An object is kept as two parallel vectors rather than a vector of pairs:
// std::vector may hold the incomplete JsonValue, but a std::pair of it
// cannot be instantiated inside the class (libstdc++ 12 with clang 19).
struct JsonValue {
    enum class Kind { Null, Boolean, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<JsonValue> array;
    std::vector<std::string> objectKeys;
    std::vector<JsonValue> objectValues;

    const JsonValue* Get(const std::string& key) const;
};

inline const JsonValue* JsonValue::Get(const std::string& key) const {
    if (kind != Kind::Object) return nullptr;
    for (size_t i = 0; i < objectKeys.size(); ++i) {
        if (objectKeys[i] == key) return &objectValues[i];
    }
    return nullptr;
}

class JsonReader {
public:
    explicit JsonReader(const std::string& text) : text_(text) {}

    bool Parse(JsonValue& out) {
        SkipSpace();
        if (!ParseValue(out)) return false;
        SkipSpace();
        return pos_ == text_.size();
    }

private:
    const std::string& text_;
    size_t pos_ = 0;

    bool AtEnd() const { return pos_ >= text_.size(); }
    char Peek() const { return AtEnd() ? '\0' : text_[pos_]; }

    void SkipSpace() {
        while (!AtEnd()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
            else break;
        }
    }

    bool Consume(const char* literal) {
        size_t n = 0;
        while (literal[n]) ++n;
        if (text_.compare(pos_, n, literal) != 0) return false;
        pos_ += n;
        return true;
    }

    bool ParseValue(JsonValue& out) {
        switch (Peek()) {
            case '{': return ParseObject(out);
            case '[': return ParseArray(out);
            case '"': out.kind = JsonValue::Kind::String;
                      return ParseString(out.string);
            case 't': out.kind = JsonValue::Kind::Boolean; out.boolean = true;
                      return Consume("true");
            case 'f': out.kind = JsonValue::Kind::Boolean; out.boolean = false;
                      return Consume("false");
            case 'n': out.kind = JsonValue::Kind::Null;
                      return Consume("null");
            default:  return ParseNumber(out);
        }
    }

    bool ParseObject(JsonValue& out) {
        out.kind = JsonValue::Kind::Object;
        ++pos_;                                   // '{'
        SkipSpace();
        if (Peek() == '}') { ++pos_; return true; }
        for (;;) {
            SkipSpace();
            if (Peek() != '"') return false;
            std::string key;
            if (!ParseString(key)) return false;
            SkipSpace();
            if (Peek() != ':') return false;
            ++pos_;
            SkipSpace();
            JsonValue value;
            if (!ParseValue(value)) return false;
            out.objectKeys.push_back(std::move(key));
            out.objectValues.push_back(std::move(value));
            SkipSpace();
            if (Peek() == ',') { ++pos_; continue; }
            if (Peek() == '}') { ++pos_; return true; }
            return false;
        }
    }

    bool ParseArray(JsonValue& out) {
        out.kind = JsonValue::Kind::Array;
        ++pos_;                                   // '['
        SkipSpace();
        if (Peek() == ']') { ++pos_; return true; }
        for (;;) {
            SkipSpace();
            JsonValue value;
            if (!ParseValue(value)) return false;
            out.array.push_back(std::move(value));
            SkipSpace();
            if (Peek() == ',') { ++pos_; continue; }
            if (Peek() == ']') { ++pos_; return true; }
            return false;
        }
    }

    static int HexDigit(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
    }

    bool ParseHex4(uint32_t& code) {
        if (pos_ + 4 > text_.size()) return false;
        code = 0;
        for (int i = 0; i < 4; ++i) {
            const int d = HexDigit(text_[pos_ + static_cast<size_t>(i)]);
            if (d < 0) return false;
            code = (code << 4) | static_cast<uint32_t>(d);
        }
        pos_ += 4;
        return true;
    }

    static void AppendUtf8(std::string& out, uint32_t code) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    bool ParseString(std::string& out) {
        ++pos_;                                   // opening quote
        for (;;) {
            if (AtEnd()) return false;
            const char c = text_[pos_++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (AtEnd()) return false;
            const char e = text_[pos_++];
            switch (e) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u': {
                    uint32_t code = 0;
                    if (!ParseHex4(code)) return false;
                    if (code >= 0xD800 && code <= 0xDBFF) {
                        // High surrogate: a low one must follow.
                        uint32_t low = 0;
                        if (Consume("\\u") && ParseHex4(low) &&
                            low >= 0xDC00 && low <= 0xDFFF) {
                            code = 0x10000 + ((code - 0xD800) << 10) +
                                   (low - 0xDC00);
                        } else {
                            code = 0xFFFD;
                        }
                    } else if (code >= 0xDC00 && code <= 0xDFFF) {
                        code = 0xFFFD;
                    }
                    AppendUtf8(out, code);
                    break;
                }
                default: return false;
            }
        }
    }

    // JSON numbers are dot-decimal by definition; read them digit by digit
    // so the result never depends on the process locale.
    bool ParseNumber(JsonValue& out) {
        out.kind = JsonValue::Kind::Number;
        const size_t start = pos_;
        bool negative = false;
        if (Peek() == '-') { negative = true; ++pos_; }
        double value = 0.0;
        bool digits = false;
        while (Peek() >= '0' && Peek() <= '9') {
            value = value * 10.0 + (text_[pos_] - '0');
            ++pos_;
            digits = true;
        }
        if (Peek() == '.') {
            ++pos_;
            double scale = 0.1;
            while (Peek() >= '0' && Peek() <= '9') {
                value += (text_[pos_] - '0') * scale;
                scale *= 0.1;
                ++pos_;
                digits = true;
            }
        }
        if (!digits) { pos_ = start; return false; }
        if (Peek() == 'e' || Peek() == 'E') {
            ++pos_;
            bool expNegative = false;
            if (Peek() == '+') ++pos_;
            else if (Peek() == '-') { expNegative = true; ++pos_; }
            int exponent = 0;
            bool expDigits = false;
            while (Peek() >= '0' && Peek() <= '9') {
                if (exponent < 1000) exponent = exponent * 10 + (text_[pos_] - '0');
                ++pos_;
                expDigits = true;
            }
            if (!expDigits) { pos_ = start; return false; }
            for (int i = 0; i < exponent; ++i) {
                value = expNegative ? value / 10.0 : value * 10.0;
            }
        }
        out.number = negative ? -value : value;
        return true;
    }
};

// Append `s` to `out` as a quoted JSON string.
inline void AppendJsonString(std::string& out, const std::string& s) {
    out += '"';
    for (const unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if (c < 0x20) {
                    static const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 0xF];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

} // namespace detail
} // namespace UltraAI
