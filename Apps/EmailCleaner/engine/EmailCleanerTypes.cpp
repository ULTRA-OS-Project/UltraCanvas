// Apps/EmailCleaner/engine/EmailCleanerTypes.cpp
// Enum conversions, address/date parsing and the UTC calendar arithmetic the
// timetable and timeline bucketing are built on.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "EmailCleanerTypes.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace EmailCleaner {

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

const char* kMonthNames[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

// Days since 1970-01-01 from a civil date (Howard Hinnant's algorithm).
int64_t DaysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);              // [0, 399]
    const unsigned doy = static_cast<unsigned>((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;             // [0, 146096]
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

// Inverse of DaysFromCivil.
void CivilFromDays(int64_t z, int& y, unsigned& m, unsigned& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);           // [0, 146096]
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t yy = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp + (mp < 10 ? 3 : -9);
    y = static_cast<int>(yy + (m <= 2));
}

// Floor division, so negative epochs (pre-1970 dates) bucket correctly.
int64_t FloorDiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

} // namespace

// ---- Categories ------------------------------------------------------------

std::string ToString(MessageCategory category) {
    switch (category) {
        case MessageCategory::Personal:      return "personal";
        case MessageCategory::Newsletter:    return "newsletter";
        case MessageCategory::Notification:  return "notification";
        case MessageCategory::ProductSpam:   return "product-spam";
        case MessageCategory::AdultContent:  return "adult";
        case MessageCategory::DatingScam:    return "dating-scam";
        case MessageCategory::PhishingScam:  return "phishing";
        case MessageCategory::FinancialScam: return "financial-scam";
        case MessageCategory::MalwareRisk:   return "malware-risk";
        case MessageCategory::Unclassified:  break;
    }
    return "unclassified";
}

MessageCategory CategoryFromString(const std::string& s) {
    const std::string k = Lower(Trim(s));
    if (k == "personal")       return MessageCategory::Personal;
    if (k == "newsletter")     return MessageCategory::Newsletter;
    if (k == "notification")   return MessageCategory::Notification;
    if (k == "product-spam" || k == "productspam" || k == "spam")
        return MessageCategory::ProductSpam;
    if (k == "adult" || k == "adult-content" || k == "sex")
        return MessageCategory::AdultContent;
    if (k == "dating-scam" || k == "dating" || k == "romance-scam")
        return MessageCategory::DatingScam;
    if (k == "phishing" || k == "phishing-scam")
        return MessageCategory::PhishingScam;
    if (k == "financial-scam" || k == "financial" || k == "fraud")
        return MessageCategory::FinancialScam;
    if (k == "malware-risk" || k == "malware")
        return MessageCategory::MalwareRisk;
    return MessageCategory::Unclassified;
}

std::string CategoryLabel(MessageCategory category) {
    switch (category) {
        case MessageCategory::Personal:      return "Personal";
        case MessageCategory::Newsletter:    return "Newsletter";
        case MessageCategory::Notification:  return "Notification";
        case MessageCategory::ProductSpam:   return "Product spam";
        case MessageCategory::AdultContent:  return "Adult content";
        case MessageCategory::DatingScam:    return "Dating scam";
        case MessageCategory::PhishingScam:  return "Phishing";
        case MessageCategory::FinancialScam: return "Financial scam";
        case MessageCategory::MalwareRisk:   return "Malware risk";
        case MessageCategory::Unclassified:  break;
    }
    return "Unclassified";
}

bool IsUnwanted(MessageCategory category) {
    switch (category) {
        case MessageCategory::ProductSpam:
        case MessageCategory::AdultContent:
        case MessageCategory::DatingScam:
        case MessageCategory::PhishingScam:
        case MessageCategory::FinancialScam:
        case MessageCategory::MalwareRisk:
            return true;
        default:
            return false;
    }
}

const std::vector<MessageCategory>& AllCategories() {
    static const std::vector<MessageCategory> all = {
        MessageCategory::Unclassified, MessageCategory::Personal,
        MessageCategory::Newsletter,   MessageCategory::Notification,
        MessageCategory::ProductSpam,  MessageCategory::AdultContent,
        MessageCategory::DatingScam,   MessageCategory::PhishingScam,
        MessageCategory::FinancialScam, MessageCategory::MalwareRisk
    };
    return all;
}

