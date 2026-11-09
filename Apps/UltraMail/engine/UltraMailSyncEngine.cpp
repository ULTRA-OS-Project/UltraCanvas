// Apps/UltraMail/engine/UltraMailSyncEngine.cpp
// Version: 0.7.0 - collision-free body directories: a folder whose name is not
//                  in title case ("work", "WORK") is cached under its name and
//                  a ~<case mask> suffix (CacheDirectoryOf), so case twins no
//                  longer share - and overwrite - one directory on Windows and
//                  macOS; MigrateBodyCache moves an earlier version's
//                  directories once
// Version: 0.6.1 - SyncFolders stores the server's inbox as INBOX whatever it is
//                  listed as ("Inbox", a JMAP mailbox of role inbox): the mail is
//                  synced under INBOX, and the account bar counts the folder
//                  with the inbox role - which was a second, empty folder
// Version: 0.6.0 - message files read and written through UltraMailStorage (sealed at rest)
// Version: 0.5.1 - the envelope date parses an ISO 8601 UTCDate (JMAP's
//                  receivedAt) as well as an RFC 2822 Date header (IMAP)
// Version: 0.5.0 - CreateFolder / DeleteFolder
// Version: 0.4.0 - the verified sender domain is stored with the verdict;
//                  RescanStaleVerdicts after each sync of a folder
// Version: 0.3.0 - SyncFolders keeps the server's separator and drops the folders
//                  the server no longer lists
// Version: 0.2.0 - RefreshFolder / FetchMissing; the UIDNEXT check on the cache
// Version: 0.1.1 - envelope subject/from/to are RFC 2047 decoded when stored
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailSyncEngine.h"
#include "UltraMailStorage.h"
#include "UltraMailMimeCodec.h"

#include "UltraMailThreatScan.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <vector>
#include <UltraCanvasUtils.h>
#include <UltraNet/UltraNetMime.h>
#include "../../../UltraCanvas/include/UltraCanvasPathUtf8.h"

using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;
namespace fs = std::filesystem;

namespace UltraMail {

namespace {

// "Anna Schmidt <anna@x.com>" -> name "Anna Schmidt", addr "anna@x.com".
void ParseFromField(const std::string& from, std::string& name, std::string& addr) {
    // Strip surrounding whitespace and double-quotes from the display name and
    // address (e.g. "Schmidt, Anna" <a@x> -> name: Schmidt, Anna).
    const std::string kStrip = " \t\r\n\"";
    std::size_t lt = from.find('<');
    if (lt != std::string::npos) {
        std::size_t gt = from.find('>', lt);
        addr = from.substr(lt + 1, gt == std::string::npos ? std::string::npos : gt - lt - 1);
        name = UltraCanvas::Trim(from.substr(0, lt), kStrip);
    } else {
        addr = UltraCanvas::Trim(from, kStrip);
        name.clear();
    }
    addr = UltraCanvas::Trim(addr, kStrip);
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
    UltraNetMailFlags nf = UltraNetMailFlags::NoFlags;
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

// Parse an ISO 8601 / RFC 3339 timestamp to epoch seconds (0 on failure). JMAP
// delivers a message's time as a UTCDate ("2026-07-17T09:00:00Z"), not an RFC
// 2822 Date header, so the envelope date field holds this shape for a JMAP
// account. Accepts a 'T' or ' ' between date and time, an optional fractional
// second, and a 'Z' / ±hh:mm zone (absent is read as UTC, as JMAP always is).
int64_t ParseIso8601Date(const std::string& s) {
    int Y = 0, Mo = 0, D = 0, h = 0, mi = 0, se = 0;
    int n = std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &Y, &Mo, &D, &h, &mi, &se);
    if (n < 6)
        n = std::sscanf(s.c_str(), "%d-%d-%d %d:%d:%d", &Y, &Mo, &D, &h, &mi, &se);
    if (n < 6 || Mo < 1 || Mo > 12 || Y < 1970) return 0;

    int64_t epoch = DaysFromCivil(Y, static_cast<unsigned>(Mo),
                                  static_cast<unsigned>(D)) * 86400
                    + h * 3600 + mi * 60 + se;

    // The zone marker sits after the seconds (past any ".fff"); search skips the
    // date's own two '-'. 'Z'/none = UTC; "+hh:mm" / "-hh:mm" is subtracted.
    std::size_t z = s.find_first_of("Zz+-", 10);
    if (z != std::string::npos && (s[z] == '+' || s[z] == '-')) {
        int sign = s[z] == '+' ? 1 : -1;
        int hh = 0, mm = 0;
        if (std::sscanf(s.c_str() + z + 1, "%d:%d", &hh, &mm) >= 1)
            epoch -= sign * (hh * 3600 + mm * 60);
    }
    return epoch;
}

// The envelope date field holds an RFC 2822 Date header from an IMAP account and
// an ISO 8601 UTCDate from a JMAP one; parse whichever it is. A leading
// "YYYY-MM-DD" (the two dashes at [4] and [7]) marks the ISO form.
int64_t ParseEnvelopeDate(const std::string& s) {
    if (s.size() >= 10 && s[4] == '-' && s[7] == '-')
        return ParseIso8601Date(s);
    return ParseRfc2822Date(s);
}

std::string SanitizeFolder(const std::string& folder) {
    std::string out;
    for (char c : folder)
        out.push_back((c == '/' || c == '\\' || c == ':') ? '_' : c);
    if (out.empty()) out = "INBOX";
    return out;
}

bool SameNameNoCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

bool IsAsciiLetter(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

// The casing a name's directory is plain in: each word's first letter upper
// case and the rest lower ("Sent Items", "[Gmail]_All Mail", "INBOX.Drafts"),
// a leading "inbox" level as INBOX. A word starts after anything but a letter,
// a digit or a byte of a multi-byte character. One fixed form per spelling:
// of two names that differ only in case, at most one is in it.
std::string TitleCase(const std::string& name) {
    std::string out = name;
    bool wordStart = true;
    for (char& ch : out) {
        const auto c = static_cast<unsigned char>(ch);
        if (IsAsciiLetter(c)) {
            ch = static_cast<char>(wordStart ? std::toupper(c) : std::tolower(c));
            wordStart = false;
        } else {
            wordStart = !(std::isdigit(c) || c >= 0x80);
        }
    }
    if (out.size() >= 5 && SameNameNoCase(out.substr(0, 5), "INBOX") &&
        (out.size() == 5 || !(std::isalnum(static_cast<unsigned char>(out[5])) ||
                              static_cast<unsigned char>(out[5]) >= 0x80)))
        for (std::size_t i = 0; i < 5; ++i)
            out[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[i])));
    return out;
}

// The directory an earlier version cached the folder's bodies in.
std::string LegacyCacheDirectory(const std::string& folder) { return SanitizeFolder(folder); }

} // namespace

