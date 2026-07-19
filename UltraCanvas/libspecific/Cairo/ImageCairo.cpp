// libspecific/Cairo/ImageCairo.cpp
// Cross-platform image loader implementation using PIMPL idiom
// Version: 2.1.0
// Last Modified: 2026-05-11
// Author: UltraCanvas Framework

#include "UltraCanvasImage.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasFileError.h"
#include "ImageCairo.h"
#ifdef HAS_LIBVIPS
#include "VipsQoiLoader.h"
#endif
#ifdef HAS_LIBRSVG
#include "SvgDocumentCairo.h"
#endif

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <mutex>
#include <memory>
#include <iostream>
#include <unordered_map>
#ifdef _WIN32
#include <stdlib.h>
#include <windows.h>
#endif
#include <fmt/os.h>
#include "UltraCanvasDebug.h"

#define HAS_PIXMAPS_CACHE 1

namespace UltraCanvas {
    struct UCPixmapCairoCacheEntry {
        std::shared_ptr<UCPixmapCairo> payload;
        std::chrono::steady_clock::time_point lastAccess;
        size_t GetEntrySize() {
            return payload->GetWidth() * payload->GetHeight() * 4 + sizeof(UCPixmapCairoCacheEntry) + sizeof(UCPixmapCairo);
        }
    };

    struct UCImageRasterCacheEntry {
        std::shared_ptr<UCImageRaster> payload;
        std::chrono::steady_clock::time_point lastAccess;
        size_t GetEntrySize() {
            return payload->GetDataSize() + sizeof(UCPixmapCairoCacheEntry) + sizeof(UCImageRaster);
        }
    };

#if HAS_PIXMAPS_CACHE
    UCCache<UCPixmapCairo, UCPixmapCairoCacheEntry> g_PixmapsCache(50 * 1024 * 1024);
#else
    UCCache<UCPixmapCairo, UCPixmapCairoCacheEntry> g_PixmapsCache(0);
#endif
    UCCache<UCImageRaster, UCImageRasterCacheEntry> g_ImagesCache(50 * 1024 * 1024);


    UCPixmapCairo::UCPixmapCairo(cairo_surface_t *surf) {
        surface = surf;
        if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
            debugOutput << "UCPixmapCairo: Invalid surface" << std::endl;
            return;
        }
        pixelsPtr = (uint32_t *)cairo_image_surface_get_data(surface);
        width = cairo_image_surface_get_width(surf);
        height = cairo_image_surface_get_height(surf);
    }

    UCPixmapCairo::UCPixmapCairo(int w, int h) {
        Init(w, h);
    }

    UCPixmapCairo::~UCPixmapCairo() {
        if (surface) {
            cairo_surface_destroy(surface);
        }
    }

    bool UCPixmapCairo::Init(int w, int h) {
        if (pixelsPtr && w == width && h == height) {
            Clear();
            return true;
        }

        width = w;
        height = h;
        if (surface) {
            cairo_surface_destroy(surface);
            surface = nullptr;
        }
        surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
            debugOutput << "UCPixmapCairo: Cant create surface" << std::endl;
            return false;
        }
        pixelsPtr = (uint32_t *)cairo_image_surface_get_data(surface);
        return true;
    }

    void UCPixmapCairo::SetPixel(int x, int y, uint32_t pixel) {
        if (pixelsPtr && x >= 0 && x < width && y >= 0 && y < height) {
            pixelsPtr[y * width + x] = pixel;
        }
    }

    uint32_t UCPixmapCairo::GetPixel(int x, int y) const {
        if (pixelsPtr && x >= 0 && x < width && y >= 0 && y < height) {
            return pixelsPtr[y * width + x];
        }
        return 0;
    }
    void UCPixmapCairo::Clear() {
        if (pixelsPtr) {
            memset(pixelsPtr, 0, width * height * 4);
            cairo_surface_mark_dirty(surface);
        }
    }

    void UCPixmapCairo::Flush() {
        cairo_surface_flush(surface);
    }

    void UCPixmapCairo::MarkDirty() {
        if (pixelsPtr) {
            cairo_surface_mark_dirty(surface);
        }
    }

    uint32_t* UCPixmapCairo::GetPixelData() {
        return pixelsPtr;
    }

    void UCPixmapCairo::SetDeviceScale(double scale) {
        if (!surface || scale <= 0.0) return;
        deviceScale = scale;
        cairo_surface_set_device_scale(surface, scale, scale);
        // width/height stay at raw pixel dims; GetWidth/GetHeight now divide
        // by deviceScale to report logical units to drawing code.
    }


