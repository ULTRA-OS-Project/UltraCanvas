// Apps/DemoApp/UltraCanvasGLShaderTab.cpp
// "Shaders" tab of the OpenGL showcase: a full-screen quad driven entirely by
// animated fragment shaders. A dropdown switches between several procedural
// effects (plasma, raymarched scene, Julia fractal, tunnel, warp starfield,
// a Twigl/つぶやきGLSL "geek-mode" chaotic lattice, a numerically-integrated
// Rössler strange attractor, and a p5.js "Ball Surface" port).
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasGLDemoSupport.h"

#ifdef ULTRACANVAS_ENABLE_GL

#include "UltraCanvasGLSurface.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasContainer.h"

#include <memory>
#include <vector>
#include <string>

namespace UltraCanvas {

using namespace gldemo;

namespace {

// Shared fullscreen-triangle vertex shader: emits clip-space coverage and a
// 0..1 uv with no vertex buffer needed (gl_VertexID trick).
const char* kFullscreenVS = R"(#version 330 core
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Common fragment-shader preamble (uniforms + helpers).
const char* kFragHeader = R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform float uTime;
uniform vec2 uResolution;
mat2 rot(float a){ float c=cos(a), s=sin(a); return mat2(c,-s,s,c); }
float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1,311.7)))*43758.5453); }
)";

struct ShaderEffect {
    std::string label;
    std::string body;     // fragment-shader main(), appended to kFragHeader
};

