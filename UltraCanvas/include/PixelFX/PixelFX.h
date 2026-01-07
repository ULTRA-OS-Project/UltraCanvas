// PixelFX/include/PixelFX.h
// Comprehensive bitmap processing module for UltraCanvas powered by libvips
// Version: 1.1.0
// Last Modified: 2025-11-30
// Author: UltraCanvas Framework
#pragma once

#ifndef PIXELFX_H
#define PIXELFX_H

// ===== LIBVIPS INCLUDES =====
#include <vips/vips8>

// ===== STANDARD INCLUDES =====
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <functional>
#include <stdexcept>

namespace PixelFX {

// ===== ERROR HANDLING =====
    class PixelFXException : public std::runtime_error {
    public:
        explicit PixelFXException(const std::string& message)
                : std::runtime_error("PixelFX Error: " + message) {}
    };

// ===== ENUMERATIONS =====

    enum class AccessMode {
        Random = VIPS_ACCESS_RANDOM,                 // Full random access (default)
        Sequential = VIPS_ACCESS_SEQUENTIAL,             // Sequential top-to-bottom access
        SequentialUnbuffered = VIPS_ACCESS_SEQUENTIAL_UNBUFFERED   // Sequential without caching
    };

    enum class BandFormat {
        FmtUChar = VIPS_FORMAT_UCHAR,      // Unsigned 8-bit
        FmtChar = VIPS_FORMAT_CHAR,       // Signed 8-bit
        FmtUShort = VIPS_FORMAT_USHORT,     // Unsigned 16-bit
        FmtShort= VIPS_FORMAT_SHORT,      // Signed 16-bit
        FmtUInt = VIPS_FORMAT_UINT,       // Unsigned 32-bit
        FmtInt = VIPS_FORMAT_INT,        // Signed 32-bit
        FmtFloat = VIPS_FORMAT_FLOAT,      // 32-bit float
        FmtDouble = VIPS_FORMAT_DOUBLE,     // 64-bit double
        FmtComplex = VIPS_FORMAT_COMPLEX,    // Complex float
        FmtDComplex = VIPS_FORMAT_DPCOMPLEX    // Complex double
    };

    enum class Interpretation {
        Multiband =  VIPS_INTERPRETATION_MULTIBAND,
        BW =  VIPS_INTERPRETATION_B_W,
        Histogram =  VIPS_INTERPRETATION_HISTOGRAM,
        XYZ =  VIPS_INTERPRETATION_XYZ,
        Lab =  VIPS_INTERPRETATION_LAB,
        CMYK =  VIPS_INTERPRETATION_CMYK,
        LabQ =  VIPS_INTERPRETATION_LABQ,
        RGB =  VIPS_INTERPRETATION_RGB,
        CMC =  VIPS_INTERPRETATION_CMC,
        LCH =  VIPS_INTERPRETATION_LCH,
        Labs =  VIPS_INTERPRETATION_LABS,
        sRGB =  VIPS_INTERPRETATION_sRGB,
        YXY =  VIPS_INTERPRETATION_YXY,
        Fourier =  VIPS_INTERPRETATION_FOURIER,
        RGB16 =  VIPS_INTERPRETATION_RGB16,
        Grey16 =  VIPS_INTERPRETATION_GREY16,
        Matrix =  VIPS_INTERPRETATION_MATRIX,
        scRGB =  VIPS_INTERPRETATION_scRGB,
        HSV =  VIPS_INTERPRETATION_HSV
    };