#ifdef HAS_LIBVIPS
    bool UCImageRaster::InitializeImageSubsysterm(const char *programName) {
#ifdef _WIN32
        std::string exeDir = GetExecutableDir();
        auto setEnv = [&](const char *name, const std::string &val) {
            SetEnvironmentVariableA(name, val.c_str());
            _putenv_s(name, val.c_str());
            debugOutput << "  ENV " << name << "=" << val << std::endl;
        };
        setEnv("MAGICK_HOME", exeDir);
        setEnv("MAGICK_CONFIGURE_PATH",
               exeDir + "\\etc\\ImageMagick-7;" + exeDir + "\\lib\\ImageMagick-7.1.2\\config-Q16HDRI");
        setEnv("MAGICK_CODER_MODULE_PATH",
               exeDir + "\\lib\\ImageMagick-7.1.2\\modules-Q16HDRI\\coders");
#endif
        if (VIPS_INIT(programName ? programName : "UCImageSubsys") != 0) return false;
        vips_foreign_load_qoi_init_types();
        return true;
    }

    void UCImageRaster::ShutdownImageSubsysterm() {
        vips_shutdown();
    }
#else
    bool UCImageRaster::InitializeImageSubsysterm(const char *) { return true; }
    void UCImageRaster::ShutdownImageSubsysterm() {}
#endif

    UCImageRaster::~UCImageRaster() {
        if (ownData && imgDataPtr) {
            free(imgDataPtr);
            imgDataPtr = nullptr;
            imgDataSize = 0;
        }
    }
    
    std::shared_ptr<UCImageRaster> UCImageRaster::Get(const std::string &imagePath) {
        std::shared_ptr<UCImageRaster> im = g_ImagesCache.GetFromCache(imagePath);
        if (!im) {
#if HAS_PIXMAPS_CACHE
            im = UCImageRaster::Load(imagePath, true);
#else
            im = UCImageVips::Load(imagePath, false);
#endif
            if (im->IsValid()) {
                g_ImagesCache.AddToCache(imagePath, im);
            }
        }
        return im;
    }

    std::shared_ptr<UCImageRaster> UCImageRaster::GetFromMemory(const uint8_t* data, size_t dataSize) {
        char filename[200];
        snprintf(filename, sizeof(filename), ":mem:%p:%ld", data, dataSize);
        std::shared_ptr<UCImageRaster> im = g_ImagesCache.GetFromCache(filename);
        if (!im) {
            im = UCImageRaster::LoadFromMemory(data, dataSize);
            if (im->IsValid()) {
                g_ImagesCache.AddToCache(filename, im);
            }
        }
        return im;
    }

    bool UCImageRaster::LoadFileToMemory(const std::string &imagePath) {
        if (imgDataPtr && ownData) {
            free(imgDataPtr);
            imgDataPtr = nullptr;
            ownData = false;
        }
        try {
            std::ifstream file(imagePath, std::ios::binary | std::ios::ate);
            std::streamsize fileSize = file.tellg();
            file.seekg(0);
            imgDataPtr = (uint8_t *)malloc(fileSize);
            if (imgDataPtr) {
                file.read((char*)imgDataPtr, fileSize);
                imgDataSize = fileSize;
                ownData = true;
            } else {
                throw std::runtime_error("Not enough memory");
            }
            file.close();
        } catch (std::exception& err) {
            if (imgDataPtr) {
                free(imgDataPtr);
                imgDataPtr = nullptr;
            }
            debugOutput << "UCImage::Load: Failed Failed to load image to memory " << imagePath << " Err:" << err.what() << std::endl;
            std::string access = DescribeFileReadError(imagePath);
            errorMessage = !access.empty()
                ? access
                : (std::string("Could not read the image file: ") + imagePath + " (" + err.what() + ")");
        }
        return imgDataPtr != nullptr;
    }

#ifdef HAS_LIBVIPS
    std::shared_ptr<UCImageRaster> UCImageRaster::Load(const std::string &imagePath, bool loadOnlyHeader) {
        auto result = std::make_shared<UCImageRaster>(imagePath);
#ifdef HAS_LIBRSVG
        // SVG parse-once path: take the dimensions from the retained parsed
        // document instead of a vips header read (which parses the XML too).
        // CreatePixmap rasterizes from the same parsed document, so the file
        // is read and parsed exactly once while it stays cached.
        if (UCSvgDocument::IsSvgPath(imagePath)) {
            auto svgDoc = UCSvgDocument::Get(imagePath);
            if (svgDoc && svgDoc->IsValid()) {
                result->width  = static_cast<int>(std::lround(svgDoc->GetIntrinsicWidth()));
                result->height = static_cast<int>(std::lround(svgDoc->GetIntrinsicHeight()));
                return result;
            }
            // Parse failed — fall through to vips for its detailed error reporting.
        }
#endif
        try {
            vips::VImage vipsImage = result->GetVImage();
            result->width = vipsImage.width();
            result->height = vipsImage.height();
            result->ReadAnimationMetadata(vipsImage);
            if (!loadOnlyHeader) {
                result->LoadFileToMemory(imagePath);
            }
        } catch (vips::VError& err) {
            debugOutput << "UCImage::Load: Failed Failed to load image for " << imagePath << " Err:" << err.what() << std::endl;
            // Prefer a clear file-access reason (missing / locked / no permission);
            // otherwise the file opened but the format is unsupported or damaged.
            std::string access = DescribeFileReadError(imagePath);
            result->errorMessage = !access.empty()
                ? access
                : (std::string("The image format is not supported or the file is damaged: ")
                   + imagePath + " (" + err.what() + ")");
        }

        return result;
    }

    vips::VImage UCImageRaster::GetVImage() {
        if (imgDataPtr) { // Create VImage from memory buffer
            return vips::VImage::new_from_buffer(imgDataPtr, imgDataSize, "");
        } else { // Create VImage from file
            return vips::VImage::new_from_file(fileName.c_str());
        }
    }

