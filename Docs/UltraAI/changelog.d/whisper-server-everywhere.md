- **Local speech to text on every platform.** The gallery's `whisper-server`
  now has a build for macOS (Metal on Apple silicon, CPU on Intel Macs, from
  13.3), Windows on ARM (CPU and Vulkan) and Vulkan on Windows x64, from this
  repository's release `whisper-server-v1.9.0-r1`, each built by
  `.github/workflows/whisper-server.yml` and tested by transcribing a sample
  with the runtime's own command line, and pinned by SHA-256. The speech
  models are offered there, and Auto picks Metal or Vulkan where the computer
  has the GPU. Linux keeps upstream's CPU builds by default: its Vulkan
  builds need glibc 2.39 (Ubuntu 24.04 and later), so they are optional,
  used only when chosen. The model server's seed carries the same list.
