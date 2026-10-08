# UltraCanvasImageElement Documentation

## Overview

**UltraCanvasImageElement** is a versatile image display component in the UltraCanvas framework that provides comprehensive image loading, caching, transformation, and interaction capabilities. It supports multiple image formats and offers various scaling modes for flexible image presentation.

**Version:** 1.0.2  
**Last Modified:** 2026-10-07  
**Author:** UltraCanvas Framework  
**Header:** `include/UltraCanvasImageElement.h`  
**Implementation:** `core/UltraCanvasImageElement.cpp`

## Features

- **Multi-format Support**: every format the image loader reads (`UCImageLoadFormat`: PNG, JPEG, GIF, WebP, TIFF, BMP, SVG, ICO, AVIF, HEIF, JPEG XL and more)
- **Flexible Loading**: Load from a file, or show an already-decoded `UCImage` (for example one decoded from memory)
- **Fit Modes**: CSS-style `object-fit` (`ImageFitMode`), `object-position` and background repeat
- **Transformations**: Rotation, scaling, offset, opacity, tint
- **Animation**: Animated GIF and WebP play automatically
- **Interaction**: Click, hover and drag support
- **Error Handling**: The reason for a failed load, and an error placeholder
- **Performance**: Decoded images are cached by path

## Class Definition

```cpp
namespace UltraCanvas {
    class UltraCanvasImageElement : public UltraCanvasUIElement {
        // members are listed in the sections below
    };
}
```

## Enumerations and Structures

### ImageFitMode
Controls how the image is fitted into the element's content box
(`UltraCanvasCommonTypes.h`, the CSS `object-fit` values):

```cpp
enum class ImageFitMode {
    NoScale,    // Original size
    Contain,    // Scale uniformly to fit (keeps aspect ratio) - the default
    Cover,      // Scale uniformly to fill (may crop)
    Fill,       // Stretch to the bounds (may distort)
    ScaleDown   // Like Contain, but never enlarges
};
```

### ImagePosition
Where a fitted image that does not fill its box sits in it, per axis (CSS
`object-position`). The default is centred:

```cpp
struct ImageAxisPosition {
    float value   = 0.5f;    // fraction of the free space (0 = left/top, 1 = right/bottom)
    bool  pixels  = false;   // value is px, not a fraction
    bool  fromEnd = false;   // px measured from the right / bottom edge

    static ImageAxisPosition Fraction(float f);
    static ImageAxisPosition Pixels(float px, bool fromEndEdge = false);
};

struct ImagePosition {
    ImageAxisPosition x;
    ImageAxisPosition y;
    bool IsCentred() const;
};
```

## Constructors

```cpp
UltraCanvasImageElement(const std::string& identifier,
                        float x, float y, float w, float h);
UltraCanvasImageElement(const std::string& identifier,
                        float w, float h);                        // positioned by a layout
UltraCanvasImageElement(const std::string& identifier = "ImageElement");   // sized from the image
```

Without a size, the layout gives the element the image's natural size.

## Image Loading Methods

### LoadFromFile
```cpp
bool LoadFromFile(const std::string& filePath, bool forceLoad = false);
```
Loads an image from a file path and returns true on success. Loading is
synchronous: `onImageLoaded` or `onImageLoadFailed` fires before it returns.
Images are cached by path; `forceLoad = true` evicts the cached copy first
(use it after the file on disk changed).

### LoadFromImage
```cpp
bool LoadFromImage(std::shared_ptr<UCImage> img);
```
Shows an image that is already decoded, for example one from
`UCImage::LoadFromMemory()`. It reports like `LoadFromFile()`: a valid image
returns true and fires `onImageLoaded`; an image whose decode failed returns
false, fires `onImageLoadFailed` with the reason and shows the error
placeholder. A null or empty image (`std::make_shared<UCImage>()`) clears the
element and fires neither.

### GetLastError
```cpp
const std::string& GetLastError() const;
```
The reason the last `LoadFromFile()` or `LoadFromImage()` failed (missing,
locked, unsupported format, undecodable data); empty on success.

## Display Properties

### Fit, Position and Repeat
```cpp
void SetFitMode(ImageFitMode mode);
ImageFitMode GetFitMode() const;

void SetImagePosition(const ImagePosition& position);
const ImagePosition& GetImagePosition() const;

void SetImageRepeat(bool x, bool y);     // tile across and/or down (CSS background-repeat)
bool GetImageRepeatX() const;
bool GetImageRepeatY() const;

// With a width and no height, the height follows the picture's aspect ratio,
// larger as well as smaller (off by default: the height then only shrinks)
void SetHeightFollowsWidth(bool follows);
bool GetHeightFollowsWidth() const;

// Element-local rectangle the image is drawn into for the current fit and position
Rect2Df ImageDrawRect() const;
```