// With these two functions:
    std::shared_ptr<UCImageRaster> UCImageRaster::LoadFromMemory(const uint8_t* data, size_t dataSize) {
        char filename[200];
        snprintf(filename, sizeof(filename), ":mem:%p:%ld", data, dataSize);
        auto result = std::make_shared<UCImageRaster>(filename);

        if (!data || dataSize == 0) {
            result->errorMessage = "Invalid data: null pointer or zero size";
            return result;
        }

        try {
            result->imgDataPtr = (uint8_t *)data;
            result->imgDataSize = dataSize;

            auto vipsImage = result->GetVImage();

            result->width = vipsImage.width();
            result->height = vipsImage.height();
            result->ReadAnimationMetadata(vipsImage);
        } catch (vips::VError& err) {
            debugOutput << "UCImageVips::LoadFromMemory: Failed to load image from buffer. Err:" << err.what() << std::endl;
            result->errorMessage = std::string("Failed to load image from memory buffer. Err:") + err.what();
        }

        return result;
    }
#else
    std::shared_ptr<UCImageRaster> UCImageRaster::Load(const std::string &imagePath, bool loadOnlyHeader) {
        auto result = std::make_shared<UCImageRaster>(imagePath);
        result->LoadFileToMemory(imagePath);
        result->errorMessage = "Image loading not yet implemented (no libvips)";
        return result;
    }

    std::shared_ptr<UCImageRaster> UCImageRaster::LoadFromMemory(const uint8_t* data, size_t dataSize) {
        char filename[200];
        snprintf(filename, sizeof(filename), ":mem:%p:%zu", data, dataSize);
        auto result = std::make_shared<UCImageRaster>(filename);
        if (!data || dataSize == 0) {
            result->errorMessage = "Invalid data: null pointer or zero size";
            return result;
        }
        result->imgDataPtr = (uint8_t *)data;
        result->imgDataSize = dataSize;
        result->errorMessage = "Image loading not yet implemented (no libvips)";
        return result;
    }
#endif

#ifdef HAS_LIBVIPS
    void UCImageRaster::ReadAnimationMetadata(vips::VImage& vipsImage) {
        nPages = 1;
        hasFrameDelays = false;
        try {
            if (vipsImage.get_typeof("n-pages") != 0) {
                nPages = std::max(1, vipsImage.get_int("n-pages"));
            }
            // Only animation-capable loaders (GIF, animated WebP) attach
            // per-frame delays; multi-page TIFF / PDF expose n-pages too but
            // must keep displaying as stills.
            hasFrameDelays = nPages > 1 && vipsImage.get_typeof("delay") != 0;
        } catch (vips::VError&) {
            nPages = 1;
            hasFrameDelays = false;
        }
    }

    std::shared_ptr<UCImageAnimation> UCImageRaster::GetAnimation() {
        if (animation) return animation;
        if (!IsAnimated() || animationDecodeFailed) return nullptr;
        try {
            // Re-open the source with n=-1 so every page loads, stacked
            // vertically ("toilet roll"); libvips composites GIF disposal so
            // each page is a complete frame.
            auto options = vips::VImage::option()->set("n", -1);
            vips::VImage strip = imgDataPtr
                ? vips::VImage::new_from_buffer(imgDataPtr, imgDataSize, "", options)
                : vips::VImage::new_from_file(fileName.c_str(), options);

            int pageH = strip.get_typeof("page-height") != 0
                        ? strip.get_int("page-height") : height;
            if (pageH <= 0 || strip.height() < pageH) {
                throw UCImageError("invalid page-height");
            }
            const int frameCount = strip.height() / pageH;
            if (frameCount <= 1) throw UCImageError("single frame");

            // Refuse to fully decode sequences that would not fit in a sane
            // amount of memory; the image keeps displaying frame 0.
            constexpr size_t kMaxAnimationBytes = 512u * 1024u * 1024u;
            const size_t decodedBytes = static_cast<size_t>(strip.width()) * pageH * 4u * frameCount;
            if (decodedBytes > kMaxAnimationBytes) {
                throw UCImageError("animation too large to decode fully");
            }

            std::vector<int> delays;
            if (strip.get_typeof("delay") != 0) {
                delays = strip.get_array_int("delay");
            }
            int loop = 0;
            if (strip.get_typeof("loop") != 0) {
                loop = std::max(0, strip.get_int("loop"));
            }

            // Materialize the whole strip once so the per-frame crops below
            // don't re-run the file decoder frameCount times.
            strip = strip.copy_memory();

            auto anim = std::make_shared<UCImageAnimation>();
            anim->width = strip.width();
            anim->height = pageH;
            anim->loopCount = loop;
            anim->frames.reserve(frameCount);
            anim->delaysMs.reserve(frameCount);
            for (int i = 0; i < frameCount; ++i) {
                vips::VImage frame = strip.extract_area(0, i * pageH, strip.width(), pageH);
                anim->frames.push_back(CreatePixmapFromVImage(frame));
                anim->delaysMs.push_back(i < static_cast<int>(delays.size()) ? delays[i] : 100);
            }
            animation = anim;
        } catch (std::exception& err) {
            debugOutput << "UCImageRaster::GetAnimation: cannot decode animation for "
                        << fileName << " Err:" << err.what() << std::endl;
            animationDecodeFailed = true;   // don't retry on every frame request
            return nullptr;
        }
        return animation;
    }
