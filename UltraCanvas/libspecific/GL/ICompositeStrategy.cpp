// ICompositeStrategy.cpp
// Composite strategy implementations
#include "GL/ICompositeStrategy.h"
#include "GL/GLFramebuffer.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasImage.h"

#include <iostream>

namespace UltraCanvas {

// CompositeStrategyCPU implementation
struct CompositeStrategyCPU::Impl {
    PixelBuffer buffer;
    UCPixmap pixmap;
    int lastWidth = 0;
    int lastHeight = 0;
};

CompositeStrategyCPU::CompositeStrategyCPU()
    : impl_(std::make_unique<Impl>())
{
}

CompositeStrategyCPU::~CompositeStrategyCPU() = default;

bool CompositeStrategyCPU::Composite(GLFramebuffer& framebuffer,
                                     IRenderContext* ctx,
                                     int destX, int destY,
                                     int destWidth, int destHeight,
                                     bool readback)
{
    if (!ctx) {
        return false;
    }

    int fbWidth = framebuffer.GetWidth();
    int fbHeight = framebuffer.GetHeight();

    if (fbWidth <= 0 || fbHeight <= 0) {
        return false;
    }

    if (readback) {
        // Use BGRA format for Cairo compatibility
        PixelFormat format = PixelFormat::BGRA8();

        // Reallocate buffer and pixmap if size changed
        if (fbWidth != impl_->lastWidth || fbHeight != impl_->lastHeight) {
            if (!impl_->buffer.Allocate(fbWidth, fbHeight, format)) {
                std::cerr << "[CompositeStrategyCPU] Failed to allocate pixel buffer" << std::endl;
                return false;
            }
            if (!impl_->pixmap.Init(fbWidth, fbHeight)) {
                std::cerr << "[CompositeStrategyCPU] Failed to initialize pixmap" << std::endl;
                return false;
            }
            impl_->lastWidth = fbWidth;
            impl_->lastHeight = fbHeight;
        }

        if (!framebuffer.ReadPixels(impl_->buffer.data, impl_->buffer.size, format)) {
            std::cerr << "[CompositeStrategyCPU] Failed to read pixels from framebuffer" << std::endl;
            return false;
        }

        int rowBytes = fbWidth * format.bytesPerPixel;
        uint8_t* data = static_cast<uint8_t*>(impl_->buffer.data);

        // Flip vertically in-place (GL origin bottom-left, Cairo top-left)
        for (int y = 0; y < fbHeight / 2; ++y) {
            uint8_t* topRow = data + y * impl_->buffer.rowStride;
            uint8_t* bottomRow = data + (fbHeight - 1 - y) * impl_->buffer.rowStride;
            for (int x = 0; x < rowBytes; ++x) {
                uint8_t tmp = topRow[x];
                topRow[x] = bottomRow[x];
                bottomRow[x] = tmp;
            }
        }

        impl_->pixmap.Flush();
        uint32_t* pixmapData = impl_->pixmap.GetPixelData();
        if (!pixmapData) {
            return false;
        }
        for (int y = 0; y < fbHeight; ++y) {
            uint32_t* srcRow = reinterpret_cast<uint32_t*>(data + y * impl_->buffer.rowStride);
            uint32_t* dstRow = pixmapData + y * fbWidth;
            memcpy(dstRow, srcRow, fbWidth * sizeof(uint32_t));
        }
        impl_->pixmap.MarkDirty();
    }

    // Always draw the cached pixmap (even when readback=false)
    if (!impl_->pixmap.IsValid()) {
        return false;
    }
    Rect2Df srcRect(0, 0, static_cast<float>(fbWidth), static_cast<float>(fbHeight));
    Rect2Df dstRect(static_cast<float>(destX), static_cast<float>(destY),
                   static_cast<float>(destWidth), static_cast<float>(destHeight));
    ctx->DrawPartOfPixmap(impl_->pixmap, srcRect, dstRect);

    return true;
}

// Factory function
std::unique_ptr<ICompositeStrategy> CreateCompositeStrategy() {
    // For now, only CPU strategy is implemented
    // Future: Check for EGL Image support and use that if available
    return std::make_unique<CompositeStrategyCPU>();
}

} // namespace UltraCanvas