std::vector<ShaderEffect> BuildEffects() {
    std::vector<ShaderEffect> fx;

    fx.push_back({"Plasma", R"(
void main(){
    vec2 uv = (vUV - 0.5) * vec2(uResolution.x/uResolution.y, 1.0);
    float t = uTime;
    float v = 0.0;
    v += sin((uv.x*8.0) + t);
    v += sin((uv.y*8.0 + t)*0.8);
    v += sin((uv.x+uv.y)*8.0 + t*1.3);
    float cx = uv.x + 0.5*sin(t*0.3);
    float cy = uv.y + 0.5*cos(t*0.4);
    v += sin(sqrt(cx*cx+cy*cy)*12.0 + t);
    v *= 0.25;
    vec3 col = 0.5 + 0.5*cos(6.2831*(vec3(0.0,0.33,0.67) + v));
    FragColor = vec4(col, 1.0);
}
)"});

    fx.push_back({"Raymarched Scene", R"(
float sdSphere(vec3 p, float r){ return length(p) - r; }
float sdPlane(vec3 p){ return p.y + 1.0; }
float map(vec3 p){
    vec3 q = p;
    q.x -= 1.1*sin(uTime*0.7);
    float s1 = sdSphere(q, 0.7);
    vec3 r = p; r.x += 1.1*sin(uTime*0.7);
    r.y -= 0.3*sin(uTime*1.3);
    float s2 = sdSphere(r, 0.5);
    float pl = sdPlane(p);
    float k = 0.6;
    float h = clamp(0.5 + 0.5*(s2-s1)/k, 0.0, 1.0);
    float blob = mix(s2, s1, h) - k*h*(1.0-h);
    return min(blob, pl);
}
vec3 calcNormal(vec3 p){
    vec2 e = vec2(0.001, 0.0);
    return normalize(vec3(
        map(p+e.xyy)-map(p-e.xyy),
        map(p+e.yxy)-map(p-e.yxy),
        map(p+e.yyx)-map(p-e.yyx)));
}
void main(){
    vec2 uv = (vUV*2.0-1.0);
    uv.x *= uResolution.x/uResolution.y;
    vec3 ro = vec3(0.0, 0.6, 4.0);
    vec3 rd = normalize(vec3(uv, -1.6));
    float a = sin(uTime*0.2)*0.3;
    ro.xz *= rot(a); rd.xz *= rot(a);
    float t = 0.0; float d = 0.0; bool hit=false;
    for(int i=0;i<96;i++){
        vec3 p = ro + rd*t;
        d = map(p);
        if(d < 0.001){ hit=true; break; }
        t += d;
        if(t > 30.0) break;
    }
    vec3 col = vec3(0.05,0.07,0.11) + 0.15*rd.y;
    if(hit){
        vec3 p = ro + rd*t;
        vec3 n = calcNormal(p);
        vec3 L = normalize(vec3(0.7,0.9,0.4));
        float diff = max(dot(n,L),0.0);
        float spec = pow(max(dot(reflect(-L,n), -rd),0.0), 32.0);
        float shadow = 1.0;
        float ts = 0.05;
        for(int i=0;i<32;i++){
            float hs = map(p + L*ts);
            if(hs<0.001){ shadow=0.3; break; }
            ts += hs;
            if(ts>8.0) break;
        }
        vec3 base = (p.y < -0.95)
            ? vec3(0.2,0.25,0.3)*(0.5+0.5*mod(floor(p.x)+floor(p.z),2.0))
            : vec3(0.9,0.4,0.3);
        col = base*(0.2 + diff*shadow) + spec*shadow;
        float fog = exp(-0.02*t*t);
        col = mix(vec3(0.05,0.07,0.11), col, fog);
    }
    col = pow(col, vec3(0.4545));
    FragColor = vec4(col,1.0);
}
)"});

    fx.push_back({"Julia Fractal", R"(
void main(){
    vec2 uv = (vUV*2.0-1.0);
    uv.x *= uResolution.x/uResolution.y;
    uv *= 1.5;
    vec2 c = 0.7885 * vec2(cos(uTime*0.4), sin(uTime*0.4));
    vec2 z = uv;
    float i;
    const float MAXI = 128.0;
    for(i=0.0;i<MAXI;i++){
        z = vec2(z.x*z.x - z.y*z.y, 2.0*z.x*z.y) + c;
        if(dot(z,z) > 4.0) break;
    }
    float m = i/MAXI;
    if(i>=MAXI){ FragColor = vec4(0.02,0.02,0.04,1.0); return; }
    float sm = m + 1.0 - log(log(length(z)))/log(2.0)/MAXI;
    vec3 col = 0.5 + 0.5*cos(6.2831*(vec3(0.0,0.4,0.7) + sm*3.0 + uTime*0.1));
    FragColor = vec4(col,1.0);
}
)"});

    fx.push_back({"Tunnel", R"(
void main(){
    vec2 uv = (vUV*2.0-1.0);
    uv.x *= uResolution.x/uResolution.y;
    float a = atan(uv.y, uv.x);
    float r = length(uv);
    float u = a/3.14159;
    float v = 0.3/r + uTime*0.5;
    float chk = mod(floor(u*8.0) + floor(v*4.0), 2.0);
    vec3 base = mix(vec3(0.1,0.2,0.5), vec3(0.9,0.6,0.2), chk);
    float glow = smoothstep(0.0, 1.2, r);
    vec3 col = base * r * 1.4;
    col += vec3(0.6,0.7,1.0)*(1.0-glow)*0.6;
    FragColor = vec4(col,1.0);
}
)"});

    fx.push_back({"Warp Starfield", R"(
void main(){
    vec2 uv = (vUV*2.0-1.0);
    uv.x *= uResolution.x/uResolution.y;
    vec3 col = vec3(0.0);
    for(int l=0;l<3;l++){
        float depth = float(l);
        float t = uTime*(0.4+depth*0.3);
        vec2 p = uv * (1.0 + depth*0.6);
        p *= rot(depth*0.5);
        vec2 g = floor(p*8.0);
        float rnd = hash(g + depth*23.0);
        vec2 cell = fract(p*8.0) - 0.5;
        float tw = 0.5 + 0.5*sin(t*3.0 + rnd*30.0);
        float star = smoothstep(0.12, 0.0, length(cell)) * step(0.92, rnd) * tw;
        vec3 tint = 0.6 + 0.4*cos(vec3(0.0,2.0,4.0) + rnd*10.0);
        col += star * tint * (1.0 - depth*0.25);
    }
    float vig = 1.0 - 0.4*dot(uv,uv);
    FragColor = vec4(col*vig, 1.0);
}
)"});

    // ------------------------------------------------------------------------
    // Twigl / つぶやきGLSL "geek-mode" one-liner, ported to the tab's uniforms.
    // The original implicit variables map as: r->uResolution, FC->gl_FragCoord
    // (reconstructed as vUV*uResolution), t->uTime, o->FragColor accumulator,
    // rotate2D->rot. It raymarches a chaotic cosine lattice (ceil()-quantised
    // cells) and tone-maps the accumulated glow with tanh().
    fx.push_back({"Chaotic Lattice (Twigl)", R"(
void main(){
    vec2 r = uResolution;
    vec2 FC = vUV * r;                 // gl_FragCoord-equivalent pixel position
    float t = uTime;
    vec4 o = vec4(0.0);
    float i = 0.0, d = 0.0, s = 0.0;
    for(; ++i < 1e2; ){
        vec3 p = vec3((FC.xy*2.0 - r.xy)/r.y * d * rot(t*0.5), d - 8.0);
        p.yz *= rot(t*0.5);
        s = 0.01 + 0.1*abs(max(
                cos(dot(sin(ceil(p/0.3)), cos(ceil(p/0.6)).yzx)),
                length(ceil(p)) - 4.0) - i/1e2);
        d += s;
        o.rgb += max(1.3*sin(p*vec3(1.0,2.0,3.0) + i*0.8)/s, vec3(-length(p*p)));
    }
    o = tanh(o*o/1e6);
    FragColor = vec4(o.rgb, 1.0);
}
)"});

    // ------------------------------------------------------------------------
    // The Rössler strange attractor — the nonlinear system the snippet above
    // was *captioned* as (but does not implement). Here the actual ODEs are
    // Euler-integrated each frame into a trajectory that is accumulated as
    // additive glow; an orbiting camera makes the folded band read as 3D.
    //   dx/dt = -y - z
    //   dy/dt =  x + a*y
    //   dz/dt =  b + z*(x - c)         classic params a = b = 0.2, c = 5.7
    fx.push_back({"Rössler Attractor", R"(
void main(){
    vec2 uv = (vUV*2.0 - 1.0);
    uv.x *= uResolution.x/uResolution.y;

    const float a = 0.2, b = 0.2, c = 5.7;   // classic Rössler parameters
    const float dt = 0.02;
    const int   STEPS = 650;

    // Integrate a transient first so we start on the attractor, not at the seed.
    vec3 P = vec3(0.1, 0.0, 0.0);
    for(int i=0;i<300;i++){
        vec3 dP = vec3(-P.y - P.z, P.x + a*P.y, b + P.z*(P.x - c));
        P += dP*dt;
    }

    mat2 yaw = rot(uTime*0.25);               // orbiting camera
    mat2 tilt = rot(0.5);                      // fixed 3/4 view tilt
    vec3 center = vec3(0.0, 0.0, 2.0);         // rough centroid of the band
    float zoom = 0.085;

    vec3 col = vec3(0.0);
    for(int i=0;i<STEPS;i++){
        vec3 dP = vec3(-P.y - P.z, P.x + a*P.y, b + P.z*(P.x - c));
        P += dP*dt;

        vec3 q = (P - center) * zoom;
        q.xz = yaw * q.xz;
        q.yz = tilt * q.yz;

        vec2 s = q.xy / (q.z + 2.2);           // perspective projection
        float dist = length(uv - s);
        float ti = float(i)/float(STEPS);
        vec3 tint = 0.5 + 0.5*cos(6.2831*(vec3(0.0,0.33,0.67) + ti));
        col += tint * 0.0009 / (dist*dist + 0.0008);
    }

    col = col / (col + 1.0);                    // Reinhard tone map
    col = pow(col, vec3(0.4545));               // gamma
    FragColor = vec4(col, 1.0);
}
)"});

    // ------------------------------------------------------------------------
    // "Ball Surface" — port of a p5.js generative sketch. The original draws
    // four overlaid rings (o = 0..3) of 2048 white circles each; every circle's
    // angle i sets a noise-warped radius F, and fill(W, 9*F) makes brightness
    // scale with F over a fading-trail background. p5 has no GLSL runtime here,
    // so each pixel re-accumulates the same per-angle splats: a GLSL value-noise
    // stands in for p5's noise(), brightness is weighted by F (with a constant
    // folding in the trail's steady-state amplification ~255/20), and angles
    // whose radicand is negative are skipped just as p5 skips a NaN circle.
    fx.push_back({"Ball Surface (p5.js port)", R"(
float vnoise(vec2 p){
    vec2 ip = floor(p), fp = fract(p);
    vec2 u = fp*fp*(3.0 - 2.0*fp);
    float a = hash(ip + vec2(0.0,0.0));
    float b = hash(ip + vec2(1.0,0.0));
    float c = hash(ip + vec2(0.0,1.0));
    float dd = hash(ip + vec2(1.0,1.0));
    return mix(mix(a,b,u.x), mix(c,dd,u.x), u.y);
}
void main(){
    // Map the surface to the sketch's 400x400 space, centred and aspect-fit.
    float S = min(uResolution.x, uResolution.y);
    vec2 P = (vUV*uResolution - 0.5*uResolution)/S * 400.0 + vec2(200.0);
    float f = -uTime*0.3;              // sketch advances f by -0.01 per frame
    float acc = 0.0;
    const float TAU = 6.28318530718;
    for(int o=0;o<4;o++){             // four overlaid layers: q(3),q(2),q(1),q(0)
        float of = float(o);
        for(float a=0.0;a<TAU;a+=0.02){              // denser ~314 samples/layer
            float rad = vnoise(vec2(2.0*sin(a+f+of), 7.0*cos(a+f+of)))
                        + 2.0*sin(of+a);
            if(rad < 0.0) continue;                  // p5 sqrt(NaN): circle skipped
            float F = sqrt(rad);
            float rr = 120.0*cos(a + f*0.5);
            vec2 c = vec2(sin(a+F)*rr + 200.0, 120.0*cos(a+F) + 200.0);
            // fill(W, 9*F): brightness ~ F; *0.4 folds in the trail's steady
            // accumulation. Splat radius ~1.7px matches the 3px-diameter circle.
            acc += 0.4 * F * smoothstep(1.7, 0.0, length(P - c));
        }
    }
    vec3 col = vec3(clamp(acc, 0.0, 1.0));
    FragColor = vec4(col, 1.0);
}
)"});

    return fx;
}

struct ShaderState {
    int currentIndex = 0;
    int activeProgram = -1;     // program currently bound's matching index
    float speed = 1.0f;
    float clock = 0.0f;
};

struct ShaderGLResources {
    GLuint vao = 0;                 // empty VAO required by core profile
    std::vector<GLuint> programs;
    std::vector<GLint> timeLoc;
    std::vector<GLint> resLoc;
    bool ready = false;
};

} // namespace

std::shared_ptr<UltraCanvasUIElement> CreateGLShaderTab() {
    auto root = std::make_shared<UltraCanvasContainer>("GLShaderTab", 0, 0, 1000, 700);

    auto title = std::make_shared<UltraCanvasLabel>("GLShaderTitle", 16, 8, 700, 24);
    title->SetText("Shader Playground — full-screen procedural fragment shaders");
    title->SetFontSize(14);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Color(40, 40, 120, 255));
    root->AddChild(title);

    auto effects = std::make_shared<std::vector<ShaderEffect>>(BuildEffects());
    auto state = std::make_shared<ShaderState>();
    auto glRes = std::make_shared<ShaderGLResources>();

    GLSurfaceConfig cfg;
    cfg.glVersionMajor = 3; cfg.glVersionMinor = 3; cfg.coreProfile = true;
    cfg.depthBits = 0; cfg.samples = 1;

    auto surface = std::make_shared<UltraCanvasGLSurface>(cfg, "ShaderSurface", 16, 40, 660, 560);
    surface->SetRenderMode(RenderMode::Continuous);

    surface->SetInitCallback([glRes, effects]() {
        glGenVertexArrays(1, &glRes->vao);   // bound but empty; vertices from gl_VertexID
        for (const auto& fx : *effects) {
            std::string fs = std::string(kFragHeader) + fx.body;
            GLuint prog = LinkProgram(kFullscreenVS, fs.c_str());
            glRes->programs.push_back(prog);
            glRes->timeLoc.push_back(prog ? glGetUniformLocation(prog, "uTime") : -1);
            glRes->resLoc.push_back(prog ? glGetUniformLocation(prog, "uResolution") : -1);
        }
        glRes->ready = true;
    });

    surface->SetRenderCallback([glRes, state](const RenderSurfaceInfo& info) {
        if (!glRes->ready) return;
        int idx = state->currentIndex;
        if (idx < 0 || idx >= (int)glRes->programs.size() || glRes->programs[idx] == 0) {
            glClearColor(0.1f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            return;
        }
        state->clock += float(info.deltaTime) * state->speed;

        glDisable(GL_DEPTH_TEST);
        glUseProgram(glRes->programs[idx]);
        glUniform1f(glRes->timeLoc[idx], state->clock);
        glUniform2f(glRes->resLoc[idx], float(info.width), float(info.height));
        glBindVertexArray(glRes->vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glUseProgram(0);
    });

    surface->SetCleanupCallback([glRes]() {
        for (GLuint p : glRes->programs) if (p) glDeleteProgram(p);
        if (glRes->vao) glDeleteVertexArrays(1, &glRes->vao);
        *glRes = ShaderGLResources{};
    });

    root->AddChild(surface);

    // ---------------------------------------------------------- control panel
    auto panel = std::make_shared<UltraCanvasContainer>("ShaderControls", 690, 40, 296, 560);
    panel->SetBackgroundColor(Color(246, 246, 248, 255));
    panel->SetBorders(1.0f);
    panel->SetPadding(10.0f);

    auto pTitle = std::make_shared<UltraCanvasLabel>("ShaderCtrlTitle", 10, 8, 270, 22);
    pTitle->SetText("Effect");
    pTitle->SetFontWeight(FontWeight::Bold);
    pTitle->SetFontSize(13);
    panel->AddChild(pTitle);

    auto fxDrop = std::make_shared<UltraCanvasDropdown>("ShaderDropdown", 10, 36, 270, 28);
    for (const auto& fx : *effects) fxDrop->AddItem(fx.label);
    fxDrop->SetSelectedIndex(0, false);
    fxDrop->onSelectionChanged = [state, surface](int idx, const DropdownItem&) {
        state->currentIndex = idx;
        surface->RequestRender();
    };
    panel->AddChild(fxDrop);

    auto speedLabel = std::make_shared<UltraCanvasLabel>("ShaderSpeedLabel", 10, 78, 270, 18);
    speedLabel->SetText("Animation Speed");
    speedLabel->SetFontSize(11);
    panel->AddChild(speedLabel);

    auto speedSlider = std::make_shared<UltraCanvasSlider>("ShaderSpeedSlider", 10, 98, 270, 24);
    speedSlider->SetRange(0.0f, 3.0f);
    speedSlider->SetValue(1.0f);
    speedSlider->onValueChanged = [state](float v) { state->speed = v; };
    panel->AddChild(speedSlider);

    auto info = std::make_shared<UltraCanvasLabel>("ShaderInfo", 10, 140, 270, 400);
    info->SetText(
        "Each effect is a single fragment\n"
        "shader rendered over one full-screen\n"
        "triangle — no geometry, no textures.\n\n"
        "Included effects:\n"
        "• Plasma — classic additive sines\n"
        "• Raymarched Scene — SDF spheres\n"
        "  with soft shadows on a checker\n"
        "  floor and distance fog\n"
        "• Julia Fractal — animated escape-\n"
        "  time set with smooth colouring\n"
        "• Tunnel — polar checkerboard\n"
        "• Warp Starfield — layered parallax\n"
        "• Chaotic Lattice — a Twigl /\n"
        "  つぶやきGLSL geek-mode one-liner\n"
        "  raymarching a cosine cell field\n"
        "• Rössler Attractor — the real\n"
        "  ODE system (a=b=0.2, c=5.7)\n"
        "  Euler-integrated into a glowing\n"
        "  orbiting 3D trajectory\n"
        "• Ball Surface — port of a p5.js\n"
        "  sketch: four noise-warped rings\n"
        "  of F-weighted white splats\n\n"
        "uTime and uResolution are the only\n"
        "inputs; the speed slider scales time."
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
