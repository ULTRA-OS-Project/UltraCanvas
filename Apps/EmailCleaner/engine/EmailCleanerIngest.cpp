// Apps/EmailCleaner/engine/EmailCleanerIngest.cpp
// Parsing (over UltraNet's MIME reader), classification and the walk over
// UltraMail's cached message bodies.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "EmailCleanerIngest.h"

#include "EmailCleanerUnsubscribe.h"

#include <UltraNet/UltraNetMime.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace EmailCleaner {

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Case-insensitive lookup in the parsed top-level header map. UltraNet keeps
// header names as received, so a caller cannot assume any particular case.
std::string Header(const std::map<std::string, std::string>& headers,
                   const std::string& name) {
    const std::string wanted = Lower(name);
    for (const auto& [key, value] : headers) {
        if (Lower(key) == wanted) return value;
    }
    return "";
}

bool HasHeader(const std::map<std::string, std::string>& headers,
               const std::string& name) {
    const std::string wanted = Lower(name);
    for (const auto& [key, value] : headers) {
        if (Lower(key) == wanted) return true;
    }
    return false;
}

// RFC 3834 / RFC 2919 / RFC 2369: the headers that mark machine-sent bulk mail.
bool LooksBulk(const std::map<std::string, std::string>& headers) {
    if (HasHeader(headers, "List-Unsubscribe")) return true;
    if (HasHeader(headers, "List-Id")) return true;
    if (HasHeader(headers, "List-Post")) return true;
    if (HasHeader(headers, "Auto-Submitted")) {
        const std::string value = Lower(Header(headers, "Auto-Submitted"));
        if (!value.empty() && value != "no") return true;
    }
    const std::string precedence = Lower(Header(headers, "Precedence"));
    if (precedence == "bulk" || precedence == "list" || precedence == "junk") return true;
    if (!Header(headers, "X-Campaign-Id").empty()) return true;
    if (!Header(headers, "X-Mailer-Lid").empty()) return true;
    return false;
}

bool AddressedTo(const std::vector<std::string>& recipients,
                 const std::vector<std::string>& carbonCopies,
                 const std::string& ownerAddress) {
    if (ownerAddress.empty()) return true;   // unknown owner: do not penalise
    const std::string owner = Lower(ownerAddress);
    auto matches = [&owner](const std::vector<std::string>& list) {
        for (const std::string& entry : list) {
            std::string name, address;
            ParseAddress(entry, name, address);
            if (address == owner) return true;
        }
        return false;
    };
    return matches(recipients) || matches(carbonCopies);
}

} // namespace

void IngestStats::Add(const IngestStats& other) {
    filesSeen   += other.filesSeen;
    analysed    += other.analysed;
    stored      += other.stored;
    skipped     += other.skipped;
    failed      += other.failed;
    unwanted    += other.unwanted;
    attachments += other.attachments;
}

bool ReadFileBytes(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

int64_t UidFromFileName(const std::string& fileName) {
    const size_t dot = fileName.rfind('.');
    const std::string stem = (dot == std::string::npos) ? fileName : fileName.substr(0, dot);
    if (stem.empty()) return 0;
    for (char c : stem) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return 0;
    }
    return std::strtoll(stem.c_str(), nullptr, 10);
}

// ---- Analysis --------------------------------------------------------------

