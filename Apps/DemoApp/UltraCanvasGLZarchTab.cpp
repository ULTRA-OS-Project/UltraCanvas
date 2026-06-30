// Apps/DemoApp/UltraCanvasGLZarchTab.cpp
// "Zarch" tab of the OpenGL showcase: a small real-time 3D simulation inspired by
// David Braben's Zarch (Acorn Archimedes, 1987). A checkerboarded biome terrain
// scrolls toward the camera while a low-poly hover-ship bobs above it, dropping a
// shadow, surrounded by cone "trees" and a particle exhaust trail.
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasGLDemoSupport.h"

#ifdef ULTRACANVAS_ENABLE_GL

#include "UltraCanvasGLSurface.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasContainer.h"

#include <memory>
#include <vector>
#include <cmath>

namespace UltraCanvas {

using namespace gldemo;

namespace {

constexpr int   kGrid = 30;        // cells across / deep
constexpr float kCell = 1.0f;      // world size of one cell
constexpr float kWater = 0.0f;     // water level

// ---------------------------------------------------------------- terrain fn
float TerrainHeight(float gx, float gz) {
    float h = 1.25f * std::sin(gx * 0.30f) * std::cos(gz * 0.27f)
            + 0.60f * std::sin(gx * 0.70f + gz * 0.50f)
            + 0.45f * std::cos(gz * 0.20f - gx * 0.15f)
            + 0.30f * std::sin((gx + gz) * 0.12f);
    return h;
}

Vec3 BiomeColor(float h, int checker) {
    Vec3 c;
    if (h <= kWater)                 c = {0.25f, 0.40f, 0.75f};   // water
    else if (h < 0.45f)              c = {0.80f, 0.78f, 0.45f};   // sand
    else if (h < 1.4f)               c = {0.30f, 0.62f, 0.30f};   // grass
    else if (h < 2.1f)              c = {0.55f, 0.50f, 0.42f};    // rock
    else                            c = {0.92f, 0.92f, 0.95f};    // snow
    float tint = (checker & 1) ? 0.82f : 1.0f;                    // checkerboard
    return {c.x * tint, c.y * tint, c.z * tint};
}

float Hash2(int x, int y) {
    // Use unsigned arithmetic throughout: the grid indices reach well past the
    // point where these large multipliers overflow a signed int, and signed
    // overflow is undefined behaviour. With -O0 it happens to wrap, but an
    // optimised (release) build is free to miscompile it.
    uint32_t n = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    n = (n ^ (n >> 13)) * 1274126177u;
    return float((n ^ (n >> 16)) & 0x7fffffffu) / float(0x7fffffff);
}

// --------------------------------------------------------------- shaders
const char* kLitVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec3 aColor;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat3 uNormalMat;
out vec3 vNormal;
out vec3 vColor;
out float vDist;
void main(){
    vec4 world = uModel * vec4(aPos,1.0);
    vNormal = normalize(uNormalMat * aNormal);
    vColor = aColor;
    vec4 viewPos = uView * world;
    vDist = -viewPos.z;
    gl_Position = uProj * viewPos;
}
)";

const char* kLitFS = R"(#version 330 core
in vec3 vNormal;
in vec3 vColor;
in float vDist;
out vec4 FragColor;
uniform vec3 uLightDir;
void main(){
    vec3 N = normalize(vNormal);
    float diff = max(dot(N, normalize(uLightDir)), 0.0);
    vec3 col = vColor * (0.35 + 0.65*diff);
    // distance fade into the dark horizon
    float fog = clamp(1.0 - (vDist - 14.0)/16.0, 0.0, 1.0);
    col = mix(vec3(0.0), col, fog);
    FragColor = vec4(col, 1.0);
}
)";

const char* kFlatVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
void main(){ gl_Position = uProj * uView * uModel * vec4(aPos,1.0); }
)";

const char* kFlatFS = R"(#version 330 core
out vec4 FragColor;
uniform vec4 uColor;
void main(){ FragColor = uColor; }
)";

