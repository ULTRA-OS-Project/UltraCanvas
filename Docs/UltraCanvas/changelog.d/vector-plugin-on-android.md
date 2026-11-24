- **The Vector plug-in builds for Android.** It needs only libvips, zlib
  and tinyxml2, which the Android sysroot has, so the Android defaults no
  longer switch it off: an Android app that links `UltraCanvasVectorPlugin`
  and calls `RegisterVectorFormatsPlugin()` opens and saves SVG, AI, DXF,
  DWG, EMF, WMF and ODG drawings. MobilePC does, for ArtCreator. The CDR,
  XAR and EPS plug-ins stay off there.
