// PixelFX/core/PixelFX.cpp
// Comprehensive bitmap processing module for UltraCanvas powered by libvips
// Version: 1.1.0
// Last Modified: 2025-11-30
// Author: UltraCanvas Framework

#include "PixelFX/PixelFX.h"
#include "../libspecific/Cairo/VipsQoiLoader.h"
#include <algorithm>
#include <cmath>

namespace PixelFX {

// ===== STATIC STATE =====
    static bool g_Initialized = false;

// ============================================================================
// IMAGE CLASS IMPLEMENTATION (inherits from vips::VImage)
// ============================================================================

    PFXImage::PFXImage() : vips::VImage() {}

    PFXImage::PFXImage(const vips::VImage& vipsImage) : vips::VImage(vipsImage) {}

    PFXImage::PFXImage(vips::VImage&& vipsImage) : vips::VImage(std::move(vipsImage)) {}

    PFXImage::PFXImage(const PFXImage& other) : vips::VImage(other) {}

    PFXImage::PFXImage(PFXImage&& other) noexcept
            : vips::VImage(std::move(other)) {}

    PFXImage& PFXImage::operator=(const PFXImage& other) {
        if (this != &other) {
            vips::VImage::operator=(other);
        }
        return *this;
    }

    PFXImage& PFXImage::operator=(PFXImage&& other) noexcept {
        if (this != &other) {
            vips::VImage::operator=(std::move(other));
        }
        return *this;
    }

    PFXImage& PFXImage::operator=(const vips::VImage& vipsImage) {
        vips::VImage::operator=(vipsImage);
        return *this;
    }

// UltraCanvas-style accessors (PascalCase wrappers)
    int PFXImage::Width() const { return width(); }
    int PFXImage::Height() const { return height(); }
    int PFXImage::Bands() const { return bands(); }

    BandFormat PFXImage::GetFormat() const {
        return static_cast<BandFormat>(format());
    }

    Interpretation PFXImage::GetInterpretation() const {
        return static_cast<Interpretation>(interpretation());
    }

    double PFXImage::Xres() const { return xres(); }
    double PFXImage::Yres() const { return yres(); }

    size_t PFXImage::GetMemorySize() const {
        return static_cast<size_t>(width()) * height() * bands() * vips_format_sizeof(format());
    }

    bool PFXImage::IsValid() const {
        return get_image() != nullptr;
    }

    std::string PFXImage::GetSourceFilename() const {
        return filename();
    }

// Static factory methods
    PFXImage PFXImage::FromFile(const std::string& filename, AccessMode access) {
        try {
            PFXImage img(vips::VImage::new_from_file(filename.c_str(),
                                                     vips::VImage::option()->set("access", (int)access)));
            return img;
        } catch (const vips::VError& e) {
            throw PixelFXException("Failed to load image: " + std::string(e.what()));
        }
    }

    PFXImage PFXImage::FromBuffer(void* buffer, size_t length, const std::string& formatHint) {
        try {
            return PFXImage::new_from_buffer(buffer, length, formatHint.c_str());
        } catch (const vips::VError& e) {
            throw PixelFXException("Failed to load from buffer: " + std::string(e.what()));
        }
    }

    PFXImage PFXImage::FromMemory(void* data, int width, int height, int bands, BandFormat format) {
        VipsBandFormat vfmt = (VipsBandFormat)format;
        size_t size = static_cast<size_t>(width) * height * bands * vips_format_sizeof(vfmt);
        return PFXImage::new_from_memory(data, size, width, height, bands, vfmt);
    }

    PFXImage PFXImage::CreateBlack(int width, int height, int bands) {
        return PFXImage::black(width, height, vips::VImage::option()->set("bands", bands));
    }

    PFXImage PFXImage::CreateWhite(int width, int height, int bands) {
        return PFXImage::black(width, height, vips::VImage::option()->set("bands", bands)).invert();
    }

    PFXImage PFXImage::CreateSolid(int width, int height, const std::vector<double>& pixel) {
        vips::VImage black = vips::VImage::black(width, height,
                                                 vips::VImage::option()->set("bands", static_cast<int>(pixel.size())));
        return PFXImage(black.linear(std::vector<double>(pixel.size(), 1.0), pixel));
    }

// ============================================================================
// INITIALIZATION
// ============================================================================

//    bool Initialize(const char* programName) {
//        if (g_Initialized) return true;
//        if (VIPS_INIT(programName ? programName : "PixelFX") != 0) return false;
//        vips_foreign_load_qoi_init_types();
//        g_Initialized = true;
//        return true;
//    }
//
//    void Shutdown() {
//        if (g_Initialized) {
//            vips_shutdown();
//            g_Initialized = false;
//        }
//    }

//    bool IsInitialized() { return g_Initialized; }

// ============================================================================
// FILE I/O NAMESPACE IMPLEMENTATION
// ============================================================================
    namespace FileIO {

        PFXImage Load(const std::string& filename, AccessMode access) {
            return PFXImage::FromFile(filename, access);
        }

        PFXImage LoadWithOptions(const std::string& filename, vips::VOption* options) {
            try {
                return PFXImage(vips::VImage::new_from_file(filename.c_str(), options));
            } catch (const vips::VError& e) {
                throw PixelFXException("Failed to load image: " + std::string(e.what()));
            }
        }

        PFXImage LoadFromBuffer(void* buffer, size_t length, const std::string& formatHint) {
            return PFXImage::FromBuffer(buffer, length, formatHint);
        }

        PFXImage LoadFromMemory(std::vector<uint8_t>& data, const std::string& formatHint) {
            return LoadFromBuffer(data.data(), data.size(), formatHint);
        }

        PFXImage LoadPng(const std::string& filename) {
            try { return PFXImage::pngload(filename.c_str()); }
            catch (const vips::VError& e) { throw PixelFXException("Failed to load PNG: " + std::string(e.what())); }
        }

        PFXImage LoadJpeg(const std::string& filename) {
            try { return PFXImage::jpegload(filename.c_str()); }
            catch (const vips::VError& e) { throw PixelFXException("Failed to load JPEG: " + std::string(e.what())); }
        }

        PFXImage LoadWebp(const std::string& filename) {
            try { return PFXImage::webpload(filename.c_str()); }
            catch (const vips::VError& e) { throw PixelFXException("Failed to load WebP: " + std::string(e.what())); }
        }

        PFXImage LoadTiff(const std::string& filename) {
            try { return PFXImage::tiffload(filename.c_str()); }
            catch (const vips::VError& e) { throw PixelFXException("Failed to load TIFF: " + std::string(e.what())); }
        }

        PFXImage LoadGif(const std::string& filename) {
            try { return PFXImage::gifload(filename.c_str()); }
            catch (const vips::VError& e) { throw PixelFXException("Failed to load GIF: " + std::string(e.what())); }
        }

