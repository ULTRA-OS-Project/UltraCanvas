// ImageRenderingExample.cpp
// Example demonstrating UltraCanvas Linux image rendering capabilities
// Version: 1.0.0
// Last Modified: 2025-01-17
// Author: UltraCanvas Framework

#include "UltraCanvasUI.h"
//#include "../UltraCanvas/OS/Linux/UltraCanvasLinuxWindow.h"
#include "../UltraCanvas/OS/Linux/UltraCanvasLinuxRenderContext.h"
#include "../UltraCanvas/OS/Linux/UltraCanvasLinuxImageLoader.h"
#include "UltraCanvasImageElement.h"
//#include "UltraCanvasRenderInterface.h"
#include <iostream>
#include <vector>
#include <string>

using namespace UltraCanvas;

class ImageDemoWindow : public UltraCanvasWindow {
private:
    std::vector<std::string> imagePaths;
    int currentImageIndex;
    bool showImageInfo;
    
public:
    ImageDemoWindow() : UltraCanvasWindow(), currentImageIndex(0), showImageInfo(true) {
        // Sample image paths (you can modify these to point to your test images)
        imagePaths = {
            "./assets/sample.png",
            "./assets/sample1.png",
            "./assets/sample2.jpg",
        };
    }

    virtual bool Create(const WindowConfig& config) override {
        if (UltraCanvasWindow::Create(config)) {
            CreateUserInterface();
            return true;
        }
        return false;
    }

    void CreateUserInterface() {
        std::cout << "=== Creating Cross-Platform UI Elements ===" << std::endl;


        auto dropdown = DropdownBuilder("images", 300, 450, 180)
                .AddItem("Sample one", "0")
                .AddItem("Sample two", "1")
                .AddItem("Sample 3", "2")
                .SetStyle(DropdownStyles::Modern())
                .OnSelectionChanged([this](int index, const DropdownItem& item) {
                    std::cout << "Selected: " << item.text << " (" << item.value << ")" << std::endl;
                    this->currentImageIndex = atoi(item.value.c_str());
                })
                .Build();

        // Add all elements to the window using framework methods
        AddElement(dropdown);
    }

    void Render() override {
        // Call base class rendering first
        UltraCanvasLinuxWindow::Render();
        
        // Set up rendering context
        ULTRACANVAS_WINDOW_RENDER_SCOPE(this);
        
        int width, height;
        GetSize(width, height);

        // Clear background
//        SetFillColor(Color(240, 240, 240, 255));
//        FillRectangle(Rect2D(0, 0, width, height));
        
        // Draw title
        SetFillColor(Color(50, 50, 50, 255));
        DrawText("UltraCanvas Image Rendering Demo", Point2D(20, 30));
        DrawText("Press SPACE to cycle images, I for info, C to clear cache", Point2D(20, 50));
        
        // Demonstrate different image rendering methods
        DemonstrateImageRendering();
        
        // Show image information if enabled
        if (showImageInfo && !imagePaths.empty()) {
            ShowImageInformation();
        }
        
        // Show cache statistics
        ShowCacheStatistics();
    }
    
