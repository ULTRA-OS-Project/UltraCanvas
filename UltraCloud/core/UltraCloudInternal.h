// UltraCloud/core/UltraCloudInternal.h
// Small helpers shared by the module's own sources (not public API): JSON in
// and out, RFC 3339 dates, query-string building, and what an upload tells
// CloudService about itself (UploadReport).
// Version: 0.5.0 - LocalFileDates / GiveFileDates, ParseRfc3339 / ParseHttpDate:
//                  a transfer keeps the file's dates
// Version: 0.4.0 - UploadReport
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <DataFormats/UltraCanvasJSON.h>
#include <UltraNet/UltraNetUrl.h>

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"
#include "../../UltraCanvas/include/UltraCanvasFileTimes.h"   // a file's dates across a transfer

namespace UltraCloud {
namespace internal {

using UltraCanvas::JSONValue;

// What a provider whose uploads land as they arrive (partialUploadsRemain)
// reports about the upload it is running, to the CloudService call that set
// it on the thread (SetThreadUploadReport). The service needs to know when
// the server has created the file at its name - from then on what is there
// is the upload's, and is removed if it does not finish - and whether the
// server refused to create it, which leaves the name as it was.
struct UploadReport {
    // Going straight to the file's name: an SFTP server says nothing when it
    // creates the file, so the provider looks in the folder first, and a
    // name that is free there counts as created by this upload.
    bool checkFirst = false;
    // The provider can tell when the server creates the file (FTP: the reply
    // to STOR); `opened` false then means nothing was created at all.
    bool watched = false;
    bool opened = false;      // the server created (or emptied) the file
    bool refused = false;     // the server refused to create it; nothing written
    // Called once, on the uploading thread, when `opened` becomes true.
    std::function<void()> onOpened;
};
inline thread_local UploadReport* t_uploadReport = nullptr;
// Sets the calling thread's report and answers the one it replaces.
inline UploadReport* SetThreadUploadReport(UploadReport* report) {
    UploadReport* previous = t_uploadReport;
    t_uploadReport = report;
    return previous;
}
inline UploadReport* ThreadUploadReport() { return t_uploadReport; }

inline JSONValue ParseJson(const std::string& text) {
    UltraCanvas::JSONParseResult pr;
    return UltraCanvas::JSON::Parse(text, &pr);
}

inline std::string ToJson(const JSONValue& value) {
    return UltraCanvas::JSON::Serialize(value);
}

inline std::vector<uint8_t> Bytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

// "2026-09-04T10:00:00Z" for an epoch second (UTC); empty for <= 0.
inline std::string Rfc3339(int64_t epoch) {
    if (epoch <= 0) return "";
    std::time_t t = static_cast<std::time_t>(epoch);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

// ---- A file's dates across a transfer ---------------------------------------
// An upload sends the local file's dates where the service keeps them, and a
// download gives the local copy the server's - so a copy to or from a drive
// keeps the date the file was made rather than the moment it travelled. In
// whole seconds, UTC; 0 is "not known" throughout.

// Days since 1970-01-01 of a UTC calendar date (Howard Hinnant's algorithm).
inline int64_t DaysFromCivil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

inline int64_t EpochFromUtcParts(int year, int month, int day, int hour, int minute, int second) {
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || second < 0 || second > 60)
        return 0;
    return DaysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400 +
           hour * 3600 + minute * 60 + second;
}

// "2026-09-04T10:00:00Z", with or without a fraction ("...00.123Z") or an
// offset instead of the Z ("...00+02:00"); 0 when it is not one.
inline int64_t ParseRfc3339(const std::string& text) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    if (std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &se) != 6) return 0;
    int64_t t = EpochFromUtcParts(y, mo, d, h, mi, se);
    if (!t) return 0;
    std::string::size_type at = 19;
    if (at < text.size() && text[at] == '.')
        while (++at < text.size() && text[at] >= '0' && text[at] <= '9') {}
    if (at < text.size() && (text[at] == '+' || text[at] == '-')) {
        int oh = 0, om = 0;
        if (std::sscanf(text.c_str() + at + 1, "%2d:%2d", &oh, &om) == 2)
            t -= (text[at] == '+' ? 1 : -1) * static_cast<int64_t>(oh * 3600 + om * 60);
    }
    return t;
}

