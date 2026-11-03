// Apps/DemoApp/UltraCanvasBitmapFormatDemo.cpp
// Comprehensive bitmap format specifications and capabilities
// Version: 1.2.0 - JPEG XL, with a JXL-art sample (media/images/jxl_art2.jxl)
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): a title and
//                  description, About / Format Specification / image /
//                  properties as kit cards, the spec and property grids as
//                  wrapping tiles, every text sized to fit
// Version: 1.0.1
// Last Modified: 2026-10-27
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasMediaViewerWindow.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasTextArea.h"
#include "../dialogs/UltraCanvasImageExportDialog.h"
#include "PixelFX/PixelFX.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <fmt/os.h>

namespace UltraCanvas {

// (ShowInMediaViewer, the full-size viewer every sample page opens, is in
// UltraCanvasMediaViewerExamples.cpp: that file is built in every
// configuration, this one only with libvips.)

// ===== FORMAT INFO STRUCTURES =====

    struct TechnicalSpec {
        std::string label;
        std::string value;
    };

    struct CapabilityItem {
        std::string label;
        std::string value;
        bool isSupported;  // true = green (supported), false = orange (feature/limitation)
    };

    // Structured 3x3 specification grid rendered at the bottom of each format page.
    // A value starting with "No" (case-insensitive) is rendered as an "unsupported" cell.
    struct FormatSpecification {
        std::string compression;     // "Lossless", "Lossy", "Lossless or Lossy"
        std::string colorSpace;      // e.g. "RGB, RGBA, Grayscale, Indexed"
        std::string channels;        // e.g. "1-4"
        std::string bitsPerChannel;  // e.g. "1/2/4/8/16-bit"
        std::string hdrSupport;      // "No" / "10-bit" / "10/12-bit"
        std::string transparency;    // "No" / "1-bit" / "8-bit" / "8/16-bit"
        std::string animation;       // "Yes" / "No" (multi-page TIFF counts as No)
        std::string interlacing;     // "Yes" / "No"
        std::string metadata;        // "No" / "EXIF, XMP, ICC" etc.
    };

    struct BitmapFormatInfo {
        // Basic info
        std::string formatName;
        std::string fullName;
        std::string extensions;

        // About section
        std::string aboutTitle;
        std::string aboutDescription;
        Color accentColor;

        // Technical specifications (legacy: still populated for reference)
        std::vector<TechnicalSpec> containerSpecs;
        std::vector<TechnicalSpec> codecSpecs;

        // Capabilities (6 items for 3x2 grid)
        std::vector<CapabilityItem> capabilities;