    enum class BlendMode {
        Clear	=  VIPS_BLEND_MODE_CLEAR,
        Source	=  VIPS_BLEND_MODE_SOURCE,
        Over	=  VIPS_BLEND_MODE_OVER,
        In	=  VIPS_BLEND_MODE_IN,
        Out	=  VIPS_BLEND_MODE_OUT,
        Atop	=  VIPS_BLEND_MODE_ATOP,
        Dest	=  VIPS_BLEND_MODE_DEST,
        DestOver	=  VIPS_BLEND_MODE_DEST_OVER,
        DestIn	=  VIPS_BLEND_MODE_DEST_IN,
        DestOut	=  VIPS_BLEND_MODE_DEST_OUT,
        DestAtop	=  VIPS_BLEND_MODE_DEST_ATOP,
        Xor	=  VIPS_BLEND_MODE_XOR,
        Add	=  VIPS_BLEND_MODE_ADD,
        Saturate	=  VIPS_BLEND_MODE_SATURATE,
        Multiply	=  VIPS_BLEND_MODE_MULTIPLY,
        Screen	=  VIPS_BLEND_MODE_SCREEN,
        Overlay	=  VIPS_BLEND_MODE_OVERLAY,
        Darken	=  VIPS_BLEND_MODE_DARKEN,
        Lighten	=  VIPS_BLEND_MODE_LIGHTEN,
        ColourDodge	=  VIPS_BLEND_MODE_COLOUR_DODGE,
        ColourBurn	=  VIPS_BLEND_MODE_COLOUR_BURN,
        HardLight	=  VIPS_BLEND_MODE_HARD_LIGHT,
        SoftLight	=  VIPS_BLEND_MODE_SOFT_LIGHT,
        Difference	=  VIPS_BLEND_MODE_DIFFERENCE,
        Exclusion	=  VIPS_BLEND_MODE_EXCLUSION
    };

    enum class Kernel {
        Nearest	=  VIPS_KERNEL_NEAREST,
        Linear	=  VIPS_KERNEL_LINEAR,
        Cubic	=  VIPS_KERNEL_CUBIC,
        Mitchell	=  VIPS_KERNEL_MITCHELL,
        Lanczos2	=  VIPS_KERNEL_LANCZOS2,
        Lanczos3	=  VIPS_KERNEL_LANCZOS3
    };

    enum class Direction {
        Horizontal = VIPS_DIRECTION_HORIZONTAL,
        Vertical = VIPS_DIRECTION_VERTICAL
    };

    enum class Angle {
        D0 = VIPS_ANGLE_D0,
        D90 = VIPS_ANGLE_D90,
        D180 = VIPS_ANGLE_D180,
        D270 = VIPS_ANGLE_D270
    };

    enum class Extend {
        Black	=  VIPS_EXTEND_BLACK,
        Copy	=  VIPS_EXTEND_COPY,
        Repeat	=  VIPS_EXTEND_REPEAT,
        Mirror	=  VIPS_EXTEND_MIRROR,
        White	=  VIPS_EXTEND_WHITE,
        Background	=  VIPS_EXTEND_BACKGROUND
    };

    enum class Combine {
        Max,
        Sum,
        Min
    };

// ===== CORE IMAGE CLASS =====
// Inherits from vips::VImage - provides full libvips API access
// plus UltraCanvas-style convenience methods

    class PFXImage : public vips::VImage {
    public:
        // ===== CONSTRUCTORS =====
        PFXImage();

        // Construct from vips::VImage (implicit conversion allowed)
        PFXImage(const vips::VImage& vipsImage);
        PFXImage(vips::VImage&& vipsImage);

        // Copy and move
        PFXImage(const PFXImage& other);
        PFXImage(PFXImage&& other) noexcept;

        ~PFXImage() = default;

        // ===== ASSIGNMENT =====
        PFXImage& operator=(const PFXImage& other);
        PFXImage& operator=(PFXImage&& other) noexcept;
        PFXImage& operator=(const vips::VImage& vipsImage);

        // ===== ULTRACANVAS-STYLE ACCESSORS =====
        // These provide PascalCase alternatives to libvips methods

        int Width() const;
        int Height() const;
        int Bands() const;
        BandFormat GetFormat() const;
        Interpretation GetInterpretation() const;
        double Xres() const;
        double Yres() const;
        size_t GetMemorySize() const;
        bool IsValid() const;

