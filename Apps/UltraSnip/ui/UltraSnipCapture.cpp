// Apps/UltraSnip/ui/UltraSnipCapture.cpp
// See the header.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraSnipCapture.h"

#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <system_error>

using namespace UltraCanvas;

namespace UltraSnip {

Rect2Di RectBetween(const Point2Di& a, const Point2Di& b) {
    const int left = std::min(a.x, b.x);
    const int top = std::min(a.y, b.y);
    const int right = std::max(a.x, b.x);
    const int bottom = std::max(a.y, b.y);
    return Rect2Di(left, top, right - left + 1, bottom - top + 1);
}

Rect2Di ClipRect(const Rect2Di& rect, const Rect2Di& bounds) {
    const int left = std::max(rect.x, bounds.x);
    const int top = std::max(rect.y, bounds.y);
    const int right = std::min(rect.x + rect.width, bounds.x + bounds.width);
    const int bottom = std::min(rect.y + rect.height, bounds.y + bounds.height);
    if (right <= left || bottom <= top) return Rect2Di(left, top, 0, 0);
    return Rect2Di(left, top, right - left, bottom - top);
}

bool CropToRgba(const DesktopScreenImage& shot, const Rect2Di& area, ClipboardDib::RgbaImage& out, Rect2Di* taken) {
    out = ClipboardDib::RgbaImage();
    if (!shot.IsValid()) return false;
    const Rect2Di inside = ClipRect(area, Rect2Di(shot.x, shot.y, shot.width, shot.height));
    if (inside.width <= 0 || inside.height <= 0) return false;
    out.width = inside.width;
    out.height = inside.height;
    out.pixels.resize(static_cast<size_t>(inside.width) * inside.height * 4);
    for (int row = 0; row < inside.height; ++row) {
        const uint8_t* src = shot.pixels.data() +
                             static_cast<size_t>(inside.y - shot.y + row) * static_cast<size_t>(shot.stride) +
                             static_cast<size_t>(inside.x - shot.x) * 4;
        uint8_t* dst = out.pixels.data() + static_cast<size_t>(row) * inside.width * 4;
        for (int col = 0; col < inside.width; ++col, src += 4, dst += 4) {
            dst[0] = src[2];    // the capture is B G R x
            dst[1] = src[1];
            dst[2] = src[0];
            dst[3] = 255;
        }
    }
    if (taken) *taken = inside;
    return true;
}

int TopmostWindowAt(const std::vector<SnipWindowBounds>& windows, const Point2Di& point) {
    for (int i = static_cast<int>(windows.size()) - 1; i >= 0; --i) {
        const Rect2Di& r = windows[static_cast<size_t>(i)].bounds;
        if (r.width <= 0 || r.height <= 0) continue;
        if (point.x >= r.x && point.x < r.x + r.width && point.y >= r.y && point.y < r.y + r.height) return i;
    }
    return -1;
}

Rect2Di PlaceBarOutside(const Rect2Di& region, const Rect2Di& screen, int barWidth, int barHeight, int gap) {
    int x = region.x + (region.width - barWidth) / 2;
    x = std::clamp(x, screen.x, std::max(screen.x, screen.x + screen.width - barWidth));
    int y = region.y - gap - barHeight;
    if (y < screen.y) {
        y = region.y + region.height + gap;
        if (y + barHeight > screen.y + screen.height) y = screen.y + gap;
    }
    return Rect2Di(x, y, barWidth, barHeight);
}

std::string FormatElapsed(double seconds) {
    const long long total = std::max(0LL, static_cast<long long>(std::floor(seconds)));
    const long long h = total / 3600, m = (total % 3600) / 60, s = total % 60;
    char buffer[32];
    if (h > 0) std::snprintf(buffer, sizeof buffer, "%lld:%02lld:%02lld", h, m, s);
    else std::snprintf(buffer, sizeof buffer, "%lld:%02lld", m, s);
    return buffer;
}

bool WriteFileBytes(const std::string& path, const std::vector<uint8_t>& bytes, std::string& error) {
    namespace fs = std::filesystem;
    const fs::path target = PathFromUtf8(path);
    std::error_code ec;
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    if (ec) {
        error = "The folder " + PathToUtf8(target.parent_path()) + " could not be created: " + ec.message();
        return false;
    }
    std::FILE* file = OpenFileUtf8(path, "wb");
    if (!file) {
        error = "The file could not be created.";
        return false;
    }
    const bool ok = bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    const bool closed = std::fclose(file) == 0;
    if (!ok || !closed) {
        error = "The file could not be written.";
        std::error_code ignored;
        fs::remove(target, ignored);
        return false;
    }
    return true;
}

bool CopyFileTo(const std::string& from, const std::string& to, std::string& error) {
    namespace fs = std::filesystem;
    const fs::path source = PathFromUtf8(from);
    const fs::path target = PathFromUtf8(to);
    std::error_code ec;
    if (fs::equivalent(source, target, ec)) return true;
    ec.clear();
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    if (ec) {
        error = "The folder " + PathToUtf8(target.parent_path()) + " could not be created: " + ec.message();
        return false;
    }
    fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        error = "The file could not be copied: " + ec.message();
        return false;
    }
    return true;
}

} // namespace UltraSnip