        // Structured specification (3x3 grid, replaces legacy tech specs at bottom of page)
        FormatSpecification specification;
    };

// ===== JPEG FORMAT =====
    BitmapFormatInfo GetJPEGFormatInfo() {
        BitmapFormatInfo info;

        info.formatName = "JPEG";
        info.fullName = "Joint Photographic Experts Group";
        info.extensions = ".jpg, .jpeg, .jpe, .jfif";
        info.accentColor = Color(220, 53, 69, 255);  // Red

        // About
        info.aboutTitle = "About JPEG";
        info.aboutDescription =
                "JPEG (Joint Photographic Experts Group) is a lossy compression format optimized for "
                "photographs and complex images with smooth color gradients. It achieves high compression "
                "ratios by selectively discarding image data less noticeable to human vision. JPEG is the "
                "most widely used image format on the web and in digital photography, supported by virtually "
                "all devices and software since its introduction in 1992.";

        // Technical Specifications - Container
        info.containerSpecs = {
                {"Standard", "ISO/IEC 10918-1 (ITU-T T.81)"},
                {"Introduced", "1992"},
                {"Extensions", ".jpg, .jpeg, .jpe, .jfif"}
        };

        // Technical Specifications - Codec
        info.codecSpecs = {
                {"Compression", "DCT-based lossy"},
                {"Color Modes", "YCbCr, Grayscale, CMYK"},
                {"Bit Depth", "8-bit per channel"}
        };

        // Capabilities
        info.capabilities = {
                {"Compression", "Lossy DCT", false},
                {"Quality", "Adjustable 1-100", false},
                {"Alpha Channel", "Not Supported", false},
                {"Animation", "Not Supported", false},
                {"Metadata", "EXIF, IPTC, XMP", true},
                {"Progressive", "Supported", true}
        };

        // Format Specification
        info.specification = {
                /* compression    */ "Lossy",
                /* colorSpace     */ "YCbCr, Grayscale, CMYK",
                /* channels       */ "1-4",
                /* bitsPerChannel */ "8-bit",
                /* hdrSupport     */ "No",
                /* transparency   */ "No",
                /* animation      */ "No",
                /* interlacing    */ "Yes (Progressive)",
                /* metadata       */ "EXIF, IPTC, XMP"
        };

        return info;
    }

// ===== PNG FORMAT =====
    BitmapFormatInfo GetPNGFormatInfo() {
        BitmapFormatInfo info;

        info.formatName = "PNG";
        info.fullName = "Portable Network Graphics";
        info.extensions = ".png";
        info.accentColor = Color(13, 110, 253, 255);  // Blue

        // About
        info.aboutTitle = "About PNG";
        info.aboutDescription =
                "PNG (Portable Network Graphics) is a lossless image format that supports transparency "
                "through an alpha channel. Developed as a patent-free replacement for GIF, PNG excels at "
                "storing images with sharp edges, text, and graphics. It uses DEFLATE compression to reduce "
                "file size without any quality loss, making it ideal for screenshots, logos, icons, and "
                "any image requiring exact color reproduction or transparency.";

        // Technical Specifications - Container
        info.containerSpecs = {
                {"Standard", "ISO/IEC 15948:2004"},
                {"Introduced", "1996"},
                {"Extensions", ".png"}
        };

        // Technical Specifications - Codec
        info.codecSpecs = {
                {"Compression", "DEFLATE (zlib)"},
                {"Color Modes", "RGB, RGBA, Grayscale, Indexed"},
                {"Bit Depth", "1/2/4/8/16-bit per channel"}
        };

        // Capabilities
        info.capabilities = {
                {"Compression", "Lossless", true},
                {"Quality", "Perfect fidelity", true},
                {"Alpha Channel", "8/16-bit", true},
                {"Animation", "APNG extension", true},
                {"Metadata", "tEXt, iTXt, zTXt", true},
                {"Interlacing", "Adam7", true}
        };

        // Format Specification
        info.specification = {
                /* compression    */ "Lossless (DEFLATE)",
                /* colorSpace     */ "RGB, RGBA, Grayscale, Indexed",
                /* channels       */ "1-4",
                /* bitsPerChannel */ "1/2/4/8/16-bit",
                /* hdrSupport     */ "No",
                /* transparency   */ "Yes (8/16-bit)",
                /* animation      */ "Yes (APNG)",
                /* interlacing    */ "Yes (Adam7)",
                /* metadata       */ "tEXt, iTXt, zTXt"
        };

        return info;
    }

// ===== GIF FORMAT =====
    BitmapFormatInfo GetGIFFormatInfo() {
        BitmapFormatInfo info;

        info.formatName = "GIF";
        info.fullName = "Graphics Interchange Format";
        info.extensions = ".gif";
        info.accentColor = Color(111, 66, 193, 255);  // Purple

        // About
        info.aboutTitle = "About GIF";
        info.aboutDescription =
                "GIF (Graphics Interchange Format) is one of the oldest image formats, introduced by "
                "CompuServe in 1987. It uses LZW compression and supports up to 256 colors from a 24-bit "
                "palette. GIF's key feature is animation support, allowing multiple frames in a single file. "
                "While limited in color depth, GIF remains popular for short animations, reaction images, "
                "and simple graphics due to universal browser and platform support.";

        // Technical Specifications - Container
        info.containerSpecs = {
                {"Standard", "GIF87a / GIF89a"},
                {"Introduced", "1987 / 1989"},
                {"Extensions", ".gif"}
        };

        // Technical Specifications - Codec
        info.codecSpecs = {
                {"Compression", "LZW (lossless)"},
                {"Color Modes", "Indexed (palette)"},
                {"Bit Depth", "1-8 bit (max 256 colors)"}
        };

        // Capabilities
        info.capabilities = {
                {"Compression", "LZW Lossless", true},
                {"Quality", "256 colors max", false},
                {"Alpha Channel", "1-bit only", false},
                {"Animation", "Multi-frame", true},
                {"Metadata", "Comment blocks", true},
                {"Interlacing", "Supported", true}
        };

        // Format Specification
        info.specification = {
                /* compression    */ "Lossless (LZW)",
                /* colorSpace     */ "Indexed (palette)",
                /* channels       */ "1 (palette index)",
                /* bitsPerChannel */ "1-8 bit (256 colors)",
                /* hdrSupport     */ "No",
                /* transparency   */ "Yes (1-bit)",
                /* animation      */ "Yes (multi-frame)",
                /* interlacing    */ "Yes",
                /* metadata       */ "Comment blocks"
        };

        return info;
    }

// ===== BMP FORMAT =====
    BitmapFormatInfo GetBMPFormatInfo() {
        BitmapFormatInfo info;

        info.formatName = "BMP";
        info.fullName = "Windows Bitmap";
        info.extensions = ".bmp, .dib";
        info.accentColor = Color(0, 128, 128, 255);  // Teal

        // About
        info.aboutTitle = "About BMP";
        info.aboutDescription =
                "BMP (Bitmap) is Microsoft's native uncompressed image format for Windows, introduced "
                "with Windows 1.0 in 1985. It stores pixel data in a straightforward manner, typically "
                "without compression, resulting in large file sizes but extremely fast read/write speeds. "
                "BMP supports various color depths and optional RLE compression. While rarely used for "
                "distribution due to size, BMP remains useful as an intermediate format and for Windows "
                "system graphics.";

        // Technical Specifications - Container
        info.containerSpecs = {
                {"Standard", "Microsoft BMP v5"},
                {"Introduced", "1985"},
                {"Extensions", ".bmp, .dib"}
        };

        // Technical Specifications - Codec
        info.codecSpecs = {
                {"Compression", "None, RLE4, RLE8"},
                {"Color Modes", "RGB, RGBA, Indexed"},
                {"Bit Depth", "1/4/8/16/24/32-bit"}
        };

        // Capabilities
        info.capabilities = {
                {"Compression", "None/RLE", false},
                {"Quality", "Uncompressed", true},
                {"Alpha Channel", "32-bit RGBA", true},
                {"Animation", "Not Supported", false},
                {"Metadata", "Limited", false},
                {"Loading Speed", "Very Fast", true}
        };

        // Format Specification
        info.specification = {
                /* compression    */ "None or RLE",
                /* colorSpace     */ "RGB, RGBA, Indexed",
                /* channels       */ "1-4",
                /* bitsPerChannel */ "1/4/8/16/24/32-bit",
                /* hdrSupport     */ "No",
                /* transparency   */ "Yes (8-bit RGBA)",
                /* animation      */ "No",
                /* interlacing    */ "No",
                /* metadata       */ "Limited"
        };

        return info;
    }

// ===== WebP FORMAT =====
    BitmapFormatInfo GetWebPFormatInfo() {
        BitmapFormatInfo info;

        info.formatName = "WebP";
        info.fullName = "Web Picture Format";
        info.extensions = ".webp";
        info.accentColor = Color(52, 168, 83, 255);  // Google Green

        // About
        info.aboutTitle = "About WebP";
        info.aboutDescription =
                "WebP is a modern image format developed by Google, designed specifically for the web. "
                "It provides superior compression compared to JPEG and PNG, offering both lossy and lossless "
                "modes. WebP supports transparency (alpha channel) and animation, combining the best features "
                "of JPEG, PNG, and GIF in a single format. Files are typically 25-35% smaller than equivalent "
                "JPEG/PNG images, making it ideal for web optimization and faster page loading.";

        // Technical Specifications - Container
        info.containerSpecs = {
                {"Standard", "RIFF container (WebP)"},
                {"Introduced", "2010"},
                {"Extensions", ".webp"}
        };

        // Technical Specifications - Codec
        info.codecSpecs = {
                {"Compression", "VP8 (lossy), VP8L (lossless)"},
                {"Color Modes", "RGB, RGBA, YUV420"},
                {"Bit Depth", "8-bit per channel"}
        };

        // Capabilities
        info.capabilities = {
                {"Compression", "Lossy & Lossless", true},
                {"Quality", "25-35% smaller", true},
                {"Alpha Channel", "8-bit supported", true},
                {"Animation", "Multi-frame", true},
                {"Metadata", "EXIF, XMP, ICC", true},
                {"Browser Support", "All modern", true}
        };

        // Format Specification
        info.specification = {
                /* compression    */ "Lossless or Lossy (VP8 / VP8L)",
                /* colorSpace     */ "RGB, RGBA, YUV420",
                /* channels       */ "1-4",
                /* bitsPerChannel */ "8-bit",
                /* hdrSupport     */ "No",
                /* transparency   */ "Yes (8-bit)",
                /* animation      */ "Yes (multi-frame)",
                /* interlacing    */ "No",
                /* metadata       */ "EXIF, XMP, ICC"
        };

        return info;
    }

// ===== AVIF FORMAT =====
    BitmapFormatInfo GetAVIFFormatInfo() {
        BitmapFormatInfo info;

        info.formatName = "AVIF";
        info.fullName = "AV1 Image File Format";
        info.extensions = ".avif";
        info.accentColor = Color(102, 16, 242, 255);  // Indigo

        // About
        info.aboutTitle = "About AVIF";
        info.aboutDescription =
                "AVIF (AV1 Image File Format) is a next-generation image format based on the AV1 video "
                "codec developed by the Alliance for Open Media. It provides significantly better compression "
                "than JPEG, PNG, and WebP while maintaining excellent image quality. AVIF supports HDR, "
                "wide color gamuts (WCG), and transparency. Files are typically 50% smaller than JPEG at "
                "equivalent quality, making it ideal for bandwidth-conscious web applications.";

        // Technical Specifications - Container
        info.containerSpecs = {
                {"Standard", "ISO/IEC 23000-22 (HEIF)"},
                {"Introduced", "2019"},
                {"Extensions", ".avif"}
        };

        // Technical Specifications - Codec
        info.codecSpecs = {
                {"Compression", "AV1 intra-frame"},
                {"Color Modes", "RGB, YUV, Monochrome"},
                {"Bit Depth", "8/10/12-bit HDR"}
        };

        // Capabilities
        info.capabilities = {
                {"Compression", "AV1-based", true},
                {"Quality", "50% smaller vs JPEG", true},
                {"Alpha Channel", "Supported", true},
                {"Animation", "Image sequences", true},
                {"HDR Support", "10/12-bit", true},
                {"Wide Gamut", "BT.2020, P3", true}
        };

        // Format Specification
        info.specification = {
                /* compression    */ "Lossless or Lossy (AV1)",
                /* colorSpace     */ "RGB, YUV, Monochrome",
                /* channels       */ "1-4",
                /* bitsPerChannel */ "8/10/12-bit",
                /* hdrSupport     */ "Yes (10/12-bit)",
                /* transparency   */ "Yes (8-bit+)",
                /* animation      */ "Yes (image sequences)",
                /* interlacing    */ "No",
                /* metadata       */ "EXIF, XMP, ICC"
        };

        return info;
    }

// ===== TIFF FORMAT =====
    BitmapFormatInfo GetTIFFFormatInfo() {
        BitmapFormatInfo info;

        info.formatName = "TIFF";
        info.fullName = "Tagged Image File Format";
        info.extensions = ".tiff, .tif";
        info.accentColor = Color(184, 134, 11, 255);  // Dark Goldenrod

        // About
        info.aboutTitle = "About TIFF";
        info.aboutDescription =
                "TIFF (Tagged Image File Format) is a flexible, adaptable format widely used in professional "
                "photography, publishing, and archival applications. Developed by Aldus (now Adobe), TIFF "
                "supports multiple compression schemes including none, LZW, ZIP, and JPEG. It can store "
                "multiple images (pages) in a single file and preserves extensive metadata. While file sizes "
                "are typically large, TIFF maintains maximum image quality and is the preferred format for "
                "print production and long-term archival.";

        // Technical Specifications - Container
        info.containerSpecs = {
                {"Standard", "TIFF 6.0 / BigTIFF"},
                {"Introduced", "1986"},
                {"Extensions", ".tiff, .tif"}
        };

        // Technical Specifications - Codec
        info.codecSpecs = {
                {"Compression", "None, LZW, ZIP, JPEG"},
                {"Color Modes", "RGB, CMYK, LAB, YCbCr"},
                {"Bit Depth", "1/8/16/32-bit, floating point"}
        };

        // Capabilities
        info.capabilities = {
                {"Compression", "Multiple options", true},
                {"Quality", "Archival grade", true},
                {"Alpha Channel", "Multiple channels", true},
                {"Multi-page", "Supported", true},
                {"Metadata", "Extensive EXIF/IPTC", true},
                {"Color Profiles", "Full ICC support", true}
        };

        // Format Specification
        info.specification = {
                /* compression    */ "None, LZW, ZIP, JPEG, Deflate",
                /* colorSpace     */ "RGB, CMYK, LAB, YCbCr, Grayscale",
                /* channels       */ "1-N (extensible)",
                /* bitsPerChannel */ "1/8/16/32-bit, float",
                /* hdrSupport     */ "Yes (16-bit / float)",
                /* transparency   */ "Yes (multi-channel)",
                /* animation      */ "No (multi-page)",
                /* interlacing    */ "No",
                /* metadata       */ "EXIF, IPTC, XMP"
        };

        return info;
    }

// ===== QOI FORMAT =====
    BitmapFormatInfo GetQOIFormatInfo() {
        BitmapFormatInfo info;

        info.formatName = "QOI";
        info.fullName = "Quite OK Image Format";
        info.extensions = ".qoi";
        info.accentColor = Color(255, 107, 107, 255);  // Coral

        // About
        info.aboutTitle = "About QOI";
        info.aboutDescription =
                "QOI (Quite OK Image Format) is a fast, lossless image format designed for simplicity and "
                "speed. Created by Dominic Szablewski in 2021, QOI achieves compression comparable to PNG "
                "but with 20-50x faster encoding and 3-4x faster decoding. The entire specification fits "
                "on a single page, making it extremely easy to implement. QOI is ideal for game development, "
                "real-time applications, and scenarios where encoding/decoding speed is critical.";

        // Technical Specifications - Container
        info.containerSpecs = {
                {"Standard", "QOI Specification 1.0"},
                {"Introduced", "2021"},
                {"Extensions", ".qoi"}
        };

        // Technical Specifications - Codec
        info.codecSpecs = {
                {"Compression", "Run-length + indexing"},
                {"Color Modes", "RGB, RGBA"},
                {"Bit Depth", "8-bit per channel"}
        };

        // Capabilities
        info.capabilities = {
                {"Compression", "Lossless", true},
                {"Quality", "Perfect fidelity", true},
                {"Alpha Channel", "8-bit RGBA", true},
                {"Encode Speed", "20-50x vs PNG", true},
                {"Decode Speed", "3-4x vs PNG", true},
                {"Simplicity", "~300 lines code", true}
        };

        // Format Specification
        info.specification = {
                /* compression    */ "Lossless (RLE + indexing)",
                /* colorSpace     */ "RGB, RGBA",
                /* channels       */ "3-4",
                /* bitsPerChannel */ "8-bit",
                /* hdrSupport     */ "No",
                /* transparency   */ "Yes (8-bit)",
                /* animation      */ "No",
                /* interlacing    */ "No",
                /* metadata       */ "No"
        };

        return info;
    }

// ===== RAW FORMAT =====
    BitmapFormatInfo GetRAWFormatInfo() {
        BitmapFormatInfo info;

        info.formatName = "RAW";
        info.fullName = "Camera Raw Image";
        info.extensions = ".raw, .cr2, .cr3, .nef, .arw, .dng, .orf, .rw2";
        info.accentColor = Color(64, 64, 64, 255);  // Dark Gray

        // About
        info.aboutTitle = "About RAW";
        info.aboutDescription =
                "RAW files contain minimally processed data directly from a camera's image sensor. Unlike "
                "JPEG, RAW preserves all captured information, giving photographers maximum flexibility in "
                "post-processing. Each camera manufacturer has proprietary RAW formats (CR2/CR3 for Canon, "
                "NEF for Nikon, ARW for Sony, etc.). Adobe's DNG provides a standardized RAW format. RAW "
                "files are larger but offer superior dynamic range, white balance adjustment, and non-destructive "
                "editing capabilities essential for professional photography.";

        // Technical Specifications - Container
        info.containerSpecs = {
                {"Standard", "Vendor-specific / DNG"},
                {"Common Formats", "CR2, CR3, NEF, ARW, DNG"},
                {"Extensions", ".raw, .cr2, .nef, .arw, .dng"}
        };

        // Technical Specifications - Codec
        info.codecSpecs = {
                {"Compression", "Lossless or uncompressed"},
                {"Color Modes", "Bayer CFA, X-Trans, Foveon"},
                {"Bit Depth", "12/14/16-bit per channel"}
        };

        // Capabilities
        info.capabilities = {
                {"Compression", "Minimal/None", false},
                {"Quality", "Sensor-level data", true},
                {"Dynamic Range", "Maximum preserved", true},
                {"White Balance", "Adjustable post", true},
                {"Metadata", "Full EXIF/MakerNote", true},
                {"Editing", "Non-destructive", true}
        };

        // Format Specification
        info.specification = {
                /* compression    */ "Minimal or Lossless",
                /* colorSpace     */ "Bayer CFA, X-Trans, Foveon",
                /* channels       */ "1 (CFA pattern)",
                /* bitsPerChannel */ "12/14/16-bit",
                /* hdrSupport     */ "Yes (sensor native)",
                /* transparency   */ "No",
                /* animation      */ "No",
                /* interlacing    */ "No",
                /* metadata       */ "EXIF + MakerNote"
        };

        return info;
    }

// ===== PSP FORMAT =====
    BitmapFormatInfo GetPSPFormatInfo() {
        BitmapFormatInfo info;

        info.formatName = "PSP";
        info.fullName = "Paint Shop Pro Image";
        info.extensions = ".psp, .pspimage";
        info.accentColor = Color(0, 102, 204, 255);  // Corel Blue

        // About
        info.aboutTitle = "About PSP";
        info.aboutDescription =
                "PSP (Paint Shop Pro Image) is the native format for Corel Paint Shop Pro, a popular image "
                "editing application. The format preserves all editing information including layers, masks, "
                "adjustment layers, vector objects, and selection data. PSP files support non-destructive "
                "editing workflows, allowing users to return and modify individual elements. While primarily "
                "used within Paint Shop Pro, the format demonstrates advanced features similar to PSD for "
                "Photoshop users.";

        // Technical Specifications - Container
        info.containerSpecs = {
                {"Standard", "Corel proprietary"},
                {"Introduced", "1990s (JASC era)"},
                {"Extensions", ".psp, .pspimage"}
        };

        // Technical Specifications - Codec
        info.codecSpecs = {
                {"Compression", "RLE, LZ77"},
                {"Color Modes", "RGB, CMYK, Grayscale, Indexed"},
                {"Bit Depth", "1/8/16-bit per channel"}
        };

        // Capabilities
        info.capabilities = {
                {"Compression", "Lossless RLE/LZ77", true},
                {"Layers", "Full support", true},
                {"Alpha/Masks", "Multiple channels", true},
                {"Vector Objects", "Supported", true},
                {"Adjustment Layers", "Non-destructive", true},
                {"Selections", "Saved in file", true}
        };

        // Format Specification
        info.specification = {
                /* compression    */ "Lossless (RLE / LZ77)",
                /* colorSpace     */ "RGB, CMYK, Grayscale, Indexed",
                /* channels       */ "Multi (layers + masks)",
                /* bitsPerChannel */ "1/8/16-bit",
                /* hdrSupport     */ "No",
                /* transparency   */ "Yes (multi-channel masks)",
                /* animation      */ "No",
                /* interlacing    */ "No",
                /* metadata       */ "Layers, vector objects"
        };

        return info;
    }

// ===== HEIF FORMAT (for reference) =====
    BitmapFormatInfo GetHEIFFormatInfo() {
        BitmapFormatInfo info;

        info.formatName = "HEIF";
        info.fullName = "High Efficiency Image Format";
        info.extensions = ".heif, .heic, .heics, .avci";
        info.accentColor = Color(249, 115, 22, 255);  // Orange

        // About
        info.aboutTitle = "About HEIF/HEIC";
        info.aboutDescription =
                "HEIF/HEIC (High Efficiency Image Format) is an image container format based on HEVC (H.265) "
                "video compression. It provides superior compression efficiency compared to JPEG while "
                "maintaining high image quality. HEIF supports features like image sequences, transparency, "
                "depth maps, and HDR. This format is used by default on Apple devices since iOS 11 and "
                "macOS High Sierra.";

        // Technical Specifications - Container
        info.containerSpecs = {
                {"Standard", "ISO/IEC 23008-12 (HEIF)"},
                {"Introduced", "2015"},
                {"Extensions", ".heif, .heic, .heics, .avci"}
        };

        // Technical Specifications - Codec
        info.codecSpecs = {
                {"Compression", "HEVC (H.265)"},
                {"Color Modes", "RGB, YCbCr"},
                {"Bit Depth", "8/10-bit HDR"}
        };

        // Capabilities
        info.capabilities = {
                {"Compression", "HEVC-based", false},
                {"Quality", "High Efficiency", false},
                {"Alpha Channel", "Supported", true},
                {"Image Sequences", "Supported", true},
                {"Depth Maps", "Supported", true},
                {"HDR", "10-bit support", true}
        };

        // Format Specification
        info.specification = {
                /* compression    */ "Lossy (HEVC) or Lossless",
                /* colorSpace     */ "RGB, YCbCr",
                /* channels       */ "1-4",
                /* bitsPerChannel */ "8/10-bit",
                /* hdrSupport     */ "Yes (10-bit)",
                /* transparency   */ "Yes (8-bit)",
                /* animation      */ "Yes (image sequences)",
                /* interlacing    */ "No",
                /* metadata       */ "EXIF, XMP"
        };

        return info;
    }

// ===== JPEG XL FORMAT =====
    BitmapFormatInfo GetJXLFormatInfo() {
        BitmapFormatInfo info;

        info.formatName = "JPEG XL";
        info.fullName = "JPEG XL Image Coding System";
        info.extensions = ".jxl";
        info.accentColor = Color(13, 148, 136, 255);  // Teal

        // About
        info.aboutTitle = "About JPEG XL";
        info.aboutDescription =
                "JPEG XL (ISO/IEC 18181) is the JPEG committee's successor to JPEG. It has a lossy mode "
                "(VarDCT) and a lossless one (Modular), both progressive, and it can recompress an existing "
                "JPEG losslessly about 20% smaller and give back the original file bit for bit. It covers "
                "HDR and wide gamut up to 32-bit float per channel, alpha, animation and any number of "
                "extra channels. The sample is JXL art: its Modular predictor tree draws the whole "
                "2160 x 3840 picture from an 82-byte file. Reading and writing it needs a libvips built "
                "with libjxl.";

        // Technical Specifications - Container
        info.containerSpecs = {
                {"Standard", "ISO/IEC 18181"},
                {"Introduced", "2021"},
                {"Extensions", ".jxl"}
        };

        // Technical Specifications - Codec
        info.codecSpecs = {
                {"Compression", "VarDCT (lossy), Modular (lossless)"},
                {"Color Modes", "RGB, Grayscale, CMYK"},
                {"Bit Depth", "Up to 32-bit float"}
        };

        // Capabilities
        info.capabilities = {
                {"Compression", "Lossy and lossless", true},
                {"JPEG Recompression", "Lossless, ~20% smaller", true},
                {"Alpha Channel", "Supported", true},
                {"Animation", "Supported", true},
                {"HDR Support", "Up to 32-bit float", true},
                {"Progressive", "Supported", true}
        };

        // Format Specification
        info.specification = {
                /* compression    */ "Lossless or Lossy (VarDCT)",
                /* colorSpace     */ "RGB, Grayscale, CMYK",
                /* channels       */ "1-4 + extra channels",
                /* bitsPerChannel */ "8/16-bit, 32-bit float",
                /* hdrSupport     */ "Yes (PQ, HLG, float)",
                /* transparency   */ "Yes (any depth)",
                /* animation      */ "Yes",
                /* interlacing    */ "Yes (progressive)",
                /* metadata       */ "EXIF, XMP, ICC"
        };

        return info;
    }

// ===== FORMAT INFO REGISTRY =====
    BitmapFormatInfo GetBitmapFormatInfo(const std::string& format) {
        static std::unordered_map<std::string, BitmapFormatInfo(*)()> formatRegistry = {
                {"JPEG", GetJPEGFormatInfo},
                {"JPG", GetJPEGFormatInfo},
                {"PNG", GetPNGFormatInfo},
                {"GIF", GetGIFFormatInfo},
                {"BMP", GetBMPFormatInfo},
                {"WEBP", GetWebPFormatInfo},
                {"WebP", GetWebPFormatInfo},
                {"AVIF", GetAVIFFormatInfo},
                {"TIFF", GetTIFFFormatInfo},
                {"TIF", GetTIFFFormatInfo},
                {"QOI", GetQOIFormatInfo},
                {"RAW", GetRAWFormatInfo},
                {"CR2", GetRAWFormatInfo},
                {"NEF", GetRAWFormatInfo},
                {"ARW", GetRAWFormatInfo},
                {"DNG", GetRAWFormatInfo},
                {"PSP", GetPSPFormatInfo},
                {"PSPIMAGE", GetPSPFormatInfo},
                {"HEIF", GetHEIFFormatInfo},
                {"HEIC", GetHEIFFormatInfo},
                {"JXL", GetJXLFormatInfo}
        };

        // Convert to uppercase for lookup
        std::string upperFormat = format;
        std::transform(upperFormat.begin(), upperFormat.end(), upperFormat.begin(), ::toupper);

        auto it = formatRegistry.find(upperFormat);
        if (it != formatRegistry.end()) {
            return it->second();
        }

        // Return empty info if format not found
        return BitmapFormatInfo{};
    }

// ===== HELPER: CREATE FORMAT DEMO PAGE =====
// This helper function creates a demo page for any bitmap format using the format info
    std::shared_ptr<UltraCanvasContainer> UltraCanvasDemoApplication::CreateBitmapFormatDemoPage(
            const std::string& format,
            const std::string& sampleImagePath,
            int baseId
    ) {
        BitmapFormatInfo info = GetBitmapFormatInfo(format);
        if (info.formatName.empty()) {
            return nullptr;
        }

        // Top to bottom: About, Format Specification, then the image preview
        // beside the image's properties (they wrap under each other when the
        // window is narrow). The cards are made here in that order and filled
        // in below.
        auto page = DemoKit::CreatePage(info.formatName + "DemoPage", info.formatName + " Images",
                                        info.fullName + " (" + info.extensions + ")");
        auto aboutCard = DemoKit::AddCard(page, "AboutCard");
        auto specCard = DemoKit::AddCard(page, "SpecCard", "Format Specification");
        auto bottomRow = DemoKit::AddRow(page, "ImageRow");

        int id = baseId + 1;

        // ===== IMAGE PREVIEW CARD (bottom row, left) =====
        // As wide as the image frame needs; the properties take the rest.
        auto imageCard = DemoKit::AddCard(bottomRow, "ImageCard", "Demo " + info.formatName + " Image", "", 270);
        imageCard->layoutItem.SetFlexGrow(0);

        auto imageFrame = std::make_shared<UltraCanvasContainer>("ImageFrame", 0, 0, 230, 170);
        imageFrame->SetBackgroundColor(Color(241, 245, 249, 255));
        imageFrame->SetBorders(1, Color(200, 200, 200, 255));
        // The frame keeps its size; the image sits in it at (4, 4). A notice
        // that replaces the image is centred in it by the frame's column.
        imageFrame->layout.SetFlexColumn()
                          .SetFlexJustifyContent(CSSLayout::JustifyContent::Center)
                          .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        DemoKit::AddFixed(imageCard, imageFrame);

        auto image = std::make_shared<UltraCanvasImageElement>("Image", 4, 4, 222, 162);
        // A build whose libvips lacks this format's codec (the WebAssembly
        // sysroot reads PNG, JPEG, TIFF, WebP and GIF; BMP needs ImageMagick,
        // HEIC and AVIF libheif) cannot show the sample. The page still
        // builds - the About and Format Specification cards do not depend
        // on it - and says so where the picture and its properties would be.
        const bool readable = image->LoadFromFile(sampleImagePath);
        // libvips ends its messages with a newline; on one line here.
        std::string unreadableReason = readable ? std::string() : image->GetLastError();
        for (char& c : unreadableReason) if (c == '\n' || c == '\r') c = ' ';
        while (!unreadableReason.empty() && unreadableReason.back() == ' ') unreadableReason.pop_back();
        const std::string unreadableNotice =
            "This build cannot read " + info.formatName + " files.";
        image->SetFitMode(ImageFitMode::Contain);
        // Hand, not LookingGlass: a click opens the image in the viewer, and a
        // magnifier reads as "zoom" rather than "click to open".
        image->SetClickable(true);   // also sets the hand cursor
        image->onClick = [sampleImagePath]() {
            ShowInMediaViewer(sampleImagePath);
        };
        imageFrame->AddChild(image);
        if (!readable) {
            image->SetVisible(false);
            auto notice = DemoKit::AddText(imageFrame, "Unreadable", unreadableNotice);
            notice->SetFontSize(11);
            notice->SetFontWeight(FontWeight::Bold);
            notice->SetTextColor(Color(234, 88, 12, 255));
            notice->SetAlignment(TextAlignment::Center, VerticalAlignment::Top);
            notice->SetPadding(0, 12, 0, 12);
        }

        // Extract filename
        std::string filename = sampleImagePath;
        size_t lastSlash = sampleImagePath.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            filename = sampleImagePath.substr(lastSlash + 1);
        }

