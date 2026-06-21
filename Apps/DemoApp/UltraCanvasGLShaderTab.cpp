// Apps/DemoApp/UltraCanvasGLShaderTab.cpp
// "Shaders" tab of the OpenGL showcase: a full-screen quad driven entirely by
// animated fragment shaders. A dropdown switches between several procedural
// effects (plasma, raymarched scene, Julia fractal, tunnel,
// seven Twigl/つぶやきGLSL "geek-mode" snippets ("Borg Sphere" lattice,
// "Pulse" ray-fold, "Fragments" tube turbulence, "Mountains" fractal terrain,
// "Horizon" turbulent landscape, "Protostar2" glowing core and "Plasma Orb"
// warped field), p5.js "Ball Surface" and "Alien Caterpillar" ports, and an
// openFrameworks "Circles" node-link network — the "Circles" network and the
// "Alien Caterpillar" point cloud are drawn as native 2D GL geometry rather
// than fragment shaders). The generating source for each effect is shown
// in a GLSL-syntax-highlighted text area below.
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasGLDemoSupport.h"

#ifdef ULTRACANVAS_ENABLE_GL

#include "UltraCanvasGLSurface.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasButton.h"

#include <memory>
#include <vector>
#include <string>
#include <functional>

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
    int customKind = 0;   // 0 => fragment shader; 1 => Circles; 2 => Alien Caterpillar
};

// Flat-colour 2D program for the openFrameworks "Circles" effect (lines + disks
// in a world space that maps to clip via uScale; one solid colour per draw).
const char* kCircles2DVS = R"(#version 330 core
layout(location=0) in vec2 aPos;
uniform vec2 uScale;
void main(){ gl_Position = vec4(aPos*uScale, 0.0, 1.0); }
)";
const char* kCircles2DFS = R"(#version 330 core
out vec4 FragColor;
uniform vec4 uColor;
void main(){ FragColor = uColor; }
)";

