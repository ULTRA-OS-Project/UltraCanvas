// Apps/UltraMail/engine/UltraMailAttachmentCache.h
// Materialises attachment bytes to files on disk so a path-based viewer
// (UltraCanvasMediaViewer) can open them. Filenames are sanitised (no path
// traversal) and de-duplicated within the cache directory; the extension is
// preserved so the viewer can pick the right renderer.
//
// The files are copies made only so a viewer can open them - the message
// itself stays in the body cache - so they are pruned: Prune() drops the ones
// not written for a while, then the oldest until the rest fits a size cap.
// The app prunes at start-up, before any viewer has a file open.
// Version: 0.2.0 - Prune(); paths through PathFromUtf8
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailMimeCodec.h"

#include <cstdint>
#include <string>

namespace UltraMail {

struct AttachmentPruneStats {
    int      removed = 0;
    uint64_t bytesRemoved = 0;
};

class AttachmentCache {
public:
    explicit AttachmentCache(std::string cacheDir) : cacheDir_(std::move(cacheDir)) {}

    const std::string& Directory() const { return cacheDir_; }

    // Write the attachment into the cache directory, returning the file path
    // (empty on failure). Repeated writes of the same content reuse the file
    // (and mark it as just written, so Prune keeps it); a name collision with
    // different content gets a numeric suffix.
    std::string Write(const Attachment& attachment) const;

    // Delete the files in the cache directory (never a subdirectory) that
    // were last written more than `maxAgeSeconds` ago, then the oldest of the
    // rest until they fit in `maxBytes`. 0 for either means no such limit -
    // Prune(0, 0) keeps everything; to empty the directory pass maxAgeSeconds
    // = -1. A file that cannot be deleted (still open elsewhere) is skipped.
    AttachmentPruneStats Prune(int64_t maxAgeSeconds, uint64_t maxBytes) const;

    // Write the attachment to an explicit destination path (e.g. from a
    // "Save As…" dialog). Returns false on failure.
    bool SaveAs(const Attachment& attachment, const std::string& destPath) const;

    // Sanitise a proposed filename to a safe basename (exposed for testing).
    static std::string SanitizeFilename(const std::string& name,
                                        const std::string& mediaType);

private:
    std::string cacheDir_;
};

} // namespace UltraMail
