- **Widgets can be runtime modules: `ULTRACANVAS_WIDGETS`, VideoTimeline
  first.** The widget counterpart of `ULTRACANVAS_CHARTS`. A selectable widget
  is compiled into the core when named in `ULTRACANVAS_WIDGETS` (`all`, the
  default, so nothing changes for an existing build). One left out is built as
  its own module, `ultracanvas-widget-<name>` in `<build>/Plugins/Elements`,
  which registers through the element plugin loader. The video timeline is
  registered as `"video-timeline"` (`MakeVideoTimelineDescriptor`,
  `RegisterVideoTimelineElement`). `UltraCanvas_RegisterSelectedWidgets()`
  registers the widgets compiled in. In a static core it also keeps the core
  objects the modules need, so a host loading a widget module calls it at
  startup. UltraVideo and DemoApp use the timeline's typed API, so configuring
  them with the timeline left out stops with an error. `VideoTimelinePluginTest`
  creates the timeline by name in both builds.