        // Source filename tracking
        std::string GetSourceFilename() const;
        void SetSourceFilename(const std::string& filename);

        // ===== STATIC FACTORY METHODS =====
        // UltraCanvas-style static constructors

        static PFXImage FromFile(const std::string& filename, AccessMode access = AccessMode::Random);
        static PFXImage FromBuffer(void* buffer, size_t length, const std::string& formatHint = "");
        static PFXImage FromMemory(void* data, int width, int height, int bands, BandFormat format);

        static PFXImage CreateBlack(int width, int height, int bands = 3);
        static PFXImage CreateWhite(int width, int height, int bands = 3);
        static PFXImage CreateSolid(int width, int height, const std::vector<double>& pixel);
    };

// ===== INITIALIZATION =====
//    bool Initialize(const char* programName = nullptr);
//    void Shutdown();
//    bool IsInitialized();

// ============================================================================
// FILE I/O NAMESPACE
// Load and save images in various formats
// ============================================================================
    namespace FileIO {

        // ===== LOADING FUNCTIONS =====
        PFXImage Load(const std::string& filename, AccessMode access = AccessMode::Random);
        PFXImage LoadWithOptions(const std::string& filename, vips::VOption* options);
        PFXImage LoadFromBuffer(void* buffer, size_t length, const std::string& formatHint = "");
        PFXImage LoadFromMemory(std::vector<uint8_t>& data, const std::string& formatHint = "");

        // ===== FORMAT-SPECIFIC LOADERS =====
        PFXImage LoadPng(const std::string& filename);
        PFXImage LoadJpeg(const std::string& filename);
        PFXImage LoadWebp(const std::string& filename);
        PFXImage LoadTiff(const std::string& filename);
        PFXImage LoadGif(const std::string& filename);
        PFXImage LoadHeif(const std::string& filename);
        PFXImage LoadPdf(const std::string& filename, int page = 0, double dpi = 72.0);
        PFXImage LoadSvg(const std::string& filename, double dpi = 72.0, double scale = 1.0);
        PFXImage LoadRaw(const std::string& filename, int width, int height, int bands);

        // ===== SAVING FUNCTIONS =====
        bool Save(const PFXImage& image, const std::string& filename);
        bool SaveWithOptions(const PFXImage& image, const std::string& filename, vips::VOption* options);
        std::vector<uint8_t> SaveToBuffer(const PFXImage& image, const std::string& format);

        // ===== FORMAT-SPECIFIC SAVERS =====
        bool SavePng(const PFXImage& image, const std::string& filename, int compression = 6);
        bool SaveJpeg(const PFXImage& image, const std::string& filename, int quality = 85);
        bool SaveWebp(const PFXImage& image, const std::string& filename, int quality = 85, bool lossless = false);
        bool SaveTiff(const PFXImage& image, const std::string& filename, const std::string& compression = "deflate");
        bool SaveGif(const PFXImage& image, const std::string& filename);
        bool SaveHeif(const PFXImage& image, const std::string& filename, int quality = 50, bool lossless = false);
        bool SaveAvif(const PFXImage& image, const std::string& filename, int quality = 50);
        bool SavePpm(const PFXImage& image, const std::string& filename);
        bool SaveFits(const PFXImage& image, const std::string& filename);

        // ===== UTILITY =====
        std::vector<std::string> GetSupportedLoadFormats();
        std::vector<std::string> GetSupportedSaveFormats();
        bool IsFormatSupported(const std::string& format);
        std::string DetectFormat(const std::string& filename);

    } // namespace FileIO

// ============================================================================
// ARITHMETIC NAMESPACE
// ============================================================================
    namespace Arithmetic {

