// Apps/UltraSnip/ui/UltraSnipCapture.cpp
// See the header.
// Version: 0.3.0 - RectBetween, ClipRect, CropToRgba and TopmostWindowAt moved to the framework
//                  (core/UltraCanvasScreenAreaPicker.cpp)
// Version: 0.2.0 - the error texts go through Tr (UltraLocalisation)
// Version: 0.1.1 - SnipFilePath
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraSnipCapture.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <system_error>

using namespace UltraCanvas;

namespace UltraSnip {

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

std::string SnipFilePath(const std::string& defaultPath, const std::string& folder, const std::string& extension) {
    namespace fs = std::filesystem;
    fs::path path = PathFromUtf8(defaultPath);
    std::string ext = extension;
    if (!ext.empty() && ext[0] != '.') ext = "." + ext;
    if (!ext.empty()) path.replace_extension(PathFromUtf8(ext));
    if (!folder.empty()) path = PathFromUtf8(folder) / path.filename();
    return PathToUtf8(path);
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
        error = TrF("The folder {0} could not be created: {1}", PathToUtf8(target.parent_path()), ec.message());
        return false;
    }
    std::FILE* file = OpenFileUtf8(path, "wb");
    if (!file) {
        error = Tr("The file could not be created.");
        return false;
    }
    const bool ok = bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    const bool closed = std::fclose(file) == 0;
    if (!ok || !closed) {
        error = Tr("The file could not be written.");
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
        error = TrF("The folder {0} could not be created: {1}", PathToUtf8(target.parent_path()), ec.message());
        return false;
    }
    fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        error = TrF("The file could not be copied: {0}", ec.message());
        return false;
    }
    return true;
}

} // namespace UltraSnip
