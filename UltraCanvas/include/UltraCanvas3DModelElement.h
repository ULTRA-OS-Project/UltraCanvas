// UltraCanvas3DModelElement.h
// 3D Model loading and display component - fully integrated with UltraCanvas
// Version: 1.0.0
// Last Modified: 2024-12-30
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderInterface.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasGraphicsPluginSystem.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <iostream>

namespace UltraCanvas {

// ===== 3D MODEL FORMATS =====
enum class Model3DFormat {
    Unknown,
    ThreeDS,     // .3ds - 3D Studio Max
    ThreeDM,     // .3dm - Rhino 3D
    POV,         // .pov - POV-Ray
    STD,         // .std - Standard format
    OBJ,         // .obj - Wavefront OBJ
    FBX,         // .fbx - Autodesk FBX
    DAE,         // .dae - COLLADA
    GLTF,        // .gltf - GL Transmission Format
    PLY,         // .ply - Polygon File Format
    STL          // .stl - STereoLithography
};

// ===== 3D MODEL DATA =====
struct Model3DData {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    std::vector<float> normals;
    std::vector<float> texCoords;
    std::vector<std::string> materials;
    
    // Bounding box
    Point2D minBounds = Point2D(0, 0);
    Point2D maxBounds = Point2D(0, 0);
    float depth = 0.0f;
    
    // Model properties
    int vertexCount = 0;
    int faceCount = 0;
    bool hasTextures = false;
    bool hasNormals = false;
    
    Model3DData() = default;
    
    bool IsValid() const {
        return !vertices.empty() && vertexCount > 0;
    }
    
    Point2D GetSize() const {
        return Point2D(maxBounds.x - minBounds.x, maxBounds.y - minBounds.y);
    }
};

// ===== 3D MODEL VIEWING PARAMETERS =====
struct Model3DViewParams {
    // Camera/viewing
    Point2D cameraPosition = Point2D(0, 0);
    float cameraDistance = 5.0f;
    float cameraRotationX = 0.0f;  // Pitch
    float cameraRotationY = 0.0f;  // Yaw
    float cameraRotationZ = 0.0f;  // Roll
    float fieldOfView = 45.0f;
    
    // Model transformation
    Point2D modelPosition = Point2D(0, 0);
    Point2D modelScale = Point2D(1.0f, 1.0f);
    float modelRotationX = 0.0f;
    float modelRotationY = 0.0f;
    float modelRotationZ = 0.0f;
    
    // Lighting
    bool enableLighting = true;
    Point2D lightPosition = Point2D(1.0f, 1.0f);
    Color lightColor = Colors::White;
    float ambientLight = 0.3f;
    
    // Rendering
    bool wireframe = false;
    bool showNormals = false;
    bool enableShading = true;
    Color backgroundColor = Color(50, 50, 50, 255);
    
    Model3DViewParams() = default;
};

// ===== 3D MODEL ELEMENT =====
class UltraCanvas3DModelElement : public UltraCanvasElement {
private:
    StandardProperties properties;
    
    // Model data
    std::string modelPath;
    Model3DFormat modelFormat = Model3DFormat::Unknown;
    Model3DData modelData;
    Model3DViewParams viewParams;
    
    // Loading state
    bool isLoaded = false;
    bool isLoading = false;
    bool hasError = false;
    std::string errorMessage;
    
    // Interaction
    bool allowRotation = true;
    bool allowZoom = true;
    bool allowPan = true;
    Point2D lastMousePos;
    bool isDragging = false;
    
    // Rendering
    bool autoCenter = true;
    bool autoScale = true;
    float defaultDistance = 5.0f;
    
public:
    // ===== EVENTS =====
    std::function<void()> onModelLoaded;
    std::function<void(const std::string&)> onModelLoadFailed;
    std::function<void(const Model3DViewParams&)> onViewChanged;
    std::function<void()> onModelClicked;
    
