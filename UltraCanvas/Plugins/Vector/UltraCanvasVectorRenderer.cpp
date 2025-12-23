// UltraCanvasVectorRenderer.cpp
// Vector Graphics Rendering Engine for UltraCanvas
// Version: 1.0.0
// Last Modified: 2025-01-20
// Author: UltraCanvas Framework

#include "UltraCanvasVectorRenderer.h"
#include "UltraCanvasVectorStorage.h"
#include "UltraCanvasRenderContext.h"
#include <stack>
#include <cmath>
#include <algorithm>

namespace UltraCanvas {

// ===== VECTOR RENDERER IMPLEMENTATION =====

class VectorRenderer::Impl {
public:
    // Rendering options
    struct RenderOptions {
        bool EnableAntialiasing = true;
        bool EnableSubpixelRendering = false;
        bool EnableGPUAcceleration = true;
        bool RenderInvisibleElements = false;
        bool DebugMode = false;
        float PixelRatio = 1.0f;  // For high-DPI displays
        Rect2Df ViewportBounds;    // For culling
        bool EnableCulling = true;
        
        // Quality settings
        int CurveSubdivisions = 16;
        float GradientQuality = 1.0f;
        float TextQuality = 1.0f;
    } options;
    
    // Render statistics
    struct RenderStats {
        uint32_t ElementsRendered = 0;
        uint32_t ElementsCulled = 0;
        uint32_t PathCommandsProcessed = 0;
        uint32_t GradientsCreated = 0;
        uint32_t PatternsCreated = 0;
        double RenderTimeMs = 0.0;
        
        void Reset() {
            ElementsRendered = 0;
            ElementsCulled = 0;
            PathCommandsProcessed = 0;
            GradientsCreated = 0;
            PatternsCreated = 0;
            RenderTimeMs = 0.0;
        }
    } stats;
    
    // Current rendering state
    IRenderContext* context = nullptr;
    std::stack<Matrix3x3> transformStack;
    std::stack<float> opacityStack;
    float currentOpacity = 1.0f;
    
    // Resource caches
    std::map<size_t, GradientHandle> gradientCache;
    std::map<size_t, PatternHandle> patternCache;
    std::map<std::string, FontHandle> fontCache;
    
    // Main rendering methods
    void RenderDocument(IRenderContext* ctx, const VectorDocument& document);
    void RenderLayer(const VectorLayer& layer);
    void RenderElement(const VectorElement& element);
    
    // Element type renderers
    void RenderRectangle(const VectorRect& rect);
    void RenderCircle(const VectorCircle& circle);
    void RenderEllipse(const VectorEllipse& ellipse);
    void RenderLine(const VectorLine& line);
    void RenderPolyline(const VectorPolyline& polyline);
    void RenderPolygon(const VectorPolygon& polygon);
    void RenderPath(const VectorPath& path);
    void RenderText(const VectorText& text);
    void RenderImage(const VectorImage& image);
    void RenderGroup(const VectorGroup& group);
    void RenderUse(const VectorUse& use);
    
    // Style application
    void ApplyStyle(const VectorStyle& style, bool forStroke = false);
    void ApplyFill(const FillData& fill);
    void ApplyStroke(const StrokeData& stroke);
    void ApplyTransform(const Matrix3x3& transform);
    void ApplyOpacity(float opacity);
    void ApplyBlendMode(BlendMode mode);
    void ApplyClipPath(const std::string& clipPathId);
    void ApplyMask(const std::string& maskId);
    void ApplyFilter(const std::string& filterId);
    
    // Fill renderers
    void SetSolidFill(const Color& color);
    void SetGradientFill(const GradientData& gradient);
    void SetPatternFill(const PatternData& pattern);
    void SetImageFill(const std::string& imagePath);
    
    // Gradient renderers
    void RenderLinearGradient(const LinearGradientData& gradient);
    void RenderRadialGradient(const RadialGradientData& gradient);
    void RenderConicalGradient(const ConicalGradientData& gradient);
    void RenderMeshGradient(const MeshGradientData& gradient);
    
    // Path processing
    void ProcessPath(const PathData& pathData);
    void ProcessPathCommand(const PathCommand& cmd, Point2Df& currentPoint);
    
    // Utility methods
    bool IsElementVisible(const VectorElement& element) const;
    bool IsInViewport(const Rect2Df& bounds) const;
    Rect2Df GetElementBounds(const VectorElement& element) const;
    size_t HashGradient(const GradientData& gradient) const;
    size_t HashPattern(const PatternData& pattern) const;
    
