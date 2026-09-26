// core/UltraCanvasVectorPreview.cpp
// The provider seam declared in UltraCanvasVectorPreview.h, plus the drawing
// half every caller shares: a VectorDocument fitted into an offscreen render
// context and read back as a pixmap.
//
// Version: 1.1.0
// Last Modified: 2026-09-26
// Author: UltraCanvas Framework

#include "UltraCanvasVectorPreview.h"

#include "DataFormats/UltraCanvasVectorRenderer.h"
#include "UltraCanvasRenderContext.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <mutex>

namespace UltraCanvas {

namespace {

// Installed once at start-up and read from the media viewer and the Filer's
// preview workers, so the mutex guards the install rather than contention. A
// copy is taken under the lock and called outside it: a provider parsing a
// large DWG must not hold up every other thread's extension test.
std::mutex& ProviderMutex() {
    static std::mutex m;
    return m;
}

VectorPreviewProvider& ProviderSlot() {
    static VectorPreviewProvider provider;
    return provider;
}

VectorPreviewProvider CurrentProvider() {
    std::lock_guard<std::mutex> lock(ProviderMutex());
    return ProviderSlot();
}

// The extension of a path, or of a bare extension, lowercased and undotted.
std::string ExtensionOf(const std::string& extensionOrPath) {
    const size_t slash = extensionOrPath.find_last_of("/\\");
    const std::string name = slash == std::string::npos
                                     ? extensionOrPath
                                     : extensionOrPath.substr(slash + 1);
    const size_t dot = name.rfind('.');
    std::string extension = dot == std::string::npos ? name : name.substr(dot + 1);
    for (char& c : extension)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return extension;
}

bool ProviderClaims(const VectorPreviewProvider& provider,
                    const std::string& extension) {
    if (!provider.Valid() || extension.empty()) return false;
    for (const std::string& claimed : provider.Extensions())
        if (claimed == extension) return true;
    return false;
}

// A path rather than a bare extension: only then is there a file whose header
// a content-decided format (a .bak holding a drawing) could be read from.
bool LooksLikePath(const std::string& extensionOrPath) {
    return extensionOrPath.find_first_of("/\\.") != std::string::npos;
}

int DeviceSize(int logical, float scale) {
    if (logical <= 0) return 0;
    if (scale <= 0.0f) scale = 1.0f;
    return std::max(1, static_cast<int>(std::lround(logical * scale)));
}

}   // namespace

void SetVectorPreviewProvider(VectorPreviewProvider provider) {
    std::lock_guard<std::mutex> lock(ProviderMutex());
    ProviderSlot() = provider.Valid() ? std::move(provider) : VectorPreviewProvider{};
}

bool CanPreviewVectorExtension(const std::string& extensionOrPath) {
    const VectorPreviewProvider provider = CurrentProvider();
    if (!provider.Valid()) return false;
    if (ProviderClaims(provider, ExtensionOf(extensionOrPath))) return true;
    // The suffix says nothing; the file itself may. Only asked for something
    // that is actually a path - an extension on its own has no header.
    return provider.ClaimsFile && LooksLikePath(extensionOrPath) &&
           provider.ClaimsFile(extensionOrPath);
}

std::vector<std::string> PreviewableVectorExtensions() {
    std::vector<std::string> extensions;
    const VectorPreviewProvider provider = CurrentProvider();
    if (provider.Valid()) {
        for (std::string claimed : provider.Extensions()) {
            for (char& c : claimed)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (!claimed.empty()) extensions.push_back(std::move(claimed));
        }
    }
    std::sort(extensions.begin(), extensions.end());
    extensions.erase(std::unique(extensions.begin(), extensions.end()),
                     extensions.end());
    return extensions;
}

std::shared_ptr<VectorStorage::VectorDocument> LoadVectorPreviewDocument(
        const std::string& path) {
    const VectorPreviewProvider provider = CurrentProvider();
    if (!provider.Valid()) return nullptr;
    if (!ProviderClaims(provider, ExtensionOf(path))) {
        if (!provider.ClaimsFile || !provider.ClaimsFile(path)) return nullptr;
    }
    return provider.Load(path);
}

std::shared_ptr<VectorStorage::VectorDocument> ImportVectorDocument(
        const std::string& path, const VectorPreviewProvider::NoteFn& note) {
    const VectorPreviewProvider provider = CurrentProvider();
    if (!provider.Valid()) return nullptr;
    if (!ProviderClaims(provider, ExtensionOf(path))) {
        if (!provider.ClaimsFile || !provider.ClaimsFile(path)) return nullptr;
    }
    return provider.Import ? provider.Import(path, note) : provider.Load(path);
}

std::vector<std::string> SavableVectorExtensions() {
    std::vector<std::string> extensions;
    const VectorPreviewProvider provider = CurrentProvider();
    if (provider.Valid() && provider.SaveExtensions && provider.Save) {
        for (std::string e : provider.SaveExtensions()) {
            for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (!e.empty()) extensions.push_back(std::move(e));
        }
    }
    std::sort(extensions.begin(), extensions.end());
    extensions.erase(std::unique(extensions.begin(), extensions.end()), extensions.end());
    return extensions;
}

bool ExportVectorDocument(const VectorStorage::VectorDocument& document,
                          const std::string& path,
                          const VectorPreviewProvider::NoteFn& note) {
    const VectorPreviewProvider provider = CurrentProvider();
    if (!provider.Valid() || !provider.Save) return false;
    return provider.Save(document, path, note);
}

std::shared_ptr<UCPixmap> RenderVectorDocumentPixmap(
        const VectorStorage::VectorDocument& document, int w, int h,
        float scale, const Color& background) {
    const int pw = DeviceSize(w, scale);
    const int ph = DeviceSize(h, scale);
    if (pw <= 0 || ph <= 0) return nullptr;

    // The drawing's own size. A document that declares none is measured from
    // what it holds, and one that measures to nothing has nothing to draw.
    double dw = document.Size.width;
    double dh = document.Size.height;
    if (dw <= 0.0 || dh <= 0.0) {
        const Rect2Dd bounds = document.GetBoundingBox();
        dw = bounds.width;
        dh = bounds.height;
    }
    if (dw <= 0.0 || dh <= 0.0) return nullptr;

    auto pixmap = std::make_shared<UCPixmap>();
    if (!pixmap->Init(pw, ph)) return nullptr;
    std::unique_ptr<IRenderContext> ctx = CreateRenderContext(Size2Di(pw, ph), nullptr);
    if (!ctx) return nullptr;

    ctx->Clear(background);

    // Fitted and centred, aspect ratio kept - the same framing the thumbnail
    // tiles and the preview pane expect of every other file kind.
    const double fit = std::min(pw / dw, ph / dh);
    ctx->PushState();
    ctx->Translate((pw - dw * fit) * 0.5, (ph - dh * fit) * 0.5);
    ctx->Scale(fit, fit);

    VectorRenderer renderer;
    VectorRenderOptions options;
    options.ViewportBounds = Rect2Dd(0, 0, dw, dh);
    // No PixelRatio: RenderDocument scales by it on top of the context's
    // transform, and the fit is already on the context - setting it too drew
    // every preview at fit squared (a large drawing shrunk into a corner, a
    // small one enlarged and cropped).
    renderer.SetOptions(options);
    renderer.RenderDocument(ctx.get(), document);
    ctx->PopState();

    ctx->FlushToSurface(pixmap->GetSurface(), Point2Dd(0, 0));
    pixmap->MarkDirty();
    pixmap->Flush();
    return pixmap;
}

std::shared_ptr<UCPixmap> RenderVectorPreviewPixmap(const std::string& path,
                                                    int w, int h, float scale) {
    auto document = LoadVectorPreviewDocument(path);
    if (!document) return nullptr;
    return RenderVectorDocumentPixmap(*document, w, h, scale);
}

}   // namespace UltraCanvas