        PFXImage Add(const PFXImage& a, const PFXImage& b);
        PFXImage Add(const PFXImage& image, double value);
        PFXImage Add(const PFXImage& image, const std::vector<double>& values);
        PFXImage Subtract(const PFXImage& a, const PFXImage& b);
        PFXImage Subtract(const PFXImage& image, double value);
        PFXImage Multiply(const PFXImage& a, const PFXImage& b);
        PFXImage Multiply(const PFXImage& image, double value);
        PFXImage Multiply(const PFXImage& image, const std::vector<double>& values);
        PFXImage Divide(const PFXImage& a, const PFXImage& b);
        PFXImage Divide(const PFXImage& image, double value);
        PFXImage Remainder(const PFXImage& a, const PFXImage& b);
        PFXImage Remainder(const PFXImage& image, double value);

        PFXImage Abs(const PFXImage& image);
        PFXImage Sign(const PFXImage& image);
        PFXImage Floor(const PFXImage& image);
        PFXImage Ceil(const PFXImage& image);
        PFXImage Rint(const PFXImage& image);
        PFXImage Pow(const PFXImage& image, double exponent);
        PFXImage Exp(const PFXImage& image);
        PFXImage Exp10(const PFXImage& image);
        PFXImage Log(const PFXImage& image);
        PFXImage Log10(const PFXImage& image);
        PFXImage Sqrt(const PFXImage& image);

        PFXImage Sin(const PFXImage& image);
        PFXImage Cos(const PFXImage& image);
        PFXImage Tan(const PFXImage& image);
        PFXImage Asin(const PFXImage& image);
        PFXImage Acos(const PFXImage& image);
        PFXImage Atan(const PFXImage& image);
        PFXImage Atan2(const PFXImage& y, const PFXImage& x);
        PFXImage Sinh(const PFXImage& image);
        PFXImage Cosh(const PFXImage& image);
        PFXImage Tanh(const PFXImage& image);
        PFXImage Asinh(const PFXImage& image);
        PFXImage Acosh(const PFXImage& image);
        PFXImage Atanh(const PFXImage& image);

        PFXImage Equal(const PFXImage& a, const PFXImage& b);
        PFXImage NotEqual(const PFXImage& a, const PFXImage& b);
        PFXImage Less(const PFXImage& a, const PFXImage& b);
        PFXImage LessEq(const PFXImage& a, const PFXImage& b);
        PFXImage More(const PFXImage& a, const PFXImage& b);
        PFXImage MoreEq(const PFXImage& a, const PFXImage& b);

        PFXImage And(const PFXImage& a, const PFXImage& b);
        PFXImage Or(const PFXImage& a, const PFXImage& b);
        PFXImage Eor(const PFXImage& a, const PFXImage& b);
        PFXImage Not(const PFXImage& image);
        PFXImage ShiftLeft(const PFXImage& image, int n);
        PFXImage ShiftRight(const PFXImage& image, int n);

        double Avg(const PFXImage& image);
        double Min(const PFXImage& image);
        double Max(const PFXImage& image);
        double Deviate(const PFXImage& image);

        struct Stats {
            double min, max, sum, sum2, avg, sd;
        };
        Stats GetStats(const PFXImage& image);
        std::vector<double> GetPoint(const PFXImage& image, int x, int y);

        PFXImage ComplexForm(const PFXImage& real, const PFXImage& imag);
        PFXImage Complexget(const PFXImage& image, bool getReal);
        PFXImage Polar(const PFXImage& image);
        PFXImage Rect(const PFXImage& image);
        PFXImage Conj(const PFXImage& image);

        PFXImage Ifthenelse(const PFXImage& condition, const PFXImage& thenImage, const PFXImage& elseImage);
        PFXImage Blend(const PFXImage& base, const PFXImage& overlay, BlendMode mode);
        PFXImage Composite(const std::vector<PFXImage>& images, BlendMode mode);

    } // namespace Arithmetic

// ============================================================================
// COLOUR NAMESPACE
// ============================================================================
    namespace Colour {

        PFXImage ToSrgb(const PFXImage& image);
        PFXImage ToLab(const PFXImage& image);
        PFXImage ToXyz(const PFXImage& image);
        PFXImage ToLch(const PFXImage& image);
        PFXImage ToCmc(const PFXImage& image);
        PFXImage ToHsv(const PFXImage& image);
        PFXImage ToScrgb(const PFXImage& image);

