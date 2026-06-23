// Plugins/Models/STL/UltraCanvasSTLElement.cpp
// Implementation of the STL viewer element (GL viewer + non-GL fallback)
// Version: 1.0.0
// Last Modified: 2026-06-21
// Author: UltraCanvas Framework

#include "UltraCanvasSTLElement.h"
#include "UltraCanvasDebug.h"

#include <cmath>
#include <string>
#include <vector>

// GL headers must live at global scope (not inside namespace UltraCanvas).
// Include strategy matches libspecific/GL/GLFramebuffer.cpp.
#ifdef ULTRACANVAS_ENABLE_GL
#if defined(__APPLE__)
    #include <OpenGL/gl3.h>
    #include <OpenGL/gl3ext.h>
#elif defined(_WIN32)
    #include <GL/glew.h>
#else
    #ifndef GL_GLEXT_PROTOTYPES
    #define GL_GLEXT_PROTOTYPES
    #endif
    #include <GL/gl.h>
    #include <GL/glext.h>
#endif
#endif // ULTRACANVAS_ENABLE_GL

namespace UltraCanvas {

#ifdef ULTRACANVAS_ENABLE_GL

namespace {

    constexpr float kPi = 3.14159265358979323846f;

    const char* kVertexShader =
        "#version 330 core\n"
        "layout(location = 0) in vec3 aPos;\n"
        "layout(location = 1) in vec3 aNormal;\n"
        "uniform mat4 uMVP;\n"
        "uniform mat4 uModel;\n"
        "out vec3 vNormal;\n"
        "void main() {\n"
        "    gl_Position = uMVP * vec4(aPos, 1.0);\n"
        "    vNormal = mat3(uModel) * aNormal;\n"
        "}\n";

    const char* kFragmentShader =
        "#version 330 core\n"
        "in vec3 vNormal;\n"
        "uniform vec3 uLightDir;\n"
        "uniform vec3 uColor;\n"
        "out vec4 FragColor;\n"
        "void main() {\n"
        "    vec3 N = normalize(vNormal);\n"
        "    vec3 L = normalize(uLightDir);\n"
        "    float diff = abs(dot(N, L));\n"   // two-sided shading
        "    float ambient = 0.28;\n"
        "    vec3 c = uColor * (ambient + 0.72 * diff);\n"
        "    FragColor = vec4(c, 1.0);\n"
        "}\n";

    unsigned int CompileShader(unsigned int type, const char* src) {
        unsigned int shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        int ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024] = {0};
            glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
            debugOutput << "[STL] Shader compile failed: " << log << std::endl;
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    unsigned int LinkProgram(unsigned int vs, unsigned int fs) {
        unsigned int prog = glCreateProgram();
        glAttachShader(prog, vs);
        glAttachShader(prog, fs);
        glLinkProgram(prog);
        int ok = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[1024] = {0};
            glGetProgramInfoLog(prog, sizeof(log) - 1, nullptr, log);
            debugOutput << "[STL] Program link failed: " << log << std::endl;
            glDeleteProgram(prog);
            return 0;
        }
        return prog;
    }

} // anonymous namespace

UltraCanvasSTLElement::UltraCanvasSTLElement(const std::string& identifier,
                                             float x, float y, float width, float height)
    : UltraCanvasGLSurface(identifier, x, y, width, height) {
    // 3.3 core with depth buffer (the GLSurface default) is exactly what we need.
    SetRenderMode(RenderMode::OnDemand);
}

UltraCanvasSTLElement::~UltraCanvasSTLElement() = default;

bool UltraCanvasSTLElement::LoadFromFile(const std::string& filePath) {
    Mesh3D mesh;
    std::string error;
    if (!UltraCanvasSTLLoader::Load(filePath, mesh, &error)) {
        if (onLoadError) onLoadError(error);
        debugOutput << "[STL] Load failed: " << error << std::endl;
        return false;
    }
    SetMesh(mesh);
    if (onLoadComplete) onLoadComplete();
    return true;
}

void UltraCanvasSTLElement::SetMesh(const Mesh3D& mesh) {
    mesh_ = mesh;
    if (!mesh_.bounds.IsValid()) mesh_.ComputeBounds();
    meshDirty_ = true;
    ResetCameraToFit();
    RequestRender();
}