    // Resource management
    void ClearCaches();
    void PreloadResources(const VectorDocument& document);
};

// ===== PUBLIC INTERFACE =====

VectorRenderer::VectorRenderer() : impl(std::make_unique<Impl>()) {}
VectorRenderer::~VectorRenderer() = default;

void VectorRenderer::RenderDocument(IRenderContext* context, const VectorDocument& document) {
    impl->RenderDocument(context, document);
}

void VectorRenderer::RenderElement(IRenderContext* context, const VectorElement& element) {
    impl->context = context;
    impl->RenderElement(element);
}

void VectorRenderer::SetRenderOptions(const RenderOptions& options) {
    impl->options = options;
}

VectorRenderer::RenderStats VectorRenderer::GetRenderStats() const {
    return impl->stats;
}

void VectorRenderer::ClearCaches() {
    impl->ClearCaches();
}



// ===== IMPLEMENTATION =====

void VectorRenderer::Impl::RenderDocument(IRenderContext* ctx, const VectorDocument& document) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    context = ctx;
    stats.Reset();
    
    // Save initial state
    context->Save();
    
    // Set up viewport if specified
    if (document.ViewBox.width > 0 && document.ViewBox.height > 0) {
        // Calculate viewport transform
        float scaleX = options.ViewportBounds.width / document.ViewBox.width;
        float scaleY = options.ViewportBounds.height / document.ViewBox.height;
        
        // Preserve aspect ratio based on document settings
        if (document.PreserveAspectRatio != "none") {
            float scale = std::min(scaleX, scaleY);
            scaleX = scaleY = scale;
            
            // Center in viewport
            float dx = (options.ViewportBounds.width - document.ViewBox.width * scale) / 2;
            float dy = (options.ViewportBounds.height - document.ViewBox.height * scale) / 2;
            context->Translate(dx, dy);
        }
        
        context->Scale(scaleX, scaleY);
        context->Translate(-document.ViewBox.x, -document.ViewBox.y);
    }
    
    // Apply high-DPI scaling
    if (options.PixelRatio != 1.0f) {
        context->Scale(options.PixelRatio, options.PixelRatio);
    }
    
    // Render background if specified
    if (document.BackgroundColor.has_value()) {
        context->SetFillColor(document.BackgroundColor.value());
        context->FillRectangle(
            document.ViewBox.x, document.ViewBox.y,
            document.ViewBox.width, document.ViewBox.height
        );
    }
    
    // Preload resources if enabled
    if (options.EnableGPUAcceleration) {
        PreloadResources(document);
    }
    
    // Render all layers
    for (const auto& layer : document.Layers) {
        if (layer->Visible || options.RenderInvisibleElements) {
            RenderLayer(*layer);
        }
    }
    
    // Restore initial state
    context->Restore();
    
    // Calculate render time
    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration = endTime - startTime;
    stats.RenderTimeMs = duration.count();
}

void VectorRenderer::Impl::RenderLayer(const VectorLayer& layer) {
    // Save state for layer
    context->Save();
    
    // Apply layer opacity
    float layerOpacity = layer.Opacity * currentOpacity;
    opacityStack.push(currentOpacity);
    currentOpacity = layerOpacity;
    
    // Apply layer blend mode if specified
    if (layer.BlendMode != BlendMode::Normal) {
        ApplyBlendMode(layer.BlendMode);
    }
    
    // Apply layer transform if present
    if (layer.Transform.has_value()) {
        ApplyTransform(layer.Transform.value());
    }
    
    // Render all children
    for (const auto& child : layer.Children) {
        RenderElement(*child);
    }
    
    // Restore opacity
    currentOpacity = opacityStack.top();
    opacityStack.pop();
    
    // Restore state
    context->Restore();
}

void VectorRenderer::Impl::RenderElement(const VectorElement& element) {
    // Check visibility
    if (!IsElementVisible(element)) {
        stats.ElementsCulled++;
        return;
    }
    
    // Check viewport culling
    if (options.EnableCulling) {
        Rect2Df bounds = GetElementBounds(element);
        if (!IsInViewport(bounds)) {
            stats.ElementsCulled++;
            return;
        }
    }
    
    // Save state
    context->Save();
    
    // Apply element transform
    if (element.Transform.has_value()) {
        ApplyTransform(element.Transform.value());
    }
    
    // Apply element opacity
    float elementOpacity = element.Style.Opacity * currentOpacity;
    ApplyOpacity(elementOpacity);
    
    // Apply clip path if specified
    if (!element.Style.ClipPath.empty()) {
        ApplyClipPath(element.Style.ClipPath);
    }
    
    // Apply mask if specified
    if (!element.Style.Mask.empty()) {
        ApplyMask(element.Style.Mask);
    }
    
    // Apply filters if specified
    for (const auto& filter : element.Style.Filters) {
        ApplyFilter(filter);
    }
    
    // Render based on element type
    switch (element.Type) {
        case VectorElementType::Rectangle:
        case VectorElementType::RoundedRectangle:
            RenderRectangle(static_cast<const VectorRect&>(element));
            break;
            
        case VectorElementType::Circle:
            RenderCircle(static_cast<const VectorCircle&>(element));
            break;
            
        case VectorElementType::Ellipse:
            RenderEllipse(static_cast<const VectorEllipse&>(element));
            break;
            
        case VectorElementType::Line:
            RenderLine(static_cast<const VectorLine&>(element));
            break;
            
        case VectorElementType::Polyline:
            RenderPolyline(static_cast<const VectorPolyline&>(element));
            break;
            
        case VectorElementType::Polygon:
            RenderPolygon(static_cast<const VectorPolygon&>(element));
            break;
            
        case VectorElementType::Path:
            RenderPath(static_cast<const VectorPath&>(element));
            break;
            
        case VectorElementType::Text:
            RenderText(static_cast<const VectorText&>(element));
            break;
            
        case VectorElementType::Image:
            RenderImage(static_cast<const VectorImage&>(element));
            break;
            
        case VectorElementType::Group:
        case VectorElementType::Layer:
            RenderGroup(static_cast<const VectorGroup&>(element));
            break;
            
        case VectorElementType::Use:
            RenderUse(static_cast<const VectorUse&>(element));
            break;
            
        default:
            if (options.DebugMode) {
                // Draw placeholder for unknown element types
                context->SetStrokeColor(Colors::Red);
                context->SetLineWidth(1.0f);
                Rect2Df bounds = GetElementBounds(element);
                context->DrawRectangle(bounds.x, bounds.y, bounds.width, bounds.height);
            }
            break;
    }
    
    // Restore state
    context->Restore();
    
    stats.ElementsRendered++;
}

