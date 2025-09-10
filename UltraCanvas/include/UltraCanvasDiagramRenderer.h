// UltraCanvasDiagramRenderer.h
// Advanced diagram rendering system with multi-format support and plugin architecture
// Version: 1.0.0
// Last Modified: 2025-01-01
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasGraphicsPluginSystem.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasFileDialog.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <future>
#include <chrono>
#include <fstream>
#include <sstream>

namespace UltraCanvas {

// ===== DIAGRAM FORMAT DEFINITIONS =====
enum class DiagramFormat {
    Unknown, PlantUML, Mermaid, Graphviz, BPMN, Flowchart, 
    ERD, Sequence, Class, Activity, State, Component, 
    Deployment, UseCase, Gantt, GitGraph, Journey, 
    Pie, C4Context, C4Container, C4Component, Ditaa, 
    BlockDiag, SeqDiag, ActDiag, NwDiag, PacketDiag, 
    RackDiag, Kroki, D2, Excalidraw, Nomnoml, Pikchr,
    Structurizr, Vega, VegaLite, WaveDrom, Bytefield,
    Erd, GraphQLVoyager, Dbml, Symbolator, TikZ
};

enum class DiagramOutputFormat {
    SVG, PNG, PDF, EPS, JPEG, Base64, Raw
};

enum class DiagramRenderingEngine {
    Local,          // Local renderer executable
    Embedded,       // Embedded library
    WebService,     // Remote web service (Kroki)
    Plugin,         // Custom plugin
    Cached          // Pre-rendered cache
};

enum class DiagramState {
    Empty, Loading, Rendered, Error, Cached
};

// ===== DIAGRAM METADATA =====
struct DiagramMetadata {
    std::string title;
    std::string description;
    std::string author;
    std::string version;
    DiagramFormat format = DiagramFormat::Unknown;
    DiagramOutputFormat outputFormat = DiagramOutputFormat::SVG;
    DiagramRenderingEngine engine = DiagramRenderingEngine::Local;
    
    std::string sourceFile;
    std::string outputFile;
    size_t sourceSize = 0;
    size_t outputSize = 0;
    
    std::chrono::system_clock::time_point lastModified;
    std::chrono::system_clock::time_point lastRendered;
    
    int width = 0, height = 0;
    std::string backgroundColor;
    float scaleFactor = 1.0f;
    
    bool isAnimated = false;
    float animationDuration = 0.0f;
    
    std::unordered_map<std::string, std::string> customProperties;
    
    bool IsValid() const {
        return format != DiagramFormat::Unknown && !sourceFile.empty();
    }
    
    std::string GetFormatString() const {
        switch (format) {
            case DiagramFormat::PlantUML: return "PlantUML";
            case DiagramFormat::Mermaid: return "Mermaid";
            case DiagramFormat::Graphviz: return "Graphviz";
            case DiagramFormat::BPMN: return "BPMN";
            case DiagramFormat::Flowchart: return "Flowchart";
            case DiagramFormat::ERD: return "ERD";
            case DiagramFormat::Sequence: return "Sequence";
            case DiagramFormat::Class: return "Class";
            case DiagramFormat::Activity: return "Activity";
            case DiagramFormat::State: return "State";
            default: return "Unknown";
        }
    }
    
    std::string GetEngineString() const {
        switch (engine) {
            case DiagramRenderingEngine::Local: return "Local";
            case DiagramRenderingEngine::Embedded: return "Embedded";
            case DiagramRenderingEngine::WebService: return "Web Service";
            case DiagramRenderingEngine::Plugin: return "Plugin";
            case DiagramRenderingEngine::Cached: return "Cached";
            default: return "Unknown";
        }
    }
};

// ===== DIAGRAM RENDERER INTERFACE =====
class IDiagramRenderer {
public:
    virtual ~IDiagramRenderer() = default;
    
    // Renderer Information
    virtual std::string GetRendererName() = 0;
    virtual std::string GetRendererVersion() = 0;
    virtual std::vector<DiagramFormat> GetSupportedFormats() = 0;
    virtual std::vector<DiagramOutputFormat> GetSupportedOutputFormats() = 0;
    virtual std::vector<std::string> GetSupportedExtensions() = 0;
    
