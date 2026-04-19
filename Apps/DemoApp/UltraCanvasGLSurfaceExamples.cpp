// Apps/DemoApp/UltraCanvasGLSurfaceExamples.cpp
// OpenGL Surface demonstration - spinning cube with real OpenGL rendering
// Version: 1.0.0
// Last Modified: 2025-01-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"

#ifdef ULTRACANVAS_ENABLE_GL

#include "UltraCanvasGLSurface.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasBoxLayout.h"

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#include <cmath>
#include <iostream>

namespace UltraCanvas {

// Simple spinning cube demo state
struct CubeState {
    float rotationX = 0.0f;
    float rotationY = 0.0f;
    float rotationSpeed = 1.0f;
    bool spinning = true;
    Color clearColor{40, 60, 80, 255};
};

// Cube vertices (position only, simple colored cube)
static const float cubeVertices[] = {
    // Front face
    -0.5f, -0.5f,  0.5f,
     0.5f, -0.5f,  0.5f,
     0.5f,  0.5f,  0.5f,
    -0.5f,  0.5f,  0.5f,
    // Back face
    -0.5f, -0.5f, -0.5f,
    -0.5f,  0.5f, -0.5f,
     0.5f,  0.5f, -0.5f,
     0.5f, -0.5f, -0.5f,
    // Top face
    -0.5f,  0.5f, -0.5f,
    -0.5f,  0.5f,  0.5f,
     0.5f,  0.5f,  0.5f,
     0.5f,  0.5f, -0.5f,
    // Bottom face
    -0.5f, -0.5f, -0.5f,
     0.5f, -0.5f, -0.5f,
     0.5f, -0.5f,  0.5f,
    -0.5f, -0.5f,  0.5f,
    // Right face
     0.5f, -0.5f, -0.5f,
     0.5f,  0.5f, -0.5f,
     0.5f,  0.5f,  0.5f,
     0.5f, -0.5f,  0.5f,
    // Left face
    -0.5f, -0.5f, -0.5f,
    -0.5f, -0.5f,  0.5f,
    -0.5f,  0.5f,  0.5f,
    -0.5f,  0.5f, -0.5f,
};

// Face colors (RGBA for each face)
static const float faceColors[] = {
    1.0f, 0.0f, 0.0f, 1.0f,  // Front - Red
    0.0f, 1.0f, 0.0f, 1.0f,  // Back - Green
    0.0f, 0.0f, 1.0f, 1.0f,  // Top - Blue
    1.0f, 1.0f, 0.0f, 1.0f,  // Bottom - Yellow
    1.0f, 0.0f, 1.0f, 1.0f,  // Right - Magenta
    0.0f, 1.0f, 1.0f, 1.0f,  // Left - Cyan
};

// Simple vertex shader
static const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;

uniform mat4 uModelView;
uniform mat4 uProjection;
uniform vec4 uColor;

out vec4 vColor;

void main() {
    gl_Position = uProjection * uModelView * vec4(aPos, 1.0);
    vColor = uColor;
}
)";

// Simple fragment shader
static const char* fragmentShaderSource = R"(
#version 330 core
in vec4 vColor;
out vec4 FragColor;

void main() {
    FragColor = vColor;
}
)";

// Helper to compile shader
static GLuint CompileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        std::cerr << "[GLSurfaceDemo] Shader compilation failed: " << infoLog << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// Helper to create shader program
static GLuint CreateShaderProgram() {
    GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, vertexShaderSource);
    if (!vertexShader) return 0;

    GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    if (!fragmentShader) {
        glDeleteShader(vertexShader);
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        std::cerr << "[GLSurfaceDemo] Shader linking failed: " << infoLog << std::endl;
        glDeleteProgram(program);
        program = 0;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return program;
}

