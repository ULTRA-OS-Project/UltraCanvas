// libspecific/Cairo/ImageCairo.h
// Base interface for cross-platform image handling in UltraCanvas
// Version: 1.4.0 - GetFresh: the cached image checked against the file
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework
#pragma once
#ifndef IMAGECAIRO_H
#define IMAGECAIRO_H
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasFileStamp.h"
#ifdef HAS_LIBVIPS
#include "PixelFX/PixelFX.h"
#endif
#include <string>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <cairo/cairo.h>
#ifdef HAS_LIBVIPS
#include <vips/vips8>
#endif
#undef Rect

namespace UltraCanvas {
// ===== CROSS-PLATFORM IMAGE CLASS =====
    class UCImageError : public std::runtime_error {
    public:
        UCImageError(const std::string& msg) : std::runtime_error(msg) {}
    };

    class UCPixmapCairo : public IPixmap {
        cairo_surface_t * surface = nullptr;
        uint32_t* pixelsPtr = nullptr;
        // Raw pixel dimensions of the underlying Cairo surface buffer.
        // SetPixel/GetPixel/Clear and any direct buffer access use these.
        int width = 0;
        int height = 0;
        // 1.0 unless SetDeviceScale was called. When > 1 the surface holds
        // raw pixels for HiDPI but presents itself at logical size = raw / scale.
        double deviceScale = 1.0;
    public:
        UCPixmapCairo() = default;
        explicit UCPixmapCairo(int w, int h);
        explicit UCPixmapCairo(cairo_surface_t * surf);
        ~UCPixmapCairo();

        bool Init(int w, int h) override;
        cairo_surface_t * GetSurface() const { return surface; };
        void SetPixel(int x, int y, uint32_t pixel) override;
        uint32_t GetPixel(int x, int y) const override;
        // Logical dimensions presented to drawing code. These are what
        // DrawPixmap should use for layout math; the source pattern's
        // device_scale handles the raw-to-logical mapping.
        int GetWidth() const override {
            return deviceScale > 0.0 ? static_cast<int>(width / deviceScale) : width;
        }
        int GetHeight() const override {
            return deviceScale > 0.0 ? static_cast<int>(height / deviceScale) : height;
        }
        // Raw pixel buffer dimensions (e.g. for direct memcpy/byte-level
        // operations that need the actual stride/footprint).
        int GetRawWidth() const { return width; }
        int GetRawHeight() const { return height; }
        uint32_t* GetPixelData() override;
        bool IsValid() const override { return pixelsPtr != nullptr; }
        void Flush() override;
        void MarkDirty() override;
        void Clear() override;

        // Tag this pixmap as HiDPI: pixels were rasterized at scale × the
        // intended logical size. Surface gets cairo_surface_set_device_scale
        // so DrawPixmap composes correctly; GetWidth/GetHeight start
        // returning the logical size.
        void SetDeviceScale(double scale);
        double GetDeviceScale() const { return deviceScale; }
    };

    // ===== ANIMATED IMAGE FRAME SEQUENCE =====
    // Fully decoded frames of an animated image (GIF, animated WebP). Every
    // frame is a complete composited image (libvips resolves GIF frame
    // disposal while loading), so frame N can be drawn without frame N-1.
    // Produced lazily by UCImageRaster::GetAnimation() and shared between all
    // elements displaying the same image. Playback timing lives in
    // UCImageAnimationController (include/UltraCanvasImageAnimation.h).
    class UCImageAnimation {
    public:
        std::vector<std::shared_ptr<UCPixmapCairo>> frames;
        std::vector<int> delaysMs;   // per-frame display time; same length as frames
        int loopCount = 0;           // 0 = repeat forever, N = play N times
        int width = 0;               // frame dimensions (all frames equal)
        int height = 0;

        int GetFrameCount() const { return static_cast<int>(frames.size()); }

        std::shared_ptr<UCPixmapCairo> GetFramePixmap(int index) const {
            if (index < 0 || index >= static_cast<int>(frames.size())) return nullptr;
            return frames[index];
        }

        // Delay before advancing past frame `index`. Zero / near-zero encoded
        // delays are shown for 100ms — the browser convention many GIFs rely on.
        int GetFrameDelayMs(int index) const {
            int d = (index >= 0 && index < static_cast<int>(delaysMs.size()))
                    ? delaysMs[index] : 100;
            return d < 20 ? 100 : d;
        }

        size_t GetMemoryBytes() const {
            return frames.size() * static_cast<size_t>(width) * height * 4;
        }
    };

    class UCImageRaster {
    private:
        int width = 0;
        int height = 0;
        uint8_t *imgDataPtr = nullptr;
        size_t imgDataSize = 0;
        bool ownData = false;
        std::string fileName;

        // Transparency state, resolved on the first HasTransparency() call and
        // kept: -1 unknown, 0 opaque, 1 shows what is behind it.
        int transparency = -1;