        auto filenameLabel = DemoKit::AddCaption(imageCard, "Filename", filename);
        filenameLabel->SetTextColor(Color(100, 116, 139, 255));
        filenameLabel->SetAlignment(TextAlignment::Center, VerticalAlignment::Top);

        // The buttons are as wide as their icon and text.
        auto buttonRow = DemoKit::AddRow(imageCard, "ImageButtons", 10);
        auto viewBtn = std::make_shared<UltraCanvasButton>("ViewBtn", 0, 0, 0, 32);
        viewBtn->SetText("Full Size");
        SetFlatButtonIcon(*viewBtn, "menu/zoom-in.svg");
        viewBtn->SetFontSize(10);
        viewBtn->SetColors(info.accentColor, info.accentColor);
        viewBtn->SetTextColors(Color(255, 255, 255, 255));
        viewBtn->SetCornerRadius(6);
        viewBtn->SetMouseCursor(UCMouseCursor::Hand);
        viewBtn->onClick = [sampleImagePath]() {
            ShowInMediaViewer(sampleImagePath);
        };
        viewBtn->SetVisible(readable);
        DemoKit::Add(buttonRow, viewBtn);

        auto exportBtn = std::make_shared<UltraCanvasButton>("ExportBtn", 0, 0, 0, 32);
        exportBtn->SetText("Export");
        SetFlatButtonIcon(*exportBtn, "menu/extract.svg");
        exportBtn->SetFontSize(10);
        exportBtn->SetColors(Color(241, 245, 249, 255), Color(226, 232, 240, 255));
        exportBtn->SetTextColors(Color(71, 85, 105, 255));
        exportBtn->SetCornerRadius(6);
        exportBtn->onClick = [sampleImagePath]() {
            // A throw out of a click handler ends the program (and the
            // WebAssembly build has no console to say why), so a sample the
            // viewer could show but libvips cannot reopen is reported instead.
            try {
                auto vimg = vips::VImage::new_from_file(sampleImagePath.c_str());
                auto win = CreateImageExportDialog(vimg);
                win->Create();
                win->Show();
            } catch (const std::exception& e) {
                debugOutput << "Bitmap page: export of " << sampleImagePath
                            << " failed: " << e.what() << std::endl;
            }
        };
        exportBtn->SetVisible(readable);
        DemoKit::Add(buttonRow, exportBtn);

