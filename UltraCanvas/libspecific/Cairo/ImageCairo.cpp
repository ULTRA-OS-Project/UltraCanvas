// libspecific/Cairo/ImageCairo.cpp
// Cross-platform image loader implementation using PIMPL idiom
// Version: 2.0.0
// Last Modified: 2025-10-24
// Author: UltraCanvas Framework

#include "UltraCanvasImage.h"
#include "UltraCanvasUtils.h"
#include "ImageCairo.h"
#ifndef _WIN32
#include "VipsQoiLoader.h"
#endif

#include <algorithm>
#include <fstream>
#include <sstream>
#include <mutex>
#include <memory>
#include <iostream>
#include <unordered_map>
#include <fmt/os.h>

#define HAS_PIXMAPS_CACHE 1

namespace UltraCanvas {
    typedef UCCache<UCPixmapCairo> UCPixmapsCache;
#if HAS_PIXMAPS_CACHE
    UCPixmapsCache g_PixmapsCache(50 * 1024 * 1024);
#else
    UCPixmapsCache g_PixmapsCache(0);
#endif
    typedef UCCache<UCImageRaster> UCImagesCache;
    UCImagesCache g_ImagesCache(50 * 1024 * 1024);


    UCPixmapCairo::UCPixmapCairo(cairo_surface_t *surf) {
        surface = surf;
        if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
            std::cerr << "UCPixmapCairo: Invalid surface" << std::endl;
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
            std::cerr << "UCPixmapCairo: Cant create surface" << std::endl;
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

    size_t UCPixmapCairo::GetDataSize() {
        return width * height * 4;
    }


#ifndef _WIN32
    bool UCImageRaster::InitializeImageSubsysterm(const char *programName) {
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
            std::cerr << "UCImage::Load: Failed Failed to load image to memory " << imagePath << " Err:" << err.what() << std::endl;
            errorMessage = std::string("Failed to load image ") + imagePath + " Err:" + err.what();
        }
        return imgDataPtr != nullptr;
    }

#ifndef _WIN32
    std::shared_ptr<UCImageRaster> UCImageRaster::Load(const std::string &imagePath, bool loadOnlyHeader) {
        auto result = std::make_shared<UCImageRaster>(imagePath);
        try {
            vips::VImage vipsImage = result->GetVImage();
            result->width = vipsImage.width();
            result->height = vipsImage.height();
            if (!loadOnlyHeader) {
                result->LoadFileToMemory(imagePath);
            }
        } catch (vips::VError& err) {
            std::cerr << "UCImage::Load: Failed Failed to load image for " << imagePath << " Err:" << err.what() << std::endl;
            result->errorMessage = std::string("Failed to load image ") + imagePath + " Err:" + err.what();
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
        } catch (vips::VError& err) {
            std::cerr << "UCImageVips::LoadFromMemory: Failed to load image from buffer. Err:" << err.what() << std::endl;
            result->errorMessage = std::string("Failed to load image from memory buffer. Err:") + err.what();
        }

        return result;
    }
#else
    std::shared_ptr<UCImageRaster> UCImageRaster::Load(const std::string &imagePath, bool loadOnlyHeader) {
        auto result = std::make_shared<UCImageRaster>(imagePath);
        // TODO: Windows image loading (WIC)
        result->LoadFileToMemory(imagePath);
        result->errorMessage = "Image loading not yet implemented on Windows";
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
        // TODO: Windows image loading (WIC)
        result->errorMessage = "Image loading not yet implemented on Windows";
        return result;
    }
#endif

    std::string UCImageRaster::MakePixmapCacheKey(int w, int h, ImageFitMode fitMode) {
        char key[300];
        snprintf(key, sizeof(key) - 1, "%s?w:%dh:%dc:%d", fileName.c_str(), w, h, static_cast<int>(fitMode));
        return std::string(key);
    }

