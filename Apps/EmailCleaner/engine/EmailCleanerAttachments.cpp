// Apps/EmailCleaner/engine/EmailCleanerAttachments.cpp
// Version: 0.3.0 (Phase 3)
// Author: UltraCanvas Framework / ULTRA OS
#include "EmailCleanerAttachments.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8

#include "EmailCleanerClassifier.h"
#include "UltraMailAttachmentCache.h"   // the write and the pruning

#include <UltraNet/UltraNetMime.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

namespace EmailCleaner {

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool ReadFile(const std::string& path, std::string& out) {
    std::ifstream in(UltraCanvas::PathFromUtf8(path), std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return in.good() || in.eof();
}

} // namespace

std::string ToString(AttachmentFetch status) {
    switch (status) {
        case AttachmentFetch::Ok:            return "ok";
        case AttachmentFetch::RefusedRisky:  return "refused-risky";
        case AttachmentFetch::NoSuchMessage: return "no-such-message";
        case AttachmentFetch::NoSuchPart:    return "no-such-part";
        case AttachmentFetch::Unreadable:    return "unreadable";
    }
    return "unknown";
}

std::string DescribeFetch(AttachmentFetch status, const std::string& filename) {
    const std::string name = filename.empty() ? "This attachment" : ("\"" + filename + "\"");
    switch (status) {
        case AttachmentFetch::Ok:
            return name + " is ready to view.";
        case AttachmentFetch::RefusedRisky:
            // Said plainly, because the user is entitled to know it was a
            // decision and not a failure.
            return name + " is an executable, script or macro-bearing file. "
                   "EmailCleaner will not open or copy it — that is exactly the "
                   "kind of attachment it flags. It stays in the mail cache, "
                   "untouched.";
        case AttachmentFetch::NoSuchMessage:
            return "The cached copy of this message is gone — sync the account "
                   "in UltraMail and load the mail again.";
        case AttachmentFetch::NoSuchPart:
            return name + " is no longer in that message. Re-analyse to refresh "
                   "what the index knows.";
        case AttachmentFetch::Unreadable:
            return name + " could not be read out of the cached message.";
    }
    return name + " could not be opened.";
}

std::string CachedMessagePath(const std::string& mailCacheDir,
                              const std::string& accountId,
                              const std::string& folder,
                              int64_t uid) {
    if (mailCacheDir.empty() || accountId.empty() || folder.empty() || uid <= 0)
        return "";
    std::filesystem::path p = PathFromUtf8(mailCacheDir) / PathFromUtf8(accountId) / PathFromUtf8(folder);
    p /= (std::to_string(uid) + ".eml");
    return PathToUtf8(p);
}

std::string SafeAttachmentName(const std::string& filename,
                               const std::string& mediaType) {
    // Only ever a basename: everything up to the last separator goes, so a
    // name carrying "../" or an absolute path cannot leave the cache directory.
    std::string name = filename;
    const size_t slash = name.find_last_of("/\\:");
    if (slash != std::string::npos) name = name.substr(slash + 1);

    std::string safe;
    safe.reserve(name.size());
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        // Keep it to characters every filesystem accepts, and drop control
        // bytes outright.
        if (std::isalnum(u) || c == '.' || c == '-' || c == '_' || c == ' ')
            safe += c;
        else
            safe += '_';
    }
    // A name of nothing but dots ("." / "..") is not a name.
    while (!safe.empty() && (safe.front() == '.' || safe.front() == ' ')) safe.erase(0, 1);
    while (!safe.empty() && safe.back() == ' ') safe.pop_back();
    if (safe.size() > 120) safe.resize(120);

    if (safe.empty()) {
        safe = "attachment";
        // Give the viewer something to dispatch on when the name was useless.
        const std::string type = Lower(mediaType);
        const size_t slash2 = type.find('/');
        if (slash2 != std::string::npos && slash2 + 1 < type.size()) {
            std::string ext = type.substr(slash2 + 1);
            std::string clean;
            for (char c : ext)
                if (std::isalnum(static_cast<unsigned char>(c))) clean += c;
            if (!clean.empty() && clean.size() <= 8) safe += "." + clean;
        }
    }
    return safe;
}