// ===== ELEMENT RENDERERS =====

void VectorRenderer::Impl::RenderRectangle(const VectorRect& rect) {
    context->BeginPath();
    
    if (rect.RadiusX > 0 || rect.RadiusY > 0) {
        // Rounded rectangle
        float rx = rect.RadiusX;
        float ry = rect.RadiusY > 0 ? rect.RadiusY : rect.RadiusX;
        
        float x = rect.Bounds.x;
        float y = rect.Bounds.y;
        float w = rect.Bounds.width;
        float h = rect.Bounds.height;
        
        // Clamp radii to half of dimensions
        rx = std::min(rx, w / 2);
        ry = std::min(ry, h / 2);
        
        context->MoveTo(x + rx, y);
        context->LineTo(x + w - rx, y);
        context->QuadraticCurveTo(x + w, y, x + w, y + ry);
        context->LineTo(x + w, y + h - ry);
        context->QuadraticCurveTo(x + w, y + h, x + w - rx, y + h);
        context->LineTo(x + rx, y + h);
        context->QuadraticCurveTo(x, y + h, x, y + h - ry);
        context->LineTo(x, y + ry);
        context->QuadraticCurveTo(x, y, x + rx, y);
        context->ClosePath();
    } else {
        // Regular rectangle
        context->Rect(rect.Bounds.x, rect.Bounds.y, rect.Bounds.width, rect.Bounds.height);
    }
    
    // Apply style and render
    ApplyStyle(rect.Style);
    
    if (rect.Style.Fill.has_value()) {
        ApplyFill(rect.Style.Fill.value());
        context->Fill();
    }
    
    if (rect.Style.Stroke.has_value()) {
        ApplyStroke(rect.Style.Stroke.value());
        context->Stroke();
    }
}

void VectorRenderer::Impl::RenderCircle(const VectorCircle& circle) {
    context->BeginPath();
    context->Arc(circle.Center.x, circle.Center.y, circle.Radius, 0, 2 * M_PI);
    context->ClosePath();
    
    ApplyStyle(circle.Style);
    
    if (circle.Style.Fill.has_value()) {
        ApplyFill(circle.Style.Fill.value());
        context->Fill();
    }
    
    if (circle.Style.Stroke.has_value()) {
        ApplyStroke(circle.Style.Stroke.value());
        context->Stroke();
    }
}

void VectorRenderer::Impl::RenderEllipse(const VectorEllipse& ellipse) {
    context->BeginPath();
    
    // Save current transform
    context->Save();
    
    // Apply ellipse transform
    context->Translate(ellipse.Center.x, ellipse.Center.y);
    context->Scale(ellipse.RadiusX, ellipse.RadiusY);
    
    // Draw unit circle
    context->Arc(0, 0, 1, 0, 2 * M_PI);
    context->ClosePath();
    
    // Restore transform
    context->Restore();
    
    ApplyStyle(ellipse.Style);
    
    if (ellipse.Style.Fill.has_value()) {
        ApplyFill(ellipse.Style.Fill.value());
        context->Fill();
    }
    
    if (ellipse.Style.Stroke.has_value()) {
        ApplyStroke(ellipse.Style.Stroke.value());
        context->Stroke();
    }
}

void VectorRenderer::Impl::RenderLine(const VectorLine& line) {
    context->BeginPath();
    context->MoveTo(line.Start.x, line.Start.y);
    context->LineTo(line.End.x, line.End.y);
    
    // Lines only have stroke, no fill
    if (line.Style.Stroke.has_value()) {
        ApplyStroke(line.Style.Stroke.value());
        context->Stroke();
    } else {
        // Default stroke if none specified
        context->SetStrokeColor(Colors::Black);
        context->SetLineWidth(1.0f);
        context->Stroke();
    }
}