const char* kParticleVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aColor;
layout(location=2) in float aSize;
uniform mat4 uView;
uniform mat4 uProj;
out vec4 vColor;
void main(){
    vec4 viewPos = uView * vec4(aPos,1.0);
    gl_Position = uProj * viewPos;
    gl_PointSize = aSize / max(-viewPos.z, 0.5);
    vColor = aColor;
}
)";

const char* kParticleFS = R"(#version 330 core
in vec4 vColor;
out vec4 FragColor;
void main(){
    vec2 d = gl_PointCoord - vec2(0.5);
    float a = smoothstep(0.5, 0.0, length(d));
    FragColor = vec4(vColor.rgb, vColor.a * a);
}
)";

// ------------------------------------------------------------- static meshes
// Flat-shaded cone (apex up). Interleaved pos+normal+color.
std::vector<float> MakeCone(int seg, float radius, float height, Vec3 color) {
    std::vector<float> v;
    auto push = [&](Vec3 p, Vec3 n) {
        v.insert(v.end(), {p.x, p.y, p.z, n.x, n.y, n.z, color.x, color.y, color.z});
    };
    Vec3 apex{0, height, 0};
    for (int i = 0; i < seg; ++i) {
        float a0 = 2 * kPi * i / seg, a1 = 2 * kPi * (i + 1) / seg;
        Vec3 b0{std::cos(a0) * radius, 0, std::sin(a0) * radius};
        Vec3 b1{std::cos(a1) * radius, 0, std::sin(a1) * radius};
        Vec3 n = Normalize(Cross(b1 - apex, b0 - apex));
        push(apex, n); push(b0, n); push(b1, n);
    }
    return v;
}

// Flat-shaded octahedron "ship" body, stretched into a lander silhouette.
std::vector<float> MakeShip() {
    std::vector<float> v;
    Vec3 bodyColor{0.75f, 0.82f, 0.95f};
    Vec3 trimColor{0.30f, 0.35f, 0.55f};
    auto tri = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 col) {
        Vec3 n = Normalize(Cross(b - a, c - a));
        for (Vec3 p : {a, b, c})
            v.insert(v.end(), {p.x, p.y, p.z, n.x, n.y, n.z, col.x, col.y, col.z});
    };
    Vec3 top{0, 0.45f, 0}, bot{0, -0.30f, 0};
    Vec3 e0{0.55f, 0, 0}, e1{0, 0, 0.55f}, e2{-0.55f, 0, 0}, e3{0, 0, -0.55f};
    // upper pyramid (body)
    tri(top, e0, e1, bodyColor); tri(top, e1, e2, bodyColor);
    tri(top, e2, e3, bodyColor); tri(top, e3, e0, bodyColor);
    // lower pyramid (darker trim / thrusters)
    tri(bot, e1, e0, trimColor); tri(bot, e2, e1, trimColor);
    tri(bot, e3, e2, trimColor); tri(bot, e0, e3, trimColor);
    return v;
}

// ------------------------------------------------------------------- state
struct Particle { Vec3 pos, vel; float life, maxLife; };

struct ZarchState {
    float scroll = 0.0f;
    float speed = 4.0f;
    bool running = true;

    float shipX = 0.0f;        // current
    float shipTargetX = 0.0f;  // steered target
    bool dragging = false;
    float bobPhase = 0.0f;

    std::vector<Particle> particles;
    float spawnAccum = 0.0f;
};

struct ZarchGLResources {
    GLuint litProg = 0, flatProg = 0, partProg = 0;
    // lit uniforms
    GLint lModel = -1, lView = -1, lProj = -1, lNormal = -1, lLight = -1;
    // flat uniforms
    GLint fModel = -1, fView = -1, fProj = -1, fColor = -1;
    // particle uniforms
    GLint pView = -1, pProj = -1;

    GLuint terrainVAO = 0, terrainVBO = 0; GLsizei terrainVerts = 0;
    GLuint shipVAO = 0, shipVBO = 0; GLsizei shipVerts = 0;
    GLuint coneVAO = 0, coneVBO = 0; GLsizei coneVerts = 0;
    GLuint shadowVAO = 0, shadowVBO = 0;
    GLuint partVAO = 0, partVBO = 0;
    bool ready = false;
};

