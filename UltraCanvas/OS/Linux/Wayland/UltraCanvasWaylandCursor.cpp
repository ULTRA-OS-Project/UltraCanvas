// OS/Linux/Wayland/UltraCanvasWaylandCursor.cpp
// The mouse cursor on Wayland: a client draws its own. The standard shapes
// come from the cursor theme (XCURSOR_THEME / XCURSOR_SIZE, as every other
// toolkit reads them) through libwayland-cursor, at the scale of the output
// the window is on; a cursor from a picture is a PNG read by cairo into a
// shared-memory buffer of its own.
// Version: 0.1.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework

#include "UltraCanvasWaylandApplication.h"
#include "UltraCanvasWaylandWindow.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasPathUtf8.h"

#include <wayland-cursor.h>
#include <cairo/cairo.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <vector>

namespace UltraCanvas {

    namespace {
        // The theme's names for a shape, in the order to try: the freedesktop
        // name first, then the X name the older themes carry.
        std::vector<const char*> CursorNames(UCMouseCursor cursor) {
            switch (cursor) {
                case UCMouseCursor::Hand: return { "pointer", "hand2", "hand1" };
                case UCMouseCursor::Text: return { "text", "xterm" };
                case UCMouseCursor::Wait: return { "wait", "watch" };
                case UCMouseCursor::AppStarting: return { "progress", "left_ptr_watch", "watch" };
                case UCMouseCursor::Cross: return { "crosshair", "cross" };
                case UCMouseCursor::Help: return { "help", "question_arrow" };
                case UCMouseCursor::NotAllowed: return { "not-allowed", "crossed_circle", "X_cursor" };
                case UCMouseCursor::LookingGlass: return { "zoom-in", "crosshair" };
                case UCMouseCursor::SizeAll: return { "all-scroll", "fleur", "move" };
                case UCMouseCursor::SizeNS: return { "ns-resize", "sb_v_double_arrow", "v_double_arrow" };
                case UCMouseCursor::SizeWE: return { "ew-resize", "sb_h_double_arrow", "h_double_arrow" };
                case UCMouseCursor::SizeNWSE: return { "nwse-resize", "bd_double_arrow", "size_fdiag" };
                case UCMouseCursor::SizeNESW: return { "nesw-resize", "fd_double_arrow", "size_bdiag" };
                case UCMouseCursor::ContextMenu: return { "context-menu", "left_ptr" };
                default: return { "default", "left_ptr", "arrow" };
            }
        }

        int ThemeSize() {
            const char* size = std::getenv("XCURSOR_SIZE");
            if (size) {
                const int value = std::atoi(size);
                if (value > 0) return value;
            }
            return 24;
        }
    } // namespace

    wl_cursor_theme* UltraCanvasWaylandApplication::CursorThemeForScale(int scale) {
        if (scale < 1) scale = 1;
        auto found = cursorThemes.find(scale);
        if (found != cursorThemes.end()) return found->second;
        wl_cursor_theme* theme = shm ? wl_cursor_theme_load(std::getenv("XCURSOR_THEME"), ThemeSize() * scale, shm) : nullptr;
        if (!theme) {
            debugOutput << "UltraCanvas Wayland: no cursor theme could be loaded" << std::endl;
        }
        cursorThemes[scale] = theme;
        return theme;
    }

    UltraCanvasWaylandApplication::ImageCursor* UltraCanvasWaylandApplication::LoadImageCursor(
            const std::string& filename, int hotspotX, int hotspotY, int scale) {
        if (scale < 1) scale = 1;
        const std::string key = filename + "@" + std::to_string(scale);
        auto found = imageCursors.find(key);
        if (found != imageCursors.end()) return found->second.buffer ? &found->second : nullptr;
        ImageCursor& cursor = imageCursors[key];   // kept even when it fails, so it is tried once
        if (!shm) return nullptr;

        // cairo reads PNG; any other picture leaves the standard cursor up.
        cairo_surface_t* image = cairo_image_surface_create_from_png(filename.c_str());
        if (!image || cairo_surface_status(image) != CAIRO_STATUS_SUCCESS) {
            debugOutput << "UltraCanvas Wayland: cursor picture " << filename << " could not be read (PNG only)" << std::endl;
            if (image) cairo_surface_destroy(image);
            return nullptr;
        }
        const int width = cairo_image_surface_get_width(image) * scale;
        const int height = cairo_image_surface_get_height(image) * scale;
        const int stride = width * 4;
        const size_t size = static_cast<size_t>(stride) * height;
        int fd = memfd_create("ultracanvas-cursor", MFD_CLOEXEC);
        if (fd < 0 || ftruncate(fd, static_cast<off_t>(size)) != 0) {
            if (fd >= 0) close(fd);
            cairo_surface_destroy(image);
            return nullptr;
        }
        void* map = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (map == MAP_FAILED) {
            close(fd);
            cairo_surface_destroy(image);
            return nullptr;
        }
        // Draw the picture, scaled, straight into the shared memory
        // (premultiplied ARGB, which is WL_SHM_FORMAT_ARGB8888).
        cairo_surface_t* target = cairo_image_surface_create_for_data(
                static_cast<unsigned char*>(map), CAIRO_FORMAT_ARGB32, width, height, stride);
        cairo_t* cr = cairo_create(target);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, image, 0, 0);
        cairo_paint(cr);
        cairo_destroy(cr);
        cairo_surface_flush(target);
        cairo_surface_destroy(target);
        cairo_surface_destroy(image);

