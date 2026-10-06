// include/UltraCanvasImageCursorCache.h
// Cursors drawn from an image file, kept once per screen scaling (backend-internal)
// Version: 1.0.0
// Last Modified: 2026-10-05
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_IMAGE_CURSOR_CACHE_H
#define ULTRACANVAS_IMAGE_CURSOR_CACHE_H

#include "UltraCanvasCommonTypes.h"
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>

namespace UltraCanvas {

// A system cursor follows the screen's scaling by itself; one drawn from a
// picture (the context-menu and magnifier cursors, the colour picker's
// eyedropper) is drawn at one size. Kept under the cursor alone it stayed the
// size of the screen it was first shown on after its window moved to a screen
// with other scaling. This keeps the file each image cursor comes from and
// draws it once for every scaling it is asked for.
//
// `Handle` is the platform's cursor (HCURSOR, an X11 Cursor); an empty
// Handle{} means "could not be drawn". The platform supplies how to draw one
// from a file and how to release one.
    template <typename Handle>
    class UCImageCursorCache {
    public:
        using DrawFn = std::function<Handle(const std::string& file, int hotspotX, int hotspotY, float scale)>;
        using ReleaseFn = std::function<void(Handle)>;

        UCImageCursorCache(DrawFn drawFn, ReleaseFn releaseFn)
                : draw(std::move(drawFn)), release(std::move(releaseFn)) {}
        ~UCImageCursorCache() = default;   // the owner calls Clear() while it can still release
        UCImageCursorCache(const UCImageCursorCache&) = delete;
        UCImageCursorCache& operator=(const UCImageCursorCache&) = delete;

        bool HasSource(UCMouseCursor cursor) const { return sources.count(cursor) != 0; }

        // The cursor drawn for `scale` from the picture registered for
        // `cursor` - drawn now if this scaling has not been asked for yet.
        // Handle{} when nothing is registered or the picture cannot be drawn
        // (remembered, so a missing file is not retried on every move).
        Handle Get(UCMouseCursor cursor, float scale) {
            auto src = sources.find(cursor);
            if (src == sources.end()) return Handle{};
            const int percent = ScalePercent(scale);
            const auto key = std::make_pair(cursor, percent);
            auto it = drawn.find(key);
            if (it != drawn.end()) return it->second;
            Handle handle = draw(src->second.file, src->second.hotspotX, src->second.hotspotY,
                                 static_cast<float>(percent) / 100.0f);
            drawn[key] = handle;
            return handle;
        }

        // Makes `file` the picture of `cursor`, dropping what was drawn from
        // an earlier one. Registering the same file again keeps what is drawn.
        // False, and nothing registered, when the picture cannot be drawn at
        // `scale`.
        bool SetSource(UCMouseCursor cursor, const std::string& file,
                       int hotspotX, int hotspotY, float scale) {
            auto src = sources.find(cursor);
            const bool same = src != sources.end() && src->second.file == file
                              && src->second.hotspotX == hotspotX && src->second.hotspotY == hotspotY;
            if (!same) {
                Forget(cursor);
                sources[cursor] = Source{file, hotspotX, hotspotY};
            }
            if (Get(cursor, scale) != Handle{}) return true;
            Forget(cursor);
            return false;
        }

        // Releases every cursor drawn and forgets every picture.
        void Clear() {
            for (auto& [key, handle] : drawn) {
                if (handle != Handle{}) release(handle);
            }
            drawn.clear();
            sources.clear();
        }

    private:
        struct Source {
            std::string file;
            int hotspotX = 0;
            int hotspotY = 0;
        };

        static int ScalePercent(float scale) {
            if (!(scale > 0.0f)) scale = 1.0f;
            const int percent = static_cast<int>(std::lround(scale * 100.0f));
            return percent > 0 ? percent : 100;
        }

        void Forget(UCMouseCursor cursor) {
            for (auto it = drawn.begin(); it != drawn.end();) {
                if (it->first.first == cursor) {
                    if (it->second != Handle{}) release(it->second);
                    it = drawn.erase(it);
                } else {
                    ++it;
                }
            }
            sources.erase(cursor);
        }

        DrawFn draw;
        ReleaseFn release;
        std::unordered_map<UCMouseCursor, Source> sources;
        std::map<std::pair<UCMouseCursor, int>, Handle> drawn;   // (cursor, scale in percent)
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_IMAGE_CURSOR_CACHE_H