std::string CacheDirectoryOf(const std::string& folder) {
    const std::string name = SanitizeFolder(folder);
    const std::string title = TitleCase(name);
    // Which letters differ from the title case, one bit each in letter order,
    // four to a hex digit, lowest first.
    std::string mask;
    int nibble = 0, bit = 0;
    bool any = false;
    for (std::size_t i = 0; i < name.size(); ++i) {
        if (!IsAsciiLetter(static_cast<unsigned char>(name[i]))) continue;
        if (name[i] != title[i]) { nibble |= 1 << bit; any = true; }
        if (++bit == 4) {
            mask.push_back("0123456789abcdef"[nibble]);
            nibble = 0; bit = 0;
        }
    }
    if (bit > 0) mask.push_back("0123456789abcdef"[nibble]);
    while (!mask.empty() && mask.back() == '0') mask.pop_back();
    // A '~' of the name doubled, so the one before the mask stands alone.
    std::string out;
    for (char c : name) {
        out.push_back(c);
        if (c == '~') out.push_back('~');
    }
    if (any) out += "~" + mask;
    return out;
}

std::string FolderOfCacheDirectory(const std::string& directory) {
    std::string out;
    for (std::size_t i = 0; i < directory.size(); ++i) {
        if (directory[i] != '~') { out.push_back(directory[i]); continue; }
        if (i + 1 < directory.size() && directory[i + 1] == '~') { out.push_back('~'); ++i; continue; }
        break;   // the lone '~' before the case mask
    }
    return out;
}

namespace {

// `uid` of a cached body's file name ("123.eml"), or -1.
int64_t UidOfBodyFile(const fs::path& file) {
    if (file.extension() != ".eml") return -1;
    const std::string stem = PathToUtf8(file.stem());
    if (stem.empty() || stem.size() > 18) return -1;
    for (char c : stem) if (!std::isdigit(static_cast<unsigned char>(c))) return -1;
    return std::stoll(stem);
}

// Moves `from` to `to`; a body already at `to` wins and `from` goes.
bool MoveBodyFile(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    if (fs::exists(to, ec)) return fs::remove(from, ec) || !fs::exists(from, ec);
    fs::create_directories(to.parent_path(), ec);
    fs::rename(from, to, ec);
    return !ec;
}

} // namespace

