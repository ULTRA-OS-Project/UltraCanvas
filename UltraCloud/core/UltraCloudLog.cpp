// UltraCloud/core/UltraCloudLog.cpp
// Version: 1.0.0
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework / ULTRA OS
#include <UltraCloud/UltraCloudLog.h>
#include <UltraCloud/UltraCloudWebDav.h>   // WebDavErrorMessage

#include "core/UltraCloudInternal.h"

#include <cctype>
#include <cstddef>
#include <utility>

namespace UltraCloud {

namespace {

thread_local LogCallback t_log;

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Query parameters whose value is never written to the log, by any part of
// their name.
bool NameCarriesSecret(const std::string& name) {
    static const char* const kSecretWords[] = {
        "token", "auth", "key", "secret", "sig", "code", "pass", "session", "credential"};
    const std::string lower = Lower(name);
    for (const char* word : kSecretWords)
        if (lower.find(word) != std::string::npos) return true;
    return false;
}

// Runs of whitespace as one space, trimmed - an error message is shown on one
// line - and at most `limit` bytes, cut on a character boundary.
std::string OneLine(const std::string& text, std::size_t limit = 300) {
    std::string out;
    out.reserve(text.size());
    bool space = false;
    for (char c : text) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            space = !out.empty();
            continue;
        }
        if (space) out.push_back(' ');
        space = false;
        out.push_back(c);
    }
    if (out.size() > limit) {
        std::size_t cut = limit;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
        out = out.substr(0, cut) + "...";
    }
    return out;
}

// "path/not_found/.." -> "path/not_found": Dropbox ends its summaries in a
// run of dots that stands for "and more detail elsewhere".
std::string TrimDropboxSummary(std::string s) {
    while (!s.empty() && (s.back() == '.' || s.back() == '/' || s.back() == ' ')) s.pop_back();
    return s;
}

std::string JsonErrorReason(const internal::JSONValue& v) {
    // Dropbox
    if (v["error_summary"].IsString())
        return TrimDropboxSummary(v["error_summary"].GetString());
    const internal::JSONValue& error = v["error"];
    // OAuth 2 token endpoint (RFC 6749 5.2): "error" is the code itself.
    if (error.IsString()) {
        std::string reason = error.GetString();
        const std::string description = v["error_description"].GetString();
        if (!description.empty()) reason += ": " + description;
        return reason;
    }
    if (error.IsObject()) {
        // Google: a machine reason in errors[0].reason, a human message;
        // Microsoft Graph: a string code and a message.
        std::string code;
        const internal::JSONValue& errors = error["errors"];
        if (errors.IsArray() && errors.GetSize() > 0) code = errors[0]["reason"].GetString();
        if (code.empty() && error["code"].IsString()) code = error["code"].GetString();
        const std::string message = error["message"].GetString();
        if (!code.empty() && !message.empty()) return code + ": " + message;
        return !code.empty() ? code : message;
    }
    return v["message"].GetString();
}

// Days since 1970-01-01 of a proleptic Gregorian date (Howard Hinnant's
// days_from_civil), so an HTTP date needs neither timegm nor a locale.
long long DaysFromCivil(long long y, unsigned m, unsigned d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}

bool ReadNumber(const std::string& s, std::size_t& pos, long long& out) {
    std::size_t start = pos;
    long long v = 0;
    while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) {
        if (v > 100000000000LL) return false;
        v = v * 10 + (s[pos] - '0');
        ++pos;
    }
    out = v;
    return pos > start;
}

} // namespace

LogCallback SetThreadLog(LogCallback sink) {
    LogCallback previous = std::move(t_log);
    t_log = std::move(sink);
    return previous;
}

bool ThreadLogActive() { return static_cast<bool>(t_log); }

void LogToThread(LogKind kind, const std::string& text, int httpStatus) {
    if (!t_log || text.empty()) return;
    LogLine line;
    line.kind = kind;
    line.text = text;
    line.httpStatus = httpStatus;
    t_log(line);
}