        PFXImage LabToXyz(const PFXImage& image);
        PFXImage XyzToLab(const PFXImage& image);
        PFXImage LabToLch(const PFXImage& image);
        PFXImage LchToLab(const PFXImage& image);
        PFXImage SrgbToHsv(const PFXImage& image);
        PFXImage HsvToSrgb(const PFXImage& image);
        PFXImage SrgbToScrgb(const PFXImage& image);
        PFXImage ScrgbToSrgb(const PFXImage& image);
        PFXImage ColourSpace(const PFXImage& image, Interpretation space);

        PFXImage IccImport(const PFXImage& image, const std::string& profilePath = "");
        PFXImage IccExport(const PFXImage& image, const std::string& profilePath = "");
        PFXImage IccTransform(const PFXImage& image, const std::string& outputProfile);

        PFXImage Brightness(const PFXImage& image, double factor);
        PFXImage Contrast(const PFXImage& image, double factor);
        PFXImage Saturation(const PFXImage& image, double factor);
        PFXImage Gamma(const PFXImage& image, double gamma);
        PFXImage Invert(const PFXImage& image);
        PFXImage Grayscale(const PFXImage& image);
        PFXImage Sepia(const PFXImage& image, double intensity = 1.0);

        PFXImage HistFind(const PFXImage& image);
        PFXImage HistNorm(const PFXImage& image);
        PFXImage HistEqual(const PFXImage& image);
        PFXImage HistMatch(const PFXImage& image, const PFXImage& reference);
        PFXImage HistPlot(const PFXImage& histogram);
        bool HistIsMonotonic(const PFXImage& histogram);
        double HistEntropy(const PFXImage& histogram);

        PFXImage ExtractBand(const PFXImage& image, int band, int numBands = 1);
        PFXImage Bandjoin(const std::vector<PFXImage>& images);
        PFXImage Bandjoin(const PFXImage& a, const PFXImage& b);
        PFXImage BandjoinConst(const PFXImage& image, const std::vector<double>& constants);
        PFXImage Bandmean(const PFXImage& image);
        PFXImage Bandfold(const PFXImage& image);
        PFXImage Bandunfold(const PFXImage& image);

        PFXImage Premultiply(const PFXImage& image);
        PFXImage Unpremultiply(const PFXImage& image);
        PFXImage Flatten(const PFXImage& image, const std::vector<double>& background = {255, 255, 255});
        bool HasAlpha(const PFXImage& image);
        PFXImage AddAlpha(const PFXImage& image);
        PFXImage RemoveAlpha(const PFXImage& image);

    } // namespace Colour

// ============================================================================
// DRAW NAMESPACE
// ============================================================================
    namespace Draw {

        void Circle(PFXImage& image, int cx, int cy, int radius, const std::vector<double>& ink, bool fill = false);
        void Rect(PFXImage& image, int left, int top, int width, int height, const std::vector<double>& ink, bool fill = false);
        void Line(PFXImage& image, int x1, int y1, int x2, int y2, const std::vector<double>& ink);
        void Point(PFXImage& image, int x, int y, const std::vector<double>& ink);
        void FloodFill(PFXImage& image, int x, int y, const std::vector<double>& ink);
        void FloodFillEqual(PFXImage& image, int x, int y, const std::vector<double>& ink, const std::vector<double>& target);
        void Smudge(PFXImage& image, int left, int top, int width, int height);
        void Insert(PFXImage& image, const PFXImage& sub, int x, int y);
        void Mask(PFXImage& image, const PFXImage& mask, int x, int y, const std::vector<double>& ink);

    } // namespace Draw

// ============================================================================
// CONVOLUTION NAMESPACE
// ============================================================================
    namespace Convolution {

