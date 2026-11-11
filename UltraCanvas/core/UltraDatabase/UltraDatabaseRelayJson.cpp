// core/UltraDatabase/UltraDatabaseRelayJson.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraDatabaseRelayJson.h"

#include "UltraDatabaseInternal.h"   // FormatDoubleClassic, ParseDoubleClassic

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ultradb_relay {

// ===== VALUE =====

Json Json::Truth(bool v) { Json j; j.type = Type::Boolean; j.boolean = v; return j; }
Json Json::Int(int64_t v) { Json j; j.type = Type::Int; j.integer = v; return j; }
Json Json::Double(double v) { Json j; j.type = Type::Double; j.real = v; return j; }
Json Json::String(std::string v) { Json j; j.type = Type::String; j.text = std::move(v); return j; }
Json Json::Array() { Json j; j.type = Type::Array; return j; }
Json Json::Object() { Json j; j.type = Type::Object; return j; }

Json& Json::Set(const std::string& key, Json value) {
    for (size_t i = 0; i < keys.size(); ++i)
        if (keys[i] == key) {
            items[i] = std::move(value);
            return *this;
        }
    keys.push_back(key);
    items.push_back(std::move(value));
    return *this;
}

Json& Json::Push(Json value) {
    items.push_back(std::move(value));
    return *this;
}

const Json* Json::Get(const std::string& key) const {
    if (type != Type::Object) return nullptr;
    for (size_t i = 0; i < keys.size(); ++i)
        if (keys[i] == key) return &items[i];
    return nullptr;
}

std::string Json::Str(const std::string& key) const {
    const Json* j = Get(key);
    return j && j->type == Type::String ? j->text : std::string();
}

int64_t Json::Num(const std::string& key, int64_t fallback) const {
    const Json* j = Get(key);
    if (!j) return fallback;
    if (j->type == Type::Int) return j->integer;
    if (j->type == Type::Double) return static_cast<int64_t>(j->real);
    return fallback;
}

bool Json::Flag(const std::string& key) const {
    const Json* j = Get(key);
    return j && j->type == Type::Boolean && j->boolean;
}

// ===== TEXT =====

namespace {

void DumpString(const std::string& s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof buffer, "\\u%04x", c);
                    out += buffer;
                } else {
                    out += static_cast<char>(c);   // UTF-8 passes as it is
                }
        }
    }
    out += '"';
}

void Dump(const Json& j, std::string& out) {
    switch (j.type) {
        case Json::Type::Null:   out += "null"; break;
        case Json::Type::Boolean:   out += j.boolean ? "true" : "false"; break;
        case Json::Type::Int:    out += std::to_string(j.integer); break;
        case Json::Type::Double: {
            if (!std::isfinite(j.real)) {
                // Not JSON numbers: an object a reader cannot take for one.
                out += std::isnan(j.real) ? "{\"d\":\"nan\"}" : j.real < 0 ? "{\"d\":\"-inf\"}" : "{\"d\":\"inf\"}";
                break;
            }
            std::string number = ultradb_internal::FormatDoubleClassic(j.real);
            // Always a double on the way back: 1 would read as an integer.
            if (number.find_first_of(".eE") == std::string::npos) number += ".0";
            out += number;
            break;
        }
        case Json::Type::String: DumpString(j.text, out); break;
        case Json::Type::Array:
            out += '[';
            for (size_t i = 0; i < j.items.size(); ++i) {
                if (i) out += ',';
                Dump(j.items[i], out);
            }
            out += ']';
            break;
        case Json::Type::Object:
            out += '{';
            for (size_t i = 0; i < j.keys.size(); ++i) {
                if (i) out += ',';
                DumpString(j.keys[i], out);
                out += ':';
                Dump(j.items[i], out);
            }
            out += '}';
            break;
    }
}

struct Parser {
    const std::string& s;
    size_t at = 0;
    std::string error;
    int depth = 0;