AnalyzedMessage Ingestor::Analyze(const std::string& rawMessage,
                                  const std::string& accountId,
                                  const std::string& folder,
                                  int64_t uid,
                                  const std::string& ownerAddress) const {
    AnalyzedMessage message;
    message.accountId = accountId;
    message.folder    = folder;
    message.uid       = uid;
    message.sizeBytes = static_cast<int64_t>(rawMessage.size());

    UltraNetMimeMessage parsed;
    if (!UltraNet_MimeParse(rawMessage, parsed)) {
        // Not a MIME message at all — keep the row so the sender still shows
        // up on the map, but there is nothing to classify.
        return message;
    }

    message.messageId = parsed.messageId;
    message.subject   = parsed.subject;
    ParseAddress(parsed.from, message.senderName, message.senderAddr);
    message.senderDomain = DomainOf(message.senderAddr);

    if (!ParseRfc5322Date(parsed.date, message.date)) {
        // No usable Date: header. Leave it at 0 — the timetable and timeline
        // skip undated messages rather than putting them all on the epoch.
        message.date = 0;
    }

    const std::map<std::string, std::string>& headers = parsed.root.headers;
    message.automated = LooksBulk(headers);

    std::string replyToName, replyToAddr;
    ParseAddress(Header(headers, "Reply-To"), replyToName, replyToAddr);

    // The sender's unsubscribe offer, captured now so acting on it later does
    // not need the raw message back.
    const UnsubscribeInfo unsub = ParseListUnsubscribe(Header(headers, "List-Unsubscribe"));
    message.unsubMailto        = unsub.mailto;
    message.unsubMailtoSubject = unsub.mailtoSubject;
    message.unsubUrl           = unsub.httpUrl;
    message.unsubOneClick =
        ParseListUnsubscribePost(Header(headers, "List-Unsubscribe-Post")) &&
        !unsub.httpUrl.empty();

    std::string body;
    bool bodyIsHtml = false;
    UltraNet_MimeGetDisplayBody(parsed, body, bodyIsHtml);

    std::vector<UltraNetMimeAttachmentView> parts;
    UltraNet_MimeCollectAttachments(parsed, parts, /*includeInline=*/true);
    message.attachments.reserve(parts.size());
    for (const UltraNetMimeAttachmentView& part : parts) {
        AttachmentRecord record;
        record.filename  = part.filename;
        record.mediaType = Lower(part.mediaType);
        record.sizeBytes = static_cast<int64_t>(part.data.size());
        record.isInline  = part.isInline;
        record.risky     = Classifier::IsRiskyAttachment(record.filename, record.mediaType);
        if (!record.isInline) {
            message.attachmentCount += 1;
            message.attachmentBytes += record.sizeBytes;
        }
        message.attachments.push_back(std::move(record));
    }

    ClassifierInput input;
    input.subject          = parsed.subject;
    input.body             = body;
    input.senderName       = message.senderName;
    input.senderAddr       = message.senderAddr;
    input.replyToAddr      = replyToAddr;
    input.attachments      = message.attachments;
    input.bulkHeaders      = message.automated;
    input.addressedToOwner = AddressedTo(parsed.to, parsed.cc, ownerAddress);

    const Classification verdict = classifier_.Classify(input);
    message.SetClassifierVerdict(verdict.category, verdict.score);
    message.hits = verdict.hits;

    // A blocked sender stays classified as whatever it is — blocking is the
    // user's decision about a sender, not a re-reading of the content — but
    // the stamp travels with the row so the map and the filters can use it.
    message.blocked = store_.IsBlocked(message.senderAddr, message.senderDomain);

    // A verdict override is the opposite: the user telling us the classifier
    // read this sender wrong, so it *does* replace the verdict. The store keeps
    // the classifier's own answer alongside, which is what lets the correction
    // be taken back later. Applied here so a freshly ingested message is right
    // immediately, rather than only after the next ApplyOverridesToMessages().
    VerdictOverride override;
    if (store_.FindOverride(message.senderAddr, message.senderDomain, override))
        message.ApplyOverride(override.category);
    return message;
}

// ---- Single messages -------------------------------------------------------

bool Ingestor::IngestRaw(const std::string& rawMessage, const std::string& accountId,
                         const std::string& folder, int64_t uid,
                         const IngestOptions& options, AnalyzedMessage* out) {
    // An incremental scan leaves an already-analysed message alone; a re-scan
    // after a rule change (skipExisting = false) writes over it.
    if (options.skipExisting && uid != 0 && store_.HasMessage(accountId, folder, uid))
        return true;

    AnalyzedMessage message =
        Analyze(rawMessage, accountId, folder, uid, options.ownerAddress);
    if (!options.storeHits) message.hits.clear();

    const UltraDbResult r = store_.UpsertMessage(message);
    if (out) *out = message;
    return static_cast<bool>(r);
}