        PFXImage LoadHeif(const std::string& filename) {
            try { return PFXImage::heifload(filename.c_str()); }
            catch (const vips::VError& e) { throw PixelFXException("Failed to load HEIF: " + std::string(e.what())); }
        }

        PFXImage LoadPdf(const std::string& filename, int page, double dpi) {
            try {
                return PFXImage(vips::VImage::pdfload(filename.c_str(),
                                                      vips::VImage::option()->set("page", page)->set("dpi", dpi)));
            } catch (const vips::VError& e) { throw PixelFXException("Failed to load PDF: " + std::string(e.what())); }
        }

        PFXImage LoadSvg(const std::string& filename, double dpi, double scale) {
            try {
                return PFXImage(vips::VImage::svgload(filename.c_str(),
                                                      vips::VImage::option()->set("dpi", dpi)->set("scale", scale)));
            } catch (const vips::VError& e) { throw PixelFXException("Failed to load SVG: " + std::string(e.what())); }
        }

        PFXImage LoadRaw(const std::string& filename, int width, int height, int bands) {
            try { return PFXImage::rawload(filename.c_str(), width, height, bands); }
            catch (const vips::VError& e) { throw PixelFXException("Failed to load raw: " + std::string(e.what())); }
        }

        bool Save(const PFXImage& image, const std::string& filename) {
            try { image.write_to_file(filename.c_str()); return true; }
            catch (const vips::VError& e) { throw PixelFXException("Failed to save image: " + std::string(e.what())); }
        }

        bool SaveWithOptions(const PFXImage& image, const std::string& filename, vips::VOption* options) {
            try { image.write_to_file(filename.c_str(), options); return true; }
            catch (const vips::VError& e) { throw PixelFXException("Failed to save image: " + std::string(e.what())); }
        }

        std::vector<uint8_t> SaveToBuffer(const PFXImage& image, const std::string& format) {
            try {
                void* buf; size_t size;
                image.write_to_buffer(format.c_str(), &buf, &size);
                std::vector<uint8_t> result(static_cast<uint8_t*>(buf), static_cast<uint8_t*>(buf) + size);
                g_free(buf);
                return result;
            } catch (const vips::VError& e) { throw PixelFXException("Failed to save to buffer: " + std::string(e.what())); }
        }

        bool SavePng(const PFXImage& image, const std::string& filename, int compression) {
            try { image.pngsave(filename.c_str(), vips::VImage::option()->set("compression", compression)); return true; }
            catch (const vips::VError& e) { throw PixelFXException("Failed to save PNG: " + std::string(e.what())); }
        }

        bool SaveJpeg(const PFXImage& image, const std::string& filename, int quality) {
            try { image.jpegsave(filename.c_str(), vips::VImage::option()->set("Q", quality)); return true; }
            catch (const vips::VError& e) { throw PixelFXException("Failed to save JPEG: " + std::string(e.what())); }
        }

        bool SaveWebp(const PFXImage& image, const std::string& filename, int quality, bool lossless) {
            try { image.webpsave(filename.c_str(), vips::VImage::option()->set("Q", quality)->set("lossless", lossless)); return true; }
            catch (const vips::VError& e) { throw PixelFXException("Failed to save WebP: " + std::string(e.what())); }
        }

        bool SaveTiff(const PFXImage& image, const std::string& filename, const std::string& compression) {
            try { image.tiffsave(filename.c_str(), vips::VImage::option()->set("compression", compression.c_str())); return true; }
            catch (const vips::VError& e) { throw PixelFXException("Failed to save TIFF: " + std::string(e.what())); }
        }

        bool SaveGif(const PFXImage& image, const std::string& filename) {
            try { image.gifsave(filename.c_str()); return true; }
            catch (const vips::VError& e) { throw PixelFXException("Failed to save GIF: " + std::string(e.what())); }
        }

        bool SaveHeif(const PFXImage& image, const std::string& filename, int quality, bool lossless) {
            try { image.heifsave(filename.c_str(), vips::VImage::option()->set("Q", quality)->set("lossless", lossless)); return true; }
            catch (const vips::VError& e) { throw PixelFXException("Failed to save HEIF: " + std::string(e.what())); }
        }

        bool SaveAvif(const PFXImage& image, const std::string& filename, int quality) {
            try { image.heifsave(filename.c_str(), vips::VImage::option()->set("Q", quality)->set("compression", VIPS_FOREIGN_HEIF_COMPRESSION_AV1)); return true; }
            catch (const vips::VError& e) { throw PixelFXException("Failed to save AVIF: " + std::string(e.what())); }
        }

        bool SavePpm(const PFXImage& image, const std::string& filename) {
            try { image.ppmsave(filename.c_str()); return true; }
            catch (const vips::VError& e) { throw PixelFXException("Failed to save PPM: " + std::string(e.what())); }
        }

        bool SaveFits(const PFXImage& image, const std::string& filename) {
            try { image.fitssave(filename.c_str()); return true; }
            catch (const vips::VError& e) { throw PixelFXException("Failed to save FITS: " + std::string(e.what())); }
        }

        std::vector<std::string> GetSupportedLoadFormats() {
            return {"png", "jpg", "jpeg", "webp", "tiff", "tif", "gif", "heic", "heif", "avif", "pdf", "svg", "raw", "ppm", "fits", "exr"};
        }

        std::vector<std::string> GetSupportedSaveFormats() {
            return {"png", "jpg", "jpeg", "webp", "tiff", "tif", "gif", "heic", "heif", "avif", "ppm", "fits"};
        }

        bool IsFormatSupported(const std::string& format) {
            auto formats = GetSupportedLoadFormats();
            std::string lower = format;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            return std::find(formats.begin(), formats.end(), lower) != formats.end();
        }

        std::string DetectFormat(const std::string& filename) {
            size_t pos = filename.rfind('.');
            if (pos == std::string::npos) return "";
            std::string ext = filename.substr(pos + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            return ext;
        }

    } // namespace FileIO

// ============================================================================
// ARITHMETIC NAMESPACE IMPLEMENTATION
// ============================================================================
    namespace Arithmetic {

        PFXImage Add(const PFXImage& a, const PFXImage& b) { return PFXImage(a.add(b)); }
        PFXImage Add(const PFXImage& image, double value) { return PFXImage(image.linear(1.0, value)); }
        PFXImage Add(const PFXImage& image, const std::vector<double>& values) { return PFXImage(image.linear(std::vector<double>(values.size(), 1.0), values)); }
        PFXImage Subtract(const PFXImage& a, const PFXImage& b) { return PFXImage(a.subtract(b)); }
        PFXImage Subtract(const PFXImage& image, double value) { return PFXImage(image.linear(1.0, -value)); }
        PFXImage Multiply(const PFXImage& a, const PFXImage& b) { return PFXImage(a.multiply(b)); }
        PFXImage Multiply(const PFXImage& image, double value) { return PFXImage(image.linear(value, 0.0)); }
        PFXImage Multiply(const PFXImage& image, const std::vector<double>& values) { return PFXImage(image.linear(values, std::vector<double>(values.size(), 0.0))); }
        PFXImage Divide(const PFXImage& a, const PFXImage& b) { return PFXImage(a.divide(b)); }
        PFXImage Divide(const PFXImage& image, double value) { return PFXImage(image.linear(1.0 / value, 0.0)); }
        PFXImage Remainder(const PFXImage& a, const PFXImage& b) { return PFXImage(a.remainder(b)); }
        PFXImage Remainder(const PFXImage& image, double value) { return PFXImage(image.remainder_const({value})); }