    void Space() {
        while (at < s.size() && (s[at] == ' ' || s[at] == '\t' || s[at] == '\n' || s[at] == '\r')) ++at;
    }
    bool Fail(const char* what) {
        if (error.empty()) error = std::string(what) + " at byte " + std::to_string(at);
        return false;
    }
    bool Literal(const char* word) {
        size_t i = 0;
        for (; word[i]; ++i)
            if (at + i >= s.size() || s[at + i] != word[i]) return false;
        at += i;
        return true;
    }
    static void Utf8(uint32_t cp, std::string& out) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    bool Hex4(uint32_t& out) {
        if (at + 4 > s.size()) return Fail("a short \\u escape");
        out = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s[at++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<uint32_t>(c - 'A' + 10);
            else return Fail("a bad \\u escape");
        }
        return true;
    }
    bool String(std::string& out) {
        ++at;   // the opening quote
        while (at < s.size()) {
            const char c = s[at++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (at >= s.size()) break;
            const char e = s[at++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!Hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && at + 6 <= s.size() && s[at] == '\\' && s[at + 1] == 'u') {
                        at += 2;
                        uint32_t low = 0;
                        if (!Hex4(low)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                    Utf8(cp, out);
                    break;
                }
                default: return Fail("a bad escape");
            }
        }
        return Fail("a string never closed");
    }
    bool Number(Json& out) {
        const size_t start = at;
        if (at < s.size() && s[at] == '-') ++at;
        bool real = false;
        while (at < s.size()) {
            const char c = s[at];
            if (c >= '0' && c <= '9') { ++at; continue; }
            if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') { real = true; ++at; continue; }
            break;
        }
        const std::string number = s.substr(start, at - start);
        if (number.empty() || number == "-") return Fail("a bad number");
        if (!real) {
            errno = 0;
            char* end = nullptr;
            const long long v = std::strtoll(number.c_str(), &end, 10);
            if (errno == 0 && end && *end == '\0') {
                out = Json::Int(v);
                return true;
            }
        }
        double d = 0.0;
        if (!ultradb_internal::ParseDoubleClassic(number, d)) return Fail("a bad number");
        out = Json::Double(d);
        return true;
    }
    bool Value(Json& out) {
        Space();
        if (at >= s.size()) return Fail("a value missing");
        if (++depth > 200) return Fail("nesting too deep");
        const char c = s[at];
        bool ok = true;
        if (c == '{') {
            out = Json::Object();
            ++at;
            Space();
            if (at < s.size() && s[at] == '}') {
                ++at;
            } else {
                while (ok) {
                    Space();
                    std::string key;
                    if (at >= s.size() || s[at] != '"' || !String(key)) { ok = Fail("a member name expected"); break; }
                    Space();
                    if (at >= s.size() || s[at] != ':') { ok = Fail("':' expected"); break; }
                    ++at;
                    Json value;
                    if (!Value(value)) { ok = false; break; }
                    out.keys.push_back(std::move(key));
                    out.items.push_back(std::move(value));
                    Space();
                    if (at < s.size() && s[at] == ',') { ++at; continue; }
                    if (at < s.size() && s[at] == '}') { ++at; break; }
                    ok = Fail("',' or '}' expected");
                }
            }
            // {"d": "nan"} is a double that JSON has no number for.
            if (ok && out.keys.size() == 1 && out.keys[0] == "d" && out.items[0].type == Json::Type::String) {
                const std::string word = out.items[0].text;
                double d = 0.0;
                if (ultradb_internal::ParseDoubleClassic(word, d)) out = Json::Double(d);
            }
        } else if (c == '[') {
            out = Json::Array();
            ++at;
            Space();
            if (at < s.size() && s[at] == ']') {
                ++at;
            } else {
                while (ok) {
                    Json item;
                    if (!Value(item)) { ok = false; break; }
                    out.items.push_back(std::move(item));
                    Space();
                    if (at < s.size() && s[at] == ',') { ++at; continue; }
                    if (at < s.size() && s[at] == ']') { ++at; break; }
                    ok = Fail("',' or ']' expected");
                }
            }
        } else if (c == '"') {
            std::string text;
            ok = String(text);
            out = Json::String(std::move(text));
        } else if (Literal("true")) {
            out = Json::Truth(true);
        } else if (Literal("false")) {
            out = Json::Truth(false);
        } else if (Literal("null")) {
            out = Json::Null();
        } else {
            ok = Number(out);
        }
        --depth;
        return ok;
    }
};

} // namespace