AttachmentFetch FetchAttachment(const std::string& mailCacheDir,
                                const std::string& accountId,
                                const std::string& folder,
                                int64_t uid,
                                const AttachmentRecord& record,
                                std::vector<uint8_t>& outBytes) {
    outBytes.clear();

    // The refusal comes first, and does not trust the stored stamp on its own:
    // an index written before a rule change, or by an older version, must not
    // be able to get a risky file opened.
    if (record.risky || Classifier::IsRiskyAttachment(record.filename, record.mediaType))
        return AttachmentFetch::RefusedRisky;

    const std::string path = CachedMessagePath(mailCacheDir, accountId, folder, uid);
    if (path.empty()) return AttachmentFetch::NoSuchMessage;

    std::error_code ec;
    if (!std::filesystem::is_regular_file(UltraCanvas::PathFromUtf8(path), ec)) return AttachmentFetch::NoSuchMessage;

    std::string raw;
    if (!ReadFile(path, raw) || raw.empty()) return AttachmentFetch::Unreadable;

    UltraNetMimeMessage parsed;
    if (!UltraNet_MimeParse(raw, parsed)) return AttachmentFetch::Unreadable;

    std::vector<UltraNetMimeAttachmentView> attachments;
    UltraNet_MimeCollectAttachments(parsed, attachments, /*includeInline=*/true);
    if (attachments.empty()) return AttachmentFetch::NoSuchPart;

    const UltraNetMimeAttachmentView* match = nullptr;
    if (record.filename.empty()) {
        match = &attachments.front();
    } else {
        const std::string wanted = Lower(record.filename);
        for (const UltraNetMimeAttachmentView& a : attachments) {
            if (Lower(a.filename) == wanted) { match = &a; break; }
        }
    }
    if (!match) return AttachmentFetch::NoSuchPart;

    // The part the message actually carries decides, not the index row: a name
    // recorded as harmless must not open a part whose real type is not.
    if (Classifier::IsRiskyAttachment(match->filename, match->mediaType))
        return AttachmentFetch::RefusedRisky;

    outBytes = match->data;
    return AttachmentFetch::Ok;
}

std::string WriteToCache(const std::string& cacheDir,
                         const std::string& filename,
                         const std::string& mediaType,
                         const std::vector<uint8_t>& bytes) {
    if (cacheDir.empty()) return "";

    std::error_code ec;
    const std::filesystem::path dir = PathFromUtf8(cacheDir);
    std::filesystem::create_directories(dir, ec);

    // The write itself is UltraMail's AttachmentCache: identical bytes reuse
    // the copy already there (and mark it as just opened, for the pruning),
    // and a *different* attachment with the same name - two "invoice.pdf"
    // from two senders - gets "invoice (1).pdf" instead of overwriting the
    // first, which may still be open in a viewer.
    UltraMail::Attachment attachment;
    attachment.filename  = SafeAttachmentName(filename, mediaType);
    attachment.mediaType = mediaType;
    attachment.data      = bytes;
    const std::string written = UltraMail::AttachmentCache(cacheDir).Write(attachment);
    if (written.empty()) return "";

    // Belt and braces: whatever the sanitisers produced, the file has to sit
    // inside the cache directory. One that does not is removed, not returned.
    const std::filesystem::path target = PathFromUtf8(written);
    const std::filesystem::path root = std::filesystem::weakly_canonical(dir, ec);
    const std::filesystem::path resolved =
        std::filesystem::weakly_canonical(target.parent_path(), ec);
    if (ec || resolved != root) {
        std::filesystem::remove(target, ec);
        return "";
    }
    return written;
}

int PruneAttachmentCache(const std::string& cacheDir, int64_t maxAgeSeconds,
                         uint64_t maxBytes) {
    if (cacheDir.empty()) return 0;
    return UltraMail::AttachmentCache(cacheDir).Prune(maxAgeSeconds, maxBytes).removed;
}

} // namespace EmailCleaner