void VectorRenderer::Impl::RenderPolyline(const VectorPolyline& polyline) {
    if (polyline.Points.empty()) return;
    
    context->BeginPath();
    context->MoveTo(polyline.Points[0].x, polyline.Points[0].y);
    
    for (size_t i = 1; i < polyline.Points.size(); i++) {
        context->LineTo(polyline.Points[i].x, polyline.Points[i].y);
    }
    
    // Polylines are not closed
    ApplyStyle(polyline.Style);
    
    if (polyline.Style.Stroke.has_value()) {
        ApplyStroke(polyline.Style.Stroke.value());
        context->Stroke();
    }
}

void VectorRenderer::Impl::RenderPolygon(const VectorPolygon& polygon) {
    if (polygon.Points.empty()) return;
    
    context->BeginPath();
    context->MoveTo(polygon.Points[0].x, polygon.Points[0].y);
    
    for (size_t i = 1; i < polygon.Points.size(); i++) {
        context->LineTo(polygon.Points[i].x, polygon.Points[i].y);
    }
    
    context->ClosePath();
    
    ApplyStyle(polygon.Style);
    
    if (polygon.Style.Fill.has_value()) {
        ApplyFill(polygon.Style.Fill.value());
        if (polygon.Style.ClipRule == FillRule::EvenOdd) {
            context->SetFillRule(FillRule::EvenOdd);
        }
        context->Fill();
    }
    
    if (polygon.Style.Stroke.has_value()) {
        ApplyStroke(polygon.Style.Stroke.value());
        context->Stroke();
    }
}

void VectorRenderer::Impl::RenderPath(const VectorPath& path) {
    context->BeginPath();
    ProcessPath(path.Path);
    
    ApplyStyle(path.Style);
    
    if (path.Style.Fill.has_value()) {
        ApplyFill(path.Style.Fill.value());
        if (path.Style.ClipRule == FillRule::EvenOdd) {
            context->SetFillRule(FillRule::EvenOdd);
        }
        context->Fill();
    }
    
    if (path.Style.Stroke.has_value()) {
        ApplyStroke(path.Style.Stroke.value());
        context->Stroke();
    }
}

void VectorRenderer::Impl::ProcessPath(const PathData& pathData) {
    Point2Df currentPoint{0, 0};
    Point2Df pathStart{0, 0};
    Point2Df lastControlPoint{0, 0};
    
    for (const auto& cmd : pathData.Commands) {
        ProcessPathCommand(cmd, currentPoint);
        stats.PathCommandsProcessed++;
        
        // Track path start for close path
        if (cmd.Type == PathCommandType::MoveTo) {
            pathStart = currentPoint;
        } else if (cmd.Type == PathCommandType::ClosePath) {
            currentPoint = pathStart;
        }
    }
}

void VectorRenderer::Impl::ProcessPathCommand(const PathCommand& cmd, Point2Df& currentPoint) {
    switch (cmd.Type) {
        case PathCommandType::MoveTo: {
            float x = cmd.Parameters[0];
            float y = cmd.Parameters[1];
            if (cmd.Relative) {
                x += currentPoint.x;
                y += currentPoint.y;
            }
            context->MoveTo(x, y);
            currentPoint = {x, y};
            break;
        }
        
        case PathCommandType::LineTo: {
            float x = cmd.Parameters[0];
            float y = cmd.Parameters[1];
            if (cmd.Relative) {
                x += currentPoint.x;
                y += currentPoint.y;
            }
            context->LineTo(x, y);
            currentPoint = {x, y};
            break;
        }
        
        case PathCommandType::CurveTo: {
            float x1 = cmd.Parameters[0];
            float y1 = cmd.Parameters[1];
            float x2 = cmd.Parameters[2];
            float y2 = cmd.Parameters[3];
            float x3 = cmd.Parameters[4];
            float y3 = cmd.Parameters[5];
            
            if (cmd.Relative) {
                x1 += currentPoint.x;
                y1 += currentPoint.y;
                x2 += currentPoint.x;
                y2 += currentPoint.y;
                x3 += currentPoint.x;
                y3 += currentPoint.y;
            }
            
            context->BezierCurveTo(x1, y1, x2, y2, x3, y3);
            currentPoint = {x3, y3};
            break;
        }
        
        case PathCommandType::QuadraticTo: {
            float x1 = cmd.Parameters[0];
            float y1 = cmd.Parameters[1];
            float x2 = cmd.Parameters[2];
            float y2 = cmd.Parameters[3];
            
            if (cmd.Relative) {
                x1 += currentPoint.x;
                y1 += currentPoint.y;
                x2 += currentPoint.x;
                y2 += currentPoint.y;
            }
            
            context->QuadraticCurveTo(x1, y1, x2, y2);
            currentPoint = {x2, y2};
            break;
        }
        
        case PathCommandType::ArcTo: {
            float rx = cmd.Parameters[0];
            float ry = cmd.Parameters[1];
            float rotation = cmd.Parameters[2];
            bool largeArc = cmd.Parameters[3] > 0.5f;
            bool sweep = cmd.Parameters[4] > 0.5f;
            float x = cmd.Parameters[5];
            float y = cmd.Parameters[6];
            
            if (cmd.Relative) {
                x += currentPoint.x;
                y += currentPoint.y;
            }
            
            // Convert SVG arc to center parameterization and render
            RenderSVGArc(currentPoint, {x, y}, rx, ry, rotation, largeArc, sweep);
            currentPoint = {x, y};
            break;
        }
        
        case PathCommandType::ClosePath:
            context->ClosePath();
            break;
    }
}

