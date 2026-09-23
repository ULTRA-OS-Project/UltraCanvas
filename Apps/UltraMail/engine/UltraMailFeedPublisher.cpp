// Apps/UltraMail/engine/UltraMailFeedPublisher.cpp
// See UltraMailFeedPublisher.h.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailFeedPublisher.h"

#include <chrono>

#ifdef ULTRAMAIL_HAVE_ULTRAMESSAGE
#  include <UltraMessage/UltraMessage.h>
#  include <UltraMessage/UltraMessageEndpoint.h>
#endif

namespace UltraMail {

namespace {

int64_t NowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

FeedPublisher::FeedPublisher() = default;

FeedPublisher::~FeedPublisher() {
    Disconnect();
}

bool FeedPublisher::Compiled() {
#ifdef ULTRAMAIL_HAVE_ULTRAMESSAGE
    return true;
#else
    return false;
#endif
}

void FeedPublisher::SetEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    enabled_ = enabled;
}

bool FeedPublisher::Enabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return enabled_;
}

void FeedPublisher::SetPolicy(const FeedPolicy& policy) {
    std::lock_guard<std::mutex> lock(mutex_);
    policy_ = policy;
}

void FeedPublisher::SetAccount(const std::string& accountId, const std::string& email,
                               const std::string& displayName) {
    std::lock_guard<std::mutex> lock(mutex_);
    accounts_[accountId] = {email, displayName};
}

bool FeedPublisher::Qualifies(const MessageEnvelope& m, int64_t nowEpochSeconds, const FeedPolicy& policy) {
    if (m.flags & Flag_Seen) return false;                 // already read elsewhere
    if (m.flags & (Flag_Deleted | Flag_Draft)) return false;
    if (policy.skipAutomated && m.automated) return false;
    if (policy.maxAgeDays > 0) {
        const int64_t maxAge = int64_t(policy.maxAgeDays) * 86400;
        if (m.date <= 0) return false;                     // undated: cannot tell old from new
        if (nowEpochSeconds - m.date > maxAge) return false;
    }
    return true;
}

bool FeedPublisher::Admit(const std::string& accountId, int64_t nowEpochSeconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    Window& w = windows_[accountId];
    if (w.start == 0 || nowEpochSeconds - w.start >= policy_.windowSeconds) {
        w.start = nowEpochSeconds;
        w.count = 0;
    }
    if (policy_.maxPerAccountPerWindow > 0 && w.count >= policy_.maxPerAccountPerWindow) return false;
    ++w.count;
    return true;
}

bool FeedPublisher::EnsureConnectedLocked() {
#ifdef ULTRAMAIL_HAVE_ULTRAMESSAGE
    if (endpoint_ != 0 && UltraMsg_IsConnected(static_cast<UltraMsgHandle>(endpoint_))) return true;
    endpoint_ = 0;
    UltraMsgConnectOptions options;
    options.appId = "org.ultraos.ultramail";
    options.displayName = "UltraMail";
    options.deliverOnUIThread = false;   // this endpoint only posts
    UltraMsgResult error;
    UltraMsgHandle handle = UltraMsg_Connect(options, &error);
    if (handle == UltraMsgInvalidHandle) {
        lastError_ = error.message.empty() ? "cannot connect to the UltraMessage bus" : error.message;
        return false;
    }
    endpoint_ = static_cast<uint64_t>(handle);
    lastError_.clear();
    return true;
#else
    lastError_ = "built without UltraMessage";
    return false;
#endif
}

bool FeedPublisher::Publish(const MessageEnvelope& m) {
    const int64_t now = NowSeconds();
    FeedPolicy policy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!enabled_) return false;
        policy = policy_;
    }
    if (!Qualifies(m, now, policy)) return false;
    if (!Admit(m.accountId, now)) return false;

#ifdef ULTRAMAIL_HAVE_ULTRAMESSAGE
    std::lock_guard<std::mutex> lock(mutex_);
    if (!EnsureConnectedLocked()) return false;

    UltraMessage::MailMessage mail;
    auto label = accounts_.find(m.accountId);
    mail.account = label != accounts_.end() && !label->second.email.empty() ? label->second.email : m.accountId;
    mail.folder = m.folder;
    mail.from = {m.fromName, m.fromAddr};
    for (const auto& to : m.to) mail.to.push_back({"", to});
    mail.subject = m.subject;
    mail.read = false;
    mail.flagged = (m.flags & Flag_Flagged) != 0;
    mail.externalId = !m.messageId.empty() ? m.messageId
                                           : m.accountId + ":" + m.folder + ":" + std::to_string(m.uid);
    mail.threadId = m.inReplyTo;
    UltraCanvas::JSONValue body = UltraMessage::MakeMailMessage(mail);
    body.Set("accountId", m.accountId);
    body.Set("uid", m.uid);
    body.Set("date", m.date);
    if (m.automated) body.Set("automated", true);

    UltraMsgResult r = UltraMsg_Post(static_cast<UltraMsgHandle>(endpoint_), UltraMsgTopics::MailMessage, body);
    if (!r) {
        lastError_ = r.message;
        if (r.code == UltraMsgResultCode::NotConnected) {
            UltraMsg_Disconnect(static_cast<UltraMsgHandle>(endpoint_));
            endpoint_ = 0;
        }
        return false;
    }
    ++published_;
    return true;
#else
    return false;
#endif
}

void FeedPublisher::Disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
#ifdef ULTRAMAIL_HAVE_ULTRAMESSAGE
    if (endpoint_ != 0) UltraMsg_Disconnect(static_cast<UltraMsgHandle>(endpoint_));
#endif
    endpoint_ = 0;
}

int64_t FeedPublisher::PublishedCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return published_;
}

std::string FeedPublisher::LastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

} // namespace UltraMail
