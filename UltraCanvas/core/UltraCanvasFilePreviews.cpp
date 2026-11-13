// core/UltraCanvasFilePreviews.cpp
// The preview provider registry (UltraCanvasFilePreviews.h). The providers of
// the formats compiled into the framework are in
// UltraCanvasFilePreviewsBuiltin.cpp, so this file depends on none of them.
//
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "UltraCanvasFilePreviews.h"
#include "UltraCanvasRichDocument.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <utility>

namespace UltraCanvas {

namespace {

struct Registry {
    std::shared_mutex mutex;
    // Registration order; a later provider wins an extension both list.
    std::vector<std::shared_ptr<const FilePreviewProvider>> providers;
};

Registry& TheRegistry() {
    static Registry registry;
    return registry;
}

// "Report.PDF" -> "pdf", ".pdf" -> "pdf", "pdf" -> "pdf". The last extension
// of a path; a name without a dot is taken as an extension already.
std::string ExtensionOf(const std::string& extensionOrPath) {
    std::string ext = extensionOrPath;
    const size_t slash = ext.find_last_of("/\\");
    const size_t dot = ext.rfind('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        ext = ext.substr(dot + 1);
    } else if (slash != std::string::npos) {
        return std::string();   // a path whose last component has no extension
    }
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

bool IsAvailable(const FilePreviewProvider& provider) {
    if (!provider.Available) return true;
    try {
        return provider.Available();
    } catch (...) {
        return false;
    }
}

enum class Ask { Thumbnail, Document };

// The provider answering `ask` for `ext`: the latest registered that lists it
// and is available now.
std::shared_ptr<const FilePreviewProvider> ProviderFor(const std::string& ext, Ask ask) {
    if (ext.empty()) return nullptr;
    RegisterBuiltinFilePreviewProviders();
    std::vector<std::shared_ptr<const FilePreviewProvider>> candidates;
    {
        Registry& registry = TheRegistry();
        std::shared_lock<std::shared_mutex> lock(registry.mutex);
        for (auto it = registry.providers.rbegin(); it != registry.providers.rend(); ++it) {
            const FilePreviewProvider& p = **it;
            const std::vector<std::string>& exts =
                    ask == Ask::Thumbnail ? p.thumbnailExtensions : p.documentExtensions;
            const bool canAnswer = ask == Ask::Thumbnail ? static_cast<bool>(p.RenderThumbnail)
                                                         : static_cast<bool>(p.ReadDocument);
            if (canAnswer && std::find(exts.begin(), exts.end(), ext) != exts.end())
                candidates.push_back(*it);
        }
    }
    // Available() is asked outside the lock: it may be as slow as probing a
    // backend.
    for (const auto& candidate : candidates)
        if (IsAvailable(*candidate)) return candidate;
    return nullptr;
}

std::vector<std::string> ExtensionsFor(Ask ask) {
    RegisterBuiltinFilePreviewProviders();
    std::vector<std::shared_ptr<const FilePreviewProvider>> providers;
    {
        Registry& registry = TheRegistry();
        std::shared_lock<std::shared_mutex> lock(registry.mutex);
        providers = registry.providers;
    }
    std::set<std::string> extensions;
    for (const auto& p : providers) {
        const bool canAnswer = ask == Ask::Thumbnail ? static_cast<bool>(p->RenderThumbnail)
                                                     : static_cast<bool>(p->ReadDocument);
        if (!canAnswer || !IsAvailable(*p)) continue;
        const auto& exts = ask == Ask::Thumbnail ? p->thumbnailExtensions : p->documentExtensions;
        extensions.insert(exts.begin(), exts.end());
    }
    return {extensions.begin(), extensions.end()};
}

std::shared_ptr<UCPixmap> RenderWith(const FilePreviewProvider& provider, const std::string& path,
                                     const FileThumbnailRequest& request) {
    try {
        return provider.RenderThumbnail(path, request);
    } catch (...) {
        std::fprintf(stderr, "[FilePreviews] %s threw drawing a thumbnail\n", provider.name.c_str());
        return nullptr;
    }
}

} // namespace

void RegisterFilePreviewProvider(FilePreviewProvider provider) {
    const bool thumbnails = provider.RenderThumbnail && !provider.thumbnailExtensions.empty();
    const bool documents = provider.ReadDocument && !provider.documentExtensions.empty();
    if (provider.name.empty() || (!thumbnails && !documents)) {
        std::fprintf(stderr, "[FilePreviews] provider '%s' ignored: it answers nothing\n",
                     provider.name.c_str());
        return;
    }
    // Extensions are matched lowercase and without a leading dot. Only the
    // dot is taken off: "fb2.zip" stays what it says.
    for (auto* list : {&provider.thumbnailExtensions, &provider.documentExtensions}) {
        for (std::string& ext : *list) {
            ext.erase(0, ext.find_first_not_of('.'));
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        }
    }

    auto shared = std::make_shared<const FilePreviewProvider>(std::move(provider));
    Registry& registry = TheRegistry();
    std::unique_lock<std::shared_mutex> lock(registry.mutex);
    registry.providers.erase(
            std::remove_if(registry.providers.begin(), registry.providers.end(),
                           [&](const auto& p) { return p->name == shared->name; }),
            registry.providers.end());
    registry.providers.push_back(std::move(shared));
}

bool CanRenderFileThumbnail(const std::string& extensionOrPath) {
    return ProviderFor(ExtensionOf(extensionOrPath), Ask::Thumbnail) != nullptr;
}

std::shared_ptr<UCPixmap> RenderFileThumbnailFromProvider(const std::string& path,
                                                          const FileThumbnailRequest& request) {
    return RenderFileThumbnailAs(path, ExtensionOf(path), request);
}

std::shared_ptr<UCPixmap> RenderFileThumbnailAs(const std::string& path,
                                                const std::string& extension,
                                                const FileThumbnailRequest& request) {
    if (request.width <= 0 || request.height <= 0) return nullptr;
    const auto provider = ProviderFor(ExtensionOf(extension), Ask::Thumbnail);
    return provider ? RenderWith(*provider, path, request) : nullptr;
}

bool CanReadPreviewDocument(const std::string& extensionOrPath) {
    return ProviderFor(ExtensionOf(extensionOrPath), Ask::Document) != nullptr;
}

bool ReadPreviewDocument(const std::string& path, UCRichDocument& document) {
    const auto provider = ProviderFor(ExtensionOf(path), Ask::Document);
    if (!provider) return false;
    try {
        return provider->ReadDocument(path, document);
    } catch (...) {
        std::fprintf(stderr, "[FilePreviews] %s threw reading a document\n", provider->name.c_str());
        return false;
    }
}

std::vector<std::string> FileThumbnailExtensions() { return ExtensionsFor(Ask::Thumbnail); }
std::vector<std::string> PreviewDocumentExtensions() { return ExtensionsFor(Ask::Document); }

} // namespace UltraCanvas