        PFXImage GaussianBlur(const PFXImage& image, double sigma);
        PFXImage Blur(const PFXImage& image, int radius);
        PFXImage BoxBlur(const PFXImage& image, int radius);
        PFXImage CannyBlur(const PFXImage& image, double sigma);
        PFXImage Sharpen(const PFXImage& image, double sigma = 0.5, double x1 = 2.0, double m2 = 1.0);
        PFXImage UnsharpMask(const PFXImage& image, double sigma, double amount);

        PFXImage Sobel(const PFXImage& image);
        PFXImage Canny(const PFXImage& image, double sigma = 1.4, double low = 0.1, double high = 0.3);
        PFXImage Laplacian(const PFXImage& image);
        PFXImage Prewitt(const PFXImage& image);
        PFXImage Scharr(const PFXImage& image);

        PFXImage Convolve(const PFXImage& image, const PFXImage& mask);
        PFXImage ConvolveSeparable(const PFXImage& image, const PFXImage& hMask, const PFXImage& vMask);
        PFXImage ConvolveInt(const PFXImage& image, const PFXImage& mask, int scale, int offset);

        PFXImage CreateGaussianKernel(double sigma, double minAmpl = 0.2);
        PFXImage CreateLoGKernel(double sigma, double minAmpl = 0.2);
        PFXImage CreateSharpenKernel(double sigma, double x1, double m2);
        PFXImage CreateMatrix(int width, int height, const std::vector<double>& values);

        PFXImage Fastcor(const PFXImage& image, const PFXImage& ref);
        PFXImage Spcor(const PFXImage& image, const PFXImage& ref);

    } // namespace Convolution

// ============================================================================
// CONVERSION NAMESPACE
// ============================================================================
    namespace Conversion {

        PFXImage Cast(const PFXImage& image, BandFormat format, bool shift = false);
        PFXImage CastUchar(const PFXImage& image);
        PFXImage CastUshort(const PFXImage& image);
        PFXImage CastFloat(const PFXImage& image);
        PFXImage CastDouble(const PFXImage& image);
        PFXImage Scale(const PFXImage& image, double exp = 0.25);
        PFXImage Msb(const PFXImage& image, int band = -1);
        PFXImage Byteswap(const PFXImage& image);

        PFXImage Arrayjoin(const std::vector<PFXImage>& images, int across = -1);
        PFXImage Grid(const PFXImage& image, int tileHeight, int across, int down);
        PFXImage Wrap(const PFXImage& image, int x = 0, int y = 0);
        PFXImage Unwrap(const PFXImage& image, int x = 0, int y = 0);

        PFXImage Copy(const PFXImage& image);
        PFXImage CopyMemory(const PFXImage& image);
        std::vector<uint8_t> ToMemory(const PFXImage& image);
        PFXImage FromMemory(const void* data, int width, int height, int bands, BandFormat format);

        PFXImage Replicate(const PFXImage& image, int across, int down);
        PFXImage Embed(const PFXImage& image, int x, int y, int width, int height, Extend extend = Extend::Black);
        PFXImage Gravity(const PFXImage& image, int direction, int width, int height, Extend extend = Extend::Black);
        PFXImage Subsample(const PFXImage& image, int xfac, int yfac = 0);
        PFXImage Zoom(const PFXImage& image, int xfac, int yfac = 0);

        PFXImage Join(const PFXImage& a, const PFXImage& b, Direction direction,
                      bool expand = false, int shim = 0, const std::vector<double>& background = {0});
        PFXImage ExtractArea(const PFXImage& image, int left, int top, int width, int height);
        PFXImage Crop(const PFXImage& image, int left, int top, int width, int height);
        PFXImage SmartCrop(const PFXImage& image, int width, int height);
        PFXImage Insert(const PFXImage& main, const PFXImage& sub, int x, int y, bool expand = false);

    } // namespace Conversion

// ============================================================================
// RESAMPLE NAMESPACE
// ============================================================================
    namespace Resample {

