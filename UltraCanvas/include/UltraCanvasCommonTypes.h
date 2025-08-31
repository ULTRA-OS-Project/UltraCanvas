// UltraCanvasCommonTypes.h
// Unified common types and structures for UltraCanvas Framework
// Version: 2.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <cmath>
#include <vector>
#include <string>

namespace UltraCanvas {

// ===== COMMON GEOMETRIC TYPES =====
template <typename T>
struct Point2D {
    T x, y;

    Point2D(T px = 0, T py = 0) : x(px), y(py) {}

    // Operators
    Point2D operator+(const Point2D& other) const { return Point2D(x + other.x, y + other.y); }
    Point2D operator-(const Point2D& other) const { return Point2D(x - other.x, y - other.y); }
    Point2D operator*(float scalar) const { return Point2D(x * scalar, y * scalar); }
    Point2D& operator+=(const Point2D& other) { x += other.x; y += other.y; return *this; }
    Point2D& operator-=(const Point2D& other) { x -= other.x; y -= other.y; return *this; }
    Point2D& operator*=(float scalar) { x *= scalar; y *= scalar; return *this; }

    // Utility methods
    T Distance(const Point2D& other) const {
        T dx = x - other.x, dy = y - other.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    T DistanceSquared(const Point2D& other) const {
        float dx = x - other.x, dy = y - other.y;
        return dx * dx + dy * dy;
    }

    T Length() const {
        return std::sqrt(x * x + y * y);
    }

    Point2D Normalized() const {
        T len = Length();
        return len > 0 ? Point2D(x / len, y / len) : Point2D(0, 0);
    }
};

typedef struct Point2D<float> Point2Df;
typedef struct Point2D<int> Point2Di;
typedef struct Point2D<long> Point2Dl;

template <typename T>
struct Rect2D {
    T x, y, width, height;

    Rect2D(T px = 0, T py = 0, T w = 0, T h = 0)
        : x(px), y(py), width(w), height(h) {}

    // Convenience methods
    T Left() const { return x; }
    T Right() const { return x + width; }
    T Top() const { return y; }
    T Bottom() const { return y + height; }
    Point2D<T> TopLeft() const { return Point2D<T>(x, y); }
    Point2D<T> TopRight() const { return Point2D<T>(x + width, y); }
    Point2D<T> BottomLeft() const { return Point2D<T>(x, y + height); }
    Point2D<T> BottomRight() const { return Point2D<T>(x + width, y + height); }
    Point2D<T> Center() const { return Point2D<T>(x + width / 2, y + height / 2); }

    bool Contains(const Point2D<T>& point) const {
        return point.x >= x && point.x <= x + width && point.y >= y && point.y <= y + height;
    }

    bool Contains(T px, T py) const {
        return px >= x && px <= x + width && py >= y && py <= y + height;
    }

    bool Intersects(const Rect2D& other) const {
        return !(other.x > x + width || other.x + other.width < x ||
                 other.y > y + height || other.y + other.height < y);
    }

    Rect2D Intersection(const Rect2D& other) const {
        T left = std::max(x, other.x);
        T top = std::max(y, other.y);
        T right = std::min(x + width, other.x + other.width);
        T bottom = std::min(y + height, other.y + other.height);

        if (left < right && top < bottom) {
            return Rect2D(left, top, right - left, bottom - top);
        }
        return Rect2D(0, 0, 0, 0); // No intersection
    }

    Rect2D Union(const Rect2D& other) const {
        if (width == 0 && height == 0) return other;
        if (other.width == 0 && other.height == 0) return *this;

        T left = std::min(x, other.x);
        T top = std::min(y, other.y);
        T right = std::max(x + width, other.x + other.width);
        T bottom = std::max(y + height, other.y + other.height);

        return Rect2D(left, top, right - left, bottom - top);
    }
};

typedef struct Rect2D<float> Rect2Df;
typedef struct Rect2D<int> Rect2Di;
typedef struct Rect2D<long> Rect2Dl;

// ===== UNIFIED COLOR SYSTEM =====

struct Color {
    uint8_t r, g, b, a;
    
    // Constructors
    Color(uint8_t red = 0, uint8_t green = 0, uint8_t blue = 0, uint8_t alpha = 255) 
        : r(red), g(green), b(blue), a(alpha) {}
    
    // Conversion methods
    uint32_t ToARGB() const { 
        return (static_cast<uint32_t>(a) << 24) | 
               (static_cast<uint32_t>(r) << 16) | 
               (static_cast<uint32_t>(g) << 8) | 
               static_cast<uint32_t>(b);
    }
    
    uint32_t ToRGBA() const {
        return (static_cast<uint32_t>(r) << 24) | 
               (static_cast<uint32_t>(g) << 16) | 
               (static_cast<uint32_t>(b) << 8) | 
               static_cast<uint32_t>(a);
    }
    
    static Color FromARGB(uint32_t argb) {
        return Color(
            static_cast<uint8_t>((argb >> 16) & 0xFF), // R
            static_cast<uint8_t>((argb >> 8) & 0xFF),  // G
            static_cast<uint8_t>(argb & 0xFF),         // B
            static_cast<uint8_t>((argb >> 24) & 0xFF)  // A
        );
    }
    