        wl_shm_pool* pool = wl_shm_create_pool(shm, fd, static_cast<int32_t>(size));
        cursor.buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);
        close(fd);
        cursor.map = map;
        cursor.mapSize = size;
        cursor.width = width;
        cursor.height = height;
        cursor.hotspotX = hotspotX * scale;
        cursor.hotspotY = hotspotY * scale;
        cursor.scale = scale;
        return cursor.buffer ? &cursor : nullptr;
    }

    void UltraCanvasWaylandApplication::FreeImageCursors() {
        for (auto& entry : imageCursors) {
            if (entry.second.buffer) wl_buffer_destroy(entry.second.buffer);
            if (entry.second.map) munmap(entry.second.map, entry.second.mapSize);
        }
        imageCursors.clear();
    }

    bool UltraCanvasWaylandApplication::ApplyCursor(UltraCanvasWaylandWindow* window) {
        if (!pointer || !cursorSurface) return false;
        if (pointerCursor == UCMouseCursor::NoCursor) {
            wl_pointer_set_cursor(pointer, pointerEnterSerial, nullptr, 0, 0);
            return true;
        }
        const int scale = window ? window->GetOutputScale() : 1;
        if (!pointerCursorFile.empty()) {
            if (ImageCursor* image = LoadImageCursor(pointerCursorFile, pointerCursorHotspotX, pointerCursorHotspotY, scale)) {
                wl_surface_set_buffer_scale(cursorSurface, scale);
                wl_surface_attach(cursorSurface, image->buffer, 0, 0);
                wl_surface_damage_buffer(cursorSurface, 0, 0, image->width, image->height);
                wl_surface_commit(cursorSurface);
                wl_pointer_set_cursor(pointer, pointerEnterSerial, cursorSurface,
                                      image->hotspotX / scale, image->hotspotY / scale);
                return true;
            }
        }
        wl_cursor_theme* theme = CursorThemeForScale(scale);
        if (!theme) return false;
        wl_cursor* cursor = nullptr;
        for (const char* name : CursorNames(pointerCursor)) {
            cursor = wl_cursor_theme_get_cursor(theme, name);
            if (cursor) break;
        }
        if (!cursor) {
            for (const char* name : CursorNames(UCMouseCursor::Default)) {
                cursor = wl_cursor_theme_get_cursor(theme, name);
                if (cursor) break;
            }
        }
        if (!cursor || cursor->image_count == 0) return false;
        // The first frame of an animated cursor; the frames are not played.
        wl_cursor_image* image = cursor->images[0];
        wl_buffer* buffer = wl_cursor_image_get_buffer(image);
        if (!buffer) return false;
        wl_surface_set_buffer_scale(cursorSurface, scale);
        wl_surface_attach(cursorSurface, buffer, 0, 0);
        wl_surface_damage_buffer(cursorSurface, 0, 0, static_cast<int32_t>(image->width), static_cast<int32_t>(image->height));
        wl_surface_commit(cursorSurface);
        wl_pointer_set_cursor(pointer, pointerEnterSerial, cursorSurface,
                              static_cast<int32_t>(image->hotspot_x) / scale, static_cast<int32_t>(image->hotspot_y) / scale);
        return true;
    }

    bool UltraCanvasWaylandApplication::SelectMouseCursorNative(UltraCanvasWindowBase* win, UCMouseCursor cur) {
        pointerCursor = cur;
        pointerCursorFile.clear();
        if (win && win != pointerWindow) return true;   // put up when the pointer enters it
        return ApplyCursor(pointerWindow);
    }

    bool UltraCanvasWaylandApplication::SelectMouseCursorNative(UltraCanvasWindowBase* win, UCMouseCursor cur,
                                                                 const char* filename, int hotspotX, int hotspotY) {
        pointerCursor = cur;
        pointerCursorFile = filename ? filename : "";
        pointerCursorHotspotX = hotspotX;
        pointerCursorHotspotY = hotspotY;
        if (win && win != pointerWindow) return true;
        return ApplyCursor(pointerWindow);
    }

} // namespace UltraCanvas
