// Apps/DemoApp/UltraCanvasGLModelsTab.cpp
// "3D Models" tab of the OpenGL showcase: loads complex .obj meshes from disk
// into an UltraCanvasGLSurface and renders them with Phong lighting. The camera
// orbits with mouse drag and zooms with the wheel.
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasGLDemoSupport.h"

#ifdef ULTRACANVAS_ENABLE_GL

#include "UltraCanvasGLSurface.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"

#include <memory>
#include <vector>
#include <string>

namespace UltraCanvas {

using namespace gldemo;

namespace {

// -------------------------------------------------------------- demo state
struct ModelEntry {
    std::string label;
    std::string file;       // relative to resources/media (empty = procedural)
    Color color;
};

struct ModelDemoState {
    std::vector<ModelEntry> catalog;
    std::vector<Mesh> meshes;        // CPU meshes, lazily loaded, parallel to catalog
    std::vector<bool> loaded;

    int currentIndex = 0;            // model shown
    int uploadedIndex = -1;          // model currently in the GPU buffer

    // Orbit camera
    float yaw = 0.6f;
    float pitch = 0.35f;
    float distance = 3.2f;

    bool autoRotate = true;
    float autoSpeed = 0.5f;
    bool wireframe = false;
    float spin = 0.0f;               // accumulated auto-rotation
};

struct ModelGLResources {
    GLuint program = 0;
    GLuint vao = 0, vbo = 0, ebo = 0;
    GLint uModel = -1, uView = -1, uProj = -1, uNormalMat = -1;
    GLint uColor = -1, uLightDirA = -1, uLightDirB = -1, uCameraPos = -1;
    GLsizei indexCount = 0;
    bool ready = false;
};

const char* kModelVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat3 uNormalMat;
out vec3 vWorldPos;
out vec3 vNormal;
void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    vNormal = normalize(uNormalMat * aNormal);
    gl_Position = uProj * uView * world;
}
)";

const char* kModelFS = R"(#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
out vec4 FragColor;
uniform vec3 uColor;
uniform vec3 uLightDirA;
uniform vec3 uLightDirB;
uniform vec3 uCameraPos;
void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCameraPos - vWorldPos);

    vec3 La = normalize(uLightDirA);
    vec3 Lb = normalize(uLightDirB);

    float diffA = max(dot(N, La), 0.0);
    float diffB = max(dot(N, Lb), 0.0) * 0.4;

    vec3 Ha = normalize(La + V);
    float specA = pow(max(dot(N, Ha), 0.0), 48.0);

    vec3 ambient = uColor * 0.18;
    vec3 keyLight = vec3(1.0, 0.97, 0.9);
    vec3 fillLight = vec3(0.5, 0.6, 0.8);
    vec3 color = ambient
               + uColor * keyLight * diffA
               + uColor * fillLight * diffB
               + keyLight * specA * 0.6;

    // subtle rim light for shape readability
    float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    color += vec3(0.15, 0.18, 0.25) * rim;

    FragColor = vec4(color, 1.0);
}
)";

// GLSurface subclass that turns mouse drag into orbit and wheel into zoom.
class OrbitGLSurface : public UltraCanvasGLSurface {
public:
    OrbitGLSurface(const GLSurfaceConfig& cfg, const std::string& id,
                   float x, float y, float w, float h,
                   std::shared_ptr<ModelDemoState> state)
        : UltraCanvasGLSurface(cfg, id, x, y, w, h), state_(std::move(state)) {}

    bool OnEvent(const UCEvent& event) override {
        switch (event.type) {
            case UCEventType::MouseDown:
                if (event.button == UCMouseButton::Left) {
                    dragging_ = true;
                    lastX_ = event.pointer.x;
                    lastY_ = event.pointer.y;
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
                    float dx = float(event.pointer.x - lastX_);
                    float dy = float(event.pointer.y - lastY_);
                    lastX_ = event.pointer.x;
                    lastY_ = event.pointer.y;
                    state_->yaw   += dx * 0.01f;
                    state_->pitch += dy * 0.01f;
                    const float lim = kPi * 0.49f;
                    state_->pitch = std::max(-lim, std::min(lim, state_->pitch));
                    RequestRender();
                    return true;
                }
                break;
            case UCEventType::MouseWheel: {
                float step = (event.wheelDelta > 0) ? 0.9f : 1.1f;
                state_->distance = std::max(1.6f, std::min(8.0f, state_->distance * step));
                RequestRender();
                return true;
            }
            default:
                break;
        }
        return UltraCanvasGLSurface::OnEvent(event);
    }

private:
    std::shared_ptr<ModelDemoState> state_;
    bool dragging_ = false;
    int lastX_ = 0, lastY_ = 0;
};