bool UltraCanvasSTLElement::SaveToFile(const std::string& filePath, STLFormat format,
                                       std::string* outError) const {
    return UltraCanvasSTLLoader::Save(filePath, mesh_, format, outError);
}

void UltraCanvasSTLElement::SetAutoRotate(bool enable) {
    autoRotate_ = enable;
    SetRenderMode(enable ? RenderMode::Continuous : RenderMode::OnDemand);
    RequestRender();
}

void UltraCanvasSTLElement::ResetCameraToFit() {
    target_ = Vec3{0.0f, 0.0f, 0.0f};   // model is centered at origin in model space
    // With the model normalized to a unit-ish radius, a distance of ~3 frames it.
    distance_ = 3.0f;
    yaw_ = 0.6f;
    pitch_ = 0.4f;
}

void UltraCanvasSTLElement::OnGLInit() {
    unsigned int vs = CompileShader(GL_VERTEX_SHADER, kVertexShader);
    unsigned int fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    if (vs && fs) {
        program_ = LinkProgram(vs, fs);
    }
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);

    if (program_) {
        uMVP_ = glGetUniformLocation(program_, "uMVP");
        uModel_ = glGetUniformLocation(program_, "uModel");
        uLightDir_ = glGetUniformLocation(program_, "uLightDir");
        uColor_ = glGetUniformLocation(program_, "uColor");
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vboPositions_);
    glGenBuffers(1, &vboNormals_);
    glGenBuffers(1, &ebo_);

    glReady_ = (program_ != 0);
    meshDirty_ = true;
}

void UltraCanvasSTLElement::UploadMesh() {
    if (!glReady_ || mesh_.Empty()) {
        indexCount_ = 0;
        meshDirty_ = false;
        return;
    }

    glBindVertexArray(vao_);

    glBindBuffer(GL_ARRAY_BUFFER, vboPositions_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh_.positions.size() * sizeof(Vec3)),
                 mesh_.positions.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vec3), (void*)0);

    glBindBuffer(GL_ARRAY_BUFFER, vboNormals_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh_.normals.size() * sizeof(Vec3)),
                 mesh_.normals.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vec3), (void*)0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh_.indices.size() * sizeof(uint32_t)),
                 mesh_.indices.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);

    indexCount_ = static_cast<int>(mesh_.indices.size());
    meshDirty_ = false;
}

void UltraCanvasSTLElement::OnGLRender(const RenderSurfaceInfo& info) {
    if (meshDirty_) UploadMesh();

    glViewport(0, 0, info.width, info.height);
    glClearColor(0.12f, 0.13f, 0.16f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!glReady_ || indexCount_ == 0) return;

    glEnable(GL_DEPTH_TEST);

    if (autoRotate_) {
        yaw_ += static_cast<float>(info.deltaTime) * 0.6f;
    }

    // Model space: center the mesh at the origin and normalize its size.
    Vec3 center = mesh_.bounds.Center();
    float radius = mesh_.bounds.Radius();
    float invR = (radius > 1e-6f) ? (1.0f / radius) : 1.0f;

    Mat4 normModel = Mat4::Scale(invR) * Mat4::Translation(-center.x, -center.y, -center.z);
    Mat4 rot = Mat4::RotationY(yaw_) * Mat4::RotationX(pitch_);
    Mat4 model = rot * normModel;

    float aspect = (info.height > 0)
                   ? static_cast<float>(info.width) / static_cast<float>(info.height)
                   : 1.0f;
    Mat4 proj = Mat4::Perspective(45.0f * kPi / 180.0f, aspect, 0.05f, 100.0f);
    Mat4 view = Mat4::LookAt(Vec3{0.0f, 0.0f, distance_},
                             Vec3{0.0f, 0.0f, 0.0f},
                             Vec3{0.0f, 1.0f, 0.0f});
    Mat4 mvp = proj * view * model;

    glUseProgram(program_);
    glUniformMatrix4fv(uMVP_, 1, GL_FALSE, mvp.m);
    glUniformMatrix4fv(uModel_, 1, GL_FALSE, model.m);
    glUniform3f(uLightDir_, 0.4f, 0.7f, 1.0f);
    glUniform3f(uColor_, modelColor_.x, modelColor_.y, modelColor_.z);

    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    glUseProgram(0);
}

