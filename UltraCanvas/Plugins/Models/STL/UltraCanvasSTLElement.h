// Plugins/Models/STL/UltraCanvasSTLElement.h
// UI element that displays a loaded 3D mesh.
// When built with ULTRACANVAS_ENABLE_GL it renders a shaded 3D model on a
// UltraCanvasGLSurface; otherwise it draws the same view with the software
// rasterizer (UltraCanvasModelRaster.h). Either way it orbits with the mouse
// and reports the view as a ModelViewPose, which is what lets a caller turn
// the framing the user chose into a bitmap.
// Version: 1.1.0
// Last Modified: 2026-09-13
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_STL_ELEMENT_H
#define ULTRACANVAS_STL_ELEMENT_H

#include "UltraCanvas3DTypes.h"
#include "UltraCanvasModelRaster.h"   // ModelViewPose, kModelDefaultColor, the software still
#include "UltraCanvasSmoothScroll.h"
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
        const Vec3& GetModelColor() const { return modelColor_; }
        void SetAutoRotate(bool enable);

        // ----- the view the user has orbited to -----
        // The three numbers that define what is on screen. Hand them to
        // RenderMeshPixmap() (UltraCanvasModelRaster.h) and the still that
        // comes back is this view - which is how "turn this model into a
        // bitmap" keeps the framing the user chose.
        ModelViewPose GetViewPose() const { return ModelViewPose{yaw_, pitch_, distance_}; }
        void SetViewPose(const ModelViewPose& pose);
        // Back to the framing the viewer opened at.
        void ResetView() { ResetCameraToFit(); RequestRender(); }

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
        // The wheel dollies the camera: the distance eases towards its target
        // instead of stepping, so a spin reads as one continuous move (the dolly
        // is multiplicative, which is what UltraCanvasSmoothZoom animates).
        UltraCanvasSmoothZoom dollyAnim_;
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

// ===== NON-GL FALLBACK (data + software-rendered still) =====
//
// Without GL there is no interaction to offer - no orbit, no zoom - so this
// draws the one thing that is still worth drawing: a shaded three-quarter
// still of the mesh, from the same software rasterizer the Filer's thumbnails
// use (UltraCanvasModelRaster.h). It used to print the model's triangle count
// and a line suggesting the reader rebuild with GL enabled.
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

        void SetModelColor(const Vec3& rgb) { modelColor_ = rgb; InvalidateRender(); RequestRedraw(); }
        const Vec3& GetModelColor() const { return modelColor_; }
        // Auto-rotation needs a frame clock this element does not have; the
        // orbit is still there, it just has to be driven by hand.
        void SetAutoRotate(bool) {}

        // ----- the view the user has orbited to -----
        // The same three numbers the GL viewer keeps, so a caller reads the
        // view the same way on either build - and so the still
        // RenderMeshPixmap() returns is exactly what is on screen here,
        // because this element draws itself with that very call.
        ModelViewPose GetViewPose() const { return pose_; }
        void SetViewPose(const ModelViewPose& pose);
        void ResetView() { SetViewPose(ModelViewPose::Default()); }

        std::function<void(const std::string&)> onLoadError;
        std::function<void()> onLoadComplete;

        bool OnEvent(const UCEvent& event) override;
        bool AcceptsFocus() const override { return true; }
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;

    private:
        // The rasterized still, kept until the mesh, the pose or the
        // element's size changes: re-rendering a view that has not moved
        // would return the same pixels.
        void InvalidateRender();

        Mesh3D mesh_;
        Vec3 modelColor_ = kModelDefaultColor;
        ModelViewPose pose_;
        std::shared_ptr<UCPixmap> rendered_;
        int renderedWidth_ = 0;
        int renderedHeight_ = 0;

        // Mouse drag state, as in the GL build: drag orbits, wheel dollies.
        bool dragging_ = false;
        int lastMouseX_ = 0;
        int lastMouseY_ = 0;
    };

#endif // ULTRACANVAS_ENABLE_GL

} // namespace UltraCanvas

#endif // ULTRACANVAS_STL_ELEMENT_H
