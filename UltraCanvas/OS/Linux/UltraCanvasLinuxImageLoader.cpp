// OS/Linux/UltraCanvasLinuxImageLoader.cpp
// Linux image loading implementation with PNG and JPG support
// Version: 1.0.0
// Last Modified: 2025-01-17
// Author: UltraCanvas Framework

#include "UltraCanvasLinuxImageLoader.h"
#include <png.h>
#include <jpeglib.h>
#include <jerror.h>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <setjmp.h>
#include <cstring>

namespace UltraCanvas {

// ===== STATIC MEMBER INITIALIZATION =====
std::unordered_map<std::string, std::shared_ptr<CachedImage>> LinuxImageLoader::imageCache;
std::mutex LinuxImageLoader::cacheMutex;
size_t LinuxImageLoader::maxCacheSize = 50 * 1024 * 1024; // 50MB default
size_t LinuxImageLoader::currentCacheSize = 0;
bool LinuxImageLoader::cachingEnabled = true;

// ===== PNG LOADING IMPLEMENTATION =====
cairo_surface_t* LinuxImageLoader::LoadPngImage(const std::string& filePath, int& width, int& height) {
    FILE* file = fopen(filePath.c_str(), "rb");
    if (!file) {
        std::cerr << "LinuxImageLoader: Cannot open PNG file: " << filePath << std::endl;
        return nullptr;
    }

    // Check PNG signature
    uint8_t header[8];
    if (fread(header, 1, 8, file) != 8 || png_sig_cmp(header, 0, 8)) {
        std::cerr << "LinuxImageLoader: Invalid PNG file: " << filePath << std::endl;
        fclose(file);
        return nullptr;
    }

    // Create PNG structures
    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png_ptr) {
        std::cerr << "LinuxImageLoader: Failed to create PNG read struct" << std::endl;
        fclose(file);
        return nullptr;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        std::cerr << "LinuxImageLoader: Failed to create PNG info struct" << std::endl;
        png_destroy_read_struct(&png_ptr, nullptr, nullptr);
        fclose(file);
        return nullptr;
    }

    // Error handling
    if (setjmp(png_jmpbuf(png_ptr))) {
        std::cerr << "LinuxImageLoader: Error reading PNG file: " << filePath << std::endl;
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
        std::cerr << "LinuxImageLoader: Failed to create Cairo surface for PNG" << std::endl;
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

cairo_surface_t* LinuxImageLoader::LoadJpegImage(const std::string& filePath, int& width, int& height) {
    FILE* file = fopen(filePath.c_str(), "rb");
    if (!file) {
        std::cerr << "LinuxImageLoader: Cannot open JPEG file: " << filePath << std::endl;
        return nullptr;
    }

    struct jpeg_decompress_struct cinfo;
    JpegErrorManager jerr;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = JpegErrorExit;

    if (setjmp(jerr.setjmp_buffer)) {
        std::cerr << "LinuxImageLoader: Error reading JPEG file: " << filePath << std::endl;
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
        std::cerr << "LinuxImageLoader: Unsupported JPEG color format" << std::endl;
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
        std::cerr << "LinuxImageLoader: Failed to create Cairo surface for JPEG" << std::endl;
        delete[] argb_data;
        return nullptr;
    }

    cairo_surface_mark_dirty(surface);
    return surface;
}

// ===== MEMORY-BASED PNG LOADING =====
struct PngMemoryReader {
    const uint8_t* data;
    size_t size;
    size_t offset;
};

void PngReadMemoryCallback(png_structp png_ptr, png_bytep outBytes, png_size_t byteCountToRead) {
    PngMemoryReader* reader = reinterpret_cast<PngMemoryReader*>(png_get_io_ptr(png_ptr));
    
    if (reader->offset + byteCountToRead > reader->size) {
        png_error(png_ptr, "Read past end of memory buffer");
        return;
    }
    
    memcpy(outBytes, reader->data + reader->offset, byteCountToRead);
    reader->offset += byteCountToRead;
}

cairo_surface_t* LinuxImageLoader::LoadPngFromMemory(const uint8_t* data, size_t size, int& width, int& height) {
    if (!data || size < 8) return nullptr;

    // Check PNG signature
    if (png_sig_cmp(const_cast<uint8_t*>(data), 0, 8)) {
        std::cerr << "LinuxImageLoader: Invalid PNG data in memory" << std::endl;
        return nullptr;
    }

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png_ptr) return nullptr;

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_read_struct(&png_ptr, nullptr, nullptr);
        return nullptr;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
        return nullptr;
    }

