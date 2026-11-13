- **A chart the build would have in no form now stops the configure, or is
  compiled in.** A chart left out of `ULTRACANVAS_CHARTS` (the default
  selection is empty) must be buildable as a runtime module. On WebAssembly
  and Android, which load no modules, it is now compiled in, as the chart
  engine proposal's compile-time fallback (§12.11) says. Before, it was left
  out with a warning, so the default WebAssembly and Android builds had no
  parallel coordinate chart. With `ULTRACANVAS_CHART_RUNTIME_MODULES=OFF`, or
  a static core on Windows, the configure now stops and names the chart and
  the ways out, as it does for widgets.
