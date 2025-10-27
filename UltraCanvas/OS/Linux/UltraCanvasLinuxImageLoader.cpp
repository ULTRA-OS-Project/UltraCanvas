// OS/Linux/UltraCanvasLinuxImageLoader.cpp
// Linux-specific image loader implementation using Cairo
// Version: 2.0.0
// Last Modified: 2025-10-24
// Author: UltraCanvas Framework

#include "UltraCanvasImageLoader.h"
#include "UltraCanvasUtils.h"
#include <cairo/cairo.h>
#include <fstream>
#include <iostream>
#include <png.h>
#include <jpeglib.h>
#include <jerror.h>
#include <algorithm>
#include <setjmp.h>
#include <cstring>

namespace UltraCanvas {
    UltraCanvasLinuxImageLoader UltraCanvasLinuxImageLoader::instance;

    cairo_surface_t* LoadPngImage(const std::string& filePath, int& width, int& height) {
        FILE* file = fopen(filePath.c_str(), "rb");
        if (!file) {
            std::cerr << "UltraCanvasLinuxImageLoader: Cannot open PNG file: " << filePath << std::endl;
            return nullptr;
        }

        // Check PNG signature
        uint8_t header[8];
        if (fread(header, 1, 8, file) != 8 || png_sig_cmp(header, 0, 8)) {
            std::cerr << "UltraCanvasLinuxImageLoader: Invalid PNG file: " << filePath << std::endl;
            fclose(file);
            return nullptr;
        }

        // Create PNG structures
        png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
        if (!png_ptr) {
            std::cerr << "UltraCanvasLinuxImageLoader: Failed to create PNG read struct" << std::endl;
            fclose(file);
            return nullptr;
        }

        png_infop info_ptr = png_create_info_struct(png_ptr);
        if (!info_ptr) {
            std::cerr << "UltraCanvasLinuxImageLoader: Failed to create PNG info struct" << std::endl;
            png_destroy_read_struct(&png_ptr, nullptr, nullptr);
            fclose(file);
            return nullptr;
        }

        // Error handling
        if (setjmp(png_jmpbuf(png_ptr))) {
            std::cerr << "UltraCanvasLinuxImageLoader: Error reading PNG file: " << filePath << std::endl;
            png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
            fclose(file);
            return nullptr;
        }

        png_init_io(png_ptr, file);
        png_set_sig_bytes(png_ptr, 8);
        png_read_info(png_ptr, info_ptr);

        // Get image info
        width = png_get_image_width(png_ptr, info_ptr);
        height = png_get_image_height(png_ptr, info_ptr);
        png_byte color_type = png_get_color_type(png_ptr, info_ptr);
        png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);

        // Configure PNG reading for RGBA format
        if (bit_depth == 16) png_set_strip_16(png_ptr);
        if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png_ptr);
        if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png_ptr);
        if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png_ptr);
        if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE)
            png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);
        if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
            png_set_gray_to_rgb(png_ptr);

        png_read_update_info(png_ptr, info_ptr);

        // Allocate memory for image data
        size_t row_bytes = png_get_rowbytes(png_ptr, info_ptr);
        uint8_t* image_data = new uint8_t[height * row_bytes];
        png_bytep* row_pointers = new png_bytep[height];

        for (int y = 0; y < height; y++) {
            row_pointers[y] = image_data + y * row_bytes;
        }

        // Read the image
        png_read_image(png_ptr, row_pointers);
        png_read_end(png_ptr, nullptr);

        // Create Cairo surface
        cairo_surface_t* surface = cairo_image_surface_create_for_data(
                image_data, CAIRO_FORMAT_ARGB32, width, height, cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width));

        if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
            std::cerr << "UltraCanvasLinuxImageLoader: Failed to create Cairo surface for PNG" << std::endl;
            delete[] image_data;
            delete[] row_pointers;
            png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
            fclose(file);
            return nullptr;
        }

        // Convert RGBA to BGRA for Cairo
        uint32_t* pixels = reinterpret_cast<uint32_t*>(image_data);
        for (int i = 0; i < width * height; i++) {
            uint32_t pixel = pixels[i];
            uint8_t r = (pixel >> 0) & 0xFF;
            uint8_t g = (pixel >> 8) & 0xFF;
            uint8_t b = (pixel >> 16) & 0xFF;
            uint8_t a = (pixel >> 24) & 0xFF;

            // Apply premultiplied alpha
            if (a == 0) {
                // Fully transparent pixel
                pixels[i] = 0;
            } else if (a == 255) {
                // Fully opaque pixel - no premultiplication needed
                pixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
            } else {
                // Partially transparent - premultiply RGB by alpha
                uint8_t premul_r = (r * a) / 255;
                uint8_t premul_g = (g * a) / 255;
                uint8_t premul_b = (b * a) / 255;
                pixels[i] = (a << 24) | (premul_r << 16) | (premul_g << 8) | premul_b;
            }
            //pixels[i] = (a << 24) | (r << 16) | (g << 8) | b; // ARGB for Cairo
        }

        cairo_surface_mark_dirty(surface);

        // Cleanup
        delete[] row_pointers;
        png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
        fclose(file);

        return surface;
    }