    // Format Support
    virtual bool CanRender(DiagramFormat format) = 0;
    virtual bool CanRender(const std::string& filePath) = 0;
    virtual DiagramFormat DetectFormat(const std::string& content) = 0;
    virtual DiagramFormat DetectFormatFromFile(const std::string& filePath) = 0;
    
    // Rendering
    virtual std::future<std::vector<uint8_t>> RenderAsync(const std::string& source, DiagramFormat format, DiagramOutputFormat outputFormat = DiagramOutputFormat::SVG) = 0;
    virtual std::vector<uint8_t> Render(const std::string& source, DiagramFormat format, DiagramOutputFormat outputFormat = DiagramOutputFormat::SVG) = 0;
    virtual std::future<std::vector<uint8_t>> RenderFileAsync(const std::string& filePath, DiagramOutputFormat outputFormat = DiagramOutputFormat::SVG) = 0;
    virtual std::vector<uint8_t> RenderFile(const std::string& filePath, DiagramOutputFormat outputFormat = DiagramOutputFormat::SVG) = 0;
    
    // Metadata
    virtual DiagramMetadata GetMetadata(const std::string& source, DiagramFormat format) = 0;
    virtual DiagramMetadata GetFileMetadata(const std::string& filePath) = 0;
    
    // Validation
    virtual bool ValidateSource(const std::string& source, DiagramFormat format) = 0;
    virtual std::vector<std::string> GetValidationErrors(const std::string& source, DiagramFormat format) = 0;
    
    // Capabilities
    virtual bool SupportsLivePreview() = 0;
    virtual bool SupportsInteractivity() = 0;
    virtual bool SupportsAnimation() = 0;
    virtual bool SupportsExport() = 0;
    virtual bool IsAvailable() = 0;
};

// ===== DIAGRAM CONFIGURATION =====
struct DiagramRenderConfig {
    DiagramOutputFormat outputFormat = DiagramOutputFormat::SVG;
    DiagramRenderingEngine preferredEngine = DiagramRenderingEngine::Local;
    
    // Output Options
    int width = -1;                    // -1 = auto
    int height = -1;                   // -1 = auto
    float scaleFactor = 1.0f;
    std::string backgroundColor;
    bool transparentBackground = false;
    
    // Quality Options
    int dpi = 300;
    int quality = 85;                  // For JPEG
    bool antialiasing = true;
    
    // Performance Options
    bool enableCaching = true;
    std::string cacheDirectory = "cache/diagrams";
    int maxCacheSize = 100;            // MB
    int renderTimeout = 30;            // seconds
    
    // Advanced Options
    std::unordered_map<std::string, std::string> engineOptions;
    std::unordered_map<std::string, std::string> formatOptions;
    
    static DiagramRenderConfig Default() { return DiagramRenderConfig(); }
    
    static DiagramRenderConfig HighQuality() {
        DiagramRenderConfig config;
        config.dpi = 600;
        config.quality = 95;
        config.scaleFactor = 2.0f;
        return config;
    }
    
    static DiagramRenderConfig Fast() {
        DiagramRenderConfig config;
        config.dpi = 150;
        config.quality = 70;
        config.enableCaching = true;
        return config;
    }
};

// ===== MAIN DIAGRAM ELEMENT =====
class UltraCanvasDiagramElement : public UltraCanvasElement {
private:
    // ===== STANDARD PROPERTIES (REQUIRED) =====
    StandardProperties properties;
    
    // ===== DIAGRAM STATE =====
    std::shared_ptr<IDiagramRenderer> renderer;
    DiagramMetadata metadata;
    DiagramState currentState = DiagramState::Empty;
    DiagramRenderConfig renderConfig;
    
    // ===== CONTENT =====
    std::string sourceContent;
    std::vector<uint8_t> renderedData;
    std::shared_ptr<UltraCanvasImageElement> imageElement;
    
    // ===== RENDERING STATE =====
    std::future<std::vector<uint8_t>> renderFuture;
    bool isRenderingAsync = false;
    std::chrono::steady_clock::time_point renderStartTime;
    
