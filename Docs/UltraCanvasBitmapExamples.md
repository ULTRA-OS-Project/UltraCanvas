# UltraCanvasImageElement Documentation

## Overview

**UltraCanvasImageElement** is a versatile image display component in the UltraCanvas framework that provides comprehensive image loading, caching, transformation, and interaction capabilities. It supports multiple image formats and offers various scaling modes for flexible image presentation.

**Version:** 1.0.0  
**Last Modified:** 2024-12-30  
**Author:** UltraCanvas Framework  
**Header:** `include/UltraCanvasImageElement.h`  
**Implementation:** `core/UltraCanvasImageElement.cpp`

## Features

- **Multi-format Support**: PNG, JPEG, BMP, GIF, TIFF, WebP, SVG, ICO, AVIF
- **Flexible Loading**: Load from file or memory
- **Scaling Modes**: Multiple image scaling and positioning options
- **Transformations**: Rotation, scaling, offset, tinting, opacity
- **Interaction**: Click and drag support with customizable cursors
- **Error Handling**: Graceful error display with placeholders
- **Performance**: Caching and async loading options
- **Visual Effects**: Tinting, opacity, smooth scaling

## Class Definition

```cpp
namespace UltraCanvas {
    class UltraCanvasImageElement : public UltraCanvasUIElement {
        // ... implementation
    };
}
```

## Enumerations

### ImageFormat
Defines supported image file formats:

```cpp
enum class ImageFormat {
    Unknown,
    PNG,
    JPEG,
    JPG,
    BMP,
    GIF,
    TIFF,
    WEBP,
    SVG,
    ICO,
    AVIF
};
```

### ImageScaleMode
Controls how images are scaled and positioned:

```cpp
enum class ImageScaleMode {
    NoScale,         // Display at original size
    Percentage,         // Percentage to fit bounds (may distort)
    Uniform,         // Scale uniformly to fit (maintain aspect ratio)
    UniformToFill,   // Scale uniformly to fill (may crop)
    Center,          // Center without scaling
    Tile             // Tile to fill bounds
};
```

### ImageLoadState
Tracks the loading status of an image:

```cpp
enum class ImageLoadState {
    NotLoaded,
    Loading,
    Loaded,
    Failed
};
```

## Data Structures

### ImageData
Contains raw image data and metadata:

```cpp
struct ImageData {
    std::vector<uint8_t> rawData;  // Raw image bytes
    int width = 0;                 // Image width
    int height = 0;                // Image height  
    int channels = 0;              // Color channels (3=RGB, 4=RGBA)
    ImageFormat format;            // Image format
    bool isValid = false;          // Validity flag
    
    size_t GetDataSize() const;   // Calculate data size
    bool HasAlpha() const;         // Check for alpha channel
};
```

## Constructor

```cpp
UltraCanvasImageElement(
    const std::string& identifier = "ImageElement",
    long id = 0,
    long x = 0,
    long y = 0,
    long w = 100,
    long h = 100
);
```

Creates an image element with specified position and dimensions.

## Image Loading Methods

### LoadFromFile
```cpp
bool LoadFromFile(const std::string& filePath);
```
Loads an image from a file path. Returns true on success.

### LoadFromMemory
```cpp
bool LoadFromMemory(const std::vector<uint8_t>& data, ImageFormat format = ImageFormat::Unknown);
bool LoadFromMemory(const uint8_t* data, size_t size, ImageFormat format = ImageFormat::Unknown);
```
Loads an image from memory buffer. Format can be auto-detected if Unknown.

## Display Properties

### Scale Mode
```cpp
void SetScaleMode(ImageScaleMode mode);
ImageScaleMode GetScaleMode() const;
```
Controls image scaling behavior.

### Visual Effects
```cpp
void SetTintColor(const Color& color);         // Apply color tint
void SetOpacity(float alpha);                  // Set transparency (0.0-1.0)
float GetOpacity() const;
void SetRotation(float degrees);               // Rotate image
void SetScale(float sx, float sy);             // Scale transformation
void SetOffset(float ox, float oy);            // Position offset
```

## Image Information

### Status Queries
```cpp
bool IsLoaded() const;                         // Check if loaded successfully
bool IsLoading() const;                        // Check if currently loading
bool HasError() const;                         // Check for load errors
const std::string& GetErrorMessage() const;    // Get error details
```

### Image Properties
```cpp
Point2Di GetImageSize() const;                 // Get original dimensions
ImageFormat GetImageFormat() const;            // Get detected format
const std::string& GetImagePath() const;       // Get source file path
```

## Interaction Features

### Click Support
```cpp
void SetClickable(bool enable);
```
Enables/disables click interaction. Changes cursor to hand pointer when enabled.

### Drag Support
```cpp
void SetDraggable(bool enable);
```
Enables/disables drag-and-drop functionality.

## Event Callbacks

```cpp
std::function<void()> onImageLoaded;                    // Image loaded successfully
std::function<void(const std::string&)> onImageLoadFailed;  // Load failed with error
std::function<void()> onImageClicked;                   // Image clicked
std::function<void(const Point2Di&)> onImageDragged;    // Image dragged (delta position)
```

## Factory Functions

### CreateImageElement
```cpp
std::shared_ptr<UltraCanvasImageElement> CreateImageElement(
    const std::string& identifier, long id,
    long x, long y, long w, long h
);
```
Creates a basic image element.

### CreateImageFromFile
```cpp
std::shared_ptr<UltraCanvasImageElement> CreateImageFromFile(
    const std::string& identifier, long id,
    long x, long y, long w, long h,
    const std::string& imagePath
);
```
Creates and loads an image from file.

