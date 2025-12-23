// UltraCanvasVectorRenderer.h
// Vector Graphics Rendering Interface for UltraCanvas
// Version: 1.0.0
// Last Modified: 2025-01-20
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasVectorStorage.h"
#include "UltraCanvasRenderContext.h"
#include <stack>
#include <functional>

#define ULTRACANVAS_USE_CAIRO 1

namespace UltraCanvas {
namespace VectorRenderer {

using namespace VectorStorage;

// ===== RENDERING OPTIONS =====

struct RenderOptions {
    // Quality settings
    enum class RenderQuality {
        Draft,      // Fast, lower quality
        Normal,     // Balanced
        High,       // High quality, slower
        Maximum     // Best quality, slowest
    } Quality = RenderQuality::Normal;
    
    // Anti-aliasing
    bool EnableAntiAliasing = true;
    int AntiAliasingLevel = 4;  // 1, 2, 4, 8, 16
    
    // Text rendering
    bool EnableTextAntiAliasing = true;
    bool EnableSubpixelRendering = true;
    bool EnableHinting = true;
    enum class HintingMode {
        None,
        Slight,
        Normal,
        Full
    } TextHinting = HintingMode::Normal;
    
    // Path rendering
    float CurveFlatteningTolerance = 0.25f;
    bool EnableStrokeAdjustment = true;  // Adjust strokes for pixel alignment
    
    // Effects
    bool EnableFilters = true;
    bool EnableShadows = true;
    bool EnableGradients = true;
    int MaxFilterPasses = 10;
    
    // Performance
    bool EnableCaching = true;
    bool EnableOcclusion = true;  // Skip hidden elements
    bool EnableLOD = false;       // Level of detail for complex paths
    float LODThreshold = 1000.0f; // Path complexity threshold
    
    // Viewport
    Rect2Df ViewportRect;
    bool ClipToViewport = true;
    
    // Transform
    Matrix3x3 GlobalTransform = Matrix3x3::Identity();
    
    // Debug
    bool ShowBoundingBoxes = false;
    bool ShowPathPoints = false;
    bool ShowPathDirection = false;
    bool ShowLayerBounds = false;
    Color DebugColor = Color(255, 0, 255, 128);
};

// ===== RENDER STATE =====

struct RenderState {
    Matrix3x3 Transform;
    VectorStyle Style;
    float GlobalOpacity = 1.0f;
    Rect2Df ClipRect;
    bool HasClip = false;
    
    // Computed values
    Matrix3x3 CombinedTransform;
    float CombinedOpacity;
};

// ===== RENDERER INTERFACE =====

class IVectorRenderer {
public:
    virtual ~IVectorRenderer() = default;
    
    // Initialization
    virtual bool Initialize(IRenderContext* context) = 0;
    virtual void Shutdown() = 0;
    
    // Rendering
    virtual void BeginFrame(const RenderOptions& options = RenderOptions()) = 0;
    virtual void EndFrame() = 0;
    
    virtual void RenderDocument(const VectorDocument& document) = 0;
    virtual void RenderLayer(const VectorLayer& layer) = 0;
    virtual void RenderElement(const VectorElement& element) = 0;
    
    // State management
    virtual void PushState() = 0;
    virtual void PopState() = 0;
    virtual RenderState& GetCurrentState() = 0;
    
    // Transform
    virtual void SetTransform(const Matrix3x3& transform) = 0;
    virtual void MultiplyTransform(const Matrix3x3& transform) = 0;
    virtual void ResetTransform() = 0;
    
    // Style
    virtual void SetStyle(const VectorStyle& style) = 0;
    virtual void SetFill(const FillData& fill) = 0;
    virtual void SetStroke(const StrokeData& stroke) = 0;
    virtual void SetOpacity(float opacity) = 0;
    
    // Clipping
    virtual void SetClipPath(const PathData& path, FillRule rule = FillRule::NonZero) = 0;
    virtual void SetClipRect(const Rect2Df& rect) = 0;
    virtual void ClearClip() = 0;
    
    // Performance queries
    virtual size_t GetDrawCallCount() const = 0;
    virtual size_t GetVertexCount() const = 0;
    virtual float GetLastFrameTime() const = 0;
};

// ===== SOFTWARE RENDERER =====

class SoftwareVectorRenderer : public IVectorRenderer, public IVectorVisitor {
public:
    SoftwareVectorRenderer();
    ~SoftwareVectorRenderer();
    
    // IVectorRenderer implementation
    bool Initialize(IRenderContext* context) override;
    void Shutdown() override;
    
    void BeginFrame(const RenderOptions& options = RenderOptions()) override;
    void EndFrame() override;
    