    // ===== INTERACTION =====
    bool showSource = false;
    bool enableLivePreview = false;
    bool autoResize = false;
    float zoomLevel = 1.0f;
    Point2D panOffset = Point2D(0, 0);
    
    // ===== ERROR HANDLING =====
    std::string lastError;
    std::vector<std::string> validationErrors;
    
public:
    // ===== CONSTRUCTOR (REQUIRED PATTERN) =====
    UltraCanvasDiagramElement(const std::string& identifier = "DiagramElement", 
                             long id = 0, long x = 0, long y = 0, long w = 400, long h = 300)
        : UltraCanvasElement(identifier, id, x, y, w, h) {
        
        // Initialize standard properties
        properties = StandardProperties(identifier, id, x, y, w, h);
        properties.MousePtr = MousePointer::Default;
        properties.MouseCtrl = MouseControls::Object2D;
        
        // Initialize render config
        renderConfig = DiagramRenderConfig::Default();
        
        // Create image element for rendered output
        imageElement = std::make_shared<UltraCanvasImageElement>(
            identifier + "_image", id + 1000, 0, 0, w, h);
        AddChild(imageElement.get());
    }

    // ===== CONTENT MANAGEMENT =====
    bool LoadFromFile(const std::string& filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            SetError("Failed to open file: " + filePath);
            return false;
        }
        
        std::ostringstream content;
        content << file.rdbuf();
        file.close();
        
        // Detect format from file
        DiagramFormat format = DetectDiagramFormat(filePath);
        if (format == DiagramFormat::Unknown) {
            SetError("Unknown diagram format: " + filePath);
            return false;
        }
        
        // Find appropriate renderer
        renderer = FindRenderer(format);
        if (!renderer) {
            SetError("No renderer available for format: " + metadata.GetFormatString());
            return false;
        }
        
        sourceContent = content.str();
        metadata = renderer->GetFileMetadata(filePath);
        metadata.sourceFile = filePath;
        
        if (onContentLoaded) onContentLoaded(metadata);
        
        return RenderDiagram();
    }
    
    bool SetSource(const std::string& source, DiagramFormat format) {
        sourceContent = source;
        
        // Find appropriate renderer
        renderer = FindRenderer(format);
        if (!renderer) {
            SetError("No renderer available for format: " + metadata.GetFormatString());
            return false;
        }
        
        metadata = renderer->GetMetadata(source, format);
        metadata.format = format;
        
        if (onContentLoaded) onContentLoaded(metadata);
        
        return RenderDiagram();
    }
    
    const std::string& GetSource() const { return sourceContent; }
    const DiagramMetadata& GetMetadata() const { return metadata; }
    DiagramState GetState() const { return currentState; }
    
    // ===== RENDERING CONTROL =====
    bool RenderDiagram() {
        if (!renderer || sourceContent.empty()) {
            SetError("No renderer or source content available");
            return false;
        }
        
        // Validate source first
        if (!renderer->ValidateSource(sourceContent, metadata.format)) {
            validationErrors = renderer->GetValidationErrors(sourceContent, metadata.format);
            SetError("Source validation failed");
            return false;
        }
        
        SetState(DiagramState::Loading);
        
        if (enableLivePreview || !renderer->SupportsLivePreview()) {
            return RenderSync();
        } else {
            return RenderAsync();
        }
    }
    
    bool RenderSync() {
        try {
            renderStartTime = std::chrono::steady_clock::now();
            
            renderedData = renderer->Render(sourceContent, metadata.format, renderConfig.outputFormat);
            
            if (renderedData.empty()) {
                SetError("Rendering produced no output");
                return false;
            }
            
            return ApplyRenderedData();
        } catch (const std::exception& e) {
            SetError("Rendering failed: " + std::string(e.what()));
            return false;
        }
    }
    
    bool RenderAsync() {
        try {
            renderStartTime = std::chrono::steady_clock::now();
            isRenderingAsync = true;
            
            renderFuture = renderer->RenderAsync(sourceContent, metadata.format, renderConfig.outputFormat);
            
            return true;
        } catch (const std::exception& e) {
            SetError("Async rendering failed: " + std::string(e.what()));
            isRenderingAsync = false;
            return false;
        }
    }
    
