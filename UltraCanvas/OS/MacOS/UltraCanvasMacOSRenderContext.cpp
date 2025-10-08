// OS/MacOS/UltraCanvasMacOSRenderContext.cpp
// Complete Core Graphics render context implementation
// Version: 1.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#include "UltraCanvasMacOSRenderContext.h"
#include <iostream>
#include <algorithm>
#include <cmath>

#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>
#import <Foundation/Foundation.h>

namespace UltraCanvas {

// ===== DOUBLE BUFFER IMPLEMENTATION =====
MacOSCoreGraphicsDoubleBuffer::MacOSCoreGraphicsDoubleBuffer()
    : windowContext(nullptr)
    , windowLayer(nullptr)
    , stagingLayer(nullptr)
    , stagingContext(nullptr)
    , bufferWidth(0)
    , bufferHeight(0)
    , isValid(false)
    , colorSpace(nullptr) {
}

MacOSCoreGraphicsDoubleBuffer::~MacOSCoreGraphicsDoubleBuffer() {
    Cleanup();
}

bool MacOSCoreGraphicsDoubleBuffer::Initialize(int width, int height, void* windowContextPtr) {
    std::lock_guard<std::mutex> lock(bufferMutex);
    
    windowContext = static_cast<CGContextRef>(windowContextPtr);
    if (!windowContext) {
        std::cerr << "UltraCanvas macOS: Invalid window context provided" << std::endl;
        return false;
    }
    
    bufferWidth = width;
    bufferHeight = height;
    
    // Create optimal color space
    CreateOptimalColorSpace();
    
    // Create staging layer
    if (!CreateStagingLayer()) {
        std::cerr << "UltraCanvas macOS: Failed to create staging layer" << std::endl;
        return false;
    }
    
    isValid = true;
    std::cout << "UltraCanvas macOS: Double buffer initialized (" << width << "x" << height << ")" << std::endl;
    return true;
}

bool MacOSCoreGraphicsDoubleBuffer::Resize(int newWidth, int newHeight) {
    std::lock_guard<std::mutex> lock(bufferMutex);
    
    if (newWidth == bufferWidth && newHeight == bufferHeight) {
        return true; // No change needed
    }
    
    bufferWidth = newWidth;
    bufferHeight = newHeight;
    
    // Recreate staging layer with new dimensions
    DestroyStagingLayer();
    return CreateStagingLayer();
}

void MacOSCoreGraphicsDoubleBuffer::SwapBuffers() {
    std::lock_guard<std::mutex> lock(bufferMutex);
    
    if (!isValid || !windowContext || !stagingLayer) {
        return;
    }
    
    // Draw staging layer to window context
    CGRect destRect = CGRectMake(0, 0, bufferWidth, bufferHeight);
    CGContextDrawLayerAtPoint(windowContext, CGPointZero, stagingLayer);
    CGContextFlush(windowContext);
}

void MacOSCoreGraphicsDoubleBuffer::Cleanup() {
    std::lock_guard<std::mutex> lock(bufferMutex);
    
    DestroyStagingLayer();
    
    if (colorSpace) {
        CGColorSpaceRelease(colorSpace);
        colorSpace = nullptr;
    }
    
    windowContext = nullptr;
    isValid = false;
}

bool MacOSCoreGraphicsDoubleBuffer::CreateStagingLayer() {
    if (!windowContext || bufferWidth <= 0 || bufferHeight <= 0) {
        return false;
    }
    
    CGSize layerSize = CGSizeMake(bufferWidth, bufferHeight);
    stagingLayer = CGLayerCreateWithContext(windowContext, layerSize, nullptr);
    
    if (!stagingLayer) {
        std::cerr << "UltraCanvas macOS: Failed to create CGLayer" << std::endl;
        return false;
    }
    
    stagingContext = CGLayerGetContext(stagingLayer);
    if (!stagingContext) {
        std::cerr << "UltraCanvas macOS: Failed to get layer context" << std::endl;
        CGLayerRelease(stagingLayer);
        stagingLayer = nullptr;
        return false;
    }
    
    // Configure staging context
    CGContextSetShouldAntialias(stagingContext, true);
    CGContextSetAllowsAntialiasing(stagingContext, true);
    CGContextSetInterpolationQuality(stagingContext, kCGInterpolationHigh);
    
    return true;
}

void MacOSCoreGraphicsDoubleBuffer::DestroyStagingLayer() {
    if (stagingLayer) {
        CGLayerRelease(stagingLayer);
        stagingLayer = nullptr;
    }
    stagingContext = nullptr;
}

void MacOSCoreGraphicsDoubleBuffer::CreateOptimalColorSpace() {
    colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    if (!colorSpace) {
        colorSpace = CGColorSpaceCreateDeviceRGB();
    }
}

// ===== PIXEL BUFFER IMPLEMENTATION =====
MacOSPixelBuffer::MacOSPixelBuffer()
    : cgImage(nullptr)
    , dataProvider(nullptr)
    , colorSpace(nullptr)
    , width(0)
    , height(0)
    , needsUpdate(true) {
}

MacOSPixelBuffer::MacOSPixelBuffer(int w, int h)
    : MacOSPixelBuffer() {
    Initialize(w, h);
}

MacOSPixelBuffer::~MacOSPixelBuffer() {
    Clear();
}

bool MacOSPixelBuffer::Initialize(int w, int h) {
    if (w <= 0 || h <= 0) return false;
    
    width = w;
    height = h;
    pixelData.resize(w * h);
    needsUpdate = true;
    
    // Create color space
    colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    if (!colorSpace) {
        colorSpace = CGColorSpaceCreateDeviceRGB();
    }
    
    return true;
}

void MacOSPixelBuffer::Clear() {
    ReleaseCGImage();
    
    if (colorSpace) {
        CGColorSpaceRelease(colorSpace);
        colorSpace = nullptr;
    }
    
    pixelData.clear();
    width = height = 0;
    needsUpdate = true;
}

bool MacOSPixelBuffer::IsValid() const {
    return width > 0 && height > 0 && !pixelData.empty();
}

size_t MacOSPixelBuffer::GetSizeInBytes() const {
    return pixelData.size() * sizeof(uint32_t);
}

uint32_t* MacOSPixelBuffer::GetPixelData() {
    needsUpdate = true;
    return pixelData.data();
}

void MacOSPixelBuffer::SetPixel(int x, int y, uint32_t pixel) {
    if (x >= 0 && x < width && y >= 0 && y < height) {
        pixelData[y * width + x] = pixel;
        needsUpdate = true;
    }
}

uint32_t MacOSPixelBuffer::GetPixel(int x, int y) const {
    if (x >= 0 && x < width && y >= 0 && y < height) {
        return pixelData[y * width + x];
    }
    return 0;
}

CGImageRef MacOSPixelBuffer::GetCGImage() {
    if (needsUpdate || !cgImage) {
        UpdateCGImage();
    }
    return cgImage;
}

void MacOSPixelBuffer::UpdateCGImage() {
    ReleaseCGImage();
    CreateCGImage();
    needsUpdate = false;
}

void MacOSPixelBuffer::CreateCGImage() {
    if (pixelData.empty() || !colorSpace) return;
    
    size_t bitsPerComponent = 8;
    size_t bitsPerPixel = 32;
    size_t bytesPerRow = width * 4;
    
    dataProvider = CGDataProviderCreateWithData(nullptr, pixelData.data(), 
                                                pixelData.size() * sizeof(uint32_t), 
                                                nullptr);
    
    if (dataProvider) {
        cgImage = CGImageCreate(width, height, bitsPerComponent, bitsPerPixel, 
                               bytesPerRow, colorSpace, 
                               kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big,
                               dataProvider, nullptr, false, kCGRenderingIntentDefault);
    }
}

void MacOSPixelBuffer::ReleaseCGImage() {
    if (cgImage) {
        CGImageRelease(cgImage);
        cgImage = nullptr;
    }
    
    if (dataProvider) {
        CGDataProviderRelease(dataProvider);
        dataProvider = nullptr;
    }
}

// ===== RENDER CONTEXT IMPLEMENTATION =====
MacOSRenderContext::MacOSRenderContext(CGContextRef context, int width, int height, bool enableDoubleBuffering)
    : cgContext(nullptr)
    , backBuffer(nullptr)
    , ownsContext(false)
    , doubleBufferingEnabled(enableDoubleBuffering)
    , globalAlpha(1.0f)
    , colorSpace(nullptr)
    , currentPath(nullptr)
    , viewportWidth(width)
    , viewportHeight(height)
    , hasClipRect(false)
    , owningThread(std::this_thread::get_id())
    , cachedFillColor(nullptr)
    , cachedStrokeColor(nullptr)
    , cachedFont(nullptr)
    , cachedFontSize(0) {
    
    SetCGContext(context);
    InitializeCoreGraphics(width, height);
}

MacOSRenderContext::MacOSRenderContext(int width, int height, bool enableDoubleBuffering)
    : MacOSRenderContext(nullptr, width, height, enableDoubleBuffering) {
    // Create our own context
    ownsContext = true;
    
    CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    size_t bytesPerRow = width * 4;
    
    cgContext = CGBitmapContextCreate(nullptr, width, height, 8, bytesPerRow, 
                                     colorSpace, kCGImageAlphaPremultipliedLast);
    
    CGColorSpaceRelease(colorSpace);
    
    if (!cgContext) {
        std::cerr << "UltraCanvas macOS: Failed to create bitmap context" << std::endl;
    }
}

MacOSRenderContext::~MacOSRenderContext() {
    Cleanup();
}

bool MacOSRenderContext::InitializeCoreGraphics(int width, int height) {
    viewportWidth = width;
    viewportHeight = height;
    
    // Create color space
    CreateColorSpace();
    
    // Setup default state
    SetupDefaultState();
    
    // Initialize double buffering if enabled
    if (doubleBufferingEnabled && cgContext) {
        doubleBuffer = std::make_unique<MacOSCoreGraphicsDoubleBuffer>();
        if (!doubleBuffer->Initialize(width, height, cgContext)) {
            std::cerr << "UltraCanvas macOS: Failed to initialize double buffering" << std::endl;
            doubleBufferingEnabled = false;
        }
    }
    
    std::cout << "UltraCanvas macOS: Render context initialized (" << width << "x" << height << ")" << std::endl;
    return true;
}

void MacOSRenderContext::SetupDefaultState() {
    if (!cgContext) return;
    
    SafeExecute([this]() {
        // Set default drawing properties
        CGContextSetLineWidth(cgContext, 1.0);
        CGContextSetLineJoin(cgContext, kCGLineJoinRound);
        CGContextSetLineCap(cgContext, kCGLineCapRound);
        CGContextSetMiterLimit(cgContext, 10.0);
        
        // Enable antialiasing
        CGContextSetShouldAntialias(cgContext, true);
        CGContextSetAllowsAntialiasing(cgContext, true);
        CGContextSetInterpolationQuality(cgContext, kCGInterpolationHigh);
        
        // Set default colors
        CGContextSetRGBFillColor(cgContext, 0, 0, 0, 1); // Black
        CGContextSetRGBStrokeColor(cgContext, 0, 0, 0, 1); // Black
        
        // Set blend mode
        CGContextSetBlendMode(cgContext, kCGBlendModeNormal);
    });
}

void MacOSRenderContext::CreateColorSpace() {
    if (!colorSpace) {
        colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        if (!colorSpace) {
            colorSpace = CGColorSpaceCreateDeviceRGB();
        }
    }
}

void MacOSRenderContext::SetCGContext(CGContextRef context) {
    cgContext = context;
}

// ===== STATE MANAGEMENT =====
void MacOSRenderContext::PushState() {
    SafeExecute([this]() {
        if (cgContext) {
            CGContextSaveGState(cgContext);
            contextStack.push(cgContext);
        }
    });
}

void MacOSRenderContext::PopState() {
    SafeExecute([this]() {
        if (cgContext && !contextStack.empty()) {
            CGContextRestoreGState(cgContext);
            contextStack.pop();
        }
    });
}

void MacOSRenderContext::ResetState() {
    SafeExecute([this]() {
        // Pop all states
        while (!contextStack.empty()) {
            PopState();
        }
        
        // Reset to default state
        SetupDefaultState();
    });
}

// ===== TRANSFORMATION =====
void MacOSRenderContext::Translate(float x, float y) {
    SafeExecute([this, x, y]() {
        if (cgContext) {
            CGContextTranslateCTM(cgContext, x, y);
        }
    });
}

void MacOSRenderContext::Rotate(float angle) {
    SafeExecute([this, angle]() {
        if (cgContext) {
            CGContextRotateCTM(cgContext, angle);
        }
    });
}

void MacOSRenderContext::Scale(float sx, float sy) {
    SafeExecute([this, sx, sy]() {
        if (cgContext) {
            CGContextScaleCTM(cgContext, sx, sy);
        }
    });
}

void MacOSRenderContext::SetTransform(float a, float b, float c, float d, float e, float f) {
    SafeExecute([this, a, b, c, d, e, f]() {
        if (cgContext) {
            CGAffineTransform transform = CGAffineTransformMake(a, b, c, d, e, f);
            CGContextSetCTM(cgContext, transform);
        }
    });
}

void MacOSRenderContext::ResetTransform() {
    SafeExecute([this]() {
        if (cgContext) {
            CGAffineTransform identity = CGAffineTransformIdentity;
            CGContextSetCTM(cgContext, identity);
        }
    });
}

// ===== CLIPPING =====
void MacOSRenderContext::SetClipRect(float x, float y, float w, float h) {
    SafeExecute([this, x, y, w, h]() {
        if (cgContext) {
            CGRect rect = CGRectMake(x, y, w, h);
            CGContextClipToRect(cgContext, rect);
            clipRect = rect;
            hasClipRect = true;
        }
    });
}

void MacOSRenderContext::ClearClipRect() {
    SafeExecute([this]() {
        // To clear clipping, we need to restore the graphics state
        // This is typically done by managing state save/restore
        hasClipRect = false;
    });
}

void MacOSRenderContext::ClipRect(float x, float y, float w, float h) {
    SafeExecute([this, x, y, w, h]() {
        if (cgContext) {
            CGRect rect = CGRectMake(x, y, w, h);
            CGContextClipToRect(cgContext, rect);
            
            if (hasClipRect) {
                clipRect = CGRectIntersection(clipRect, rect);
            } else {
                clipRect = rect;
                hasClipRect = true;
            }
        }
    });
}

// ===== STYLE MANAGEMENT =====
void MacOSRenderContext::SetDrawingStyle(const DrawingStyle& style) {
    currentDrawingStyle = style;
    
    SafeExecute([this, &style]() {
        if (cgContext) {
            // Set line width
            CGContextSetLineWidth(cgContext, style.lineWidth);
            
            // Set line cap
            CGLineCap lineCap = kCGLineCapButt;
            switch (style.lineCap) {
                case LineCap::Round: lineCap = kCGLineCapRound; break;
                case LineCap::Square: lineCap = kCGLineCapSquare; break;
                default: lineCap = kCGLineCapButt; break;
            }
            CGContextSetLineCap(cgContext, lineCap);
            
            // Set line join
            CGLineJoin lineJoin = kCGLineJoinMiter;
            switch (style.lineJoin) {
                case LineJoin::Round: lineJoin = kCGLineJoinRound; break;
                case LineJoin::Bevel: lineJoin = kCGLineJoinBevel; break;
                default: lineJoin = kCGLineJoinMiter; break;
            }
            CGContextSetLineJoin(cgContext, lineJoin);
            
            // Update colors
            UpdateFillColor(style.fillColor);
            UpdateStrokeColor(style.strokeColor);
        }
    });
}

void MacOSRenderContext::SetTextStyle(const TextStyle& style) {
    currentTextStyle = style;
    
    // Font will be cached when needed during text rendering
    cachedFontName = style.fontFamily;
    cachedFontSize = style.fontSize;
    
    // Release cached font to force recreation
    if (cachedFont) {
        CFRelease(cachedFont);
        cachedFont = nullptr;
    }
}

void MacOSRenderContext::SetAlpha(float alpha) {
    globalAlpha = std::max(0.0f, std::min(1.0f, alpha));
    
    SafeExecute([this]() {
        if (cgContext) {
            CGContextSetAlpha(cgContext, globalAlpha);
        }
    });
}

// ===== BASIC SHAPES =====
void MacOSRenderContext::DrawLine(float x1, float y1, float x2, float y2) {
    SafeExecute([this, x1, y1, x2, y2]() {
        if (cgContext) {
            CGContextBeginPath(cgContext);
            CGContextMoveToPoint(cgContext, x1, y1);
            CGContextAddLineToPoint(cgContext, x2, y2);
            CGContextStrokePath(cgContext);
        }
    });
}

void MacOSRenderContext::DrawRectangle(float x, float y, float width, float height) {
    SafeExecute([this, x, y, width, height]() {
        if (cgContext) {
            CGRect rect = CGRectMake(x, y, width, height);
            CGContextStrokeRect(cgContext, rect);
        }
    });
}

void MacOSRenderContext::FillRectangle(float x, float y, float width, float height) {
    SafeExecute([this, x, y, width, height]() {
        if (cgContext) {
            CGRect rect = CGRectMake(x, y, width, height);
            CGContextFillRect(cgContext, rect);
        }
    });
}

void MacOSRenderContext::DrawCircle(float centerX, float centerY, float radius) {
    SafeExecute([this, centerX, centerY, radius]() {
        if (cgContext) {
            CGRect rect = CGRectMake(centerX - radius, centerY - radius, 
                                   radius * 2, radius * 2);
            CGContextStrokeEllipseInRect(cgContext, rect);
        }
    });
}

void MacOSRenderContext::FillCircle(float centerX, float centerY, float radius) {
    SafeExecute([this, centerX, centerY, radius]() {
        if (cgContext) {
            CGRect rect = CGRectMake(centerX - radius, centerY - radius, 
                                   radius * 2, radius * 2);
            CGContextFillEllipseInRect(cgContext, rect);
        }
    });
}

void MacOSRenderContext::DrawEllipse(float x, float y, float width, float height) {
    SafeExecute([this, x, y, width, height]() {
        if (cgContext) {
            CGRect rect = CGRectMake(x, y, width, height);
            CGContextStrokeEllipseInRect(cgContext, rect);
        }
    });
}

void MacOSRenderContext::FillEllipse(float x, float y, float width, float height) {
    SafeExecute([this, x, y, width, height]() {
        if (cgContext) {
            CGRect rect = CGRectMake(x, y, width, height);
            CGContextFillEllipseInRect(cgContext, rect);
        }
    });
}

// ===== PATH OPERATIONS =====
void MacOSRenderContext::BeginPath() {
    SafeExecute([this]() {
        CreatePathIfNeeded();
        if (cgContext) {
            CGContextBeginPath(cgContext);
        }
    });
}

void MacOSRenderContext::ClosePath() {
    SafeExecute([this]() {
        if (cgContext) {
            CGContextClosePath(cgContext);
        }
    });
}

void MacOSRenderContext::MoveTo(float x, float y) {
    SafeExecute([this, x, y]() {
        CreatePathIfNeeded();
        if (cgContext) {
            CGContextMoveToPoint(cgContext, x, y);
        }
        if (currentPath) {
            CGPathMoveToPoint(currentPath, nullptr, x, y);
        }
    });
}

void MacOSRenderContext::LineTo(float x, float y) {
    SafeExecute([this, x, y]() {
        CreatePathIfNeeded();
        if (cgContext) {
            CGContextAddLineToPoint(cgContext, x, y);
        }
        if (currentPath) {
            CGPathAddLineToPoint(currentPath, nullptr, x, y);
        }
    });
}

void MacOSRenderContext::CurveTo(float cp1x, float cp1y, float cp2x, float cp2y, float x, float y) {
    SafeExecute([this, cp1x, cp1y, cp2x, cp2y, x, y]() {
        CreatePathIfNeeded();
        if (cgContext) {
            CGContextAddCurveToPoint(cgContext, cp1x, cp1y, cp2x, cp2y, x, y);
        }
        if (currentPath) {
            CGPathAddCurveToPoint(currentPath, nullptr, cp1x, cp1y, cp2x, cp2y, x, y);
        }
    });
}

void MacOSRenderContext::ArcTo(float x1, float y1, float x2, float y2, float radius) {
    SafeExecute([this, x1, y1, x2, y2, radius]() {
        CreatePathIfNeeded();
        if (cgContext) {
            CGContextAddArcToPoint(cgContext, x1, y1, x2, y2, radius);
        }
        if (currentPath) {
            CGPathAddArcToPoint(currentPath, nullptr, x1, y1, x2, y2, radius);
        }
    });
}

void MacOSRenderContext::Arc(float centerX, float centerY, float radius, float startAngle, float endAngle, bool counterclockwise) {
    SafeExecute([this, centerX, centerY, radius, startAngle, endAngle, counterclockwise]() {
        CreatePathIfNeeded();
        if (cgContext) {
            CGContextAddArc(cgContext, centerX, centerY, radius, startAngle, endAngle, counterclockwise ? 1 : 0);
        }
        if (currentPath) {
            CGPathAddArc(currentPath, nullptr, centerX, centerY, radius, startAngle, endAngle, counterclockwise ? 1 : 0);
        }
    });
}

void MacOSRenderContext::StrokePath() {
    SafeExecute([this]() {
        if (cgContext) {
            CGContextStrokePath(cgContext);
        }
    });
}

void MacOSRenderContext::FillLinePath() {
    SafeExecute([this]() {
        if (cgContext) {
            CGContextFillLinePath(cgContext);
        }
    });
}

// ===== TEXT RENDERING =====
void MacOSRenderContext::DrawText(const std::string& text, float x, float y) {
    SafeExecute([this, &text, x, y]() {
        if (cgContext && !text.empty()) {
            // Get or create font
            CTFontRef font = GetOrCreateFont(currentTextStyle.fontFamily, currentTextStyle.fontSize);
            if (!font) return;
            
            // Create attributed string
            CFStringRef string = CFStringCreateWithCString(nullptr, text.c_str(), kCFStringEncodingUTF8);
            if (!string) return;
            
            CFMutableDictionaryRef attributes = CFDictionaryCreateMutable(nullptr, 2, 
                &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
            
            CFDictionarySetValue(attributes, kCTFontAttributeName, font);
            
            // Set text color
            CGColorRef textColor = CreateCGColor(currentTextStyle.color);
            CFDictionarySetValue(attributes, kCTForegroundColorAttributeName, textColor);
            
            CFAttributedStringRef attributedString = CFAttributedStringCreate(nullptr, string, attributes);
            
            // Create line and draw
            CTLineRef line = CTLineCreateWithAttributedString(attributedString);
            CGContextSetTextPosition(cgContext, x, y);
            CTLineDraw(line, cgContext);
            
            // Cleanup
            CFRelease(line);
            CFRelease(attributedString);
            CFRelease(attributes);
            CFRelease(string);
            CGColorRelease(textColor);
        }
    });
}

void MacOSRenderContext::DrawTextInRect(const std::string& text, float x, float y, float width, float height, TextAlign align) {
    SafeExecute([this, &text, x, y, width, height, align]() {
        if (cgContext && !text.empty()) {
            // Create frame setter for multi-line text
            CTFontRef font = GetOrCreateFont(currentTextStyle.fontFamily, currentTextStyle.fontSize);
            if (!font) return;
            
            CFStringRef string = CFStringCreateWithCString(nullptr, text.c_str(), kCFStringEncodingUTF8);
            if (!string) return;
            
            CFMutableDictionaryRef attributes = CFDictionaryCreateMutable(nullptr, 2, 
                &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
            
            CFDictionarySetValue(attributes, kCTFontAttributeName, font);
            
            CGColorRef textColor = CreateCGColor(currentTextStyle.color);
            CFDictionarySetValue(attributes, kCTForegroundColorAttributeName, textColor);
            
            // Set alignment
            CTTextAlignment ctAlign = kCTTextAlignmentLeft;
            switch (align) {
                case TextAlign::Center: ctAlign = kCTTextAlignmentCenter; break;
                case TextAlign::Right: ctAlign = kCTTextAlignmentRight; break;
                default: ctAlign = kCTTextAlignmentLeft; break;
            }
            
            CTParagraphStyleSetting alignmentSetting = {kCTParagraphStyleSpecifierAlignment, sizeof(ctAlign), &ctAlign};
            CTParagraphStyleRef paragraphStyle = CTParagraphStyleCreate(&alignmentSetting, 1);
            CFDictionarySetValue(attributes, kCTParagraphStyleAttributeName, paragraphStyle);
            
            CFAttributedStringRef attributedString = CFAttributedStringCreate(nullptr, string, attributes);
            CTFramesetterRef framesetter = CTFramesetterCreateWithAttributedString(attributedString);
            
            // Create path for frame
            CGPathRef path = CGPathCreateWithRect(CGRectMake(x, y, width, height), nullptr);
            CTFrameRef frame = CTFramesetterCreateFrame(framesetter, CFRangeMake(0, 0), path, nullptr);
            
            // Draw frame
            CTFrameDraw(frame, cgContext);
            
            // Cleanup
            CFRelease(frame);
            CGPathRelease(path);
            CFRelease(framesetter);
            CFRelease(attributedString);
            CFRelease(paragraphStyle);
            CFRelease(attributes);
            CFRelease(string);
            CGColorRelease(textColor);
        }
    });
}

Rect2D MacOSRenderContext::MeasureText(const std::string& text) {
    if (text.empty()) return Rect2D(0, 0, 0, 0);
    
    CTFontRef font = GetOrCreateFont(currentTextStyle.fontFamily, currentTextStyle.fontSize);
    if (!font) return Rect2D(0, 0, 0, 0);
    
    CFStringRef string = CFStringCreateWithCString(nullptr, text.c_str(), kCFStringEncodingUTF8);
    if (!string) return Rect2D(0, 0, 0, 0);
    
    CFMutableDictionaryRef attributes = CFDictionaryCreateMutable(nullptr, 1, 
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(attributes, kCTFontAttributeName, font);
    
    CFAttributedStringRef attributedString = CFAttributedStringCreate(nullptr, string, attributes);
    CTLineRef line = CTLineCreateWithAttributedString(attributedString);
    
    CGRect bounds = CTLineGetBoundsWithOptions(line, kCTLineBoundsUseOpticalBounds);
    
    CFRelease(line);
    CFRelease(attributedString);
    CFRelease(attributes);
    CFRelease(string);
    
    return Rect2D(bounds.origin.x, bounds.origin.y, bounds.size.width, bounds.size.height);
}

float MacOSRenderContext::GetTextHeight() const {
    CTFontRef font = GetOrCreateFont(currentTextStyle.fontFamily, currentTextStyle.fontSize);
    if (!font) return 0.0f;
    
    CGFloat ascent = CTFontGetAscent(font);
    CGFloat descent = CTFontGetDescent(font);
    CGFloat leading = CTFontGetLeading(font);
    
    return static_cast<float>(ascent + descent + leading);
}

// ===== IMAGE OPERATIONS =====
void MacOSRenderContext::DrawImage(const std::string& imagePath, float x, float y) {
    SafeExecute([this, &imagePath, x, y]() {
        if (cgContext) {
            // Load image from path
            CFStringRef path = CFStringCreateWithCString(nullptr, imagePath.c_str(), kCFStringEncodingUTF8);
            CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, path, kCFURLPOSIXPathStyle, false);
            
            CGImageSourceRef imageSource = CGImageSourceCreateWithURL(url, nullptr);
            if (imageSource) {
                CGImageRef image = CGImageSourceCreateImageAtIndex(imageSource, 0, nullptr);
                if (image) {
                    CGFloat width = CGImageGetWidth(image);
                    CGFloat height = CGImageGetHeight(image);
                    CGRect rect = CGRectMake(x, y, width, height);
                    CGContextDrawImage(cgContext, rect, image);
                    CGImageRelease(image);
                }
                CFRelease(imageSource);
            }
            
            CFRelease(url);
            CFRelease(path);
        }
    });
}

void MacOSRenderContext::DrawImage(const std::string& imagePath, float x, float y, float width, float height) {
    SafeExecute([this, &imagePath, x, y, width, height]() {
        if (cgContext) {
            CFStringRef path = CFStringCreateWithCString(nullptr, imagePath.c_str(), kCFStringEncodingUTF8);
            CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, path, kCFURLPOSIXPathStyle, false);
            
            CGImageSourceRef imageSource = CGImageSourceCreateWithURL(url, nullptr);
            if (imageSource) {
                CGImageRef image = CGImageSourceCreateImageAtIndex(imageSource, 0, nullptr);
                if (image) {
                    CGRect rect = CGRectMake(x, y, width, height);
                    CGContextDrawImage(cgContext, rect, image);
                    CGImageRelease(image);
                }
                CFRelease(imageSource);
            }
            
            CFRelease(url);
            CFRelease(path);
        }
    });
}

void MacOSRenderContext::DrawImageFromBuffer(IPixelBuffer* buffer, float x, float y) {
    if (!buffer) return;
    
    SafeExecute([this, buffer, x, y]() {
        MacOSPixelBuffer* macBuffer = dynamic_cast<MacOSPixelBuffer*>(buffer);
        if (macBuffer && cgContext) {
            CGImageRef image = macBuffer->GetCGImage();
            if (image) {
                CGFloat width = CGImageGetWidth(image);
                CGFloat height = CGImageGetHeight(image);
                CGRect rect = CGRectMake(x, y, width, height);
                CGContextDrawImage(cgContext, rect, image);
            }
        }
    });
}

void MacOSRenderContext::DrawImageFromBuffer(IPixelBuffer* buffer, float x, float y, float width, float height) {
    if (!buffer) return;
    
    SafeExecute([this, buffer, x, y, width, height]() {
        MacOSPixelBuffer* macBuffer = dynamic_cast<MacOSPixelBuffer*>(buffer);
        if (macBuffer && cgContext) {
            CGImageRef image = macBuffer->GetCGImage();
            if (image) {
                CGRect rect = CGRectMake(x, y, width, height);
                CGContextDrawImage(cgContext, rect, image);
            }
        }
    });
}

// ===== BUFFER OPERATIONS =====
void MacOSRenderContext::Clear() {
    SafeExecute([this]() {
        if (cgContext) {
            CGRect rect = CGRectMake(0, 0, viewportWidth, viewportHeight);
            CGContextClearRect(cgContext, rect);
        }
    });
}

void MacOSRenderContext::Clear(const Color& color) {
    SafeExecute([this, &color]() {
        if (cgContext) {
            CGColorRef cgColor = CreateCGColor(color);
            CGContextSetColorWithColor(cgContext, cgColor);
            
            CGRect rect = CGRectMake(0, 0, viewportWidth, viewportHeight);
            CGContextFillRect(cgContext, rect);
            
            CGColorRelease(cgColor);
        }
    });
}

std::unique_ptr<IPixelBuffer> MacOSRenderContext::CaptureBuffer() {
    return CaptureBuffer(0, 0, viewportWidth, viewportHeight);
}

std::unique_ptr<IPixelBuffer> MacOSRenderContext::CaptureBuffer(float x, float y, float width, float height) {
    if (!cgContext) return nullptr;
    
    auto buffer = std::make_unique<MacOSPixelBuffer>(static_cast<int>(width), static_cast<int>(height));
    
    SafeExecute([this, &buffer, x, y, width, height]() {
        // Create a bitmap context from the buffer
        uint32_t* pixelData = buffer->GetPixelData();
        CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        
        CGContextRef bitmapContext = CGBitmapContextCreate(
            pixelData, width, height, 8, width * 4, colorSpace, 
            kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
        
        if (bitmapContext) {
            // Copy from main context to bitmap context
            CGImageRef image = CGBitmapContextCreateImage(cgContext);
            if (image) {
                CGRect srcRect = CGRectMake(x, y, width, height);
                CGImageRef croppedImage = CGImageCreateWithImageInRect(image, srcRect);
                
                if (croppedImage) {
                    CGRect destRect = CGRectMake(0, 0, width, height);
                    CGContextDrawImage(bitmapContext, destRect, croppedImage);
                    CGImageRelease(croppedImage);
                }
                CGImageRelease(image);
            }
            CGContextRelease(bitmapContext);
        }
        
        CGColorSpaceRelease(colorSpace);
        buffer->MarkAsUpdated();
    });
    
    return std::unique_ptr<IPixelBuffer>(buffer.release());
}

void MacOSRenderContext::Flush() {
    SafeExecute([this]() {
        if (cgContext) {
            CGContextFlush(cgContext);
        }
        
        if (doubleBufferingEnabled && doubleBuffer) {
            doubleBuffer->SwapBuffers();
        }
    });
}

// ===== ADVANCED OPERATIONS =====
void MacOSRenderContext::SetBlendMode(BlendMode mode) {
    SafeExecute([this, mode]() {
        if (cgContext) {
            CGBlendMode cgMode = UCBlendModeToCGBlendMode(mode);
            CGContextSetBlendMode(cgContext, cgMode);
        }
    });
}

void MacOSRenderContext::DrawGradient(const std::vector<Color>& colors, const std::vector<float>& stops, 
                                     float startX, float startY, float endX, float endY) {
    SafeExecute([this, &colors, &stops, startX, startY, endX, endY]() {
        if (cgContext && !colors.empty()) {
            CGGradientRef gradient = nullptr;
            CGColorSpaceRef gradientColorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
            
            // Create color array
            CFMutableArrayRef colorArray = CFArrayCreateMutable(nullptr, colors.size(), &kCFTypeArrayCallBacks);
            
            for (const auto& color : colors) {
                CGColorRef cgColor = CreateCGColor(color);
                CFArrayAppendValue(colorArray, cgColor);
                CGColorRelease(cgColor);
            }
            
            // Create gradient
            const float* locations = stops.empty() ? nullptr : stops.data();
            gradient = CGGradientCreateWithColors(gradientColorSpace, colorArray, locations);
            
            if (gradient) {
                CGPoint startPoint = CGPointMake(startX, startY);
                CGPoint endPoint = CGPointMake(endX, endY);
                CGContextDrawLinearGradient(cgContext, gradient, startPoint, endPoint, 0);
                CGGradientRelease(gradient);
            }
            
            CFRelease(colorArray);
            CGColorSpaceRelease(gradientColorSpace);
        }
    });
}

void MacOSRenderContext::EnableDoubleBuffering(bool enable) {
    doubleBufferingEnabled = enable;
    
    if (enable && !doubleBuffer && cgContext) {
        doubleBuffer = std::make_unique<MacOSCoreGraphicsDoubleBuffer>();
        doubleBuffer->Initialize(viewportWidth, viewportHeight, cgContext);
    } else if (!enable && doubleBuffer) {
        doubleBuffer.reset();
    }
}

void MacOSRenderContext::SetOwningThread() {
    owningThread = std::this_thread::get_id();
}

// ===== HELPER METHODS =====
CGColorRef MacOSRenderContext::CreateCGColor(const Color& color) {
    if (!colorSpace) CreateColorSpace();
    
    CGFloat components[4] = {color.r, color.g, color.b, color.a * globalAlpha};
    return CGColorCreate(colorSpace, components);
}

void MacOSRenderContext::UpdateFillColor(const Color& color) {
    SafeExecute([this, &color]() {
        if (cgContext) {
            if (cachedFillColor) {
                CGColorRelease(cachedFillColor);
            }
            cachedFillColor = CreateCGColor(color);
            CGContextSetColorWithColor(cgContext, cachedFillColor);
        }
    });
}

void MacOSRenderContext::UpdateStrokeColor(const Color& color) {
    SafeExecute([this, &color]() {
        if (cgContext) {
            if (cachedStrokeColor) {
                CGColorRelease(cachedStrokeColor);
            }
            cachedStrokeColor = CreateCGColor(color);
            CGContextSetColorWithColor(cgContext, cachedStrokeColor);
        }
    });
}

void MacOSRenderContext::ReleaseCachedColors() {
    if (cachedFillColor) {
        CGColorRelease(cachedFillColor);
        cachedFillColor = nullptr;
    }
    
    if (cachedStrokeColor) {
        CGColorRelease(cachedStrokeColor);
        cachedStrokeColor = nullptr;
    }
}

CTFontRef MacOSRenderContext::GetOrCreateFont(const std::string& fontName, float fontSize) {
    // Check cache
    if (cachedFont && cachedFontName == fontName && cachedFontSize == fontSize) {
        return cachedFont;
    }
    
    // Release old cached font
    if (cachedFont) {
        CFRelease(cachedFont);
        cachedFont = nullptr;
    }
    
    // Create new font
    CFStringRef fontNameString = CFStringCreateWithCString(nullptr, fontName.c_str(), kCFStringEncodingUTF8);
    cachedFont = CTFontCreateWithName(fontNameString, fontSize, nullptr);
    
    // If font creation failed, use system font
    if (!cachedFont) {
        cachedFont = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, fontSize, nullptr);
    }
    
    CFRelease(fontNameString);
    
    cachedFontName = fontName;
    cachedFontSize = fontSize;
    
    return cachedFont;
}

void MacOSRenderContext::ReleaseFontCache() {
    if (cachedFont) {
        CFRelease(cachedFont);
        cachedFont = nullptr;
    }
    cachedFontName.clear();
    cachedFontSize = 0;
}

void MacOSRenderContext::CreatePathIfNeeded() {
    if (!currentPath) {
        currentPath = CGPathCreateMutable();
    }
}

void MacOSRenderContext::ApplyCurrentPath() {
    if (currentPath && cgContext) {
        CGContextAddPath(cgContext, currentPath);
    }
}

// ===== COORDINATE CONVERSION =====
CGPoint MacOSRenderContext::UCPointToCGPoint(const Point2D& point) {
    return CGPointMake(point.x, point.y);
}

CGRect MacOSRenderContext::UCRectToCGRect(const Rect2D& rect) {
    return CGRectMake(rect.x, rect.y, rect.width, rect.height);
}

Point2D MacOSRenderContext::CGPointToUCPoint(CGPoint point) {
    return Point2D(point.x, point.y);
}

Rect2D MacOSRenderContext::CGRectToUCRect(CGRect rect) {
    return Rect2D(rect.origin.x, rect.origin.y, rect.size.width, rect.size.height);
}

// ===== BLEND MODE CONVERSION =====
CGBlendMode MacOSRenderContext::UCBlendModeToCGBlendMode(BlendMode mode) {
    switch (mode) {
        case BlendMode::Normal: return kCGBlendModeNormal;
        case BlendMode::Multiply: return kCGBlendModeMultiply;
        case BlendMode::Screen: return kCGBlendModeScreen;
        case BlendMode::Overlay: return kCGBlendModeOverlay;
        case BlendMode::Darken: return kCGBlendModeDarken;
        case BlendMode::Lighten: return kCGBlendModeLighten;
        case BlendMode::ColorDodge: return kCGBlendModeColorDodge;
        case BlendMode::ColorBurn: return kCGBlendModeColorBurn;
        case BlendMode::HardLight: return kCGBlendModeHardLight;
        case BlendMode::SoftLight: return kCGBlendModeSoftLight;
        case BlendMode::Difference: return kCGBlendModeDifference;
        case BlendMode::Exclusion: return kCGBlendModeExclusion;
        default: return kCGBlendModeNormal;
    }
}

// ===== CLEANUP =====
void MacOSRenderContext::Cleanup() {
    ReleaseCachedColors();
    ReleaseFontCache();
    
    if (currentPath) {
        CGPathRelease(currentPath);
        currentPath = nullptr;
    }
    
    if (colorSpace) {
        CGColorSpaceRelease(colorSpace);
        colorSpace = nullptr;
    }
    
    if (doubleBuffer) {
        doubleBuffer.reset();
    }
    
    if (ownsContext && cgContext) {
        CGContextRelease(cgContext);
        cgContext = nullptr;
    }
}

} // namespace UltraCanvas