#else
    std::shared_ptr<UCImageAnimation> UCImageRaster::GetAnimation() {
        return nullptr;
    }
#endif

    std::string UCImageRaster::MakePixmapCacheKey(int w, int h, ImageFitMode fitMode, float scale) {
        char key[300];
        // Including `scale` prevents 1x and 2x rasterizations of the same
        // file/size from colliding in the shared cache (would otherwise show
        // the wrong pixmap on a window dragged between Retina/non-Retina).
        snprintf(key, sizeof(key) - 1, "%s?w:%dh:%dc:%dr:%g",
                 fileName.c_str(), w, h, static_cast<int>(fitMode), static_cast<double>(scale));
        return std::string(key);
    }

    std::shared_ptr<UCPixmapCairo> UCImageRaster::GetPixmap(int w, int h, ImageFitMode fitMode, float scale) {
        if (!errorMessage.empty() || fileName.empty()) {
            return nullptr;
        }
        if (!w || !h) {
            w = width;
            h = height;
        }
        if (scale <= 0.0f) scale = 1.0f;
#if HAS_PIXMAPS_CACHE
        std::string key = MakePixmapCacheKey(w, h, fitMode, scale);
        std::shared_ptr<UCPixmapCairo> pm = g_PixmapsCache.GetFromCache(key);
        if (!pm) {
            pm = CreatePixmap(w, h, fitMode, scale);
            if (pm) {
                g_PixmapsCache.AddToCache(key, pm);
            }
        }
#else
        std::shared_ptr<UCPixmapCairo> pm = CreatePixmap(w, h, fitMode, scale);
#endif
        return pm;
    }

#ifdef HAS_LIBVIPS
    std::shared_ptr<UCPixmapCairo> UCImageRaster::CreatePixmap(int w, int h, ImageFitMode fitMode, float scale) {
        if (scale <= 0.0f) scale = 1.0f;
#ifdef HAS_LIBRSVG
        // SVG fast path: rasterize from the cached parsed document — no file
        // read and no XML re-parse per requested size (each zoom level /
        // layout size only pays for the vector render itself).
        if (UCSvgDocument::IsSvgPath(fileName)) {
            auto svgDoc = UCSvgDocument::Get(fileName);
            if (svgDoc && svgDoc->IsValid()) {
                auto pm = svgDoc->RenderPixmap(w, h, fitMode, scale);
                if (pm) return pm;
            }
            // Fall through to the generic vips path on failure.
        }
#endif
        try {
            // Rasterize at backing-pixel resolution. For SVG (libvips uses
            // librsvg under the hood) this yields sharp edges at the target
            // device scale; for raster sources, libvips picks a scaled
            // thumbnail size and the pixmap is tagged so DrawPixmap downscales
            // back to logical units cleanly.
            const int rawW = static_cast<int>(w * scale);
            const int rawH = static_cast<int>(h * scale);
            auto options = vips::VImage::option();
            switch (fitMode) {
                case ImageFitMode::Fill:
                    options = options->set("height", rawH)->set("size", VipsSize::VIPS_SIZE_FORCE);
                    break;
                case ImageFitMode::Contain:
                    options = options->set("height", rawH);
                    break;
                case ImageFitMode::Cover:
                    options = options->set("height", rawH)->set("crop", VipsInteresting::VIPS_INTERESTING_CENTRE);
                    break;
                case ImageFitMode::ScaleDown:
                    options = options->set("height", rawH)->set("size", VipsSize::VIPS_SIZE_DOWN);
                    break;
                case ImageFitMode::NoScale:
                    // For NoScale, the source's intrinsic pixel grid is the
                    // truth — don't oversample (would just upscale a finite
                    // raster with no extra detail).
                    return CreatePixmapFromVImage(vips::VImage::thumbnail(fileName.c_str(), width, options));
            }
            std::shared_ptr<UCPixmapCairo> pm;
            if (imgDataPtr) {
                VipsBlob *blob = vips_blob_new(nullptr, imgDataPtr, imgDataSize);
                auto vimg = vips::VImage::thumbnail_buffer(blob, rawW, options);
                vips_area_unref(VIPS_AREA(blob));
                pm = CreatePixmapFromVImage(vimg);
            } else {
                pm = CreatePixmapFromVImage(vips::VImage::thumbnail(fileName.c_str(), rawW, options));
            }
            if (pm && scale != 1.0f) {
                pm->SetDeviceScale(scale);
            }
            return pm;
        } catch (vips::VError& err) {
            debugOutput << "UCImage::CreatePixmap: Failed to make pixmap for " << fileName << " Err:" << err.what() << std::endl;
            errorMessage = std::string("Failed to make pixmap Err:") + err.what();
        }
        return nullptr;
    }

    void rgba2bgra_premultiplied(uint32_t *src, uint32_t *dst, int n) {
        for (int x = 0; x < n; x++) {
            uint32_t rgba = GUINT32_FROM_BE(src[x]);
            uint8_t a = rgba & 0xff;

            uint32_t bgra;

            if (a == 0)
                bgra = 0;
            else if (a == 255)
                bgra = (rgba & 0x00ff00ff) |
                       (rgba & 0x0000ff00) << 16 |
                       (rgba & 0xff000000) >> 16;
            else {
                int r = (rgba >> 24) & 0xff;
                int g = (rgba >> 16) & 0xff;
                int b = (rgba >> 8) & 0xff;

                r = ((r * a) + 128) >> 8;
                g = ((g * a) + 128) >> 8;
                b = ((b * a) + 128) >> 8;

                bgra = (b << 24) | (g << 16) | (r << 8) | a;
            }

            dst[x] = GUINT32_TO_BE(bgra);
        }
    }

    std::shared_ptr<UCPixmapCairo> CreatePixmapFromVImage(vips::VImage vipsImage) {
        // Ensure 3-band RGB (handles grayscale)
        if (vipsImage.bands() < 3) {
            vipsImage = vipsImage.colourspace(VIPS_INTERPRETATION_sRGB);
        }

        vipsImage = vipsImage.cast(VIPS_FORMAT_UCHAR);

        // Add alpha channel if missing
        if (vipsImage.bands() == 3) {
            vipsImage = vipsImage.bandjoin(255);
        } else {
            if (vipsImage.bands() > 4) {
                vipsImage = vipsImage.extract_band(0, vips::VImage::option()->set("n", 4));
            }
        }

        int w = vipsImage.width();
        int h = vipsImage.height();

        cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w,h);
        if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
            throw UCImageError("Failed to create Cairo surface");
        }
        uint32_t *src = (uint32_t*)vipsImage.data();
        uint32_t *dst = (uint32_t*)cairo_image_surface_get_data(surface);
        if (!dst) {
            throw UCImageError("Failed to get surface data");
        }

        rgba2bgra_premultiplied(src, dst, w * h);

        cairo_surface_mark_dirty(surface);

        return std::make_shared<UCPixmapCairo>(surface);
    }