void VectorRenderer::Impl::RenderSVGArc(const Point2Df& start, const Point2Df& end,
                                        float rx, float ry, float rotation,
                                        bool largeArc, bool sweep) {
    // Convert SVG arc to center parameterization
    // This is complex math - simplified version for now
    
    if (rx == 0 || ry == 0) {
        // Degenerate case - just draw a line
        context->LineTo(end.x, end.y);
        return;
    }
    
    // For now, approximate with bezier curves
    // Full implementation would convert endpoint to center parameterization
    
    float cx = (start.x + end.x) / 2;
    float cy = (start.y + end.y) / 2;
    
    // Simple arc approximation
    context->Save();
    context->Translate(cx, cy);
    context->Rotate(rotation * M_PI / 180);
    context->Scale(rx, ry);
    
    // Draw arc (simplified)
    float startAngle = atan2(start.y - cy, start.x - cx);
    float endAngle = atan2(end.y - cy, end.x - cx);
    
    if (sweep) {
        if (endAngle < startAngle) endAngle += 2 * M_PI;
    } else {
        if (startAngle < endAngle) startAngle += 2 * M_PI;
    }
    
    context->Arc(0, 0, 1, startAngle, endAngle);
    context->Restore();
}

// ===== TEXT RENDERING =====

void VectorRenderer::Impl::RenderText(const VectorText& text) {
    if (text.Spans.empty()) return;
    
    // Set base font
    context->SetFont(text.BaseStyle.FontFamily, text.BaseStyle.FontSize);
    
    // Apply text style
    if (text.BaseStyle.Weight == FontWeight::Bold) {
        context->SetFontWeight(FontWeight::Bold);
    }
    if (text.BaseStyle.Style == FontStyle::Italic) {
        context->SetFontStyle(FontStyle::Italic);
    }
    
    // Current text position
    float x = text.Position.x;
    float y = text.Position.y;
    
    // Render each span
    for (const auto& span : text.Spans) {
        // Apply span-specific style if different
        if (span.Style.FontFamily != text.BaseStyle.FontFamily ||
            span.Style.FontSize != text.BaseStyle.FontSize) {
            context->SetFont(span.Style.FontFamily, span.Style.FontSize);
        }
        
        // Apply span color
        if (text.Style.Fill.has_value()) {
            ApplyFill(text.Style.Fill.value());
        } else {
            context->SetFillColor(Colors::Black);
        }
        
        // Apply span position if specified
        if (span.Position.has_value()) {
            x = span.Position->x;
            y = span.Position->y;
        }
        
        // Apply delta positioning
        for (size_t i = 0; i < span.Text.length() && i < span.DeltaX.size(); i++) {
            x += span.DeltaX[i];
        }
        for (size_t i = 0; i < span.Text.length() && i < span.DeltaY.size(); i++) {
            y += span.DeltaY[i];
        }
        
        // Draw text
        context->DrawText(span.Text, x, y);
        
        // Advance position (simplified - should measure text width)
        x += span.Text.length() * text.BaseStyle.FontSize * 0.6f;
    }
    
    // Render text decorations
    if (text.BaseStyle.Decoration & TextDecoration::Underline) {
        float lineY = y + text.BaseStyle.FontSize * 0.1f;
        context->MoveTo(text.Position.x, lineY);
        context->LineTo(x, lineY);
        context->Stroke();
    }
    
    if (text.BaseStyle.Decoration & TextDecoration::Overline) {
        float lineY = y - text.BaseStyle.FontSize * 0.8f;
        context->MoveTo(text.Position.x, lineY);
        context->LineTo(x, lineY);
        context->Stroke();
    }
    
    if (text.BaseStyle.Decoration & TextDecoration::LineThrough) {
        float lineY = y - text.BaseStyle.FontSize * 0.3f;
        context->MoveTo(text.Position.x, lineY);
        context->LineTo(x, lineY);
        context->Stroke();
    }
}

// ===== IMAGE RENDERING =====