    std::shared_ptr<UCPixmapCairo> UCImageRaster::GetPixmap(int w, int h, ImageFitMode fitMode) {
        if (!errorMessage.empty() || fileName.empty()) {
            return nullptr;
        }
        if (!w || !h) {
            w = width;
            h = height;
        }
#if HAS_PIXMAPS_CACHE
        std::string key = MakePixmapCacheKey(w, h, fitMode);
        std::shared_ptr<UCPixmapCairo> pm = g_PixmapsCache.GetFromCache(key);
        if (!pm) {
            pm = CreatePixmap(width, height, fitMode);
            if (pm) {
                g_PixmapsCache.AddToCache(key, pm);
            }
        }
#else
        std::shared_ptr<UCPixmapCairo> pm = CreatePixmap(width, height, fitMode);
#endif
        return pm;
    }

#ifndef _WIN32
    std::shared_ptr<UCPixmapCairo> UCImageRaster::CreatePixmap(int w, int h, ImageFitMode fitMode) {
        try {
            auto options = vips::VImage::option();
            switch (fitMode) {
                case ImageFitMode::Fill:
                    options = options->set("height", h)->set("size", VipsSize::VIPS_SIZE_FORCE);
                    break;
                case ImageFitMode::Contain:
                    options = options->set("height", h);
                    break;
                case ImageFitMode::Cover:
                    options = options->set("height", h)->set("crop", VipsInteresting::VIPS_INTERESTING_CENTRE);
                    break;
                case ImageFitMode::ScaleDown:
                    options = options->set("height", h)->set("size", VipsSize::VIPS_SIZE_DOWN);
                    break;
                case ImageFitMode::NoScale:
                    w = width;
                    break;
            }
            if (imgDataPtr) {
                VipsBlob *blob = vips_blob_new(nullptr, imgDataPtr, imgDataSize);
                auto vimg = vips::VImage::thumbnail_buffer(blob, w, options);
                vips_area_unref(VIPS_AREA(blob));
                return CreatePixmapFromVImage(vimg);
            } else {
                return CreatePixmapFromVImage(vips::VImage::thumbnail(fileName.c_str(), w, options));
            }
        } catch (vips::VError& err) {
            std::cerr << "UCImage::CreatePixmap: Failed to make pixmap for " << fileName << " Err:" << err.what() << std::endl;
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
    std::shared_ptr<UCPixmapCairo> UCImageRaster::CreatePixmap(int, int, ImageFitMode) {
        // TODO: Windows image pixmap creation (WIC/Direct2D)
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

#ifndef _WIN32
    std::string UCImageRaster::Save(const std::string &imagePath, const UCImageSave::ImageExportOptions& opts) {
        vips::VImage vImg;
        try {
            vImg = GetVImage();
        } catch (vips::VError& err) {
            std::cerr << "UCImageRaster::Save: Failed to load image. Err:" << err.what() << std::endl;
            return err.what();
        }
        return ExportVImage(vImg, imagePath, opts);
    }

    std::string ExportVImage(vips::VImage vImg, const std::string &imagePath, const UCImageSave::ImageExportOptions& opts) {
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
                    vImg.gifsave(imagePath.c_str(), vips::VImage::option()
                            ->set("bitdepth", bitDepth)
                            ->set("interlace", opts.gif.interlace)
                            ->set("dither", opts.gif.dithering ? 1.0 : 0.0));
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
                    vImg.ppmsave(imagePath.c_str(), vips::VImage::option());
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

                default:
                    std::cerr << "UCImageRaster::Save: Failed save image: " << imagePath
                              << " Err: Unsupported format (" << static_cast<int>(opts.format) << ")" << std::endl;
                    return fmt::format("Unsupported format ({})", static_cast<int>(opts.format));
            }
        } catch (vips::VError& err) {
            std::cerr << "UCImageRaster::Save: Failed save image: " << imagePath << " Err:" << err.what() << std::endl;
            return err.what();
        }
        return "";
    }
#else
    std::string UCImageRaster::Save(const std::string &, const UCImageSave::ImageExportOptions&) {
        // TODO: Windows image export (WIC)
        return "Image export not yet implemented on Windows";
    }
#endif
}