// "Wed, 03 Sep 2026 10:00:00 GMT" (RFC 1123, HTTP's Last-Modified); 0 when not.
inline int64_t ParseHttpDate(const std::string& text) {
    const std::string::size_type comma = text.find(", ");
    const std::string rest = comma == std::string::npos ? text : text.substr(comma + 2);
    int d = 0, y = 0, h = 0, mi = 0, se = 0;
    char month[8] = {0};
    if (std::sscanf(rest.c_str(), "%d %7s %d %d:%d:%d", &d, month, &y, &h, &mi, &se) != 6) return 0;
    static const char* kMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    for (int m = 0; m < 12; ++m)
        if (std::string(month) == kMonths[m]) return EpochFromUtcParts(y, m + 1, d, h, mi, se);
    return 0;
}

// The local file's modification and creation dates (creation 0 where the
// system does not keep one).
inline void LocalFileDates(const std::string& localPath, int64_t& modified, int64_t& created) {
    UltraCanvas::FileTimes times;
    UltraCanvas::GetFileTimes(localPath, times);
    modified = UltraCanvas::FileTimes::ToSeconds(times.modified);
    created = UltraCanvas::FileTimes::ToSeconds(times.created);
}

// Gives a downloaded file the server's dates; a 0 leaves that one as it is.
// Best effort - a file that would not take a date is still the file.
inline void GiveFileDates(const std::string& localPath, int64_t modified, int64_t created) {
    UltraCanvas::FileTimes times;
    times.modified = modified > 0 ? UltraCanvas::FileTimes::FromSeconds(modified) : 0;
    times.created = created > 0 ? UltraCanvas::FileTimes::FromSeconds(created) : 0;
    UltraCanvas::SetFileTimes(localPath, times);
}

// "?a=1&b=x%20y" from key/value pairs (values percent-encoded).
inline std::string Query(const std::vector<std::pair<std::string, std::string>>& params) {
    std::string q;
    for (const auto& [k, v] : params) {
        q += q.empty() ? "?" : "&";
        q += k + "=" + UltraNet_UrlEncode(v);
    }
    return q;
}

// ---- Chunked file reading (large uploads never load the whole file) ----------
// Size of a local file, -1 when it cannot be read.
inline int64_t FileSize(const std::string& path) {
    std::error_code ec;
    auto size = std::filesystem::file_size(UltraCanvas::PathFromUtf8(path), ec);
    return ec ? -1 : static_cast<int64_t>(size);
}
// Bytes [offset, offset + size) of the file into `out` (short at the end).
inline bool ReadChunk(std::ifstream& is, int64_t offset, int64_t size, std::vector<uint8_t>& out) {
    out.assign(static_cast<std::size_t>(size), 0);
    is.clear();
    is.seekg(offset, std::ios::beg);
    is.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
    const auto got = is.gcount();
    if (got < 0) return false;
    out.resize(static_cast<std::size_t>(got));
    return true;
}
// "bytes s-e/total" for a chunk.
inline std::string ContentRange(int64_t offset, int64_t length, int64_t total) {
    return "bytes " + std::to_string(offset) + "-" + std::to_string(offset + length - 1) + "/"
         + std::to_string(total);
}

// The last path segment ("report.pdf" of "/Docs/report.pdf") and its parent.
inline std::string Leaf(const std::string& path) {
    auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
inline std::string Parent(const std::string& path) {
    auto slash = path.find_last_of('/');
    return (slash == std::string::npos || slash == 0) ? "/" : path.substr(0, slash);
}

} // namespace internal
} // namespace UltraCloud