int MigrateBodyCache(const std::string& emlDir, const LocalStore& store) {
    const fs::path root = PathFromUtf8(emlDir);
    const fs::path marker = root / ".layout-2";
    std::error_code ec;
    if (fs::exists(marker, ec)) return 0;
    int moved = 0;
    bool complete = true;
    std::vector<Account> accounts;
    store.ListAccounts(accounts);
    for (const auto& account : accounts) {
        const fs::path accountDir = root / PathFromUtf8(account.accountId);
        if (!fs::is_directory(accountDir, ec)) continue;
        std::vector<Folder> folders;
        store.ListFolders(account.accountId, folders);
        for (const auto& folder : folders) {
            const std::string legacyName = LegacyCacheDirectory(folder.name);
            const std::string newName = CacheDirectoryOf(folder.name);
            if (legacyName == newName) continue;
            const fs::path legacy = accountDir / PathFromUtf8(legacyName);
            const fs::path target = accountDir / PathFromUtf8(newName);
            if (!fs::is_directory(legacy, ec)) continue;

            // The folders whose old directory is this one: their names differ
            // only in case, on a file system that ignores it (Windows, macOS).
            std::set<int64_t> twinUids;
            bool shared = false;
            for (const auto& other : folders) {
                if (other.name == folder.name) continue;
                const fs::path otherLegacy = accountDir / PathFromUtf8(LegacyCacheDirectory(other.name));
                std::error_code eq;
                if (!fs::exists(otherLegacy, eq) || !fs::equivalent(legacy, otherLegacy, eq) || eq)
                    continue;
                shared = true;
                std::vector<int64_t> uids;
                store.ListUids(account.accountId, other.name, uids);
                twinUids.insert(uids.begin(), uids.end());
            }

            if (!shared && !fs::exists(target, ec)) {
                fs::rename(legacy, target, ec);
                if (ec) complete = false; else ++moved;
                continue;
            }
            // File by file: into a directory that is there already, or out of
            // one a twin shares - then only this folder's own bodies, and one
            // both numbered alike is nobody's for sure: it goes, and each
            // folder downloads its own again when it is read.
            std::set<int64_t> own;
            if (shared) {
                std::vector<int64_t> uids;
                store.ListUids(account.accountId, folder.name, uids);
                own.insert(uids.begin(), uids.end());
            }
            std::vector<fs::path> files;
            for (const auto& entry : fs::directory_iterator(legacy, ec))
                if (entry.is_regular_file(ec)) files.push_back(entry.path());
            for (const auto& file : files) {
                const int64_t uid = UidOfBodyFile(file);
                if (shared) {
                    if (uid < 0 || !own.count(uid)) continue;   // the twin's
                    if (twinUids.count(uid)) { fs::remove(file, ec); continue; }
                }
                if (!MoveBodyFile(file, target / file.filename())) complete = false;
            }
            fs::remove(legacy, ec);   // only once it is empty
            ++moved;
        }
    }
    if (complete) {
        fs::create_directories(root, ec);
        std::ofstream(marker) << "2\n";
    }
    return moved;
}

std::string CachedBodyPath(const std::string& emlDir, const std::string& accountId,
                           const std::string& folder, int64_t uid) {
    // Every part through PathFromUtf8: joining a std::string straight onto a
    // path converts it in the ANSI code page on Windows, so a folder whose
    // name is outside that code page was cached under a mangled name.
    fs::path p = PathFromUtf8(emlDir) / PathFromUtf8(accountId)
               / PathFromUtf8(CacheDirectoryOf(folder)) / (std::to_string(uid) + ".eml");
    return PathToUtf8(p);
}

std::string SyncEngine::BodyPath(const std::string& accountId,
                                 const std::string& folder, int64_t uid) const {
    return CachedBodyPath(emlDir_, accountId, folder, uid);
}

void SyncEngine::RemoveFolderBodies(const std::string& accountId, const std::string& folder) {
    std::error_code ec;
    fs::remove_all(PathFromUtf8(BodyPath(accountId, folder, 0)).parent_path(), ec);
}

