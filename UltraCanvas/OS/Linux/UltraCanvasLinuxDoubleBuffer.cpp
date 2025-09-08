// UltraCanvasDoubleBuffer.cpp
// Double buffer rendering implementation for cross-platform compatibility
// Version: 1.0.0
// Last Modified: 2025-09-08
// Author: UltraCanvas Framework

#include "UltraCanvasLinuxRenderContext.h"
#include <iostream>
#include <cstring>
#include <cairo/cairo.h>

namespace UltraCanvas {

// ===== LINUX CAIRO DOUBLE BUFFER IMPLEMENTATION =====

    LinuxCairoDoubleBuffer::LinuxCairoDoubleBuffer()
            : windowSurface(nullptr)
            , windowContext(nullptr)
            , stagingSurface(nullptr)
            , stagingContext(nullptr)
            , bufferWidth(0)
            , bufferHeight(0)
            , isValid(false) {
    }

    LinuxCairoDoubleBuffer::~LinuxCairoDoubleBuffer() {
        Cleanup();
    }

    bool LinuxCairoDoubleBuffer::Initialize(int width, int height, void* windowSurf) {
        std::lock_guard<std::mutex> lock(bufferMutex);

        if (width <= 0 || height <= 0 || !windowSurf) {
            std::cerr << "LinuxCairoDoubleBuffer: Invalid parameters" << std::endl;
            return false;
        }

        windowSurface = static_cast<cairo_surface_t*>(windowSurf);
        windowContext = cairo_create(windowSurface);

        if (!windowContext) {
            std::cerr << "LinuxCairoDoubleBuffer: Failed to create window context" << std::endl;
            throw std::runtime_error("LinuxCairoDoubleBuffer: Failed to create window context");
        }

        bufferWidth = width;
        bufferHeight = height;

        if (!CreateStagingSurface()) {
            cairo_destroy(windowContext);
            windowContext = nullptr;
            return false;
        }

        isValid = true;
        std::cout << "LinuxCairoDoubleBuffer: Initialized " << width << "x" << height << std::endl;
        return true;
    }

    bool LinuxCairoDoubleBuffer::CreateStagingSurface() {
        // Create image surface for staging (back buffer)
        stagingSurface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bufferWidth, bufferHeight);

        if (cairo_surface_status(stagingSurface) != CAIRO_STATUS_SUCCESS) {
            std::cerr << "LinuxCairoDoubleBuffer: Failed to create staging surface" << std::endl;
            return false;
        }

        stagingContext = cairo_create(stagingSurface);

        if (cairo_status(stagingContext) != CAIRO_STATUS_SUCCESS) {
            std::cerr << "LinuxCairoDoubleBuffer: Failed to create staging context" << std::endl;
            cairo_surface_destroy(stagingSurface);
            stagingSurface = nullptr;
            return false;
        }

        // Clear staging surface to transparent
        cairo_set_operator(stagingContext, CAIRO_OPERATOR_CLEAR);
        cairo_paint(stagingContext);
        cairo_set_operator(stagingContext, CAIRO_OPERATOR_OVER);

        return true;
    }

    void LinuxCairoDoubleBuffer::DestroyStagingSurface() {
        if (stagingContext) {
            cairo_destroy(stagingContext);
            stagingContext = nullptr;
        }

        if (stagingSurface) {
            cairo_surface_destroy(stagingSurface);
            stagingSurface = nullptr;
        }
    }

    bool LinuxCairoDoubleBuffer::Resize(int newWidth, int newHeight) {
        std::lock_guard<std::mutex> lock(bufferMutex);

        if (newWidth <= 0 || newHeight <= 0) {
            return false;
        }

        if (newWidth == bufferWidth && newHeight == bufferHeight) {
            return true; // No change needed
        }

        // Update dimensions


        bufferWidth = newWidth;
        bufferHeight = newHeight;

        auto oldStagingSurface = stagingSurface;
        auto oldStagingContext = stagingContext;

        // Recreate staging surface with new dimensions
        if (!CreateStagingSurface()) {
            isValid = false;
            return false;
        }

        int oldSurfaceWidth = cairo_image_surface_get_width(oldStagingSurface);
        int oldSurfaceHeight = cairo_image_surface_get_height(oldStagingSurface);

        int copyWidth = std::min(bufferWidth, oldSurfaceWidth);
        int copyHeight = std::min(bufferHeight, oldSurfaceHeight);
        if (copyWidth > 0 && copyHeight > 0) {
            // Actually perform the copy operation
            cairo_save(stagingContext);
            cairo_set_source_surface(stagingContext, oldStagingSurface, 0, 0);
            cairo_rectangle(stagingContext, 0, 0, copyWidth, copyHeight);
            cairo_clip(stagingContext);
            cairo_paint(stagingContext);
            cairo_restore(stagingContext);

        }

        cairo_destroy(oldStagingContext);
        cairo_surface_destroy(oldStagingSurface);

        std::cout << "LinuxCairoDoubleBuffer: Resized to " << newWidth << "x" << newHeight << std::endl;
        return true;
    }

    void LinuxCairoDoubleBuffer::SwapBuffers() {
        std::lock_guard<std::mutex> lock(bufferMutex);

        if (!isValid || !windowContext || !stagingSurface) {
            return;
        }

        // Copy staging surface to window surface
        cairo_set_source_surface(windowContext, stagingSurface, 0, 0);
        cairo_set_operator(windowContext, CAIRO_OPERATOR_SOURCE);
        cairo_paint(windowContext);

        // Flush window surface to ensure drawing is complete
        cairo_surface_flush(windowSurface);
    }

    void LinuxCairoDoubleBuffer::Cleanup() {
        std::lock_guard<std::mutex> lock(bufferMutex);

        DestroyStagingSurface();

        if (windowContext) {
            cairo_destroy(windowContext);
            windowContext = nullptr;
        }

        windowSurface = nullptr; // Don't destroy - we don't own it
        isValid = false;

        std::cout << "LinuxCairoDoubleBuffer: Cleaned up" << std::endl;
    }
} // namespace UltraCanvas