void SetupLitAttribs() {
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
}

// Rebuild the scrolling terrain mesh (flat-shaded, per-tile biome colour).
void BuildTerrain(std::vector<float>& out, float scroll) {
    out.clear();
    float frac = std::fmod(scroll, kCell);
    int scrollCells = (int)std::floor(scroll / kCell);

    auto push = [&](Vec3 p, Vec3 n, Vec3 c) {
        out.insert(out.end(), {p.x, p.y, p.z, n.x, n.y, n.z, c.x, c.y, c.z});
    };

    for (int iz = 0; iz < kGrid; ++iz) {
        for (int ix = 0; ix < kGrid; ++ix) {
            int gx = ix - kGrid / 2;
            int gz = -(iz + scrollCells);   // global cell index (scrolls)

            float wx0 = (ix - kGrid / 2) * kCell;
            float wx1 = wx0 + kCell;
            float wz0 = -iz * kCell + frac;
            float wz1 = wz0 - kCell;

            float h00 = TerrainHeight(gx,     -(float)(iz + scrollCells));
            float h10 = TerrainHeight(gx + 1, -(float)(iz + scrollCells));
            float h01 = TerrainHeight(gx,     -(float)(iz + 1 + scrollCells));
            float h11 = TerrainHeight(gx + 1, -(float)(iz + 1 + scrollCells));

            // flatten water
            auto level = [](float h) { return h <= kWater ? kWater : h; };
            float l00 = level(h00), l10 = level(h10), l01 = level(h01), l11 = level(h11);

            Vec3 p00{wx0, l00, wz0}, p10{wx1, l10, wz0};
            Vec3 p01{wx0, l01, wz1}, p11{wx1, l11, wz1};

            int checker = (gx + gz) & 1;
            float havg = (h00 + h10 + h01 + h11) * 0.25f;
            Vec3 col = BiomeColor(havg, checker);

            Vec3 nA = Normalize(Cross(p10 - p00, p01 - p00));
            push(p00, nA, col); push(p10, nA, col); push(p01, nA, col);
            Vec3 nB = Normalize(Cross(p11 - p10, p01 - p10));
            push(p10, nB, col); push(p11, nB, col); push(p01, nB, col);
        }
    }
}

class ZarchGLSurface : public UltraCanvasGLSurface {
public:
    ZarchGLSurface(const GLSurfaceConfig& cfg, const std::string& id,
                   float x, float y, float w, float h, std::shared_ptr<ZarchState> st)
        : UltraCanvasGLSurface(cfg, id, x, y, w, h), state_(std::move(st)) {}

    bool OnEvent(const UCEvent& event) override {
        switch (event.type) {
            case UCEventType::MouseDown:
                if (event.button == UCMouseButton::Left) {
                    state_->dragging = true;
                    lastX_ = event.pointer.x;
                    return true;
                }
                break;
            case UCEventType::MouseUp:
                state_->dragging = false;
                break;
            case UCEventType::MouseMove:
                if (state_->dragging) {
                    float dx = float(event.pointer.x - lastX_);
                    lastX_ = event.pointer.x;
                    float lim = kGrid * 0.5f - 3.0f;
                    state_->shipTargetX = std::max(-lim, std::min(lim,
                                          state_->shipTargetX + dx * 0.05f));
                    return true;
                }
                break;
            default: break;
        }
        return UltraCanvasGLSurface::OnEvent(event);
    }

private:
    std::shared_ptr<ZarchState> state_;
    int lastX_ = 0;
};

} // namespace

