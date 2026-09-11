// core/UltraCanvasModelPreview.cpp
// The provider seam declared in UltraCanvasModelPreview.h: one installed
// provider, guarded, folded together with core's own STL loader.
//
// Version: 1.0.0
// Last Modified: 2026-09-11
// Author: UltraCanvas Framework

#include "UltraCanvasModelPreview.h"

#include "Models/STL/UltraCanvasSTLLoader.h"

#include <algorithm>
#include <cctype>
#include <mutex>

namespace UltraCanvas {

namespace {

// The provider is installed once at start-up and read from the Filer's
// preview workers, so the mutex is for the install rather than for contention.
// A copy is taken under the lock and called outside it: a provider that reads
// a large FBX must not hold up every other thread's extension test.
std::mutex& ProviderMutex() {
    static std::mutex m;
    return m;
}

ModelPreviewProvider& ProviderSlot() {
    static ModelPreviewProvider provider;
    return provider;
}

ModelPreviewProvider CurrentProvider() {
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

bool ProviderClaims(const ModelPreviewProvider& provider, const std::string& extension) {
    if (!provider.Valid() || extension.empty()) return false;
    for (const std::string& claimed : provider.Extensions())
        if (claimed == extension) return true;
    return false;
}

} // namespace

void SetModelPreviewProvider(ModelPreviewProvider provider) {
    std::lock_guard<std::mutex> lock(ProviderMutex());
    ProviderSlot() = provider.Valid() ? std::move(provider) : ModelPreviewProvider{};
}

bool CanPreviewModelExtension(const std::string& extensionOrPath) {
    const std::string extension = ExtensionOf(extensionOrPath);
    if (extension == "stl") return true;
    return ProviderClaims(CurrentProvider(), extension);
}

std::vector<std::string> PreviewableModelExtensions() {
    std::vector<std::string> extensions{"stl"};
    const ModelPreviewProvider provider = CurrentProvider();
    if (provider.Valid()) {
        for (std::string claimed : provider.Extensions()) {
            for (char& c : claimed)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (!claimed.empty()) extensions.push_back(std::move(claimed));
        }
    }
    std::sort(extensions.begin(), extensions.end());
    extensions.erase(std::unique(extensions.begin(), extensions.end()), extensions.end());
    return extensions;
}

bool LoadModelPreviewMesh(const std::string& path, Mesh3D& out) {
    out.Clear();

    // STL stays core's own, even with a provider installed: the loader is
    // already here, it is the one format guaranteed to be readable, and it
    // keeps a build without the Models plugin behaving exactly as before.
    if (UltraCanvasSTLLoader::HasSTLExtension(path)) {
        if (!UltraCanvasSTLLoader::Load(path, out) || out.Empty()) return false;
        if (!out.bounds.IsValid()) out.ComputeBounds();
        return true;
    }

    const ModelPreviewProvider provider = CurrentProvider();
    if (!ProviderClaims(provider, ExtensionOf(path))) return false;
    if (!provider.Load(path, out) || out.Empty()) return false;
    if (!out.bounds.IsValid()) out.ComputeBounds();
    return true;
}

} // namespace UltraCanvas
