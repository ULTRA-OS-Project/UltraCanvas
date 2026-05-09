// GLFramebuffer.cpp
// OpenGL Framebuffer Object (FBO) implementation
#include "GL/GLFramebuffer.h"
#include "UltraCanvasGLSurface.h"

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#include <cstdlib>
#include <cstring>
#include <iostream>

// Ensure we have the required GL function pointers
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_DEPTH_STENCIL_ATTACHMENT
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#endif
#ifndef GL_DEPTH_ATTACHMENT
#define GL_DEPTH_ATTACHMENT 0x8D00
#endif
#ifndef GL_STENCIL_ATTACHMENT
#define GL_STENCIL_ATTACHMENT 0x8D20
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8 0x88F0
#endif
#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24 0x81A6
#endif
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

namespace UltraCanvas {

// PixelFormat implementations
PixelFormat PixelFormat::BGRA8() {
    PixelFormat fmt;
    fmt.glFormat = GL_BGRA;
    fmt.glType = GL_UNSIGNED_BYTE;
    fmt.bytesPerPixel = 4;
    fmt.rowAlignment = 4;
    return fmt;
}

PixelFormat PixelFormat::RGBA8() {
    PixelFormat fmt;
    fmt.glFormat = GL_RGBA;
    fmt.glType = GL_UNSIGNED_BYTE;
    fmt.bytesPerPixel = 4;
    fmt.rowAlignment = 4;
    return fmt;
}

// PixelBuffer implementation
PixelBuffer::~PixelBuffer() {
    Free();
}

PixelBuffer::PixelBuffer(PixelBuffer&& other) noexcept
    : data(other.data)
    , size(other.size)
    , width(other.width)
    , height(other.height)
    , rowStride(other.rowStride)
    , format(other.format)
{
    other.data = nullptr;
    other.size = 0;
}

PixelBuffer& PixelBuffer::operator=(PixelBuffer&& other) noexcept {
    if (this != &other) {
        Free();
        data = other.data;
        size = other.size;
        width = other.width;
        height = other.height;
        rowStride = other.rowStride;
        format = other.format;
        other.data = nullptr;
        other.size = 0;
    }
    return *this;
}

bool PixelBuffer::Allocate(int w, int h, const PixelFormat& fmt) {
    Free();

    if (w <= 0 || h <= 0) {
        return false;
    }

    // Calculate row stride with alignment
    int rowBytes = w * fmt.bytesPerPixel;
    int alignment = fmt.rowAlignment;
    rowStride = (rowBytes + alignment - 1) & ~(alignment - 1);

    size = static_cast<size_t>(rowStride) * h;
    width = w;
    height = h;
    format = fmt;

    data = malloc(size);
    if (!data) {
        size = 0;
        return false;
    }

    return true;
}

void PixelBuffer::Free() {
    if (data) {
        free(data);
        data = nullptr;
    }
    size = 0;
    width = 0;
    height = 0;
    rowStride = 0;
}

// GLFramebuffer implementation
GLFramebuffer::GLFramebuffer() = default;

GLFramebuffer::~GLFramebuffer() {
    Destroy();
}

GLFramebuffer::GLFramebuffer(GLFramebuffer&& other) noexcept
    : fbo_(other.fbo_)
    , colorTexture_(other.colorTexture_)
    , depthStencilRbo_(other.depthStencilRbo_)
    , msaaFbo_(other.msaaFbo_)
    , msaaColorRbo_(other.msaaColorRbo_)
    , msaaDepthStencilRbo_(other.msaaDepthStencilRbo_)
    , width_(other.width_)
    , height_(other.height_)
    , samples_(other.samples_)
    , depthBits_(other.depthBits_)
    , stencilBits_(other.stencilBits_)
{
    other.fbo_ = 0;
    other.colorTexture_ = 0;
    other.depthStencilRbo_ = 0;
    other.msaaFbo_ = 0;
    other.msaaColorRbo_ = 0;
    other.msaaDepthStencilRbo_ = 0;
}

GLFramebuffer& GLFramebuffer::operator=(GLFramebuffer&& other) noexcept {
    if (this != &other) {
        Destroy();
        fbo_ = other.fbo_;
        colorTexture_ = other.colorTexture_;
        depthStencilRbo_ = other.depthStencilRbo_;
        msaaFbo_ = other.msaaFbo_;
        msaaColorRbo_ = other.msaaColorRbo_;
        msaaDepthStencilRbo_ = other.msaaDepthStencilRbo_;
        width_ = other.width_;
        height_ = other.height_;
        samples_ = other.samples_;
        depthBits_ = other.depthBits_;
        stencilBits_ = other.stencilBits_;

        other.fbo_ = 0;
        other.colorTexture_ = 0;
        other.depthStencilRbo_ = 0;
        other.msaaFbo_ = 0;
        other.msaaColorRbo_ = 0;
        other.msaaDepthStencilRbo_ = 0;
    }
    return *this;
}

bool GLFramebuffer::Create(int width, int height, const GLSurfaceConfig& config) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    Destroy();

    width_ = width;
    height_ = height;
    samples_ = config.samples;
    depthBits_ = config.depthBits;
    stencilBits_ = config.stencilBits;

    return CreateAttachments();
}

