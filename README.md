# UltraCanvas

**A Modern Cross-Platform C++20 UI & Rendering Framework**

UltraCanvas is a modular framework designed to simplify the development of rich graphical applications across Windows, Linux, macOS, and UltraOS. It provides a unified API for windowing, rendering, and event handling while keeping platform-specific code cleanly separated.

---------

## Core Features

**Unified Cross-Platform API**

-   Single codebase targeting multiple operating systems
-   Platform-specific implementations isolated in dedicated folders
-   Automatic handling of redraw events and system inputs

**Comprehensive UI Toolkit**

-   Buttons, sliders, input fields, tabs, menus, tree views
-   Advanced layout containers and styled text rendering
-   Full Unicode support with FreeType-based glyph rendering

**Flexible Rendering Engine**

-   2D primitives: lines, circles, rectangles, polygons
-   Vector graphics with SVG parsing and transformations
-   3D support via OpenGL/Vulkan backends
-   Bitmap formats: PNG, JPG, BMP, GIF, AVIF, WebP

**Extensible Plugin System**

-   Dynamic loading for file types and media renderers
-   Support for PDF, SVG, 3D models (.3ds, .3dm), audio, and video
-   Open architecture for custom renderers and input backends

----------

## Project Structure

```
         UltraCanvas/
         ├── include/          # Header declarations
         ├── core/             # Platform-independent implementations
         ├── OS/Linux/         # Linux-specific code (X11/Wayland, Cairo)
         ├── OS/MSWindows/     # Windows-specific code (Win32, Direct2D)
         ├── OS/MacOS/         # macOS-specific code (Cocoa, Core Graphics)
         └── OS/UltraOS/       # UltraOS-specific code
```

----------

## Technical Stack

-   **Language:** C++20
-   **Graphics:** OpenGL, Vulkan, Cairo, Direct2D, Core Graphics
-   **Text Rendering:** FreeType with advanced layout and kerning
-   **Build System:** CMake

----------

## Design Principles

-   PascalCase naming convention for all identifiers
-   Clear separation between interface and platform implementations
-   GPU acceleration with double buffering and resource caching
-   Modular architecture enabling incremental feature adoption

----------

## License

Open Source — Contributions Welcome

----------

_Developed by Cloverleaf UG_