// ---- Match fields ----------------------------------------------------------

std::string ToString(MatchField field) {
    switch (field) {
        case MatchField::Subject:    return "subject";
        case MatchField::Body:       return "body";
        case MatchField::Sender:     return "sender";
        case MatchField::Attachment: return "attachment";
        case MatchField::Any:        break;
    }
    return "any";
}

MatchField MatchFieldFromString(const std::string& s) {
    const std::string k = Lower(Trim(s));
    if (k == "subject")    return MatchField::Subject;
    if (k == "body")       return MatchField::Body;
    if (k == "sender" || k == "from") return MatchField::Sender;
    if (k == "attachment" || k == "attachments") return MatchField::Attachment;
    return MatchField::Any;
}

// ---- Metrics ---------------------------------------------------------------

std::string ToString(SenderMetric metric) {
    switch (metric) {
        case SenderMetric::TotalBytes:      return "total-bytes";
        case SenderMetric::AttachmentBytes: return "attachment-bytes";
        case SenderMetric::UnwantedCount:   return "unwanted-count";
        case SenderMetric::MessageCount:    break;
    }
    return "message-count";
}

double MetricValue(const SenderBlock& block, SenderMetric metric) {
    switch (metric) {
        case SenderMetric::TotalBytes:      return static_cast<double>(block.totalBytes);
        case SenderMetric::AttachmentBytes: return static_cast<double>(block.attachmentBytes);
        case SenderMetric::UnwantedCount:   return static_cast<double>(block.unwantedCount);
        case SenderMetric::MessageCount:    break;
    }
    return static_cast<double>(block.messageCount);
}

std::string ToString(TimeBucket bucket) {
    switch (bucket) {
        case TimeBucket::Week:  return "week";
        case TimeBucket::Month: return "month";
        case TimeBucket::Year:  return "year";
        case TimeBucket::Day:   break;
    }
    return "day";
}

// ---- Timetable -------------------------------------------------------------

int Timetable::At(int day, int hour) const {
    if (day < 0 || day >= Days || hour < 0 || hour >= Hours) return 0;
    return counts[static_cast<size_t>(day) * Hours + hour];
}

void Timetable::Add(int day, int hour, int count) {
    if (day < 0 || day >= Days || hour < 0 || hour >= Hours || count <= 0) return;
    int& cell = counts[static_cast<size_t>(day) * Hours + hour];
    cell += count;
    total += count;
    if (cell > peakCount) {
        peakCount = cell;
        peakDay   = day;
        peakHour  = hour;
    }
}

void Timetable::Recompute() {
    total = 0;
    peakCount = 0;
    peakDay = -1;
    peakHour = -1;
    for (int d = 0; d < Days; ++d) {
        for (int h = 0; h < Hours; ++h) {
            const int c = counts[static_cast<size_t>(d) * Hours + h];
            total += c;
            if (c > peakCount) {
                peakCount = c;
                peakDay   = d;
                peakHour  = h;
            }
        }
    }
}

std::string WeekdayName(int day, bool shortForm) {
    static const char* shortNames[7] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
    static const char* longNames[7]  = { "Monday", "Tuesday", "Wednesday", "Thursday",
                                         "Friday", "Saturday", "Sunday" };
    if (day < 0 || day > 6) return "";
    return shortForm ? shortNames[day] : longNames[day];
}

// ---- Addresses -------------------------------------------------------------