    void CancelRendering() {
        if (isRenderingAsync) {
            // Future cancellation would be implementation-specific
            isRenderingAsync = false;
            SetState(DiagramState::Empty);
        }
    }
    
    // ===== CONFIGURATION =====
    void SetRenderConfig(const DiagramRenderConfig& config) {
        renderConfig = config;
    }
    
    const DiagramRenderConfig& GetRenderConfig() const { return renderConfig; }
    
    void SetRenderer(std::shared_ptr<IDiagramRenderer> diagramRenderer) {
        renderer = diagramRenderer;
    }
    
    std::shared_ptr<IDiagramRenderer> GetRenderer() const { return renderer; }
    
    // ===== DISPLAY OPTIONS =====
    void SetShowSource(bool show) { showSource = show; }
    bool GetShowSource() const { return showSource; }
    
    void SetLivePreview(bool enable) { enableLivePreview = enable; }
    bool GetLivePreview() const { return enableLivePreview; }
    
    void SetAutoResize(bool enable) { 
        autoResize = enable;
        if (imageElement) {
            // Would implement auto-resize logic
        }
    }
    bool GetAutoResize() const { return autoResize; }
    
    // ===== ZOOM AND PAN =====
    void SetZoom(float zoom) {
        zoomLevel = std::max(0.1f, std::min(zoom, 10.0f));
        UpdateImageTransform();
    }
    
    float GetZoom() const { return zoomLevel; }
    
    void ZoomIn() { SetZoom(zoomLevel * 1.2f); }
    void ZoomOut() { SetZoom(zoomLevel / 1.2f); }
    void ZoomToFit() { 
        if (metadata.width > 0 && metadata.height > 0) {
            float scaleX = GetWidth() / static_cast<float>(metadata.width);
            float scaleY = GetHeight() / static_cast<float>(metadata.height);
            SetZoom(std::min(scaleX, scaleY));
        }
    }
    void ZoomReset() { SetZoom(1.0f); }
    
    void SetPan(const Point2D& offset) {
        panOffset = offset;
        UpdateImageTransform();
    }
    
    const Point2D& GetPan() const { return panOffset; }
    
    // ===== ERROR HANDLING =====
    bool HasError() const { return !lastError.empty(); }
    const std::string& GetLastError() const { return lastError; }
    const std::vector<std::string>& GetValidationErrors() const { return validationErrors; }
    
    void ClearError() {
        lastError.clear();
        validationErrors.clear();
    }
    
