// Apps/EmailCleaner/engine/EmailCleanerAttachments.h
// Getting an attachment back out of the mail cache so it can be looked at.
//
// The analysis database stores attachment *metadata* only — name, media type,
// size, and whether the type is risky. The bytes stay where UltraMail put them,
// in the cached .eml. This module finds that file, re-parses it, and hands back
// the bytes for one attachment, so the detail view can open it in
// UltraCanvasMediaViewer without EmailCleaner keeping a second copy of every
// attachment in the mailbox.
//
// The judgement here is the refusal. EmailCleaner exists to deal with unwanted
// mail, and a large share of what it looks at carries executables, scripts and
// macro-bearing documents — that is one of the signals it classifies on. An app
// that will happily hand such a file to a viewer, or write it somewhere
// convenient, is a delivery mechanism. So extraction of a risky attachment is
// refused outright and says why: nothing is written to disk, and the file stays
// in the mail cache where it already was.
// Version: 0.3.0 (Phase 3)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "EmailCleanerTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace EmailCleaner {

// Why an attachment could not be produced. Ok means `bytes` is filled.
enum class AttachmentFetch {
    Ok = 0,
    RefusedRisky,    // executable / script / macro-bearing: not opened at all
    NoSuchMessage,   // the cached .eml is gone (mailbox re-synced, cache cleared)
    NoSuchPart,      // the message no longer carries an attachment by that name
    Unreadable       // the file exists but could not be read or parsed
};

std::string ToString(AttachmentFetch status);

// One sentence for the UI, naming the attachment.
std::string DescribeFetch(AttachmentFetch status, const std::string& filename);

// The .eml UltraMail caches for a message:
//   <mailCacheDir>/<accountId>/<folder>/<uid>.eml
std::string CachedMessagePath(const std::string& mailCacheDir,
                              const std::string& accountId,
                              const std::string& folder,
                              int64_t uid);

// Read one attachment's bytes out of a cached message.
//
// `record` is the row the analysis database holds; its `risky` flag is honoured
// and re-derived from the name and media type, so a stale or absent stamp
// cannot get a risky file opened. Matching is by filename, falling back to the
// first attachment when the name is empty.
AttachmentFetch FetchAttachment(const std::string& mailCacheDir,
                                const std::string& accountId,
                                const std::string& folder,
                                int64_t uid,
                                const AttachmentRecord& record,
                                std::vector<uint8_t>& outBytes);

// Write bytes into `cacheDir` under a sanitised version of `filename`, so a
// path-based viewer can open them. Returns the written path, or empty on
// failure. Refuses names that would escape the directory.
std::string WriteToCache(const std::string& cacheDir,
                         const std::string& filename,
                         const std::string& mediaType,
                         const std::vector<uint8_t>& bytes);

// Sanitise a proposed attachment name to a safe basename. Exposed for testing:
// this is the function that has to hold against "../../.bashrc".
std::string SafeAttachmentName(const std::string& filename,
                               const std::string& mediaType);

} // namespace EmailCleaner