        PFXImage Abs(const PFXImage& image) { return PFXImage(image.abs()); }
        PFXImage Sign(const PFXImage& image) { return PFXImage(image.sign()); }
        PFXImage Floor(const PFXImage& image) { return PFXImage(image.floor()); }
        PFXImage Ceil(const PFXImage& image) { return PFXImage(image.ceil()); }
        PFXImage Rint(const PFXImage& image) { return PFXImage(image.rint()); }
        PFXImage Pow(const PFXImage& image, double exponent) { return PFXImage(image.pow(exponent)); }
        PFXImage Exp(const PFXImage& image) { return PFXImage(image.exp()); }
        PFXImage Exp10(const PFXImage& image) { return PFXImage(image.exp10()); }
        PFXImage Log(const PFXImage& image) { return PFXImage(image.log()); }
        PFXImage Log10(const PFXImage& image) { return PFXImage(image.log10()); }
        PFXImage Sqrt(const PFXImage& image) { return PFXImage(image.pow(0.5)); }

        PFXImage Sin(const PFXImage& image) { return PFXImage(image.sin()); }
        PFXImage Cos(const PFXImage& image) { return PFXImage(image.cos()); }
        PFXImage Tan(const PFXImage& image) { return PFXImage(image.tan()); }
        PFXImage Asin(const PFXImage& image) { return PFXImage(image.asin()); }
        PFXImage Acos(const PFXImage& image) { return PFXImage(image.acos()); }
        PFXImage Atan(const PFXImage& image) { return PFXImage(image.atan()); }
        PFXImage Atan2(const PFXImage& y, const PFXImage& x) { return PFXImage(y.math2(x, VIPS_OPERATION_MATH2_ATAN2)); }
        PFXImage Sinh(const PFXImage& image) { return PFXImage(image.sinh()); }
        PFXImage Cosh(const PFXImage& image) { return PFXImage(image.cosh()); }
        PFXImage Tanh(const PFXImage& image) { return PFXImage(image.tanh()); }
        PFXImage Asinh(const PFXImage& image) { return PFXImage(image.asinh()); }
        PFXImage Acosh(const PFXImage& image) { return PFXImage(image.acosh()); }
        PFXImage Atanh(const PFXImage& image) { return PFXImage(image.atanh()); }

        PFXImage Equal(const PFXImage& a, const PFXImage& b) { return PFXImage(a.relational(b, VIPS_OPERATION_RELATIONAL_EQUAL)); }
        PFXImage NotEqual(const PFXImage& a, const PFXImage& b) { return PFXImage(a.relational(b, VIPS_OPERATION_RELATIONAL_NOTEQ)); }
        PFXImage Less(const PFXImage& a, const PFXImage& b) { return PFXImage(a.relational(b, VIPS_OPERATION_RELATIONAL_LESS)); }
        PFXImage LessEq(const PFXImage& a, const PFXImage& b) { return PFXImage(a.relational(b, VIPS_OPERATION_RELATIONAL_LESSEQ)); }
        PFXImage More(const PFXImage& a, const PFXImage& b) { return PFXImage(a.relational(b, VIPS_OPERATION_RELATIONAL_MORE)); }
        PFXImage MoreEq(const PFXImage& a, const PFXImage& b) { return PFXImage(a.relational(b, VIPS_OPERATION_RELATIONAL_MOREEQ)); }

        PFXImage And(const PFXImage& a, const PFXImage& b) { return PFXImage(a.boolean(b, VIPS_OPERATION_BOOLEAN_AND)); }
        PFXImage Or(const PFXImage& a, const PFXImage& b) { return PFXImage(a.boolean(b, VIPS_OPERATION_BOOLEAN_OR)); }
        PFXImage Eor(const PFXImage& a, const PFXImage& b) { return PFXImage(a.boolean(b, VIPS_OPERATION_BOOLEAN_EOR)); }
        PFXImage Not(const PFXImage& image) { return PFXImage(image.invert()); }
        PFXImage ShiftLeft(const PFXImage& image, int n) { return PFXImage(image.boolean_const(VIPS_OPERATION_BOOLEAN_LSHIFT, {static_cast<double>(n)})); }
        PFXImage ShiftRight(const PFXImage& image, int n) { return PFXImage(image.boolean_const(VIPS_OPERATION_BOOLEAN_RSHIFT, {static_cast<double>(n)})); }

        double Avg(const PFXImage& image) { return image.avg(); }
        double Min(const PFXImage& image) { return image.min(); }
        double Max(const PFXImage& image) { return image.max(); }
        double Deviate(const PFXImage& image) { return image.deviate(); }

        Stats GetStats(const PFXImage& image) {
            vips::VImage stats = image.stats();
            Stats result;
            result.min = stats(0, 0)[0]; result.max = stats(1, 0)[0];
            result.sum = stats(2, 0)[0]; result.sum2 = stats(3, 0)[0];
            result.avg = stats(4, 0)[0]; result.sd = stats(5, 0)[0];
            return result;
        }

        std::vector<double> GetPoint(const PFXImage& image, int x, int y) { return image.getpoint(x, y); }

        PFXImage ComplexForm(const PFXImage& real, const PFXImage& imag) { return PFXImage(real.complexform(imag)); }
        PFXImage Complexget(const PFXImage& image, bool getReal) { return PFXImage(image.complexget(getReal ? VIPS_OPERATION_COMPLEXGET_REAL : VIPS_OPERATION_COMPLEXGET_IMAG)); }
        PFXImage Polar(const PFXImage& image) { return PFXImage(image.complex(VIPS_OPERATION_COMPLEX_POLAR)); }
        PFXImage Rect(const PFXImage& image) { return PFXImage(image.complex(VIPS_OPERATION_COMPLEX_RECT)); }
        PFXImage Conj(const PFXImage& image) { return PFXImage(image.complex(VIPS_OPERATION_COMPLEX_CONJ)); }

        PFXImage Ifthenelse(const PFXImage& condition, const PFXImage& thenImage, const PFXImage& elseImage) {
            return PFXImage(condition.ifthenelse(thenImage, elseImage));
        }

        PFXImage Blend(const PFXImage& base, const PFXImage& overlay, BlendMode mode) {
            std::vector<vips::VImage> images = {overlay, base};
            std::vector<int> modes = {static_cast<int>((VipsBlendMode)mode)};
            return PFXImage::composite(images, modes);
        }