// Load (and cache) the CPU mesh for the given catalog entry.
const Mesh& EnsureMesh(const std::shared_ptr<ModelDemoState>& st, int index) {
    if (!st->loaded[index]) {
        Mesh mesh;
        const ModelEntry& e = st->catalog[index];
        bool ok = false;
        if (!e.file.empty()) {
            std::string path = NormalizePath(GetResourcesDir() + e.file);
            ok = LoadOBJ(path, mesh);
            if (ok) mesh.NormalizeToUnit();
        }
        if (!ok) {
            mesh = MakeIcosphere(3);   // fallback so the tab is never empty
        }
        st->meshes[index] = std::move(mesh);
        st->loaded[index] = true;
    }
    return st->meshes[index];
}

void UploadMesh(ModelGLResources& gl, const Mesh& mesh) {
    glBindVertexArray(gl.vao);
    glBindBuffer(GL_ARRAY_BUFFER, gl.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 mesh.vertices.size() * sizeof(float),
                 mesh.vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gl.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 mesh.indices.size() * sizeof(uint32_t),
                 mesh.indices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    gl.indexCount = (GLsizei)mesh.indices.size();
}

} // namespace

std::shared_ptr<UltraCanvasUIElement> CreateGLModelsTab() {
    auto root = std::make_shared<UltraCanvasContainer>("GLModelsTab", 0, 0, 1000, 700);

    auto title = std::make_shared<UltraCanvasLabel>("GLModelsTitle", 16, 8, 700, 24);
    title->SetText("Complex 3D Models — Wavefront OBJ loaded into the GL surface");
    title->SetFontSize(14);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Color(40, 40, 120, 255));
    root->AddChild(title);

    auto state = std::make_shared<ModelDemoState>();
    state->catalog = {
        {"Torus Knot (11,520 tris)", "media/models/torusknot.obj", Color(230, 120, 60, 255)},
        {"Spring Coil (9,216 tris)", "media/models/spring.obj",   Color(120, 200, 130, 255)},
        {"Ico Sphere (1,280 tris)",  "media/models/icosphere.obj", Color(120, 160, 230, 255)},
    };
    state->meshes.resize(state->catalog.size());
    state->loaded.assign(state->catalog.size(), false);

    auto glRes = std::make_shared<ModelGLResources>();

    GLSurfaceConfig cfg;
    cfg.glVersionMajor = 3; cfg.glVersionMinor = 3; cfg.coreProfile = true;
    cfg.depthBits = 24; cfg.samples = 4;   // MSAA for smooth model edges

    auto surface = std::make_shared<OrbitGLSurface>(cfg, "ModelViewer", 16, 40, 660, 560, state);
    surface->SetRenderMode(RenderMode::Continuous);

    surface->SetInitCallback([glRes, state]() {
        glRes->program = LinkProgram(kModelVS, kModelFS);
        if (!glRes->program) return;
        glRes->uModel     = glGetUniformLocation(glRes->program, "uModel");
        glRes->uView      = glGetUniformLocation(glRes->program, "uView");
        glRes->uProj      = glGetUniformLocation(glRes->program, "uProj");
        glRes->uNormalMat = glGetUniformLocation(glRes->program, "uNormalMat");
        glRes->uColor     = glGetUniformLocation(glRes->program, "uColor");
        glRes->uLightDirA = glGetUniformLocation(glRes->program, "uLightDirA");
        glRes->uLightDirB = glGetUniformLocation(glRes->program, "uLightDirB");
        glRes->uCameraPos = glGetUniformLocation(glRes->program, "uCameraPos");

        glGenVertexArrays(1, &glRes->vao);
        glGenBuffers(1, &glRes->vbo);
        glGenBuffers(1, &glRes->ebo);

        UploadMesh(*glRes, EnsureMesh(state, state->currentIndex));
        state->uploadedIndex = state->currentIndex;

        glEnable(GL_DEPTH_TEST);
        glRes->ready = true;
    });

    surface->SetRenderCallback([glRes, state](const RenderSurfaceInfo& info) {
        if (!glRes->ready) return;

        if (state->uploadedIndex != state->currentIndex) {
            UploadMesh(*glRes, EnsureMesh(state, state->currentIndex));
            state->uploadedIndex = state->currentIndex;
        }

        glClearColor(0.07f, 0.08f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (state->autoRotate)
            state->spin += float(info.deltaTime) * state->autoSpeed;

        float aspect = info.height > 0 ? float(info.width) / float(info.height) : 1.0f;
        Mat4 proj = Perspective(45.0f * kPi / 180.0f, aspect, 0.05f, 100.0f);

        // Orbit camera position from yaw/pitch/distance
        float cp = std::cos(state->pitch), sp = std::sin(state->pitch);
        float cy = std::cos(state->yaw), sy = std::sin(state->yaw);
        Vec3 eye{state->distance * cp * sy,
                 state->distance * sp,
                 state->distance * cp * cy};
        Mat4 view = LookAt(eye, {0, 0, 0}, {0, 1, 0});
        Mat4 model = RotateY(state->spin);

        // Lighting is done in world space, so the normal matrix uses model only.
        auto nrmModel = NormalMatrix(model);

        glUseProgram(glRes->program);
        glUniformMatrix4fv(glRes->uModel, 1, GL_FALSE, model.m);
        glUniformMatrix4fv(glRes->uView,  1, GL_FALSE, view.m);
        glUniformMatrix4fv(glRes->uProj,  1, GL_FALSE, proj.m);
        glUniformMatrix3fv(glRes->uNormalMat, 1, GL_FALSE, nrmModel.data());

        const Color& c = state->catalog[state->currentIndex].color;
        glUniform3f(glRes->uColor, c.r / 255.0f, c.g / 255.0f, c.b / 255.0f);
        glUniform3f(glRes->uLightDirA, 0.5f, 0.8f, 0.6f);
        glUniform3f(glRes->uLightDirB, -0.6f, 0.3f, -0.5f);
        glUniform3f(glRes->uCameraPos, eye.x, eye.y, eye.z);

        glPolygonMode(GL_FRONT_AND_BACK, state->wireframe ? GL_LINE : GL_FILL);
        glBindVertexArray(glRes->vao);
        glDrawElements(GL_TRIANGLES, glRes->indexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glUseProgram(0);
    });

    surface->SetCleanupCallback([glRes]() {
        if (glRes->ebo) glDeleteBuffers(1, &glRes->ebo);
        if (glRes->vbo) glDeleteBuffers(1, &glRes->vbo);
        if (glRes->vao) glDeleteVertexArrays(1, &glRes->vao);
        if (glRes->program) glDeleteProgram(glRes->program);
        *glRes = ModelGLResources{};
    });

    root->AddChild(surface);

    // ---------------------------------------------------------- control panel
    auto panel = std::make_shared<UltraCanvasContainer>("ModelControls", 690, 40, 296, 560);
    panel->SetBackgroundColor(Color(246, 246, 248, 255));
    panel->SetBorders(1.0f);
    panel->SetPadding(10.0f);

    auto pTitle = std::make_shared<UltraCanvasLabel>("ModelCtrlTitle", 10, 8, 270, 22);
    pTitle->SetText("Model");
    pTitle->SetFontWeight(FontWeight::Bold);
    pTitle->SetFontSize(13);
    panel->AddChild(pTitle);

    auto modelDrop = std::make_shared<UltraCanvasDropdown>("ModelDropdown", 10, 36, 270, 28);
    for (const auto& e : state->catalog) modelDrop->AddItem(e.label);
    modelDrop->SetSelectedIndex(0, false);
    modelDrop->onSelectionChanged = [state, surface](int idx, const DropdownItem&) {
        if (idx >= 0 && idx < (int)state->catalog.size()) {
            state->currentIndex = idx;
            surface->RequestRender();
        }
    };
    panel->AddChild(modelDrop);

    auto rotBtn = std::make_shared<UltraCanvasButton>("AutoRotateBtn", 10, 80, 130, 30);
    rotBtn->SetText("Pause Spin");
    rotBtn->onClick = [rotBtn, state]() {
        state->autoRotate = !state->autoRotate;
        rotBtn->SetText(state->autoRotate ? "Pause Spin" : "Resume Spin");
    };
    panel->AddChild(rotBtn);

    auto wireBtn = std::make_shared<UltraCanvasButton>("WireframeBtn", 150, 80, 130, 30);
    wireBtn->SetText("Wireframe");
    wireBtn->onClick = [wireBtn, state, surface]() {
        state->wireframe = !state->wireframe;
        wireBtn->SetText(state->wireframe ? "Solid" : "Wireframe");
        surface->RequestRender();
    };
    panel->AddChild(wireBtn);

    auto speedLabel = std::make_shared<UltraCanvasLabel>("SpinSpeedLabel", 10, 124, 270, 18);
    speedLabel->SetText("Spin Speed");
    speedLabel->SetFontSize(11);
    panel->AddChild(speedLabel);

    auto speedSlider = std::make_shared<UltraCanvasSlider>("SpinSpeedSlider", 10, 144, 270, 24);
    speedSlider->SetRange(0.0f, 2.5f);
    speedSlider->SetValue(state->autoSpeed);
    speedSlider->onValueChanged = [state](float v) { state->autoSpeed = v; };
    panel->AddChild(speedSlider);

    auto info = std::make_shared<UltraCanvasLabel>("ModelInfo", 10, 186, 270, 360);
    info->SetText(
        "Real OBJ meshes are streamed from\n"
        "media/models/ into the GL surface\n"
        "and shaded with a two-light Phong\n"
        "model plus a rim term.\n\n"
        "Interaction:\n"
        "• Drag with the mouse to orbit\n"
        "• Mouse wheel to zoom\n"
        "• Dropdown switches the model\n"
        "• 4x MSAA smooths the silhouette\n\n"
        "The loader handles v / vn / faces,\n"
        "polygon triangulation and negative\n"
        "indices, and computes smooth normals\n"
        "when a file provides none."
    );
    info->SetFontSize(11);
    info->SetAlignment(TextAlignment::Left);
    info->SetTextColor(Color(60, 60, 60, 255));
    panel->AddChild(info);

    root->AddChild(panel);
    return root;
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_ENABLE_GL
