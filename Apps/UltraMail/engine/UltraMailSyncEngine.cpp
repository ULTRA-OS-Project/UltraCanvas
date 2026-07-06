// Apps/UltraMail/engine/UltraMailSyncEngine.cpp
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailSyncEngine.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace UltraMail {

namespace {

std::string Trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '"')) ++a;
    while (b > a && (s[b-1] == ' ' || s[b-1] == '\t' || s[b-1] == '"')) --b;
    return s.substr(a, b - a);
}

// "Anna Schmidt <anna@x.com>" -> name "Anna Schmidt", addr "anna@x.com".
void ParseFromField(const std::string& from, std::string& name, std::string& addr) {
    std::size_t lt = from.find('<');
    if (lt != std::string::npos) {
        std::size_t gt = from.find('>', lt);
        addr = from.substr(lt + 1, gt == std::string::npos ? std::string::npos : gt - lt - 1);
        name = Trim(from.substr(0, lt));
    } else {
        addr = Trim(from);
        name.clear();
    }
    addr = Trim(addr);
}

uint32_t MapNetFlagsToLocal(UltraNetMailFlags nf) {
    uint32_t f = Flag_None;
    if (UltraNetHasFlag(nf, UltraNetMailFlags::Seen))     f |= Flag_Seen;
    if (UltraNetHasFlag(nf, UltraNetMailFlags::Answered)) f |= Flag_Answered;
    if (UltraNetHasFlag(nf, UltraNetMailFlags::Flagged))  f |= Flag_Flagged;
    if (UltraNetHasFlag(nf, UltraNetMailFlags::Deleted))  f |= Flag_Deleted;
    if (UltraNetHasFlag(nf, UltraNetMailFlags::Draft))    f |= Flag_Draft;
    return f;
}

UltraNetMailFlags MapLocalFlagToNet(uint32_t lf) {
    UltraNetMailFlags nf = UltraNetMailFlags::None;
    if (lf & Flag_Seen)     nf |= UltraNetMailFlags::Seen;
    if (lf & Flag_Answered) nf |= UltraNetMailFlags::Answered;
    if (lf & Flag_Flagged)  nf |= UltraNetMailFlags::Flagged;
    if (lf & Flag_Deleted)  nf |= UltraNetMailFlags::Deleted;
    if (lf & Flag_Draft)    nf |= UltraNetMailFlags::Draft;
    return nf;
}

int MonthNum(const char* m) {
    static const char* names[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                  "Jul","Aug","Sep","Oct","Nov","Dec"};
    for (int i = 0; i < 12; ++i) if (std::strncmp(m, names[i], 3) == 0) return i + 1;
    return 0;
}

// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm).
int64_t DaysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

// Parse an RFC 2822 Date header to epoch seconds (0 on failure). Portable —
// avoids strptime/timegm.
int64_t ParseRfc2822Date(const std::string& s) {
    if (s.empty()) return 0;
    int day = 0, year = 0, h = 0, mi = 0, se = 0;
    char mon[8] = {0}, tz[8] = {0};

    // With leading weekday ("Tue, 14 Jan 2026 14:02:00 +0100").
    int n = std::sscanf(s.c_str(), "%*[^,], %d %3s %d %d:%d:%d %7s",
                        &day, mon, &year, &h, &mi, &se, tz);
    if (n < 6)
        n = std::sscanf(s.c_str(), "%d %3s %d %d:%d:%d %7s",
                        &day, mon, &year, &h, &mi, &se, tz);
    if (n < 6) return 0;

    int month = MonthNum(mon);
    if (month == 0 || year < 1970) return 0;

    int64_t epoch = DaysFromCivil(year, static_cast<unsigned>(month),
                                  static_cast<unsigned>(day)) * 86400
                    + h * 3600 + mi * 60 + se;

    // Apply the timezone offset to reach UTC.
    if (tz[0] == '+' || tz[0] == '-') {
        int sign = tz[0] == '+' ? 1 : -1;
        int hh = (tz[1] - '0') * 10 + (tz[2] - '0');
        int mm = (tz[3] - '0') * 10 + (tz[4] - '0');
        epoch -= sign * (hh * 3600 + mm * 60);
    }
    return epoch;
}