        PFXImage Composite(const std::vector<PFXImage>& images, BlendMode mode) {
            std::vector<vips::VImage> vipsImages;
            std::vector<int> modes;
            for (const auto& img : images) {
                vipsImages.push_back(img);
                modes.push_back(static_cast<int>((VipsBlendMode)mode));
            }
            if (!modes.empty()) modes.pop_back();
            return PFXImage::composite(vipsImages, modes);
        }

    } // namespace Arithmetic

// ============================================================================
// COLOUR NAMESPACE IMPLEMENTATION
// ============================================================================
    namespace Colour {

        PFXImage ToSrgb(const PFXImage& image) { return PFXImage(image.colourspace(VIPS_INTERPRETATION_sRGB)); }
        PFXImage ToLab(const PFXImage& image) { return PFXImage(image.colourspace(VIPS_INTERPRETATION_LAB)); }
        PFXImage ToXyz(const PFXImage& image) { return PFXImage(image.colourspace(VIPS_INTERPRETATION_XYZ)); }
        PFXImage ToLch(const PFXImage& image) { return PFXImage(image.colourspace(VIPS_INTERPRETATION_LCH)); }
        PFXImage ToCmc(const PFXImage& image) { return PFXImage(image.colourspace(VIPS_INTERPRETATION_CMC)); }
        PFXImage ToHsv(const PFXImage& image) { return PFXImage(image.colourspace(VIPS_INTERPRETATION_HSV)); }
        PFXImage ToScrgb(const PFXImage& image) { return PFXImage(image.colourspace(VIPS_INTERPRETATION_scRGB)); }

        PFXImage LabToXyz(const PFXImage& image) { return PFXImage(image.Lab2XYZ()); }
        PFXImage XyzToLab(const PFXImage& image) { return PFXImage(image.XYZ2Lab()); }
        PFXImage LabToLch(const PFXImage& image) { return PFXImage(image.Lab2LCh()); }
        PFXImage LchToLab(const PFXImage& image) { return PFXImage(image.LCh2Lab()); }
        PFXImage SrgbToHsv(const PFXImage& image) { return PFXImage(image.sRGB2HSV()); }
        PFXImage HsvToSrgb(const PFXImage& image) { return PFXImage(image.HSV2sRGB()); }
        PFXImage SrgbToScrgb(const PFXImage& image) { return PFXImage(image.sRGB2scRGB()); }
        PFXImage ScrgbToSrgb(const PFXImage& image) { return PFXImage(image.scRGB2sRGB()); }
        PFXImage ColourSpace(const PFXImage& image, Interpretation space) { return PFXImage(image.colourspace((VipsInterpretation)space)); }

        PFXImage IccImport(const PFXImage& image, const std::string& profilePath) {
            if (profilePath.empty()) return PFXImage(image.icc_import());
            return PFXImage(image.icc_import(vips::VImage::option()->set("input_profile", profilePath.c_str())));
        }

        PFXImage IccExport(const PFXImage& image, const std::string& profilePath) {
            if (profilePath.empty()) return PFXImage(image.icc_export());
            return PFXImage(image.icc_export(vips::VImage::option()->set("output_profile", profilePath.c_str())));
        }

        PFXImage IccTransform(const PFXImage& image, const std::string& outputProfile) { return PFXImage(image.icc_transform(outputProfile.c_str())); }

        PFXImage Brightness(const PFXImage& image, double factor) { return PFXImage(image.linear(factor, 0.0)); }
        PFXImage Contrast(const PFXImage& image, double factor) { double mean = image.avg(); return PFXImage(image.linear(factor, mean * (1.0 - factor))); }

        PFXImage Saturation(const PFXImage& image, double factor) {
            vips::VImage lch = image.colourspace(VIPS_INTERPRETATION_LCH);
            vips::VImage l = lch.extract_band(0);
            vips::VImage c = lch.extract_band(1).linear(factor, 0.0);
            vips::VImage h = lch.extract_band(2);
            vips::VImage result = l.bandjoin(c).bandjoin(h);
            result = result.copy(vips::VImage::option()->set("interpretation", VIPS_INTERPRETATION_LCH));
            return PFXImage(result.colourspace(image.interpretation()));
        }

        PFXImage Gamma(const PFXImage& image, double gamma) { return PFXImage(image.gamma(vips::VImage::option()->set("exponent", 1.0 / gamma))); }
        PFXImage Invert(const PFXImage& image) { return PFXImage(image.invert()); }
        PFXImage Grayscale(const PFXImage& image) { return PFXImage(image.colourspace(VIPS_INTERPRETATION_B_W)); }

        PFXImage Sepia(const PFXImage& image, double intensity) {
            vips::VImage matrix = vips::VImage::new_matrixv(3, 3, 0.393, 0.769, 0.189, 0.349, 0.686, 0.168, 0.272, 0.534, 0.131);
            vips::VImage rgb = image.colourspace(VIPS_INTERPRETATION_sRGB);
            if (rgb.bands() > 3) {
                vips::VImage alpha = rgb.extract_band(3, vips::VImage::option()->set("n", rgb.bands() - 3));
                rgb = rgb.extract_band(0, vips::VImage::option()->set("n", 3));
                return PFXImage(rgb.recomb(matrix).bandjoin(alpha));
            }
            return PFXImage(rgb.recomb(matrix));
        }

        PFXImage HistFind(const PFXImage& image) { return PFXImage(image.hist_find()); }
        PFXImage HistNorm(const PFXImage& image) { return PFXImage(image.hist_norm()); }
        PFXImage HistEqual(const PFXImage& image) { return PFXImage(image.hist_equal()); }
        PFXImage HistMatch(const PFXImage& image, const PFXImage& reference) { return PFXImage(image.hist_match(reference)); }
        PFXImage HistPlot(const PFXImage& histogram) { return PFXImage(histogram.hist_plot()); }
        bool HistIsMonotonic(const PFXImage& histogram) { return histogram.hist_ismonotonic(); }
        double HistEntropy(const PFXImage& histogram) { return histogram.hist_entropy(); }

        PFXImage ExtractBand(const PFXImage& image, int band, int numBands) { return PFXImage(image.extract_band(band, vips::VImage::option()->set("n", numBands))); }

        PFXImage Bandjoin(const std::vector<PFXImage>& images) {
            std::vector<vips::VImage> vipsImages;
            for (const auto& img : images) vipsImages.push_back(img);
            return PFXImage::bandjoin(vipsImages);
        }

        PFXImage Bandjoin(const PFXImage& a, const PFXImage& b) { return PFXImage(a.bandjoin(b)); }
        PFXImage BandjoinConst(const PFXImage& image, const std::vector<double>& constants) { return PFXImage(image.bandjoin_const(constants)); }
        PFXImage Bandmean(const PFXImage& image) { return PFXImage(image.bandmean()); }
        PFXImage Bandfold(const PFXImage& image) { return PFXImage(image.bandfold()); }
        PFXImage Bandunfold(const PFXImage& image) { return PFXImage(image.bandunfold()); }