    // ===== EXPORT =====
    bool ExportToFile(const std::string& filePath, DiagramOutputFormat format = DiagramOutputFormat::SVG) {
        if (!renderer || sourceContent.empty()) return false;
        
        try {
            std::vector<uint8_t> exportData = renderer->Render(sourceContent, metadata.format, format);
            
            std::ofstream file(filePath, std::ios::binary);
            if (!file.is_open()) return false;
            
            file.write(reinterpret_cast<const char*>(exportData.data()), exportData.size());
            file.close();
            
            if (onExported) onExported(filePath, format);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
    
    std::vector<uint8_t> ExportToMemory(DiagramOutputFormat format = DiagramOutputFormat::SVG) {
        if (!renderer || sourceContent.empty()) return {};
        
        try {
            return renderer->Render(sourceContent, metadata.format, format);
        } catch (const std::exception&) {
            return {};
        }
    }
    
    // ===== RENDERING (REQUIRED OVERRIDE) =====
    void Render() override {
        if (!IsVisible()) return;
        
        ctx->PushState();
        
        // Check async rendering status
        if (isRenderingAsync && renderFuture.valid()) {
            if (renderFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                try {
                    renderedData = renderFuture.get();
                    isRenderingAsync = false;
                    
                    if (!renderedData.empty()) {
                        ApplyRenderedData();
                    } else {
                        SetError("Async rendering produced no output");
                    }
                } catch (const std::exception& e) {
                    SetError("Async rendering failed: " + std::string(e.what()));
                    isRenderingAsync = false;
                }
            }
        }
        
        // Draw background
        ctx->SetFillColor(Colors::White);
        ctx->DrawRectangle(GetBounds());
        
        // Render state-specific content
        switch (currentState) {
            case DiagramState::Empty:
                RenderEmptyState();
                break;
                
            case DiagramState::Loading:
                RenderLoadingState();
                break;
                
            case DiagramState::Rendered:
                RenderDiagramContent();
                break;
                
            case DiagramState::Error:
                RenderErrorState();
                break;
                
            case DiagramState::Cached:
                RenderDiagramContent();
                break;
        }
        
        // Render children (image element)
        RenderChildren();
        
        // Show source overlay if enabled
        if (showSource) {
            RenderSourceOverlay();
        }
    }
    
    // ===== EVENT HANDLING (REQUIRED OVERRIDE) =====
    bool OnEvent(const UCEvent& event) override {
        if (!IsActive() || !IsVisible()) return false;
        
        switch (event.type) {
            case UCEventType::MouseDown:
                HandleMouseDown(event);
                break;
                
            case UCEventType::MouseMove:
                HandleMouseMove(event);
                break;
                
            case UCEventType::MouseWheel:
                HandleMouseWheel(event);
                break;
                
            case UCEventType::KeyDown:
                HandleKeyDown(event);
                break;
        }
        return false;
    }
    
    // ===== EVENT CALLBACKS =====
    std::function<void(const DiagramMetadata&)> onContentLoaded;
    std::function<void(DiagramState)> onStateChanged;
    std::function<void(const std::string&)> onError;
    std::function<void(const std::string&, DiagramOutputFormat)> onExported;
    std::function<void(float)> onZoomChanged;
    std::function<void(const Point2D&)> onPanChanged;

private:
    // ===== DIAGRAM FORMAT DETECTION =====
    DiagramFormat DetectDiagramFormat(const std::string& filePath) {
        std::string ext = GetFileExtension(filePath);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        // File extension based detection
        if (ext == "puml" || ext == "plantuml") return DiagramFormat::PlantUML;
        if (ext == "mmd" || ext == "mermaid") return DiagramFormat::Mermaid;
        if (ext == "dot" || ext == "gv") return DiagramFormat::Graphviz;
        if (ext == "bpmn") return DiagramFormat::BPMN;
        if (ext == "d2") return DiagramFormat::D2;
        if (ext == "nomnoml") return DiagramFormat::Nomnoml;
        
        // Content-based detection would require reading the file
        return DiagramFormat::Unknown;
    }
    
    std::shared_ptr<IDiagramRenderer> FindRenderer(DiagramFormat format) {
        // This would search the diagram renderer registry
        // For now, return a basic renderer
        return std::make_shared<BasicDiagramRenderer>();
    }
    
    // ===== STATE MANAGEMENT =====
    void SetState(DiagramState newState) {
        if (currentState != newState) {
            currentState = newState;
            if (onStateChanged) onStateChanged(newState);
        }
    }
    
    void SetError(const std::string& error) {
        lastError = error;
        SetState(DiagramState::Error);
        if (onError) onError(error);
    }
    
    // ===== RENDERING HELPERS =====
    bool ApplyRenderedData() {
        if (renderedData.empty()) return false;
        
        // Save rendered data to temporary file for image element
        std::string tempFile = "temp_diagram_" + std::to_string(GetID()) + ".svg";
        std::ofstream file(tempFile, std::ios::binary);
        if (file.is_open()) {
            file.write(reinterpret_cast<const char*>(renderedData.data()), renderedData.size());
            file.close();
            
            if (imageElement) {
                imageElement->LoadFromFile(tempFile);
                UpdateImageTransform();
            }
            
            SetState(DiagramState::Rendered);
            return true;
        }
        
        return false;
    }
    
    void UpdateImageTransform() {
        if (!imageElement) return;
        
        // Apply zoom and pan transformations
        imageElement->SetScale(zoomLevel, zoomLevel);
        imageElement->SetOffset(panOffset.x, panOffset.y);
    }
    
    void RenderEmptyState() {
        ctx->SetTextColor(Colors::Gray);
        ctx->SetFont("Arial", 14.0f);
        
        std::string message = "No diagram loaded";
        Point2D center(GetX() + GetWidth() / 2.0f, GetY() + GetHeight() / 2.0f);
        ctx->DrawText(message, Point2D(center.x - 60, center.y));
    }
    
    void RenderLoadingState() {
        ctx->SetTextColor(Colors::Blue);
        ctx->SetFont("Arial", 14.0f);
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - renderStartTime);
        
        std::string message = "Rendering... (" + std::to_string(elapsed.count()) + "s)";
        Point2D center(GetX() + GetWidth() / 2.0f, GetY() + GetHeight() / 2.0f);
        ctx->DrawText(message, Point2D(center.x - 60, center.y));
        
        // Simple progress indicator
        ctx->SetFillColor(Colors::Blue);
        float progress = (elapsed.count() % 10) / 10.0f;
        Rect2D progressBar(GetX() + 20, center.y + 20, (GetWidth() - 40) * progress, 4);
        ctx->DrawRectangle(progressBar);
    }
    
    void RenderDiagramContent() {
        // Image element renders the actual diagram
        // Additional decorations could be added here
    }
    
    void RenderErrorState() {
        ctx->SetFillColor(Color(255, 240, 240));
        ctx->DrawRectangle(GetBounds());
        
        ctx->SetTextColor(Colors::Red);
        ctx->SetFont("Arial", 12.0f);
        
        ctx->DrawText("Error: " + lastError, Point2D(GetX() + 10, GetY() + 20));
        
        if (!validationErrors.empty()) {
            float currentY = GetY() + 40;
            for (const auto& error : validationErrors) {
                ctx->DrawText("• " + error, Point2D(GetX() + 15, currentY));
                currentY += 16;
            }
        }
    }
    
    void RenderSourceOverlay() {
        // Semi-transparent overlay with source text
        ctx->SetFillColor(Color(0, 0, 0, 180));
        Rect2D overlay(GetX(), GetY(), GetWidth(), GetHeight());
        ctx->DrawRectangle(overlay);
        
        ctx->SetTextColor(Colors::White);
        ctx->SetFont("Courier New", 10.0f);
        
        // Show first few lines of source
        std::istringstream stream(sourceContent);
        std::string line;
        float currentY = GetY() + 20;
        int lineCount = 0;
        
        while (std::getline(stream, line) && lineCount < 20) {
            ctx->DrawText(line, Point2D(GetX() + 10, currentY));
            currentY += 12;
            lineCount++;
        }
        
        if (std::getline(stream, line)) {
            DrawText("...", Point2D(GetX() + 10, currentY));
        }
    }
    
    // ===== EVENT HANDLERS =====
    void HandleMouseDown(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return;
        SetFocus(true);
    }
    
    void HandleMouseMove(const UCEvent& event) {
        // Handle panning if middle mouse button or specific key is held
    }
    
    void HandleMouseWheel(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return;
        
        float zoomFactor = (event.delta > 0) ? 1.1f : 0.9f;
        SetZoom(zoomLevel * zoomFactor);
        
        if (onZoomChanged) onZoomChanged(zoomLevel);
    }
    
    void HandleKeyDown(const UCEvent& event) {
        if (!IsFocused()) return;
        
        switch (event.virtualKey) {
            case 'R':
            case 'r':
                if (event.ctrl) {
                    RenderDiagram();
                }
                break;
            case 'S':
            case 's':
                if (event.ctrl) {
                    showSource = !showSource;
                }
                break;
            case UCKeys::Space:
                ZoomToFit();
                break;
            case '0':
                if (event.ctrl) {
                    ZoomReset();
                }
                break;
            case '=':
            case '+':
                ZoomIn();
                break;
            case '-':
                ZoomOut();
                break;
        }
    }
    
    // ===== UTILITY METHODS =====
    std::string GetFileExtension(const std::string& filePath) {
        size_t pos = filePath.find_last_of('.');
        return (pos != std::string::npos) ? filePath.substr(pos + 1) : "";
    }
};

// ===== BASIC DIAGRAM RENDERER IMPLEMENTATION =====
class BasicDiagramRenderer : public IDiagramRenderer {
private:
    std::vector<DiagramFormat> supportedFormats = {
        DiagramFormat::PlantUML, DiagramFormat::Mermaid, DiagramFormat::Graphviz
    };
    
public:
    std::string GetRendererName() override { return "Basic Diagram Renderer"; }
    std::string GetRendererVersion() override { return "1.0.0"; }
    