// ===== JPEG LOADING IMPLEMENTATION =====
    struct JpegErrorManager {
        struct jpeg_error_mgr pub;
        jmp_buf setjmp_buffer;
    };

    void JpegErrorExit(j_common_ptr cinfo) {
        JpegErrorManager* err = reinterpret_cast<JpegErrorManager*>(cinfo->err);
        longjmp(err->setjmp_buffer, 1);
    }

    cairo_surface_t* LoadJpegImage(const std::string& filePath, int& width, int& height) {
        FILE* file = fopen(filePath.c_str(), "rb");
        if (!file) {
            std::cerr << "UltraCanvasLinuxImageLoader: Cannot open JPEG file: " << filePath << std::endl;
            return nullptr;
        }

        struct jpeg_decompress_struct cinfo;
        JpegErrorManager jerr;

        cinfo.err = jpeg_std_error(&jerr.pub);
        jerr.pub.error_exit = JpegErrorExit;

        if (setjmp(jerr.setjmp_buffer)) {
            std::cerr << "UltraCanvasLinuxImageLoader: Error reading JPEG file: " << filePath << std::endl;
            jpeg_destroy_decompress(&cinfo);
            fclose(file);
            return nullptr;
        }

        jpeg_create_decompress(&cinfo);
        jpeg_stdio_src(&cinfo, file);
        jpeg_read_header(&cinfo, TRUE);

        // Force RGB output
        cinfo.out_color_space = JCS_RGB;
        jpeg_start_decompress(&cinfo);

        width = cinfo.output_width;
        height = cinfo.output_height;
        int channels = cinfo.output_components;

        if (channels != 3) {
            std::cerr << "UltraCanvasLinuxImageLoader: Unsupported JPEG color format" << std::endl;
            jpeg_destroy_decompress(&cinfo);
            fclose(file);
            return nullptr;
        }

        // Allocate memory for RGB data
        size_t row_stride = width * channels;
        uint8_t* rgb_data = new uint8_t[height * row_stride];
        uint8_t* row_pointer = rgb_data;

        // Read scanlines
        while (cinfo.output_scanline < cinfo.output_height) {
            jpeg_read_scanlines(&cinfo, &row_pointer, 1);
            row_pointer += row_stride;
        }

        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        fclose(file);

        // Convert RGB to ARGB for Cairo
        uint8_t* argb_data = new uint8_t[width * height * 4];
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int rgb_index = (y * width + x) * 3;
                int argb_index = (y * width + x) * 4;

                uint8_t r = rgb_data[rgb_index + 0];
                uint8_t g = rgb_data[rgb_index + 1];
                uint8_t b = rgb_data[rgb_index + 2];

                // Cairo ARGB format
                argb_data[argb_index + 0] = b;  // B
                argb_data[argb_index + 1] = g;  // G
                argb_data[argb_index + 2] = r;  // R
                argb_data[argb_index + 3] = 255; // A
            }
        }

        delete[] rgb_data;

        // Create Cairo surface
        cairo_surface_t* surface = cairo_image_surface_create_for_data(
                argb_data, CAIRO_FORMAT_ARGB32, width, height, cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width));

        if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
            std::cerr << "UltraCanvasLinuxImageLoader: Failed to create Cairo surface for JPEG" << std::endl;
            delete[] argb_data;
            return nullptr;
        }

        cairo_surface_mark_dirty(surface);
        return surface;
    }

// ===== PLATFORM-SPECIFIC LOADING IMPLEMENTATIONS =====

    std::shared_ptr<UCImage> UltraCanvasLinuxImageLoader::LoadFromFile(const std::string& imagePath) {
        auto result = std::make_shared<UCImage>();

        // Use existing UltraCanvasLinuxImageLoader
        if (imagePath.empty()) {
            result->errorMessage = "Empty image path";
            return result;
        }

        // Load based on format
        cairo_surface_t* surface = nullptr;
        int width = 0, height = 0;
        std::cout << "UltraCanvasLinuxImageLoader::LoadImage file=" << imagePath << std::endl;
        std::string ext = GetFileExtension(imagePath);
        if (ext == "png") {
            std::cout << "UltraCanvasLinuxImageLoader::LoadImage png=" << imagePath << std::endl;
            surface = LoadPngImage(imagePath, width, height);
        } else if (ext == "jpg" || ext == "jpeg") {
            std::cout << "IsJpegFile" << std::endl;
            surface = LoadJpegImage(imagePath, width, height);
        } else {
            result->errorMessage = "Unsupported image format";
            return result;
        }

        if (surface) {
            result->SetSurface(surface, true);
        } else {
            result->errorMessage = "Failed to load image";
        }

        return result;
    }