        PFXImage Resize(const PFXImage& image, double scale, Kernel kernel = Kernel::Lanczos3);
        PFXImage Resize(const PFXImage& image, double hscale, double vscale, Kernel kernel = Kernel::Lanczos3);
        PFXImage ResizeTo(const PFXImage& image, int width, int height, Kernel kernel = Kernel::Lanczos3);
        PFXImage Thumbnail(const PFXImage& image, int width, int height = 0);
        PFXImage ThumbnailFile(const std::string& filename, int width, int height = 0);
        PFXImage Reduce(const PFXImage& image, double xshrink, double yshrink, Kernel kernel = Kernel::Lanczos3);
        PFXImage Shrink(const PFXImage& image, double xshrink, double yshrink);
        PFXImage Shrinkh(const PFXImage& image, int hshrink);
        PFXImage Shrinkv(const PFXImage& image, int vshrink);
        PFXImage Mapim(const PFXImage& image, const PFXImage& index);

        PFXImage Rotate(const PFXImage& image, double angle);
        PFXImage Rot90(const PFXImage& image);
        PFXImage Rot180(const PFXImage& image);
        PFXImage Rot270(const PFXImage& image);
        PFXImage Rot(const PFXImage& image, Angle angle);

        PFXImage FlipHorizontal(const PFXImage& image);
        PFXImage FlipVertical(const PFXImage& image);
        PFXImage Flip(const PFXImage& image, Direction direction);

        PFXImage Affine(const PFXImage& image, double a, double b, double c, double d,
                        double odx = 0, double ody = 0, Kernel kernel = Kernel::Lanczos3);
        PFXImage Similarity(const PFXImage& image, double scale = 1.0, double angle = 0.0,
                            double odx = 0, double ody = 0);
        PFXImage Quadratic(const PFXImage& image, const PFXImage& coeff);
        PFXImage Autorot(const PFXImage& image);
        double FindAngle(const PFXImage& image);
        PFXImage Deskew(const PFXImage& image, double threshold = 0.0);

    } // namespace Resample

// ============================================================================
// GENERATE NAMESPACE
// ============================================================================
    namespace Generate {

        PFXImage Black(int width, int height, int bands = 3);
        PFXImage White(int width, int height, int bands = 3);
        PFXImage Grey(int width, int height, double grey = 128.0);
        PFXImage Solid(int width, int height, const std::vector<double>& pixel);

        PFXImage Zone(int width, int height);
        PFXImage Sines(int width, int height, bool uchar = false, double hfreq = 0.5, double vfreq = 0.5);
        PFXImage Eye(int width, int height, double factor = 0.5);
        PFXImage Xyz(int width, int height);
        PFXImage Gaussnoise(int width, int height, double mean = 128.0, double sigma = 30.0);
        PFXImage Perlin(int width, int height, int cellSize = 256);
        PFXImage Worley(int width, int height, int cellSize = 256);

        PFXImage Identity(int bands = 1, bool ushort = false, int size = 256);
        PFXImage LinearGradient(int width, int height, Direction direction = Direction::Horizontal);
        PFXImage RadialGradient(int width, int height);

        PFXImage Text(const std::string& text, const std::string& font = "sans 12",
                      int width = 0, int height = 0, int dpi = 72);

        PFXImage Mask(int width, int height, double optical = 1.0, double reject = 0.5);
        PFXImage MaskIdeal(int width, int height, double frequencyCutoff);
        PFXImage MaskButterworth(int width, int height, double order, double frequencyCutoff, double amplitudeCutoff);
        PFXImage MaskGaussian(int width, int height, double frequencyCutoff, double amplitudeCutoff);
        PFXImage MaskRing(int width, int height, double frequency, double ringwidth);

        PFXImage NewFromImage(const PFXImage& source, const std::vector<double>& pixel);
        PFXImage CopyBlank(const PFXImage& source);

    } // namespace Generate

// ============================================================================
// HEADER NAMESPACE
// ============================================================================
    namespace Header {