std::vector<ShaderEffect> BuildEffects() {
    std::vector<ShaderEffect> fx;

    // ------------------------------------------------------------------------
    // "Horizon" — a Twigl / つぶやきGLSL "geek-mode" one-liner. A ray marches a
    // turbulent landscape under a glowing sky; each step adds warm glow weighted
    // by 1/f and the radial distance, an inner octave loop warps the sample, and
    // the step size f comes from a folded terrain height. The original packs the
    // height into one chained min/max assignment whose result depends on
    // left-to-right argument evaluation (unspecified in GLSL); it is expanded
    // here into sequential statements so the behaviour is deterministic.
    fx.push_back({"Horizon (Twigl)", R"(
void main(){
    vec2 r = uResolution;
    vec3 FC = vec3(vUV * r, 0.0);          // gl_FragCoord (z = 0)
    float t = uTime;
    vec4 o = vec4(0.0);
    vec3 c = vec3(0.0), p = vec3(0.0);
    float i = 0.0, z = 0.1, f = 0.0;
    for(; i++ < 1e2; o += vec4(9.0,4.0,2.0,0.0)/f/length(c.xy/z)){
        p = c = z*normalize(FC.rgb*2.0 - r.xyy);
        for(p.x *= f = 0.6; f++ < 9.0; p += sin(p.yzx*f + 0.5*z - t/4.0)/f);
        // z += f = .03+.1*max(f=6.-.2*z+min(f=(p+c).y,-f*.2),-f*.6);
        float A = (p + c).y;
        float B = min(A, -0.2*A);
        float C = 6.0 - 0.2*z + B;
        float D = max(C, -0.6*C);
        f = 0.03 + 0.1*D;
        z += f;
    }
    o = tanh(o*o/9e8);
    FragColor = vec4(o.rgb, 1.0);
}
)"});

    // ------------------------------------------------------------------------
    // "Protostar2" — a Twigl / つぶやきGLSL "geek-mode" one-liner. A ray marches a
    // glowing volumetric core: each sample point p is folded about a time- and
    // field-varying axis a (the a*dot - cross term is a Rodrigues-style twist),
    // an inner octave loop accretes cos turbulence into a, and the step size and
    // colour come from the resulting field length s; tanh tone-maps the glow.
    fx.push_back({"Protostar2 (Twigl)", R"(
void main(){
    vec2 r = uResolution;
    vec3 FC = vec3(vUV * r, 0.0);          // gl_FragCoord (z = 0)
    float t = uTime;
    vec4 o = vec4(0.0);
    float i = 0.0, z = 0.0, d = 0.0, s = 0.0;
    for(; i++ < 2e2; o += (cos(s/0.6 + vec4(0,1,2,0)) + 1.1)/d){
        vec3 p = z*normalize(FC.rgb*2.0 - r.xyy);
        vec3 a = normalize(cos(vec3(0,1,0) + t - 0.4*s));
        p.z += 9.0;
        a = a*dot(a, p) - cross(a, p);     // twist p about axis a
        for(d = 1.0; d++ < 6.0; )
            s = length(a += cos(a*d + t).yzx/d);
        z += d = 0.1*(abs(sin(s - t)) + abs(a.y)/6.0);
    }
    o = tanh(o*o/2e7);
    FragColor = vec4(o.rgb, 1.0);
}
)"});

    // ------------------------------------------------------------------------
    // "Plasma Orb" — a Twigl / つぶやきGLSL one-liner. A domain-warped field v is
    // iterated through eight cos octaves and accumulated into o; the result is
    // tinted by a vertical exp() gradient and a radial exp(-l) falloff, then
    // tanh tone-mapped — a glowing plasma sphere.
    fx.push_back({"Plasma Orb (Twigl)", R"(
void main(){
    vec2 r = uResolution;
    vec2 FC = vUV * r;                     // gl_FragCoord.xy
    float t = uTime;
    vec4 o = vec4(0.0);
    vec2 p = (FC.xy*2.0 - r)/r.y;
    vec2 l = vec2(0.0);
    vec2 v = p*(1.0 - (l += abs(0.7 - dot(p, p))))/0.2;
    for(float i = 0.0; i++ < 8.0; o += (sin(v.xyyx) + 1.0)*abs(v.x - v.y)*0.2)
        v += cos(v.yx*i + vec2(0.0, i) + t)/i + 0.7;
    o = tanh(exp(p.y*vec4(1.0,-1.0,-2.0,0.0)) * exp(-4.0*l.x) / o);
    FragColor = vec4(o.rgb, 1.0);
}
)"});

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

    // ------------------------------------------------------------------------
    // "Borg Sphere" — a Twigl / つぶやきGLSL "geek-mode" one-liner, ported to the
    // tab's uniforms. The original implicit variables map as: r->uResolution,
    // FC->gl_FragCoord (reconstructed as vUV*uResolution), t->uTime,
    // o->FragColor accumulator, rotate2D->rot. It raymarches a chaotic cosine
    // lattice of ceil()-quantised cells (the cubic, hull-like "Borg" structure)
    // and tone-maps the accumulated glow with tanh().
    fx.push_back({"Borg Sphere (Twigl)", R"(
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
    // "Ball Surface" — port of a p5.js generative sketch. The original draws
    // four overlaid rings (o = 0..3) of 2048 white circles each; every circle's
    // angle i sets a noise-warped radius F, and fill(W, 9*F) makes brightness
    // scale with F over a fading-trail background. p5 has no GLSL runtime here,
    // so each pixel re-accumulates the same per-angle splats: a GLSL value-noise
    // stands in for p5's noise(), brightness is weighted by F (with a constant
    // folding in the trail's steady-state amplification ~255/20), and angles
    // whose radicand is negative are skipped just as p5 skips a NaN circle.
    fx.push_back({"Ball Surface (p5.js port)", R"(
uniform float uBright;   // overall splat brightness (p5 fill alpha + trail gain)
uniform float uSplat;    // splat radius in sketch pixels (circle size)
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
            // fill(W, 9*F): brightness ~ F; uBright folds in the trail's steady
            // accumulation. uSplat is the splat radius (~1.7px ≈ 3px-dia circle).
            acc += uBright * F * smoothstep(uSplat, 0.0, length(P - c));
        }
    }
    vec3 col = vec3(clamp(acc, 0.0, 1.0));
    FragColor = vec4(col, 1.0);
}
)"});

    // ------------------------------------------------------------------------
    // "Pulse" — another Twigl / つぶやきGLSL "geek-mode" one-liner. Each pixel
    // walks a ray (z is the marched distance), repeatedly reflecting the sample
    // point about a time-varying axis v (the dot/cross term is a Rodrigues-style
    // fold) and accumulating tinted glow weighted by 1/d/z; tanh tone-maps the
    // result. Mapped onto the tab's uniforms: r->uResolution, FC->vUV*res,
    // t->uTime, o->FragColor accumulator.
    fx.push_back({"Pulse (Twigl)", R"(
uniform float uPulseStep;    // march step scale (was the literal 0.2)
uniform float uPulsePhase;   // phase added to the fold axis (was vec3(6,1,4))
void main(){
    vec2 r = uResolution;
    vec3 FC = vec3(vUV * r, 0.0);          // gl_FragCoord (z = 0)
    float t = uTime;
    vec4 o = vec4(0.0);
    vec3 p, v;
    for(float i=0.0, z=0.0, d=0.0; i++ < 5e1; o += vec4(3.0, z, 6.0, 1.0)/d/z){
        p = z * normalize(2.0*FC.rgb - r.xyy);
        p.z += 9.0;
        v = normalize(cos((t + i)/2.0 + vec3(6.0, 1.0, 4.0) + uPulsePhase));
        p = dot(v, p)*v + cross(v, p);     // fold the sample about axis v
        z += d = uPulseStep*length(p.xy/vec2(1.0, 9.0));
    }
    o = tanh(o/2e2);
    FragColor = vec4(o.rgb, 1.0);
}
)"});

    // ------------------------------------------------------------------------
    // "Fragments" — another Twigl / つぶやきGLSL "geek-mode" one-liner. A ray is
    // marched (z = distance); at each step the sample point p is folded a few
    // times by a quantised sine turbulence (the inner loop), then density f is
    // taken from a wobbling tube SDF and tinted glow is accumulated; tanh tone-
    // maps the result. PI/PI2 (Twigl globals) are supplied locally here.
    fx.push_back({"Fragments (Twigl)", R"(
uniform float uFragFold;     // inner turbulence fold count (was the literal 6.)
uniform float uFragRadius;   // tube radius (was the literal 5.)
void main(){
    vec2 r = uResolution;
    vec3 FC = vec3(vUV * r, 0.0);          // gl_FragCoord (z = 0)
    float t = uTime;
    vec4 o = vec4(0.0);
    const float PI = 3.14159265, PI2 = 6.28318530;
    vec3 p;
    for(float i=0.0, z=0.0, f=0.0; i++ < 3e1;
        z += f = 0.003 + abs(length(p.xy) - uFragRadius + dot(cos(p), sin(p).yzx))/8.0,
        o += (1.0 + sin(i*0.3 + z + t + vec4(6.0, 1.0, 2.0, 0.0)))/f){
        for(p = z*normalize(FC.rgb*2.0 - r.xyy), p.z -= t, f = 1.0;
            f++ < uFragFold && f < 16.0;
            p += sin(round(p.yxz*PI2)/PI*f)/f);
    }
    o = tanh(o/1e3);
    FragColor = vec4(o.rgb, 1.0);
}
)"});

    // ------------------------------------------------------------------------
    // "Mountains" — a Twigl / つぶやきGLSL "geek-mode" one-liner. A ray marches a
    // log-polar fractal terrain: each step accumulates HSV-tinted glow, then an
    // inner octave loop (s doubling) sums a sin/cos turbulence used to advance.
    // Twigl's built-in hsv() is supplied locally; the implicit FC/r/t/o map onto
    // the tab's vUV*res / uResolution / uTime / FragColor. (No final tone-map in
    // the original, so the accumulated colour is written straight out.)
    fx.push_back({"Mountains (Twigl)", R"(
vec3 hsv(float h, float s, float v){
    vec3 rgb = clamp(abs(mod(h*6.0 + vec3(0.0,4.0,2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
    return v * mix(vec3(1.0), rgb, s);
}
void main(){
    vec2 r = uResolution;
    vec2 FC = vUV * r;                      // gl_FragCoord.xy
    float t = uTime;
    vec4 o = vec4(0.0);
    float i=0.0, e=0.0, g=0.0, R=0.0, s=0.0;
    vec3 q=vec3(0.0), p=vec3(0.0), d=vec3((FC.xy - 0.5*r)/r, 0.8);
    for(q.yz -= 1.0; i++ < 99.0; ){
        e += i/2e4;
        o.rgb += hsv(p.y, 0.5, dot(e, i*0.015));
        s = 4.0;
        p = q += d*e*R*0.3;
        g += p.z;
        p = vec3(log2(R = length(p)) - t*0.2, exp2(mod(-p.z, s)/R) - 0.2, p);
        for(e = --p.y; s < 1e4; s += s)
            e += -abs(dot(sin(p.yzx*s), cos(p.xzy*s))/s*0.5);
    }
    FragColor = vec4(o.rgb, 1.0);
}
)"});

    // ------------------------------------------------------------------------
    // "Circles" — an openFrameworks (C++) generative sketch, not a fragment
    // shader. customKind 1 => the surface draws it with native 2D GL geometry
    // (see RenderCirclesNetwork). The body is empty (no program).
    fx.push_back({"Circles (openFrameworks)", "", 1});

    // "Alien Caterpillar" — a p5.js point-cloud sketch (10000 parametric points).
    // customKind 2 => drawn as GL_POINTS by RenderCaterpillar.
    fx.push_back({"Alien Caterpillar (p5.js port)", "", 2});

    return fx;
}

// For effects derived from an iconic original (a Twigl one-liner, a p5.js
// sketch, a closed-form intensity, an ODE system) this returns that original as
// a comment header, shown in the source viewer above the running GLSL so a
// reader sees the inspiration alongside the implementation. Empty for the
// stock effects, whose GLSL body is itself the "formula".
std::string EffectFormula(const std::string& label) {
    if (label == "Borg Sphere (Twigl)")
        return R"(// Source — Twigl / つぶやきGLSL "geek-mode" one-liner:
// for(float i,d,s;++i<1e2;){
//   vec3 p=vec3((FC.xy*2.-r.xy)/r.y*d*rotate2D(t/2.),d-8.);
//   p.yz*=rotate2D(t/2.);
//   d+=s=.01+.1*abs(max(cos(dot(sin(ceil(p/.3)),cos(ceil(p/.6)).yzx)),
//                       length(ceil(p))-4.)-i/1e2);
//   o.rgb+=max(1.3*sin(p*vec3(1,2,3)+i*.8)/s,-length(p*p));
// } o=tanh(o*o/1e6);)";
    if (label == "Pulse (Twigl)")
        return R"(// Source — Twigl / つぶやきGLSL one-liner:
// vec3 p,v;for(float i,z,d;i++<5e1;o+=vec4(3,z,6,1)/d/z)
//   p=z*normalize(2.*FC.rgb-r.xyy),p.z+=9.,
//   p=dot(v=normalize(cos((t+i)/2.+vec3(6,1,4))),p)*v+cross(v,p),
//   z+=d=.2*length(p.xy/vec2(1,9));
// o=tanh(o/2e2);)";
    if (label == "Ball Surface (p5.js port)")
        return R"(// Source — p5.js generative sketch "Ball Surface":
// f=0,d=200,draw=o=>{for(f||createCanvas(W=400,W),
//   noStroke(background(0,20)),
//   q=o=>{for(i=0;i<TAU;i+=PI/1024)r=120*cos(i+f/2),
//     F=sqrt(noise(2*sin(i+f+o),7*cos(i+f+o))+2*sin(o+i)),
//     fill(W,9*F),circle(sin(i+F)*r+d,120*cos(i+F)+d,3)},
//   k=4;k--;)q(k);f-=.01};)";
    if (label == "Protostar2 (Twigl)")
        return R"(// Source — Twigl / つぶやきGLSL one-liner:
// for(float i,z,d,s;i++<2e2;o+=(cos(s/.6+vec4(0,1,2,0))+1.1)/d){
//   vec3 p=z*normalize(FC.rgb*2.-r.xyy),
//        a=normalize(cos(vec3(0,1,0)+t-.4*s));
//   p.z+=9.,a=a*dot(a,p)-cross(a,p);
//   for(d=1.;d++<6.;)s=length(a+=cos(a*d+t).yzx/d);
//   z+=d=.1*(abs(sin(s-t))+abs(a.y)/6.);
// }
// o=tanh(o*o/2e7);)";
    if (label == "Horizon (Twigl)")
        return R"(// Source — Twigl / つぶやきGLSL one-liner:
// vec3 c,p;
// for(float i,z=.1,f;i++<1e2;o+=vec4(9,4,2,0)/f/length(c.xy/z)){
//   p=c=z*normalize(FC.rgb*2.-r.xyy);
//   for(p.x*=f=.6;f++<9.;p+=sin(p.yzx*f+.5*z-t/4.)/f);
//   z+=f=.03+.1*max(f=6.-.2*z+min(f=(p+c).y,-f*.2),-f*.6);
// }
// o=tanh(o*o/9e8);)";
    if (label == "Mountains (Twigl)")
        return R"(// Source — Twigl / つぶやきGLSL one-liner:
// float i,e,g,R,s;vec3 q,p,d=vec3((FC.xy-.5*r)/r,.8);
// for(q.yz--;i++<99.;){
//   e+=i/2e4;
//   o.rgb+=hsv(p.y,.5,dot(e,i*.015));
//   s=4.;p=q+=d*e*R*.3;g+=p.z;
//   p=vec3(log2(R=length(p))-t*.2,exp2(mod(-p.z,s)/R)-.2,p);
//   for(e=--p.y;s<1e4;s+=s)
//     e+=-abs(dot(sin(p.yzx*s),cos(p.xzy*s))/s*.5);
// })";
    if (label == "Fragments (Twigl)")
        return R"(// Source — Twigl / つぶやきGLSL one-liner:
// vec3 p;
// for(float i,z,f;i++<3e1;
//     z+=f=.003+abs(length(p.xy)-5.+dot(cos(p),sin(p).yzx))/8.,
//     o+=(1.+sin(i*.3+z+t+vec4(6,1,2,0)))/f)
//   for(p=z*normalize(FC.rgb*2.-r.xyy),p.z-=t,f=1.;f++<6.;
//       p+=sin(round(p.yxz*PI2)/PI*f)/f);
// o=tanh(o/1e3);)";
    if (label == "Plasma Orb (Twigl)")
        return R"(// Source — Twigl / つぶやきGLSL one-liner:
// vec2 p=(FC.xy*2.-r)/r.y,l,v=p*(1.-(l+=abs(.7-dot(p,p))))/.2;
// for(float i;i++<8.;o+=(sin(v.xyyx)+1.)*abs(v.x-v.y)*.2)
//   v+=cos(v.yx*i+vec2(0,i)+t)/i+.7;
// o=tanh(exp(p.y*vec4(1,-1,-2,0))*exp(-4.*l.x)/o);)";
    if (label == "Alien Caterpillar (p5.js port)")
        return R"(// Source — p5.js generative sketch (drawn here as GL_POINTS):
// a=(x,y,d=mag(k=(4+sin(y*2-t)*3)*cos(x/29),e=y/8-13))=>
//   point((q=3*sin(k*2)+.3/k+sin(y/25)*k*(9+4*sin(e*9-d*3+t*2)))
//          +30*cos(c=d-t)+200,
//         q*sin(c)+d*39-220)
// t=0,draw=$=>{t||createCanvas(w=400,w);background(9).stroke(w,96);
//   for(t+=PI/240,i=1e4;i--;)a(i,i/235)})";
    if (label == "Circles (openFrameworks)")
        return R"(// Source — openFrameworks (C++); drawn as native 2D geometry, not a shader:
// ofSeedRandom(39);                          // deterministic each frame
// for (int i = 0; i < 180; i++) {            // 180 Perlin-noise-placed nodes
//   auto loc = glm::vec2(
//     ofMap(ofNoise(ofRandom(1000), ofGetFrameNum()*0.0035), 0,1, -300,300),
//     ofMap(ofNoise(ofRandom(1000), ofGetFrameNum()*0.0035), 0,1, -300,300));
//   location_list.push_back(loc);
// }
// for (i) for (k != i)                       // link nodes closer than 50px
//   if (glm::distance(loc[i], loc[k]) < 50) {
//     mesh.addVertex(loc[i]); mesh.addVertex(loc[k]); near_count++;
//   }
// circle.z = pow(1.4, near_count);           // ring radius grows with degree
// // draw: wireframe links, ring outlines, white interiors (r-1), dark dots (r=2))";
    return "";
}

// A GL surface that can take keyboard focus, so it receives the Escape key
// (delivered by the app to the focused element) used to leave the maximized
// "zoom" view. Behaviour is otherwise identical to UltraCanvasGLSurface.
class ZoomableGLSurface : public UltraCanvasGLSurface {
public:
    ZoomableGLSurface(const GLSurfaceConfig& cfg, const std::string& id,
                      float x, float y, float w, float h)
        : UltraCanvasGLSurface(cfg, id, x, y, w, h) {}
    bool AcceptsFocus() const override { return true; }
};

struct ShaderState {
    int currentIndex = 0;
    int activeProgram = -1;     // program currently bound's matching index
    float speed = 1.0f;
    float clock = 0.0f;
    // Live, slider-driven effect parameters (ignored by effects that lack them).
    float ballBright = 0.4f;    // Ball Surface splat brightness
    float ballSplat = 1.7f;     // Ball Surface splat radius (px)
    float pulseStep = 0.2f;     // Pulse march-step scale
    float pulsePhase = 0.0f;    // Pulse fold-axis phase
    float fragFold = 6.0f;      // Fragments inner fold count
    float fragRadius = 5.0f;    // Fragments tube radius
    float circlesFrame = 0.0f;  // Circles network animation frame counter
    float circlesNodes = 180.0f;// Circles node count
    float circlesLink = 50.0f;  // Circles link distance threshold
    float circlesGrowth = 1.4f; // Circles ring growth base (pow(base, degree))
    float caterT = 0.0f;        // Alien Caterpillar animation time
};

struct ShaderGLResources {
    GLuint vao = 0;                 // empty VAO required by core profile
    std::vector<GLuint> programs;
    std::vector<GLint> timeLoc;
    std::vector<GLint> resLoc;
    // Optional per-effect uniforms; -1 where an effect doesn't declare them.
    // glUniform1f(-1, ...) is a silent no-op, so these can be set every frame.
    std::vector<GLint> brightLoc, splatLoc;     // Ball Surface uBright/uSplat
    std::vector<GLint> pStepLoc, pPhaseLoc;     // Pulse uPulseStep/uPulsePhase
    std::vector<GLint> fFoldLoc, fRadiusLoc;    // Fragments uniforms
    // "Circles" effect: a flat-colour 2D program with one dynamic vertex buffer.
    GLuint cProg = 0, cVao = 0, cVbo = 0;
    GLint cScaleLoc = -1, cColorLoc = -1;
    bool ready = false;
};

// ---------------------------------------------------- "Circles" 2D network ---
// CPU value noise (an ofNoise stand-in) and a triangle-fan disk builder, used to
// reproduce the openFrameworks node-link sketch as native 2D GL geometry.
inline float cFract(float x){ return x - std::floor(x); }
inline float cHash2(float x, float y){
    return cFract(std::sin(x*127.1f + y*311.7f) * 43758.5453f);
}
inline float cVNoise(float x, float y){
    float xi = std::floor(x), yi = std::floor(y), xf = x - xi, yf = y - yi;
    float u = xf*xf*(3.0f - 2.0f*xf), v = yf*yf*(3.0f - 2.0f*yf);
    float a = cHash2(xi, yi),       b = cHash2(xi+1.0f, yi);
    float c = cHash2(xi, yi+1.0f),  d = cHash2(xi+1.0f, yi+1.0f);
    return a + (b-a)*u + (c-a)*v + (a-b-c+d)*u*v;     // ~[0,1]
}
inline void AppendDisk(std::vector<float>& vtx, float cx, float cy, float r){
    if (r <= 0.0f) return;
    const int S = 28;
    const float TAU = 6.2831853f;
    for (int s = 0; s < S; ++s){
        float a0 = TAU*float(s)/float(S), a1 = TAU*float(s+1)/float(S);
        vtx.push_back(cx);                 vtx.push_back(cy);
        vtx.push_back(cx + r*std::cos(a0)); vtx.push_back(cy + r*std::sin(a0));
        vtx.push_back(cx + r*std::cos(a1)); vtx.push_back(cy + r*std::sin(a1));
    }
}

// Draws the "Circles" sketch: 180 noise-placed nodes (drifting with `frame`),
// links between nodes closer than 50 units, and rings whose radius grows as
// pow(1.4, neighbour count) — links, dark ring bases, bg interiors, dark dots.
inline void RenderCirclesNetwork(ShaderGLResources& g, int W, int H, float frame,
                                 int nodes, float linkDist, float growth){
    glDisable(GL_DEPTH_TEST);
    glClearColor(239.0f/255.0f, 239.0f/255.0f, 239.0f/255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!g.cProg) return;

    const int N = std::max(2, std::min(nodes, 600));   // bound the O(N^2) pass
    std::vector<float> px(N), py(N);
    std::vector<int> cnt(N, 0);
    uint32_t rng = 39u;                          // ofSeedRandom(39), reset per frame
    auto rnd = [&]() -> float {                  // ~ ofRandom(0,1)
        rng = rng*1664525u + 1013904223u;
        return float(rng >> 8) * (1.0f/16777216.0f);
    };
    float t2 = frame * 0.0035f;                  // ofGetFrameNum()*0.0035 drift
    for (int i = 0; i < N; ++i){
        float sx = rnd()*1000.0f, sy = rnd()*1000.0f;   // ofRandom(1000) seeds
        px[i] = cVNoise(sx, t2)*600.0f - 300.0f;        // ofMap(noise,0,1,-300,300)
        py[i] = cVNoise(sy, t2)*600.0f - 300.0f;
    }
    std::vector<float> lines;
    for (int i = 0; i < N; ++i)
        for (int k = 0; k < N; ++k){
            if (i == k) continue;
            float dx = px[i]-px[k], dy = py[i]-py[k];
            if (dx*dx + dy*dy < linkDist*linkDist){
                lines.push_back(px[i]); lines.push_back(py[i]);
                lines.push_back(px[k]); lines.push_back(py[k]);
                cnt[i]++;
            }
        }
    std::vector<float> ringDisks, bgDisks, dots;
    for (int i = 0; i < N; ++i){
        float r = std::pow(growth, float(cnt[i]));
        AppendDisk(ringDisks, px[i], py[i], r);
        AppendDisk(bgDisks,   px[i], py[i], r - 1.5f);
        AppendDisk(dots,      px[i], py[i], 2.0f);
    }

    // World [-360,360] -> clip; uniform scale keeps circles round and fits the view.
    float view = 360.0f, sx2, sy2;
    if (W >= H){ sy2 = 1.0f/view; sx2 = sy2 * float(H)/float(W); }
    else       { sx2 = 1.0f/view; sy2 = sx2 * float(W)/float(H); }

    glUseProgram(g.cProg);
    glUniform2f(g.cScaleLoc, sx2, -sy2);
    glBindVertexArray(g.cVao);
    glBindBuffer(GL_ARRAY_BUFFER, g.cVbo);

    const float dark[4] = {39.0f/255.0f, 39.0f/255.0f, 39.0f/255.0f, 1.0f};
    const float bg[4]   = {239.0f/255.0f, 239.0f/255.0f, 239.0f/255.0f, 1.0f};
    auto drawBatch = [&](const std::vector<float>& v, GLenum mode, const float* col){
        if (v.empty()) return;
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size()*sizeof(float)),
                     v.data(), GL_DYNAMIC_DRAW);
        glUniform4fv(g.cColorLoc, 1, col);
        glDrawArrays(mode, 0, (GLsizei)(v.size()/2));
    };

    glLineWidth(1.0f);
    drawBatch(lines,     GL_LINES,     dark);   // links
    drawBatch(ringDisks, GL_TRIANGLES, dark);   // ring bases
    drawBatch(bgDisks,   GL_TRIANGLES, bg);     // carve interiors -> leaves ring
    drawBatch(dots,      GL_TRIANGLES, dark);   // centre dots

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glUseProgram(0);
}

// Draws the p5.js "Alien Caterpillar" sketch: 10000 points placed by a
// parametric formula over i (the loop index) and y=i/235, animated by t, drawn
// white with low alpha over a dark background — a fresh point cloud each frame.
inline void RenderCaterpillar(ShaderGLResources& g, int W, int H, float t){
    glDisable(GL_DEPTH_TEST);
    glClearColor(9.0f/255.0f, 9.0f/255.0f, 9.0f/255.0f, 1.0f);   // background(9)
    glClear(GL_COLOR_BUFFER_BIT);
    if (!g.cProg) return;

    const int N = 10000;
    std::vector<float> pts;
    pts.reserve(N*2);
    for (int i = N; i > 0; --i){
        float x = float(i), y = float(i)/235.0f;
        float k = (4.0f + std::sin(y*2.0f - t)*3.0f) * std::cos(x/29.0f);
        float e = y/8.0f - 13.0f;
        float d = std::sqrt(k*k + e*e);                              // mag(k,e)
        float q = 3.0f*std::sin(k*2.0f) + 0.3f/k
                + std::sin(y/25.0f)*k*(9.0f + 4.0f*std::sin(e*9.0f - d*3.0f + t*2.0f));
        float c = d - t;
        float xp = q + 30.0f*std::cos(c) + 200.0f;                  // p5 point x
        float yp = q*std::sin(c) + d*39.0f - 220.0f;                // p5 point y
        pts.push_back(xp - 200.0f);     // centre on the 400x400 canvas
        pts.push_back(yp - 200.0f);
    }

    // p5 canvas half-extent ~200; uScale.y negative keeps p5's y-down orientation.
    float view = 200.0f, sx2, sy2;
    if (W >= H){ sy2 = 1.0f/view; sx2 = sy2 * float(H)/float(W); }
    else       { sx2 = 1.0f/view; sy2 = sx2 * float(W)/float(H); }

    glUseProgram(g.cProg);
    glUniform2f(g.cScaleLoc, sx2, -sy2);
    glUniform4f(g.cColorLoc, 1.0f, 1.0f, 1.0f, 96.0f/255.0f);       // stroke(w,96)
    glBindVertexArray(g.cVao);
    glBindBuffer(GL_ARRAY_BUFFER, g.cVbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(pts.size()*sizeof(float)),
                 pts.data(), GL_DYNAMIC_DRAW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glPointSize(1.0f);
    glDrawArrays(GL_POINTS, 0, (GLsizei)(pts.size()/2));
    glDisable(GL_BLEND);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glUseProgram(0);
}

} // namespace

std::shared_ptr<UltraCanvasUIElement> CreateGLShaderTab() {
    auto root = std::make_shared<UltraCanvasContainer>("GLShaderTab", 0, 0, 1004, 692);

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

    auto surface = std::make_shared<ZoomableGLSurface>(cfg, "ShaderSurface", 16, 40, 660, 360);
    surface->SetRenderMode(RenderMode::Continuous);

    surface->SetInitCallback([glRes, effects]() {
        glGenVertexArrays(1, &glRes->vao);   // bound but empty; vertices from gl_VertexID
        for (const auto& fx : *effects) {
            GLuint prog = 0;                 // custom (non-shader) effects have no program
            if (fx.customKind == 0) {
                std::string fs = std::string(kFragHeader) + fx.body;
                prog = LinkProgram(kFullscreenVS, fs.c_str());
            }
            glRes->programs.push_back(prog);
            glRes->timeLoc.push_back(prog ? glGetUniformLocation(prog, "uTime") : -1);
            glRes->resLoc.push_back(prog ? glGetUniformLocation(prog, "uResolution") : -1);
            glRes->brightLoc.push_back(prog ? glGetUniformLocation(prog, "uBright") : -1);
            glRes->splatLoc.push_back(prog ? glGetUniformLocation(prog, "uSplat") : -1);
            glRes->pStepLoc.push_back(prog ? glGetUniformLocation(prog, "uPulseStep") : -1);
            glRes->pPhaseLoc.push_back(prog ? glGetUniformLocation(prog, "uPulsePhase") : -1);
            glRes->fFoldLoc.push_back(prog ? glGetUniformLocation(prog, "uFragFold") : -1);
            glRes->fRadiusLoc.push_back(prog ? glGetUniformLocation(prog, "uFragRadius") : -1);
        }

        // Set up the flat-colour 2D program + dynamic buffer for "Circles".
        glRes->cProg = LinkProgram(kCircles2DVS, kCircles2DFS);
        if (glRes->cProg) {
            glRes->cScaleLoc = glGetUniformLocation(glRes->cProg, "uScale");
            glRes->cColorLoc = glGetUniformLocation(glRes->cProg, "uColor");
        }
        glGenVertexArrays(1, &glRes->cVao);
        glGenBuffers(1, &glRes->cVbo);
        glBindVertexArray(glRes->cVao);
        glBindBuffer(GL_ARRAY_BUFFER, glRes->cVbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        glRes->ready = true;
    });

    surface->SetRenderCallback([glRes, state, effects](const RenderSurfaceInfo& info) {
        if (!glRes->ready) return;
        int idx = state->currentIndex;
        // Custom (non-shader) effects render their own GL geometry.
        if (idx >= 0 && idx < (int)effects->size() && (*effects)[idx].customKind != 0) {
            if ((*effects)[idx].customKind == 1) {
                state->circlesFrame += float(info.deltaTime) * 25.0f * state->speed;
                RenderCirclesNetwork(*glRes, info.width, info.height, state->circlesFrame,
                                     (int)(state->circlesNodes + 0.5f), state->circlesLink,
                                     state->circlesGrowth);
            } else {  // customKind 2 => Alien Caterpillar
                state->caterT += float(info.deltaTime) * 0.8f * state->speed;
                RenderCaterpillar(*glRes, info.width, info.height, state->caterT);
            }
            return;
        }
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
        // Optional per-effect uniforms (no-ops where the location is -1).
        glUniform1f(glRes->brightLoc[idx], state->ballBright);
        glUniform1f(glRes->splatLoc[idx], state->ballSplat);
        glUniform1f(glRes->pStepLoc[idx], state->pulseStep);
        glUniform1f(glRes->pPhaseLoc[idx], state->pulsePhase);
        glUniform1f(glRes->fFoldLoc[idx], state->fragFold);
        glUniform1f(glRes->fRadiusLoc[idx], state->fragRadius);
        glBindVertexArray(glRes->vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glUseProgram(0);
    });

    surface->SetCleanupCallback([glRes]() {
        for (GLuint p : glRes->programs) if (p) glDeleteProgram(p);
        if (glRes->vao) glDeleteVertexArrays(1, &glRes->vao);
        if (glRes->cProg) glDeleteProgram(glRes->cProg);
        if (glRes->cVao) glDeleteVertexArrays(1, &glRes->cVao);
        if (glRes->cVbo) glDeleteBuffers(1, &glRes->cVbo);
        *glRes = ShaderGLResources{};
    });

    root->AddChild(surface);

    // ------------------------------------------------ generating-source viewer
    // Builds the read-only text shown under the surface: the original formula /
    // snippet (if any) as a comment header, followed by the live GLSL body.
    auto sourceFor = [effects](int i) -> std::string {
        if (i < 0 || i >= (int)effects->size()) return "";
        const ShaderEffect& fx = (*effects)[i];
        std::string head = EffectFormula(fx.label);
        return head.empty() ? fx.body : head + "\n" + fx.body;
    };

    auto codeTitle = std::make_shared<UltraCanvasLabel>("ShaderSrcTitle", 16, 406, 660, 18);
    codeTitle->SetText("Generating source (read-only) — original formula + live GLSL");
    codeTitle->SetFontSize(12);
    codeTitle->SetFontWeight(FontWeight::Bold);
    codeTitle->SetTextColor(Color(40, 40, 120, 255));
    root->AddChild(codeTitle);

    auto codeArea = std::make_shared<UltraCanvasTextArea>("ShaderSource", 16, 428, 660, 252);
    codeArea->SetHighlightSyntax(true);
    codeArea->SetProgrammingLanguage("GLSL");    // dedicated GLSL grammar
    codeArea->ApplyDarkTheme();
    codeArea->SetReadOnly(true);
    codeArea->SetShowLineNumbers(true);
    codeArea->SetWordWrap(false);
    codeArea->SetFontSize(12.0f);
    codeArea->SetText(sourceFor(0));
    root->AddChild(codeArea);

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
    panel->AddChild(fxDrop);   // onSelectionChanged is wired up once the slider
                               // groups below exist (it toggles their visibility).

    auto speedLabel = std::make_shared<UltraCanvasLabel>("ShaderSpeedLabel", 10, 78, 270, 18);
    speedLabel->SetText("Animation Speed");
    speedLabel->SetFontSize(11);
    panel->AddChild(speedLabel);

    auto speedSlider = std::make_shared<UltraCanvasSlider>("ShaderSpeedSlider", 10, 98, 270, 24);
    speedSlider->SetRange(0.0f, 3.0f);
    speedSlider->SetValue(1.0f);
    speedSlider->onValueChanged = [state](float v) { state->speed = v; };
    panel->AddChild(speedSlider);

    // ----------------------------------------- per-effect parameter sliders
    // Locate the effects that carry live uniforms so the matching slider group
    // can be revealed only when that effect is selected.
    int ballIdx = -1, pulseIdx = -1, fragIdx = -1, circlesIdx = -1;
    for (int i = 0; i < (int)effects->size(); ++i) {
        if ((*effects)[i].label == "Ball Surface (p5.js port)")  ballIdx = i;
        if ((*effects)[i].label == "Pulse (Twigl)")              pulseIdx = i;
        if ((*effects)[i].label == "Fragments (Twigl)")          fragIdx = i;
        if ((*effects)[i].label == "Circles (openFrameworks)")   circlesIdx = i;
    }

    auto addSlider = [](std::shared_ptr<UltraCanvasContainer>& group, const char* id,
                        int y, float lo, float hi, float val,
                        std::function<void(float)> cb,
                        float step = 0.0f, const char* fmt = "%.2f") {
        auto s = std::make_shared<UltraCanvasSlider>(id, 0, y, 270, 22);
        s->SetRange(lo, hi);
        if (step > 0.0f) s->SetStep(step);
        s->SetValue(val);
        s->SetValueDisplay(SliderValueDisplay::Number);
        s->SetValueFormat(fmt);
        s->onValueChanged = std::move(cb);
        group->AddChild(s);
    };
    auto addCaption = [](std::shared_ptr<UltraCanvasContainer>& group, const char* id,
                         const char* text, int y, bool bold = false) {
        auto l = std::make_shared<UltraCanvasLabel>(id, 0, y, 270, 16);
        l->SetText(text);
        l->SetFontSize(bold ? 12 : 11);
        if (bold) l->SetFontWeight(FontWeight::Bold);
        group->AddChild(l);
    };

    // Ball Surface — brightness and splat radius shape the dotted rings.
    auto ballGroup = std::make_shared<UltraCanvasContainer>("BallParams", 10, 128, 276, 140);
    ballGroup->SetBackgroundColor(Color(246, 246, 248, 255));
    addCaption(ballGroup, "BS_title", "Ball Surface controls", 0, true);
    addCaption(ballGroup, "BS_bL", "Brightness", 26);
    addSlider(ballGroup, "BS_b", 44, 0.05f, 1.5f, 0.4f, [state](float v){ state->ballBright = v; });
    addCaption(ballGroup, "BS_sL", "Splat radius (px)", 74);
    addSlider(ballGroup, "BS_s", 92, 0.5f, 5.0f, 1.7f, [state](float v){ state->ballSplat = v; });
    ballGroup->SetVisible(false);
    panel->AddChild(ballGroup);

    // Pulse — step scale (march density) and fold-axis phase shape the motion.
    auto pulseGroup = std::make_shared<UltraCanvasContainer>("PulseParams", 10, 128, 276, 140);
    pulseGroup->SetBackgroundColor(Color(246, 246, 248, 255));
    addCaption(pulseGroup, "PU_title", "Pulse controls", 0, true);
    addCaption(pulseGroup, "PU_stepL", "Step scale", 26);
    addSlider(pulseGroup, "PU_step", 44, 0.05f, 0.6f, 0.2f, [state](float v){ state->pulseStep = v; });
    addCaption(pulseGroup, "PU_phL", "Fold phase", 74);
    addSlider(pulseGroup, "PU_ph", 92, 0.0f, 6.2832f, 0.0f, [state](float v){ state->pulsePhase = v; });
    pulseGroup->SetVisible(false);
    panel->AddChild(pulseGroup);

    // Fragments — inner fold count and tube radius shape the crystalline burst.
    auto fragGroup = std::make_shared<UltraCanvasContainer>("FragmentsParams", 10, 128, 276, 140);
    fragGroup->SetBackgroundColor(Color(246, 246, 248, 255));
    addCaption(fragGroup, "FR_title", "Fragments controls", 0, true);
    addCaption(fragGroup, "FR_foldL", "Fold count (turbulence)", 26);
    addSlider(fragGroup, "FR_fold", 44, 2.0f, 10.0f, 6.0f,
              [state](float v){ state->fragFold = v; }, 1.0f, "%.0f");
    addCaption(fragGroup, "FR_radL", "Tube radius (core size)", 74);
    addSlider(fragGroup, "FR_rad", 92, 1.0f, 12.0f, 5.0f, [state](float v){ state->fragRadius = v; });
    fragGroup->SetVisible(false);
    panel->AddChild(fragGroup);

    // Circles — node count, link distance and ring growth shape the network.
    auto circlesGroup = std::make_shared<UltraCanvasContainer>("CirclesParams", 10, 128, 276, 180);
    circlesGroup->SetBackgroundColor(Color(246, 246, 248, 255));
    addCaption(circlesGroup, "CI_title", "Circles controls", 0, true);
    addCaption(circlesGroup, "CI_nL", "Node count", 26);
    addSlider(circlesGroup, "CI_n", 44, 30.0f, 400.0f, 180.0f,
              [state](float v){ state->circlesNodes = v; }, 1.0f, "%.0f");
    addCaption(circlesGroup, "CI_dL", "Link distance", 74);
    addSlider(circlesGroup, "CI_d", 92, 20.0f, 100.0f, 50.0f,
              [state](float v){ state->circlesLink = v; }, 0.0f, "%.0f");
    addCaption(circlesGroup, "CI_gL", "Ring growth (pow base)", 122);
    addSlider(circlesGroup, "CI_g", 140, 1.1f, 1.6f, 1.4f,
              [state](float v){ state->circlesGrowth = v; });
    circlesGroup->SetVisible(false);
    panel->AddChild(circlesGroup);

    // Now that the groups exist, wire dropdown selection to reveal the right one.
    fxDrop->onSelectionChanged =
        [state, surface, codeArea, sourceFor, ballGroup, pulseGroup,
         fragGroup, circlesGroup, ballIdx, pulseIdx,
         fragIdx, circlesIdx](int idx, const DropdownItem&) {
            state->currentIndex = idx;
            codeArea->SetText(sourceFor(idx));
            ballGroup->SetVisible(idx == ballIdx);
            pulseGroup->SetVisible(idx == pulseIdx);
            fragGroup->SetVisible(idx == fragIdx);
            circlesGroup->SetVisible(idx == circlesIdx);
            surface->RequestRender();
        };

    auto info = std::make_shared<UltraCanvasLabel>("ShaderInfo", 10, 318, 270, 232);
    info->SetText(
        "Most effects are a single fragment\n"
        "shader over one full-screen triangle\n"
        "(Circles is 2D geometry). The syntax-\n"
        "highlighted source viewer below the\n"
        "canvas shows the original formula and\n"
        "the live source for the chosen effect.\n\n"
        "Effects: Horizon (Twigl turbulent\n"
        "landscape), Protostar2 (Twigl glowing\n"
        "core), Plasma Orb (Twigl warped field),\n"
        "Plasma, Raymarched Scene, Julia\n"
        "Fractal, Tunnel,\n"
        "Borg Sphere (Twigl one-liner),\n"
        "Ball Surface (p5.js port),\n"
        "Pulse (Twigl ray-fold),\n"
        "Fragments (Twigl tube turbulence),\n"
        "Mountains (Twigl fractal terrain),\n"
        "Circles (openFrameworks network),\n"
        "Alien Caterpillar (p5.js points).\n\n"
        "The speed slider scales time. Select\n"
        "Ball Surface, Pulse, Fragments or\n"
        "Circles to reveal live parameter\n"
        "sliders above — e.g. change the\n"
        "Circles node count and link range.\n\n"
        "Click the ⛶ icon or double-click the\n"
        "canvas to maximize it; press Esc or\n"
        "click once to restore."
    );
    info->SetFontSize(11);
    info->SetAlignment(TextAlignment::Left);
    info->SetTextColor(Color(60, 60, 60, 255));
    panel->AddChild(info);

    root->AddChild(panel);

    // ---------------------------------------------------- maximize / zoom view
    // A "⛶" icon button overlays the top-right of the canvas (the GL surface
    // composites into the 2D layer, so a later sibling draws on top of it).
    // It maximizes the canvas over the whole tab, hiding the controls and
    // source viewer; double-clicking the canvas does the same, and Esc or a
    // single click restores. The button stays visible so it can toggle back.
    const Point2Df btnHome{644.0f, 46.0f};
    auto zoomBtn = std::make_shared<UltraCanvasButton>(
        "ShaderZoomBtn", btnHome.x, btnHome.y, 28.0f, 24.0f, "⛶");
    zoomBtn->SetFontSize(15.0f);
    zoomBtn->SetBackgroundColor(Color(0, 0, 0, 120));
    zoomBtn->SetTextColors(Color(255, 255, 255, 255));
    zoomBtn->SetCornerRadius(4.0f);
    zoomBtn->SetTooltip("Maximize canvas (or double-click it; Esc to restore)");
    root->AddChild(zoomBtn);

    // Raw pointers are captured deliberately: the surface/button own the
    // callbacks below and every element here outlives them via `root`, so
    // capturing shared_ptrs would form a reference cycle that leaks the tab.
    auto zoomActive = std::make_shared<bool>(false);
    UltraCanvasGLSurface* surf = surface.get();
    UltraCanvasContainer* rootPtr = root.get();
    UltraCanvasButton* btnPtr = zoomBtn.get();
    std::vector<UltraCanvasUIElement*> chrome = {
        title.get(), codeTitle.get(), codeArea.get(), panel.get()
    };
    const Point2Df homePos{16.0f, 40.0f};
    const float homeW = 660.0f, homeH = 360.0f;

    // Shared toggle, called by both the button and the canvas gestures.
    auto toggleZoom = std::make_shared<std::function<void()>>();
    *toggleZoom = [zoomActive, surf, rootPtr, btnPtr, chrome, homePos, homeW, homeH, btnHome]() {
        if (!*zoomActive) {
            *zoomActive = true;
            for (auto* el : chrome) el->SetVisible(false);
            auto area = rootPtr->GetContentArea();
            surf->SetElementAbsolutePosition({0.0f, 0.0f});
            surf->SetElementSize(CSSLayout::Dimension::Px((float)area.width),
                                 CSSLayout::Dimension::Px((float)area.height));
            btnPtr->SetElementAbsolutePosition({(float)area.width - 36.0f, 8.0f});
            btnPtr->SetTooltip("Restore canvas (Esc or click)");
            surf->SetFocus(true);   // route Esc (sent to the focused element) here
            surf->RequestRender();
        } else {
            *zoomActive = false;
            surf->SetElementAbsolutePosition(homePos);
            surf->SetElementSize(CSSLayout::Dimension::Px(homeW),
                                 CSSLayout::Dimension::Px(homeH));
            btnPtr->SetElementAbsolutePosition(btnHome);
            btnPtr->SetTooltip("Maximize canvas (or double-click it; Esc to restore)");
            for (auto* el : chrome) el->SetVisible(true);
            surf->RequestRender();
        }
    };

    zoomBtn->SetOnClick([toggleZoom]() { (*toggleZoom)(); });

    surf->SetEventCallback([zoomActive, toggleZoom](const UCEvent& e) -> bool {
        if (!*zoomActive) {
            if (e.type == UCEventType::MouseDoubleClick) { (*toggleZoom)(); return true; }
            return false;
        }
        // Maximized: a single click or Esc returns to the normal layout.
        if (e.type == UCEventType::MouseDown ||
            (e.type == UCEventType::KeyDown && e.virtualKey == UCKeys::Escape)) {
            (*toggleZoom)();
            return true;
        }
        return false;
    });

    return root;
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_ENABLE_GL