    std::vector<DiagramFormat> GetSupportedFormats() override { return supportedFormats; }
    std::vector<DiagramOutputFormat> GetSupportedOutputFormats() override {
        return {DiagramOutputFormat::SVG, DiagramOutputFormat::PNG};
    }
    
    std::vector<std::string> GetSupportedExtensions() override {
        return {"puml", "plantuml", "mmd", "mermaid", "dot", "gv"};
    }
    
    bool CanRender(DiagramFormat format) override {
        return std::find(supportedFormats.begin(), supportedFormats.end(), format) != supportedFormats.end();
    }
    
    bool CanRender(const std::string& filePath) override {
        // Simple file extension check
        std::string ext = GetFileExtension(filePath);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        auto extensions = GetSupportedExtensions();
        return std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
    }
    
    DiagramFormat DetectFormat(const std::string& content) override {
        // Simple content-based detection
        if (content.find("@startuml") != std::string::npos) return DiagramFormat::PlantUML;
        if (content.find("graph") != std::string::npos && content.find("-->") != std::string::npos) return DiagramFormat::Mermaid;
        if (content.find("digraph") != std::string::npos) return DiagramFormat::Graphviz;
        
        return DiagramFormat::Unknown;
    }
    
    DiagramFormat DetectFormatFromFile(const std::string& filePath) override {
        std::ifstream file(filePath);
        if (!file.is_open()) return DiagramFormat::Unknown;
        
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return DetectFormat(content);
    }
    
