// UltraCanvas/Plugins/UltraNet/jmap/JmapPlugin.cpp
// Version: 0.3.0 (Phase 3 — push: WatchMailbox / StopWatching over the
//                 session's event source)
// Version: 0.2.0 (Phase 2 — the write path: StoreFlags, MoveMessage,
//                 AppendMessage, ExpungeMessage, FetchAllFlags and SendMail
//                 through EmailSubmission)
// Version: 0.1.0 (Phase 1 — read path)
// JMAP (RFC 8620/8621) plug-in. Implements the IMailboxProtocolPlugin surface
// — folder listing, mailbox status, envelope sync, raw-message fetch, the
// legacy bulk FetchMessages, keyword and mailbox changes, blob upload with
// Email/import, destroy, and sending through EmailSubmission — over the
// UltraNet_Http* core API (no libcurl code of its own; connection pooling,
// TLS policy, proxies and timeouts come from the core) - and push: a watch
// (WatchMailbox) holds the session's event source open (UltraNet_SseStreamAsync)
// and reports a change whenever a StateChange event carries an Email or
// Mailbox state the watch has not seen.
//
// Flags: $seen / $answered / $flagged / $draft are keywords. JMAP has no
// \Deleted: setting it moves the message to the Trash mailbox (or destroys
// it when it is there already, or the account has none), clearing it is a
// no-op, and ExpungeMessage destroys. Sending: the raw message is uploaded,
// imported into Drafts (or Sent, or the inbox) and handed to
// EmailSubmission/set with the Identity whose address is the From; on
// success the server files it in Sent with $draft cleared, or destroys it
// when the account has no Sent mailbox.
//
// Server URL convention: "jmap://host[:port]/" resolves the session resource
// at "https://host[:port]/.well-known/jmap" (a non-root path overrides the
// well-known location). An explicit "https://…" session URL is accepted too.
// Authentication: Basic (username/password) or Bearer/OAuth2 token from
// UltraNetMailOptions.credentials; URL userinfo is a Basic fallback.
//
// JMAP Email ids are opaque strings; the uint32_t uids this interface exposes
// are assigned per (account, folder) by JmapCore's UidMap in ascending
// receivedAt order and are stable for the plug-in instance's lifetime.
// uidValidity is synthesised (FNV-1a of accountId+mailboxId), so a fresh
// process keeps the same uidValidity and reassigns the same uids as long as
// the mailbox only grew — exactly the resync semantics IMAP clients expect.
//
// Build: produces ultranet_jmap.{so,dylib}. Loaded at runtime by
// UltraNet_RefreshPlugins(). Wire logic lives in JmapCore.h (pure, tested).

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetHttp.h>
#include <UltraNet/UltraNetMime.h>
#include <UltraNet/UltraNetPlugins.h>
#include <UltraNet/UltraNetSse.h>
#include "UltraNetPluginHostShim.h"
#include <UltraNet/UltraNetUrl.h>

#include "JmapCore.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace ultranet_jmap;

constexpr int kPingSeconds     = 300;    // the event source's keep-alive
constexpr int kQueryPageSize   = 500;    // Email/query ids per page
constexpr int kQueryMaxIds     = 100000; // safety cap on ids walked per folder
constexpr int kEmailGetChunk   = 250;    // ids per Email/get call

// Resolve the session-resource URL from a plug-in server URL. Captures URL
// userinfo as a Basic-auth fallback.
bool ResolveSessionUrl(const std::string& serverUrl, std::string& outSessionUrl,
                       std::string& outUser, std::string& outPass) {
    UltraNetUrlComponents c;
    if (!UltraNet_ParseUrl(serverUrl, c)) return false;
    outUser = c.username;
    outPass = c.password;
    if (c.scheme == "https") {
        // Explicit session URL — strip userinfo, keep the rest verbatim.
        std::ostringstream os;
        os << "https://" << c.host;
        if (c.port > 0) os << ':' << c.port;
        os << (c.path.empty() ? "/" : c.path);
        if (!c.query.empty()) os << '?' << c.query;
        outSessionUrl = os.str();
        return true;
    }
    if (c.scheme != "jmap") return false;
    std::ostringstream os;
    os << "https://" << c.host;
    if (c.port > 0) os << ':' << c.port;
    os << (c.path.empty() || c.path == "/" ? "/.well-known/jmap" : c.path);
    outSessionUrl = os.str();
    return true;
}

// Session URLs are absolute in practice; resolve the path-only form some
// servers emit against the session document's origin.
std::string ResolveAgainst(const std::string& baseUrl, const std::string& url) {
    if (url.empty() || url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0)
        return url;
    UltraNetUrlComponents c;
    if (url.front() != '/' || !UltraNet_ParseUrl(baseUrl, c)) return url;
    std::ostringstream os;
    os << c.scheme << "://" << c.host;
    if (c.port > 0) os << ':' << c.port;
    os << url;
    return os.str();
}

UltraNetHttpOptions MakeHttpOptions(const UltraNetMailOptions& opt,
                                    const std::string& urlUser, const std::string& urlPass) {
    UltraNetHttpOptions o;
    o.connectTimeoutMs = opt.connectTimeoutMs;
    o.timeoutMs        = opt.operationTimeoutMs;
    o.credentials      = opt.credentials;

    const auto& cred = o.credentials;
    const bool bearer = !cred.token.empty() &&
        (cred.type == UltraNetAuthType::Bearer || cred.type == UltraNetAuthType::OAuth2);
    if (bearer) {
        o.authType = UltraNetAuthType::Bearer;
    } else if (!cred.username.empty()) {
        o.authType = UltraNetAuthType::Basic;
    } else if (!urlUser.empty()) {
        o.authType = UltraNetAuthType::Basic;
        o.credentials.type = UltraNetAuthType::Basic;
        o.credentials.username = urlUser;
        o.credentials.password = urlPass;
    }
    o.headers.Set("accept", "application/json");
    return o;
}

