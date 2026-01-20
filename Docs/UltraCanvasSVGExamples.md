# UltraCanvasSVGElement Documentation

## Overview

`UltraCanvasSVGElement` is a powerful UI component in the UltraCanvas framework that enables rendering and manipulation of Scalable Vector Graphics (SVG) files. It provides full support for SVG 1.1 specifications including paths, shapes, text, gradients, filters, and transformations.

## Header Information

```cpp
// UltraCanvasSVGPlugin.h
// SVG rendering and manipulation support for UltraCanvas
// Version: 2.0.0
// Last Modified: 2025-01-02
// Author: UltraCanvas Framework
```

## Class Definition

```cpp
namespace UltraCanvas {
    class UltraCanvasSVGElement : public UltraCanvasUIElement {
        // ... implementation
    };
}
```

## Key Features

- **File Loading**: Load SVG content from files or strings
- **Aspect Ratio Preservation**: Automatic scaling with aspect ratio preservation
- **Full SVG Support**: Paths, shapes, text, gradients, filters, transformations
- **Interactive**: Mouse event handling and hover effects
- **Performance Optimized**: GPU-accelerated rendering through render context
- **Cross-Platform**: Works on Windows, Linux, macOS

## Constructor

```cpp
UltraCanvasSVGElement(
    const std::string& identifier,  // Unique identifier for the element
    long id,                        // Numeric ID
    long x,                         // X position
    long y,                         // Y position
    long w,                         // Width
    long h                          // Height
);
```

### Parameters
- `identifier`: String identifier for the SVG element
- `id`: Unique numeric ID for element identification
- `x`, `y`: Position coordinates within parent container
- `w`, `h`: Dimensions of the SVG rendering area

## Public Methods

### LoadFromFile
```cpp
bool LoadFromFile(const std::string& filepath);
```
Loads SVG content from a file.

**Parameters:**
- `filepath`: Path to the SVG file

**Returns:**
- `true` if successfully loaded
- `false` if loading failed

**Example:**
```cpp
auto svgElement = std::make_shared<UltraCanvasSVGElement>("logo", 10, 10, 200, 200);
if (svgElement->LoadFromFile("media/logo.svg")) {
    // SVG loaded successfully
}
```

### LoadFromString
```cpp
bool LoadFromString(const std::string& svgContent);
```
Loads SVG content from a string.

**Parameters:**
- `svgContent`: SVG XML content as string

**Returns:**
- `true` if successfully parsed
- `false` if parsing failed

**Example:**
```cpp
std::string svgData = R"(
    <?xml version="1.0" encoding="UTF-8"?>
    <svg width="100" height="100" xmlns="http://www.w3.org/2000/svg">
        <circle cx="50" cy="50" r="40" fill="blue"/>
    </svg>
)";
svgElement->LoadFromString(svgData);
```

### Render
```cpp
void Render(IRenderContext* ctx) override;
```
Renders the SVG content to the screen. Called automatically by the framework during the render cycle.

### SetScale / GetScale
```cpp
void SetScale(float scale);
float GetScale() const;
```
Controls the scaling factor for the SVG content.

**Parameters:**
- `scale`: Scaling factor (1.0 = original size, 2.0 = double size)

**Example:**
```cpp
svgElement->SetScale(1.5f); // Scale to 150%
```

### SetPreserveAspectRatio / GetPreserveAspectRatio
```cpp
void SetPreserveAspectRatio(bool preserve);
bool GetPreserveAspectRatio() const;
```
Controls whether aspect ratio is preserved when scaling.

**Parameters:**
- `preserve`: `true` to maintain aspect ratio, `false` to stretch

### GetDocument
```cpp
const SVGDocument* GetDocument();
```
Returns pointer to the underlying SVG document for advanced manipulation.

## Supported SVG Elements

### Basic Shapes
- `<rect>` - Rectangles with optional rounded corners
- `<circle>` - Circles
- `<ellipse>` - Ellipses
- `<line>` - Lines
- `<polyline>` - Connected line segments
- `<polygon>` - Closed polygons