SyncOutcome SyncEngine::SyncFolders(const std::string& accountId,
                                    const std::string& serverUrl,
                                    const UltraNetMailOptions& options) {
    std::vector<UltraNetMailFolder> folders;
    UltraNetResult r = mailbox_.ListFolders(serverUrl, folders, options);
    if (!r) return SyncOutcome::Fail(r);

    SyncOutcome out;
    std::vector<Folder> stored;
    store_.ListFolders(accountId, stored);
    std::set<std::string> listed;
    for (const auto& f : folders) {
        Folder lf;
        lf.accountId = accountId;
        lf.name = f.name;
        lf.role = FolderRoleFromString(f.role);
        // The inbox is INBOX here, as the inbox sync stores its mail, however
        // the server names it: an IMAP server may list "Inbox" (the name is
        // case-insensitive), a JMAP server lists its role=inbox mailbox by
        // its display name. Kept under that name it became a second folder,
        // with the inbox role but none of the mail.
        if (lf.role == FolderRole::Inbox) lf.name = "INBOX";
        lf.selectable = f.selectable;   // \Noselect containers (e.g. "[Gmail]")
        lf.delimiter = f.delimiter;
        if (store_.UpsertFolder(lf)) out.stats.folders++;
        listed.insert(lf.name);
    }

    // A folder the server no longer lists was deleted or renamed there: it
    // leaves the tree, with its messages and their cached bodies. Only on a
    // list that names something - an empty one is never taken for "every
    // folder is gone" - and never the inbox, which every mailbox has.
    if (!listed.empty()) {
        for (const auto& s : stored) {
            if (listed.count(s.name) || s.name == "INBOX") continue;
            if (!store_.RemoveFolder(accountId, s.name)) continue;
            RemoveFolderBodies(accountId, s.name);
            out.stats.foldersRemoved++;
        }
    }
    return out;
}

SyncOutcome SyncEngine::CreateFolder(const std::string& accountId, const std::string& folder,
                                     const std::string& serverUrl,
                                     const UltraNetMailOptions& options) {
    if (folder.empty()) return SyncOutcome::Fail("The folder has no name.");
    UltraNetResult r = mailbox_.CreateFolder(serverUrl, folder, options);
    if (!r) return SyncOutcome::Fail(r);
    // The list names it now, with the attributes and separator the server
    // gave it; a list that cannot be read leaves it for the next sync.
    SyncOutcome listed = SyncFolders(accountId, serverUrl, options);
    return listed.ok ? listed : SyncOutcome{};
}

SyncOutcome SyncEngine::DeleteFolder(const std::string& accountId, const std::string& folder,
                                     const std::string& serverUrl,
                                     const UltraNetMailOptions& options) {
    if (folder.empty() || folder == "INBOX")
        return SyncOutcome::Fail("The inbox cannot be deleted.");
    UltraNetResult r = mailbox_.DeleteFolder(serverUrl, folder, options);
    if (!r) return SyncOutcome::Fail(r);
    SyncOutcome out;
    if (store_.RemoveFolder(accountId, folder)) {
        RemoveFolderBodies(accountId, folder);
        out.stats.foldersRemoved = 1;
    }
    SyncOutcome listed = SyncFolders(accountId, serverUrl, options);
    if (listed.ok) {
        listed.stats.foldersRemoved += out.stats.foldersRemoved;
        return listed;
    }
    return out;
}

bool SyncEngine::FolderStillListed(const std::string& accountId, const std::string& folder,
                                   const std::string& serverUrl,
                                   const UltraNetMailOptions& options) {
    if (folder == "INBOX") return true;
    SyncOutcome listed = SyncFolders(accountId, serverUrl, options);
    if (!listed.ok) return true;
    std::vector<Folder> folders;
    if (!store_.ListFolders(accountId, folders)) return true;
    for (const auto& f : folders) if (f.name == folder) return true;
    return false;
}

MessageEnvelope SyncEngine::ToStored(const std::string& accountId, const std::string& folder,
                                    const UltraNetMailEnvelope& e) const {
    MessageEnvelope m;
    m.accountId = accountId;
    m.folder    = folder;
    m.uid       = static_cast<int64_t>(e.uid);
    m.messageId = e.messageId;
    m.inReplyTo = e.inReplyTo;
    // Subject / display names arrive as RFC 2047 encoded-words on the
    // envelope path (unlike a full message parse). Decode them once here
    // so the store — list, preview and collected contacts — holds
    // readable text.
    m.subject   = UltraNet_MimeDecodeHeader(e.subject);
    ParseFromField(e.from, m.fromName, m.fromAddr);
    m.fromName = UltraNet_MimeDecodeHeader(m.fromName);
    m.to    = e.to;
    for (auto& addr : m.to) addr = UltraNet_MimeDecodeHeader(addr);
    m.date  = ParseEnvelopeDate(e.date);
    m.flags = MapNetFlagsToLocal(e.flags);
    // Automated/bulk detection needs List-*/Precedence headers, which the
    // envelope fetch does not carry yet; left false for now.
    m.automated = false;
    return m;
}

