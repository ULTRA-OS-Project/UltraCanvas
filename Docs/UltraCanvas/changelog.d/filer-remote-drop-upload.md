- **UltraCanvasFilerWidget: files dropped onto a remote folder are uploaded.**
  A new optional hook, `remoteUpload`, receives the paths dropped onto a
  remote folder shown in the widget (from another program, or from another
  display of the same window); the host puts them onto the drive and
  refreshes. Without it the drop went through the local paste path and was
  refused as "not a writable folder". Dragging a remote display's own
  entries onto one of its folder tiles is refused with a message that says
  so, instead of "not a folder". Used by UltraFiler 1.47.0.
