# UltraCanvas

**A Modern Cross-Platform C++20 UI & Rendering Framework**

UltraCanvas is a modular framework designed to simplify the development of rich graphical applications across Windows, Linux, macOS, and UltraOS. It provides a unified API for windowing, rendering, and event handling while keeping platform-specific code cleanly separated.

----------

## Core Features

**Unified Cross-Platform API**

-   Single codebase targeting multiple operating systems
-   Platform-specific implementations isolated in dedicated folders
-   Automatic handling of redraw events and system inputs

**Comprehensive UI Toolkit**

-   Buttons (standard, split buttons with 3-section support)
-   Text inputs, text areas, password fields with strength meter
-   Labels, checkboxes, radio buttons
-   Dropdown menus, combo boxes
-   Sliders, progress bars, spinners
-   Tree views, tabbed containers
-   Modal dialogs, tooltips, toasts
-   Color picker, date picker
-   Segmented controls, chips
-   Advanced layout containers (BoxLayout, GridLayout, FlexLayout)
-   Full Unicode support with styled text rendering

**Data Visualization**

-   Line, bar, area, and scatter charts
-   Financial charts with candlestick support
-   Waterfall and Sankey diagrams
-   Diverging bar charts
-   Population/demographic charts
-   Fully customizable styling

**Flexible Rendering Engine**

-   2D primitives: lines, circles, rectangles, polygons
-   Vector graphics with SVG parsing and transformations
-   Gradients (linear, radial, conic)
-   Path filling with multiple modes
-   Bitmap formats: PNG, JPG, BMP, GIF, AVIF, WebP, QOI
-   Double buffering for smooth rendering

**Extensible Plugin System**

-   Dynamic loading for file types and media renderers
-   CDR (CorelDRAW) format support via librevenge
-   XAR (XARA) vector format support
-   PDF support (poppler-based, optional)
-   SVG plugin
-   Open architecture for custom renderers and input backends

**Advanced Features**

-   Text editor with syntax highlighting tokenizer
-   Markdown rendering support
-   Audio/video element support
-   Cross-platform clipboard management
-   Mouse cursor customization
-   Event system with bubbling and capture phases

----------

## Project Structure

```
UltraCanvas/
├── Apps/                 # Demo applications and examples
├── Docs/                 # Component documentation
├── UltraCanvas/
│   ├── include/          # Public header declarations
│   ├── core/             # Platform-independent implementations
│   ├── OS/Linux/         # Linux-specific code (X11, Cairo)
│   ├── OS/MSWindows/     # Windows-specific code (Win32, Direct2D)
│   ├── OS/MacOS/         # macOS-specific code (Cocoa, Core Graphics)
│   ├── OS/BSD/           # BSD-specific code
│   ├── OS/WASM/          # WebAssembly browser support
│   └── Plugins/          # Extensible plugin modules
├── 3rdparty/             # Vendored dependencies
└── media/                # Assets and resources
```

----------

## Technical Stack

-   **Language:** C++20 (with Objective-C++ for macOS)
-   **Build System:** CMake (3.16+)
-   **Graphics:** Cairo, OpenGL, Direct2D, Core Graphics
-   **Text Rendering:** FreeType, Pango, HarfBuzz
-   **Image Processing:** libvips
-   **Utilities:** glib-2.0, tinyxml2, jsoncpp, fmt

----------

## Building

**Prerequisites**

Install the required dependencies for your platform:

```bash
# Ubuntu/Debian
sudo apt install build-essential cmake libcairo2-dev libpango1.0-dev \
    libfreetype6-dev libvips-dev libjsoncpp-dev libharfbuzz-dev

# macOS
brew install cmake cairo pango freetype vips jsoncpp harfbuzz
```

**Build**

```bash
mkdir build && cd build
cmake ..
make
```

----------

## Design Principles

-   PascalCase naming convention for all identifiers
-   Clear separation between interface and platform implementations
-   GPU acceleration with double buffering and resource caching
-   Modular architecture enabling incremental feature adoption
-   Event bubbling with capture phases
-   Layout dirty flags for efficient rendering

----------

## Planned Features

The following features are planned for future releases:

-   **UltraOS support** - Native support for UltraOS platform
-   **Vulkan backend** - Additional GPU rendering backend
-   **3D model support** - Loading and rendering .3ds, .3dm formats
-   **SmartHome drivers** - Device drivers for Matter, Thread, and Zigbee protocols
-   **Wayland support** - Native Wayland windowing on Linux

----------

## License

MIT License - Copyright 2025 Cloverleaf UG (contributions welcome)

----------

_Developed by Cloverleaf UG_