void ParseAddress(const std::string& headerValue,
                  std::string& outName, std::string& outAddress) {
    outName.clear();
    outAddress.clear();

    const std::string v = Trim(headerValue);
    if (v.empty()) return;

    const size_t lt = v.find('<');
    const size_t gt = (lt == std::string::npos) ? std::string::npos : v.find('>', lt + 1);
    if (lt != std::string::npos && gt != std::string::npos) {
        outAddress = Lower(Trim(v.substr(lt + 1, gt - lt - 1)));
        outName    = Trim(v.substr(0, lt));
    } else {
        // A bare address, possibly followed by a parenthesised comment:
        //   erika@example.com (Erika Example)
        const size_t open = v.find('(');
        if (open != std::string::npos) {
            const size_t close = v.find(')', open + 1);
            if (close != std::string::npos)
                outName = Trim(v.substr(open + 1, close - open - 1));
            outAddress = Lower(Trim(v.substr(0, open)));
        } else {
            outAddress = Lower(v);
        }
    }

    // Strip surrounding quotes from the display name.
    if (outName.size() >= 2 && outName.front() == '"' && outName.back() == '"')
        outName = Trim(outName.substr(1, outName.size() - 2));

    // An address with no '@' is not one; treat the whole value as a name.
    if (outAddress.find('@') == std::string::npos && outName.empty() && !outAddress.empty()) {
        outName = Trim(headerValue);
        outAddress.clear();
    }
}

std::string DomainOf(const std::string& address) {
    const size_t at = address.rfind('@');
    if (at == std::string::npos || at + 1 >= address.size()) return "";
    return Lower(address.substr(at + 1));
}

std::string LocalPartOf(const std::string& address) {
    const size_t at = address.rfind('@');
    if (at == std::string::npos) return Lower(address);
    return Lower(address.substr(0, at));
}

// ---- Dates -----------------------------------------------------------------

namespace {

// Alphabetic zones RFC 5322 keeps for backward compatibility, in minutes.
bool AlphabeticZoneOffset(const std::string& zone, int& outMinutes) {
    struct Entry { const char* name; int minutes; };
    static const Entry kZones[] = {
        { "ut", 0 },   { "gmt", 0 },  { "z", 0 },
        { "est", -5 * 60 }, { "edt", -4 * 60 },
        { "cst", -6 * 60 }, { "cdt", -5 * 60 },
        { "mst", -7 * 60 }, { "mdt", -6 * 60 },
        { "pst", -8 * 60 }, { "pdt", -7 * 60 },
    };
    const std::string k = Lower(zone);
    for (const Entry& e : kZones) {
        if (k == e.name) { outMinutes = e.minutes; return true; }
    }
    // Single-letter military zones are defined as +0000 by RFC 5322 §4.3.
    if (k.size() == 1 && std::isalpha(static_cast<unsigned char>(k[0]))) {
        outMinutes = 0;
        return true;
    }
    return false;
}

} // namespace

bool ParseRfc5322Date(const std::string& value, int64_t& outEpoch) {
    // Tokenise on whitespace, commas and the time separators, keeping the
    // structure: [Day,] DD Mon YYYY HH:MM[:SS] [zone]
    std::vector<std::string> tokens;
    std::string current;
    for (char c : value) {
        if (std::isspace(static_cast<unsigned char>(c)) || c == ',') {
            if (!current.empty()) { tokens.push_back(current); current.clear(); }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) tokens.push_back(current);

    // Drop a leading day-of-week token ("Mon", "Monday").
    size_t i = 0;
    if (i < tokens.size() && !std::isdigit(static_cast<unsigned char>(tokens[i][0])))
        ++i;

    if (i + 3 >= tokens.size() + 1) return false;   // need day, month, year at least
    if (i + 2 >= tokens.size()) return false;

    const int day = std::atoi(tokens[i].c_str());
    if (day < 1 || day > 31) return false;
    ++i;

    int month = 0;
    const std::string monthTok = Lower(tokens[i]).substr(0, 3);
    for (int m = 0; m < 12; ++m) {
        if (monthTok == Lower(kMonthNames[m])) { month = m + 1; break; }
    }
    if (month == 0) return false;
    ++i;

    if (i >= tokens.size()) return false;
    int year = std::atoi(tokens[i].c_str());
    if (year < 100) year += (year < 50) ? 2000 : 1900;   // obsolete 2-digit years
    if (year < 1900 || year > 9999) return false;
    ++i;

    int hour = 0, minute = 0, second = 0;
    if (i < tokens.size() && tokens[i].find(':') != std::string::npos) {
        std::sscanf(tokens[i].c_str(), "%d:%d:%d", &hour, &minute, &second);
        ++i;
    }
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60)
        return false;

    int zoneMinutes = 0;
    if (i < tokens.size()) {
        const std::string& zone = tokens[i];
        if ((zone[0] == '+' || zone[0] == '-') && zone.size() >= 5) {
            const int sign = (zone[0] == '-') ? -1 : 1;
            const int hh = std::atoi(zone.substr(1, 2).c_str());
            const int mm = std::atoi(zone.substr(3, 2).c_str());
            zoneMinutes = sign * (hh * 60 + mm);
        } else {
            AlphabeticZoneOffset(zone, zoneMinutes);
        }
    }

    outEpoch = MakeUtcTime(year, month, day, hour, minute, second)
             - static_cast<int64_t>(zoneMinutes) * 60;
    return true;
}