### Complex Elements
- `<path>` - Complex paths with all SVG path commands
- `<g>` - Groups for organizing elements
- `<text>` - Text rendering with font support
- `<image>` - Embedded images
- `<use>` - Element references

### Styling & Effects
- `<linearGradient>` - Linear color gradients
- `<radialGradient>` - Radial color gradients
- `<filter>` - Filter effects (blur, etc.)
- `<clipPath>` - Clipping paths
- `<defs>` - Reusable definitions

## Supported Attributes

### Style Attributes
- `fill` - Fill color or gradient reference
- `stroke` - Stroke color or gradient reference
- `stroke-width` - Width of strokes
- `opacity` - Element opacity (0-1)
- `fill-opacity` - Fill opacity
- `stroke-opacity` - Stroke opacity

### Transform Attributes
- `transform` - Transformations (translate, rotate, scale, skew)

### Text Attributes
- `font-family` - Font family name
- `font-size` - Font size
- `font-weight` - Font weight (bold, normal, etc.)
- `text-anchor` - Text crossAlignment

## Event Handling

The SVG element supports standard UltraCanvas event handling:

```cpp
svgElement->SetEventCallback([](const UCEvent& event) {
    switch (event.type) {
        case UCEventType::MouseUp:
            // Handle click
            return true;
        case UCEventType::MouseEnter:
            // Handle hover enter
            return true;
        case UCEventType::MouseLeave:
            // Handle hover leave
            return true;
        default:
            return false;
    }
});
```

## Usage Examples

### Basic SVG Loading and Display
```cpp
// Create container for SVG
auto container = std::make_shared<UltraCanvasContainer>("svgContainer", 20, 20, 300, 300);

// Create SVG element
auto svgElement = std::make_shared<UltraCanvasSVGElement>(
    "mySVG",
    101,
    10, 10,  // Position within container
    280, 280 // Size
);

// Load SVG file
svgElement->LoadFromFile("icons/app-icon.svg");
svgElement->SetPreserveAspectRatio(true);

// Add to container
container->AddChild(svgElement);
```

### Interactive SVG with Hover Effects
```cpp
class InteractiveSVG {
private:
    std::shared_ptr<UltraCanvasSVGElement> svg;
    std::shared_ptr<UltraCanvasContainer> container;

public:
    void Setup() {
        container = std::make_shared<UltraCanvasContainer>("svgBox", 50, 50, 200, 200);
        
        auto style = container->GetContainerStyle();
        style.borderWidth = 2;
        style.borderColor = Color(180, 180, 180, 255);
        container->SetContainerStyle(style);

        svg = std::make_shared<UltraCanvasSVGElement>("icon", 10, 10, 180, 180);
        svg->LoadFromFile("media/icon.svg");

        svg->SetEventCallback([this](const UCEvent& event) {
            return HandleSVGEvent(event);
        });

        container->AddChild(svg);
    }

    bool HandleSVGEvent(const UCEvent& event) {
        auto style = container->GetContainerStyle();
        
        switch (event.type) {
            case UCEventType::MouseEnter:
                style.borderColor = Color(100, 149, 237, 255); // Highlight
                svg->SetScale(1.1f); // Slight zoom
                container->SetContainerStyle(style);
                return true;
                
            case UCEventType::MouseLeave:
                style.borderColor = Color(180, 180, 180, 255); // Normal
                svg->SetScale(1.0f); // Reset zoom
                container->SetContainerStyle(style);
                return true;
                
            case UCEventType::MouseUp:
                OnSVGClick();
                return true;
                
            default:
                return false;
        }
    }

    void OnSVGClick() {
        std::cout << "SVG clicked!" << std::endl;
    }
};
```

