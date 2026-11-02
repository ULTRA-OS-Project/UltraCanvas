- **An element plug-in compiled against other framework headers is not
  called.** The classes a plug-in creates derive from `UltraCanvasUIElement`
  (the charts from deeper framework classes still), and the host calls their
  virtual functions and reads their members where the framework's headers put
  them - which no version number covered. CMake now writes
  `UltraCanvasFrameworkHeadersId.h` (a hash of every header under
  `include/`, `libspecific/` and `OS/`, taken at configure time like a DLL's
  build id, carried by the library's interface and installed beside the
  headers), `ULTRACANVAS_DEFINE_ELEMENT_PLUGIN` exports it as
  `UltraCanvas_PluginHeadersId`, and `UltraCanvas_RefreshElementPlugins`
  skips a plug-in with another id, `"unknown"` or none before it calls
  `UltraCanvas_PluginInit`, logging both ids and that it must be rebuilt.
  `cmake/UltraCanvasLibraryBuildId.cmake` gains `ultracanvas_hash_headers`,
  which the DLL build id now uses too (the ids it gives are unchanged).