        PFXImage Premultiply(const PFXImage& image) { return PFXImage(image.premultiply()); }
        PFXImage Unpremultiply(const PFXImage& image) { return PFXImage(image.unpremultiply()); }
        PFXImage Flatten(const PFXImage& image, const std::vector<double>& background) { return PFXImage(image.flatten(vips::VImage::option()->set("background", background))); }
        bool HasAlpha(const PFXImage& image) { return image.has_alpha(); }
        PFXImage AddAlpha(const PFXImage& image) { return HasAlpha(image) ? image : PFXImage(image.bandjoin_const({255.0})); }
        PFXImage RemoveAlpha(const PFXImage& image) { return !HasAlpha(image) ? image : PFXImage(image.extract_band(0, vips::VImage::option()->set("n", image.bands() - 1))); }

    } // namespace Colour

// ============================================================================
// DRAW NAMESPACE IMPLEMENTATION
// ============================================================================
    namespace Draw {

        void Circle(PFXImage& image, int cx, int cy, int radius, const std::vector<double>& ink, bool fill) {
            image.draw_circle(ink, cx, cy, radius, vips::VImage::option()->set("fill", fill));
        }

        void Rect(PFXImage& image, int left, int top, int width, int height, const std::vector<double>& ink, bool fill) {
            image.draw_rect(ink, left, top, width, height, vips::VImage::option()->set("fill", fill));
        }

        void Line(PFXImage& image, int x1, int y1, int x2, int y2, const std::vector<double>& ink) { image.draw_line(ink, x1, y1, x2, y2); }
        void Point(PFXImage& image, int x, int y, const std::vector<double>& ink) { image.draw_rect(ink, x, y, 1, 1); }
        void FloodFill(PFXImage& image, int x, int y, const std::vector<double>& ink) { image.draw_flood(ink, x, y); }
        void FloodFillEqual(PFXImage& image, int x, int y, const std::vector<double>& ink, const std::vector<double>& target) { image.draw_flood(ink, x, y, vips::VImage::option()->set("equal", true)); }
        void Smudge(PFXImage& image, int left, int top, int width, int height) { image.draw_smudge(left, top, width, height); }
        void Insert(PFXImage& image, const PFXImage& sub, int x, int y) { image.draw_image(sub, x, y); }
        void Mask(PFXImage& image, const PFXImage& mask, int x, int y, const std::vector<double>& ink) { image.draw_mask(ink, mask, x, y); }

    } // namespace Draw

// ============================================================================
// CONVOLUTION NAMESPACE IMPLEMENTATION
// ============================================================================
    namespace Convolution {

        PFXImage GaussianBlur(const PFXImage& image, double sigma) { return PFXImage(image.gaussblur(sigma)); }
        PFXImage Blur(const PFXImage& image, int radius) { return GaussianBlur(image, radius / 2.0); }

        PFXImage BoxBlur(const PFXImage& image, int radius) {
            int size = radius * 2 + 1;
            vips::VImage kernel = image.new_from_image(std::vector<double>(size * size, 1.0 / (size * size)));
            return PFXImage(image.conv(kernel));
        }

        PFXImage CannyBlur(const PFXImage& image, double sigma) { return GaussianBlur(image, sigma); }
        PFXImage Sharpen(const PFXImage& image, double sigma, double x1, double m2) { return PFXImage(image.sharpen(vips::VImage::option()->set("sigma", sigma)->set("x1", x1)->set("m2", m2))); }

        PFXImage UnsharpMask(const PFXImage& image, double sigma, double amount) {
            vips::VImage blurred = image.gaussblur(sigma);
            return PFXImage(image.linear(1.0 + amount, 0.0).subtract(blurred.linear(amount, 0.0)));
        }

        PFXImage Sobel(const PFXImage& image) { return PFXImage(image.sobel()); }
        PFXImage Canny(const PFXImage& image, double sigma, double low, double high) { return PFXImage(image.canny(vips::VImage::option()->set("sigma", sigma)->set("precision", VIPS_PRECISION_FLOAT))); }

        PFXImage Laplacian(const PFXImage& image) {
            vips::VImage kernel = vips::VImage::new_matrixv(3, 3, 0.0, -1.0, 0.0, -1.0, 4.0, -1.0, 0.0, -1.0, 0.0);
            return PFXImage(image.conv(kernel));
        }

        PFXImage Prewitt(const PFXImage& image) {
            vips::VImage kernelX = vips::VImage::new_matrixv(3, 3, -1.0, 0.0, 1.0, -1.0, 0.0, 1.0, -1.0, 0.0, 1.0);
            vips::VImage kernelY = vips::VImage::new_matrixv(3, 3, -1.0, -1.0, -1.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
            vips::VImage gx = image.conv(kernelX);
            vips::VImage gy = image.conv(kernelY);
            return PFXImage((gx.pow(2).add(gy.pow(2))).pow(0.5));
        }

        PFXImage Scharr(const PFXImage& image) {
            vips::VImage kernelX = vips::VImage::new_matrixv(3, 3, -3.0, 0.0, 3.0, -10.0, 0.0, 10.0, -3.0, 0.0, 3.0);
            vips::VImage kernelY = vips::VImage::new_matrixv(3, 3, -3.0, -10.0, -3.0, 0.0, 0.0, 0.0, 3.0, 10.0, 3.0);
            vips::VImage gx = image.conv(kernelX);
            vips::VImage gy = image.conv(kernelY);
            return PFXImage((gx.pow(2).add(gy.pow(2))).pow(0.5));
        }

        PFXImage Convolve(const PFXImage& image, const PFXImage& mask) { return PFXImage(image.conv(mask)); }
        PFXImage ConvolveSeparable(const PFXImage& image, const PFXImage& hMask, const PFXImage& vMask) { return PFXImage(image.convsep(hMask)); }
        PFXImage ConvolveInt(const PFXImage& image, const PFXImage& mask, int scale, int offset) { return PFXImage(image.convi(mask)); }

        PFXImage CreateGaussianKernel(double sigma, double minAmpl) { return PFXImage::gaussmat(sigma, minAmpl); }
        PFXImage CreateLoGKernel(double sigma, double minAmpl) { return PFXImage::logmat(sigma, minAmpl); }
        PFXImage CreateSharpenKernel(double sigma, double x1, double m2) { return PFXImage::gaussmat(sigma, 0.2); }
        PFXImage CreateMatrix(int width, int height, std::vector<double>& values) { return PFXImage::new_matrix(width, height, values.data(), values.size()); }

        PFXImage Fastcor(const PFXImage& image, const PFXImage& ref) { return PFXImage(image.fastcor(ref)); }
        PFXImage Spcor(const PFXImage& image, const PFXImage& ref) { return PFXImage(image.spcor(ref)); }

    } // namespace Convolution

// ============================================================================
// CONVERSION NAMESPACE IMPLEMENTATION
// ============================================================================
    namespace Conversion {