std::string SanitizeFolder(const std::string& folder) {
    std::string out;
    for (char c : folder)
        out.push_back((c == '/' || c == '\\' || c == ':') ? '_' : c);
    if (out.empty()) out = "INBOX";
    return out;
}

} // namespace

std::string SyncEngine::BodyPath(const std::string& accountId,
                                 const std::string& folder, int64_t uid) const {
    fs::path p = fs::path(emlDir_) / accountId / SanitizeFolder(folder)
               / (std::to_string(uid) + ".eml");
    return p.string();
}

SyncOutcome SyncEngine::SyncFolders(const std::string& accountId,
                                    const std::string& serverUrl,
                                    const UltraNetMailOptions& options) {
    std::vector<UltraNetMailFolder> folders;
    UltraNetResult r = mailbox_.ListFolders(serverUrl, folders, options);
    if (!r) return SyncOutcome::Fail(r.message);

    SyncOutcome out;
    for (const auto& f : folders) {
        Folder lf;
        lf.accountId = accountId;
        lf.name = f.name;
        lf.role = FolderRoleFromString(f.role);
        if (store_.UpsertFolder(lf)) out.stats.folders++;
    }
    return out;
}

SyncOutcome SyncEngine::SyncMessages(const std::string& accountId,
                                     const std::string& folder,
                                     const std::string& serverUrl,
                                     const UltraNetMailOptions& options,
                                     bool fetchBodies) {
    int64_t sinceUid = 0;
    store_.GetMaxUid(accountId, folder, sinceUid);

    std::vector<UltraNetMailEnvelope> envelopes;
    UltraNetResult r = mailbox_.FetchEnvelopes(
        serverUrl, folder, static_cast<uint32_t>(sinceUid), envelopes, options);
    if (!r) return SyncOutcome::Fail(r.message);

    SyncOutcome out;
    for (const auto& e : envelopes) {
        MessageEnvelope m;
        m.accountId = accountId;
        m.folder    = folder;
        m.uid       = static_cast<int64_t>(e.uid);
        m.messageId = e.messageId;
        m.inReplyTo = e.inReplyTo;
        m.subject   = e.subject;
        ParseFromField(e.from, m.fromName, m.fromAddr);
        m.to    = e.to;
        m.date  = ParseRfc2822Date(e.date);
        m.flags = MapNetFlagsToLocal(e.flags);
        // Automated/bulk detection needs List-*/Precedence headers, which the
        // envelope fetch does not carry yet; left false for now.
        m.automated = false;

        if (store_.UpsertMessage(m)) out.stats.messages++;

        if (fetchBodies) {
            std::string p = FetchBody(accountId, folder, m.uid, serverUrl, options);
            if (!p.empty()) out.stats.bodies++;
        }
    }
    return out;
}

std::string SyncEngine::FetchBody(const std::string& accountId, const std::string& folder,
                                  int64_t uid, const std::string& serverUrl,
                                  const UltraNetMailOptions& options) {
    std::string raw;
    UltraNetResult r = mailbox_.FetchMessage(serverUrl, folder,
                                             static_cast<uint32_t>(uid), raw, options);
    if (!r || raw.empty()) return std::string();

    const std::string path = BodyPath(accountId, folder, uid);
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream os(path, std::ios::binary | std::ios::trunc);
    if (!os) return std::string();
    os.write(raw.data(), static_cast<std::streamsize>(raw.size()));
    if (!os) return std::string();
    return path;
}

SyncOutcome SyncEngine::SetFlag(const std::string& accountId, const std::string& folder,
                                int64_t uid, uint32_t ultramailFlag, bool set,
                                const std::string& serverUrl,
                                const UltraNetMailOptions& options) {
    UltraNetResult r = mailbox_.StoreFlags(
        serverUrl, folder, static_cast<uint32_t>(uid),
        MapLocalFlagToNet(ultramailFlag), set, options);
    if (!r) return SyncOutcome::Fail(r.message);

    UltraDbResult lr = store_.SetFlags(accountId, folder, uid, ultramailFlag, set);
    if (!lr) return SyncOutcome::Fail(lr.message);
    return SyncOutcome{};
}

} // namespace UltraMail
