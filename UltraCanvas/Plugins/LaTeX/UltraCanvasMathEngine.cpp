// Plugins/LaTeX/UltraCanvasMathEngine.cpp
// Facade of the native math engine. See UltraCanvasMathEngine.h.
//
// Version: 1.0.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework

#include "Plugins/LaTeX/UltraCanvasMathEngine.h"
#include "Plugins/LaTeX/UltraCanvasMathParser.h"

#include <fstream>
#include <mutex>

namespace UltraCanvas {

struct UltraCanvasMathEngine::Impl {
    UltraCanvasMathFont font;
    std::string lastError;
    mutable std::mutex mutex;   // the font caches are not thread-safe; serialise typesetting
};

UltraCanvasMathEngine::UltraCanvasMathEngine() : impl_(std::make_unique<Impl>()) {}
UltraCanvasMathEngine::~UltraCanvasMathEngine() = default;

bool UltraCanvasMathEngine::LoadFont(const std::string& path) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->font.Load(path)) {
        impl_->lastError = impl_->font.GetLastError();
        return false;
    }
    if (!impl_->font.HasMathTable()) {
        impl_->lastError = "font has no MATH table: " + path;
        impl_->font.Unload();
        return false;
    }
    impl_->lastError.clear();
    return true;
}

bool UltraCanvasMathEngine::LoadFontFrom(const std::vector<std::string>& searchDirs,
                                         const std::vector<std::string>& candidates) {
    for (const auto& dir : searchDirs) {
        for (const auto& name : candidates) {
            const std::string path = dir.empty() ? name : (dir.back() == '/' ? dir + name : dir + "/" + name);
            std::ifstream probe(path, std::ios::binary);
            if (!probe.good()) continue;
            if (LoadFont(path)) return true;
        }
    }
    if (impl_->lastError.empty()) impl_->lastError = "math font not found (looked for " + (candidates.empty() ? std::string("nothing") : candidates.front()) + ")";
    return false;
}

bool UltraCanvasMathEngine::IsReady() const { return impl_->font.IsLoaded() && impl_->font.HasMathTable(); }
const std::string& UltraCanvasMathEngine::GetLastError() const { return impl_->lastError; }
const UltraCanvasMathFont& UltraCanvasMathEngine::GetFont() const { return impl_->font; }

MathTypesetResult UltraCanvasMathEngine::Typeset(const std::string& latex, const MathTypesetOptions& options) const {
    MathTypesetResult result;
    if (!IsReady()) {
        result.diagnostics.push_back({impl_->lastError.empty() ? "math font not loaded" : impl_->lastError, -1, -1});
        return result;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    UltraCanvasMathParser parser;
    if (!options.preamble.empty()) {
        std::vector<MathDiagnostic> ignored;
        parser.Parse(options.preamble, ignored);
    }
    MathAtomPtr root = parser.Parse(latex, result.diagnostics);

    MathLayoutOptions lo;
    lo.fontSize = options.fontSize;
    lo.style = options.style;
    lo.color = options.color;
    lo.maxWidth = options.maxWidth;
    lo.textFallback = options.textFallback;
    UltraCanvasMathLayout layout(impl_->font, lo, result.diagnostics);
    result.root = layout.Layout(root);
    if (result.root) {
        result.width = result.root->width;
        result.height = result.root->height;
        result.depth = result.root->depth;
    }
    return result;
}

UltraCanvasMathEngine& GetSharedMathEngine() {
    static UltraCanvasMathEngine engine;
    return engine;
}

} // namespace UltraCanvas