        // Animation state ("n-pages" / "delay" loader metadata). Multi-page
        // stills (TIFF, PDF) have pages but no delays and stay static.
        int nPages = 1;
        bool hasFrameDelays = false;
        bool animationDecodeFailed = false;
        std::shared_ptr<UCImageAnimation> animation;   // lazy decode cache

        // The file as it was when this raster was read from it: its size and
        // modification time, taken just BEFORE the read, so a write that races
        // the read leaves the stamp older than what was read and the next
        // GetFresh() reads the file again. Unset for an image that did not
        // come from a file (LoadFromMemory) or whose file could not be
        // examined. Written by Load() before the raster is shared, read-only
        // after.
        FileStamp source;
        // The file now differs from the stamp above (changed, gone, or back
        // after being gone). One stat; never call it on a paint path.
        bool SourceChangedOnDisk() const;

        bool LoadFileToMemory(const std::string &imagePath);

#ifdef HAS_LIBVIPS
        // Reads "n-pages" / "delay" metadata off a freshly loaded header.
        void ReadAnimationMetadata(vips::VImage& vipsImage);
#endif

    public:
        std::string errorMessage;

        // ===== CONSTRUCTORS =====
        UCImageRaster() {};
        UCImageRaster(const std::string& fn) : fileName(fn) {};
        ~UCImageRaster();

        // The image at `path`, from the process-wide cache when it holds it.
        // Keyed by the path alone and never checked against the disk, which
        // is what makes it cheap enough for a paint path asking for the same
        // icon every frame - and why a file saved over keeps answering with
        // its old picture here until something calls GetFresh() or
        // RemoveFromCache() for it.
        static std::shared_ptr<UCImageRaster> Get(const std::string &path);
        // Get(), for a caller that must see the file as it is NOW: a file
        // browser's thumbnails, a viewer opening a file. The cached raster is
        // checked against the file's current size and modification time, and
        // when the file has changed since it was read - saved over, rewritten,
        // replaced - or the cached copy failed to decode (a file caught while
        // it was still being written, or held by a virus scanner), everything
        // cached for the path is dropped (RemoveFromCache) and the file is
        // read again. Costs one stat of the file, so it does not belong in a
        // paint path; call it where the file is about to be read anyway.
        static std::shared_ptr<UCImageRaster> GetFresh(const std::string &path);
        // The check GetFresh() makes, without the reload: when the cache holds
        // `path` and the file has changed since it was read, everything cached
        // for it is dropped (RemoveFromCache) and true is returned, so the
        // next Get() reads the file as it is now. Nothing cached, or the file
        // unchanged: false, and nothing is read. A cached decode failure is
        // left alone, unlike GetFresh(): this is for a background check that
        // runs again and again (UltraCanvasImageFileWatch), and dropping a
        // broken file each time would decode it again on every pass.
        static bool RemoveFromCacheIfChanged(const std::string &path);
        // The version of its file this image was read from: size and
        // modification time, taken before the read (invalid for an image
        // loaded from memory, or whose file could not be examined). No I/O -
        // a paint path hands it to UltraCanvasImageFileWatch so the watch
        // knows which version the view is showing.
        FileStamp GetSourceStamp() const { return source; }
        // Evict every cached artifact for `path`: the loaded raster, all of its
        // derived pixmaps (every requested size/fit/scale), for SVG sources
        // the parsed document, and libvips' own cached operations, which are
        // keyed by file name too and would otherwise hand the next load the
        // old file's header. Use after a file on disk changes so the next
        // Get()/DrawImage re-reads it instead of serving the stale copy.
        static void RemoveFromCache(const std::string &path);
        static std::shared_ptr<UCImageRaster> Load(const std::string &path, bool loadOnlyHeader = true);
        static std::shared_ptr<UCImageRaster> LoadFromMemory(const uint8_t* data, size_t dataSize);
        static std::shared_ptr<UCImageRaster> LoadFromMemory(const std::vector<uint8_t>& data) {
            return LoadFromMemory(data.data(), data.size());
        };
        static std::shared_ptr<UCImageRaster> GetFromMemory(const uint8_t* data, size_t dataSize);

        std::string Save(const std::string &imagePath, const UCImageSave::ImageExportOptions& options);

        // `scale` is the device pixel ratio of the destination render context
        // (1.0 = standard, 2.0 = Retina). When > 1.0 the pixmap is rasterized
        // at `width*scale × height*scale` raw pixels and tagged with
        // cairo_surface_set_device_scale() so it presents the requested
        // logical `width × height` to consumers. This keeps SVG icons crisp
        // on HiDPI displays without changing draw call sites.
        std::shared_ptr<UCPixmapCairo> GetPixmap(int width = 0, int height = 0,
                                                 ImageFitMode fitMode = ImageFitMode::Contain,
                                                 float scale = 1.0f);
        std::shared_ptr<UCPixmapCairo> CreatePixmap(int width, int height,
                                                    ImageFitMode fitMode = ImageFitMode::Contain,
                                                    float scale = 1.0f);
        // Decode at native size with the alpha band inverted BEFORE
        // premultiplication. Some producers (Xara .xar embedded bitmaps)
        // store transparency, not alpha, in the channel (255 = fully
        // transparent); a normal decode zeroes the colour of exactly the
        // pixels such a bitmap means to show. Images without an alpha band
        // decode normally (opaque). Not cached.
        std::shared_ptr<UCPixmapCairo> CreatePixmapAlphaInverted();
        std::string MakePixmapCacheKey(int w, int h, ImageFitMode fitMode, float scale);