        PFXImage Cast(const PFXImage& image, BandFormat format, bool shift) { return PFXImage(image.cast((VipsBandFormat)format, vips::VImage::option()->set("shift", shift))); }
        PFXImage CastUchar(const PFXImage& image) { return Cast(image, BandFormat::FmtUChar); }
        PFXImage CastUshort(const PFXImage& image) { return Cast(image, BandFormat::FmtUShort); }
        PFXImage CastFloat(const PFXImage& image) { return Cast(image, BandFormat::FmtFloat); }
        PFXImage CastDouble(const PFXImage& image) { return Cast(image, BandFormat::FmtDouble); }
        PFXImage Scale(const PFXImage& image, double exp) { return PFXImage(image.scale(vips::VImage::option()->set("exp", exp))); }
        PFXImage Msb(const PFXImage& image, int band) { return band < 0 ? PFXImage(image.msb()) : PFXImage(image.msb(vips::VImage::option()->set("band", band))); }
        PFXImage Byteswap(const PFXImage& image) { return PFXImage(image.byteswap()); }

        PFXImage Arrayjoin(const std::vector<PFXImage>& images, int across) {
            std::vector<vips::VImage> vipsImages;
            for (const auto& img : images) vipsImages.push_back(img);
            return across > 0 ? PFXImage::arrayjoin(vipsImages, vips::VImage::option()->set("across", across)) : PFXImage::arrayjoin(vipsImages);
        }

        PFXImage Grid(const PFXImage& image, int tileHeight, int across, int down) { return PFXImage(image.grid(tileHeight, across, down)); }
        PFXImage Wrap(const PFXImage& image, int x, int y) { return PFXImage(image.wrap(vips::VImage::option()->set("x", x)->set("y", y))); }
        PFXImage Unwrap(const PFXImage& image, int x, int y) { return Wrap(image, -x, -y); }
        PFXImage Copy(const PFXImage& image) { return PFXImage(image.copy()); }
        PFXImage CopyMemory(const PFXImage& image) { return PFXImage(image.copy_memory()); }

        std::vector<uint8_t> ToMemory(const PFXImage& image) {
            size_t size; void* data = image.write_to_memory(&size);
            std::vector<uint8_t> result(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
            g_free(data);
            return result;
        }

        PFXImage FromMemory(void* data, int width, int height, int bands, BandFormat format) {
            return PFXImage::FromMemory(data, width, height, bands, format);
        }

        PFXImage Replicate(const PFXImage& image, int across, int down) { return PFXImage(image.replicate(across, down)); }
        PFXImage Embed(const PFXImage& image, int x, int y, int width, int height, Extend extend) { return PFXImage(image.embed(x, y, width, height, vips::VImage::option()->set("extend", (VipsExtend)extend))); }
        PFXImage Gravity(const PFXImage& image, int direction, int width, int height, Extend extend) { return PFXImage(image.gravity(static_cast<VipsCompassDirection>(direction), width, height, vips::VImage::option()->set("extend", (VipsExtend)extend))); }
        PFXImage Subsample(const PFXImage& image, int xfac, int yfac) { return PFXImage(image.subsample(xfac, yfac == 0 ? xfac : yfac)); }
        PFXImage Zoom(const PFXImage& image, int xfac, int yfac) { return PFXImage(image.zoom(xfac, yfac == 0 ? xfac : yfac)); }
        PFXImage Join(const PFXImage& a, const PFXImage& b, Direction direction, bool expand, int shim, const std::vector<double>& background) { return PFXImage(a.join(b, (VipsDirection)direction, vips::VImage::option()->set("expand", expand)->set("shim", shim)->set("background", background))); }
        PFXImage ExtractArea(const PFXImage& image, int left, int top, int width, int height) { return PFXImage(image.extract_area(left, top, width, height)); }
        PFXImage Crop(const PFXImage& image, int left, int top, int width, int height) { return ExtractArea(image, left, top, width, height); }
        PFXImage SmartCrop(const PFXImage& image, int width, int height) { return PFXImage(image.smartcrop(width, height)); }
        PFXImage Insert(const PFXImage& main, const PFXImage& sub, int x, int y, bool expand) { return PFXImage(main.insert(sub, x, y, vips::VImage::option()->set("expand", expand))); }

    } // namespace Conversion

// ============================================================================
// RESAMPLE NAMESPACE IMPLEMENTATION
// ============================================================================
    namespace Resample {

        PFXImage Resize(const PFXImage& image, double scale, Kernel kernel) { return PFXImage(image.resize(scale, vips::VImage::option()->set("kernel", (VipsKernel)kernel))); }
        PFXImage Resize(const PFXImage& image, double hscale, double vscale, Kernel kernel) { return PFXImage(image.resize(hscale, vips::VImage::option()->set("vscale", vscale)->set("kernel", (VipsKernel)kernel))); }
        PFXImage ResizeTo(const PFXImage& image, int width, int height, Kernel kernel) { return Resize(image, static_cast<double>(width) / image.width(), static_cast<double>(height) / image.height(), kernel); }
        PFXImage Thumbnail(const PFXImage& image, int width, int height) { return height == 0 ? PFXImage(image.thumbnail_image(width)) : PFXImage(image.thumbnail_image(width, vips::VImage::option()->set("height", height))); }
        PFXImage ThumbnailFile(const std::string& filename, int width, int height) { return height == 0 ? PFXImage::thumbnail(filename.c_str(), width) : PFXImage::thumbnail(filename.c_str(), width, vips::VImage::option()->set("height", height)); }
        PFXImage Reduce(const PFXImage& image, double xshrink, double yshrink, Kernel kernel) { return PFXImage(image.reduce(xshrink, yshrink, vips::VImage::option()->set("kernel", (VipsKernel)kernel))); }
        PFXImage Shrink(const PFXImage& image, double xshrink, double yshrink) { return PFXImage(image.shrink(xshrink, yshrink)); }
        PFXImage Shrinkh(const PFXImage& image, int hshrink) { return PFXImage(image.shrinkh(hshrink)); }
        PFXImage Shrinkv(const PFXImage& image, int vshrink) { return PFXImage(image.shrinkv(vshrink)); }
        PFXImage Mapim(const PFXImage& image, const PFXImage& index) { return PFXImage(image.mapim(index)); }

        PFXImage Rotate(const PFXImage& image, double angle) { return PFXImage(image.rotate(angle)); }
        PFXImage Rot90(const PFXImage& image) { return PFXImage(image.rot(VIPS_ANGLE_D90)); }
        PFXImage Rot180(const PFXImage& image) { return PFXImage(image.rot(VIPS_ANGLE_D180)); }
        PFXImage Rot270(const PFXImage& image) { return PFXImage(image.rot(VIPS_ANGLE_D270)); }
        PFXImage Rot(const PFXImage& image, Angle angle) { return PFXImage(image.rot((VipsAngle)angle)); }