    void DemonstrateImageRendering() {
        if (imagePaths.empty() || currentImageIndex >= imagePaths.size()) {
            SetFillColor(Color(200, 50, 50, 255));
            DrawText("No images found! Please add PNG/JPG files to assets/ folder", Point2D(20, 100));
            return;
        }
        
        std::string currentImage = imagePaths[currentImageIndex];
        
        // Check if image format is supported
        if (!IsImageFormatSupported(currentImage)) {
            SetFillColor(Color(200, 50, 50, 255));
            DrawText("Unsupported image format: " + currentImage, Point2D(20, 100));
            return;
        }
        
        // Get image dimensions
        Point2D imageDims = GetImageDimensions(currentImage);
        if (imageDims.x == 0 || imageDims.y == 0) {
            SetFillColor(Color(200, 50, 50, 255));
            DrawText("Failed to load image: " + currentImage, Point2D(20, 100));
            return;
        }
        
        // 1. Draw image at original size
        SetStrokeColor(Color(100, 100, 100, 255));
        SetStrokeWidth(1);
        DrawRectangle(Rect2D(19, 79, imageDims.x + 2, imageDims.y + 2));
        DrawImage(currentImage, Point2D(20, 80));
        
        SetFillColor(Color(0, 0, 0, 255));
        DrawText("Original Size", Point2D(20, 75));
        
        // 2. Draw scaled image (fit to 200x150)
        float maxWidth = 200, maxHeight = 150;
        float scaleX = maxWidth / imageDims.x;
        float scaleY = maxHeight / imageDims.y;
        float scale = std::min(scaleX, scaleY);
        float scaledWidth = imageDims.x * scale;
        float scaledHeight = imageDims.y * scale;
        
        Rect2D scaledRect(350, 80, scaledWidth, scaledHeight);
        DrawRectangle(Rect2D(scaledRect.x - 1, scaledRect.y - 1, scaledRect.width + 2, scaledRect.height + 2));
        DrawImage(currentImage, scaledRect);
        
        DrawText("Scaled (Fit)", Point2D(350, 75));
        
        // 3. Draw stretched image (200x150 exactly)
        Rect2D stretchedRect(580, 80, 250, 250);
        DrawRectangle(Rect2D(stretchedRect.x - 1, stretchedRect.y - 1, stretchedRect.width + 2, stretchedRect.height + 2));
        DrawImage(currentImage, stretchedRect);
        
        DrawText("Stretched", Point2D(580, 75));
        
        // 4. Draw cropped image (source rectangle demo)
//        if (imageDims.x > 50 && imageDims.y > 50) {
//            Rect2D sourceRect(imageDims.x * 0.25f, imageDims.y * 0.25f,
//                             imageDims.x * 0.5f, imageDims.y * 0.5f);
//            Rect2D destRect(20, 280, 150, 150);
//
//            DrawRectangle(Rect2D(destRect.x - 1, destRect.y - 1, destRect.width + 2, destRect.height + 2));
//            DrawImage(currentImage, sourceRect, destRect);
//
//            DrawText("Center Crop", Point2D(20, 275));
//        }
        
//        // 5. Draw tiled image
//        Rect2D tileRect(200, 280, 200, 150);
//        DrawRectangle(Rect2D(tileRect.x - 1, tileRect.y - 1, tileRect.width + 2, tileRect.height + 2));
//        DrawImageTiled(currentImage, tileRect);
//
//        DrawText("Tiled", Point2D(200, 275));
//
//        // 6. Draw with different filters (if supported)
//        Rect2D filterRect(420, 280, 100, 100);
//        DrawRectangle(Rect2D(filterRect.x - 1, filterRect.y - 1, filterRect.width + 2, filterRect.height + 2));
//        DrawImageWithFilter(currentImage, filterRect, CAIRO_FILTER_NEAREST);
//
//        DrawText("Nearest Filter", Point2D(420, 275));
//
//        // 7. Demonstrate alpha blending
//        PushRenderState();
//        SetGlobalAlpha(0.7f);
//        DrawImage(currentImage, Rect2D(550, 280, 120, 90));
//        PopRenderState();
//
//        DrawText("70% Alpha", Point2D(550, 275));
    }
    
    void ShowImageInformation() {
        if (currentImageIndex >= imagePaths.size()) return;
        
        std::string currentImage = imagePaths[currentImageIndex];
        Point2D dims = GetImageDimensions(currentImage);
        
        int startY = 450;
        SetFillColor(Color(0, 0, 0, 255));
        
        DrawText("Current Image: " + currentImage, Point2D(20, startY));
        DrawText("Dimensions: " + std::to_string(static_cast<int>(dims.x)) + " x " + 
                std::to_string(static_cast<int>(dims.y)), Point2D(20, startY + 20));
        
        // Show supported formats
        auto formats = LinuxImageLoader::GetSupportedFormats();
        std::string formatList = "Supported: ";
        for (size_t i = 0; i < formats.size(); ++i) {
            formatList += formats[i];
            if (i < formats.size() - 1) formatList += ", ";
        }
        DrawText(formatList, Point2D(20, startY + 40));
    }
    
