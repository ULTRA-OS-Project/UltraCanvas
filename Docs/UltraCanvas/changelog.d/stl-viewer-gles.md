- **The STL viewer compiles for Android.** `UltraCanvasSTLElement.cpp` is part
  of the core library. Its GL viewer included desktop OpenGL's `<GL/gl.h>`,
  which the Android NDK does not have, and its shaders were written for GLSL
  3.30 only, which OpenGL ES rejects.
  - On Android it includes `<GLES3/gl3.h>`. Every call it makes is in OpenGL
    ES 3.
  - Its shaders begin with `#version 300 es`, and the fragment shader adds
    the float precision that ES requires.
  - Desktop OpenGL is unchanged. `glslangValidator` accepts both shaders as
    GLSL ES 3.00.