        // ===== IMAGE PROPERTIES (bottom row, right; populated from actual image) =====
        auto propertiesCard = DemoKit::AddCard(bottomRow, "PropertiesCard", "Image Properties");

        // ExtractImageInfo throws when libvips cannot open the file; before
        // the try below, that throw left the page without a single card.
        PixelFX::PXImageFileInfo imgInfo;
        bool haveInfo = readable;
        if (haveInfo) {
            try {
                imgInfo = PixelFX::ExtractImageInfo(sampleImagePath);
            } catch (const std::exception& e) {
                debugOutput << "Bitmap page: no properties for " << sampleImagePath
                            << ": " << e.what() << std::endl;
                haveInfo = false;
            }
        }
        if (!haveInfo) {
            auto noProps = DemoKit::AddText(propertiesCard, "NoProps",
                    unreadableNotice + (unreadableReason.empty() ? std::string()
                                                                  : " The reader said: " + unreadableReason));
            noProps->SetFontSize(11);
            noProps->SetTextColor(Color(71, 85, 105, 255));
        }

        // Property placeholders (would be populated from actual image metadata)
        std::vector<std::pair<std::string, std::string>> defaultProps = haveInfo ?
            std::vector<std::pair<std::string, std::string>>{
                {"FILE SIZE", FormatFileSize(imgInfo.fileSize)},
                {"RESOLUTION", fmt::format("{}x{}", imgInfo.width, imgInfo.height)},
                {"CHANNELS", fmt::format("{}", imgInfo.channels)},
                {"COLOR SPACE", imgInfo.colorSpace},
                {"LOADER", imgInfo.loader},
                {"BITS PER CHANNEL", fmt::format("{}", imgInfo.bitsPerChannel)},
                {"ALPHA CHANNEL", imgInfo.hasAlpha ? "Yes" : "No"},
                {"DPI", fmt::format("{}", round(imgInfo.dpiX))}
            } : std::vector<std::pair<std::string, std::string>>{};