void VectorRenderer::Impl::RenderImage(const VectorImage& image) {
    // Check if image source is available
    if (image.Source.empty() && !image.ImageData) {
        return;
    }
    
    // Handle embedded image data
    if (image.ImageData && !image.ImageData->empty()) {
        // Create temporary pixel buffer from image data
        // This would need proper image decoding
        // For now, skip embedded images
        return;
    }
    
    // Draw external image file
    context->DrawImage(image.Source, 
                      image.Bounds.x, image.Bounds.y,
                      image.Bounds.width, image.Bounds.height);
    
    // Apply opacity if needed
    if (image.Style.Opacity < 1.0f) {
        // Most render contexts handle this internally
    }
}

// ===== GROUP RENDERING =====

void VectorRenderer::Impl::RenderGroup(const VectorGroup& group) {
    // Groups just render their children with inherited transform/style
    for (const auto& child : group.Children) {
        RenderElement(*child);
    }
}

void VectorRenderer::Impl::RenderUse(const VectorUse& use) {
    // Use element references another element by ID
    // This would require access to the document's definition map
    // For now, just render as a placeholder
    
    if (options.DebugMode) {
        context->SetStrokeColor(Colors::Blue);
        context->SetLineWidth(1.0f);
        context->DrawRectangle(use.Position.x, use.Position.y,
                              use.Size.width > 0 ? use.Size.width : 100,
                              use.Size.height > 0 ? use.Size.height : 100);
    }
}

// ===== STYLE APPLICATION =====

void VectorRenderer::Impl::ApplyStyle(const VectorStyle& style, bool forStroke) {
    // Apply blend mode
    if (style.BlendMode != BlendMode::Normal) {
        ApplyBlendMode(style.BlendMode);
    }
    
    // Opacity is handled per-element
    // Fill and stroke are handled by specific element renderers
}

void VectorRenderer::Impl::ApplyFill(const FillData& fill) {
    if (auto* color = std::get_if<Color>(&fill)) {
        SetSolidFill(*color);
    } else if (auto* gradient = std::get_if<GradientData>(&fill)) {
        SetGradientFill(*gradient);
    } else if (auto* pattern = std::get_if<PatternData>(&fill)) {
        SetPatternFill(*pattern);
    } else if (auto* image = std::get_if<std::string>(&fill)) {
        SetImageFill(*image);
    }
}

void VectorRenderer::Impl::SetSolidFill(const Color& color) {
    // Apply opacity to color
    Color finalColor = color;
    finalColor.a = static_cast<uint8_t>(color.a * currentOpacity);
    context->SetFillColor(finalColor);
}

void VectorRenderer::Impl::SetGradientFill(const GradientData& gradient) {
    if (auto* linear = std::get_if<LinearGradientData>(&gradient)) {
        RenderLinearGradient(*linear);
    } else if (auto* radial = std::get_if<RadialGradientData>(&gradient)) {
        RenderRadialGradient(*radial);
    } else if (auto* conical = std::get_if<ConicalGradientData>(&gradient)) {
        RenderConicalGradient(*conical);
    } else if (auto* mesh = std::get_if<MeshGradientData>(&gradient)) {
        RenderMeshGradient(*mesh);
    }
    stats.GradientsCreated++;
}

void VectorRenderer::Impl::RenderLinearGradient(const LinearGradientData& gradient) {
    // Check cache first
    size_t hash = HashGradient(gradient);
    auto it = gradientCache.find(hash);
    
    if (it != gradientCache.end()) {
        context->SetFillGradient(it->second);
        return;
    }
    
    // Create new gradient
    std::vector<GradientStop> stops = gradient.Stops;
    
    // Ensure we have at least 2 stops
    if (stops.empty()) {
        stops.push_back({0.0f, Colors::Black, 1.0f});
        stops.push_back({1.0f, Colors::White, 1.0f});
    } else if (stops.size() == 1) {
        stops.push_back({1.0f, stops[0].StopColor, stops[0].Opacity});
    }
    
    // Apply gradient transform if present
    if (gradient.Transform.has_value()) {
        context->Save();
        ApplyTransform(gradient.Transform.value());
    }
    
    // Set gradient
    context->SetLinearGradient(
        gradient.Start.x, gradient.Start.y,
        gradient.End.x, gradient.End.y,
        stops
    );
    
    if (gradient.Transform.has_value()) {
        context->Restore();
    }
    
    // Cache for reuse
    // gradientCache[hash] = gradientHandle;
}

void VectorRenderer::Impl::RenderRadialGradient(const RadialGradientData& gradient) {
    std::vector<GradientStop> stops = gradient.Stops;
    
    // Ensure we have stops
    if (stops.empty()) {
        stops.push_back({0.0f, Colors::White, 1.0f});
        stops.push_back({1.0f, Colors::Black, 1.0f});
    }
    
    // Apply transform
    if (gradient.Transform.has_value()) {
        context->Save();
        ApplyTransform(gradient.Transform.value());
    }
    
    // Set radial gradient
    context->SetRadialGradient(
        gradient.Center.x, gradient.Center.y, gradient.Radius,
        gradient.FocalPoint.x, gradient.FocalPoint.y,
        stops
    );
    
    if (gradient.Transform.has_value()) {
        context->Restore();
    }
}