    // ===== CONSTRUCTOR =====
    UltraCanvas3DModelElement(const std::string& identifier = "Model3DElement", long id = 0,
                             long x = 0, long y = 0, long w = 300, long h = 300)
        : UltraCanvasElement(identifier, id, x, y, w, h), properties(identifier, id, x, y, w, h) {
        
        properties.MousePtr = MousePointer::Default;
        properties.MouseCtrl = MouseControls::Object3D;

    }
    
    // ===== MODEL LOADING =====
    bool LoadModelFromFile(const std::string& filePath) {
        modelPath = filePath;
        isLoading = true;
        hasError = false;
        errorMessage.clear();
        
        // Detect format from extension
        modelFormat = DetectModelFormat(filePath);
        if (modelFormat == Model3DFormat::Unknown) {
            SetError("Unsupported 3D model format");
            return false;
        }
        
        std::cout << "[UltraCanvas3DModelElement] Loading 3D model: " << filePath << std::endl;
        
        // Load model data based on format
        bool success = LoadModelData(filePath, modelFormat);
        
        if (success) {
            isLoaded = true;
            isLoading = false;
            
            if (autoCenter) {
                CenterModel();
            }
            if (autoScale) {
                ScaleToFit();
            }
            
            if (onModelLoaded) {
                onModelLoaded();
            }
            
            std::cout << "[UltraCanvas3DModelElement] Model loaded successfully. Vertices: " 
                      << modelData.vertexCount << ", Faces: " << modelData.faceCount << std::endl;
        } else {
            SetError("Failed to load 3D model data");
        }
        
        return success;
    }
    
    // ===== MODEL INFO =====
    bool IsLoaded() const { return isLoaded; }
    bool IsLoading() const { return isLoading; }
    bool HasError() const { return hasError; }
    const std::string& GetErrorMessage() const { return errorMessage; }
    const Model3DData& GetModelData() const { return modelData; }
    const std::string& GetModelPath() const { return modelPath; }
    Model3DFormat GetModelFormat() const { return modelFormat; }
    
    // ===== VIEW CONTROL =====
    void SetViewParams(const Model3DViewParams& params) {
        viewParams = params;
        if (onViewChanged) {
            onViewChanged(viewParams);
        }
    }
    
    const Model3DViewParams& GetViewParams() const {
        return viewParams;
    }
    
    void ResetView() {
        viewParams = Model3DViewParams();
        if (autoCenter) CenterModel();
        if (autoScale) ScaleToFit();
        
        if (onViewChanged) {
            onViewChanged(viewParams);
        }
    }
    
    void SetCameraRotation(float rotX, float rotY, float rotZ) {
        viewParams.cameraRotationX = rotX;
        viewParams.cameraRotationY = rotY;
        viewParams.cameraRotationZ = rotZ;
    }
    
    void SetCameraDistance(float distance) {
        viewParams.cameraDistance = std::max(0.1f, distance);
    }
    
    void SetModelRotation(float rotX, float rotY, float rotZ) {
        viewParams.modelRotationX = rotX;
        viewParams.modelRotationY = rotY;
        viewParams.modelRotationZ = rotZ;
    }
    
    // ===== INTERACTION CONTROL =====
    void SetInteractionMode(bool rotation, bool zoom, bool pan) {
        allowRotation = rotation;
        allowZoom = zoom;
        allowPan = pan;
    }
    
    void SetRenderingMode(bool wireframe, bool showNormals = false) {
        viewParams.wireframe = wireframe;
        viewParams.showNormals = showNormals;
    }
    
    // ===== RENDERING =====
    void Render() override {
        if (!IsVisible()) return;
        
        ULTRACANVAS_RENDER_SCOPE();
        
        // Draw background
        DrawFilledRect(GetBounds(), viewParams.backgroundColor);
        
        if (HasError()) {
            DrawErrorDisplay();
        } else if (IsLoading()) {
            DrawLoadingDisplay();
        } else if (IsLoaded()) {
            DrawModel();
        } else {
            DrawPlaceholder();
        }
        
        // Draw border if focused
        if (IsFocused()) {
            DrawFilledRect(GetBounds(), Colors::Transparent, Colors::Selection, 2.0f);
        }
    }
    