SyncOutcome SyncEngine::SyncMessages(const std::string& accountId,
                                     const std::string& folder,
                                     const std::string& serverUrl,
                                     const UltraNetMailOptions& options,
                                     bool fetchBodies,
                                     const std::function<void(const MessageEnvelope&)>& onMessageStored) {
    SyncOutcome out;
    int64_t sinceUid = 0;
    store_.GetMaxUid(accountId, folder, sinceUid);

    // Is the cache from this numbering of the mailbox? Two signs it is not,
    // and either drops the folder's cache so the fetch below starts again
    // from UID 0 - an incremental fetch ("UID > the highest held") would
    // otherwise skip every new message numbered below a stale highest UID:
    //  - UIDVALIDITY changed: the server renumbered the mailbox.
    //  - the cache holds a UID at or above UIDNEXT, which the mailbox has not
    //    handed out yet. A renumbering whose UIDVALIDITY change was missed
    //    (unknown before, or read wrongly), or mail from another server after
    //    the account's server changed.
    // Best-effort: a backend that cannot report STATUS keeps the incremental
    // behaviour.
    UltraNetMailboxStatus status;
    if (mailbox_.GetMailboxStatus(serverUrl, folder, status, options)) {
        out.stats.serverMessages = static_cast<int>(status.messages);
        int64_t stored = 0;
        store_.GetFolderUidValidity(accountId, folder, stored);
        const bool renumbered = status.uidValidity != 0 && stored != 0 &&
                                stored != static_cast<int64_t>(status.uidValidity);
        const bool aheadOfServer = status.uidNext != 0 &&
                                   sinceUid >= static_cast<int64_t>(status.uidNext);
        if (renumbered || aheadOfServer) {
            store_.ClearFolderMessages(accountId, folder);   // drop stale cached UIDs
            // ... and their bodies: every cached UID of the folder is stale, and
            // the new numbering would overwrite some files and orphan the rest.
            RemoveFolderBodies(accountId, folder);
            sinceUid = 0;
            out.stats.cacheReset = true;
        }
        if (status.uidValidity != 0)
            store_.SetFolderUidState(accountId, folder,
                                     static_cast<int64_t>(status.uidValidity),
                                     static_cast<int64_t>(status.uidNext));
    }

    std::vector<uint32_t> bodyUids;
    // Stream envelopes: each header lands one at a time so `onMessageStored` can
    // fill the UI list incrementally instead of the caller waiting for the whole
    // mailbox (a large INBOX otherwise looks like the app has hung).
    UltraNetResult r = mailbox_.FetchEnvelopes(
        serverUrl, folder, static_cast<uint32_t>(sinceUid),
        [&](const UltraNetMailEnvelope& e) {
            // "UID n:*" always matches the highest UID, even one below n:
            // that message is held already.
            if (sinceUid > 0 && static_cast<int64_t>(e.uid) <= sinceUid) return;
            const MessageEnvelope m = ToStored(accountId, folder, e);
            if (store_.UpsertMessage(m)) out.stats.messages++;
            if (fetchBodies) bodyUids.push_back(e.uid);
            if (onMessageStored) onMessageStored(m);
        },
        options);
    if (!r) {
        SyncOutcome fail = SyncOutcome::Fail(r);
        fail.stats = out.stats;
        return fail;
    }

    // Fetch all new bodies over ONE reused connection (see
    // IMailboxProtocolPlugin::FetchMessageBodies) instead of reconnecting per
    // message — the difference between seconds and minutes on Gmail.
    if (fetchBodies && !bodyUids.empty()) {
        mailbox_.FetchMessageBodies(
            serverUrl, folder, bodyUids,
            [&](uint32_t uid, const std::string& raw) {
                if (!WriteBody(accountId, folder, static_cast<int64_t>(uid), raw).empty())
                    out.stats.bodies++;
            },
            options);
    }
    CountStoredAttachments(accountId, folder);
    RescanStaleVerdicts(accountId, folder);
    return out;
}