    PngMemoryReader reader = {data, size, 8}; // Skip signature
    png_set_read_fn(png_ptr, &reader, PngReadMemoryCallback);
    png_set_sig_bytes(png_ptr, 8);

    // Same processing as file-based PNG loading
    png_read_info(png_ptr, info_ptr);
    width = png_get_image_width(png_ptr, info_ptr);
    height = png_get_image_height(png_ptr, info_ptr);
    
    // Configure for RGBA
    png_byte color_type = png_get_color_type(png_ptr, info_ptr);
    png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);
    
    if (bit_depth == 16) png_set_strip_16(png_ptr);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png_ptr);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png_ptr);
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png_ptr);
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png_ptr);

    png_read_update_info(png_ptr, info_ptr);

    size_t row_bytes = png_get_rowbytes(png_ptr, info_ptr);
    uint8_t* image_data = new uint8_t[height * row_bytes];
    png_bytep* row_pointers = new png_bytep[height];

    for (int y = 0; y < height; y++) {
        row_pointers[y] = image_data + y * row_bytes;
    }

    png_read_image(png_ptr, row_pointers);
    png_read_end(png_ptr, nullptr);

    cairo_surface_t* surface = cairo_image_surface_create_for_data(
        image_data, CAIRO_FORMAT_ARGB32, width, height, cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width));

    if (cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS) {
        // Convert RGBA to BGRA for Cairo
        uint32_t* pixels = reinterpret_cast<uint32_t*>(image_data);
        for (int i = 0; i < width * height; i++) {
            uint32_t pixel = pixels[i];
            uint8_t r = (pixel >> 0) & 0xFF;
            uint8_t g = (pixel >> 8) & 0xFF;
            uint8_t b = (pixel >> 16) & 0xFF;
            uint8_t a = (pixel >> 24) & 0xFF;

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
        }
        cairo_surface_mark_dirty(surface);
    } else {
        delete[] image_data;
        surface = nullptr;
    }

    delete[] row_pointers;
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    return surface;
}

// ===== MEMORY-BASED JPEG LOADING =====
struct JpegMemorySource {
    struct jpeg_source_mgr pub;
    const uint8_t* data;
    size_t size;
};

void InitMemorySource(j_decompress_ptr cinfo) {}
boolean FillInputBuffer(j_decompress_ptr cinfo) { return FALSE; }
void SkipInputData(j_decompress_ptr cinfo, long num_bytes) {
    JpegMemorySource* src = reinterpret_cast<JpegMemorySource*>(cinfo->src);
    if (num_bytes > 0) {
        src->pub.next_input_byte += num_bytes;
        src->pub.bytes_in_buffer -= num_bytes;
    }
}
void TermMemorySource(j_decompress_ptr cinfo) {}