### Dynamic SVG Generation
```cpp
void CreateDynamicSVG() {
    auto svgElement = std::make_shared<UltraCanvasSVGElement>("dynamic", 0, 0, 400, 300);
    
    std::ostringstream svg;
    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg << "<svg width=\"400\" height=\"300\" xmlns=\"http://www.w3.org/2000/svg\">\n";
    
    // Dynamic background gradient
    svg << "  <defs>\n";
    svg << "    <linearGradient id=\"bg\" x1=\"0%\" y1=\"0%\" x2=\"100%\" y2=\"100%\">\n";
    svg << "      <stop offset=\"0%\" style=\"stop-color:#667eea\" />\n";
    svg << "      <stop offset=\"100%\" style=\"stop-color:#764ba2\" />\n";
    svg << "    </linearGradient>\n";
    svg << "  </defs>\n";
    
    svg << "  <rect width=\"400\" height=\"300\" fill=\"url(#bg)\"/>\n";
    
    // Add dynamic content
    for (int i = 0; i < 5; i++) {
        int x = 80 * i + 40;
        svg << "  <circle cx=\"" << x << "\" cy=\"150\" r=\"30\" ";
        svg << "fill=\"white\" opacity=\"0.8\"/>\n";
    }
    
    svg << "</svg>\n";
    
    svgElement->LoadFromString(svg.str());
}
```

### Gallery View with Multiple SVGs
```cpp
class SVGGallery {
private:
    std::vector<std::shared_ptr<UltraCanvasSVGElement>> svgElements;
    std::vector<std::string> svgFiles = {
        "media/icon1.svg",
        "media/icon2.svg",
        "media/icon3.svg",
        "media/icon4.svg"
    };

public:
    void CreateGallery(std::shared_ptr<UltraCanvasContainer> parent) {
        int row = 0, col = 0;
        const int itemSize = 100;
        const int spacing = 20;
        
        for (size_t i = 0; i < svgFiles.size(); i++) {
            auto container = std::make_shared<UltraCanvasContainer>(
                "galleryItem" + std::to_string(i),
                400 + i,
                col * (itemSize + spacing) + spacing,
                row * (itemSize + spacing) + spacing,
                itemSize,
                itemSize
            );
            
            auto svg = std::make_shared<UltraCanvasSVGElement>(
                "svg" + std::to_string(i),
                500 + i,
                5, 5,
                itemSize - 10,
                itemSize - 10
            );
            
            svg->LoadFromFile(svgFiles[i]);
            svg->SetPreserveAspectRatio(true);
            
            container->AddChild(svg);
            parent->AddChild(container);
            svgElements.push_back(svg);
            
            // Grid layout
            col++;
            if (col >= 4) {
                col = 0;
                row++;
            }
        }
    }
};
```

## Performance Considerations

1. **Caching**: SVG documents are parsed once and cached in memory
2. **GPU Acceleration**: Rendering uses hardware acceleration when available
3. **Aspect Ratio**: Preserving aspect ratio adds minimal overhead
4. **Complex Paths**: Very complex paths may impact performance
5. **Gradients/Filters**: Advanced effects use more GPU resources

## Troubleshooting

### Common Issues

#### SVG Not Displaying
- Verify file path is correct
- Check SVG file is valid XML
- Ensure element has non-zero width/height
- Confirm parent container is visible

#### Scaling Issues
- Use `SetPreserveAspectRatio(true)` for proportional scaling
- Check SVG viewBox attribute is properly set
- Verify container dimensions accommodate scaled content

#### Performance Problems
- Simplify complex paths in SVG editor
- Reduce number of gradient/filter effects
- Consider pre-rendering static SVGs to bitmaps

## Dependencies

- **TinyXML2**: XML parsing
- **UltraCanvas Core**: Base UI element functionality
- **Render Context**: Graphics rendering interface

## File Structure

```
UltraCanvas/
├── include/
│   └── Plugins/
│       └── SVG/
│           └── UltraCanvasSVGPlugin.h
├── Plugins/
│   └── SVG/
│       └── UltraCanvasSVGPlugin.cpp
└── Apps/
    └── DemoApp/
        └── UltraCanvasSVGExamples.cpp
```

## See Also

- [UltraCanvasUIElement](UltraCanvasUIElement.md) - Base class
- [UltraCanvasContainer](UltraCanvasContainer.md) - Container for SVG elements
- [SVGDocument](SVGDocument.md) - Internal SVG document representation
- [SVGElementRenderer](SVGElementRenderer.md) - SVG rendering engine