//    std::unique_ptr<UCImage> UltraCanvasImageLoader::LoadFromMemory(
//            const uint8_t* data, size_t size, UCImageFormat format) {
//
//        // Use existing UltraCanvasLinuxImageLoader
//        UCLinuxImage result = UltraCanvasLinuxImageLoader::LoadImageFromMemory(data, size);
//
//        if (!result.success) {
//            SetLastError("Failed to load image from memory: " + result.errorMessage);
//            return nullptr;
//        }
//
//        // Create platform-specific implementation with Cairo surface
//        auto impl = CreateImageImplFromCairoSurface(result.surface, false);
//
//        // Set additional metadata
//        auto imageImpl = impl.get();
//        if (imageImpl) {
//            imageImpl->SetFormat(format);
//            imageImpl->SetFromMemory(true);
//
//            // Store the original data
//            std::vector<uint8_t> memData(data, data + size);
//            imageImpl->SetPixelData(memData.data(), memData.size());
//        }
//
//        // Create UCImage with platform implementation
//        return std::unique_ptr<UCImage>(new UCImage(std::move(impl)));
//    }
//
//// ===== LINUX-SPECIFIC SAVE IMPLEMENTATIONS =====
//
//    bool SaveImageToFilePlatform(const UCImage& image, const std::string& filePath,
//                                 UCImageFormat format) {
//
//        // Get native Cairo surface
//        cairo_surface_t* surface = static_cast<cairo_surface_t*>(image.GetNativeHandle());
//        if (!surface) {
//            return false;
//        }
//
//        // Auto-detect format from extension if not specified
//        if (format == UCImageFormat::Auto) {
//            format = UltraCanvasImageLoader::DetectFormatFromPath(filePath);
//        }
//
//        cairo_status_t status = CAIRO_STATUS_SUCCESS;
//
//        switch (format) {
//            case UCImageFormat::PNG:
//                status = cairo_surface_write_to_png(surface, filePath.c_str());
//                break;
//
//            case UCImageFormat::JPEG:
//            case UCImageFormat::BMP:
//                // These would require additional libraries or conversion
//                // For now, save as PNG with different extension
//                status = cairo_surface_write_to_png(surface, filePath.c_str());
//                break;
//
//            default:
//                return false;
//        }
//
//        return status == CAIRO_STATUS_SUCCESS;
//    }
//
//    std::vector<uint8_t> SaveImageToMemoryPlatform(const UCImage& image,
//                                                   UCImageFormat format) {
//
//        // Get native Cairo surface
//        cairo_surface_t* surface = static_cast<cairo_surface_t*>(image.GetNativeHandle());
//        if (!surface) {
//            return std::vector<uint8_t>();
//        }
//
//        std::vector<uint8_t> result;
//
//        // Cairo write callback for memory
//        auto writeFunc = [](void* closure, const unsigned char* data, unsigned int length)
//                -> cairo_status_t {
//            auto* vec = static_cast<std::vector<uint8_t>*>(closure);
//            vec->insert(vec->end(), data, data + length);
//            return CAIRO_STATUS_SUCCESS;
//        };
//
//        if (format == UCImageFormat::PNG || format == UCImageFormat::Auto) {
//            cairo_surface_write_to_png_stream(surface, writeFunc, &result);
//        }
//
//        return result;
//    }
//
//// ===== PLATFORM-SPECIFIC FORMAT SUPPORT =====
//
//    std::vector<std::string> GetPlatformSupportedLoadFormats() {
//        return LinuxImageLoader::GetSupportedFormats();
//    }
//
//    std::vector<std::string> GetPlatformSupportedSaveFormats() {
//        // Cairo natively supports PNG, can be extended with other libraries
//        return {"png"};
//    }
//
//// ===== OVERRIDE PUBLIC INTERFACE IMPLEMENTATIONS =====
//
//    bool UltraCanvasImageLoader::SaveToFile(
//            const UCImage& image, const std::string& filePath, UCImageFormat format) {
//
//        return SaveImageToFilePlatform(image, filePath, format);
//    }
//
//    std::vector<uint8_t> UltraCanvasImageLoader::SaveToMemory(
//            const UCImage& image, UCImageFormat format) {
//
//        return SaveImageToMemoryPlatform(image, format);
//    }
//
//    std::vector<std::string> UltraCanvasImageLoader::GetSupportedLoadFormats() {
//        return GetPlatformSupportedLoadFormats();
//    }
//
//    std::vector<std::string> UltraCanvasImageLoader::GetSupportedSaveFormats() {
//        return GetPlatformSupportedSaveFormats();
//    }
//
//// ===== LINUX-SPECIFIC INITIALIZATION =====
//
//    bool InitializePlatformImageLoader() {
//        // Any Linux-specific initialization
//        // Cairo doesn't need explicit initialization
//        return true;
//    }
//
//    void ShutdownPlatformImageLoader() {
//        // Any Linux-specific cleanup
//        LinuxImageLoader::ClearCache();
//    }

} // namespace UltraCanvas