        PFXImage FlipHorizontal(const PFXImage& image) { return PFXImage(image.flip(VIPS_DIRECTION_HORIZONTAL)); }
        PFXImage FlipVertical(const PFXImage& image) { return PFXImage(image.flip(VIPS_DIRECTION_VERTICAL)); }
        PFXImage Flip(const PFXImage& image, Direction direction) { return PFXImage(image.flip((VipsDirection)direction)); }

        PFXImage Affine(const PFXImage& image, double a, double b, double c, double d, double odx, double ody, Kernel kernel) {
            return PFXImage(image.affine({a, b, c, d}, vips::VImage::option()->set("odx", odx)->set("ody", ody)->set("interpolate", vips_interpolate_new("bicubic"))));
        }

        PFXImage Similarity(const PFXImage& image, double scale, double angle, double odx, double ody) { return PFXImage(image.similarity(vips::VImage::option()->set("scale", scale)->set("angle", angle)->set("odx", odx)->set("ody", ody))); }
        PFXImage Quadratic(const PFXImage& image, const PFXImage& coeff) { return PFXImage(image.quadratic(coeff)); }
        PFXImage Autorot(const PFXImage& image) { return PFXImage(image.autorot()); }
        double FindAngle(const PFXImage& image) { return 0.0; }
        PFXImage Deskew(const PFXImage& image, double threshold) { double angle = FindAngle(image); return std::abs(angle) > threshold ? Rotate(image, -angle) : image; }

    } // namespace Resample

// ============================================================================
// GENERATE NAMESPACE IMPLEMENTATION
// ============================================================================
    namespace Generate {

        PFXImage Black(int width, int height, int bands) { return PFXImage::black(width, height, vips::VImage::option()->set("bands", bands)); }
        PFXImage White(int width, int height, int bands) { return PFXImage::black(width, height, vips::VImage::option()->set("bands", bands)).invert(); }
        PFXImage Grey(int width, int height, double grey) { return PFXImage::grey(width, height, vips::VImage::option()->set("uchar", true)); }
        PFXImage Solid(int width, int height, const std::vector<double>& pixel) { return PFXImage::CreateSolid(width, height, pixel); }
        PFXImage Zone(int width, int height) { return PFXImage::zone(width, height); }
        PFXImage Sines(int width, int height, bool uchar, double hfreq, double vfreq) { return PFXImage::sines(width, height, vips::VImage::option()->set("uchar", uchar)->set("hfreq", hfreq)->set("vfreq", vfreq)); }
        PFXImage Eye(int width, int height, double factor) { return PFXImage::eye(width, height, vips::VImage::option()->set("factor", factor)); }
        PFXImage Xyz(int width, int height) { return PFXImage::xyz(width, height); }
        PFXImage Gaussnoise(int width, int height, double mean, double sigma) { return PFXImage::gaussnoise(width, height, vips::VImage::option()->set("mean", mean)->set("sigma", sigma)); }
        PFXImage Perlin(int width, int height, int cellSize) { return PFXImage::perlin(width, height, vips::VImage::option()->set("cell_size", cellSize)); }
        PFXImage Worley(int width, int height, int cellSize) { return PFXImage::worley(width, height, vips::VImage::option()->set("cell_size", cellSize)); }
        PFXImage Identity(int bands, bool ushort, int size) { return PFXImage::identity(vips::VImage::option()->set("bands", bands)->set("ushort", ushort)->set("size", size)); }

        PFXImage LinearGradient(int width, int height, Direction direction) {
            vips::VImage xyz = vips::VImage::xyz(width, height);
            if (direction == Direction::Horizontal) return PFXImage(xyz.extract_band(0).linear(255.0 / width, 0.0).cast(VIPS_FORMAT_UCHAR));
            return PFXImage(xyz.extract_band(1).linear(255.0 / height, 0.0).cast(VIPS_FORMAT_UCHAR));
        }

        PFXImage RadialGradient(int width, int height) {
            vips::VImage xyz = vips::VImage::xyz(width, height);
            double cx = width / 2.0, cy = height / 2.0;
            vips::VImage x = xyz.extract_band(0).linear(1.0, -cx);
            vips::VImage y = xyz.extract_band(1).linear(1.0, -cy);
            vips::VImage dist = (x.pow(2).add(y.pow(2))).pow(0.5);
            return PFXImage(dist.linear(255.0 / std::sqrt(cx * cx + cy * cy), 0.0).cast(VIPS_FORMAT_UCHAR));
        }

        PFXImage Text(const std::string& text, const std::string& font, int width, int height, int dpi) {
            auto options = vips::VImage::option()->set("font", font.c_str())->set("dpi", dpi);
            if (width > 0) options->set("width", width);
            if (height > 0) options->set("height", height);
            return PFXImage::text(text.c_str(), options);
        }

        PFXImage Mask(int width, int height, double optical, double reject) { return PFXImage::mask_ideal(width, height, reject); }
        PFXImage MaskIdeal(int width, int height, double frequencyCutoff) { return PFXImage::mask_ideal(width, height, frequencyCutoff); }
        PFXImage MaskButterworth(int width, int height, double order, double frequencyCutoff, double amplitudeCutoff) { return PFXImage::mask_butterworth(width, height, order, frequencyCutoff, amplitudeCutoff); }
        PFXImage MaskGaussian(int width, int height, double frequencyCutoff, double amplitudeCutoff) { return PFXImage::mask_gaussian(width, height, frequencyCutoff, amplitudeCutoff); }
        PFXImage MaskRing(int width, int height, double frequency, double ringwidth) { return PFXImage::mask_ideal_ring(width, height, frequency, ringwidth); }
        PFXImage NewFromImage(const PFXImage& source, const std::vector<double>& pixel) { return PFXImage(source.new_from_image(pixel)); }
        PFXImage CopyBlank(const PFXImage& source) { return PFXImage(source.new_from_image({0.0})); }

    } // namespace Generate

// ============================================================================
// HEADER NAMESPACE IMPLEMENTATION
// ============================================================================
    namespace Header {