bool GLFramebuffer::Resize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    if (width == width_ && height == height_) {
        return true;  // No change needed
    }

    // Store config values
    int samples = samples_;
    int depthBits = depthBits_;
    int stencilBits = stencilBits_;

    // Recreate
    DestroyAttachments();
    width_ = width;
    height_ = height;
    samples_ = samples;
    depthBits_ = depthBits;
    stencilBits_ = stencilBits;

    return CreateAttachments();
}

void GLFramebuffer::Destroy() {
    DestroyAttachments();
    width_ = 0;
    height_ = 0;
    samples_ = 0;
    depthBits_ = 0;
    stencilBits_ = 0;
}

bool GLFramebuffer::CreateAttachments() {
    // Create main FBO with texture (for readback)
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    // Create color texture
    glGenTextures(1, &colorTexture_);
    glBindTexture(GL_TEXTURE_2D, colorTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture_, 0);

    // Create depth/stencil renderbuffer if needed
    if (depthBits_ > 0 || stencilBits_ > 0) {
        glGenRenderbuffers(1, &depthStencilRbo_);
        glBindRenderbuffer(GL_RENDERBUFFER, depthStencilRbo_);

        GLenum internalFormat;
        if (depthBits_ > 0 && stencilBits_ > 0) {
            internalFormat = GL_DEPTH24_STENCIL8;
            glRenderbufferStorage(GL_RENDERBUFFER, internalFormat, width_, height_);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthStencilRbo_);
        } else if (depthBits_ > 0) {
            internalFormat = GL_DEPTH_COMPONENT24;
            glRenderbufferStorage(GL_RENDERBUFFER, internalFormat, width_, height_);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthStencilRbo_);
        }
    }

    // Check completeness
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[GLFramebuffer] Main FBO not complete: 0x" << std::hex << status << std::endl;
        DestroyAttachments();
        return false;
    }

    // Create MSAA FBO if multisampling requested
    if (samples_ > 1) {
        glGenFramebuffers(1, &msaaFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);

        // MSAA color renderbuffer
        glGenRenderbuffers(1, &msaaColorRbo_);
        glBindRenderbuffer(GL_RENDERBUFFER, msaaColorRbo_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_RGBA8, width_, height_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaaColorRbo_);

        // MSAA depth/stencil if needed
        if (depthBits_ > 0 || stencilBits_ > 0) {
            glGenRenderbuffers(1, &msaaDepthStencilRbo_);
            glBindRenderbuffer(GL_RENDERBUFFER, msaaDepthStencilRbo_);

            GLenum internalFormat = (stencilBits_ > 0) ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24;
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, internalFormat, width_, height_);

            if (stencilBits_ > 0) {
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, msaaDepthStencilRbo_);
            } else {
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msaaDepthStencilRbo_);
            }
        }

        status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "[GLFramebuffer] MSAA FBO not complete: 0x" << std::hex << status << std::endl;
            DestroyAttachments();
            return false;
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void GLFramebuffer::DestroyAttachments() {
    if (msaaDepthStencilRbo_) {
        glDeleteRenderbuffers(1, &msaaDepthStencilRbo_);
        msaaDepthStencilRbo_ = 0;
    }
    if (msaaColorRbo_) {
        glDeleteRenderbuffers(1, &msaaColorRbo_);
        msaaColorRbo_ = 0;
    }
    if (msaaFbo_) {
        glDeleteFramebuffers(1, &msaaFbo_);
        msaaFbo_ = 0;
    }
    if (depthStencilRbo_) {
        glDeleteRenderbuffers(1, &depthStencilRbo_);
        depthStencilRbo_ = 0;
    }
    if (colorTexture_) {
        glDeleteTextures(1, &colorTexture_);
        colorTexture_ = 0;
    }
    if (fbo_) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
}

void GLFramebuffer::Bind() {
    if (samples_ > 1 && msaaFbo_) {
        // Bind MSAA FBO for rendering
        glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    }
    glViewport(0, 0, width_, height_);
}

void GLFramebuffer::Unbind() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool GLFramebuffer::IsComplete() const {
    if (!fbo_) return false;

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return status == GL_FRAMEBUFFER_COMPLETE;
}

void GLFramebuffer::ResolveMSAA() {
    if (!IsMultisampled() || !msaaFbo_ || !fbo_) {
        return;
    }

    // Blit from MSAA FBO to main FBO
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_);
    glBlitFramebuffer(0, 0, width_, height_,
                      0, 0, width_, height_,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool GLFramebuffer::ReadPixels(PixelBuffer& buffer, const PixelFormat& format) {
    if (!buffer.Allocate(width_, height_, format)) {
        return false;
    }
    return ReadPixels(buffer.data, buffer.size, format);
}

bool GLFramebuffer::ReadPixels(void* buffer, size_t bufferSize, const PixelFormat& format) {
    if (!buffer || !fbo_) {
        return false;
    }

    size_t requiredSize = static_cast<size_t>(width_) * height_ * format.bytesPerPixel;
    if (bufferSize < requiredSize) {
        return false;
    }

    // Resolve MSAA if needed
    if (IsMultisampled()) {
        ResolveMSAA();
    }

    // Read from the main FBO (which has the texture)
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);

    // Set pixel pack alignment
    glPixelStorei(GL_PACK_ALIGNMENT, format.rowAlignment);

    // Read pixels
    glReadPixels(0, 0, width_, height_, format.glFormat, format.glType, buffer);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    return true;
}

} // namespace UltraCanvas