UltraNetResult MapHttpFailure(const UltraNetResult& r, const UltraNetResponse& resp,
                              const std::string& what) {
    if (!r) return r;    // transport-level failure already carries the cause
    UltraNetResultCode code = UltraNetResultCode::HttpError;
    if (resp.statusCode == 401) code = UltraNetResultCode::AuthenticationFailed;
    else if (resp.statusCode == 403) code = UltraNetResultCode::AccessDenied;
    else if (resp.statusCode == 404) code = UltraNetResultCode::NotFound;
    UltraNetResult out = UltraNetResult::Error(code,
        what + " failed with HTTP " + std::to_string(resp.statusCode));
    out.httpStatus = resp.statusCode;
    return out;
}

// ============================================================================
class JmapPlugin : public IMailboxProtocolPlugin {
public:
    std::string GetName() const override    { return "UltraNet-JMAP"; }
    std::string GetVersion() const override { return "0.3.0"; }
    std::vector<std::string> GetSupportedSchemes() const override { return {"jmap"}; }
    UltraNetResult Initialize(const UltraNetConfig&) override { return UltraNetResult::Ok(); }
    void Shutdown() override {
        // The watches first: each onEnd (Cancelled) runs on the worker and
        // must not find the plug-in half gone.
        std::vector<UltraNetHandle> open;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& [handle, watch] : watches_) open.push_back(handle);
        }
        for (UltraNetHandle handle : open) StopWatching(handle);
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_.clear();
        uidMaps_.clear();
        watches_.clear();
    }

    // ---- push ---------------------------------------------------------------

    UltraNetResult WatchMailbox(const std::string& serverUrl, const UltraNetMailOptions& options,
                                std::function<void(const UltraNetMailChange&)> onChange,
                                std::function<void(const UltraNetResult&)> onEnd,
                                UltraNetHandle& outWatch) override {
        outWatch = UltraNetInvalidHandle;
        JmapSession session;
        std::string user, pass;
        UltraNetResult r = GetSession(serverUrl, options, session, user, pass);
        if (!r) return r;
        if (session.eventSourceUrl.empty())
            return UltraNetResult::Error(UltraNetResultCode::Unsupported,
                                         "the JMAP server offers no event source (push)");

        auto watch = std::make_shared<Watch>();
        watch->accountId = session.accountId;
        watch->onChange  = std::move(onChange);
        watch->onEnd     = std::move(onEnd);

        UltraNetHttpRequest req;
        req.url     = BuildEventSourceUrl(ResolveAgainst(session.apiUrl, session.eventSourceUrl),
                                          kPingSeconds);
        req.method  = UltraNetHttpMethod::Get;
        req.options = MakeHttpOptions(options, user, pass);
        // The stream stays open as long as the server keeps it: no
        // whole-transfer limit, and a stall limit of two missed pings.
        req.options.timeoutMs      = -1;
        req.options.stallTimeoutMs = 2 * kPingSeconds * 1000;

        const UltraNetHandle handle = UltraNet_SseStreamAsync(
            req,
            [watch](const UltraNetSseEvent& e) { watch->OnEvent(e); },
            [this, watch](const UltraNetResult& result) {
                // Off the table before the caller hears, so a StopWatching
                // from inside onEnd finds nothing to cancel.
                watch->ended.store(true);
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    for (auto it = watches_.begin(); it != watches_.end(); ++it)
                        if (it->second == watch) { watches_.erase(it); break; }
                }
                if (watch->onEnd) watch->onEnd(result);
            });
        if (handle == UltraNetInvalidHandle)
            return UltraNetResult::Error(UltraNetResultCode::PluginError,
                                         "the event source stream could not be opened");
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // The stream may have ended before the handle was known: then
            // there is nothing to keep.
            if (!watch->ended.load()) watches_[handle] = watch;
        }
        outWatch = handle;
        return UltraNetResult::Ok();
    }

    void StopWatching(UltraNetHandle watch) override {
        bool known = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            known = watches_.count(watch) != 0;
        }
        if (known) UltraNet_CancelRequest(watch);   // onComplete erases it and ends the watch
    }

    // ---- IMailProtocolPlugin ----------------------------------------------

    // Upload the raw message, import it and submit it in one request. The
    // server URL (options.serverUrl, as the SMTP plug-in reads it) is the
    // jmap:// URL of the account's incoming server - one account, one
    // protocol - with the same credentials.
    UltraNetResult SendMail(const UltraNetMailMessage& message,
                            const UltraNetMailOptions& options) override {
        if (options.serverUrl.empty())
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                         "SendMail needs the jmap:// server URL in options.serverUrl");
        std::vector<std::string> recipients;
        recipients.insert(recipients.end(), message.to.begin(), message.to.end());
        recipients.insert(recipients.end(), message.cc.begin(), message.cc.end());
        recipients.insert(recipients.end(), message.bcc.begin(), message.bcc.end());
        if (recipients.empty())
            return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                         "the message has no recipient");

        JmapSession session;
        std::string user, pass;
        UltraNetResult r = GetSession(options.serverUrl, options, session, user, pass);
        if (!r) return r;

        // The identity to submit as (the From address, or the account's first).
        std::string body;
        r = ApiPost(session, options, user, pass, BuildIdentityGetRequest(session.accountId), body);
        if (!r) return r;
        json args;
        std::string err;
        if (!FindMethodResponse(body, "Identity/get", args, err))
            return UltraNetResult::Error(UltraNetResultCode::PluginError,
                                         "Identity/get: " + err);
        std::vector<JmapIdentity> identities;
        ParseIdentities(args, identities);
        const JmapIdentity* identity = PickIdentity(identities, message.from);
        if (!identity)
            return UltraNetResult::Error(UltraNetResultCode::AccessDenied,
                                         "the account has no sending identity");

        // Where the message is held while it is submitted, and filed after.
        std::vector<JmapMailbox> mailboxes;
        r = GetMailboxes(session, options, user, pass, mailboxes);
        if (!r) return r;
        const JmapMailbox* sent   = FindMailboxByRole(mailboxes, "sent");
        const JmapMailbox* drafts = FindMailboxByRole(mailboxes, "drafts");
        const JmapMailbox* hold   = drafts ? drafts : sent ? sent : FindMailboxByRole(mailboxes, "inbox");
        if (!hold && !mailboxes.empty()) hold = &mailboxes.front();
        if (!hold)
            return UltraNetResult::Error(UltraNetResultCode::NotFound,
                                         "the account has no mailbox to hold the message");

        std::string blobId;
        r = UploadBlob(session, options, user, pass, UltraNet_MimeBuildMailMessage(message), blobId);
        if (!r) return r;

        r = ApiPost(session, options, user, pass,
                    BuildSendRequest(session.accountId, blobId, hold->id,
                                     sent ? sent->id : std::string(), identity->id,
                                     message.from, recipients),
                    body);
        if (!r) return r;
        if (!FindMethodResponse(body, "Email/import", args, err))
            return UltraNetResult::Error(UltraNetResultCode::PluginError, "Email/import: " + err);
        if (SetErrorFor(args, "notCreated", "m", err))
            return UltraNetResult::Error(UltraNetResultCode::PluginError, "Email/import: " + err);
        if (!FindMethodResponse(body, "EmailSubmission/set", args, err))
            return UltraNetResult::Error(UltraNetResultCode::PluginError,
                                         "EmailSubmission/set: " + err);
        if (SetErrorFor(args, "notCreated", "s", err))
            return UltraNetResult::Error(MapSubmissionError(err), "the server refused the submission: " + err);
        if (CreatedId(args, "s").empty())
            return UltraNetResult::Error(UltraNetResultCode::PluginError,
                                         "EmailSubmission/set reported no submission");
        return UltraNetResult::Ok();
    }

    UltraNetResult FetchMessages(const std::string& url,
                                 std::vector<UltraNetMailMessage>& outMessages,
                                 const UltraNetMailOptions& options) override {
        outMessages.clear();

        UltraNetUrlComponents c;
        if (!UltraNet_ParseUrl(url, c) || (c.scheme != "jmap" && c.scheme != "https"))
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                         "expected jmap:// (or https:// session) URL");
        std::string folder = c.path;
        if (!folder.empty() && folder.front() == '/') folder.erase(0, 1);
        // For jmap:// URLs the path selects the folder; https:// session URLs
        // carry no folder, so both default to the inbox.
        if (c.scheme == "https") folder.clear();

        JmapSession session;
        std::string user, pass;
        std::string serverUrl = url;
        if (c.scheme == "jmap" && !folder.empty()) {
            // Strip the folder path so the session resolves at /.well-known/jmap.
            std::ostringstream os;
            os << "jmap://" << c.host;
            if (c.port > 0) os << ':' << c.port;
            os << '/';
            serverUrl = os.str();
        }
        UltraNetResult r = GetSession(serverUrl, options, session, user, pass);
        if (!r) return r;

        std::vector<JmapMailbox> mailboxes;
        r = GetMailboxes(session, options, user, pass, mailboxes);
        if (!r) return r;
        const JmapMailbox* box = FindMailboxByPath(mailboxes, folder);
        if (!box) return UltraNetResult::Error(UltraNetResultCode::NotFound,
                                               "no such mailbox: " + folder);

        std::vector<std::string> ids;
        r = QueryAllIds(session, options, user, pass, box->id, ids);
        if (!r) return r;
        if (ids.empty()) return UltraNetResult::Ok();

        // Most-recent N, kept in ascending order (same shape as the IMAP/POP3
        // bulk fetch).
        if (options.maxMessages > 0 && static_cast<int>(ids.size()) > options.maxMessages)
            ids.erase(ids.begin(), ids.end() - options.maxMessages);

        for (std::size_t off = 0; off < ids.size(); off += kEmailGetChunk) {
            std::vector<std::string> chunk(
                ids.begin() + off,
                ids.begin() + std::min(ids.size(), off + kEmailGetChunk));
            std::string body;
            r = ApiPost(session, options, user, pass,
                        BuildEmailGetBodyRequest(session.accountId, chunk), body);
            if (!r) return r;
            json args;
            std::string err;
            if (!FindMethodResponse(body, "Email/get", args, err))
                return UltraNetResult::Error(UltraNetResultCode::PluginError, err);
            if (!args.contains("list") || !args["list"].is_array()) continue;
            for (const auto& m : args["list"]) {
                JmapEnvelope e;
                ParseEmailObject(m, e);
                UltraNetMailMessage msg;
                msg.from    = e.env.from;
                msg.to      = e.env.to;
                msg.cc      = e.env.cc;
                msg.subject = e.env.subject;
                if (!e.env.messageId.empty()) msg.headers["Message-ID"] = e.env.messageId;
                if (!e.env.date.empty())      msg.headers["Date"] = e.env.date;
                ExtractBestBody(m, msg.body, msg.contentType);
                outMessages.push_back(std::move(msg));
            }
        }
        return UltraNetResult::Ok();
    }

    // ---- IMailboxProtocolPlugin -------------------------------------------

    UltraNetResult ListFolders(const std::string& serverUrl,
                               std::vector<UltraNetMailFolder>& outFolders,
                               const UltraNetMailOptions& options) override {
        outFolders.clear();
        JmapSession session;
        std::string user, pass;
        UltraNetResult r = GetSession(serverUrl, options, session, user, pass);
        if (!r) return r;
        std::vector<JmapMailbox> mailboxes;
        r = GetMailboxes(session, options, user, pass, mailboxes);
        if (!r) return r;
        BuildFolderList(mailboxes, outFolders);
        return UltraNetResult::Ok();
    }

    UltraNetResult GetMailboxStatus(const std::string& serverUrl,
                                    const std::string& folder,
                                    UltraNetMailboxStatus& outStatus,
                                    const UltraNetMailOptions& options) override {
        outStatus = {};
        JmapSession session;
        std::string user, pass;
        UltraNetResult r = GetSession(serverUrl, options, session, user, pass);
        if (!r) return r;
        std::vector<JmapMailbox> mailboxes;
        r = GetMailboxes(session, options, user, pass, mailboxes);
        if (!r) return r;
        const JmapMailbox* box = FindMailboxByPath(mailboxes, folder);
        if (!box) return UltraNetResult::Error(UltraNetResultCode::NotFound,
                                               "no such mailbox: " + folder);

        outStatus.messages    = box->totalEmails;
        outStatus.unseen      = box->unreadEmails;
        outStatus.uidValidity = Fnv1a32(session.accountId + "/" + box->id);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = uidMaps_.find(UidKey(session, folder));
            const uint32_t highest =
                it != uidMaps_.end() ? it->second.HighestUid() : box->totalEmails;
            outStatus.uidNext = highest + 1;
        }
        return UltraNetResult::Ok();
    }

    UltraNetResult FetchEnvelopes(const std::string& serverUrl,
                                  const std::string& folder,
                                  uint32_t sinceUid,
                                  std::vector<UltraNetMailEnvelope>& outEnvelopes,
                                  const UltraNetMailOptions& options) override {
        outEnvelopes.clear();
        JmapSession session;
        std::string user, pass;
        UltraNetResult r = GetSession(serverUrl, options, session, user, pass);
        if (!r) return r;

        std::vector<std::pair<std::string, uint32_t>> wanted; // (id, uid), ascending
        r = MapFolderUids(session, options, user, pass, folder, sinceUid, wanted);
        if (!r) return r;
        if (wanted.empty()) return UltraNetResult::Ok();

        // Newest first, bounded by maxMessages (interface contract).
        std::reverse(wanted.begin(), wanted.end());
        if (options.maxMessages > 0 &&
            static_cast<int>(wanted.size()) > options.maxMessages)
            wanted.resize(options.maxMessages);

        std::map<std::string, uint32_t> uidById(wanted.begin(), wanted.end());
        for (std::size_t off = 0; off < wanted.size(); off += kEmailGetChunk) {
            std::vector<std::string> chunk;
            for (std::size_t i = off; i < std::min(wanted.size(), off + kEmailGetChunk); ++i)
                chunk.push_back(wanted[i].first);
            std::string body;
            r = ApiPost(session, options, user, pass,
                        BuildEmailGetRequest(session.accountId, chunk, EnvelopeProperties()),
                        body);
            if (!r) return r;
            json args;
            std::string err;
            if (!FindMethodResponse(body, "Email/get", args, err))
                return UltraNetResult::Error(UltraNetResultCode::PluginError, err);
            std::vector<JmapEnvelope> envelopes;
            ParseEmailEnvelopes(args, envelopes);
            for (auto& e : envelopes) {
                e.env.uid = uidById.count(e.id) ? uidById[e.id] : 0;
                if (e.env.uid) outEnvelopes.push_back(std::move(e.env));
            }
        }
        // Email/get returns objects in arbitrary order — restore newest first.
        std::sort(outEnvelopes.begin(), outEnvelopes.end(),
                  [](const UltraNetMailEnvelope& a, const UltraNetMailEnvelope& b) {
                      return a.uid > b.uid;
                  });
        return UltraNetResult::Ok();
    }

    UltraNetResult FetchMessage(const std::string& serverUrl,
                                const std::string& folder,
                                uint32_t uid,
                                std::string& outRaw,
                                const UltraNetMailOptions& options) override {
        outRaw.clear();
        JmapSession session;
        std::string user, pass;
        UltraNetResult r = GetSession(serverUrl, options, session, user, pass);
        if (!r) return r;

        std::string emailId = LookupId(session, folder, uid);
        if (emailId.empty()) {
            // Unknown uid — (re)build the folder's uid map, then retry.
            std::vector<std::pair<std::string, uint32_t>> all;
            r = MapFolderUids(session, options, user, pass, folder, 0, all);
            if (!r) return r;
            emailId = LookupId(session, folder, uid);
            if (emailId.empty())
                return UltraNetResult::Error(UltraNetResultCode::NotFound,
                    "no message with uid " + std::to_string(uid) + " in " + folder);
        }

        // blobId of the raw message …
        std::string body;
        r = ApiPost(session, options, user, pass,
                    BuildEmailGetRequest(session.accountId, {emailId},
                                         json::array({"id", "blobId"})), body);
        if (!r) return r;
        json args;
        std::string err;
        if (!FindMethodResponse(body, "Email/get", args, err))
            return UltraNetResult::Error(UltraNetResultCode::PluginError, err);
        std::vector<JmapEnvelope> envelopes;
        ParseEmailEnvelopes(args, envelopes);
        if (envelopes.empty() || envelopes.front().blobId.empty())
            return UltraNetResult::Error(UltraNetResultCode::NotFound,
                                         "server returned no blobId for " + emailId);

        // … then the blob itself via the session's downloadUrl template.
        const std::string dlUrl = ExpandUrlTemplate(
            ResolveAgainst(session.apiUrl, session.downloadUrl),
            {{"accountId", session.accountId},
             {"blobId", envelopes.front().blobId},
             {"type", "message/rfc822"},
             {"name", "message"}});
        UltraNetResponse resp;
        r = UltraNet_HttpGet(dlUrl, resp, MakeHttpOptions(options, user, pass));
        if (!r || !resp.IsSuccess()) return MapHttpFailure(r, resp, "blob download");
        outRaw = resp.GetBodyAsString();
        return UltraNetResult::Ok();
    }

    UltraNetResult StoreFlags(const std::string& serverUrl, const std::string& folder,
                              uint32_t uid, UltraNetMailFlags flags, bool set,
                              const UltraNetMailOptions& options) override {
        JmapSession session;
        std::string user, pass;
        UltraNetResult r = GetSession(serverUrl, options, session, user, pass);
        if (!r) return r;
        std::string emailId;
        r = ResolveEmailId(session, options, user, pass, folder, uid, emailId);
        if (!r) return r;

        // \Deleted: to the Trash mailbox, or gone for good when it is there
        // already (or there is none). Clearing it has nothing to undo.
        if (UltraNetHasFlag(flags, UltraNetMailFlags::Deleted)) {
            if (set) {
                std::vector<JmapMailbox> mailboxes;
                r = GetMailboxes(session, options, user, pass, mailboxes);
                if (!r) return r;
                const JmapMailbox* trash = FindMailboxByRole(mailboxes, "trash");
                const JmapMailbox* here  = FindMailboxByPath(mailboxes, folder);
                if (trash && (!here || here->id != trash->id))
                    r = SetMailbox(session, options, user, pass, emailId, trash->id);
                else
                    r = Destroy(session, options, user, pass, emailId);
                if (!r) return r;
            }
            flags = flags & static_cast<UltraNetMailFlags>(
                ~static_cast<uint32_t>(UltraNetMailFlags::Deleted));
        }
        if (KeywordPatch(flags, set).empty()) return UltraNetResult::Ok();

        std::string body;
        r = ApiPost(session, options, user, pass,
                    BuildEmailSetKeywordsRequest(session.accountId, emailId, flags, set), body);
        if (!r) return r;
        return CheckUpdated(body, emailId);
    }

    UltraNetResult MoveMessage(const std::string& serverUrl, const std::string& srcFolder,
                               uint32_t uid, const std::string& dstFolder,
                               const UltraNetMailOptions& options) override {
        JmapSession session;
        std::string user, pass;
        UltraNetResult r = GetSession(serverUrl, options, session, user, pass);
        if (!r) return r;
        std::string emailId;
        r = ResolveEmailId(session, options, user, pass, srcFolder, uid, emailId);
        if (!r) return r;
        std::vector<JmapMailbox> mailboxes;
        r = GetMailboxes(session, options, user, pass, mailboxes);
        if (!r) return r;
        const JmapMailbox* dst = FindMailboxByPath(mailboxes, dstFolder);
        if (!dst) return UltraNetResult::Error(UltraNetResultCode::NotFound,
                                               "no such mailbox: " + dstFolder);
        return SetMailbox(session, options, user, pass, emailId, dst->id);
    }

    UltraNetResult AppendMessage(const std::string& serverUrl, const std::string& folder,
                                 const std::string& rawMessage, UltraNetMailFlags flags,
                                 const UltraNetMailOptions& options) override {
        JmapSession session;
        std::string user, pass;
        UltraNetResult r = GetSession(serverUrl, options, session, user, pass);
        if (!r) return r;
        std::vector<JmapMailbox> mailboxes;
        r = GetMailboxes(session, options, user, pass, mailboxes);
        if (!r) return r;
        const JmapMailbox* box = FindMailboxByPath(mailboxes, folder);
        if (!box) return UltraNetResult::Error(UltraNetResultCode::NotFound,
                                               "no such mailbox: " + folder);
        std::string blobId;
        r = UploadBlob(session, options, user, pass, rawMessage, blobId);
        if (!r) return r;
        std::string body;
        r = ApiPost(session, options, user, pass,
                    BuildEmailImportRequest(session.accountId, "m", blobId, box->id, flags), body);
        if (!r) return r;
        json args;
        std::string err;
        if (!FindMethodResponse(body, "Email/import", args, err))
            return UltraNetResult::Error(UltraNetResultCode::PluginError, "Email/import: " + err);
        if (SetErrorFor(args, "notCreated", "m", err))
            return UltraNetResult::Error(UltraNetResultCode::PluginError, "Email/import: " + err);
        const std::string created = CreatedId(args, "m");
        if (created.empty())
            return UltraNetResult::Error(UltraNetResultCode::PluginError,
                                         "Email/import reported nothing created");
        // The new message gets the folder's next uid now, so a fetch by uid
        // finds it without walking the folder again.
        std::lock_guard<std::mutex> lock(mutex_);
        uidMaps_[UidKey(session, folder)].Assign(created);
        return UltraNetResult::Ok();
    }

    UltraNetResult ExpungeMessage(const std::string& serverUrl, const std::string& folder,
                                  uint32_t uid, const UltraNetMailOptions& options) override {
        JmapSession session;
        std::string user, pass;
        UltraNetResult r = GetSession(serverUrl, options, session, user, pass);
        if (!r) return r;
        std::string emailId;
        r = ResolveEmailId(session, options, user, pass, folder, uid, emailId);
        if (!r) return r;
        return Destroy(session, options, user, pass, emailId);
    }

    // Every live message of the folder with its keywords: the walk that
    // assigns the uids, then Email/get of the keywords in chunks.
    UltraNetResult FetchAllFlags(
        const std::string& serverUrl, const std::string& folder,
        const std::function<void(uint32_t uid, UltraNetMailFlags flags, bool flagsKnown)>& onFlags,
        const UltraNetMailOptions& options) override {
        JmapSession session;
        std::string user, pass;
        UltraNetResult r = GetSession(serverUrl, options, session, user, pass);
        if (!r) return r;
        std::vector<std::pair<std::string, uint32_t>> all;
        r = MapFolderUids(session, options, user, pass, folder, 0, all);
        if (!r) return r;
        std::map<std::string, uint32_t> uidById(all.begin(), all.end());
        for (std::size_t off = 0; off < all.size(); off += kEmailGetChunk) {
            std::vector<std::string> chunk;
            for (std::size_t i = off; i < std::min(all.size(), off + kEmailGetChunk); ++i)
                chunk.push_back(all[i].first);
            std::string body;
            r = ApiPost(session, options, user, pass,
                        BuildEmailGetRequest(session.accountId, chunk,
                                             json::array({"id", "keywords"})), body);
            if (!r) return r;
            json args;
            std::string err;
            if (!FindMethodResponse(body, "Email/get", args, err))
                return UltraNetResult::Error(UltraNetResultCode::PluginError, err);
            std::vector<JmapEnvelope> envelopes;
            ParseEmailEnvelopes(args, envelopes);
            std::map<std::string, UltraNetMailFlags> flagsById;
            for (const auto& e : envelopes) flagsById[e.id] = e.env.flags;
            // A message the walk listed but Email/get did not answer for is
            // still live: reported with its flags unknown.
            for (const auto& id : chunk) {
                auto f = flagsById.find(id);
                onFlags(uidById[id], f == flagsById.end() ? UltraNetMailFlags::None : f->second,
                        f != flagsById.end());
            }
        }
        return UltraNetResult::Ok();
    }