        int GetWidth(const PFXImage& image) { return image.Width(); }
        int GetHeight(const PFXImage& image) { return image.Height(); }
        int GetBands(const PFXImage& image) { return image.Bands(); }
        BandFormat GetFormat(const PFXImage& image) { return image.GetFormat(); }
        Interpretation GetInterpretation(const PFXImage& image) { return image.GetInterpretation(); }
        double GetXres(const PFXImage& image) { return image.Xres(); }
        double GetYres(const PFXImage& image) { return image.Yres(); }
        void SetXres(PFXImage& image, double xres) { image.set("xres", xres); }
        void SetYres(PFXImage& image, double yres) { image.set("yres", yres); }
        int GetXoffset(const PFXImage& image) { return image.xoffset(); }
        int GetYoffset(const PFXImage& image) { return image.yoffset(); }
        void SetXoffset(PFXImage& image, int offset) { image.set("xoffset", offset); }
        void SetYoffset(PFXImage& image, int offset) { image.set("yoffset", offset); }
        bool HasField(const PFXImage& image, const std::string& field) { return image.get_typeof(field.c_str()) != 0; }
        std::string GetString(const PFXImage& image, const std::string& field) { return image.get_string(field.c_str()); }
        int GetInt(const PFXImage& image, const std::string& field) { return image.get_int(field.c_str()); }
        double GetDouble(const PFXImage& image, const std::string& field) { return image.get_double(field.c_str()); }
        std::vector<double> GetArrayDouble(const PFXImage& image, const std::string& field) { return image.get_array_double(field.c_str()); }
        void SetString(PFXImage& image, const std::string& field, const std::string& value) { image.set(field.c_str(), value.c_str()); }
        void SetInt(PFXImage& image, const std::string& field, int value) { image.set(field.c_str(), value); }
        void SetDouble(PFXImage& image, const std::string& field, double value) { image.set(field.c_str(), value); }
        void SetArrayDouble(PFXImage& image, const std::string& field, const std::vector<double>& value) { image.set(field.c_str(), value); }
        bool RemoveField(PFXImage& image, const std::string& field) { return image.remove(field.c_str()); }
//        std::vector<std::string> GetFields(const Image& image) { return image.get_fields(); }
        std::string GetExifString(const PFXImage& image, const std::string& tag) { std::string field = "exif-ifd0-" + tag; return HasField(image, field) ? GetString(image, field) : ""; }
        int GetOrientation(const PFXImage& image) { return HasField(image, "orientation") ? GetInt(image, "orientation") : 1; }
        std::string GetFilename(const PFXImage& image) { return image.GetSourceFilename(); }
        size_t GetMemorySize(const PFXImage& image) { return image.GetMemorySize(); }
        std::string GetHistory(const PFXImage& image) { return HasField(image, "vips-image-history") ? GetString(image, "vips-image-history") : ""; }
        void SetHistory(PFXImage& image, const std::string& history) { SetString(image, "vips-image-history", history); }

    } // namespace Header

// ============================================================================
// MORPHOLOGY NAMESPACE IMPLEMENTATION
// ============================================================================
    namespace Morphology {

        PFXImage Erode(const PFXImage& image, const PFXImage& mask) { return PFXImage(image.morph(mask, VIPS_OPERATION_MORPHOLOGY_ERODE)); }
        PFXImage Dilate(const PFXImage& image, const PFXImage& mask) { return PFXImage(image.morph(mask, VIPS_OPERATION_MORPHOLOGY_DILATE)); }
        PFXImage Open(const PFXImage& image, const PFXImage& mask) { return PFXImage(image.morph(mask, VIPS_OPERATION_MORPHOLOGY_ERODE).morph(mask, VIPS_OPERATION_MORPHOLOGY_DILATE)); }
        PFXImage Close(const PFXImage& image, const PFXImage& mask) { return PFXImage(image.morph(mask, VIPS_OPERATION_MORPHOLOGY_DILATE).morph(mask, VIPS_OPERATION_MORPHOLOGY_ERODE)); }
        PFXImage Rank(const PFXImage& image, int width, int height, int index) { return PFXImage(image.rank(width, height, index)); }
        PFXImage Median(const PFXImage& image, int size) { return PFXImage(image.median(size)); }
        double Countlines(const PFXImage& image, Direction direction) { return image.countlines(static_cast<VipsDirection>(direction)); }

        PFXImage CreateDisk(int radius) {
            int size = radius * 2 + 1;
            std::vector<double> data(size * size, 0.0);
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x)
                    if ((x - radius) * (x - radius) + (y - radius) * (y - radius) <= radius * radius)
                        data[y * size + x] = 255.0;
            return PFXImage::new_matrix(size, size, data.data(), data.size());
        }

        PFXImage CreateRectangle(int width, int height) {
            auto vec = std::vector<double>(width * height, 255.0);
            return PFXImage::new_matrix(width, height, vec.data(), vec.size());
        }

        PFXImage CreateCross(int size) {
            std::vector<double> data(size * size, 0.0);
            int center = size / 2;
            for (int i = 0; i < size; ++i) { data[center * size + i] = 255.0; data[i * size + center] = 255.0; }
            return PFXImage::new_matrix(size, size, data.data(), data.size());
        }

        PFXImage CreateRing(int innerRadius, int outerRadius) {
            int size = outerRadius * 2 + 1;
            std::vector<double> data(size * size, 0.0);
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x) {
                    int dist2 = (x - outerRadius) * (x - outerRadius) + (y - outerRadius) * (y - outerRadius);
                    if (dist2 <= outerRadius * outerRadius && dist2 >= innerRadius * innerRadius)
                        data[y * size + x] = 255.0;
                }
            return PFXImage::new_matrix(size, size, data.data(), data.size());
        }

    } // namespace Morphology

// ============================================================================
// FOURIER NAMESPACE IMPLEMENTATION
// ============================================================================
    namespace Fourier {

        PFXImage Fft(const PFXImage& image) { return PFXImage(image.fwfft()); }
        PFXImage Invfft(const PFXImage& image) { return PFXImage(image.invfft()); }
        PFXImage Freqmult(const PFXImage& image, const PFXImage& mask) { return PFXImage(image.freqmult(mask)); }
        PFXImage Spectrum(const PFXImage& image) { return PFXImage(image.spectrum()); }
        PFXImage Phase(const PFXImage& image) { return PFXImage(image.phasecor(image)); }
        PFXImage WrapPhase(const PFXImage& image) { return PFXImage(image.wrap()); }

    } // namespace Fourier

// ============================================================================
// UTILITY FUNCTIONS IMPLEMENTATION
// ============================================================================

    std::string GetVersion() { return std::to_string(vips_version(0)) + "." + std::to_string(vips_version(1)) + "." + std::to_string(vips_version(2)); }
    std::vector<std::string> GetOperations() { return {"add", "subtract", "multiply", "divide", "gaussblur", "sharpen", "resize", "rotate"}; }
    std::string GetLastError() { const char* err = vips_error_buffer(); return err ? std::string(err) : ""; }
    void ClearError() { vips_error_clear(); }
    void SetCacheMax(size_t bytes) { vips_cache_set_max_mem(bytes); }
    void SetCacheMaxFiles(int files) { vips_cache_set_max_files(files); }
    void SetCacheMaxMem(size_t bytes) { vips_cache_set_max_mem(bytes); }
    void SetConcurrency(int threads) { vips_concurrency_set(threads); }
    int GetConcurrency() { return vips_concurrency_get(); }

} // namespace PixelFX