cairo_surface_t* LinuxImageLoader::LoadJpegFromMemory(const uint8_t* data, size_t size, int& width, int& height) {
    if (!data || size < 2) return nullptr;

    struct jpeg_decompress_struct cinfo;
    JpegErrorManager jerr;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = JpegErrorExit;

    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&cinfo);
        return nullptr;
    }

    jpeg_create_decompress(&cinfo);

    // Set up memory source
    JpegMemorySource* src = new JpegMemorySource;
    src->pub.init_source = InitMemorySource;
    src->pub.fill_input_buffer = FillInputBuffer;
    src->pub.skip_input_data = SkipInputData;
    src->pub.resync_to_restart = jpeg_resync_to_restart;
    src->pub.term_source = TermMemorySource;
    src->pub.bytes_in_buffer = size;
    src->pub.next_input_byte = data;
    src->data = data;
    src->size = size;
    cinfo.src = reinterpret_cast<struct jpeg_source_mgr*>(src);

    jpeg_read_header(&cinfo, TRUE);
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    width = cinfo.output_width;
    height = cinfo.output_height;

    size_t row_stride = width * 3;
    uint8_t* rgb_data = new uint8_t[height * row_stride];
    uint8_t* row_pointer = rgb_data;

    while (cinfo.output_scanline < cinfo.output_height) {
        jpeg_read_scanlines(&cinfo, &row_pointer, 1);
        row_pointer += row_stride;
    }

    jpeg_finish_decompress(&cinfo);
    delete src;
    jpeg_destroy_decompress(&cinfo);

    // Convert to ARGB
    uint8_t* argb_data = new uint8_t[width * height * 4];
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int rgb_index = (y * width + x) * 3;
            int argb_index = (y * width + x) * 4;
            
            uint8_t r = rgb_data[rgb_index + 0];
            uint8_t g = rgb_data[rgb_index + 1];
            uint8_t b = rgb_data[rgb_index + 2];
            
            argb_data[argb_index + 0] = b;   // B
            argb_data[argb_index + 1] = g;   // G
            argb_data[argb_index + 2] = r;   // R
            argb_data[argb_index + 3] = 255; // A
        }
    }

    delete[] rgb_data;

    cairo_surface_t* surface = cairo_image_surface_create_for_data(
        argb_data, CAIRO_FORMAT_ARGB32, width, height, cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width));

    if (cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS) {
        cairo_surface_mark_dirty(surface);
    } else {
        delete[] argb_data;
        surface = nullptr;
    }

    return surface;
}

// ===== FORMAT DETECTION =====
bool LinuxImageLoader::IsPngFile(const std::string& filePath) {
    std::string ext = GetFileExtension(filePath);
    return ext == "png";
}

bool LinuxImageLoader::IsJpegFile(const std::string& filePath) {
    std::string ext = GetFileExtension(filePath);
    return ext == "jpg" || ext == "jpeg";
}

bool LinuxImageLoader::IsPngData(const uint8_t* data, size_t size) {
    return size >= 8 && png_sig_cmp(const_cast<uint8_t*>(data), 0, 8) == 0;
}

bool LinuxImageLoader::IsJpegData(const uint8_t* data, size_t size) {
    return size >= 2 && data[0] == 0xFF && data[1] == 0xD8;
}

// ===== PUBLIC INTERFACE IMPLEMENTATION =====
ImageLoadResult LinuxImageLoader::LoadImage(const std::string& imagePath) {
    ImageLoadResult result;
        std::cout << "LinuxImageLoader::LoadImage path=" << imagePath << std::endl;

    if (imagePath.empty()) {
        result.errorMessage = "Empty image path";
        return result;
    }

    // Check cache first
    if (cachingEnabled) {
        std::string cacheKey = GenerateCacheKey(imagePath);
        auto cached = GetFromCache(cacheKey);
        if (cached) {
            result.success = true;
            result.surface = cached->surface;
            result.width = cached->width;
            result.height = cached->height;
            cairo_surface_reference(result.surface); // Increase reference count
            return result;
        }
    }

    // Load based on format
    cairo_surface_t* surface = nullptr;
    int width = 0, height = 0;
    std::cout << "LinuxImageLoader::LoadImage file=" << imagePath << std::endl;

    if (IsPngFile(imagePath)) {
        std::cout << "LinuxImageLoader::LoadImage png=" << imagePath << std::endl;
        surface = LoadPngImage(imagePath, width, height);
    } else if (IsJpegFile(imagePath)) {
        std::cout << "IsJpegFile" << std::endl;
        surface = LoadJpegImage(imagePath, width, height);
    } else {
        result.errorMessage = "Unsupported image format";
        return result;
    }

    if (surface) {
        result.success = true;
        result.surface = surface;
        result.width = width;
        result.height = height;

        // Add to cache
        if (cachingEnabled) {
            AddToCache(GenerateCacheKey(imagePath), surface, width, height);
        }
    } else {
        result.errorMessage = "Failed to load image";
    }

    return result;
}

