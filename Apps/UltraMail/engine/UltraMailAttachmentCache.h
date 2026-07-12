// Apps/UltraMail/engine/UltraMailAttachmentCache.h
// Materialises attachment bytes to files on disk so a path-based viewer
// (UltraCanvasMediaViewer) can open them. Filenames are sanitised (no path
// traversal) and de-duplicated within the cache directory; the extension is
// preserved so the viewer can pick the right renderer.
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailMimeCodec.h"

#include <string>

namespace UltraMail {

class AttachmentCache {
public:
    explicit AttachmentCache(std::string cacheDir) : cacheDir_(std::move(cacheDir)) {}

    const std::string& Directory() const { return cacheDir_; }

    // Write the attachment into the cache directory, returning the file path
    // (empty on failure). Repeated writes of the same content reuse the file;
    // a name collision with different content gets a numeric suffix.
    std::string Write(const Attachment& attachment) const;

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
