// include/UltraCanvasImage.h
// Base interface for cross-platform image handling in UltraCanvas
// Version: 1.0.0
// Last Modified: 2025-10-24
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASIMAGE_H
#define ULTRACANVASIMAGE_H
#include <cstdint>
#include <string>
#include <vector>

class IPixmap {
public:
    virtual ~IPixmap() = default;
    virtual bool Init(int w, int h) = 0;
    virtual void Clear() = 0;
    virtual void Flush() = 0;
    virtual bool IsValid() const = 0;
    virtual uint32_t* GetPixelData() = 0;
    virtual void SetPixel(int x, int y, uint32_t pixel) = 0;
    virtual uint32_t GetPixel(int x, int y) const = 0;
    virtual int GetWidth() const = 0;
    virtual int GetHeight() const = 0;
    virtual void MarkDirty() = 0;
};

namespace UltraCanvas {
    enum class UCImageLoadFormat {
        Autodetect,
        PNG,
        JPEG,
        JPEG2000,
        JXL,
        WEBP,
        TIFF,
        PPM,
        GIF,
        HEIF,
        AVIF,
        SVG,
        BMP,
        ICO
    };

    enum UCImageSaveFormat {
        Autodetect,
        PNG,
        JPEG,
        JPEG2000,
        JXL,
        PPM,
        GIF,
        TIFF,
        WEBP,
        HEIF,
        AVIF,
        BMP,
        ICO
    };

    namespace UCImageSave {

        enum ColorDepth {
            Monochrome_1bit,
            Indexed_4bit,
            Indexed_8bit,
            RGB_8bit,
            RGB_16bit,
//            RGB_48bit,
//            RGBA_64bit,
//            HDR_96bit,
//            HDR_128bit
        };

        enum ChromaSubsampling {
            YUV444,
            YUV422,
            YUV420,
            YUV400
        };

        enum TiffCompression {
            NoCompression,
            JPEGCompression,
            DeflateCompression,
            PackBitsCompression,
            LZWCompression,
            ZSTDCompression,
            WEBPCompression,
        };

        struct PngExportOptions {
            int compressionLevel = 6;
            bool interlace = false;
            ColorDepth colorDepth = ColorDepth::RGB_8bit;
//            bool embedICCProfile = true;
        };

        struct JpegExportOptions {
            int quality = 85;
            bool progressive = false;
            bool subsampling = false;
            bool optimizeHuffman = true;
//            bool embedICCProfile = true;
//            bool preserveExif = true;
        };

        struct WebpExportOptions {
            int quality = 80;
            bool lossless = false;
            int effort = 4;
            int targetSize = 0;
//            bool preserveExif = true;
//            bool preserveICCProfile = true;
            int alphaQuality = 100;
        };

        struct AvifExportOptions {
            int quality = 65;
            bool lossless = false;
            int speed = 6;
            ColorDepth colorDepth = ColorDepth::RGB_8bit;
//            bool hdr = false;
//            bool preserveExif = true;
        };

        struct HeifExportOptions {
            int quality = 50;
            bool lossless = false;
            ColorDepth colorDepth = ColorDepth::RGB_8bit;
            bool preserveExif = true;
        };

        struct GifExportOptions {
            ColorDepth colorDepth = ColorDepth::Indexed_8bit;
            bool interlace = false;
//            Color transparentColor = Color(255, 0, 255, 255);
            bool dithering = true;
        };

        struct BmpExportOptions {
//            ColorDepth colorDepth = ColorDepth::RGB_24bit;
            bool rleCompression = false;
        };

        struct TiffExportOptions {
            TiffCompression compression = TiffCompression::LZWCompression;
            ColorDepth colorDepth = ColorDepth::RGB_8bit;
            bool multiPage = false;
//            bool embedICCProfile = true;
//            bool preserveExif = true;
        };

        struct TgaExportOptions {
            ColorDepth colorDepth = ColorDepth::RGB_8bit;
            bool rleCompression = true;
        };

        struct IcoExportOptions {
            std::vector<int> sizes = {16, 32, 48, 256};
//            ColorDepth colorDepth = ColorDepth::RGBA_32bit;
        };

        struct QoiExportOptions {
            bool hasAlpha = true;
            bool linearColorspace = false;
        };

        struct Jpeg2000ExportOptions {
            int quality = 75;
            bool lossless = false;
//            ColorDepth colorDepth = ColorDepth::RGBA_32bit;
//            bool preserveExif = true;
        };

        struct JxlExportOptions {
            int quality = 75;
            bool lossless = false;
            int effort = 7;
//            ColorDepth colorDepth = ColorDepth::RGBA_32bit;
//            bool preserveExif = true;
        };

//        struct ExrExportOptions {
//            enum class Compression { None, RLE, ZIP, PIZ };
//            Compression compression = Compression::ZIP;
//            ColorDepth colorDepth = ColorDepth::RGBA_32bit;
//        };
// ============================================================================
// UNIFIED IMAGE EXPORT OPTIONS
// ============================================================================
        struct ImageExportOptions {
            int targetWidth = 0;
            int targetHeight = 0;
            bool maintainAspectRatio = true;
            bool preserveMetadata = true;
            bool preserveTransparency = true;

            UCImageSaveFormat format = UCImageSaveFormat::PNG;

            PngExportOptions png;
            JpegExportOptions jpeg;
            WebpExportOptions webp;
            AvifExportOptions avif;
            HeifExportOptions heif;
            GifExportOptions gif;
            BmpExportOptions bmp;
            TiffExportOptions tiff;
            TgaExportOptions tga;
            IcoExportOptions ico;
            QoiExportOptions qoi;
            Jpeg2000ExportOptions jpeg2000;
            JxlExportOptions jxl;
//            ExrExportOptions exr;
        };
    };
};

#include "../libspecific/Cairo/ImageCairo.h"
namespace UltraCanvas {
    using UCPixmap = UltraCanvas::UCPixmapCairo;
    using UCImage = UltraCanvas::UCImageRaster;

    typedef std::shared_ptr<UCImage> UCImagePtr;
    typedef std::shared_ptr<UCPixmap> UCPixmapPtr;
}
#endif