// Simple 4x4 matrix helpers
static void MatrixIdentity(float* m) {
    for (int i = 0; i < 16; i++) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void MatrixMultiply(float* result, const float* a, const float* b) {
    float temp[16];
    for (int i = 0; i < 4; i++) {        // i = column of result
        for (int j = 0; j < 4; j++) {    // j = row of result
            temp[i * 4 + j] = 0.0f;
            for (int k = 0; k < 4; k++) {
                temp[i * 4 + j] += a[k * 4 + j] * b[i * 4 + k];
            }
        }
    }
    for (int i = 0; i < 16; i++) result[i] = temp[i];
}

static void MatrixRotateX(float* m, float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);
    float rot[16];
    MatrixIdentity(rot);
    rot[5] = c;  rot[6] = s;
    rot[9] = -s; rot[10] = c;
    float temp[16];
    for (int i = 0; i < 16; i++) temp[i] = m[i];
    MatrixMultiply(m, temp, rot);
}

static void MatrixRotateY(float* m, float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);
    float rot[16];
    MatrixIdentity(rot);
    rot[0] = c;  rot[2] = -s;
    rot[8] = s;  rot[10] = c;
    float temp[16];
    for (int i = 0; i < 16; i++) temp[i] = m[i];
    MatrixMultiply(m, temp, rot);
}

static void MatrixTranslate(float* m, float x, float y, float z) {
    m[12] += m[0] * x + m[4] * y + m[8] * z;
    m[13] += m[1] * x + m[5] * y + m[9] * z;
    m[14] += m[2] * x + m[6] * y + m[10] * z;
}

static void MatrixPerspective(float* m, float fov, float aspect, float near, float far) {
    float f = 1.0f / std::tan(fov * 0.5f);
    MatrixIdentity(m);
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (far + near) / (near - far);
    m[11] = -1.0f;
    m[14] = (2.0f * far * near) / (near - far);
    m[15] = 0.0f;
}