std::string Json::Dump() const {
    std::string out;
    ultradb_relay::Dump(*this, out);
    return out;
}

bool Json::Parse(const std::string& text, Json& out, std::string& error) {
    Parser p{ text, 0, std::string(), 0 };
    out = Json();
    if (!p.Value(out)) {
        error = p.error;
        return false;
    }
    p.Space();
    if (p.at != text.size()) {
        error = "text after the value at byte " + std::to_string(p.at);
        return false;
    }
    return true;
}

// ===== BASE64 =====

namespace {
const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

std::string Base64Encode(const std::vector<uint8_t>& bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const uint32_t n = (static_cast<uint32_t>(bytes[i]) << 16) |
                           (i + 1 < bytes.size() ? static_cast<uint32_t>(bytes[i + 1]) << 8 : 0) |
                           (i + 2 < bytes.size() ? static_cast<uint32_t>(bytes[i + 2]) : 0);
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += i + 1 < bytes.size() ? kAlphabet[(n >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? kAlphabet[n & 63] : '=';
    }
    return out;
}

bool Base64Decode(const std::string& text, std::vector<uint8_t>& out) {
    out.clear();
    uint32_t buffer = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=') break;
        const char* p = std::strchr(kAlphabet, c);
        if (!p || c == '\0') return false;
        buffer = (buffer << 6) | static_cast<uint32_t>(p - kAlphabet);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF));
        }
    }
    return true;
}

// ===== THE MODULE'S TYPES =====

Json FromValue(const UltraDbValue& value) {
    switch (value.Type()) {
        case UltraDbType::Null:    return Json::Null();
        case UltraDbType::Boolean: return Json::Truth(value.AsBool());
        case UltraDbType::Int:     return Json::Int(value.AsInt64());
        case UltraDbType::Double:  return Json::Double(value.AsDouble());
        case UltraDbType::Text:    return Json::String(value.AsString());
        case UltraDbType::Blob: {
            Json blob = Json::Object();
            blob.Set("b", Json::String(Base64Encode(value.AsBlob())));
            return blob;
        }
    }
    return Json::Null();
}

UltraDbValue ToValue(const Json& json) {
    switch (json.type) {
        case Json::Type::Null:   return UltraDbValue();
        case Json::Type::Boolean:   return UltraDbValue(json.boolean);
        case Json::Type::Int:    return UltraDbValue(json.integer);
        case Json::Type::Double: return UltraDbValue(json.real);
        case Json::Type::String: return UltraDbValue(json.text);
        case Json::Type::Object: {
            std::vector<uint8_t> bytes;
            Base64Decode(json.Str("b"), bytes);
            return UltraDbValue(std::move(bytes));
        }
        case Json::Type::Array:  return UltraDbValue();
    }
    return UltraDbValue();
}

Json FromParams(const UltraDbParams& params) {
    Json list = Json::Array();
    for (const UltraDbValue& v : params) list.Push(FromValue(v));
    return list;
}

UltraDbParams ToParams(const Json* json) {
    UltraDbParams params;
    if (json)
        for (const Json& item : json->items) params.push_back(ToValue(item));
    return params;
}

Json FromStrings(const std::vector<std::string>& strings) {
    Json list = Json::Array();
    for (const std::string& s : strings) list.Push(Json::String(s));
    return list;
}

std::vector<std::string> ToStrings(const Json* json) {
    std::vector<std::string> out;
    if (json)
        for (const Json& item : json->items) out.push_back(item.text);
    return out;
}

