// Plugins/Models/STL/UltraCanvasSTLElement.h
// UI element that displays a loaded STL mesh.
// When built with ULTRACANVAS_ENABLE_GL it renders a shaded 3D model on a
// UltraCanvasGLSurface with mouse-orbit; otherwise it falls back to a 2D
// info placeholder so the plugin still builds and loads data everywhere.
// Version: 1.0.0
// Last Modified: 2026-06-21
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_STL_ELEMENT_H
#define ULTRACANVAS_STL_ELEMENT_H

#include "UltraCanvas3DTypes.h"
#include "UltraCanvasSTLLoader.h"
#include <string>
#include <memory>
#include <functional>

#ifdef ULTRACANVAS_ENABLE_GL
    #include "UltraCanvasGLSurface.h"
#else
    #include "UltraCanvasUIElement.h"
#endif

namespace UltraCanvas {

#ifdef ULTRACANVAS_ENABLE_GL

// ===== GL-BACKED 3D STL VIEWER =====
    class UltraCanvasSTLElement : public UltraCanvasGLSurface {
    public:
        UltraCanvasSTLElement(const std::string& identifier,
                              float x, float y, float width, float height);
        ~UltraCanvasSTLElement() override;

        // Load mesh data (parsing is done elsewhere by UltraCanvasSTLLoader).
        bool LoadFromFile(const std::string& filePath);
        void SetMesh(const Mesh3D& mesh);
        const Mesh3D& GetMesh() const { return mesh_; }

        // Save the currently loaded mesh back to disk.
        bool SaveToFile(const std::string& filePath,
                        STLFormat format = STLFormat::Auto,
                        std::string* outError = nullptr) const;

        // Appearance / interaction.
        void SetModelColor(const Vec3& rgb) { modelColor_ = rgb; RequestRender(); }
        void SetAutoRotate(bool enable);

        // Notifications.
        std::function<void(const std::string&)> onLoadError;
        std::function<void()> onLoadComplete;

        bool OnEvent(const UCEvent& event) override;

    protected:
        void OnGLInit() override;
        void OnGLRender(const RenderSurfaceInfo& info) override;
        void OnGLCleanup() override;

    private:
        void UploadMesh();        // (re)build GPU buffers from mesh_
        void ResetCameraToFit();  // frame the model based on its bounds

        Mesh3D mesh_;
        Vec3 modelColor_{0.78f, 0.80f, 0.85f};

        // Camera / orbit state.
        float yaw_ = 0.6f;
        float pitch_ = 0.4f;
        float distance_ = 3.0f;
        Vec3 target_{0.0f, 0.0f, 0.0f};
        bool autoRotate_ = false;

        // Mouse drag state.
        bool dragging_ = false;
        int lastMouseX_ = 0;
        int lastMouseY_ = 0;

        // GL objects (raw GL handles kept as unsigned to avoid leaking GL headers here).
        unsigned int program_ = 0;
        unsigned int vao_ = 0;
        unsigned int vboPositions_ = 0;
        unsigned int vboNormals_ = 0;
        unsigned int ebo_ = 0;
        int indexCount_ = 0;
        bool glReady_ = false;
        bool meshDirty_ = false;

        // Uniform locations.
        int uMVP_ = -1;
        int uModel_ = -1;
        int uLightDir_ = -1;
        int uColor_ = -1;
    };

#else // !ULTRACANVAS_ENABLE_GL

// ===== NON-GL FALLBACK (data + 2D placeholder) =====
    class UltraCanvasSTLElement : public UltraCanvasUIElement {
    public:
        UltraCanvasSTLElement(const std::string& identifier,
                              float x, float y, float width, float height);
        ~UltraCanvasSTLElement() override = default;

        bool LoadFromFile(const std::string& filePath);
        void SetMesh(const Mesh3D& mesh);
        const Mesh3D& GetMesh() const { return mesh_; }

        bool SaveToFile(const std::string& filePath,
                        STLFormat format = STLFormat::Auto,
                        std::string* outError = nullptr) const;

        void SetModelColor(const Vec3&) {}
        void SetAutoRotate(bool) {}

        std::function<void(const std::string&)> onLoadError;
        std::function<void()> onLoadComplete;

        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;

    private:
        Mesh3D mesh_;
    };

#endif // ULTRACANVAS_ENABLE_GL

} // namespace UltraCanvas

#endif // ULTRACANVAS_STL_ELEMENT_H
