- **A widget the build would have in no form now stops the configure.** A
  widget left out of `ULTRACANVAS_WIDGETS` must be buildable as a runtime
  module. Three things rule a module out:
  - the build has no runtime modules (WebAssembly, Android, or
    `ULTRACANVAS_WIDGET_RUNTIME_MODULES=OFF`);
  - a static core on Windows;
  - a static core for a widget built against a shared core only (the
    MediaViewer).

  The configure now stops and names the widgets and the ways out. Before, it
  warned or skipped them, and an application asking for one by name found out
  only at run time. The Element modules CI job's static row compiles the
  MediaViewer in, and a new step checks that leaving it out of a static core
  is refused.