    void RenderDocument(const VectorDocument& document) override;
    void RenderLayer(const VectorLayer& layer) override;
    void RenderElement(const VectorElement& element) override;
    
    void PushState() override;
    void PopState() override;
    RenderState& GetCurrentState() override;
    
    void SetTransform(const Matrix3x3& transform) override;
    void MultiplyTransform(const Matrix3x3& transform) override;
    void ResetTransform() override;
    
    void SetStyle(const VectorStyle& style) override;
    void SetFill(const FillData& fill) override;
    void SetStroke(const StrokeData& stroke) override;
    void SetOpacity(float opacity) override;
    
    void SetClipPath(const PathData& path, FillRule rule = FillRule::NonZero) override;
    void SetClipRect(const Rect2Df& rect) override;
    void ClearClip() override;
    
    size_t GetDrawCallCount() const override { return drawCallCount; }
    size_t GetVertexCount() const override { return vertexCount; }
    float GetLastFrameTime() const override { return lastFrameTime; }
    
    // IVectorVisitor implementation
    void Visit(VectorRect* element) override;
    void Visit(VectorCircle* element) override;
    void Visit(VectorEllipse* element) override;
    void Visit(VectorLine* element) override;
    void Visit(VectorPolyline* element) override;
    void Visit(VectorPolygon* element) override;
    void Visit(VectorPath* element) override;
    void Visit(VectorText* element) override;
    void Visit(VectorTextPath* element) override;
    void Visit(VectorGroup* element) override;
    void Visit(VectorSymbol* element) override;
    void Visit(VectorUse* element) override;
    void Visit(VectorImage* element) override;
    void Visit(VectorGradient* element) override;
    void Visit(VectorPattern* element) override;
    void Visit(VectorFilter* element) override;
    void Visit(VectorClipPath* element) override;
    void Visit(VectorMask* element) override;
    void Visit(VectorMarker* element) override;
    void Visit(VectorLayer* element) override;
    
private:
    IRenderContext* context;
    RenderOptions options;
    std::stack<RenderState> stateStack;
    RenderState currentState;
    
    // Statistics
    size_t drawCallCount;
    size_t vertexCount;
    float lastFrameTime;
    
    // Rendering helpers
    void RenderPathData(const PathData& path);
    void RenderWithFill(const FillData& fill, const std::function<void()>& renderFunc);
    void RenderWithStroke(const StrokeData& stroke, const std::function<void()>& renderFunc);
    void ApplyTransform(const Matrix3x3& transform);
    void ApplyStyle(const VectorStyle& style);
    
    // Gradient rendering
    void RenderLinearGradient(const LinearGradientData& gradient, const Rect2Df& bounds);
    void RenderRadialGradient(const RadialGradientData& gradient, const Rect2Df& bounds);
    void RenderConicalGradient(const ConicalGradientData& gradient, const Rect2Df& bounds);
    void RenderMeshGradient(const MeshGradientData& gradient, const Rect2Df& bounds);
    
    // Pattern rendering
    void RenderPattern(const PatternData& pattern, const Rect2Df& bounds);
    
    // Filter rendering
    void ApplyFilter(const FilterData& filter);
    void ApplyGaussianBlur(float radius);
    void ApplyDropShadow(const Color& color, const Point2Df& offset, float blur);
    
    // Text rendering
    void RenderTextSpan(const TextSpanData& span, const Point2Df& position);
    void RenderTextOnPath(const TextPathData& textPath);
    
    // Path operations
    std::vector<Point2Df> FlattenPath(const PathData& path);
    std::vector<std::vector<Point2Df>> TessellatePolygon(const std::vector<Point2Df>& points, FillRule rule);
    std::vector<Point2Df> GenerateStrokeGeometry(const std::vector<Point2Df>& points, const StrokeData& stroke);
};

// ===== HARDWARE ACCELERATED RENDERER =====

class HardwareVectorRenderer : public IVectorRenderer, public IVectorVisitor {
public:
    HardwareVectorRenderer();
    ~HardwareVectorRenderer();
    
    // IVectorRenderer implementation
    bool Initialize(IRenderContext* context) override;
    void Shutdown() override;
    
    void BeginFrame(const RenderOptions& options = RenderOptions()) override;
    void EndFrame() override;
    
    void RenderDocument(const VectorDocument& document) override;
    void RenderLayer(const VectorLayer& layer) override;
    void RenderElement(const VectorElement& element) override;
    
    void PushState() override;
    void PopState() override;
    RenderState& GetCurrentState() override;
    
