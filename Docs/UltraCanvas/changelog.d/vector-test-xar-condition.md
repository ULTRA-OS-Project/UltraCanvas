- Tests: `VectorFormatsPluginTest` expects `.xar` to be readable, previewable
  and loadable only when the XAR plugin is built (`ULTRACANVAS_PLUGIN_XAR`),
  as it already did for `.cdr` and the CDR plugin. A build with the Vector
  plugin and without the XAR plugin failed two checks; it now checks that XAR
  is not claimed for reading there.
