// Apps/UltraMail/engine/UltraMailSyncEngine.cpp
// Version: 0.4.0 - the verified sender domain is stored with the verdict;
//                  RescanStaleVerdicts after each sync of a folder
// Version: 0.3.0 - SyncFolders keeps the server's separator and drops the folders
//                  the server no longer lists
// Version: 0.2.0 - RefreshFolder / FetchMissing; the UIDNEXT check on the cache
// Version: 0.1.1 - envelope subject/from/to are RFC 2047 decoded when stored
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailSyncEngine.h"
#include "UltraMailMimeCodec.h"

#include "UltraMailThreatScan.h"

#include <algorithm>
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

std::string CachedBodyPath(const std::string& emlDir, const std::string& accountId,
                           const std::string& folder, int64_t uid) {
    // Every part through PathFromUtf8: joining a std::string straight onto a
    // path converts it in the ANSI code page on Windows, so a folder whose
    // name is outside that code page was cached under a mangled name.
    fs::path p = PathFromUtf8(emlDir) / PathFromUtf8(accountId)
               / PathFromUtf8(SanitizeFolder(folder)) / (std::to_string(uid) + ".eml");
    return PathToUtf8(p);
}

std::string SyncEngine::BodyPath(const std::string& accountId,
                                 const std::string& folder, int64_t uid) const {
    return CachedBodyPath(emlDir_, accountId, folder, uid);
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
        lf.selectable = f.selectable;   // \Noselect containers (e.g. "[Gmail]")
        lf.delimiter = f.delimiter;
        if (store_.UpsertFolder(lf)) out.stats.folders++;
        listed.insert(f.name);
    }

    // A folder the server no longer lists was deleted or renamed there: it
    // leaves the tree, with its messages and their cached bodies. Only on a
    // list that names something - an empty one is never taken for "every
    // folder is gone" - and never the inbox, which every mailbox has.
    if (!listed.empty()) {
        for (const auto& s : stored) {
            if (listed.count(s.name) || s.name == "INBOX") continue;
            if (!store_.RemoveFolder(accountId, s.name)) continue;
            std::error_code ec;
            fs::remove_all(PathFromUtf8(BodyPath(accountId, s.name, 0)).parent_path(), ec);
            out.stats.foldersRemoved++;
        }
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
    m.date  = ParseRfc2822Date(e.date);
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
            std::error_code ec;
            fs::remove_all(PathFromUtf8(BodyPath(accountId, folder, 0)).parent_path(), ec);
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
        std::ifstream in(PathFromUtf8(path), std::ios::binary);
        if (!in) continue;                         // body not downloaded yet
        const std::string raw((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
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
    std::ofstream os(UltraCanvas::PathFromUtf8(path), std::ios::binary | std::ios::trunc);
    if (!os) return std::string();
    os.write(raw.data(), static_cast<std::streamsize>(raw.size()));
    if (!os) return std::string();

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
        std::ifstream in(PathFromUtf8(BodyPath(accountId, folder, uid)), std::ios::binary);
        if (!in) continue;
        const std::string raw((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
        if (raw.empty()) continue;
        const ThreatReport report = ScanRawMessage(raw);
        MessageSecurity security;
        security.level  = report.level;
        security.score  = report.score;
        security.bulk   = report.bulk;
        security.reason = report.Summary();
        security.verifiedDomain = report.verifiedDomain;
        security.verifiedBy     = report.verifiedBy;
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
