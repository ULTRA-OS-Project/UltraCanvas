// GL/GLFramebuffer.h
// OpenGL Framebuffer Object (FBO) wrapper for offscreen rendering
#pragma once

#ifndef ULTRACANVAS_ENABLE_GL
#error "GL/GLFramebuffer.h requires ULTRACANVAS_ENABLE_GL to be defined"
#endif

#include <cstdint>
#include <cstddef>

// Forward declare GL types to avoid including GL headers in this header
typedef unsigned int GLuint;
typedef unsigned int GLenum;

namespace UltraCanvas {

struct GLSurfaceConfig;  // Forward declaration

// Pixel format information for readback
struct PixelFormat {
    GLenum glFormat;        // GL_RGBA, GL_BGRA_EXT, etc.
    GLenum glType;          // GL_UNSIGNED_BYTE, GL_UNSIGNED_INT_8_8_8_8_REV, etc.
    int bytesPerPixel;      // Total bytes per pixel
    int rowAlignment;       // Row alignment requirement (1, 2, 4, or 8)

    static PixelFormat BGRA8();     // For Cairo compatibility
    static PixelFormat RGBA8();     // Standard RGBA
};

// Native pixel buffer for readback (no std::vector, proper alignment)
struct PixelBuffer {
    void* data = nullptr;
    size_t size = 0;
    int width = 0;
    int height = 0;
    int rowStride = 0;      // Bytes per row (may include padding)
    PixelFormat format;

    PixelBuffer() = default;
    ~PixelBuffer();

    // Non-copyable, movable
    PixelBuffer(const PixelBuffer&) = delete;
    PixelBuffer& operator=(const PixelBuffer&) = delete;
    PixelBuffer(PixelBuffer&& other) noexcept;
    PixelBuffer& operator=(PixelBuffer&& other) noexcept;

    // Allocate buffer with proper alignment
    // Returns true on success
    bool Allocate(int w, int h, const PixelFormat& fmt);

    // Free the buffer
    void Free();

    // Check if buffer is allocated
    bool IsValid() const { return data != nullptr && size > 0; }
};

// OpenGL Framebuffer Object wrapper
class GLFramebuffer {
public:
    GLFramebuffer();
    ~GLFramebuffer();

    // Non-copyable, movable
    GLFramebuffer(const GLFramebuffer&) = delete;
    GLFramebuffer& operator=(const GLFramebuffer&) = delete;
    GLFramebuffer(GLFramebuffer&& other) noexcept;
    GLFramebuffer& operator=(GLFramebuffer&& other) noexcept;

    // Create framebuffer with specified dimensions and format
    // config is used to determine depth/stencil/MSAA settings
    bool Create(int width, int height, const GLSurfaceConfig& config);

    // Resize the framebuffer (destroys and recreates attachments)
    bool Resize(int width, int height);

    // Destroy the framebuffer and all attachments
    void Destroy();

    // Bind for rendering (as draw framebuffer)
    void Bind();

    // Unbind (bind default framebuffer 0)
    void Unbind();

    // Check if framebuffer is complete and ready for rendering
    bool IsComplete() const;

    // Dimensions
    int GetWidth() const { return width_; }
    int GetHeight() const { return height_; }

    // Get the color texture handle (for GPU compositing in future)
    GLuint GetColorTexture() const { return colorTexture_; }

    // Get the FBO handle
    GLuint GetHandle() const { return fbo_; }

    // Check if using MSAA (requires resolve before readback)
    bool IsMultisampled() const { return samples_ > 1; }
    int GetSampleCount() const { return samples_; }

    // Read pixels from the framebuffer into a buffer
    // If MSAA is enabled, this will resolve first
    // format specifies the desired output format
    bool ReadPixels(PixelBuffer& buffer, const PixelFormat& format);

    // Read pixels directly into a pre-allocated buffer
    // buffer must be large enough: width * height * format.bytesPerPixel
    // Returns true on success
    bool ReadPixels(void* buffer, size_t bufferSize, const PixelFormat& format);

private:
    GLuint fbo_ = 0;                    // Main framebuffer object
    GLuint colorTexture_ = 0;           // Color attachment (texture)
    GLuint depthStencilRbo_ = 0;        // Depth/stencil renderbuffer
    GLuint msaaFbo_ = 0;                // MSAA framebuffer (if multisampled)
    GLuint msaaColorRbo_ = 0;           // MSAA color renderbuffer
    GLuint msaaDepthStencilRbo_ = 0;    // MSAA depth/stencil renderbuffer

    int width_ = 0;
    int height_ = 0;
    int samples_ = 0;
    int depthBits_ = 0;
    int stencilBits_ = 0;

    // Resolve MSAA to the non-MSAA framebuffer
    void ResolveMSAA();

    // Helper to create attachments
    bool CreateAttachments();
    void DestroyAttachments();
};

} // namespace UltraCanvas