// GL resources for the demo
struct GLResources {
    GLuint program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLint modelViewLoc = -1;
    GLint projectionLoc = -1;
    GLint colorLoc = -1;
    bool initialized = false;
};

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateGLSurfaceExamples() {
    auto mainContainer = std::make_shared<UltraCanvasContainer>("GLSurfaceExamples", 3000, 0, 0, 1000, 760);

    // Title
    auto title = std::make_shared<UltraCanvasLabel>("GLSurfaceTitle", 3001, 20, 10, 600, 30);
    title->SetText("OpenGL Surface - Hardware Accelerated 3D Rendering");
    title->SetFontSize(16);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Color(50, 50, 150, 255));
    mainContainer->AddChild(title);

    auto subtitle = std::make_shared<UltraCanvasLabel>("GLSurfaceSubtitle", 3002, 20, 40, 800, 20);
    subtitle->SetText("Real OpenGL 3.3 rendering composited into UltraCanvas via CPU readback");
    subtitle->SetFontSize(11);
    subtitle->SetTextColor(Color(100, 100, 100, 255));
    mainContainer->AddChild(subtitle);

    // Shared state for the demo
    auto cubeState = std::make_shared<CubeState>();
    auto glResources = std::make_shared<GLResources>();

    // Create the GL surface
    GLSurfaceConfig config;
    config.glVersionMajor = 3;
    config.glVersionMinor = 3;
    config.coreProfile = true;
    config.depthBits = 24;
    config.stencilBits = 0;
    config.samples = 1;  // No MSAA for simplicity

    auto glSurface = std::make_shared<UltraCanvasGLSurface>(config, "SpinningCube", 20, 70, 600, 500);
    glSurface->SetRenderMode(RenderMode::Continuous);  // Animate continuously

    // GL initialization callback
    glSurface->SetInitCallback([glResources]() {
        std::cout << "[GLSurfaceDemo] Initializing OpenGL resources..." << std::endl;

        // Create shader program
        glResources->program = CreateShaderProgram();
        if (!glResources->program) {
            std::cerr << "[GLSurfaceDemo] Failed to create shader program" << std::endl;
            return;
        }

        // Get uniform locations
        glResources->modelViewLoc = glGetUniformLocation(glResources->program, "uModelView");
        glResources->projectionLoc = glGetUniformLocation(glResources->program, "uProjection");
        glResources->colorLoc = glGetUniformLocation(glResources->program, "uColor");

        // Create VAO and VBO
        glGenVertexArrays(1, &glResources->vao);
        glGenBuffers(1, &glResources->vbo);

        glBindVertexArray(glResources->vao);
        glBindBuffer(GL_ARRAY_BUFFER, glResources->vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);

        glEnable(GL_DEPTH_TEST);
        glResources->initialized = true;
        std::cout << "[GLSurfaceDemo] OpenGL resources initialized successfully" << std::endl;
    });

    // GL render callback
    glSurface->SetRenderCallback([glResources, cubeState](const RenderSurfaceInfo& info) {
        if (!glResources->initialized) return;

        // Clear
        glClearColor(
            cubeState->clearColor.r / 255.0f,
            cubeState->clearColor.g / 255.0f,
            cubeState->clearColor.b / 255.0f,
            1.0f
        );
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Update rotation
        if (cubeState->spinning) {
            cubeState->rotationY += static_cast<float>(info.deltaTime) * cubeState->rotationSpeed;
            cubeState->rotationX += static_cast<float>(info.deltaTime) * cubeState->rotationSpeed * 0.7f;
        }

        // Setup matrices
        float modelView[16], projection[16];

        // Perspective projection
        float aspect = static_cast<float>(info.width) / static_cast<float>(info.height);
        MatrixPerspective(projection, 45.0f * 3.14159f / 180.0f, aspect, 0.1f, 100.0f);

        // Model-view: translate back and rotate
        MatrixIdentity(modelView);
        MatrixTranslate(modelView, 0.0f, 0.0f, -3.0f);
        MatrixRotateX(modelView, cubeState->rotationX);
        MatrixRotateY(modelView, cubeState->rotationY);

        // Draw cube
        glUseProgram(glResources->program);
        glUniformMatrix4fv(glResources->modelViewLoc, 1, GL_FALSE, modelView);
        glUniformMatrix4fv(glResources->projectionLoc, 1, GL_FALSE, projection);

        glBindVertexArray(glResources->vao);

        // Draw each face with a different color
        for (int face = 0; face < 6; face++) {
            glUniform4fv(glResources->colorLoc, 1, &faceColors[face * 4]);
            glDrawArrays(GL_TRIANGLE_FAN, face * 4, 4);
        }

        glBindVertexArray(0);
        glUseProgram(0);
    });

    // GL cleanup callback
    glSurface->SetCleanupCallback([glResources]() {
        if (glResources->vbo) {
            glDeleteBuffers(1, &glResources->vbo);
            glResources->vbo = 0;
        }
        if (glResources->vao) {
            glDeleteVertexArrays(1, &glResources->vao);
            glResources->vao = 0;
        }
        if (glResources->program) {
            glDeleteProgram(glResources->program);
            glResources->program = 0;
        }
        glResources->initialized = false;
        std::cout << "[GLSurfaceDemo] OpenGL resources cleaned up" << std::endl;
    });

    mainContainer->AddChild(glSurface);

    // Control panel on the right
    auto controlPanel = std::make_shared<UltraCanvasContainer>("ControlPanel", 3010, 640, 70, 340, 500);
    controlPanel->SetBackgroundColor(Color(245, 245, 245, 255));
    controlPanel->SetBorders(1.0f);
    controlPanel->SetPadding(10.0f);

    // Control panel title
    auto controlTitle = std::make_shared<UltraCanvasLabel>("ControlTitle", 3011, 10, 10, 320, 25);
    controlTitle->SetText("Controls");
    controlTitle->SetFontWeight(FontWeight::Bold);
    controlTitle->SetFontSize(14);
    controlPanel->AddChild(controlTitle);

    // Spin toggle button
    auto spinButton = std::make_shared<UltraCanvasButton>("SpinButton", 3012, 10, 45, 150, 30);
    spinButton->SetText("Pause Rotation");
    spinButton->onClick = [spinButton, cubeState]() {
        cubeState->spinning = !cubeState->spinning;
        spinButton->SetText(cubeState->spinning ? "Pause Rotation" : "Resume Rotation");
    };
    controlPanel->AddChild(spinButton);

    // Speed slider label
    auto speedLabel = std::make_shared<UltraCanvasLabel>("SpeedLabel", 3013, 10, 90, 320, 20);
    speedLabel->SetText("Rotation Speed");
    speedLabel->SetFontSize(12);
    controlPanel->AddChild(speedLabel);

    // Speed slider
    auto speedSlider = std::make_shared<UltraCanvasSlider>("SpeedSlider", 3014, 10, 115, 300, 25);
    speedSlider->SetRange(0.1f, 5.0f);
    speedSlider->SetValue(1.0f);
    speedSlider->onValueChanged = [cubeState](float value) {
        cubeState->rotationSpeed = value;
    };
    controlPanel->AddChild(speedSlider);

    // Info section
    auto infoLabel = std::make_shared<UltraCanvasLabel>("InfoLabel", 3020, 10, 160, 320, 320);
    infoLabel->SetText(
        "This demo shows UltraCanvasGLSurface\n"
        "rendering a spinning 3D cube using\n"
        "real OpenGL 3.3 Core profile.\n\n"
        "Features demonstrated:\n"
        "• Hardware-accelerated GL context\n"
        "• FBO-based offscreen rendering\n"
        "• Shader-based rendering pipeline\n"
        "• Continuous animation mode\n"
        "• CPU readback compositing\n\n"
        "The GL content is rendered to an FBO,\n"
        "read back via glReadPixels, and\n"
        "composited into the Cairo surface.\n\n"
        "Future: GPU-native compositing via\n"
        "EGL Image / DMA-BUF for zero-copy."
    );
    infoLabel->SetFontSize(11);
    infoLabel->SetTextColor(Color(60, 60, 60, 255));
    infoLabel->SetAlignment(TextAlignment::Left);
    controlPanel->AddChild(infoLabel);

    mainContainer->AddChild(controlPanel);

    // Status bar at bottom
    auto statusBar = std::make_shared<UltraCanvasLabel>("StatusBar", 3030, 20, 580, 960, 25);
    statusBar->SetText("Status: OpenGL 3.3 Core Profile | Render Mode: Continuous | Compositing: CPU Readback");
    statusBar->SetFontSize(10);
    statusBar->SetTextColor(Color(100, 100, 100, 255));
    statusBar->SetBackgroundColor(Color(240, 240, 240, 255));
    statusBar->SetPadding(5.0f);
    mainContainer->AddChild(statusBar);

    return mainContainer;
}

} // namespace UltraCanvas

