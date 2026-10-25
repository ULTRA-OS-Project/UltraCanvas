- **ChatCube crashed at start in a folder holding older UltraCanvas DLLs**
  (Windows). It now says that the `libUltraCanvas.dll` it loaded is from
  another build - with its path and both versions - and exits instead of
  crashing; keep ChatCube with the DLLs from its own package.
