- **CI: every download retries, not only the Clang install.** After the
  `llvm.sh` fix, six more network steps still failed a job on the first
  dropped connection: the libopusenc and c-ares tarballs (`curl | tar`), the
  libjxl clone and its submodules, the MuPDF clone fallback, the rustup
  installer and its toolchain download (`build.yml`), and the emsdk clone and
  `emsdk install` (`wasm-build.yml`). The tarballs are now fetched into a
  file under curl's own retries (`--retry 5 --retry-all-errors`) and then
  unpacked, so a cut-off transfer never reaches `tar`. The rest run under the
  new `scripts/ci-retry.sh`: three tries, 30 s and 60 s apart, each announced
  as a warning, with `--clean DIR` emptying a clone's folder before every try
  so a half-made clone does not fail the next one. pip, npm, vcpkg and the
  apt installs (`ci-apt.sh`) already retried and are unchanged. `AGENTS.md`
  says to use the same for any new download.