    void ShowCacheStatistics() {
        int startY = 520;
        SetFillColor(Color(80, 80, 80, 255));
        
        size_t cacheSize = LinuxImageLoader::GetCacheSize();
        size_t memoryUsage = LinuxImageLoader::GetCacheMemoryUsage();
        bool cachingEnabled = LinuxImageLoader::IsCachingEnabled();
        
        DrawText("Image Cache Statistics:", Point2D(20, startY));
        DrawText("Cached Images: " + std::to_string(cacheSize), Point2D(20, startY + 20));
        DrawText("Memory Usage: " + std::to_string(memoryUsage / 1024) + " KB", Point2D(20, startY + 40));
        DrawText("Caching: " + std::string(cachingEnabled ? "Enabled" : "Disabled"), Point2D(20, startY + 60));
    }

    void OnEvent(const UCEvent& event) override {
        UltraCanvasWindow::OnEvent(event);
        if (event.type == UCEventType::KeyDown) {
            switch (event.virtualKey) {
                case UCKeys::Space: // Space key - cycle images
                    currentImageIndex = (currentImageIndex + 1) % imagePaths.size();
                    needsRedraw_ = true;
                    break;

                case 'i':
                case 'I': // Toggle image info
                    showImageInfo = !showImageInfo;
                    needsRedraw_ = true;
                    break;

//                case 'c':
//                case 'C': // Clear cache
//                    ClearImageCache();
//                    std::cout << "Image cache cleared!" << std::endl;
//                    needsRedraw_ = true;
//                    break;

                case 'q':
                case 'Q':
                case 27: // Escape
                    Close();
                    break;
            }
        }

    }
//    void OnKeyPress(int key) override {
//        switch (key) {
//            case ' ': // Space key - cycle images
//                currentImageIndex = (currentImageIndex + 1) % imagePaths.size();
//                needsRedraw_ = true;
//                break;
//
//            case 'i':
//            case 'I': // Toggle image info
//                showImageInfo = !showImageInfo;
//                needsRedraw_ = true;
//                break;
//
//            case 'c':
//            case 'C': // Clear cache
//                ClearImageCache();
//                std::cout << "Image cache cleared!" << std::endl;
//                needsRedraw_ = true;
//                break;
//
//            case 'q':
//            case 'Q':
//            case 27: // Escape
//                Close();
//                break;
//
//            default:
//                UltraCanvasLinuxWindow::OnKeyPress(key);
//                break;
//        }
//    }
};

