- **whisper-server built here for the platforms upstream leaves out.** A new
  workflow, `.github/workflows/whisper-server.yml`, builds whisper.cpp's
  server at a release tag for macOS (Metal on Apple silicon, CPU on Intel),
  Windows on ARM (CPU and Vulkan) and Vulkan on Windows x64 and Linux x64 and
  arm64, tests each build by transcribing whisper.cpp's JFK sample with the
  runtime's own command line, and publishes them as a release of this
  repository with `SHA256SUMS` and the gallery's bundle entries
  (`scripts/whisper_gallery_entries.py`). Run by hand; a pull request that
  changes it builds and tests without publishing. The gallery takes the
  entries once the first release is out.