std::shared_ptr<UltraCanvasUIElement> CreateGLZarchTab() {
    auto root = std::make_shared<UltraCanvasContainer>("GLZarchTab", 0, 0, 1004, 692);

    auto title = std::make_shared<UltraCanvasLabel>("GLZarchTitle", 16, 8, 760, 24);
    title->SetText("Zarch — a low-poly hover-ship simulation over scrolling biome terrain");
    title->SetFontSize(14);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Color(40, 40, 120, 255));
    root->AddChild(title);

    auto state = std::make_shared<ZarchState>();
    state->particles.resize(220);
    for (auto& p : state->particles) { p.life = 0.0f; p.maxLife = 0.0f; }

    auto glRes = std::make_shared<ZarchGLResources>();

    GLSurfaceConfig cfg;
    cfg.glVersionMajor = 3; cfg.glVersionMinor = 3; cfg.coreProfile = true;
    cfg.depthBits = 24; cfg.samples = 4;

    auto surface = std::make_shared<ZarchGLSurface>(cfg, "ZarchSurface", 16, 40, 660, 560, state);
    surface->SetRenderMode(RenderMode::Continuous);

    surface->SetInitCallback([glRes]() {
        glRes->litProg  = LinkProgram(kLitVS, kLitFS);
        glRes->flatProg = LinkProgram(kFlatVS, kFlatFS);
        glRes->partProg = LinkProgram(kParticleVS, kParticleFS);
        if (!glRes->litProg || !glRes->flatProg || !glRes->partProg) return;

        glRes->lModel  = glGetUniformLocation(glRes->litProg, "uModel");
        glRes->lView   = glGetUniformLocation(glRes->litProg, "uView");
        glRes->lProj   = glGetUniformLocation(glRes->litProg, "uProj");
        glRes->lNormal = glGetUniformLocation(glRes->litProg, "uNormalMat");
        glRes->lLight  = glGetUniformLocation(glRes->litProg, "uLightDir");

        glRes->fModel = glGetUniformLocation(glRes->flatProg, "uModel");
        glRes->fView  = glGetUniformLocation(glRes->flatProg, "uView");
        glRes->fProj  = glGetUniformLocation(glRes->flatProg, "uProj");
        glRes->fColor = glGetUniformLocation(glRes->flatProg, "uColor");

        glRes->pView = glGetUniformLocation(glRes->partProg, "uView");
        glRes->pProj = glGetUniformLocation(glRes->partProg, "uProj");

        // terrain (dynamic)
        glGenVertexArrays(1, &glRes->terrainVAO);
        glGenBuffers(1, &glRes->terrainVBO);
        glBindVertexArray(glRes->terrainVAO);
        glBindBuffer(GL_ARRAY_BUFFER, glRes->terrainVBO);
        glBufferData(GL_ARRAY_BUFFER, kGrid * kGrid * 6 * 9 * sizeof(float),
                     nullptr, GL_DYNAMIC_DRAW);
        SetupLitAttribs();

        // ship (static)
        auto ship = MakeShip();
        glRes->shipVerts = (GLsizei)(ship.size() / 9);
        glGenVertexArrays(1, &glRes->shipVAO);
        glGenBuffers(1, &glRes->shipVBO);
        glBindVertexArray(glRes->shipVAO);
        glBindBuffer(GL_ARRAY_BUFFER, glRes->shipVBO);
        glBufferData(GL_ARRAY_BUFFER, ship.size() * sizeof(float), ship.data(), GL_STATIC_DRAW);
        SetupLitAttribs();

        // cone tree (static, reused for every tree)
        auto cone = MakeCone(7, 0.35f, 1.1f, Vec3{0.16f, 0.45f, 0.20f});
        glRes->coneVerts = (GLsizei)(cone.size() / 9);
        glGenVertexArrays(1, &glRes->coneVAO);
        glGenBuffers(1, &glRes->coneVBO);
        glBindVertexArray(glRes->coneVAO);
        glBindBuffer(GL_ARRAY_BUFFER, glRes->coneVBO);
        glBufferData(GL_ARRAY_BUFFER, cone.size() * sizeof(float), cone.data(), GL_STATIC_DRAW);
        SetupLitAttribs();

        // shadow quad (static, position only)
        float quad[] = {-0.5f,0,-0.5f, 0.5f,0,-0.5f, 0.5f,0,0.5f,
                        -0.5f,0,-0.5f, 0.5f,0,0.5f, -0.5f,0,0.5f};
        glGenVertexArrays(1, &glRes->shadowVAO);
        glGenBuffers(1, &glRes->shadowVBO);
        glBindVertexArray(glRes->shadowVAO);
        glBindBuffer(GL_ARRAY_BUFFER, glRes->shadowVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        // particles (dynamic): pos(3)+color(4)+size(1)
        glGenVertexArrays(1, &glRes->partVAO);
        glGenBuffers(1, &glRes->partVBO);
        glBindVertexArray(glRes->partVAO);
        glBindBuffer(GL_ARRAY_BUFFER, glRes->partVBO);
        glBufferData(GL_ARRAY_BUFFER, 220 * 8 * sizeof(float), nullptr, GL_STREAM_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(7 * sizeof(float)));
        glEnableVertexAttribArray(2);

        glBindVertexArray(0);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_PROGRAM_POINT_SIZE);
        glRes->ready = true;
    });

    surface->SetRenderCallback([glRes, state](const RenderSurfaceInfo& info) {
        if (!glRes->ready) return;
        float dt = float(info.deltaTime);
        if (dt > 0.1f) dt = 0.1f;   // clamp after stalls

        if (state->running) {
            state->scroll += dt * state->speed;
            state->bobPhase += dt * 2.2f;
        }
        // ease ship toward steered target, plus a gentle idle sway
        if (!state->dragging)
            state->shipTargetX = std::sin(state->scroll * 0.15f) * (kGrid * 0.18f);
        state->shipX += (state->shipTargetX - state->shipX) * std::min(1.0f, dt * 3.0f);

        float shipBob = 2.8f + std::sin(state->bobPhase) * 0.35f;
        float shipZ = -6.0f;
        // Terrain height directly beneath the ship (for the ground shadow).
        // World z = shipZ maps to global cell index gz = -(scroll - shipZ)/kCell.
        float groundUnderShip = std::max(kWater,
            TerrainHeight(state->shipX / kCell, -(state->scroll - shipZ) / kCell));

        // ---- camera: behind and above, looking forward over the terrain
        Vec3 eye{0.0f, 7.5f, 9.0f};
        Vec3 look{state->shipX * 0.3f, 1.5f, -10.0f};
        Mat4 view = LookAt(eye, look, {0, 1, 0});
        float aspect = info.height > 0 ? float(info.width) / float(info.height) : 1.0f;
        Mat4 proj = Perspective(52.0f * kPi / 180.0f, aspect, 0.1f, 80.0f);
        Mat4 ident = Mat4::Identity();
        auto identN = NormalMatrix(ident);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ---- terrain
        std::vector<float> terrain;
        BuildTerrain(terrain, state->scroll);
        glRes->terrainVerts = (GLsizei)(terrain.size() / 9);
        glBindBuffer(GL_ARRAY_BUFFER, glRes->terrainVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, terrain.size() * sizeof(float), terrain.data());

        glUseProgram(glRes->litProg);
        glUniformMatrix4fv(glRes->lView, 1, GL_FALSE, view.m);
        glUniformMatrix4fv(glRes->lProj, 1, GL_FALSE, proj.m);
        glUniform3f(glRes->lLight, 0.4f, 0.85f, 0.35f);

        glUniformMatrix4fv(glRes->lModel, 1, GL_FALSE, ident.m);
        glUniformMatrix3fv(glRes->lNormal, 1, GL_FALSE, identN.data());
        glBindVertexArray(glRes->terrainVAO);
        glDrawArrays(GL_TRIANGLES, 0, glRes->terrainVerts);

        // ---- trees: scatter cones on grass tiles, scrolling with the terrain
        float frac = std::fmod(state->scroll, kCell);
        int scrollCells = (int)std::floor(state->scroll / kCell);
        glBindVertexArray(glRes->coneVAO);
        for (int iz = 2; iz < kGrid - 1; ++iz) {
            for (int ix = 0; ix < kGrid; ++ix) {
                int gx = ix - kGrid / 2;
                int gz = -(iz + scrollCells);
                if (Hash2(gx, gz) < 0.90f) continue;           // sparse
                float h = TerrainHeight(gx, -(float)(iz + scrollCells));
                if (h < 0.45f || h >= 1.4f) continue;          // grass only
                float wx = gx * kCell;
                float wz = -iz * kCell + frac;
                float s = 0.7f + Hash2(gx + 7, gz - 3) * 0.7f;
                Mat4 model = Translate(wx, h, wz) * Scale(s, s, s);
                auto nrm = NormalMatrix(model);
                glUniformMatrix4fv(glRes->lModel, 1, GL_FALSE, model.m);
                glUniformMatrix3fv(glRes->lNormal, 1, GL_FALSE, nrm.data());
                glDrawArrays(GL_TRIANGLES, 0, glRes->coneVerts);
            }
        }

        // ---- ship
        Mat4 shipModel = Translate(state->shipX, shipBob, shipZ)
                       * RotateY(std::sin(state->scroll * 0.15f) * 0.3f)
                       * RotateZ(-(state->shipTargetX - state->shipX) * 0.15f);
        auto shipN = NormalMatrix(shipModel);
        glUniformMatrix4fv(glRes->lModel, 1, GL_FALSE, shipModel.m);
        glUniformMatrix3fv(glRes->lNormal, 1, GL_FALSE, shipN.data());
        glBindVertexArray(glRes->shipVAO);
        glDrawArrays(GL_TRIANGLES, 0, glRes->shipVerts);

        // ---- shadow on the ground (flat, blended)
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glUseProgram(glRes->flatProg);
        glUniformMatrix4fv(glRes->fView, 1, GL_FALSE, view.m);
        glUniformMatrix4fv(glRes->fProj, 1, GL_FALSE, proj.m);
        float shadowScale = 1.4f + (shipBob - 2.8f);
        Mat4 shadowModel = Translate(state->shipX, groundUnderShip + 0.02f, shipZ)
                         * Scale(shadowScale, 1.0f, shadowScale);
        glUniformMatrix4fv(glRes->fModel, 1, GL_FALSE, shadowModel.m);
        glUniform4f(glRes->fColor, 0.0f, 0.0f, 0.0f, 0.35f);
        glBindVertexArray(glRes->shadowVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // ---- particle exhaust (additive)
        if (state->running) {
            state->spawnAccum += dt;
            float spawnEvery = 0.012f;
            while (state->spawnAccum > spawnEvery) {
                state->spawnAccum -= spawnEvery;
                for (auto& p : state->particles) {
                    if (p.life <= 0.0f) {
                        float rx = (Hash2((int)(state->scroll * 97), (int)p.maxLife) - 0.5f) * 0.2f;
                        float rz = (Hash2((int)(state->scroll * 53) + 11, (int)(p.pos.x * 9)) - 0.5f) * 0.2f;
                        p.pos = {state->shipX + rx, shipBob - 0.25f, shipZ + rz};
                        p.vel = {rx * 2.0f, -1.2f - Hash2((int)(p.pos.z*31),7)*0.8f, 1.5f + rz};
                        p.maxLife = 0.8f + Hash2((int)(state->scroll*13), (int)(p.pos.x*17)) * 0.6f;
                        p.life = p.maxLife;
                        break;
                    }
                }
            }
        }
        std::vector<float> pbuf;
        pbuf.reserve(state->particles.size() * 8);
        for (auto& p : state->particles) {
            if (p.life <= 0.0f) continue;
            p.life -= dt;
            p.vel.y -= dt * 1.5f;
            p.pos = p.pos + p.vel * dt;
            float t = p.life / p.maxLife;          // 1 -> 0
            // hot (white/yellow) -> orange -> red as it cools
            float r = 1.0f;
            float g = 0.3f + 0.7f * t;
            float b = 0.1f * t;
            float a = t;
            float size = 90.0f * (0.4f + 0.6f * t);
            pbuf.insert(pbuf.end(), {p.pos.x, p.pos.y, p.pos.z, r, g, b, a, size});
        }
        if (!pbuf.empty()) {
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);   // additive glow
            glUseProgram(glRes->partProg);
            glUniformMatrix4fv(glRes->pView, 1, GL_FALSE, view.m);
            glUniformMatrix4fv(glRes->pProj, 1, GL_FALSE, proj.m);
            glBindVertexArray(glRes->partVAO);
            glBindBuffer(GL_ARRAY_BUFFER, glRes->partVBO);
            glBufferSubData(GL_ARRAY_BUFFER, 0, pbuf.size() * sizeof(float), pbuf.data());
            glDrawArrays(GL_POINTS, 0, (GLsizei)(pbuf.size() / 8));
        }

        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glBindVertexArray(0);
        glUseProgram(0);
    });

    surface->SetCleanupCallback([glRes]() {
        GLuint vaos[] = {glRes->terrainVAO, glRes->shipVAO, glRes->coneVAO,
                         glRes->shadowVAO, glRes->partVAO};
        GLuint vbos[] = {glRes->terrainVBO, glRes->shipVBO, glRes->coneVBO,
                         glRes->shadowVBO, glRes->partVBO};
        for (GLuint v : vaos) if (v) glDeleteVertexArrays(1, &v);
        for (GLuint b : vbos) if (b) glDeleteBuffers(1, &b);
        if (glRes->litProg) glDeleteProgram(glRes->litProg);
        if (glRes->flatProg) glDeleteProgram(glRes->flatProg);
        if (glRes->partProg) glDeleteProgram(glRes->partProg);
        *glRes = ZarchGLResources{};
    });

    root->AddChild(surface);

    // ---------------------------------------------------------- control panel
    auto panel = std::make_shared<UltraCanvasContainer>("ZarchControls", 690, 40, 296, 560);
    panel->SetBackgroundColor(Color(246, 246, 248, 255));
    panel->SetBorders(1.0f);
    panel->SetPadding(10.0f);

    auto pTitle = std::make_shared<UltraCanvasLabel>("ZarchCtrlTitle", 10, 8, 270, 22);
    pTitle->SetText("Flight");
    pTitle->SetFontWeight(FontWeight::Bold);
    pTitle->SetFontSize(13);
    panel->AddChild(pTitle);

    auto runBtn = std::make_shared<UltraCanvasButton>("ZarchRunBtn", 10, 36, 130, 30);
    runBtn->SetText("Pause");
    runBtn->onClick = [runBtn, state]() {
        state->running = !state->running;
        runBtn->SetText(state->running ? "Pause" : "Resume");
    };
    panel->AddChild(runBtn);

    auto speedLabel = std::make_shared<UltraCanvasLabel>("ZarchSpeedLabel", 10, 80, 270, 18);
    speedLabel->SetText("Flight Speed");
    speedLabel->SetFontSize(11);
    panel->AddChild(speedLabel);

    auto speedSlider = std::make_shared<UltraCanvasSlider>("ZarchSpeedSlider", 10, 100, 270, 24);
    speedSlider->SetRange(0.0f, 12.0f);
    speedSlider->SetValue(state->speed);
    speedSlider->onValueChanged = [state](float v) { state->speed = v; };
    panel->AddChild(speedSlider);

    auto info = std::make_shared<UltraCanvasLabel>("ZarchInfo", 10, 142, 270, 400);
    info->SetText(
        "A miniature homage to Zarch (1987).\n\n"
        "Drag the mouse to steer the ship\n"
        "left and right; release to let it\n"
        "auto-pilot.\n\n"
        "Rendered every frame:\n"
        "• Scrolling biome terrain rebuilt on\n"
        "  the CPU with checkerboard tiles\n"
        "  (water, sand, grass, rock, snow)\n"
        "• Flat-shaded hover-ship that bobs\n"
        "  and banks into its motion\n"
        "• Soft ground shadow that grows\n"
        "  with altitude\n"
        "• Cone 'trees' scattered on grass\n"
        "• Additive particle exhaust trail\n\n"
        "All composited back into the\n"
        "UltraCanvas widget tree."
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