        // Get aspect ratio
        float GetAspectRatio() const {
            if (height == 0) return 1.0f;
            return static_cast<float>(width) / static_cast<float>(height);
        }
        int GetWidth() const { return width; }
        int GetHeight() const { return height; }

        // ===== TRANSPARENCY =====
        // True when something behind the image can show through it: the image
        // carries an alpha channel that is not fully opaque, or it is a vector
        // document (SVG), which paints over whatever is beneath it. Answers
        // the question a viewer asks before offering a backdrop colour.
        //
        // The answer is worked out once and kept. An alpha channel that turns
        // out to be fully opaque counts as opaque — the common "PNG with an
        // unused alpha channel" case — which costs one decode of the alpha
        // channel, so images past 16 megapixels take the channel's presence at
        // face value instead. An image with no alpha channel is answered from
        // its header alone. Without libvips (and for an unreadable file) the
        // answer is false.
        bool HasTransparency();

        // ===== ANIMATION (GIF / animated WebP) =====
        // True when the loader reported multiple pages WITH per-frame delays.
        bool IsAnimated() const { return nPages > 1 && hasFrameDelays; }
        int GetFrameCount() const { return nPages; }
        // Decode every frame (lazily, cached). Returns null for still images,
        // when decoding fails, or when the fully decoded sequence would be
        // unreasonably large — the image then displays as a still (frame 0).
        std::shared_ptr<UCImageAnimation> GetAnimation();

        // ===== EMBEDDED METADATA =====
        // Reads one metadata field the loader attached to the image, by the
        // name libvips gives it. EXIF fields are named
        // "exif-ifd<N>-<TagName>" — the capture time an album or a gallery
        // sorts by is "exif-ifd2-DateTimeOriginal", and the camera that took
        // the shot is "exif-ifd0-Make" / "exif-ifd0-Model".
        //
        // The raw value carries libvips' trailing " (…, ASCII, N components…)"
        // annotation; `stripAnnotation` (the default) removes it so the caller
        // gets just the value. Returns "" when the field is absent, the file
        // cannot be read, or the build has no libvips — callers must treat an
        // empty result as "unknown", never as an error.
        std::string GetMetadataString(const std::string& key,
                                      bool stripAnnotation = true);

#ifdef HAS_LIBVIPS
        vips::VImage GetVImage();
#endif

        size_t GetDataSize() {
            return sizeof(UCImageRaster) + 250 + imgDataSize
                   + (animation ? animation->GetMemoryBytes() : 0);
        }
        bool IsValid() { return !fileName.empty() && errorMessage.empty() && width > 0;};


        static bool InitializeImageSubsysterm(const char* programName);
        static void ShutdownImageSubsysterm();
    };

    // ===== QOI FILE EXPORT =====
    // Writes a real .qoi file - the interchange format, unlike
    // QoiCompressPixmap's in-process blobs (QoiPixmapCodec.h) - through the
    // bundled QOI encoder (qoi.cpp). Nothing but Cairo is involved, so this
    // works in builds without libvips and without an ImageMagick QOI write
    // delegate, which is what `UCImageSaveFormat::QOI` needs. The pixmap's
    // premultiplied ARGB32 pixels are un-premultiplied to straight RGBA
    // first, as the format requires. Returns "" on success, else the reason.
    std::string SavePixmapAsQoiFile(UCPixmapCairo& pixmap, const std::string& filePath);

    // The same, straight from an image file of any format the pipeline loads
    // (SVG included, rasterized by the vector renderer). `maxEdge` > 0 fits
    // the image into a maxEdge x maxEdge box keeping its aspect ratio - what
    // an application storing a picture as an icon wants; 0 keeps the source
    // resolution. Returns "" on success, else the reason.
    std::string SaveImageFileAsQoi(const std::string& sourcePath,
                                   const std::string& destPath,
                                   int maxEdge = 0);

#ifdef HAS_LIBVIPS
    std::shared_ptr<UCPixmapCairo> CreatePixmapFromVImage(vips::VImage vipsImage);
    std::string ExportVImage(vips::VImage vImg, const std::string &imagePath, const UCImageSave::ImageExportOptions& opts);

    // Runtime probes against the installed libvips build. Pass extensions
    // with leading dot (".png", ".exr"). Return true if the local libvips
    // build has a saver/loader compiled in for that extension.
    bool VipsCanSave(const std::string& extensionWithDot);
    bool VipsCanLoad(const std::string& extensionWithDot);
    // True when libvips has the ImageMagick load delegate. magickload
    // advertises no suffixes (it content-sniffs), so VipsCanLoad cannot see
    // it; for known raster extensions its presence means "will load".
    bool VipsHasMagickLoadFallback();
#endif
}
#endif