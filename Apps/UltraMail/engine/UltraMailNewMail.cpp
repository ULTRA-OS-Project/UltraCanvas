// Apps/UltraMail/engine/UltraMailNewMail.cpp
// See UltraMailNewMail.h.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailNewMail.h"

#include <algorithm>

namespace UltraMail {

namespace {

constexpr const char* kInbox = "INBOX";

// Newest first: by date, then by UID for mail of the same second.
bool Newer(const MessageEnvelope& a, const MessageEnvelope& b) {
    if (a.date != b.date) return a.date > b.date;
    return a.uid > b.uid;
}

std::string SenderOf(const MessageEnvelope& m) {
    if (!m.fromName.empty()) return m.fromName;
    if (!m.fromAddr.empty()) return m.fromAddr;
    return "an unknown sender";
}

std::string SubjectOf(const MessageEnvelope& m) {
    std::string subject = m.subject;
    // One line: a folded or broken subject would break the notification's
    // layout into several.
    std::replace(subject.begin(), subject.end(), '\n', ' ');
    std::replace(subject.begin(), subject.end(), '\r', ' ');
    return subject.empty() ? std::string("(no subject)") : subject;
}

} // namespace

bool NewMailTracker::IsNew(const MessageEnvelope& m, int64_t inboxMaxUid) {
    if (inboxMaxUid <= 0) return false;                       // the first download: history
    if (m.folder != kInbox) return false;
    if (m.uid <= inboxMaxUid) return false;                   // held before, or a repaired gap
    if (m.flags & (Flag_Seen | Flag_Deleted | Flag_Draft)) return false;
    return true;
}

void NewMailTracker::Begin(const std::string& accountId, int64_t inboxMaxUid) {
    std::lock_guard<std::mutex> lock(mutex_);
    Pending& p = pending_[accountId];
    if (p.active++ == 0) {
        p.watermark = inboxMaxUid;
        p.announce = true;
        p.uids.clear();
        p.summary = NewMailSummary{};
        p.summary.accountId = accountId;
    }
}

void NewMailTracker::Note(const MessageEnvelope& m) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pending_.find(m.accountId);
    if (it == pending_.end() || it->second.active == 0) return;
    Pending& p = it->second;
    if (!IsNew(m, p.watermark)) return;
    if (!p.uids.insert(m.uid).second) return;
    ++p.summary.count;
    auto& latest = p.summary.latest;
    latest.insert(std::upper_bound(latest.begin(), latest.end(), m, Newer), m);
    if (latest.size() > kNewMailNamed) latest.pop_back();
}

NewMailSummary NewMailTracker::Finish(const std::string& accountId, bool announce) {
    std::lock_guard<std::mutex> lock(mutex_);
    NewMailSummary out;
    out.accountId = accountId;
    auto it = pending_.find(accountId);
    if (it == pending_.end() || it->second.active == 0) return out;
    Pending& p = it->second;
    if (!announce) p.announce = false;
    if (--p.active > 0) return out;
    if (p.announce) out = std::move(p.summary);
    pending_.erase(it);
    return out;
}

NewMailText FormatNewMail(const NewMailSummary& summary, const std::string& accountLabel) {
    NewMailText text;
    if (summary.count <= 0) return text;
    if (summary.count == 1 && !summary.latest.empty()) {
        const MessageEnvelope& m = summary.latest.front();
        text.summary = "New mail from " + SenderOf(m);
        text.body = SubjectOf(m);
        if (!accountLabel.empty()) text.body += "\nto " + accountLabel;
        return text;
    }
    text.summary = std::to_string(summary.count) + " new messages";
    if (!accountLabel.empty()) text.summary += " for " + accountLabel;
    for (const auto& m : summary.latest) {
        if (!text.body.empty()) text.body += "\n";
        text.body += SenderOf(m) + ": " + SubjectOf(m);
    }
    const int named = static_cast<int>(summary.latest.size());
    if (summary.count > named) text.body += "\nand " + std::to_string(summary.count - named) + " more";
    return text;
}

} // namespace UltraMail
