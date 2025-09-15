// OS/MSWindows/UltraCanvasDoubleBuffer.cpp
// Double buffer rendering implementation for cross-platform compatibility
// Version: 1.0.0
// Last Modified: 2025-09-08
// Author: UltraCanvas Framework

#include "UltraCanvasDoubleBuffer.h"
#include <iostream>
#include <cstring>

namespace UltraCanvas {

// ===== WINDOWS D2D DOUBLE BUFFER IMPLEMENTATION =====
    WindowsD2DDoubleBuffer::WindowsD2DDoubleBuffer()
    : windowRenderTarget(nullptr)
    , stagingRenderTarget(nullptr)
    , stagingBitmap(nullptr)
    , bufferWidth(0)
    , bufferHeight(0)
    , isValid(false) {
}

WindowsD2DDoubleBuffer::~WindowsD2DDoubleBuffer() {
    Cleanup();
}

bool WindowsD2DDoubleBuffer::Initialize(int width, int height, void* windowRT) {
    std::lock_guard<std::mutex> lock(bufferMutex);

    if (width <= 0 || height <= 0 || !windowRT) {
        std::cerr << "WindowsD2DDoubleBuffer: Invalid parameters" << std::endl;
        return false;
    }

    windowRenderTarget = static_cast<ID2D1HwndRenderTarget*>(windowRT);
    bufferWidth = width;
    bufferHeight = height;

    if (!CreateStagingTarget()) {
        return false;
    }

    isValid = true;
    std::cout << "WindowsD2DDoubleBuffer: Initialized " << width << "x" << height << std::endl;
    return true;
}

bool WindowsD2DDoubleBuffer::CreateStagingTarget() {
    D2D1_SIZE_F size = D2D1::SizeF(static_cast<float>(bufferWidth), static_cast<float>(bufferHeight));

    HRESULT hr = windowRenderTarget->CreateCompatibleRenderTarget(
        size,
        &stagingRenderTarget
    );

    if (FAILED(hr)) {
        std::cerr << "WindowsD2DDoubleBuffer: Failed to create staging render target" << std::endl;
        return false;
    }

    return true;
}

void WindowsD2DDoubleBuffer::DestroyStagingTarget() {
    if (stagingBitmap) {
        stagingBitmap->Release();
        stagingBitmap = nullptr;
    }

    if (stagingRenderTarget) {
        stagingRenderTarget->Release();
        stagingRenderTarget = nullptr;
    }
}

bool WindowsD2DDoubleBuffer::Resize(int newWidth, int newHeight) {
    std::lock_guard<std::mutex> lock(bufferMutex);

    if (newWidth <= 0 || newHeight <= 0) {
        return false;
    }

    if (newWidth == bufferWidth && newHeight == bufferHeight) {
        return true; // No change needed
    }

    // Destroy old staging target
    DestroyStagingTarget();

    // Update dimensions
    bufferWidth = newWidth;
    bufferHeight = newHeight;

    // Recreate staging target with new dimensions
    if (!CreateStagingTarget()) {
        isValid = false;
        return false;
    }

    std::cout << "WindowsD2DDoubleBuffer: Resized to " << newWidth << "x" << newHeight << std::endl;
    return true;
}

void WindowsD2DDoubleBuffer::SwapBuffers() {
    std::lock_guard<std::mutex> lock(bufferMutex);

    if (!isValid || !windowRenderTarget || !stagingRenderTarget) {
        return;
    }

    // Get bitmap from staging render target
    HRESULT hr = stagingRenderTarget->GetBitmap(&stagingBitmap);
    if (FAILED(hr)) {
        std::cerr << "WindowsD2DDoubleBuffer: Failed to get staging bitmap" << std::endl;
        return;
    }

    // Draw staging bitmap to window
    D2D1_SIZE_F size = stagingBitmap->GetSize();
    D2D1_RECT_F destRect = D2D1::RectF(0, 0, size.width, size.height);

    windowRenderTarget->DrawBitmap(
        stagingBitmap,
        destRect,
        1.0f,
        D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
    );

    // Release the bitmap reference
    stagingBitmap->Release();
    stagingBitmap = nullptr;
}

void WindowsD2DDoubleBuffer::Cleanup() {
    std::lock_guard<std::mutex> lock(bufferMutex);

    DestroyStagingTarget();
    windowRenderTarget = nullptr; // Don't release - we don't own it
    isValid = false;

    std::cout << "WindowsD2DDoubleBuffer: Cleaned up" << std::endl;
}

} // namespace UltraCanvas//