ImageLoadResult LinuxImageLoader::LoadImageFromMemory(const uint8_t* data, size_t size) {
    ImageLoadResult result;
    
    if (!data || size == 0) {
        result.errorMessage = "Invalid image data";
        return result;
    }

    // Check cache
    if (cachingEnabled) {
        std::string cacheKey = GenerateCacheKey(data, size);
        auto cached = GetFromCache(cacheKey);
        if (cached) {
            result.success = true;
            result.surface = cached->surface;
            result.width = cached->width;
            result.height = cached->height;
            cairo_surface_reference(result.surface);
            return result;
        }
    }

    cairo_surface_t* surface = nullptr;
    int width = 0, height = 0;

    if (IsPngData(data, size)) {
        surface = LoadPngFromMemory(data, size, width, height);
    } else if (IsJpegData(data, size)) {
        surface = LoadJpegFromMemory(data, size, width, height);
    } else {
        result.errorMessage = "Unsupported image format in memory";
        return result;
    }

    if (surface) {
        result.success = true;
        result.surface = surface;
        result.width = width;
        result.height = height;

        if (cachingEnabled) {
            AddToCache(GenerateCacheKey(data, size), surface, width, height);
        }
    } else {
        result.errorMessage = "Failed to load image from memory";
    }

    return result;
}

// ===== CACHE MANAGEMENT =====
void LinuxImageLoader::AddToCache(const std::string& key, cairo_surface_t* surface, int width, int height) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    auto cached = std::make_shared<CachedImage>();
    cached->surface = surface;
    cached->width = width;
    cached->height = height;
    cached->lastAccessed = std::chrono::steady_clock::now();
    cached->memorySize = width * height * 4; // ARGB32
    
    cairo_surface_reference(surface); // Increase reference for cache
    
    imageCache[key] = cached;
    currentCacheSize += cached->memorySize;
    
    // Cleanup if cache is too large
    if (currentCacheSize > maxCacheSize) {
        CleanupOldCacheEntries();
    }
}

std::shared_ptr<CachedImage> LinuxImageLoader::GetFromCache(const std::string& key) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    auto it = imageCache.find(key);
    if (it != imageCache.end()) {
        it->second->lastAccessed = std::chrono::steady_clock::now();
        return it->second;
    }
    return nullptr;
}

void LinuxImageLoader::CleanupOldCacheEntries() {
    // Remove oldest entries until we're under the limit
    while (currentCacheSize > maxCacheSize * 0.8 && !imageCache.empty()) {
        auto oldest = imageCache.begin();
        auto oldestTime = oldest->second->lastAccessed;
        
        for (auto it = imageCache.begin(); it != imageCache.end(); ++it) {
            if (it->second->lastAccessed < oldestTime) {
                oldest = it;
                oldestTime = it->second->lastAccessed;
            }
        }
        
        currentCacheSize -= oldest->second->memorySize;
        imageCache.erase(oldest);
    }
}

void LinuxImageLoader::SetMaxCacheSize(size_t maxSize) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    maxCacheSize = maxSize;
    if (currentCacheSize > maxCacheSize) {
        CleanupOldCacheEntries();
    }
}

void LinuxImageLoader::ClearCache() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    imageCache.clear();
    currentCacheSize = 0;
}

size_t LinuxImageLoader::GetCacheSize() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    return imageCache.size();
}

size_t LinuxImageLoader::GetCacheMemoryUsage() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    return currentCacheSize;
}

void LinuxImageLoader::EnableCaching(bool enable) {
    cachingEnabled = enable;
    if (!enable) {
        ClearCache();
    }
}

bool LinuxImageLoader::IsCachingEnabled() {
    return cachingEnabled;
}

std::vector<std::string> LinuxImageLoader::GetSupportedFormats() {
    return {"png", "jpg", "jpeg"};
}

bool LinuxImageLoader::IsFormatSupported(const std::string& extension) {
    std::string ext = extension;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == "png" || ext == "jpg" || ext == "jpeg";
}

} // namespace UltraCanvas