SyncOutcome SyncEngine::FetchMissing(const std::string& accountId,
                                     const std::string& folder,
                                     const std::vector<uint32_t>& uids,
                                     const std::string& serverUrl,
                                     const UltraNetMailOptions& options,
                                     bool fetchBodies,
                                     const std::function<void(const MessageEnvelope&)>& onMessageStored) {
    SyncOutcome out;
    if (uids.empty()) return out;
    std::vector<uint32_t> bodyUids;
    UltraNetResult r = mailbox_.FetchEnvelopesByUid(
        serverUrl, folder, uids,
        [&](const UltraNetMailEnvelope& e) {
            const MessageEnvelope m = ToStored(accountId, folder, e);
            if (store_.UpsertMessage(m)) {
                out.stats.messages++;
                out.stats.repaired++;
            }
            if (fetchBodies) bodyUids.push_back(e.uid);
            if (onMessageStored) onMessageStored(m);
        },
        options);
    if (!r) {
        SyncOutcome fail = SyncOutcome::Fail(r);
        fail.stats = out.stats;
        return fail;
    }
    if (fetchBodies && !bodyUids.empty()) {
        mailbox_.FetchMessageBodies(
            serverUrl, folder, bodyUids,
            [&](uint32_t uid, const std::string& raw) {
                if (!WriteBody(accountId, folder, static_cast<int64_t>(uid), raw).empty())
                    out.stats.bodies++;
            },
            options);
    }
    return out;
}

SyncOutcome SyncEngine::RefreshFolder(const std::string& accountId,
                                      const std::string& folder,
                                      const std::string& serverUrl,
                                      const UltraNetMailOptions& options,
                                      bool fetchBodies,
                                      const std::function<void(const MessageEnvelope&)>& onMessageStored) {
    // New mail first: it is what the reader waits for, and it streams into
    // the list as it arrives.
    SyncOutcome out = SyncMessages(accountId, folder, serverUrl, options, fetchBodies,
                                   onMessageStored);
    if (!out.ok) return out;

    // Then the server's whole list: read state changed elsewhere, mail
    // deleted or moved elsewhere, and the UIDs the store lacks.
    std::vector<uint32_t> missing;
    SyncOutcome rec = ReconcileFlags(accountId, folder, serverUrl, options, &missing);
    out.stats.reconciled    = rec.stats.reconciled;
    out.stats.expunged      = rec.stats.expunged;
    out.stats.bodiesRemoved = rec.stats.bodiesRemoved;

    // ... and the rows earlier versions stored blank for a header they
    // could not read. Both are fetched by UID; the repair is best-effort, and
    // what it does not get is asked for again next time.
    std::vector<int64_t> blank;
    store_.ListBlankUids(accountId, folder, blank);
    for (int64_t uid : blank)
        if (uid > 0 && uid <= 0xFFFFFFFFll) missing.push_back(static_cast<uint32_t>(uid));
    std::sort(missing.begin(), missing.end());
    missing.erase(std::unique(missing.begin(), missing.end()), missing.end());
    if (!missing.empty()) {
        SyncOutcome rep = FetchMissing(accountId, folder, missing, serverUrl, options,
                                       fetchBodies, onMessageStored);
        out.stats.messages += rep.stats.messages;
        out.stats.repaired += rep.stats.repaired;
        out.stats.bodies   += rep.stats.bodies;
        CountStoredAttachments(accountId, folder);
    }
    // Bodies an earlier download did not get.
    if (fetchBodies) {
        const int bodies = FetchMissingBodies(accountId, folder, serverUrl, options);
        out.stats.bodies += bodies;
        if (bodies > 0) CountStoredAttachments(accountId, folder);
    }
    return out;
}

int SyncEngine::FetchMissingBodies(const std::string& accountId, const std::string& folder,
                                   const std::string& serverUrl,
                                   const UltraNetMailOptions& options, int limit) {
    std::vector<MessageEnvelope> newest;
    store_.ListMessages(accountId, folder, limit, newest);
    std::vector<uint32_t> uids;
    for (const auto& m : newest) {
        std::error_code ec;
        if (m.uid > 0 && m.uid <= 0xFFFFFFFFll &&
            !fs::exists(PathFromUtf8(BodyPath(accountId, folder, m.uid)), ec))
            uids.push_back(static_cast<uint32_t>(m.uid));
    }
    if (uids.empty()) return 0;
    int cached = 0;
    mailbox_.FetchMessageBodies(
        serverUrl, folder, uids,
        [&](uint32_t uid, const std::string& raw) {
            if (!WriteBody(accountId, folder, static_cast<int64_t>(uid), raw).empty()) ++cached;
        },
        options);
    return cached;
}

