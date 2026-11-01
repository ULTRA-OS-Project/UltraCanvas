- **A static build links ChatCube and chatcube-peer again.** UltraRTC's
  engine calls back into the device glue compiled into the core library
  (`UltraRtcInternal::OpenCamera`, `OpenMicrophone`, `OpenSpeaker`), and the
  core links UltraRTC: a cycle between two static archives that was declared
  in one direction only, so GNU ld's single pass left those three undefined
  in every executable that links UltraRTC (`ULTRACANVAS_BUILD_SHARED` off,
  the default; CI builds shared and never saw it). `UltraCanvas/CMakeLists.txt`
  now declares the back edge too, and CMake repeats the two archives on the
  link line.