        // Two tiles a line while they fit; a tile is as tall as its wrapped
        // label and value.
        auto propGrid = DemoKit::AddRow(propertiesCard, "PropGrid", 8);
        for (size_t i = 0; i < defaultProps.size(); ++i) {
            auto propContainer = DemoKit::CreateColumn("Prop" + std::to_string(i), 4);
            propContainer->SetBackgroundColor(Color(248, 250, 252, 255));
            propContainer->SetBorders(1, Color(226, 232, 240, 255));
            propContainer->SetPadding(8, 16, 8, 16);
            DemoKit::Add(propGrid, propContainer, 1);
            propContainer->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(220));

            auto propLabel = DemoKit::AddCaption(propContainer, "PropLabel" + std::to_string(i),
                                                 defaultProps[i].first);
            propLabel->SetFontSize(9);
            propLabel->SetTextColor(Color(100, 116, 139, 255));

            auto propValue = DemoKit::AddText(propContainer, "PropValue" + std::to_string(i),
                                              defaultProps[i].second);
            propValue->SetFontSize(13);
            propValue->SetFontWeight(FontWeight::Bold);
            propValue->SetTextColor(Color(30, 41, 59, 255));
        }

        // ===== ABOUT SECTION =====
        // The icon beside the title, the description under both.
        auto aboutHeader = DemoKit::CreateRow("AboutHeader", 12, false);
        aboutHeader->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
        DemoKit::Add(aboutCard, aboutHeader);

        // A flat document icon on a tile of the format's colour (an image,
        // not a "📄" glyph the bundled UI font does not have).
        auto aboutIcon = std::make_shared<UltraCanvasImageElement>("AboutIcon", 0, 0, 36, 36);
        aboutIcon->LoadFromFile(DemoIconPath("menu/file-icons.svg"));
        aboutIcon->SetFitMode(ImageFitMode::Contain);
        aboutIcon->SetBackgroundColor(Color(
                std::min(255, info.accentColor.r + 100),
                std::min(255, info.accentColor.g + 100),
                std::min(255, info.accentColor.b + 100),
                255
        ));
        aboutIcon->SetPadding(8);
        DemoKit::AddFixed(aboutHeader, aboutIcon);
        aboutIcon->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);

        auto aboutTitle = DemoKit::CreateHeading("AboutTitle", info.aboutTitle);
        aboutTitle->SetFontSize(14);
        aboutTitle->SetTextColor(Color(30, 41, 59, 255));
        DemoKit::Add(aboutHeader, aboutTitle, 1);

        auto aboutDesc = DemoKit::AddText(aboutCard, "AboutDesc", info.aboutDescription);
        aboutDesc->SetFontSize(11);
        aboutDesc->SetTextColor(Color(71, 85, 105, 255));

        // ===== FORMAT SPECIFICATION =====
        // 9 properties in a grid of tiles, three a line while they fit (label
        // and value side by side, the value wrapping under the label when the
        // tile is too narrow for both). Cells whose value starts with "No" get a muted orange tint to flag missing
        // features; the rest use a neutral slate background.
        struct SpecCell { std::string label; std::string value; };
        const std::vector<SpecCell> cells = {
                {"COMPRESSION",      info.specification.compression},
                {"COLOR SPACE",      info.specification.colorSpace},
                {"CHANNELS",         info.specification.channels},
                {"BITS PER CHANNEL", info.specification.bitsPerChannel},
                {"HDR SUPPORT",      info.specification.hdrSupport},
                {"TRANSPARENCY",     info.specification.transparency},
                {"ANIMATION",        info.specification.animation},
                {"INTERLACING",      info.specification.interlacing},
                {"METADATA",         info.specification.metadata},
        };

        auto specGrid = DemoKit::AddRow(specCard, "SpecGrid", 8);

        auto isUnsupportedValue = [](const std::string& v) {
            if (v.empty()) return true;
            // Match "No" or "No (..." at the start, case-insensitive
            return (v.size() >= 2 && (v[0] == 'N' || v[0] == 'n') && (v[1] == 'O' || v[1] == 'o'))
                   && (v.size() == 2 || v[2] == ' ' || v[2] == '(');
        };

        for (size_t i = 0; i < cells.size(); ++i) {
            bool unsupported = isUnsupportedValue(cells[i].value);

            auto cell = DemoKit::CreateRow("Spec" + std::to_string(i), 6);
            cell->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
            cell->SetPadding(7, 10, 7, 10);
            if (unsupported) {
                cell->SetBackgroundColor(Color(255, 247, 237, 255));
                cell->SetBorders(1, Color(254, 215, 170, 255));
            } else {
                cell->SetBackgroundColor(Color(248, 250, 252, 255));
                cell->SetBorders(1, Color(226, 232, 240, 255));
            }
            DemoKit::Add(specGrid, cell, 1);
            cell->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(250));

            // Single-line layout: label on the left (muted, normal weight),
            // value immediately to its right (bold, dark or orange when unsupported).
            auto lbl = DemoKit::CreateCaption("SpecLbl" + std::to_string(i), cells[i].label);
            lbl->SetFontSize(9);
            lbl->SetTextColor(Color(100, 116, 139, 255));
            DemoKit::Add(cell, lbl);

            auto val = DemoKit::CreateText("SpecVal" + std::to_string(i), cells[i].value);
            val->SetFontSize(11);
            val->SetFontWeight(FontWeight::Bold);
            val->SetTextColor(unsupported ? Color(234, 88, 12, 255) : Color(30, 41, 59, 255));
            DemoKit::Add(cell, val, 1);
        }

        return page;
    }

} // namespace UltraCanvas
