- **macOS: a build without the GL surface no longer needs OpenGL.framework.**
  `UltraCanvas/CMakeLists.txt` looked the framework up as `REQUIRED` whatever
  `ULTRACANVAS_ENABLE_GL` said, although only the GL surface links it. So
  `-DULTRACANVAS_ENABLE_GL=OFF` could not configure on a macOS SDK without
  OpenGL, which Apple has deprecated since 10.14. It is now looked up only
  when the surface is on. If the SDK lacks it, configure warns and turns the
  surface off, as a missing GLEW does on Windows. CI's "Verify OpenGL backend
  enabled" step still fails when the CGL backend goes missing, so a release
  cannot lose it unnoticed.
- **CI runs UltraCleaner's engine tests.** `ULTRACANVAS_BUILD_ULTRACLEANER_TESTS`
  was on in no build row, so the cleaner's suite - path guard, rule table,
  scanner, remover, album logic - ran only on developers' machines. Every row
  builds it now. Linux runs it in the full test pass, and macOS and Windows
  run it in a step of their own: the recycle bin and UTF-16 paths on Windows,
  `~/.Trash` and case-insensitive names on macOS, are code only those systems
  exercise.