int SyncEngine::CountStoredAttachments(const std::string& accountId, const std::string& folder,
                                       int limit) {
    // Bodies downloaded before the count was recorded (or by FetchBody) are
    // counted here from the cache, a bounded batch per sync, newest first -
    // the messages the list shows at the top.
    std::vector<int64_t> uids;
    if (!store_.ListUncountedAttachments(accountId, folder, limit, uids)) return 0;
    int counted = 0;
    for (int64_t uid : uids) {
        const std::string path = BodyPath(accountId, folder, uid);
        std::string raw;
        if (!ReadBodyFile(path, raw)) continue;    // body not downloaded yet
        if (raw.empty()) continue;
        if (store_.SetAttachmentCount(accountId, folder, uid, MimeCodec::CountAttachments(raw)))
            ++counted;
    }
    return counted;
}

std::string SyncEngine::WriteBody(const std::string& accountId, const std::string& folder,
                                  int64_t uid, const std::string& raw) const {
    if (raw.empty()) return std::string();
    const std::string path = BodyPath(accountId, folder, uid);
    std::error_code ec;
    fs::create_directories(PathFromUtf8(path).parent_path(), ec);
    // Sealed with the mailbox's body key when it has one (UltraMailStorage.h).
    if (!WriteBodyFile(path, raw)) return std::string();

    // A body is scanned exactly once — here, where it has just been downloaded
    // and is already in memory. The verdict goes into the index, so the message
    // list can colour its sender badge without re-reading a single .eml file,
    // and a phishing mail is marked before it is ever opened.
    const ThreatReport report = ScanRawMessage(raw);
    MessageSecurity security;
    security.level  = report.level;
    security.score  = report.score;
    security.bulk   = report.bulk;
    security.reason = report.Summary();
    security.verifiedDomain = report.verifiedDomain;
    security.verifiedBy     = report.verifiedBy;
    security.findings       = report.Codes();
    security.attachments = MimeCodec::CountAttachments(raw);   // the list's paperclip
    store_.SetSecurity(accountId, folder, uid, security);
    return path;
}

int SyncEngine::RescanStaleVerdicts(const std::string& accountId, const std::string& folder,
                                    int limit) {
    std::vector<int64_t> uids;
    if (!store_.ListStaleVerdicts(accountId, folder, kThreatRulesRevision, limit, uids))
        return 0;
    int rescanned = 0;
    for (int64_t uid : uids) {
        std::string raw;
        if (!ReadBodyFile(BodyPath(accountId, folder, uid), raw)) continue;
        if (raw.empty()) continue;
        const ThreatReport report = ScanRawMessage(raw);
        MessageSecurity security;
        security.level  = report.level;
        security.score  = report.score;
        security.bulk   = report.bulk;
        security.reason = report.Summary();
        security.verifiedDomain = report.verifiedDomain;
        security.verifiedBy     = report.verifiedBy;
        security.findings       = report.Codes();
        // attachments stays -1: the count already stored is kept.
        if (store_.SetSecurity(accountId, folder, uid, security)) ++rescanned;
    }
    return rescanned;
}

std::string SyncEngine::FetchBody(const std::string& accountId, const std::string& folder,
                                  int64_t uid, const std::string& serverUrl,
                                  const UltraNetMailOptions& options) {
    std::string raw;
    UltraNetResult r = mailbox_.FetchMessage(serverUrl, folder,
                                             static_cast<uint32_t>(uid), raw, options);
    if (!r || raw.empty()) return std::string();
    return WriteBody(accountId, folder, uid, raw);
}

SyncOutcome SyncEngine::SetFlag(const std::string& accountId, const std::string& folder,
                                int64_t uid, uint32_t ultramailFlag, bool set,
                                const std::string& serverUrl,
                                const UltraNetMailOptions& options) {
    UltraNetResult r = mailbox_.StoreFlags(
        serverUrl, folder, static_cast<uint32_t>(uid),
        MapLocalFlagToNet(ultramailFlag), set, options);
    if (!r) return SyncOutcome::Fail(r);

    UltraDbResult lr = store_.SetFlags(accountId, folder, uid, ultramailFlag, set);
    if (!lr) return SyncOutcome::Fail(lr.message);
    return SyncOutcome{};
}