    // ===== EVENT HANDLING =====
    void OnEvent(const UCEvent& event) override {
        if (!IsActive() || !IsVisible()) return;
        
        switch (event.type) {
            case UCEventType::MouseDown:
                HandleMouseDown(event);
                break;
                
            case UCEventType::MouseMove:
                HandleMouseMove(event);
                break;
                
            case UCEventType::MouseUp:
                HandleMouseUp(event);
                break;
                
            case UCEventType::MouseWheel:
                HandleMouseWheel(event);
                break;
                
            case UCEventType::KeyDown:
                HandleKeyDown(event);
                break;
        }
    }
    
private:

    Model3DFormat DetectModelFormat(const std::string& filePath) {
        size_t dotPos = filePath.find_last_of('.');
        if (dotPos == std::string::npos) return Model3DFormat::Unknown;
        
        std::string ext = filePath.substr(dotPos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        if (ext == "3ds") return Model3DFormat::ThreeDS;
        if (ext == "3dm") return Model3DFormat::ThreeDM;
        if (ext == "pov") return Model3DFormat::POV;
        if (ext == "std") return Model3DFormat::STD;
        if (ext == "obj") return Model3DFormat::OBJ;
        if (ext == "fbx") return Model3DFormat::FBX;
        if (ext == "dae") return Model3DFormat::DAE;
        if (ext == "gltf") return Model3DFormat::GLTF;
        if (ext == "ply") return Model3DFormat::PLY;
        if (ext == "stl") return Model3DFormat::STL;
        
        return Model3DFormat::Unknown;
    }
    
    bool LoadModelData(const std::string& filePath, Model3DFormat format) {
        // For now, create placeholder model data
        // In a real implementation, this would use appropriate 3D model loading libraries
        
        switch (format) {
            case Model3DFormat::ThreeDS:
                return LoadThreeDSModel(filePath);
            case Model3DFormat::ThreeDM:
                return LoadThreeDMModel(filePath);
            case Model3DFormat::OBJ:
                return LoadOBJModel(filePath);
            default:
                return LoadGenericModel(filePath);
        }
    }
    
    bool LoadThreeDSModel(const std::string& filePath) {
        // Placeholder implementation for .3ds files
        // Real implementation would use lib3ds or similar
        return CreateCubeModel(); // Placeholder
    }
    
    bool LoadThreeDMModel(const std::string& filePath) {
        // Placeholder implementation for .3dm files
        // Real implementation would use OpenNURBS library
        return CreateCubeModel(); // Placeholder
    }
    
    bool LoadOBJModel(const std::string& filePath) {
        // Placeholder implementation for .obj files
        // Real implementation would parse OBJ format
        return CreateCubeModel(); // Placeholder
    }
    
    bool LoadGenericModel(const std::string& filePath) {
        // Generic model loader - creates a simple cube for testing
        return CreateCubeModel();
    }
    
    bool CreateCubeModel() {
        // Create a simple cube for testing/placeholder
        modelData.vertices = {
            // Front face
            -1.0f, -1.0f,  1.0f,
             1.0f, -1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
            -1.0f,  1.0f,  1.0f,
            // Back face
            -1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f
        };
        
        modelData.indices = {
            0, 1, 2,  2, 3, 0,  // Front
            4, 5, 6,  6, 7, 4,  // Back
            0, 4, 7,  7, 3, 0,  // Left
            1, 5, 6,  6, 2, 1,  // Right
            3, 7, 6,  6, 2, 3,  // Top
            0, 4, 5,  5, 1, 0   // Bottom
        };
        
        modelData.vertexCount = 8;
        modelData.faceCount = 12;
        modelData.minBounds = Point2D(-1.0f, -1.0f);
        modelData.maxBounds = Point2D(1.0f, 1.0f);
        modelData.depth = 2.0f;
        
        return true;
    }
    
    void CenterModel() {
        if (!modelData.IsValid()) return;
        
        Point2D size = modelData.GetSize();
        viewParams.modelPosition = Point2D(-size.x / 2, -size.y / 2);
    }
    
    void ScaleToFit() {
        if (!modelData.IsValid()) return;
        
        Point2D size = modelData.GetSize();
        float maxDimension = std::max(size.x, std::max(size.y, modelData.depth));
        
        if (maxDimension > 0) {
            float scale = 2.0f / maxDimension; // Fit in 2x2 space
            viewParams.modelScale = Point2D(scale, scale);
            viewParams.cameraDistance = defaultDistance;
        }
    }
    
    void SetError(const std::string& message) {
        hasError = true;
        isLoading = false;
        isLoaded = false;
        errorMessage = message;
        
        std::cerr << "[UltraCanvas3DModelElement] Error: " << message << std::endl;
        
        if (onModelLoadFailed) {
            onModelLoadFailed(message);
        }
    }
    
    void DrawModel() {
        // Simplified 3D rendering using 2D projection
        // Real implementation would use OpenGL/DirectX or software 3D renderer
        
        Rect2D bounds = GetBounds();
        Point2D center = bounds.Center();
        
        // Project 3D vertices to 2D screen space (simplified)
        std::vector<Point2D> projectedVertices;
        for (size_t i = 0; i < modelData.vertices.size(); i += 3) {
            float x = modelData.vertices[i];
            float y = modelData.vertices[i + 1];
            float z = modelData.vertices[i + 2];
            
            // Apply model transformation
            x *= viewParams.modelScale.x;
            y *= viewParams.modelScale.y;
            
            // Simple orthographic projection
            float screenX = center.x + x * 50; // Scale for screen
            float screenY = center.y - y * 50; // Flip Y for screen coordinates
            
            projectedVertices.emplace_back(screenX, screenY);
        }
        
        // Draw wireframe or faces
        if (viewParams.wireframe || !viewParams.enableShading) {
            DrawWireframe(projectedVertices);
        } else {
            DrawSolidFaces(projectedVertices);
        }
        
        // Draw info overlay
        DrawInfoOverlay();
    }
    
    void DrawWireframe(const std::vector<Point2D>& vertices) {
        SetStrokeColor(Colors::White);
        SetStrokeWidth(1.0f);
        
        // Draw edges based on indices
        for (size_t i = 0; i < modelData.indices.size(); i += 3) {
            if (i + 2 < modelData.indices.size()) {
                int v1 = modelData.indices[i];
                int v2 = modelData.indices[i + 1];
                int v3 = modelData.indices[i + 2];
                
                if (v1 < vertices.size() && v2 < vertices.size() && v3 < vertices.size()) {
                    DrawLine(vertices[v1], vertices[v2]);
                    DrawLine(vertices[v2], vertices[v3]);
                    DrawLine(vertices[v3], vertices[v1]);
                }
            }
        }
    }
    
    void DrawSolidFaces(const std::vector<Point2D>& vertices) {
        // Simple filled triangles
        SetFillColor(Color(180, 180, 180, 255));
        
        for (size_t i = 0; i < modelData.indices.size(); i += 3) {
            if (i + 2 < modelData.indices.size()) {
                int v1 = modelData.indices[i];
                int v2 = modelData.indices[i + 1];
                int v3 = modelData.indices[i + 2];
                
                if (v1 < vertices.size() && v2 < vertices.size() && v3 < vertices.size()) {
                    std::vector<Point2D> triangle = {vertices[v1], vertices[v2], vertices[v3]};
                    GetRenderContext()->DrawPolygon(triangle);
                }
            }
        }
    }
    
    void DrawInfoOverlay() {
        if (!modelData.IsValid()) return;
        
        SetTextColor(Colors::White);
        SetFont("Arial", 10.0f);
        
        std::string info = "Vertices: " + std::to_string(modelData.vertexCount) + 
                          " | Faces: " + std::to_string(modelData.faceCount);
        
        DrawText(info, Point2D(GetX() + 5, GetY() + GetHeight() - 15));
    }
    
    void DrawLoadingDisplay() {
        Rect2D bounds = GetBounds();
        Point2D center = bounds.Center();
        
        SetTextColor(Colors::White);
        SetFont("Arial", 14.0f);
        DrawText("Loading 3D Model...", Point2D(center.x - 50, center.y));
    }
    
    void DrawErrorDisplay() {
        Rect2D bounds = GetBounds();
        Point2D center = bounds.Center();
        
        SetTextColor(Colors::Red);
        SetFont("Arial", 12.0f);
        DrawText("Error loading model", Point2D(center.x - 60, center.y - 10));
        
        if (!errorMessage.empty()) {
            SetFont("Arial", 10.0f);
            DrawText(errorMessage, Point2D(center.x - 80, center.y + 10));
        }
    }
    
    void DrawPlaceholder() {
        Rect2D bounds = GetBounds();
        Point2D center = bounds.Center();
        
        // Draw placeholder cube outline
        SetStrokeColor(Colors::Gray);
        SetStrokeWidth(2.0f);
        
        float size = 50;
        Rect2D cubeRect(center.x - size/2, center.y - size/2, size, size);
        DrawFilledRect(cubeRect, Colors::Transparent, Colors::Gray, 2.0f);
        
        SetTextColor(Colors::Gray);
        SetFont("Arial", 12.0f);
        DrawText("3D Model", Point2D(center.x - 30, center.y + size/2 + 20));
    }
    
    // ===== EVENT HANDLERS =====
    void HandleMouseDown(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return;
        
        SetFocus(true);
        isDragging = true;
        lastMousePos = Point2D(event.x, event.y);
        
        if (onModelClicked) {
            onModelClicked();
        }
    }
    
    void HandleMouseMove(const UCEvent& event) {
        if (!isDragging) return;
        
        Point2D currentPos(event.x, event.y);
        Point2D delta = currentPos - lastMousePos;
        
        if (allowRotation && !event.shift) {
            // Rotate model
            viewParams.modelRotationY += delta.x * 0.01f;
            viewParams.modelRotationX += delta.y * 0.01f;
        } else if (allowPan && event.shift) {
            // Pan model
            viewParams.modelPosition.x += delta.x * 0.01f;
            viewParams.modelPosition.y += delta.y * 0.01f;
        }
        
        lastMousePos = currentPos;
        
        if (onViewChanged) {
            onViewChanged(viewParams);
        }
    }
    
    void HandleMouseUp(const UCEvent& event) {
        isDragging = false;
    }
    
    void HandleMouseWheel(const UCEvent& event) {
        if (!allowZoom || !Contains(event.x, event.y)) return;
        
        float zoomFactor = event.delta > 0 ? 0.9f : 1.1f;
        viewParams.cameraDistance *= zoomFactor;
        viewParams.cameraDistance = std::max(0.1f, std::min(100.0f, viewParams.cameraDistance));
        
        if (onViewChanged) {
            onViewChanged(viewParams);
        }
    }
    
    void HandleKeyDown(const UCEvent& event) {
        if (!IsFocused()) return;
        
        switch (event.virtualKey) {
            case UCKeys::Return:
                ResetView();
                break;
                
            case 'w': case 'W':
                viewParams.wireframe = !viewParams.wireframe;
                break;
                
            case 'r': case 'R':
                if (event.ctrl && !modelPath.empty()) {
                    // Reload model
                    LoadModelFromFile(modelPath);
                } else {
                    ResetView();
                }
                break;
        }
    }
};

// ===== FACTORY FUNCTIONS =====
inline std::shared_ptr<UltraCanvas3DModelElement> CreateModel3DElement(
    const std::string& identifier, long id, long x, long y, long w, long h) {
    return UltraCanvasElementFactory::CreateWithID<UltraCanvas3DModelElement>(id, identifier, id, x, y, w, h);
}

inline std::shared_ptr<UltraCanvas3DModelElement> LoadModel3DFromFile(
    const std::string& identifier, long id, long x, long y, long w, long h, const std::string& modelPath) {
    auto element = CreateModel3DElement(identifier, id, x, y, w, h);
    element->LoadModelFromFile(modelPath);
    return element;
}

// ===== CONVENIENCE FUNCTIONS (Matching your original API) =====
inline std::shared_ptr<UltraCanvasElement> LoadModelFromFile(const std::string& modelPath) {
    auto element = CreateModel3DElement("model3d_" + std::to_string(rand()), rand(), 0, 0, 300, 300);
    element->LoadModelFromFile(modelPath);
    return std::static_pointer_cast<UltraCanvasElement>(element);
}

// ===== 3D MODEL PLUGIN =====
class Model3DPlugin : public IGraphicsPlugin {
public:
    std::string GetPluginName() const override {
        return "UltraCanvas 3D Model Plugin";
    }
    