    static Color FromRGBA(uint32_t rgba) {
        return Color(
            static_cast<uint8_t>((rgba >> 24) & 0xFF), // R
            static_cast<uint8_t>((rgba >> 16) & 0xFF), // G
            static_cast<uint8_t>((rgba >> 8) & 0xFF),  // B
            static_cast<uint8_t>(rgba & 0xFF)          // A
        );
    }
    
    // Utility methods
    Color WithAlpha(uint8_t alpha) const {
        return Color(r, g, b, alpha);
    }
    
    Color Blend(const Color& other, float factor) const {
        factor = std::max(0.0f, std::min(1.0f, factor));
        return Color(
            static_cast<uint8_t>(r + (other.r - r) * factor),
            static_cast<uint8_t>(g + (other.g - g) * factor),
            static_cast<uint8_t>(b + (other.b - b) * factor),
            static_cast<uint8_t>(a + (other.a - a) * factor)
        );
    }
    
    // Operators
    bool operator==(const Color& other) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }
    
    bool operator!=(const Color& other) const {
        return !(*this == other);
    }
};

// ===== PREDEFINED COLORS =====
namespace Colors {
    const Color Transparent(0, 0, 0, 0);
    const Color White(255, 255, 255, 255);
    const Color Black(0, 0, 0, 255);
    const Color Red(255, 0, 0, 255);
    const Color Green(0, 255, 0, 255);
    const Color Blue(0, 0, 255, 255);
    const Color Yellow(255, 255, 0, 255);
    const Color Cyan(0, 255, 255, 255);
    const Color Magenta(255, 0, 255, 255);
    const Color Gray(128, 128, 128, 255);
    const Color LightGray(192, 192, 192, 255);
    const Color DarkGray(64, 64, 64, 255);
    
    // UI Colors
    const Color WindowBackground(240, 240, 240, 255);
    const Color ButtonFace(225, 225, 225, 255);
    const Color ButtonHighlight(255, 255, 255, 255);
    const Color ButtonShadow(160, 160, 160, 255);
    const Color Selection(0, 120, 215, 255);
    const Color SelectionHover(229, 243, 255, 255);
    const Color TextDefault(0, 0, 0, 255);
    const Color TextDisabled(109, 109, 109, 255);
}

// ===== COLOR UTILITY FUNCTIONS =====
inline Color RGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return Color(r, g, b, a);
}

inline Color RGB(uint8_t r, uint8_t g, uint8_t b) {
    return Color(r, g, b, 255);
}

inline Color HSV(float h, float s, float v, uint8_t a = 255) {
    // HSV to RGB conversion
    float c = v * s;
    float x = c * (1 - std::abs(std::fmod(h / 60.0f, 2) - 1));
    float m = v - c;
    
    float r, g, b;
    
    if (h >= 0 && h < 60) {
        r = c; g = x; b = 0;
    } else if (h >= 60 && h < 120) {
        r = x; g = c; b = 0;
    } else if (h >= 120 && h < 180) {
        r = 0; g = c; b = x;
    } else if (h >= 180 && h < 240) {
        r = 0; g = x; b = c;
    } else if (h >= 240 && h < 300) {
        r = x; g = 0; b = c;
    } else {
        r = c; g = 0; b = x;
    }
    
    return Color(
        static_cast<uint8_t>((r + m) * 255),
        static_cast<uint8_t>((g + m) * 255),
        static_cast<uint8_t>((b + m) * 255),
        a
    );
}

// ===== MOUSE POINTER TYPES =====
enum class MousePointer {
    Default = 0,        // Standard arrow
    NoCursor = 1,          // No cursor (hidden)
    Hand = 2,          // Hand/pointing finger
    Text = 3,          // Text I-beam
    Wait = 4,          // Hourglass/spinner
    Cross = 5,         // Crosshair
    Help = 6,          // Help/question mark
    NotAllowed = 7,    // Not allowed/prohibited
    SizeAll = 8,       // Four-directional resize
    SizeNS = 9,        // North-South resize
    SizeWE = 10,       // West-East resize
    SizeNWSE = 11,     // Northwest-Southeast resize
    SizeNESW = 12,     // Northeast-Southwest resize
    Custom = 99        // Custom cursor
};

// ===== MOUSE CONTROL TYPES =====
enum class MouseControls {
    NoMouse = 1,          // No mouse interaction
    Input = 2,         // Input controls (text fields, etc.)
    Button = 3,        // Button controls
    Object2D = 4,      // 2D object manipulation
    Object3D = 5,      // 3D object manipulation
    Custom = 99        // Custom interaction
};

// ===== COMMON ENUMS =====

enum class Alignment {
    Left,
    Center, 
    Right,
    Top,
    Bottom,
    Stretch,
    Justify
};

enum class Direction {
    Horizontal,
    Vertical
};

enum class SizeMode {
    Fixed,      // Fixed size in pixels
    Percentage, // Percentage of container
    Auto,       // Size based on content
    Fill        // Fill remaining space
};

// ===== UTILITY FUNCTIONS =====

// Safe casting with bounds checking
template<typename T>
T Clamp(T value, T min, T max) {
    return std::max(min, std::min(max, value));
}

// Linear interpolation
template<typename T>
T Lerp(T a, T b, float t) {
    return a + (b - a) * Clamp(t, 0.0f, 1.0f);
}

// Distance between two points
inline float Distance(const Point2Df& a, const Point2Df& b) {
    return a.Distance(b);
}

inline int Distance(const Point2Di& a, const Point2Di& b) {
    return a.Distance(b);
}

} // namespace UltraCanvas