Json FromResultSet(const UltraDbResultSet& rows) {
    Json out = Json::Object();
    out.Set("columns", FromStrings(rows.Columns()));
    if (!rows.ColumnInfo().empty()) {
        Json info = Json::Array();
        for (const UltraDbColumnInfo& c : rows.ColumnInfo()) {
            Json one = Json::Object();
            one.Set("name", Json::String(c.name));
            one.Set("type", Json::String(c.declaredType));
            one.Set("schema", Json::String(c.sourceSchema));
            one.Set("table", Json::String(c.sourceTable));
            one.Set("column", Json::String(c.sourceColumn));
            info.Push(std::move(one));
        }
        out.Set("info", std::move(info));
    }
    Json list = Json::Array();
    for (const auto& row : rows) {
        Json cells = Json::Array();
        for (size_t i = 0; i < row.Size(); ++i) cells.Push(FromValue(row[i]));
        list.Push(std::move(cells));
    }
    out.Set("rows", std::move(list));
    return out;
}

void ToResultSet(const Json* json, UltraDbResultSet& out) {
    out.Clear();
    out.SetColumns(json ? ToStrings(json->Get("columns")) : std::vector<std::string>());
    if (!json) return;
    if (const Json* info = json->Get("info")) {
        std::vector<UltraDbColumnInfo> columns;
        for (const Json& one : info->items)
            columns.push_back(UltraDbColumnInfo{ one.Str("name"), one.Str("type"), one.Str("schema"), one.Str("table"),
                                                 one.Str("column") });
        out.SetColumnInfo(std::move(columns));
    }
    if (const Json* rows = json->Get("rows"))
        for (const Json& cells : rows->items) {
            std::vector<UltraDbValue> values;
            values.reserve(cells.items.size());
            for (const Json& cell : cells.items) values.push_back(ToValue(cell));
            out.AddRow(std::move(values));
        }
}

namespace {

struct CodeNameEntry {
    UltraDbResultCode code;
    const char* name;
};

const CodeNameEntry kCodes[] = {
    { UltraDbResultCode::Ok, "Ok" },
    { UltraDbResultCode::InvalidArgument, "InvalidArgument" },
    { UltraDbResultCode::ConnectionNotFound, "ConnectionNotFound" },
    { UltraDbResultCode::DriverNotFound, "DriverNotFound" },
    { UltraDbResultCode::ConnectionFailed, "ConnectionFailed" },
    { UltraDbResultCode::CredentialsUnavailable, "CredentialsUnavailable" },
    { UltraDbResultCode::QueryFailed, "QueryFailed" },
    { UltraDbResultCode::ConstraintViolation, "ConstraintViolation" },
    { UltraDbResultCode::TypeMismatch, "TypeMismatch" },
    { UltraDbResultCode::NotFound, "NotFound" },
    { UltraDbResultCode::Busy, "Busy" },
    { UltraDbResultCode::Misuse, "Misuse" },
    { UltraDbResultCode::AlreadyExists, "AlreadyExists" },
    { UltraDbResultCode::Unsupported, "Unsupported" },
    { UltraDbResultCode::Internal, "Internal" },
    { UltraDbResultCode::Unknown, "Unknown" },
    { UltraDbResultCode::Cancelled, "Cancelled" },
    { UltraDbResultCode::HostKeyUnknown, "HostKeyUnknown" },
    { UltraDbResultCode::HostKeyChanged, "HostKeyChanged" },
};

} // namespace

const char* CodeName(UltraDbResultCode code) {
    for (const auto& entry : kCodes)
        if (entry.code == code) return entry.name;
    return "Unknown";
}

UltraDbResultCode CodeFromName(const std::string& name) {
    for (const auto& entry : kCodes)
        if (name == entry.name) return entry.code;
    return UltraDbResultCode::Unknown;
}