    std::string GetPluginVersion() const override {
        return "1.0.0";
    }
    
    std::string GetDescription() const override {
        return "3D model loading and display support";
    }
    
    std::vector<std::string> GetSupportedExtensions() const override {
        return {"3ds", "3dm", "pov", "std", "obj", "fbx", "dae", "gltf", "ply", "stl"};
    }
    
    bool CanHandle(const std::string& extension) const override {
        auto extensions = GetSupportedExtensions();
        std::string lowerExt = extension;
        std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::tolower);
        return std::find(extensions.begin(), extensions.end(), lowerExt) != extensions.end();
    }
    
    bool CanHandle(const GraphicsFileInfo& fileInfo) const override {
        return fileInfo.formatType == GraphicsFormatType::ThreeD && CanHandle(fileInfo.extension);
    }
    
    std::shared_ptr<UltraCanvasElement> LoadGraphics(const std::string& filePath) override {
        return LoadModel3DFromFile("model3d_" + std::to_string(rand()), rand(), 0, 0, 300, 300, filePath);
    }
    
    std::shared_ptr<UltraCanvasElement> LoadGraphics(const GraphicsFileInfo& fileInfo) override {
        return LoadGraphics(fileInfo.filename);
    }
    
    std::shared_ptr<UltraCanvasElement> CreateGraphics(int width, int height, GraphicsFormatType type) override {
        if (type == GraphicsFormatType::ThreeD) {
            return CreateModel3DElement("new_model3d_" + std::to_string(rand()), rand(), 0, 0, width, height);
        }
        return nullptr;
    }
    
    GraphicsManipulation GetSupportedManipulations() const override {
        return GraphicsManipulation::Move | GraphicsManipulation::Rotate | 
               GraphicsManipulation::Scale | GraphicsManipulation::Transform;
    }
    
    GraphicsFileInfo GetFileInfo(const std::string& filePath) override {
        GraphicsFileInfo info(filePath);
        info.formatType = GraphicsFormatType::ThreeD;
        info.supportedManipulations = GetSupportedManipulations();
        return info;
    }
    
    bool ValidateFile(const std::string& filePath) override {
        return CanHandle(GraphicsFileInfo(filePath).extension);
    }
};

// ===== REGISTER 3D MODEL PLUGIN =====
inline void Register3DModelPlugin() {
    UltraCanvasGraphicsPluginRegistry::RegisterPlugin(std::make_shared<Model3DPlugin>());
}

} // namespace UltraCanvas