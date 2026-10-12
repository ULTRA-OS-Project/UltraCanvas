- **Video playback without a PulseAudio server.** `VideoBackendGStreamer`
  0.1.13 probes the `pulsesink` it prefers to READY before handing it to
  playbin: where no PulseAudio server answers (a bare X session, a
  container, a desktop on ALSA or JACK alone) playbin given an unopenable
  sink failed to activate its sinks and nothing played - `Play()` returned
  false, with or without sound. Now playbin's `autoaudiosink` picks a sink
  that works, a silent one at worst, which still runs the clock: the
  picture plays at pace and the position is reported. With PulseAudio
  running nothing changes.