    void SetTransform(const Matrix3x3& transform) override;
    void MultiplyTransform(const Matrix3x3& transform) override;
    void ResetTransform() override;
    
    void SetStyle(const VectorStyle& style) override;
    void SetFill(const FillData& fill) override;
    void SetStroke(const StrokeData& stroke) override;
    void SetOpacity(float opacity) override;
    
    void SetClipPath(const PathData& path, FillRule rule = FillRule::NonZero) override;
    void SetClipRect(const Rect2Df& rect) override;
    void ClearClip() override;
    
    size_t GetDrawCallCount() const override { return drawCallCount; }
    size_t GetVertexCount() const override { return vertexCount; }
    float GetLastFrameTime() const override { return lastFrameTime; }
    
    // IVectorVisitor implementation
    void Visit(VectorRect* element) override;
    void Visit(VectorCircle* element) override;
    void Visit(VectorEllipse* element) override;
    void Visit(VectorLine* element) override;
    void Visit(VectorPolyline* element) override;
    void Visit(VectorPolygon* element) override;
    void Visit(VectorPath* element) override;
    void Visit(VectorText* element) override;
    void Visit(VectorTextPath* element) override;
    void Visit(VectorGroup* element) override;
    void Visit(VectorSymbol* element) override;
    void Visit(VectorUse* element) override;
    void Visit(VectorImage* element) override;
    void Visit(VectorGradient* element) override;
    void Visit(VectorPattern* element) override;
    void Visit(VectorFilter* element) override;
    void Visit(VectorClipPath* element) override;
    void Visit(VectorMask* element) override;
    void Visit(VectorMarker* element) override;
    void Visit(VectorLayer* element) override;
    
private:
    IRenderContext* context;
    RenderOptions options;
    std::stack<RenderState> stateStack;
    RenderState currentState;
    
    // Statistics
    size_t drawCallCount;
    size_t vertexCount;
    float lastFrameTime;
    
    // GPU resources
    struct GPUResources {
        uint32_t vertexBuffer;
        uint32_t indexBuffer;
        uint32_t shaderProgram;
        uint32_t gradientTexture;
        uint32_t patternTexture;
        // ... more GPU resources
    } gpuResources;
    
    // Batch rendering
    struct RenderBatch {
        std::vector<float> vertices;
        std::vector<uint32_t> indices;
        uint32_t shaderProgram;
        Matrix3x3 transform;
        // ... batch state
    };
    
    std::vector<RenderBatch> renderBatches;
    
    void FlushBatch();
    void AddToBatch(const RenderBatch& batch);
};

// ===== CAIRO RENDERER (Linux) =====

#ifdef ULTRACANVAS_USE_CAIRO
class CairoVectorRenderer : public IVectorRenderer, public IVectorVisitor {
public:
    CairoVectorRenderer();
    ~CairoVectorRenderer();
    
    // IVectorRenderer implementation
    bool Initialize(IRenderContext* context) override;
    void Shutdown() override;
    
    void BeginFrame(const RenderOptions& options = RenderOptions()) override;
    void EndFrame() override;
    
    void RenderDocument(const VectorDocument& document) override;
    void RenderLayer(const VectorLayer& layer) override;
    void RenderElement(const VectorElement& element) override;
    
    void PushState() override;
    void PopState() override;
    RenderState& GetCurrentState() override;
    
    void SetTransform(const Matrix3x3& transform) override;
    void MultiplyTransform(const Matrix3x3& transform) override;
    void ResetTransform() override;
    
    void SetStyle(const VectorStyle& style) override;
    void SetFill(const FillData& fill) override;
    void SetStroke(const StrokeData& stroke) override;
    void SetOpacity(float opacity) override;
    
    void SetClipPath(const PathData& path, FillRule rule = FillRule::NonZero) override;
    void SetClipRect(const Rect2Df& rect) override;
    void ClearClip() override;
    
    size_t GetDrawCallCount() const override { return drawCallCount; }
    size_t GetVertexCount() const override { return vertexCount; }
    float GetLastFrameTime() const override { return lastFrameTime; }
    