    std::future<std::vector<uint8_t>> RenderAsync(const std::string& source, DiagramFormat format, DiagramOutputFormat outputFormat) override {
        return std::async(std::launch::async, [this, source, format, outputFormat]() {
            return Render(source, format, outputFormat);
        });
    }
    
    std::vector<uint8_t> Render(const std::string& source, DiagramFormat format, DiagramOutputFormat outputFormat) override {
        // Placeholder implementation - would call actual rendering engines
        std::string result = GeneratePlaceholderSVG(source, format);
        return std::vector<uint8_t>(result.begin(), result.end());
    }
    
    std::future<std::vector<uint8_t>> RenderFileAsync(const std::string& filePath, DiagramOutputFormat outputFormat) override {
        return std::async(std::launch::async, [this, filePath, outputFormat]() {
            return RenderFile(filePath, outputFormat);
        });
    }
    
    std::vector<uint8_t> RenderFile(const std::string& filePath, DiagramOutputFormat outputFormat) override {
        std::ifstream file(filePath);
        if (!file.is_open()) return {};
        
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        DiagramFormat format = DetectFormatFromFile(filePath);
        
        return Render(content, format, outputFormat);
    }
    
    DiagramMetadata GetMetadata(const std::string& source, DiagramFormat format) override {
        DiagramMetadata metadata;
        metadata.format = format;
        metadata.outputFormat = DiagramOutputFormat::SVG;
        metadata.engine = DiagramRenderingEngine::Local;
        metadata.width = 400;
        metadata.height = 300;
        metadata.lastRendered = std::chrono::system_clock::now();
        return metadata;
    }
    
    DiagramMetadata GetFileMetadata(const std::string& filePath) override {
        DiagramMetadata metadata;
        metadata.sourceFile = filePath;
        metadata.format = DetectFormatFromFile(filePath);
        
        std::ifstream file(filePath);
        if (file.is_open()) {
            file.seekg(0, std::ios::end);
            metadata.sourceSize = file.tellg();
            file.close();
        }
        
        return metadata;
    }
    
    bool ValidateSource(const std::string& source, DiagramFormat format) override {
        // Basic validation - check if source is not empty
        return !source.empty();
    }
    
    std::vector<std::string> GetValidationErrors(const std::string& source, DiagramFormat format) override {
        std::vector<std::string> errors;
        if (source.empty()) {
            errors.push_back("Source content is empty");
        }
        return errors;
    }
    