        int GetWidth(const PFXImage& image);
        int GetHeight(const PFXImage& image);
        int GetBands(const PFXImage& image);
        BandFormat GetFormat(const PFXImage& image);
        Interpretation GetInterpretation(const PFXImage& image);

        double GetXres(const PFXImage& image);
        double GetYres(const PFXImage& image);
        void SetXres(PFXImage& image, double xres);
        void SetYres(PFXImage& image, double yres);

        int GetXoffset(const PFXImage& image);
        int GetYoffset(const PFXImage& image);
        void SetXoffset(PFXImage& image, int offset);
        void SetYoffset(PFXImage& image, int offset);

        bool HasField(const PFXImage& image, const std::string& field);
        std::string GetString(const PFXImage& image, const std::string& field);
        int GetInt(const PFXImage& image, const std::string& field);
        double GetDouble(const PFXImage& image, const std::string& field);
        std::vector<double> GetArrayDouble(const PFXImage& image, const std::string& field);

        void SetString(PFXImage& image, const std::string& field, const std::string& value);
        void SetInt(PFXImage& image, const std::string& field, int value);
        void SetDouble(PFXImage& image, const std::string& field, double value);
        void SetArrayDouble(PFXImage& image, const std::string& field, const std::vector<double>& value);

        bool RemoveField(PFXImage& image, const std::string& field);
        std::vector<std::string> GetFields(const PFXImage& image);

        std::string GetExifString(const PFXImage& image, const std::string& tag);
        int GetOrientation(const PFXImage& image);
        std::string GetFilename(const PFXImage& image);
        size_t GetMemorySize(const PFXImage& image);
        std::string GetHistory(const PFXImage& image);
        void SetHistory(PFXImage& image, const std::string& history);

    } // namespace Header

// ============================================================================
// MORPHOLOGY NAMESPACE
// ============================================================================
    namespace Morphology {

        PFXImage Erode(const PFXImage& image, const PFXImage& mask);
        PFXImage Dilate(const PFXImage& image, const PFXImage& mask);
        PFXImage Open(const PFXImage& image, const PFXImage& mask);
        PFXImage Close(const PFXImage& image, const PFXImage& mask);

        PFXImage Rank(const PFXImage& image, int width, int height, int index);
        PFXImage Median(const PFXImage& image, int size);
        double Countlines(const PFXImage& image, Direction direction);

        PFXImage CreateDisk(int radius);
        PFXImage CreateRectangle(int width, int height);
        PFXImage CreateCross(int size);
        PFXImage CreateRing(int innerRadius, int outerRadius);

    } // namespace Morphology

// ============================================================================
// FOURIER NAMESPACE
// ============================================================================
    namespace Fourier {

        PFXImage Fft(const PFXImage& image);
        PFXImage Invfft(const PFXImage& image);
        PFXImage Freqmult(const PFXImage& image, const PFXImage& mask);
        PFXImage Spectrum(const PFXImage& image);
        PFXImage Phase(const PFXImage& image);
        PFXImage WrapPhase(const PFXImage& image);

    } // namespace Fourier

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

    std::string GetVersion();
    std::vector<std::string> GetOperations();
    std::string GetLastError();
    void ClearError();
    void SetCacheMax(size_t bytes);
    void SetCacheMaxFiles(int files);
    void SetCacheMaxMem(size_t bytes);
    void SetConcurrency(int threads);
    int GetConcurrency();

    struct PXImageFileInfo {
        // File information
        std::string filePath;
        size_t fileSize = 0;
        std::string fileName;
        std::string fileExtension;

        // Basic image properties
        int width = 0;
        int height = 0;
        int channels = 0;
        int bitsPerChannel;
        std::string bandFormat;
        std::string loader;

        // Color information
        std::string colorSpace;
        bool hasAlpha;

        // Resolution
        double xResolution;
        double yResolution;
        double dpiX;
        double dpiY;

    };

    PXImageFileInfo ExtractImageInfo(const std::string &filePath);
} // namespace PixelFX

#endif // PIXELFX_H