void VectorRenderer::Impl::RenderConicalGradient(const ConicalGradientData& gradient) {
    // Conical gradients are less common - approximate with radial
    RadialGradientData radial;
    radial.Center = gradient.Center;
    radial.FocalPoint = gradient.Center;
    radial.Radius = 100;  // Arbitrary
    radial.Stops = gradient.Stops;
    
    RenderRadialGradient(radial);
}

void VectorRenderer::Impl::RenderMeshGradient(const MeshGradientData& mesh) {
    // Mesh gradients are complex - would need tessellation
    // For now, render as average color
    
    if (!mesh.Patches.empty() && !mesh.Patches[0].Colors.empty()) {
        Color avgColor = mesh.Patches[0].Colors[0];
        SetSolidFill(avgColor);
    } else {
        SetSolidFill(Colors::Gray);
    }
}

void VectorRenderer::Impl::ApplyStroke(const StrokeData& stroke) {
    // Set stroke color/pattern
    if (auto* color = std::get_if<Color>(&stroke.Fill)) {
        Color finalColor = *color;
        finalColor.a = static_cast<uint8_t>(color->a * stroke.Opacity * currentOpacity);
        context->SetStrokeColor(finalColor);
    } else if (auto* gradient = std::get_if<GradientData>(&stroke.Fill)) {
        // Apply gradient to stroke
        SetGradientFill(*gradient);
        // Note: Not all render contexts support gradient strokes
    }
    
    // Set line width
    context->SetLineWidth(stroke.Width);
    
    // Set line cap
    context->SetLineCap(stroke.LineCap);
    
    // Set line join
    context->SetLineJoin(stroke.LineJoin);
    
    // Set miter limit
    context->SetMiterLimit(stroke.MiterLimit);
    
    // Set dash pattern
    if (!stroke.DashArray.empty()) {
        context->SetLineDash(stroke.DashArray.data(), stroke.DashArray.size());
        context->SetLineDashOffset(stroke.DashOffset);
    } else {
        // Clear dash pattern
        context->SetLineDash(nullptr, 0);
    }
}

void VectorRenderer::Impl::ApplyTransform(const Matrix3x3& transform) {
    context->Transform(
        transform.m[0][0], transform.m[1][0],
        transform.m[0][1], transform.m[1][1],
        transform.m[0][2], transform.m[1][2]
    );
}

void VectorRenderer::Impl::ApplyOpacity(float opacity) {
    // Most modern render contexts support global alpha
    context->SetGlobalAlpha(opacity);
}

void VectorRenderer::Impl::ApplyBlendMode(BlendMode mode) {
    // Map to render context blend modes
    switch (mode) {
        case BlendMode::Normal:
            context->SetCompositeOperation(CompositeOp::SourceOver);
            break;
        case BlendMode::Multiply:
            context->SetCompositeOperation(CompositeOp::Multiply);
            break;
        case BlendMode::Screen:
            context->SetCompositeOperation(CompositeOp::Screen);
            break;
        case BlendMode::Overlay:
            context->SetCompositeOperation(CompositeOp::Overlay);
            break;
        case BlendMode::Darken:
            context->SetCompositeOperation(CompositeOp::Darken);
            break;
        case BlendMode::Lighten:
            context->SetCompositeOperation(CompositeOp::Lighten);
            break;
        default:
            context->SetCompositeOperation(CompositeOp::SourceOver);
            break;
    }
}

// ===== UTILITY METHODS =====

bool VectorRenderer::Impl::IsElementVisible(const VectorElement& element) const {
    if (!element.Style.Visible || !element.Style.Display) {
        return false;
    }
    
    if (element.Style.Opacity <= 0.0f) {
        return false;
    }
    
    // Check if element has any visual content
    bool hasFill = element.Style.Fill.has_value();
    bool hasStroke = element.Style.Stroke.has_value();
    
    // Text and images are always visible if display is true
    if (element.Type == VectorElementType::Text ||
        element.Type == VectorElementType::Image) {
        return true;
    }
    
    // Groups are visible if they have children
    if (element.Type == VectorElementType::Group ||
        element.Type == VectorElementType::Layer) {
        const auto& group = static_cast<const VectorGroup&>(element);
        return !group.Children.empty();
    }
    
    return hasFill || hasStroke;
}

bool VectorRenderer::Impl::IsInViewport(const Rect2Df& bounds) const {
    if (options.ViewportBounds.width <= 0 || options.ViewportBounds.height <= 0) {
        return true;  // No viewport set, render everything
    }
    
    // Check if bounds intersect with viewport
    return !(bounds.x + bounds.width < options.ViewportBounds.x ||
            bounds.y + bounds.height < options.ViewportBounds.y ||
            bounds.x > options.ViewportBounds.x + options.ViewportBounds.width ||
            bounds.y > options.ViewportBounds.y + options.ViewportBounds.height);
}