bool Ingestor::IngestFile(const std::string& path, const std::string& accountId,
                          const std::string& folder, int64_t uid,
                          const IngestOptions& options, AnalyzedMessage* out) {
    std::string raw;
    if (!ReadFileBytes(path, raw)) return false;
    if (uid == 0)
        uid = UidFromFileName(std::filesystem::path(path).filename().string());
    return IngestRaw(raw, accountId, folder, uid, options, out);
}

// ---- Whole mailboxes -------------------------------------------------------

IngestStats Ingestor::IngestFolderDirectory(const std::string& directory,
                                            const std::string& accountId,
                                            const std::string& folder,
                                            const IngestOptions& options) {
    IngestStats stats;
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) return stats;

    // Sort by uid so an interrupted run resumes in a predictable place and the
    // progress numbers move monotonically.
    std::vector<std::pair<int64_t, std::filesystem::path>> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        const std::string name = entry.path().filename().string();
        if (name.size() < 5 || Lower(entry.path().extension().string()) != ".eml") continue;
        files.emplace_back(UidFromFileName(name), entry.path());
    }
    std::sort(files.begin(), files.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // One transaction for the whole folder: analysing is cheap next to the
    // per-message commit a naive loop would pay.
    std::vector<AnalyzedMessage> batch;
    batch.reserve(files.size());

    for (const auto& [uid, path] : files) {
        if (options.maxMessages > 0 && stats.analysed >= options.maxMessages) break;
        ++stats.filesSeen;

        if (options.skipExisting && uid != 0 &&
            store_.HasMessage(accountId, folder, uid)) {
            ++stats.skipped;
            continue;
        }

        std::string raw;
        if (!ReadFileBytes(path.string(), raw)) {
            ++stats.failed;
            continue;
        }

        AnalyzedMessage message =
            Analyze(raw, accountId, folder, uid, options.ownerAddress);
        if (!options.storeHits) message.hits.clear();
        ++stats.analysed;
        if (IsUnwanted(message.category)) ++stats.unwanted;
        stats.attachments += message.attachmentCount;
        batch.push_back(std::move(message));

        if (onProgress) onProgress(stats.analysed, folder);
    }

    if (!batch.empty()) {
        const UltraDbResult r = store_.UpsertMessages(batch);
        if (r) {
            stats.stored = static_cast<int>(batch.size());
        } else {
            stats.failed += static_cast<int>(batch.size());
            stats.unwanted = 0;
            stats.attachments = 0;
        }
    }

    IngestState state;
    state.accountId = accountId;
    state.folder    = folder;
    state.messages  = stats.stored;
    state.lastRun   = 0;
    store_.GetLastUid(accountId, folder, state.lastUid);
    store_.UpsertIngestState(state);
    return stats;
}

IngestStats Ingestor::IngestMailCache(const std::string& mailCacheDir,
                                      const std::string& accountId,
                                      const IngestOptions& options) {
    IngestStats stats;
    std::error_code ec;
    const std::filesystem::path root = std::filesystem::path(mailCacheDir) / accountId;
    if (!std::filesystem::is_directory(root, ec)) return stats;

    std::vector<std::filesystem::path> folders;
    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (ec) break;
        if (entry.is_directory(ec)) folders.push_back(entry.path());
    }
    std::sort(folders.begin(), folders.end());

    IngestOptions remaining = options;
    for (const std::filesystem::path& folderPath : folders) {
        if (options.maxMessages > 0) {
            const int left = options.maxMessages - stats.analysed;
            if (left <= 0) break;
            remaining.maxMessages = left;
        }
        stats.Add(IngestFolderDirectory(folderPath.string(), accountId,
                                        folderPath.filename().string(), remaining));
    }
    return stats;
}

} // namespace EmailCleaner