private:
    // One open event-source stream.
    struct Watch {
        std::string accountId;
        std::function<void(const UltraNetMailChange&)> onChange;
        std::function<void(const UltraNetResult&)> onEnd;
        std::atomic<bool> ended{false};
        std::mutex m;
        std::map<std::string, std::string> states;   // type -> last state seen
        bool primed = false;

        // A "state" event: the first one only records where the account
        // stands (the caller's own sync covers what came before the watch);
        // from then on a state the watch has not seen is a change.
        void OnEvent(const UltraNetSseEvent& e) {
            if (!e.event.empty() && e.event != "state") return;   // ping, comment
            std::map<std::string, std::string> now;
            if (!ParseStateChange(e.data, accountId, now)) return;
            std::vector<std::string> changed;
            {
                std::lock_guard<std::mutex> lock(m);
                changed = primed ? ChangedTypes(states, now) : std::vector<std::string>{};
                for (const auto& [type, state] : now) states[type] = state;
                primed = true;
            }
            if (changed.empty() || !onChange) return;
            UltraNetMailChange change;
            change.mailboxesChanged =
                std::find(changed.begin(), changed.end(), "Mailbox") != changed.end();
            onChange(change);
        }
    };

    // The id of the message `uid` names in `folder`, (re)walking the folder
    // when the map does not know it yet (a fresh process, or a message that
    // arrived since).
    UltraNetResult ResolveEmailId(const JmapSession& session, const UltraNetMailOptions& options,
                                  const std::string& user, const std::string& pass,
                                  const std::string& folder, uint32_t uid, std::string& outId) {
        outId = LookupId(session, folder, uid);
        if (!outId.empty()) return UltraNetResult::Ok();
        std::vector<std::pair<std::string, uint32_t>> all;
        UltraNetResult r = MapFolderUids(session, options, user, pass, folder, 0, all);
        if (!r) return r;
        outId = LookupId(session, folder, uid);
        if (outId.empty())
            return UltraNetResult::Error(UltraNetResultCode::NotFound,
                "no message with uid " + std::to_string(uid) + " in " + folder);
        return UltraNetResult::Ok();
    }

    UltraNetResult SetMailbox(const JmapSession& session, const UltraNetMailOptions& options,
                              const std::string& user, const std::string& pass,
                              const std::string& emailId, const std::string& mailboxId) {
        std::string body;
        UltraNetResult r = ApiPost(session, options, user, pass,
                                   BuildEmailSetMailboxRequest(session.accountId, emailId, mailboxId),
                                   body);
        if (!r) return r;
        return CheckUpdated(body, emailId);
    }

    UltraNetResult Destroy(const JmapSession& session, const UltraNetMailOptions& options,
                           const std::string& user, const std::string& pass,
                           const std::string& emailId) {
        std::string body;
        UltraNetResult r = ApiPost(session, options, user, pass,
                                   BuildEmailDestroyRequest(session.accountId, {emailId}), body);
        if (!r) return r;
        json args;
        std::string err;
        if (!FindMethodResponse(body, "Email/set", args, err))
            return UltraNetResult::Error(UltraNetResultCode::PluginError, "Email/set: " + err);
        // Already gone is as good as destroyed.
        if (SetErrorFor(args, "notDestroyed", emailId, err) && err.rfind("notFound", 0) != 0)
            return UltraNetResult::Error(UltraNetResultCode::PluginError, "Email/set destroy: " + err);
        return UltraNetResult::Ok();
    }

    // The verdict of an Email/set update of `emailId`.
    static UltraNetResult CheckUpdated(const std::string& body, const std::string& emailId) {
        json args;
        std::string err;
        if (!FindMethodResponse(body, "Email/set", args, err))
            return UltraNetResult::Error(UltraNetResultCode::PluginError, "Email/set: " + err);
        if (SetErrorFor(args, "notUpdated", emailId, err))
            return UltraNetResult::Error(err.rfind("notFound", 0) == 0 ? UltraNetResultCode::NotFound
                                                                        : UltraNetResultCode::PluginError,
                                         "Email/set update: " + err);
        return UltraNetResult::Ok();
    }

    // POST the raw message to the session's uploadUrl; `outBlobId` names it
    // for Email/import.
    UltraNetResult UploadBlob(const JmapSession& session, const UltraNetMailOptions& options,
                              const std::string& user, const std::string& pass,
                              const std::string& raw, std::string& outBlobId) {
        UltraNetHttpRequest req;
        req.url     = ExpandUrlTemplate(ResolveAgainst(session.apiUrl, session.uploadUrl),
                                        {{"accountId", session.accountId}});
        req.method  = UltraNetHttpMethod::Post;
        req.body.assign(raw.begin(), raw.end());
        req.options = MakeHttpOptions(options, user, pass);
        req.headers.Set("content-type", "message/rfc822");
        UltraNetResponse resp;
        UltraNetResult r = UltraNet_HttpRequest(req, resp);
        if (!r || !resp.IsSuccess()) return MapHttpFailure(r, resp, "blob upload");
        std::string err;
        if (!ParseUploadResponse(resp.GetBodyAsString(), outBlobId, err))
            return UltraNetResult::Error(UltraNetResultCode::PluginError, err);
        return UltraNetResult::Ok();
    }


    // A SetError type of EmailSubmission/set as a result code (RFC 8621 §7.5).
    static UltraNetResultCode MapSubmissionError(const std::string& err) {
        if (err.rfind("forbiddenFrom", 0) == 0 || err.rfind("forbiddenToSend", 0) == 0)
            return UltraNetResultCode::AccessDenied;
        if (err.rfind("invalidRecipients", 0) == 0 || err.rfind("invalidEmail", 0) == 0)
            return UltraNetResultCode::InvalidState;
        if (err.rfind("tooManyRecipients", 0) == 0 || err.rfind("tooLarge", 0) == 0 ||
            err.rfind("rateLimit", 0) == 0)
            return UltraNetResultCode::PluginError;
        return UltraNetResultCode::PluginError;
    }

    // Fetch (or reuse) the session resource for a server URL.
    UltraNetResult GetSession(const std::string& serverUrl,
                              const UltraNetMailOptions& options,
                              JmapSession& out, std::string& outUser, std::string& outPass) {
        std::string sessionUrl;
        if (!ResolveSessionUrl(serverUrl, sessionUrl, outUser, outPass))
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                "expected jmap://host/ (or https:// session) URL");

        const std::string user =
            !options.credentials.username.empty() ? options.credentials.username : outUser;
        const std::string key = sessionUrl + "|" + user;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = sessions_.find(key);
            if (it != sessions_.end()) { out = it->second; return UltraNetResult::Ok(); }
        }

        UltraNetResponse resp;
        UltraNetResult r = UltraNet_HttpGet(sessionUrl, resp,
                                            MakeHttpOptions(options, outUser, outPass));
        if (!r || !resp.IsSuccess()) return MapHttpFailure(r, resp, "JMAP session fetch");

        JmapSession session;
        std::string err;
        if (!ParseSession(resp.GetBodyAsString(), session, err))
            return UltraNetResult::Error(UltraNetResultCode::PluginError,
                                         "invalid JMAP session resource: " + err);
        const std::string base = resp.finalUrl.empty() ? sessionUrl : resp.finalUrl;
        session.apiUrl      = ResolveAgainst(base, session.apiUrl);
        session.downloadUrl = ResolveAgainst(base, session.downloadUrl);
        session.uploadUrl   = ResolveAgainst(base, session.uploadUrl);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            sessions_[key] = session;
        }
        out = session;
        return UltraNetResult::Ok();
    }

    // One JMAP API POST; `outBody` receives the raw response JSON.
    UltraNetResult ApiPost(const JmapSession& session, const UltraNetMailOptions& options,
                           const std::string& user, const std::string& pass,
                           const std::string& requestJson, std::string& outBody) {
        UltraNetHttpRequest req;
        req.url     = session.apiUrl;
        req.method  = UltraNetHttpMethod::Post;
        req.body.assign(requestJson.begin(), requestJson.end());
        req.options = MakeHttpOptions(options, user, pass);
        req.headers.Set("content-type", "application/json");
        UltraNetResponse resp;
        UltraNetResult r = UltraNet_HttpRequest(req, resp);
        if (!r || !resp.IsSuccess()) return MapHttpFailure(r, resp, "JMAP API call");
        outBody = resp.GetBodyAsString();
        return UltraNetResult::Ok();
    }

    UltraNetResult GetMailboxes(const JmapSession& session, const UltraNetMailOptions& options,
                                const std::string& user, const std::string& pass,
                                std::vector<JmapMailbox>& out) {
        std::string body;
        UltraNetResult r = ApiPost(session, options, user, pass,
                                   BuildMailboxGetRequest(session.accountId), body);
        if (!r) return r;
        json args;
        std::string err;
        if (!FindMethodResponse(body, "Mailbox/get", args, err))
            return UltraNetResult::Error(UltraNetResultCode::PluginError, err);
        if (!ParseMailboxes(args, out))
            return UltraNetResult::Error(UltraNetResultCode::PluginError,
                                         "malformed Mailbox/get response");
        return UltraNetResult::Ok();
    }

    // Page through Email/query (ascending receivedAt) collecting every id.
    UltraNetResult QueryAllIds(const JmapSession& session, const UltraNetMailOptions& options,
                               const std::string& user, const std::string& pass,
                               const std::string& mailboxId, std::vector<std::string>& out) {
        out.clear();
        int position = 0;
        while (static_cast<int>(out.size()) < kQueryMaxIds) {
            std::string body;
            UltraNetResult r = ApiPost(session, options, user, pass,
                BuildEmailQueryRequest(session.accountId, mailboxId, position, kQueryPageSize),
                body);
            if (!r) return r;
            json args;
            std::string err;
            if (!FindMethodResponse(body, "Email/query", args, err))
                return UltraNetResult::Error(UltraNetResultCode::PluginError, err);
            std::vector<std::string> page;
            if (!ParseQueryIds(args, page))
                return UltraNetResult::Error(UltraNetResultCode::PluginError,
                                             "malformed Email/query response");
            out.insert(out.end(), page.begin(), page.end());
            if (static_cast<int>(page.size()) < kQueryPageSize) break;
            position += static_cast<int>(page.size());
        }
        return UltraNetResult::Ok();
    }

    // Run the folder's id walk through its UidMap; returns ids with uid >
    // sinceUid in ascending-uid order.
    UltraNetResult MapFolderUids(const JmapSession& session, const UltraNetMailOptions& options,
                                 const std::string& user, const std::string& pass,
                                 const std::string& folder, uint32_t sinceUid,
                                 std::vector<std::pair<std::string, uint32_t>>& out) {
        out.clear();
        std::vector<JmapMailbox> mailboxes;
        UltraNetResult r = GetMailboxes(session, options, user, pass, mailboxes);
        if (!r) return r;
        const JmapMailbox* box = FindMailboxByPath(mailboxes, folder);
        if (!box) return UltraNetResult::Error(UltraNetResultCode::NotFound,
                                               "no such mailbox: " + folder);

        std::vector<std::string> ids;
        r = QueryAllIds(session, options, user, pass, box->id, ids);
        if (!r) return r;

        std::lock_guard<std::mutex> lock(mutex_);
        UidMap& map = uidMaps_[UidKey(session, folder)];
        for (const auto& id : ids) {
            const uint32_t uid = map.Assign(id);
            if (uid > sinceUid) out.emplace_back(id, uid);
        }
        std::sort(out.begin(), out.end(),
                  [](const auto& a, const auto& b) { return a.second < b.second; });
        return UltraNetResult::Ok();
    }

    static std::string UidKey(const JmapSession& session, const std::string& folder) {
        return session.accountId + "|" + folder;
    }

    std::string LookupId(const JmapSession& session, const std::string& folder, uint32_t uid) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = uidMaps_.find(UidKey(session, folder));
        return it == uidMaps_.end() ? std::string{} : it->second.IdFor(uid);
    }

    std::mutex mutex_;
    std::map<std::string, JmapSession> sessions_;  // (sessionUrl|user) → session
    std::map<std::string, UidMap>      uidMaps_;   // (accountId|folder) → uid map
    std::map<UltraNetHandle, std::shared_ptr<Watch>> watches_;   // open event-source streams
};

} // namespace

// v2 entry — preferred, works on Windows (host-vtable injection).
extern "C" ULTRANET_PLUGIN_EXPORT
void UltraNet_PluginInit(const UltraNetPluginHost* host) {
    // ABI 2: the core functions this plug-in calls come through `host`
    // (UltraNetPluginHostShim); an older host cannot serve them.
    if (!UltraNetPlugin_AttachHost(host)) return;
    host->RegisterPlugin(std::make_shared<JmapPlugin>());
}