Rect2Df VectorRenderer::Impl::GetElementBounds(const VectorElement& element) const {
    // Calculate bounds based on element type
    switch (element.Type) {
        case VectorElementType::Rectangle:
        case VectorElementType::RoundedRectangle: {
            const auto& rect = static_cast<const VectorRect&>(element);
            return rect.Bounds;
        }
        
        case VectorElementType::Circle: {
            const auto& circle = static_cast<const VectorCircle&>(element);
            return {
                circle.Center.x - circle.Radius,
                circle.Center.y - circle.Radius,
                circle.Radius * 2,
                circle.Radius * 2
            };
        }
        
        case VectorElementType::Ellipse: {
            const auto& ellipse = static_cast<const VectorEllipse&>(element);
            return {
                ellipse.Center.x - ellipse.RadiusX,
                ellipse.Center.y - ellipse.RadiusY,
                ellipse.RadiusX * 2,
                ellipse.RadiusY * 2
            };
        }
        
        case VectorElementType::Line: {
            const auto& line = static_cast<const VectorLine&>(element);
            float minX = std::min(line.Start.x, line.End.x);
            float minY = std::min(line.Start.y, line.End.y);
            float maxX = std::max(line.Start.x, line.End.x);
            float maxY = std::max(line.Start.y, line.End.y);
            return {minX, minY, maxX - minX, maxY - minY};
        }
        
        case VectorElementType::Path: {
            const auto& path = static_cast<const VectorPath&>(element);
            return path.Path.GetBounds();
        }
        
        case VectorElementType::Text: {
            const auto& text = static_cast<const VectorText&>(element);
            // Approximate text bounds
            float width = text.GetPlainText().length() * text.BaseStyle.FontSize * 0.6f;
            float height = text.BaseStyle.FontSize * 1.2f;
            return {text.Position.x, text.Position.y - height, width, height};
        }
        
        case VectorElementType::Image: {
            const auto& image = static_cast<const VectorImage&>(element);
            return image.Bounds;
        }
        
        case VectorElementType::Group:
        case VectorElementType::Layer: {
            const auto& group = static_cast<const VectorGroup&>(element);
            if (group.Children.empty()) {
                return {0, 0, 0, 0};
            }
            
            // Calculate union of all children bounds
            Rect2Df bounds = GetElementBounds(*group.Children[0]);
            for (size_t i = 1; i < group.Children.size(); i++) {
                Rect2Df childBounds = GetElementBounds(*group.Children[i]);
                
                float minX = std::min(bounds.x, childBounds.x);
                float minY = std::min(bounds.y, childBounds.y);
                float maxX = std::max(bounds.x + bounds.width, childBounds.x + childBounds.width);
                float maxY = std::max(bounds.y + bounds.height, childBounds.y + childBounds.height);
                
                bounds = {minX, minY, maxX - minX, maxY - minY};
            }
            return bounds;
        }
        
        default:
            return {0, 0, 100, 100};  // Default bounds
    }
}

size_t VectorRenderer::Impl::HashGradient(const GradientData& gradient) const {
    size_t hash = 0;
    
    // Hash gradient type and parameters
    if (auto* linear = std::get_if<LinearGradientData>(&gradient)) {
        hash = std::hash<float>{}(linear->Start.x) ^ 
               std::hash<float>{}(linear->Start.y) ^
               std::hash<float>{}(linear->End.x) ^
               std::hash<float>{}(linear->End.y);
        
        for (const auto& stop : linear->Stops) {
            hash ^= std::hash<float>{}(stop.Offset);
            hash ^= std::hash<uint32_t>{}(stop.StopColor.ToUInt32());
        }
    }
    // Add similar hashing for other gradient types
    
    return hash;
}

void VectorRenderer::Impl::ClearCaches() {
    gradientCache.clear();
    patternCache.clear();
    fontCache.clear();
}

void VectorRenderer::Impl::PreloadResources(const VectorDocument& document) {
    // Preload gradients, patterns, fonts, etc.
    // This can improve rendering performance by creating resources upfront
    
    // Scan all elements and create resources
    std::function<void(const VectorElement&)> scanElement = [&](const VectorElement& element) {
        // Preload fill resources
        if (element.Style.Fill.has_value()) {
            if (auto* gradient = std::get_if<GradientData>(&element.Style.Fill.value())) {
                size_t hash = HashGradient(*gradient);
                if (gradientCache.find(hash) == gradientCache.end()) {
                    // Create and cache gradient
                    // gradientCache[hash] = CreateGradient(*gradient);
                }
            }
        }
        
        // Scan children for groups
        if (element.Type == VectorElementType::Group ||
            element.Type == VectorElementType::Layer) {
            const auto& group = static_cast<const VectorGroup&>(element);
            for (const auto& child : group.Children) {
                scanElement(*child);
            }
        }
    };
    
    // Scan all layers
    for (const auto& layer : document.Layers) {
        for (const auto& element : layer->Children) {
            scanElement(*element);
        }
    }
}

} // namespace UltraCanvas
