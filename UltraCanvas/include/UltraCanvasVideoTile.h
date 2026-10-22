// include/UltraCanvasVideoTile.h
// A live picture fed frame by frame from any thread: a call's video, a
// self view, a decoded stream. The newest frame is shown, fitted (contain or
// cover), optionally mirrored; without a frame, a caption.
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasImage.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasUIElement.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace UltraCanvas {

enum class VideoTileFit {
    Contain,    // the whole picture, bars where the shapes differ
    Cover       // fills the tile, the overflow cut off (a call's main view)
};

enum class VideoTilePixelFormat {
    RGBA32,     // bytes R, G, B, A
    BGRA32      // bytes B, G, R, A (what the camera backends deliver)
};

struct VideoTileStyle {
    Color backgroundColor = Color(24, 24, 28, 255);
    Color captionColor = Color(205, 205, 210, 255);
    float captionFontSize = 12.0f;
    float cornerRadius = 0.0f;      // clips the picture too
    Color borderColor = Color(0, 0, 0, 0);
    float borderWidth = 0.0f;
};

class UltraCanvasVideoTile : public UltraCanvasUIElement {
public:
    UltraCanvasVideoTile(const std::string& identifier = "VideoTile",
                         long x = 0, long y = 0, long w = 320, long h = 240);
    ~UltraCanvasVideoTile() override;

    // Any thread. The pixels are copied; a frame not shown yet is replaced
    // by a newer one. `stride` 0 = width * 4.
    void PushFrame(const uint8_t* pixels, int width, int height, int stride = 0,
                   VideoTilePixelFormat format = VideoTilePixelFormat::RGBA32);
    // Any thread: drops the picture, the caption shows again.
    void ClearFrame();

    // UI thread.
    bool HasFrame() const;
    void SetCaption(const std::string& caption);
    const std::string& GetCaption() const { return caption; }
    void SetFit(VideoTileFit fit);
    VideoTileFit GetFit() const { return fit; }
    // A self view is shown mirrored, as people expect to see themselves.
    // Applies from the next frame.
    void SetMirrored(bool mirrored);
    bool IsMirrored() const { return mirrored; }
    void SetStyle(const VideoTileStyle& newStyle);
    const VideoTileStyle& GetStyle() const { return style; }

    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;

private:
    // What a frame-pushing thread and the UI thread share. A posted upload
    // holds it, not the element: `owner` is cleared when the element goes.
    struct Inbox {
        std::mutex mutex;
        UltraCanvasVideoTile* owner = nullptr;
        std::vector<uint8_t> pixels;        // BGRA, width * 4 per row
        int width = 0;
        int height = 0;
        bool fresh = false;                 // not uploaded yet
        bool cleared = false;
        bool uploadPosted = false;
    };

    void Upload();          // UI thread: the newest frame into the pixmap

    std::shared_ptr<Inbox> inbox;
    std::shared_ptr<UCPixmap> pixmap;
    int frameWidth = 0;
    int frameHeight = 0;
    std::string caption;
    VideoTileFit fit = VideoTileFit::Contain;
    bool mirrored = false;
    VideoTileStyle style;
};

} // namespace UltraCanvas