SyncOutcome SyncEngine::ReconcileFlags(const std::string& accountId,
                                       const std::string& folder,
                                       const std::string& serverUrl,
                                       const UltraNetMailOptions& options,
                                       std::vector<uint32_t>* missing) {
    if (missing) missing->clear();
    // Snapshot what we hold locally so we can both diff flags and notice UIDs the
    // server has dropped.
    std::vector<MessageEnvelope> locals;
    store_.ListMessages(accountId, folder, 0, locals);
    std::unordered_map<int64_t, uint32_t> localFlags;
    localFlags.reserve(locals.size());
    for (const auto& m : locals) localFlags[m.uid] = m.flags;

    SyncOutcome out;
    std::unordered_set<int64_t> serverUids;
    UltraNetResult r = mailbox_.FetchAllFlags(
        serverUrl, folder,
        [&](uint32_t uid, UltraNetMailFlags nf, bool flagsKnown) {
            const int64_t u = static_cast<int64_t>(uid);
            serverUids.insert(u);   // existence — drives deletion detection
            // Reconcile a flag only when the server flags were actually read;
            // rewriting on an unknown flag would wrongly mark read mail unread.
            // (UIDs new to us are the incremental SyncMessages step's job.)
            if (!flagsKnown) return;
            auto it = localFlags.find(u);
            if (it == localFlags.end()) return;
            const uint32_t want = MapNetFlagsToLocal(nf);
            if (it->second != want &&
                store_.ReplaceFlags(accountId, folder, u, want))
                out.stats.reconciled++;
        },
        options);

    // Expunge locally-held messages the server no longer lists — but NEVER on an
    // empty enumeration while we still hold mail. A failed/empty flag fetch is not
    // "the folder is empty"; treating it as such would delete the whole cache. So
    // the expunge runs only when the server positively enumerated the folder.
    const bool enumerated = r && !(serverUids.empty() && !locals.empty());
    if (enumerated) {
        std::unordered_set<int64_t> kept;
        int64_t maxUid = 0;
        for (const auto& m : locals) {
            maxUid = std::max(maxUid, m.uid);
            if (serverUids.count(m.uid)) {
                kept.insert(m.uid);
                continue;
            }
            if (store_.RemoveMessage(accountId, folder, m.uid)) {
                out.stats.expunged++;
                std::error_code ec;
                if (fs::remove(PathFromUtf8(BodyPath(accountId, folder, m.uid)), ec))
                    out.stats.bodiesRemoved++;
            }
        }
        // Bodies an earlier version left behind when it dropped only the row.
        // Only once the server has positively listed the folder, as above.
        out.stats.bodiesRemoved += PruneBodies(accountId, folder, kept, maxUid);
        // What the server lists and the store does not hold: mail an earlier
        // sync skipped. (A message that arrived since the new-mail step is in
        // here too; fetching it now is just as right.)
        if (missing) {
            for (int64_t uid : serverUids)
                if (!localFlags.count(uid)) missing->push_back(static_cast<uint32_t>(uid));
            std::sort(missing->begin(), missing->end());
        }
    }
    return out;
}

UltraDbResult SyncEngine::ForgetMessage(const std::string& accountId,
                                        const std::string& folder, int64_t uid) {
    UltraDbResult r = store_.RemoveMessage(accountId, folder, uid);
    if (!r) return r;
    std::error_code ec;
    fs::remove(PathFromUtf8(BodyPath(accountId, folder, uid)), ec);   // absent is fine
    return r;
}

int SyncEngine::PruneBodies(const std::string& accountId, const std::string& folder,
                            const std::unordered_set<int64_t>& keep, int64_t maxUid) {
    if (maxUid <= 0) return 0;
    const fs::path dir = PathFromUtf8(BodyPath(accountId, folder, 0)).parent_path();
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return 0;

    std::vector<fs::path> stale;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec) || PathToUtf8(entry.path().extension()) != ".eml")
            continue;
        const std::string stem = PathToUtf8(entry.path().stem());
        if (stem.empty() || stem.find_first_not_of("0123456789") != std::string::npos)
            continue;   // not a cached body this engine wrote
        const int64_t uid = std::strtoll(stem.c_str(), nullptr, 10);
        if (uid <= maxUid && !keep.count(uid)) stale.push_back(entry.path());
    }
    int removed = 0;
    for (const fs::path& p : stale) {
        if (fs::remove(p, ec)) ++removed;
    }
    return removed;
}

SyncOutcome SyncEngine::MoveMessage(const std::string& accountId,
                                    const std::string& srcFolder, int64_t uid,
                                    const std::string& dstFolder,
                                    const std::string& serverUrl,
                                    const UltraNetMailOptions& options) {
    UltraNetResult r = mailbox_.MoveMessage(
        serverUrl, srcFolder, static_cast<uint32_t>(uid), dstFolder, options);
    if (!r) return SyncOutcome::Fail(r);

    // The server moved it out of srcFolder; drop the local row (and its cached
    // body) so the list stops showing it. The destination folder picks it up on
    // its next sync.
    UltraDbResult lr = ForgetMessage(accountId, srcFolder, uid);
    if (!lr) return SyncOutcome::Fail(lr.message);
    return SyncOutcome{};
}

} // namespace UltraMail