#else
    std::shared_ptr<UCPixmapCairo> UCImageRaster::CreatePixmap(int, int, ImageFitMode, float) {
        return nullptr;
    }
#endif


    int ColorDepthToBitDepth(UltraCanvas::UCImageSave::ColorDepth depth) {
        switch (depth) {
            case UCImageSave::ColorDepth::Monochrome_1bit:
                return 1;
            case UCImageSave::ColorDepth::Indexed_4bit:
                return 4;
            case UCImageSave::ColorDepth::Indexed_8bit:
                return 8;
            case UCImageSave::ColorDepth::RGB_8bit:
                return 8;  // 8 bits per channel
            case UCImageSave::ColorDepth::RGB_16bit:
                return 16; // 16 bits per channel
            default:
                return 8;
        }
    }

    // Helper function to check if ColorDepth requires palette mode (indexed colors)
    bool ColorDepthIsPaletteMode(UCImageSave::ColorDepth depth) {
        return depth <= UCImageSave::ColorDepth::Indexed_8bit;
    }

    // Helper function to convert ColorDepth to AVIF/HEIF bitdepth (8, 10, or 12)
    int ColorDepthToHeifBitDepth(UltraCanvas::UCImageSave::ColorDepth depth) {
        using namespace UltraCanvas::UCImageSave;
        switch (depth) {
            case ColorDepth::Monochrome_1bit:
            case ColorDepth::Indexed_4bit:
            case ColorDepth::Indexed_8bit:
            case ColorDepth::RGB_8bit:
                return 8;
            case ColorDepth::RGB_16bit:
                return 12;  // 12-bit for higher depths
            default:
                return 8;
        }
    }

