// core/UltraDatabase/UltraDatabaseRelayJson.h
// The relay's wire format (UltraDatabaseRelay.h): a small JSON value, its
// text, and the module's types to and from it. Internal - UltraDatabase links
// no JSON library, and the relay needs only this much.
//
// A value keeps its type across the wire: an integer is a JSON integer, a
// double always has a '.' or an exponent (NaN and the infinities as {"d":
// "nan"}), a blob is {"b": base64}, NULL is null, a truth value true / false.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <UltraDatabase/UltraDatabase.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ultradb_relay {

struct Json {
    enum class Type { Null, Boolean, Int, Double, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    int64_t integer = 0;
    double real = 0.0;
    std::string text;
    std::vector<Json> items;                              // Array; an Object's values
    std::vector<std::string> keys;                        // an Object's names, beside its values

    static Json Null() { return Json(); }
    static Json Truth(bool v);
    static Json Int(int64_t v);
    static Json Double(double v);
    static Json String(std::string v);
    static Json Array();
    static Json Object();

    // Object: set or replace a member; Array: append.
    Json& Set(const std::string& key, Json value);
    Json& Push(Json value);

    const Json* Get(const std::string& key) const;
    std::string Str(const std::string& key) const;              // "" when absent or not a string
    int64_t Num(const std::string& key, int64_t fallback = 0) const;
    bool Flag(const std::string& key) const;

    std::string Dump() const;
    static bool Parse(const std::string& text, Json& out, std::string& error);
};

std::string Base64Encode(const std::vector<uint8_t>& bytes);
bool Base64Decode(const std::string& text, std::vector<uint8_t>& out);

Json FromValue(const UltraDbValue& value);
UltraDbValue ToValue(const Json& json);

Json FromParams(const UltraDbParams& params);
UltraDbParams ToParams(const Json* json);

// {"columns": [...], "info": [...], "rows": [[...], ...]}
Json FromResultSet(const UltraDbResultSet& rows);
void ToResultSet(const Json* json, UltraDbResultSet& out);

// The outcome's code, message, SQLSTATE and counts.
Json FromResult(const UltraDbResult& result);
UltraDbResult ToResult(const Json* json);

Json FromStatements(const std::vector<UltraDbStatementResult>& statements);
void ToStatements(const Json* json, std::vector<UltraDbStatementResult>& out);

Json FromObjects(const std::vector<UltraDbObjectSummary>& objects);
void ToObjects(const Json* json, std::vector<UltraDbObjectSummary>& out);

Json FromTable(const UltraDbTableSchema& table);
void ToTable(const Json* json, UltraDbTableSchema& out);

Json FromStrings(const std::vector<std::string>& strings);
std::vector<std::string> ToStrings(const Json* json);

const char* CodeName(UltraDbResultCode code);
UltraDbResultCode CodeFromName(const std::string& name);

} // namespace ultradb_relay