void UltraCanvasSTLElement::OnGLCleanup() {
    if (ebo_) { glDeleteBuffers(1, &ebo_); ebo_ = 0; }
    if (vboNormals_) { glDeleteBuffers(1, &vboNormals_); vboNormals_ = 0; }
    if (vboPositions_) { glDeleteBuffers(1, &vboPositions_); vboPositions_ = 0; }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (program_) { glDeleteProgram(program_); program_ = 0; }
    glReady_ = false;
}

bool UltraCanvasSTLElement::OnEvent(const UCEvent& event) {
    switch (event.type) {
        case UCEventType::MouseDown:
            if (event.button == UCMouseButton::Left) {
                dragging_ = true;
                lastMouseX_ = event.pointer.x;
                lastMouseY_ = event.pointer.y;
                return true;
            }
            break;
        case UCEventType::MouseUp:
            if (event.button == UCMouseButton::Left) {
                dragging_ = false;
                return true;
            }
            break;
        case UCEventType::MouseMove:
            if (dragging_) {
                int dx = event.pointer.x - lastMouseX_;
                int dy = event.pointer.y - lastMouseY_;
                lastMouseX_ = event.pointer.x;
                lastMouseY_ = event.pointer.y;
                yaw_ += dx * 0.01f;
                pitch_ += dy * 0.01f;
                const float limit = kPi * 0.5f - 0.01f;
                if (pitch_ > limit) pitch_ = limit;
                if (pitch_ < -limit) pitch_ = -limit;
                RequestRender();
                return true;
            }
            break;
        case UCEventType::MouseWheel: {
            // Wheel zoom: positive delta zooms in.
            distance_ *= (event.wheelDelta > 0) ? 0.9f : 1.1f;
            if (distance_ < 0.2f) distance_ = 0.2f;
            if (distance_ > 50.0f) distance_ = 50.0f;
            RequestRender();
            return true;
        }
        default:
            break;
    }
    return UltraCanvasGLSurface::OnEvent(event);
}

#else // !ULTRACANVAS_ENABLE_GL

// ===== NON-GL FALLBACK =====

UltraCanvasSTLElement::UltraCanvasSTLElement(const std::string& identifier,
                                             float x, float y, float width, float height)
    : UltraCanvasUIElement(identifier, x, y, width, height) {}

bool UltraCanvasSTLElement::LoadFromFile(const std::string& filePath) {
    Mesh3D mesh;
    std::string error;
    if (!UltraCanvasSTLLoader::Load(filePath, mesh, &error)) {
        if (onLoadError) onLoadError(error);
        return false;
    }
    SetMesh(mesh);
    if (onLoadComplete) onLoadComplete();
    return true;
}

void UltraCanvasSTLElement::SetMesh(const Mesh3D& mesh) {
    mesh_ = mesh;
    if (!mesh_.bounds.IsValid()) mesh_.ComputeBounds();
}

bool UltraCanvasSTLElement::SaveToFile(const std::string& filePath, STLFormat format,
                                       std::string* outError) const {
    return UltraCanvasSTLLoader::Save(filePath, mesh_, format, outError);
}

void UltraCanvasSTLElement::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
    if (!ctx) return;
    Rect2Df b = GetBounds();
    Rect2Dd box{b.x, b.y, b.width, b.height};

    ctx->SetFillPaint(Color(30, 33, 40, 255));
    ctx->FillRectangle(box);

    ctx->SetFillPaint(Color(220, 220, 225, 255));
    ctx->SetFontSize(13);
    std::string label = mesh_.Empty()
        ? std::string("STL: (no mesh loaded)")
        : ("STL: " + (mesh_.name.empty() ? std::string("model") : mesh_.name) +
           " — " + std::to_string(mesh_.TriangleCount()) + " triangles");
    ctx->DrawText(label, Point2Dd{b.x + 10.0, b.y + 22.0});
    ctx->DrawText("(build with -DULTRACANVAS_ENABLE_GL=ON for 3D preview)",
                  Point2Dd{b.x + 10.0, b.y + 42.0});
}

#endif // ULTRACANVAS_ENABLE_GL

} // namespace UltraCanvas
