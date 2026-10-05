- **A Windows crash report now names the function and the call stack.** The
  message box and the crash line in the log said only *"at 0x00007FFB1212C86D
  in libUltraCanvas.dll"* - an address that changes on every start, since the
  DLL loads somewhere else each time (ASLR), and that nobody can trace to a
  line without the dump and the matching build in a debugger. UltraPassword's
  crash on pasting a password (UCDemo 0.9.147, Windows 11) arrived as exactly
  that and could not be placed. The report now adds the offset into the module,
  which is the same on every start, and the function, taken from the module's
  export table: `libUltraCanvas.dll` is linked with
  `WINDOWS_EXPORT_ALL_SYMBOLS`, so every framework function with external
  linkage is named there (`libUltraCanvas.dll+0x<offset>
  UltraCanvas::<Class>::<Function>+0x<n>`). On x64 the box and the log
  then list up to 16 callers the same way, walked from the faulting context with
  `RtlLookupFunctionEntry` / `RtlVirtualUnwind`. A call through a freed object
  that jumps to no module at all is followed by the caller that made it. The
  filter still allocates nothing and loads nothing: it reads the modules' own
  mapped headers, never leaves the thread's stack, skips the walk for a stack
  overflow, and does not walk twice if a walk over a smashed stack faults.
  Checked under Wine 9 with a MinGW build: a fault in an exported member
  function of a DLL, and a virtual call through a bad pointer, both reported
  with every frame down to `RtlUserThreadStart`. `UltraCanvasWindowsDiagnostics.md`
  shows the new lines. ARM64 builds name the faulting function without the
  call stack.