Json FromResult(const UltraDbResult& result) {
    Json out = Json::Object();
    out.Set("code", Json::String(CodeName(result.code)));
    if (!result.message.empty()) out.Set("message", Json::String(result.message));
    if (!result.sqlState.empty()) out.Set("sqlState", Json::String(result.sqlState));
    if (!result.driver.empty()) out.Set("driver", Json::String(result.driver));
    if (result.affectedRows) out.Set("affected", Json::Int(result.affectedRows));
    if (result.lastInsertId) out.Set("lastId", Json::Int(result.lastInsertId));
    if (!result.hostKeyFingerprint.empty()) out.Set("hostKey", Json::String(result.hostKeyFingerprint));
    return out;
}

UltraDbResult ToResult(const Json* json) {
    if (!json || json->type != Json::Type::Object)
        return UltraDbResult::Error(UltraDbResultCode::Internal, "the relay's answer has no outcome", "relay");
    UltraDbResult r;
    r.code = CodeFromName(json->Str("code"));
    r.success = r.code == UltraDbResultCode::Ok;
    r.message = json->Str("message");
    r.sqlState = json->Str("sqlState");
    r.driver = json->Str("driver");
    r.affectedRows = json->Num("affected");
    r.lastInsertId = json->Num("lastId");
    r.hostKeyFingerprint = json->Str("hostKey");
    return r;
}

Json FromStatements(const std::vector<UltraDbStatementResult>& statements) {
    Json list = Json::Array();
    for (const UltraDbStatementResult& s : statements) {
        Json one = Json::Object();
        one.Set("sql", Json::String(s.sql));
        one.Set("result", FromResult(s.result));
        one.Set("returnsRows", Json::Truth(s.returnsRows));
        if (s.returnsRows) one.Set("rows", FromResultSet(s.rows));
        list.Push(std::move(one));
    }
    return list;
}

void ToStatements(const Json* json, std::vector<UltraDbStatementResult>& out) {
    out.clear();
    if (!json) return;
    for (const Json& one : json->items) {
        UltraDbStatementResult s;
        s.sql = one.Str("sql");
        s.result = ToResult(one.Get("result"));
        s.returnsRows = one.Flag("returnsRows");
        if (s.returnsRows) ToResultSet(one.Get("rows"), s.rows);
        out.push_back(std::move(s));
    }
}

Json FromObjects(const std::vector<UltraDbObjectSummary>& objects) {
    Json list = Json::Array();
    for (const UltraDbObjectSummary& o : objects) {
        Json one = Json::Object();
        one.Set("schema", Json::String(o.schema));
        one.Set("name", Json::String(o.name));
        one.Set("kind", Json::Int(static_cast<int64_t>(o.kind)));
        list.Push(std::move(one));
    }
    return list;
}

void ToObjects(const Json* json, std::vector<UltraDbObjectSummary>& out) {
    out.clear();
    if (!json) return;
    for (const Json& one : json->items)
        out.push_back(UltraDbObjectSummary{ one.Str("schema"), one.Str("name"),
                                            static_cast<UltraDbObjectKind>(one.Num("kind")) });
}

