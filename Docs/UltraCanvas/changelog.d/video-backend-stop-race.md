- **Video: stopping the GStreamer backend no longer hangs.** The backend runs
  a GLib event loop on a thread of its own, and `Stop()` quit it with
  `g_main_loop_quit` from the calling thread. A quit that came before that
  thread had reached `g_main_loop_run` was lost: the loop then started and ran
  for ever, and `Stop()` waited for it. A process that used video briefly
  could hang at exit, where a static destructor stops the backend, and the
  busier the machine the more often: `VideoCodecPluginTest` timed out in about
  1 run in 13 when 24 ran at once. The quit is now a source queued on the
  loop's own context, which the loop runs whenever it runs. A new case in
  `VideoCodecPluginTest` starts and stops the backend 200 times with a
  watchdog; it hung on every run before the change. `VideoBackendGStreamer.cpp`
  0.1.12.
