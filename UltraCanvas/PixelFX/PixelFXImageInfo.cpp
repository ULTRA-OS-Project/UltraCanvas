// PixelFXImageInfo.cpp
// Comprehensive image information extraction using libvips
// Version: 1.0.0
// Last Modified: 2025-06-24
// Author: UltraCanvas Framework

#include "PixelFX/PixelFX.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <vips/vips8>

// ============================================================================
// STRUCTURES
// ============================================================================
namespace PixelFX {
// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

    std::string InterpretationToString(VipsInterpretation interp) {
        switch (interp) {
            case VIPS_INTERPRETATION_ERROR:      return "Error";
            case VIPS_INTERPRETATION_MULTIBAND:  return "Multiband";
            case VIPS_INTERPRETATION_B_W:        return "Black and White";
            case VIPS_INTERPRETATION_HISTOGRAM:  return "Histogram";
            case VIPS_INTERPRETATION_XYZ:        return "CIE XYZ";
            case VIPS_INTERPRETATION_LAB:        return "CIE Lab";
            case VIPS_INTERPRETATION_CMYK:       return "CMYK";
            case VIPS_INTERPRETATION_LABQ:       return "LabQ";
            case VIPS_INTERPRETATION_RGB:        return "RGB";
            case VIPS_INTERPRETATION_CMC:        return "CMC";
            case VIPS_INTERPRETATION_LCH:        return "LCH";
            case VIPS_INTERPRETATION_LABS:       return "LabS";
            case VIPS_INTERPRETATION_sRGB:       return "sRGB";
            case VIPS_INTERPRETATION_YXY:        return "Yxy";
            case VIPS_INTERPRETATION_FOURIER:    return "Fourier";
            case VIPS_INTERPRETATION_RGB16:      return "RGB16";
            case VIPS_INTERPRETATION_GREY16:     return "Grey16";
            case VIPS_INTERPRETATION_MATRIX:     return "Matrix";
            case VIPS_INTERPRETATION_scRGB:      return "scRGB";
            case VIPS_INTERPRETATION_HSV:        return "HSV";
            default:                             return "Unknown";
        }
    }

    std::string BandFormatToString(VipsBandFormat format) {
        switch (format) {
            case VIPS_FORMAT_UCHAR:   return "8-bit unsigned";
            case VIPS_FORMAT_CHAR:    return "8-bit signed";
            case VIPS_FORMAT_USHORT:  return "16-bit unsigned";
            case VIPS_FORMAT_SHORT:   return "16-bit signed";
            case VIPS_FORMAT_UINT:    return "32-bit unsigned";
            case VIPS_FORMAT_INT:     return "32-bit signed";
            case VIPS_FORMAT_FLOAT:   return "32-bit float";
            case VIPS_FORMAT_COMPLEX: return "64-bit complex";
            case VIPS_FORMAT_DOUBLE:  return "64-bit double";
            case VIPS_FORMAT_DPCOMPLEX: return "128-bit double complex";
            default:                  return "Unknown";
        }
    }

    int GetBitsPerBand(VipsBandFormat format) {
        switch (format) {
            case VIPS_FORMAT_UCHAR:
            case VIPS_FORMAT_CHAR:    return 8;
            case VIPS_FORMAT_USHORT:
            case VIPS_FORMAT_SHORT:   return 16;
            case VIPS_FORMAT_UINT:
            case VIPS_FORMAT_INT:
            case VIPS_FORMAT_FLOAT:   return 32;
            case VIPS_FORMAT_COMPLEX: return 64;
            case VIPS_FORMAT_DOUBLE:  return 64;
            case VIPS_FORMAT_DPCOMPLEX: return 128;
            default:                  return 0;
        }
    }

    double GetSafeDouble(vips::VImage& image, const char* field, double defaultVal = 0.0) {
        try {
            if (image.get_typeof(field) != 0) {
                return image.get_double(field);
            }
        } catch (...) {}
        return defaultVal;
    }

    int GetSafeInt(vips::VImage& image, const char* field, int defaultVal = 0) {
        try {
            return image.get_int(field);
        } catch (...) {}
        return defaultVal;
    }

    std::string GetSafeString(vips::VImage& image, const char* field) {
        try {
            if (image.get_typeof(field) != 0) {
                return std::string(image.get_string(field));
            }
        } catch (...) {}
        return std::string();
    }

// ============================================================================
// MAIN EXTRACTION FUNCTION
// ============================================================================

    PXImageFileInfo ExtractImageInfo(const std::string& filePath) {
        PXImageFileInfo info = {};
        info.filePath = filePath;

        // Extract filename and extension
        size_t lastSlash = filePath.find_last_of("/\\");
        info.fileName = (lastSlash != std::string::npos) ? filePath.substr(lastSlash + 1) : filePath;
        size_t lastDot = info.fileName.find_last_of('.');
        info.fileExtension = (lastDot != std::string::npos) ? info.fileName.substr(lastDot + 1) : "";

        // Get file size
        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            info.fileSize = file.tellg();
            file.close();
        } else {
            return info;
        }

        try {
            // Load image with libvips
            vips::VImage image = vips::VImage::new_from_file(filePath.c_str());

            // ===== BASIC PROPERTIES =====
            info.width = image.width();
            info.height = image.height();
            info.channels = image.bands();
            info.bitsPerChannel = GetBitsPerBand(image.format());
            info.bandFormat = BandFormatToString(image.format());
            info.loader = GetSafeString(image, "vips-loader");

            // ===== COLOR INFORMATION =====
            info.colorSpace = InterpretationToString(image.interpretation());
            info.hasAlpha = (info.channels == 4 || info.channels == 2);

            // ===== RESOLUTION =====
            info.xResolution = GetSafeDouble(image, "xres", 0.0);
            info.yResolution = GetSafeDouble(image, "yres", 0.0);

            // Convert to DPI (libvips stores as pixels per mm)
            if (info.xResolution > 0) {
                info.dpiX = info.xResolution * 25.4;
                info.dpiY = info.yResolution * 25.4;
            }

        } catch (const vips::VError& e) {
            std::cerr << "libvips error: " << e.what() << std::endl;
            throw PixelFXException(std::string("libvips error: ") + e.what());
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            throw PixelFXException(std::string(e.what()));
        }

        return info;
    }
}