Json FromTable(const UltraDbTableSchema& t) {
    Json out = Json::Object();
    out.Set("schema", Json::String(t.schema));
    out.Set("name", Json::String(t.name));
    out.Set("kind", Json::Int(static_cast<int64_t>(t.kind)));
    Json columns = Json::Array();
    for (const UltraDbColumnSchema& c : t.columns) {
        Json one = Json::Object();
        one.Set("name", Json::String(c.name));
        one.Set("type", Json::String(c.declaredType));
        one.Set("valueType", Json::Int(static_cast<int64_t>(c.valueType)));
        one.Set("nullable", Json::Truth(c.nullable));
        one.Set("autoIncrement", Json::Truth(c.autoIncrement));
        one.Set("generated", Json::Truth(c.generated));
        one.Set("hasDefault", Json::Truth(c.hasDefault));
        one.Set("default", Json::String(c.defaultExpression));
        one.Set("collation", Json::String(c.collation));
        one.Set("comment", Json::String(c.comment));
        columns.Push(std::move(one));
    }
    out.Set("columns", std::move(columns));
    out.Set("primaryKey", FromStrings(t.primaryKey));
    Json keys = Json::Array();
    for (const UltraDbForeignKeySchema& k : t.foreignKeys) {
        Json one = Json::Object();
        one.Set("name", Json::String(k.name));
        one.Set("columns", FromStrings(k.columns));
        one.Set("refSchema", Json::String(k.refSchema));
        one.Set("refTable", Json::String(k.refTable));
        one.Set("refColumns", FromStrings(k.refColumns));
        one.Set("onUpdate", Json::String(k.onUpdate));
        one.Set("onDelete", Json::String(k.onDelete));
        keys.Push(std::move(one));
    }
    out.Set("foreignKeys", std::move(keys));
    Json indexes = Json::Array();
    for (const UltraDbIndexSchema& i : t.indexes) {
        Json one = Json::Object();
        one.Set("name", Json::String(i.name));
        one.Set("columns", FromStrings(i.columns));
        one.Set("unique", Json::Truth(i.unique));
        one.Set("primary", Json::Truth(i.primary));
        indexes.Push(std::move(one));
    }
    out.Set("indexes", std::move(indexes));
    Json triggers = Json::Array();
    for (const UltraDbTriggerSchema& tr : t.triggers) {
        Json one = Json::Object();
        one.Set("name", Json::String(tr.name));
        one.Set("definition", Json::String(tr.definition));
        triggers.Push(std::move(one));
    }
    out.Set("triggers", std::move(triggers));
    out.Set("view", Json::String(t.viewDefinition));
    out.Set("comment", Json::String(t.comment));
    out.Set("approxRows", Json::Int(t.approxRows));
    out.Set("sizeBytes", Json::Int(t.sizeBytes));
    return out;
}

void ToTable(const Json* json, UltraDbTableSchema& out) {
    out = UltraDbTableSchema();
    if (!json) return;
    out.schema = json->Str("schema");
    out.name = json->Str("name");
    out.kind = static_cast<UltraDbObjectKind>(json->Num("kind"));
    if (const Json* columns = json->Get("columns"))
        for (const Json& one : columns->items) {
            UltraDbColumnSchema c;
            c.name = one.Str("name");
            c.declaredType = one.Str("type");
            c.valueType = static_cast<UltraDbType>(one.Num("valueType"));
            c.nullable = one.Flag("nullable");
            c.autoIncrement = one.Flag("autoIncrement");
            c.generated = one.Flag("generated");
            c.hasDefault = one.Flag("hasDefault");
            c.defaultExpression = one.Str("default");
            c.collation = one.Str("collation");
            c.comment = one.Str("comment");
            out.columns.push_back(std::move(c));
        }
    out.primaryKey = ToStrings(json->Get("primaryKey"));
    if (const Json* keys = json->Get("foreignKeys"))
        for (const Json& one : keys->items)
            out.foreignKeys.push_back(UltraDbForeignKeySchema{ one.Str("name"), ToStrings(one.Get("columns")),
                                                               one.Str("refSchema"), one.Str("refTable"),
                                                               ToStrings(one.Get("refColumns")), one.Str("onUpdate"),
                                                               one.Str("onDelete") });
    if (const Json* indexes = json->Get("indexes"))
        for (const Json& one : indexes->items)
            out.indexes.push_back(UltraDbIndexSchema{ one.Str("name"), ToStrings(one.Get("columns")),
                                                      one.Flag("unique"), one.Flag("primary") });
    if (const Json* triggers = json->Get("triggers"))
        for (const Json& one : triggers->items)
            out.triggers.push_back(UltraDbTriggerSchema{ one.Str("name"), one.Str("definition") });
    out.viewDefinition = json->Str("view");
    out.comment = json->Str("comment");
    out.approxRows = json->Num("approxRows", -1);
    out.sizeBytes = json->Num("sizeBytes", -1);
}

} // namespace ultradb_relay