#ifdef HAS_LIBVIPS
    std::string UCImageRaster::Save(const std::string &imagePath, const UCImageSave::ImageExportOptions& opts) {
        // libvips appends errors to a process-wide buffer and VError::what()
        // returns the whole buffer. Drop leftovers from earlier operations
        // (e.g. recoverable "bad seek" noise from a previous HEIF decode) so
        // a failure here reports only its own cause.
        vips_error_clear();
        vips::VImage vImg;
        try {
            vImg = GetVImage();
        } catch (vips::VError& err) {
            debugOutput << "UCImageRaster::Save: Failed to load image. Err:" << err.what() << std::endl;
            return err.what();
        }
        return ExportVImage(vImg, imagePath, opts);
    }

    std::string ExportVImage(vips::VImage vImg, const std::string &imagePath, const UCImageSave::ImageExportOptions& opts) {
        vips_error_clear();   // see UCImageRaster::Save — keep error reports scoped to this export
        // Handle resize if target dimensions specified
        if (!opts.preserveTransparency && vImg.bands() > 3) {
            vImg = vImg.extract_band(0, vips::VImage::option()->set("n", 3));
        }
        if (opts.targetWidth > 0 || opts.targetHeight > 0) {
            int targetW = opts.targetWidth > 0 ? opts.targetWidth : vImg.width();
            int targetH = opts.targetHeight > 0 ? opts.targetHeight : vImg.height();

            if (opts.maintainAspectRatio) {
                double scaleW = static_cast<double>(targetW) / vImg.width();
                double scaleH = static_cast<double>(targetH) / vImg.height();
                double scale = std::min(scaleW, scaleH);
                vImg = vImg.resize(scale);
            } else {
                vImg = vImg.resize(static_cast<double>(targetW) / vImg.width(),
                                   vips::VImage::option()->set("vscale", static_cast<double>(targetH) / vImg.height()));
            }
        }


        try {
            int bitDepth = 8;
            switch (opts.format) {
                case UCImageSaveFormat::GIF:
                    bitDepth = std::min(8, ColorDepthToBitDepth(opts.gif.colorDepth));
                    if (vips_type_find("VipsOperation", "gifsave") != 0) {
                        vImg.gifsave(imagePath.c_str(), vips::VImage::option()
                                ->set("bitdepth", bitDepth)
                                ->set("interlace", opts.gif.interlace)
                                ->set("dither", opts.gif.dithering ? 1.0 : 0.0));
                    } else if (vips_type_find("VipsOperation", "magicksave") != 0) {
                        // Some libvips builds ship without cgif (MSYS2 passes
                        // -Dcgif=disabled), so the native gifsave operation
                        // does not exist. ImageMagick's GIF coder quantizes to
                        // <=256 colours itself; interlace/dither have no
                        // magicksave equivalent, so only bitdepth carries over.
                        vImg.magicksave(imagePath.c_str(), vips::VImage::option()
                                ->set("format", "gif")
                                ->set("bitdepth", bitDepth));
                    } else {
                        return "GIF export is not supported by this libvips build "
                               "(neither gifsave nor magicksave is available)";
                    }
                    break;

                case UCImageSaveFormat::PNG: {
                    bool usePalette = (opts.png.colorDepth <= UCImageSave::ColorDepth::Indexed_8bit);
                    bitDepth = std::min(16, ColorDepthToBitDepth(opts.png.colorDepth));

                    auto pngOpts = vips::VImage::option()
                            ->set("compression", opts.png.compressionLevel)
                            ->set("interlace", opts.png.interlace)
                            ->set("bitdepth", bitDepth);

                    if (usePalette) {
                        // Enable palette mode for indexed color depths
                        pngOpts->set("palette", true);
                    }

                    vImg.pngsave(imagePath.c_str(), pngOpts);
                    break;
                }

                case UCImageSaveFormat::JPEG:
                    vImg.jpegsave(imagePath.c_str(), vips::VImage::option()
                            ->set("Q", opts.jpeg.quality)
                            ->set("interlace", opts.jpeg.progressive)
                            ->set("optimize_coding", opts.jpeg.optimizeHuffman)
                            ->set("subsample_mode", static_cast<int>(opts.jpeg.subsampling)));
                    break;

                case UCImageSaveFormat::WEBP:
                    vImg.webpsave(imagePath.c_str(), vips::VImage::option()
                            ->set("Q", opts.webp.quality)
                            ->set("lossless", opts.webp.lossless)
                            ->set("effort", opts.webp.effort)
                            ->set("alpha_q", opts.webp.alphaQuality));
                    break;

                case UCImageSaveFormat::AVIF: {
                    int heifBitDepth = ColorDepthToHeifBitDepth(opts.avif.colorDepth);
                    vImg.heifsave(imagePath.c_str(), vips::VImage::option()
                            ->set("Q", opts.avif.quality)
                            ->set("lossless", opts.avif.lossless)
                            ->set("effort", 9 - opts.avif.speed)  // vips effort is inverse of speed
                            ->set("compression", VIPS_FOREIGN_HEIF_COMPRESSION_AV1)
                            ->set("bitdepth", heifBitDepth));
                    break;
                }

                case UCImageSaveFormat::HEIF: {
                    int heifBitDepth = ColorDepthToHeifBitDepth(opts.heif.colorDepth);
                    vImg.heifsave(imagePath.c_str(), vips::VImage::option()
                            ->set("Q", opts.heif.quality)
                            ->set("lossless", opts.heif.lossless)
                            ->set("compression", VIPS_FOREIGN_HEIF_COMPRESSION_HEVC)
                            ->set("bitdepth", heifBitDepth));
                    break;
                }

                case UCImageSaveFormat::TIFF: {
                    VipsForeignTiffCompression tiffComp = VIPS_FOREIGN_TIFF_COMPRESSION_NONE;
                    switch (opts.tiff.compression) {
                        case UCImageSave::TiffCompression::NoCompression:
                            tiffComp = VIPS_FOREIGN_TIFF_COMPRESSION_NONE;
                            break;
                        case UCImageSave::TiffCompression::JPEGCompression:
                            tiffComp = VIPS_FOREIGN_TIFF_COMPRESSION_JPEG;
                            break;
                        case UCImageSave::TiffCompression::DeflateCompression:
                            tiffComp = VIPS_FOREIGN_TIFF_COMPRESSION_DEFLATE;
                            break;
                        case UCImageSave::TiffCompression::PackBitsCompression:
                            tiffComp = VIPS_FOREIGN_TIFF_COMPRESSION_PACKBITS;
                            break;
                        case UCImageSave::TiffCompression::LZWCompression:
                            tiffComp = VIPS_FOREIGN_TIFF_COMPRESSION_LZW;
                            break;
                        case UCImageSave::TiffCompression::ZSTDCompression:
                            tiffComp = VIPS_FOREIGN_TIFF_COMPRESSION_ZSTD;
                            break;
                        case UCImageSave::TiffCompression::WEBPCompression:
                            tiffComp = VIPS_FOREIGN_TIFF_COMPRESSION_WEBP;
                            break;
                    }
                    vImg.tiffsave(imagePath.c_str(), vips::VImage::option()
                            ->set("compression", tiffComp));
                    break;
                }

                case UCImageSaveFormat::JXL: {
//                    int jxlBitDepth = ColorDepthToBitDepth(opts.jxl.colorDepth);
                    vImg.jxlsave(imagePath.c_str(), vips::VImage::option()
                            ->set("Q", opts.jxl.quality)
                            ->set("lossless", opts.jxl.lossless)
                            ->set("effort", opts.jxl.effort));
                    break;
                }

                case UCImageSaveFormat::JPEG2000:
                    vImg.jp2ksave(imagePath.c_str(), vips::VImage::option()
                            ->set("lossless", opts.jpeg2000.lossless)
                            ->set("Q", opts.jpeg2000.quality));
                    break;

                case UCImageSaveFormat::PPM:
                case UCImageSaveFormat::PGM:
                case UCImageSaveFormat::PBM:
                case UCImageSaveFormat::PFM:
                    // libvips ppmsave auto-detects PPM/PGM/PBM/PFM from the
                    // filename extension; we only control text-vs-binary.
                    vImg.ppmsave(imagePath.c_str(), vips::VImage::option()
                            ->set("ascii", !opts.pnm.binary));
                    break;

                case UCImageSaveFormat::ICO:
                case UCImageSaveFormat::BMP: {
                    auto magickOpts = vips::VImage::option();
                    if (opts.format == UCImageSaveFormat::BMP) {
                        magickOpts->set("format", "bmp");
                    } else {
                        magickOpts->set("format", "ico");
                    }
                    vImg.magicksave(imagePath.c_str(), magickOpts);
                    break;
                }

                case UCImageSaveFormat::HDR:
                    vImg.radsave(imagePath.c_str(), vips::VImage::option());
                    break;

                case UCImageSaveFormat::FITS:
                    vImg.fitssave(imagePath.c_str(), vips::VImage::option());
                    break;

                case UCImageSaveFormat::QOI: {
                    // QOI export goes through ImageMagick. The custom
                    // VipsQoiLoader plugin only registers a LOADER; if the
                    // local libvips/ImageMagick build lacks QOI write
                    // support the runtime probe filter in the dialog will
                    // hide this option.
                    vImg.magicksave(imagePath.c_str(), vips::VImage::option()
                            ->set("format", "qoi"));
                    break;
                }

                case UCImageSaveFormat::TGA: {
                    auto magickOpts = vips::VImage::option()->set("format", "tga");
                    if (opts.tga.rleCompression) {
                        magickOpts->set("compression", "RLE");
                    } else {
                        magickOpts->set("compression", "NoCompression");
                    }
                    vImg.magicksave(imagePath.c_str(), magickOpts);
                    break;
                }

                case UCImageSaveFormat::PCX: {
                    auto magickOpts = vips::VImage::option()->set("format", "pcx");
                    // ImageMagick's PCX writer applies RLE by default; the
                    // option here is informational and may be ignored by the
                    // delegate, but we set it explicitly for clarity.
                    if (!opts.pcx.rleCompression) {
                        magickOpts->set("compression", "NoCompression");
                    }
                    vImg.magicksave(imagePath.c_str(), magickOpts);
                    break;
                }

                case UCImageSaveFormat::EXR: {
                    auto magickOpts = vips::VImage::option()->set("format", "exr");
                    const char* exrComp = "ZIP";
                    switch (opts.exr.compression) {
                        case UCImageSave::ExrExportOptions::Compression::NoCompression: exrComp = "None"; break;
                        case UCImageSave::ExrExportOptions::Compression::RLE:           exrComp = "RLE"; break;
                        case UCImageSave::ExrExportOptions::Compression::ZIP:           exrComp = "ZIP"; break;
                        case UCImageSave::ExrExportOptions::Compression::PIZ:           exrComp = "PIZ"; break;
                        case UCImageSave::ExrExportOptions::Compression::PXR24:         exrComp = "PXR24"; break;
                        case UCImageSave::ExrExportOptions::Compression::B44:           exrComp = "B44"; break;
                    }
                    magickOpts->set("compression", exrComp);
                    vImg.magicksave(imagePath.c_str(), magickOpts);
                    break;
                }

                case UCImageSaveFormat::DPX:
                    vImg.magicksave(imagePath.c_str(), vips::VImage::option()
                            ->set("format", "dpx")
                            ->set("quality", opts.dpx.bitDepth));
                    break;

                case UCImageSaveFormat::CIN:
                    vImg.magicksave(imagePath.c_str(), vips::VImage::option()
                            ->set("format", "cin")
                            ->set("quality", opts.cin.bitDepth));
                    break;

                case UCImageSaveFormat::PSD:
                    vImg.magicksave(imagePath.c_str(), vips::VImage::option()
                            ->set("format", "psd")
                            ->set("compression", opts.psd.compressed ? "RLE" : "NoCompression"));
                    break;

                case UCImageSaveFormat::SGI:
                    vImg.magicksave(imagePath.c_str(), vips::VImage::option()
                            ->set("format", "sgi")
                            ->set("compression", opts.sgi.rleCompression ? "RLE" : "NoCompression"));
                    break;

                case UCImageSaveFormat::FARBFELD:
                    vImg.magicksave(imagePath.c_str(), vips::VImage::option()
                            ->set("format", "farbfeld"));
                    break;

                default:
                    debugOutput << "UCImageRaster::Save: Failed save image: " << imagePath
                              << " Err: Unsupported format (" << static_cast<int>(opts.format) << ")" << std::endl;
                    return fmt::format("Unsupported format ({})", static_cast<int>(opts.format));
            }
        } catch (vips::VError& err) {
            debugOutput << "UCImageRaster::Save: Failed save image: " << imagePath << " Err:" << err.what() << std::endl;
            // Prefer a clear destination reason (folder missing / read-only /
            // locked / no permission / disk full) over the raw vips message.
            std::string writeErr = DescribeFileWriteError(imagePath);
            return !writeErr.empty() ? writeErr
                                     : (std::string("Could not save image: ") + err.what());
        }
        return "";
    }

    // ===== Runtime libvips capability probes =====
    // vips_foreign_find_save returns the GType name of the saver vips would
    // pick for the given filename (NULL if no saver is compiled in for that
    // extension). Use a dummy filename — only the extension is inspected.
    bool VipsCanSave(const std::string& extensionWithDot) {
        if (extensionWithDot.empty()) return false;
        const std::string dummy = std::string("probe") + extensionWithDot;
        return vips_foreign_find_save(dummy.c_str()) != nullptr;
    }

    namespace {
        // Collects the suffix list every VipsForeignLoad subclass advertises.
        // vips_foreign_find_load() cannot probe a hypothetical filename — it
        // stats and content-sniffs the real file — so extension probing has to
        // read the loader class metadata instead.
        void* CollectLoaderSuffixes(GType type, void* a) {
            auto* out = static_cast<std::vector<std::string>*>(a);
            auto* cls = VIPS_OBJECT_CLASS(g_type_class_ref(type));
            auto* fc  = VIPS_FOREIGN_CLASS(cls);
            if (fc->suffs) {
                for (const char* const* s = fc->suffs; *s; ++s) {
                    out->emplace_back(*s);
                }
            }
            return nullptr;   // keep iterating over all subclasses
        }

        const std::vector<std::string>& LoaderSuffixes() {
            // Collected once, but only after vips has been initialized —
            // before that the base type does not exist yet and the (empty)
            // result must not be cached.
            static std::vector<std::string> suffixes;
            static bool collected = false;
            if (!collected) {
                GType base = g_type_from_name("VipsForeignLoad");
                if (base) {
                    vips_type_map_all(base, CollectLoaderSuffixes, &suffixes);
                    collected = true;
                }
            }
            return suffixes;
        }
    }

    bool VipsCanLoad(const std::string& extensionWithDot) {
        if (extensionWithDot.empty()) return false;
        std::string wanted = extensionWithDot;
        std::transform(wanted.begin(), wanted.end(), wanted.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const auto& suffixes = LoaderSuffixes();
        return std::find(suffixes.begin(), suffixes.end(), wanted) != suffixes.end();
    }

    bool VipsHasMagickLoadFallback() {
        // ImageMagick's loader advertises no suffixes (it content-sniffs), yet
        // handles most legacy raster formats (BMP, TGA, PSD, PCX, ...). Callers
        // that probe a known-raster extension can treat its presence as "this
        // will load", which VipsCanLoad's suffix matching cannot express.
        return vips_type_find("VipsOperation", "magickload") != 0;
    }
#else
    std::string UCImageRaster::Save(const std::string &, const UCImageSave::ImageExportOptions&) {
        return "Image export not yet implemented (no libvips)";
    }
#endif
}