// ===== ADVANCED IMAGE ELEMENT DEMO =====
class ImageElementDemo : public UltraCanvasWindow {
private:
    std::vector<std::shared_ptr<UltraCanvasImageElement>> imageElements;
    
public:
    ImageElementDemo() : UltraCanvasWindow() {
        CreateImageElements();
    }
    
private:
    void CreateImageElements() {
        // Create various image elements with different configurations
        
        // 1. Simple image element
        auto simpleImage = CreateImageFromFile("simple", 1001, 20, 80, 150, 100, "assets/sample.png");
        simpleImage->SetScaleMode(ImageScaleMode::Uniform);
        imageElements.push_back(simpleImage);
        
        // 2. Clickable image with callback
        auto clickableImage = CreateClickableImage("clickable", 1002, 200, 80, 150, 100, 
                                                  "assets/photo.jpg", []() {
            std::cout << "Image clicked!" << std::endl;
        });
        clickableImage->SetScaleMode(ImageScaleMode::UniformToFill);
        imageElements.push_back(clickableImage);
        
        // 3. Image with rotation and scaling effects
        auto transformedImage = CreateImageFromFile("transformed", 1003, 380, 80, 120, 120, "assets/logo.png");
        transformedImage->SetRotation(0.3f); // ~17 degrees
        transformedImage->SetScale(1.2f, 0.8f);
        transformedImage->SetOpacity(0.8f);
        imageElements.push_back(transformedImage);
        
        // 4. Draggable image
        auto draggableImage = CreateImageFromFile("draggable", 1004, 20, 220, 100, 100, "assets/background.jpeg");
        draggableImage->SetDraggable(true);
        draggableImage->onImageDragged = [](const Point2D& delta) {
            std::cout << "Image dragged by: " << delta.x << ", " << delta.y << std::endl;
        };
        imageElements.push_back(draggableImage);
        
        // 5. Tinted image
        auto tintedImage = CreateImageFromFile("tinted", 1005, 150, 220, 100, 100, "assets/sample.png");
        tintedImage->SetTintColor(Color(255, 200, 200, 255)); // Reddish tint
        imageElements.push_back(tintedImage);
        
        // Add all elements to window
        for (auto& element : imageElements) {
            AddElement(element.get());
        }
    }
    
protected:
    void Render() override {
        UltraCanvasLinuxWindow::Render();
        
        ULTRACANVAS_WINDOW_RENDER_SCOPE(this);
        
        int width, height;
        GetSize(width, height);
        
        // Clear background
        SetFillColor(Color(250, 250, 250, 255));
        FillRectangle(Rect2D(0, 0, width, height));
        
        // Draw title and instructions
        SetFillColor(Color(50, 50, 50, 255));
        DrawText("UltraCanvas Image Element Demo", Point2D(20, 30));
        DrawText("Drag the draggable image, click the clickable one!", Point2D(20, 50));
        
        // Elements will render themselves through the base class
    }
    
//    void OnKeyPress(int key) override {
//        if (key == 'r' || key == 'R') {
//            // Rotate all images
//            for (auto& element : imageElements) {
//                float currentRotation = element->GetRotation();
//                element->SetRotation(currentRotation + 0.1f);
//            }
//            needsRedraw_ = true;
//        } else {
//            ImageElementDemo::OnKeyPress(key);
//        }
//    }
};