int64_t MakeUtcTime(int year, int month, int day, int hour, int minute, int second) {
    return DaysFromCivil(year, month, day) * 86400
         + static_cast<int64_t>(hour) * 3600
         + static_cast<int64_t>(minute) * 60
         + second;
}

UtcParts BreakUtcTime(int64_t epochSeconds) {
    UtcParts p;
    const int64_t days = FloorDiv(epochSeconds, 86400);
    int64_t rem = epochSeconds - days * 86400;

    int y = 1970;
    unsigned m = 1, d = 1;
    CivilFromDays(days, y, m, d);
    p.year  = y;
    p.month = static_cast<int>(m);
    p.day   = static_cast<int>(d);
    p.hour   = static_cast<int>(rem / 3600); rem %= 3600;
    p.minute = static_cast<int>(rem / 60);
    p.second = static_cast<int>(rem % 60);

    // 1970-01-01 was a Thursday, i.e. index 3 with Monday == 0.
    int64_t wd = (days + 3) % 7;
    if (wd < 0) wd += 7;
    p.weekday = static_cast<int>(wd);
    return p;
}

int64_t BucketStart(int64_t epochSeconds, TimeBucket bucket) {
    const UtcParts p = BreakUtcTime(epochSeconds);
    switch (bucket) {
        case TimeBucket::Day:
            return MakeUtcTime(p.year, p.month, p.day, 0, 0, 0);
        case TimeBucket::Week: {
            const int64_t dayStart = MakeUtcTime(p.year, p.month, p.day, 0, 0, 0);
            return dayStart - static_cast<int64_t>(p.weekday) * 86400;
        }
        case TimeBucket::Month:
            return MakeUtcTime(p.year, p.month, 1, 0, 0, 0);
        case TimeBucket::Year:
            return MakeUtcTime(p.year, 1, 1, 0, 0, 0);
    }
    return epochSeconds;
}

std::string BucketLabel(int64_t bucketStart, TimeBucket bucket) {
    const UtcParts p = BreakUtcTime(bucketStart);
    char buf[64];
    switch (bucket) {
        case TimeBucket::Day:
            std::snprintf(buf, sizeof(buf), "%d %s %d", p.day, kMonthNames[p.month - 1], p.year);
            break;
        case TimeBucket::Week:
            std::snprintf(buf, sizeof(buf), "%d %s", p.day, kMonthNames[p.month - 1]);
            break;
        case TimeBucket::Month:
            std::snprintf(buf, sizeof(buf), "%s %d", kMonthNames[p.month - 1], p.year);
            break;
        case TimeBucket::Year:
            std::snprintf(buf, sizeof(buf), "%d", p.year);
            break;
    }
    return buf;
}

std::string FormatBytes(int64_t bytes) {
    static const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    double v = static_cast<double>(bytes);
    int unit = 0;
    while (v >= 1024.0 && unit < 4) { v /= 1024.0; ++unit; }
    char buf[48];
    if (unit == 0)
        std::snprintf(buf, sizeof(buf), "%lld B", static_cast<long long>(bytes));
    else
        std::snprintf(buf, sizeof(buf), "%.1f %s", v, units[unit]);
    return buf;
}

std::string FormatDate(int64_t epochSeconds) {
    if (epochSeconds <= 0) return "-";
    const UtcParts p = BreakUtcTime(epochSeconds);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d %s %d", p.day, kMonthNames[p.month - 1], p.year);
    return buf;
}

} // namespace EmailCleaner
