// Apps/UltraDBAdmin/engine/DbAdminValueTypes.cpp
// Version: 0.2.0 - MySQL / MariaDB: UTC_TIMESTAMP(), UUID()
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminValueTypes.h"

#include <cctype>

namespace UltraDBAdmin {
namespace {

std::string Lower(std::string text) {
    for (auto& c : text)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

bool Has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

bool StartsWith(const std::string& text, const char* prefix) { return text.rfind(prefix, 0) == 0; }

bool Digits(const std::string& text, size_t from, size_t count) {
    if (from + count > text.size()) return false;
    for (size_t i = from; i < from + count; ++i)
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) return false;
    return true;
}

bool IsInteger(const std::string& text) {
    size_t i = (!text.empty() && (text[0] == '-' || text[0] == '+')) ? 1 : 0;
    return i < text.size() && Digits(text, i, text.size() - i);
}

// 12, -3.5, .5, 1e6: as SQL reads a number - a dot, never a comma.
bool IsDecimal(const std::string& text) {
    size_t i = (!text.empty() && (text[0] == '-' || text[0] == '+')) ? 1 : 0;
    bool digits = false, dot = false;
    for (; i < text.size(); ++i) {
        const char c = text[i];
        if (std::isdigit(static_cast<unsigned char>(c))) { digits = true; continue; }
        if (c == '.' && !dot) { dot = true; continue; }
        if ((c == 'e' || c == 'E') && digits) return IsInteger(text.substr(i + 1));
        return false;
    }
    return digits;
}

bool IsDate(const std::string& text) {
    return text.size() == 10 && Digits(text, 0, 4) && text[4] == '-' && Digits(text, 5, 2) && text[7] == '-' &&
           Digits(text, 8, 2) && text.substr(5, 2) >= "01" && text.substr(5, 2) <= "12" && text.substr(8, 2) >= "01" &&
           text.substr(8, 2) <= "31";
}

// HH:MM, HH:MM:SS, HH:MM:SS.fff - and what follows (a zone) left to the database.
bool StartsAsTime(const std::string& text, size_t at) {
    return Digits(text, at, 2) && at + 2 < text.size() && text[at + 2] == ':' && Digits(text, at + 3, 2);
}

} // namespace

ValueKind KindOfColumn(const UltraDbColumnSchema& column) {
    const std::string type = Lower(column.declaredType);
    if (type.empty()) {
        switch (column.valueType) {
            case UltraDbType::Boolean: return ValueKind::Boolean;
            case UltraDbType::Int:     return ValueKind::Integer;
            case UltraDbType::Double:  return ValueKind::Decimal;
            case UltraDbType::Blob:    return ValueKind::Blob;
            default:                   return ValueKind::Any;
        }
    }
    if (type == "bool" || type == "boolean") return ValueKind::Boolean;
    if (Has(type, "blob") || type == "bytea") return ValueKind::Blob;
    if (type == "uuid") return ValueKind::Uuid;
    if (StartsWith(type, "json")) return ValueKind::Json;
    if (StartsWith(type, "timestamp") || type == "datetime") return ValueKind::Timestamp;
    if (type == "date") return ValueKind::Date;
    if (StartsWith(type, "time")) return ValueKind::Time;
    if (Has(type, "int") || type == "serial" || type == "bigserial" || type == "smallserial") return ValueKind::Integer;
    if (StartsWith(type, "numeric") || StartsWith(type, "decimal") || Has(type, "real") || Has(type, "double") ||
        Has(type, "float") || type == "money")
        return ValueKind::Decimal;
    if (Has(type, "char") || Has(type, "text") || Has(type, "clob") || type == "citext" || type == "name")
        return ValueKind::Text;
    return ValueKind::Any;
}

std::string KindHint(ValueKind kind) {
    switch (kind) {
        case ValueKind::Boolean:   return "true or false";
        case ValueKind::Integer:   return "a whole number";
        case ValueKind::Decimal:   return "a number, with a dot: 12.5";
        case ValueKind::Date:      return "YYYY-MM-DD";
        case ValueKind::Time:      return "HH:MM:SS";
        case ValueKind::Timestamp: return "YYYY-MM-DD HH:MM:SS";
        case ValueKind::Uuid:      return "8-4-4-4-12 hex digits";
        case ValueKind::Json:      return "JSON";
        default:                   return std::string();
    }
}

bool CheckValue(ValueKind kind, const std::string& column, const std::string& text, std::string& error) {
    error.clear();
    switch (kind) {
        case ValueKind::Boolean: {
            const std::string v = Lower(text);
            if (v == "true" || v == "false" || v == "1" || v == "0" || v == "t" || v == "f" || v == "yes" || v == "no")
                return true;
            error = column + " takes true or false, not \"" + text + "\".";
            return false;
        }
        case ValueKind::Integer:
            if (IsInteger(text)) return true;
            error = column + " takes a whole number, not \"" + text + "\".";
            return false;
        case ValueKind::Decimal:
            if (IsDecimal(text)) return true;
            error = column + " takes a number written with a dot (12.5), not \"" + text + "\".";
            return false;
        case ValueKind::Date:
            if (IsDate(text)) return true;
            error = column + " takes a date as YYYY-MM-DD (2026-01-31), not \"" + text + "\".";
            return false;
        case ValueKind::Time:
            if (StartsAsTime(text, 0)) return true;
            error = column + " takes a time as HH:MM:SS, not \"" + text + "\".";
            return false;
        case ValueKind::Timestamp:
            // A date alone is midnight; else a date, a space or T, a time.
            if (IsDate(text) ||
                (text.size() >= 16 && IsDate(text.substr(0, 10)) && (text[10] == ' ' || text[10] == 'T') &&
                 StartsAsTime(text, 11)))
                return true;
            error = column + " takes a date and time as YYYY-MM-DD HH:MM:SS, not \"" + text + "\".";
            return false;
        case ValueKind::Uuid: {
            std::string hex;
            for (const char c : text)
                if (c != '-') hex += c;
            bool ok = hex.size() == 32;
            for (const char c : hex) ok = ok && std::isxdigit(static_cast<unsigned char>(c));
            if (ok) return true;
            error = column + " takes a UUID (32 hex digits), not \"" + text + "\".";
            return false;
        }
        default:
            return true;
    }
}

std::vector<ValueFunction> ValueFunctions(SqlEngine engine, ValueKind kind) {
    const bool sqlite = engine == SqlEngine::Sqlite;
    const bool mysql = engine == SqlEngine::MySql;
    std::vector<ValueFunction> out;
    const bool any = kind == ValueKind::Any || kind == ValueKind::Text;
    if (kind == ValueKind::Timestamp || any) {
        out.push_back({"Now (CURRENT_TIMESTAMP)", "CURRENT_TIMESTAMP"});
        if (sqlite) out.push_back({"Now, local time", "datetime('now', 'localtime')"});
        else if (mysql) out.push_back({"Now, in UTC (UTC_TIMESTAMP())", "UTC_TIMESTAMP()"});
        else out.push_back({"Now, as the transaction began (now())", "now()"});
    }
    if (kind == ValueKind::Date || any) out.push_back({"Today (CURRENT_DATE)", "CURRENT_DATE"});
    if (kind == ValueKind::Time || any) out.push_back({"This time (CURRENT_TIME)", "CURRENT_TIME"});
    if (kind == ValueKind::Uuid || (any && !sqlite)) {
        if (mysql) out.push_back({"A new UUID (UUID())", "UUID()"});
        else if (!sqlite) out.push_back({"A new UUID (gen_random_uuid())", "gen_random_uuid()"});
    }
    if (any && sqlite) out.push_back({"A random id (32 hex digits)", "lower(hex(randomblob(16)))"});
    if (any && !sqlite) out.push_back({"The user (CURRENT_USER)", "CURRENT_USER"});
    return out;
}

bool IsValueFunction(const std::string& sql) {
    for (const SqlEngine engine : {SqlEngine::Sqlite, SqlEngine::Postgres, SqlEngine::MySql})
        for (const ValueKind kind : {ValueKind::Any, ValueKind::Timestamp, ValueKind::Date, ValueKind::Time, ValueKind::Uuid})
            for (const auto& function : ValueFunctions(engine, kind))
                if (function.sql == sql) return true;
    return false;
}

} // namespace UltraDBAdmin