    bool SupportsLivePreview() override { return true; }
    bool SupportsInteractivity() override { return false; }
    bool SupportsAnimation() override { return false; }
    bool SupportsExport() override { return true; }
    bool IsAvailable() override { return true; }

private:
    std::string GetFileExtension(const std::string& filePath) {
        size_t pos = filePath.find_last_of('.');
        return (pos != std::string::npos) ? filePath.substr(pos + 1) : "";
    }
    
    std::string GeneratePlaceholderSVG(const std::string& source, DiagramFormat format) {
        std::ostringstream svg;
        svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        svg << "<svg width=\"400\" height=\"300\" xmlns=\"http://www.w3.org/2000/svg\">\n";
        svg << "  <rect x=\"10\" y=\"10\" width=\"380\" height=\"280\" fill=\"#f0f0f0\" stroke=\"#ccc\" stroke-width=\"2\"/>\n";
        svg << "  <text x=\"200\" y=\"150\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"16\" fill=\"#666\">\n";
        svg << "    " << DiagramMetadata().GetFormatString() << " Diagram\n";
        svg << "  </text>\n";
        svg << "  <text x=\"200\" y=\"180\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"12\" fill=\"#999\">\n";
        svg << "    (Placeholder - Actual rendering not implemented)\n";
        svg << "  </text>\n";
        svg << "</svg>\n";
        
        return svg.str();
    }
};

// ===== DIAGRAM RENDERER REGISTRY =====
class UltraCanvasDiagramRendererRegistry {
private:
    static std::vector<std::shared_ptr<IDiagramRenderer>> renderers;
    
public:
    static void RegisterRenderer(std::shared_ptr<IDiagramRenderer> renderer) {
        renderers.push_back(renderer);
    }
    
    static void UnregisterRenderer(std::shared_ptr<IDiagramRenderer> renderer) {
        renderers.erase(std::remove(renderers.begin(), renderers.end(), renderer), renderers.end());
    }
    
    static std::shared_ptr<IDiagramRenderer> FindRendererForFormat(DiagramFormat format) {
        for (auto& renderer : renderers) {
            if (renderer->CanRender(format)) {
                return renderer;
            }
        }
        return nullptr;
    }
    
    static std::shared_ptr<IDiagramRenderer> FindRendererForFile(const std::string& filePath) {
        for (auto& renderer : renderers) {
            if (renderer->CanRender(filePath)) {
                return renderer;
            }
        }
        return nullptr;
    }
    
    static const std::vector<std::shared_ptr<IDiagramRenderer>>& GetRenderers() {
        return renderers;
    }
    
    static void ClearRenderers() {
        renderers.clear();
    }
};

// ===== FACTORY FUNCTIONS =====
inline std::shared_ptr<UltraCanvasDiagramElement> CreateDiagramElement(
    const std::string& identifier, long id, long x, long y, long w, long h) {
    return UltraCanvasElementFactory::CreateWithID<UltraCanvasDiagramElement>(
        id, identifier, id, x, y, w, h);
}

inline std::shared_ptr<UltraCanvasDiagramElement> CreateDiagramFromFile(
    const std::string& identifier, long id, long x, long y, long w, long h, 
    const std::string& filePath) {
    auto element = CreateDiagramElement(identifier, id, x, y, w, h);
    element->LoadFromFile(filePath);
    return element;
}

inline std::shared_ptr<UltraCanvasDiagramElement> CreateDiagramFromSource(
    const std::string& identifier, long id, long x, long y, long w, long h, 
    const std::string& source, DiagramFormat format) {
    auto element = CreateDiagramElement(identifier, id, x, y, w, h);
    element->SetSource(source, format);
    return element;
}

// ===== CONVENIENCE FUNCTIONS =====
inline void RegisterBasicDiagramRenderer() {
    UltraCanvasDiagramRendererRegistry::RegisterRenderer(std::make_shared<BasicDiagramRenderer>());
}

inline void InitializeDiagramSystem() {
    RegisterBasicDiagramRenderer();
}

inline void ShutdownDiagramSystem() {
    UltraCanvasDiagramRendererRegistry::ClearRenderers();
}

// ===== STATIC MEMBER DEFINITIONS =====
inline std::vector<std::shared_ptr<IDiagramRenderer>> UltraCanvasDiagramRendererRegistry::renderers;

} // namespace UltraCanvas