#else // !ULTRACANVAS_ENABLE_GL

namespace UltraCanvas {

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateGLSurfaceExamples() {
    auto container = std::make_shared<UltraCanvasContainer>("GLSurfaceExamples", 3000, 0, 0, 1000, 600);

    auto title = std::make_shared<UltraCanvasLabel>("GLSurfaceTitle", 3001, 10, 10, 300, 30);
    title->SetText("OpenGL Surface Examples");
    title->SetFontSize(16);
    title->SetFontWeight(FontWeight::Bold);
    container->AddChild(title);

    auto placeholder = std::make_shared<UltraCanvasLabel>("GLSurfacePlaceholder", 3002, 20, 50, 800, 400);
    placeholder->SetText(
        "OpenGL Surface Support - NOT AVAILABLE\n\n"
        "This build was compiled without OpenGL support.\n\n"
        "To enable OpenGL surface support:\n"
        "• Rebuild with -DULTRACANVAS_ENABLE_GL=ON\n"
        "• Ensure OpenGL development libraries are installed\n"
        "• On Linux: libgl-dev, libegl-dev\n"
        "• On macOS: OpenGL framework (included with Xcode)\n"
    );
    placeholder->SetAlignment(TextAlignment::Left);
    placeholder->SetBackgroundColor(Color(255, 200, 200, 100));
    placeholder->SetBorders(2.0f);
    placeholder->SetPadding(20.0f);
    container->AddChild(placeholder);

    return container;
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_ENABLE_GL