// ===== PERFORMANCE BENCHMARK =====
class ImagePerformanceBenchmark {
public:
    static void RunBenchmarks() {
        std::cout << "\n=== UltraCanvas Image Loading Performance Benchmark ===" << std::endl;
        
        std::vector<std::string> testImages = {
            "assets/small.png",      // < 100KB
            "assets/medium.jpg",     // 100KB - 1MB  
            "assets/large.png",      // > 1MB
            "assets/photo.jpeg"      // High resolution photo
        };
        
        for (const auto& imagePath : testImages) {
            BenchmarkImageLoading(imagePath);
        }
        
        BenchmarkCachePerformance();
    }
    
private:
    static void BenchmarkImageLoading(const std::string& imagePath) {
        std::cout << "\nBenchmarking: " << imagePath << std::endl;
        
        const int iterations = 10;
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < iterations; ++i) {
            ImageLoadResult result = LinuxImageLoader::LoadImage(imagePath);
            if (result.success) {
                cairo_surface_destroy(result.surface);
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        std::cout << "  - " << iterations << " loads: " << duration.count() << " ms" << std::endl;
        std::cout << "  - Average: " << (duration.count() / iterations) << " ms per load" << std::endl;
        
        // Get image dimensions for context
        Point2D dims = LinuxImageLoader::LoadImage(imagePath).success ? 
                      Point2D(LinuxImageLoader::LoadImage(imagePath).width, 
                             LinuxImageLoader::LoadImage(imagePath).height) : Point2D(0, 0);
        if (dims.x > 0) {
            std::cout << "  - Dimensions: " << dims.x << " x " << dims.y << std::endl;
        }
    }
    
    static void BenchmarkCachePerformance() {
        std::cout << "\n=== Cache Performance Test ===" << std::endl;
        
        LinuxImageLoader::ClearCache();
        std::string testImage = "assets/sample.png";
        
        // First load (cold cache)
        auto start = std::chrono::high_resolution_clock::now();
        ImageLoadResult result1 = LinuxImageLoader::LoadImage(testImage);
        auto end = std::chrono::high_resolution_clock::now();
        auto coldTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        if (result1.success) {
            cairo_surface_destroy(result1.surface);
        }
        
        // Second load (warm cache)
        start = std::chrono::high_resolution_clock::now();
        ImageLoadResult result2 = LinuxImageLoader::LoadImage(testImage);
        end = std::chrono::high_resolution_clock::now();
        auto warmTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        if (result2.success) {
            cairo_surface_destroy(result2.surface);
        }
        
        std::cout << "Cold cache load: " << coldTime.count() << " μs" << std::endl;
        std::cout << "Warm cache load: " << warmTime.count() << " μs" << std::endl;
        std::cout << "Speedup: " << (coldTime.count() / static_cast<double>(warmTime.count())) << "x" << std::endl;
        
        LinuxImageLoader::ClearCache();
    }
};

// ===== MAIN APPLICATION =====
int main(int argc, char* argv[]) {
    std::cout << "UltraCanvas Linux Image Rendering Demo" << std::endl;
    std::cout << "=====================================" << std::endl;
    
    // Run performance benchmarks if requested
    if (argc > 1 && std::string(argv[1]) == "--benchmark") {
        ImagePerformanceBenchmark::RunBenchmarks();
        return 0;
    }
    
    try {
        // Initialize UltraCanvas application
        auto app =  std::make_unique<UltraCanvasApplication>();
        if (!app->Initialize()) {
            std::cerr << "Failed to initialize UltraCanvas application" << std::endl;
            return -1;
        }
        
        // Configure image cache (50MB max)
        LinuxImageLoader::SetMaxCacheSize(50 * 1024 * 1024);
        LinuxImageLoader::EnableCaching(true);
        
        // Create demo window
        std::cout << "Creating image demo window..." << std::endl;
        auto window = std::make_unique<ImageDemoWindow>();
        
        WindowConfig config;
        config.title = "UltraCanvas Image Demo";
        config.width = 1024;
        config.height = 700;
        config.resizable = true;
        config.backgroundColor = Color(240, 240, 240, 255);
        
        if (!window->Create(config)) {
            std::cerr << "Failed to create demo window" << std::endl;
            return -1;
        }
        
        window->Show();
        
        std::cout << "Demo window created successfully!" << std::endl;
        std::cout << "Controls:" << std::endl;
        std::cout << "  SPACE - Cycle through images" << std::endl;
        std::cout << "  I     - Toggle image information" << std::endl;
        std::cout << "  C     - Clear image cache" << std::endl;
        std::cout << "  Q/ESC - Quit application" << std::endl;
        
        // Run the application
        app->Run();
        
        // Cleanup
        std::cout << "Cleaning up..." << std::endl;
        LinuxImageLoader::ClearCache();
        
    } catch (const std::exception& e) {
        std::cerr << "Exception in main: " << e.what() << std::endl;
        return -1;
    }
    
    std::cout << "Demo completed successfully!" << std::endl;
    return 0;
}

// ===== COMPILATION INSTRUCTIONS =====
/*
To compile this example:

g++ -std=c++20 \
    ImageRenderingExample.cpp \
    UltraCanvasLinuxImageLoader.cpp \
    UltraCanvasLinuxRenderContext.cpp \
    UltraCanvasLinuxWindow.cpp \
    UltraCanvasLinuxApplication.cpp \
    -o ImageDemo \
    `pkg-config --cflags --libs cairo` \
    -lpng -ljpeg -lX11 -pthread

Or using CMake:
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make UltraCanvasImageExample

Run the demo:
./ImageDemo

Run performance benchmarks:
./ImageDemo --benchmark
*/