### Visual Effects
```cpp
void SetTintColor(const Color& color);         // Multiply the picture's colours (white = untinted)
void SetOpacity(float alpha);                  // Set transparency (0.0-1.0)
float GetOpacity() const;
void SetRotation(float degrees);               // Rotate around the center
void SetScale(float sx, float sy);             // Scale transformation
void SetOffset(float ox, float oy);            // Position offset
```

`SetTintColor` multiplies every pixel of the picture by the colour, inside the
picture only: white (the default) leaves it unchanged, `Color(255, 0, 0)`
keeps only its red, and the colour's alpha sets the strength. It is drawn with
render-context groups; a context without groups draws the picture untinted.

## Image Information

```cpp
Point2Di GetImageSize() const;                 // Original dimensions, (0, 0) when nothing is loaded
const std::string& GetLastError() const;       // Reason for the last failed load
```

## Animation

Animated GIF and WebP images start playing as soon as they are loaded.

```cpp
void SetAnimationEnabled(bool enable);         // false: show only the first frame
bool IsAnimationEnabled() const;
bool IsAnimatedImage() const;                  // more than one frame
void PlayAnimation();
void PauseAnimation();
void StopAnimation();                          // rewinds to frame 0
bool IsAnimationPlaying() const;
UCImageAnimationController& GetAnimationController();   // frame stepping, loops, onEnded
```

## Interaction Features

### Click Support
```cpp
void SetClickable(bool enable);
```
Enables/disables click interaction (`onClick`). Changes cursor to hand pointer when enabled.

### Drag Support
```cpp
void SetDraggable(bool enable);
```
Lets the user drag the element: it moves with the pointer and reports each
step through `onImageDragged`.

## Event Callbacks

```cpp
std::function<void()> onImageLoaded;                        // LoadFromFile / LoadFromImage succeeded
std::function<void(const std::string&)> onImageLoadFailed;  // Load failed with the reason
std::function<void()> onClick;                              // Clicked (needs SetClickable(true))
std::function<void()> onHoverEnter;                         // Pointer came onto the image
std::function<void()> onHoverLeave;                         // Pointer left the image
std::function<void(const Point2Di&)> onImageDragged;        // Dragged (delta position)
```

## Factory Functions

### CreateImageElement
```cpp
inline std::shared_ptr<UltraCanvasImageElement> CreateImageElement(
    const std::string& identifier, float x, float y, float w, float h);
inline std::shared_ptr<UltraCanvasImageElement> CreateImageElement(
    const std::string& identifier, float w = 0, float h = 0);
```
Creates a basic image element.

### CreateImageFromFile
```cpp
inline std::shared_ptr<UltraCanvasImageElement> CreateImageFromFile(
    const std::string& identifier, float x, float y, float w, float h,
    const std::string& imagePath);
```
Creates and loads an image from file.

### CreateImageFromMemory
```cpp
inline std::shared_ptr<UltraCanvasImageElement> CreateImageFromMemory(
    const std::string& identifier, float x, float y, float w, float h,
    const std::vector<uint8_t>& imageData,
    UCImageLoadFormat format = UCImageLoadFormat::Autodetect);
```
Creates an element and decodes the image from memory. The loader detects the
format from the data and takes no hint, so `format` is not used; it stays for
source compatibility. Data that does not decode leaves the element showing the
error placeholder, with the reason in `GetLastError()`.

### CreateScaledImage
```cpp
inline std::shared_ptr<UltraCanvasImageElement> CreateScaledImage(
    const std::string& identifier, float x, float y, float w, float h,
    const std::string& imagePath, ImageFitMode fitMode);
```
Creates an image with a specific fit mode.

### CreateClickableImage
```cpp
inline std::shared_ptr<UltraCanvasImageElement> CreateClickableImage(
    const std::string& identifier, float x, float y, float w, float h,
    const std::string& imagePath, std::function<void()> clickCallback);
```
Creates a clickable image with callback.

## Rendering Process

```cpp
void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
```

1. **Loaded**: Draws the image (or the current animation frame) into the
   content box with the fit mode, position, repeat, tint, opacity and
   transformations
2. **Failed**: Draws the error placeholder: "ERR" and the reason
3. **Nothing loaded**: Draws nothing beyond the element's own background and border

Rounded corners (border radius) clip the picture as well.

## Event Handling

The component handles the following events:

- **MouseDown**: Fires `onClick` (when clickable) and starts a drag (when draggable)
- **MouseMove**: Moves a dragged element; fires `onHoverEnter` / `onHoverLeave`
- **MouseLeave**: Fires `onHoverLeave`
- **MouseUp**: Ends a drag

```cpp
bool OnEvent(const UCEvent& event) override;
```

## Usage Examples

### Basic Image Display
```cpp
// Create and display an image
auto image = CreateImageFromFile("logo", 10, 10, 200, 150, GetResourcesDir() + "media/logo.png");
image->SetFitMode(ImageFitMode::Contain);
window->AddChild(image);
```

### Interactive Image
```cpp
// Create clickable image with hover effect
auto button = CreateImageElement("imageButton", 100, 100, 64, 64);
button->LoadFromFile("icons/button.png");
button->SetClickable(true);
button->SetOpacity(0.8f);

button->onClick = []() {
    std::cerr << "Image clicked!" << std::endl;
};

UltraCanvasImageElement* img = button.get();   // no shared_ptr cycle
button->onHoverEnter = [img]() {
    img->SetOpacity(1.0f);
};

button->onHoverLeave = [img]() {
    img->SetOpacity(0.8f);
};
```

### Draggable Image
```cpp
// Create draggable image element
auto draggable = CreateImageFromFile("icon", 50, 50, 32, 32, "icon.png");
draggable->SetDraggable(true);

draggable->onImageDragged = [](const Point2Di& delta) {
    std::cerr << "Dragged: " << delta.x << ", " << delta.y << std::endl;
};
```

### Image Gallery with Loading Feedback
```cpp
auto gallery = CreateImageElement("photo", 0, 0, 400, 300);

// Set the callbacks before loading: LoadFromFile fires them before it returns
gallery->onImageLoaded = []() {
    std::cerr << "Image loaded successfully" << std::endl;
};

gallery->onImageLoadFailed = [](const std::string& error) {
    std::cerr << "Failed to load: " << error << std::endl;
};

if (!gallery->LoadFromFile("photos/large_photo.jpg")) {
    std::cerr << gallery->GetLastError() << std::endl;
}
```

### Transformed Image
```cpp
// Apply transformations
auto logo = CreateImageFromFile("logo", 200, 200, 100, 100, "logo.svg");
logo->SetRotation(45.0f);          // Rotate 45 degrees
logo->SetScale(1.5f, 1.5f);        // Scale 150%
logo->SetOpacity(0.8f);            // 80% opacity

// Tint a white icon with the accent colour
auto star = CreateImageFromFile("star", 10, 10, 32, 32, "icons/star-white.png");
star->SetTintColor(Color(0, 120, 215));
```

### Positioned and Tiled Images
```cpp
// Cover the box, keep the top of the picture in view
auto banner = CreateScaledImage("banner", 0, 0, 600, 120, "banner.jpg", ImageFitMode::Cover);
ImagePosition top;
top.y = ImageAxisPosition::Fraction(0.0f);
banner->SetImagePosition(top);

// Repeat a pattern across the whole element
auto stripes = CreateImageFromFile("stripes", 0, 130, 600, 40, "stripe.png");
stripes->SetFitMode(ImageFitMode::NoScale);
stripes->SetImageRepeat(true, true);
```

## Performance Considerations

1. **Image Caching**: Decoded images are cached by path, so loading the same file again is cheap
2. **Format Detection**: Automatic format detection from file extension or data headers
3. **Memory Management**: Uses smart pointers for automatic cleanup
4. **Synchronous Loading**: `LoadFromFile()` decodes on the calling thread; load large images ahead of time
5. **Efficient Rendering**: A repeated image is drawn as one pattern fill

## Error Handling

The component provides comprehensive error handling:

- File not found errors
- Unsupported format detection
- Memory allocation failures
- Invalid image data handling

The reason for a failed load is passed to `onImageLoadFailed` and returned by `GetLastError()`.

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
5. **Set the callbacks before loading**: `LoadFromFile()` and `LoadFromImage()` fire them before they return
6. **Use `forceLoad`** only when the file on disk changed: it bypasses the cache

## See Also

- [UltraCanvasUIElement](UltraCanvasUIElement.md) - Base class
- [UltraCanvasRenderContext](UltraCanvasRenderContext.md) - Rendering system
- [UltraCanvasEvent](UltraCanvasEvent.md) - Event handling
- [UltraCanvasButton](UltraCanvasButton.md) - Interactive button component
- [UltraCanvasVideoElement](UltraCanvasVideoElement.md) - Video playback component