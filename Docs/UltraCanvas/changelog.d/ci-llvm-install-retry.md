- **CI: installing Clang on the Linux legs survives a short apt.llvm.org
  outage.** The *Install Clang (Linux)* step fetched `llvm.sh` with a bare
  `wget` and no retry, so on 2026-11-03 a momentary network failure (exit 4)
  failed both Linux builds in under a second, before anything compiled. The
  script is now fetched with curl's own retries (`--retry 5
  --retry-all-errors`), and the whole fetch-and-install - `llvm.sh` reaches
  the same host again for the key, the index and the packages - is tried
  three times, 30 s and 60 s apart, before the step fails with a message
  saying the host may be down.