### CreateImageFromMemory
```cpp
std::shared_ptr<UltraCanvasImageElement> CreateImageFromMemory(
    const std::string& identifier, long id,
    long x, long y, long w, long h,
    const std::vector<uint8_t>& imageData,
    ImageFormat format = ImageFormat::Unknown
);
```
Creates and loads an image from memory.

### CreateScaledImage
```cpp
std::shared_ptr<UltraCanvasImageElement> CreateScaledImage(
    const std::string& identifier, long id,
    long x, long y, long w, long h,
    const std::string& imagePath,
    ImageScaleMode scaleMode
);
```
Creates an image with specific scale mode.

### CreateClickableImage
```cpp
std::shared_ptr<UltraCanvasImageElement> CreateClickableImage(
    const std::string& identifier, long id,
    long x, long y, long w, long h,
    const std::string& imagePath,
    std::function<void()> clickCallback
);
```
Creates a clickable image with callback.

## Rendering Process

The rendering system handles different states:

1. **Loaded State**: Renders the image with applied transformations
2. **Loading State**: Shows loading placeholder ("...")
3. **Error State**: Shows error placeholder with message
4. **Not Loaded**: Shows default placeholder

### Rendering Pipeline
```cpp
void Render(IRenderContext* ctx) override {
    if (IsLoaded())
        DrawLoadedImage(ctx);
    else if (HasError() && showErrorPlaceholder)
        DrawErrorPlaceholder(ctx);
    else if (IsLoading())
        DrawLoadingPlaceholder(ctx);
}
```

## Event Handling

The component handles the following events:

- **MouseDown**: Initiates click or drag operations
- **MouseMove**: Updates drag position
- **MouseUp**: Completes click or drag operations

```cpp
bool OnEvent(const UCEvent& event) override;
```

## Usage Examples

### Basic Image Display
```cpp
// Create and display an image
auto image = CreateImageFromFile("logo", 1, 10, 10, 200, 150, "media/logo.png");
image->SetScaleMode(ImageScaleMode::Uniform);
window->AddChild(image);
```

### Interactive Image
```cpp
// Create clickable image with hover effect
auto button = CreateImageElement("imageButton", 100, 100, 64, 64);
button->LoadFromFile("icons/button.png");
button->SetClickable(true);
button->SetTintColor(Color(200, 200, 200));

button->onImageClicked = []() {
    std::cout << "Image clicked!" << std::endl;
};

button->onHoverEnter = [button]() {
    button->SetTintColor(Colors::White);
};

button->onHoverLeave = [button]() {
    button->SetTintColor(Color(200, 200, 200));
};
```

### Draggable Image
```cpp
// Create draggable image element
auto draggable = CreateImageFromFile("icon", 3, 50, 50, 32, 32, "icon.png");
draggable->SetDraggable(true);

draggable->onImageDragged = [](const Point2Di& delta) {
    std::cout << "Dragged: " << delta.x << ", " << delta.y << std::endl;
};
```

### Image Gallery with Loading
```cpp
// Create image with loading feedback
auto gallery = CreateImageElement("photo", 0, 0, 400, 300);

gallery->onImageLoaded = []() {
    std::cout << "Image loaded successfully" << std::endl;
};

gallery->onImageLoadFailed = [](const std::string& error) {
    std::cerr << "Failed to load: " << error << std::endl;
};

// Load image asynchronously
gallery->LoadFromFile("photos/large_photo.jpg");
```

### Transformed Image
```cpp
// Apply transformations
auto logo = CreateImageFromFile("logo", 5, 200, 200, 100, 100, "logo.svg");
logo->SetRotation(45.0f);          // Rotate 45 degrees
logo->SetScale(1.5f, 1.5f);        // Scale 150%
logo->SetOpacity(0.8f);            // 80% opacity
logo->SetTintColor(Colors::Blue);  // Blue tint
```

## Performance Considerations

1. **Image Caching**: Loaded images are cached by default to avoid reloading
2. **Format Detection**: Automatic format detection from file extension or data headers
3. **Memory Management**: Uses smart pointers for automatic cleanup
4. **Lazy Loading**: Images can be loaded on-demand
5. **Efficient Rendering**: Only renders visible images

## Error Handling

The component provides comprehensive error handling:

- File not found errors
- Unsupported format detection
- Memory allocation failures
- Invalid image data handling

Error states are displayed with customizable placeholders and messages accessible via `GetErrorMessage()`.

## Platform Notes

- **Cross-platform**: Works on Windows, Linux, macOS
- **Format Support**: Actual format support depends on platform-specific implementations
- **Rendering Backend**: Uses the unified UltraCanvas rendering system
- **File I/O**: Standard C++ file operations for loading

## Best Practices

1. **Use appropriate scale modes** to maintain aspect ratios
2. **Preload critical images** before display
3. **Handle loading errors** with appropriate fallbacks
4. **Optimize image sizes** for target display dimensions
5. **Use callbacks** for async loading feedback
6. **Cache frequently used images** to improve performance

## See Also

- [UltraCanvasUIElement](UltraCanvasUIElement.md) - Base class
- [UltraCanvasRenderContext](UltraCanvasRenderContext.md) - Rendering system
- [UltraCanvasEvent](UltraCanvasEvent.md) - Event handling
- [UltraCanvasButton](UltraCanvasButton.md) - Interactive button component
- [UltraCanvasVideoElement](UltraCanvasVideoElement.md) - Video playback component