std::string LoggableUrl(const std::string& url) {
    std::string out = url;
    // user:password@ out of the authority.
    const std::size_t scheme = out.find("://");
    if (scheme != std::string::npos) {
        const std::size_t hostStart = scheme + 3;
        const std::size_t pathStart = out.find_first_of("/?#", hostStart);
        const std::size_t at = out.rfind('@', pathStart == std::string::npos ? out.size() - 1
                                                                            : pathStart);
        if (at != std::string::npos && at >= hostStart &&
            (pathStart == std::string::npos || at < pathStart))
            out.erase(hostStart, at + 1 - hostStart);
    }
    const std::size_t query = out.find('?');
    if (query == std::string::npos) return out;
    const std::size_t fragment = out.find('#', query);
    const std::string params = out.substr(query + 1, fragment == std::string::npos
                                                         ? std::string::npos
                                                         : fragment - query - 1);
    std::string masked;
    std::size_t start = 0;
    while (start <= params.size()) {
        std::size_t amp = params.find('&', start);
        if (amp == std::string::npos) amp = params.size();
        const std::string pair = params.substr(start, amp - start);
        const std::size_t eq = pair.find('=');
        if (!masked.empty() || start > 0) masked.push_back('&');
        if (eq != std::string::npos && NameCarriesSecret(pair.substr(0, eq)))
            masked += pair.substr(0, eq + 1) + "***";
        else
            masked += pair;
        start = amp + 1;
    }
    return out.substr(0, query + 1) + masked +
           (fragment == std::string::npos ? std::string() : out.substr(fragment));
}

std::string ServerErrorReason(const std::string& body) {
    const std::size_t first = body.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    if (body[first] == '{') {
        const internal::JSONValue v = internal::ParseJson(body);
        if (v.IsObject()) return OneLine(JsonErrorReason(v));
        return "";
    }
    if (body[first] == '<') return OneLine(WebDavErrorMessage(body));
    // A short plain-text answer ("Unauthorized") is the reason as it stands;
    // a long one is a page nobody wants on one line.
    if (body.size() - first > 200) return "";
    return OneLine(body);
}

long long RetryAfterSeconds(const std::string& value, long long nowEpoch) {
    std::size_t pos = value.find_first_not_of(" \t");
    if (pos == std::string::npos) return -1;
    if (std::isdigit(static_cast<unsigned char>(value[pos]))) {
        long long seconds = 0;
        return ReadNumber(value, pos, seconds) ? seconds : -1;
    }
    // IMF-fixdate, RFC 9110 5.6.7: "Sun, 06 Nov 1994 08:49:37 GMT".
    const std::size_t comma = value.find(',', pos);
    if (comma == std::string::npos) return -1;
    pos = comma + 1;
    while (pos < value.size() && value[pos] == ' ') ++pos;
    long long day = 0, year = 0, hour = 0, minute = 0, second = 0;
    if (!ReadNumber(value, pos, day)) return -1;
    while (pos < value.size() && value[pos] == ' ') ++pos;
    static const char* const kMonths[] = {"jan", "feb", "mar", "apr", "may", "jun",
                                          "jul", "aug", "sep", "oct", "nov", "dec"};
    const std::string month = Lower(value.substr(pos, 3));
    unsigned monthNumber = 0;
    for (unsigned i = 0; i < 12; ++i)
        if (month == kMonths[i]) monthNumber = i + 1;
    if (monthNumber == 0) return -1;
    pos += 3;
    while (pos < value.size() && value[pos] == ' ') ++pos;
    if (!ReadNumber(value, pos, year)) return -1;
    while (pos < value.size() && value[pos] == ' ') ++pos;
    if (!ReadNumber(value, pos, hour) || pos >= value.size() || value[pos++] != ':' ||
        !ReadNumber(value, pos, minute) || pos >= value.size() || value[pos++] != ':' ||
        !ReadNumber(value, pos, second))
        return -1;
    const long long at = DaysFromCivil(year, monthNumber, static_cast<unsigned>(day)) * 86400 +
                         hour * 3600 + minute * 60 + second;
    return at > nowEpoch ? at - nowEpoch : 0;
}

} // namespace UltraCloud