    // IVectorVisitor implementation
    void Visit(VectorRect* element) override;
    void Visit(VectorCircle* element) override;
    void Visit(VectorEllipse* element) override;
    void Visit(VectorLine* element) override;
    void Visit(VectorPolyline* element) override;
    void Visit(VectorPolygon* element) override;
    void Visit(VectorPath* element) override;
    void Visit(VectorText* element) override;
    void Visit(VectorTextPath* element) override;
    void Visit(VectorGroup* element) override;
    void Visit(VectorSymbol* element) override;
    void Visit(VectorUse* element) override;
    void Visit(VectorImage* element) override;
    void Visit(VectorGradient* element) override;
    void Visit(VectorPattern* element) override;
    void Visit(VectorFilter* element) override;
    void Visit(VectorClipPath* element) override;
    void Visit(VectorMask* element) override;
    void Visit(VectorMarker* element) override;
    void Visit(VectorLayer* element) override;
    
private:
    void* cairoContext;  // cairo_t*
    void* cairoSurface;  // cairo_surface_t*
    IRenderContext* ultraCanvasContext;
    RenderOptions options;
    std::stack<RenderState> stateStack;
    RenderState currentState;
    
    // Statistics
    size_t drawCallCount;
    size_t vertexCount;
    float lastFrameTime;
    
    // Cairo-specific helpers
    void SetCairoTransform(const Matrix3x3& transform);
    void SetCairoFill(const FillData& fill);
    void SetCairoStroke(const StrokeData& stroke);
    void RenderCairoPath(const PathData& path);
};
#endif

// ===== RENDERER FACTORY =====

class VectorRendererFactory {
public:
    enum class RendererType {
        Software,      // CPU-based rendering
        Hardware,      // GPU-accelerated
        Cairo,         // Cairo backend (Linux)
        Direct2D,      // Direct2D backend (Windows)
        CoreGraphics,  // Core Graphics backend (macOS)
        Auto           // Choose best available
    };
    
    static std::unique_ptr<IVectorRenderer> CreateRenderer(
        RendererType type = RendererType::Auto,
        IRenderContext* context = nullptr
    );
    
    static RendererType GetBestRenderer();
    static bool IsRendererAvailable(RendererType type);
};

// ===== RENDERING UTILITIES =====

// Hit testing
bool HitTestElement(const VectorElement& element, const Point2Df& point);
std::vector<const VectorElement*> HitTestDocument(
    const VectorDocument& document,
    const Point2Df& point
);

// Bounds calculation
Rect2Df CalculateElementBounds(const VectorElement& element);
Rect2Df CalculateLayerBounds(const VectorLayer& layer);
Rect2Df CalculateDocumentBounds(const VectorDocument& document);

// Intersection testing
bool ElementsIntersect(const VectorElement& a, const VectorElement& b);
bool ElementInRect(const VectorElement& element, const Rect2Df& rect);

// LOD (Level of Detail) helpers
float CalculatePathComplexity(const PathData& path);
PathData SimplifyPathForLOD(const PathData& path, float targetComplexity);

// Caching
class VectorRenderCache {
public:
    struct CacheEntry {
        std::vector<uint8_t> PixelData;
        Rect2Df Bounds;
        Matrix3x3 Transform;
        uint64_t Timestamp;
        size_t MemorySize;
    };
    
    void AddToCache(const std::string& key, const CacheEntry& entry);
    bool GetFromCache(const std::string& key, CacheEntry& entry);
    void InvalidateCache(const std::string& key);
    void ClearCache();
    void SetMaxCacheSize(size_t bytes);
    size_t GetCurrentCacheSize() const;
    
private:
    std::map<std::string, CacheEntry> cache;
    size_t maxCacheSize = 100 * 1024 * 1024;  // 100MB default
    size_t currentCacheSize = 0;
    
    void EvictOldest();
};

// ===== ANIMATION SUPPORT =====

class VectorAnimationController {
public:
    // Animation types
    enum class AnimationType {
        Transform,
        Opacity,
        Fill,
        Stroke,
        Path,
        Custom
    };
    
    // Easing functions
    enum class EasingFunction {
        Linear,
        EaseIn,
        EaseOut,
        EaseInOut,
        Cubic,
        Bounce,
        Elastic,
        Custom
    };
    
    struct Animation {
        std::string ElementId;
        AnimationType Type;
        float Duration;
        float Delay = 0.0f;
        EasingFunction Easing = EasingFunction::Linear;
        bool Loop = false;
        bool AutoReverse = false;
        std::map<std::string, std::variant<float, Color, Matrix3x3, PathData>> KeyFrames;
    };
    
    void AddAnimation(const Animation& animation);
    void RemoveAnimation(const std::string& id);
    void StartAnimation(const std::string& id);
    void StopAnimation(const std::string& id);
    void PauseAnimation(const std::string& id);
    void ResumeAnimation(const std::string& id);
    
    void Update(float deltaTime);
    void ApplyAnimations(VectorDocument& document);
    
private:
    std::vector<Animation> animations;
    std::map<std::string, float> animationStates;  // Current time for each animation
};

} // namespace VectorRenderer
} // namespace UltraCanvas
