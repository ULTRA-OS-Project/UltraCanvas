- **`scripts/check_libcxx_syntax.py` runs in a cloud session.** It
  type-checks a change against libc++, the standard library the Windows
  rows compile against, and is the one check of the Windows build a session
  can run - but it needs libc++'s headers, and the cloud image has none.
  Installing `libc++-dev` is not the answer on Ubuntu 24.04: it needs LLVM's
  `libunwind-18-dev`, which conflicts with the `libunwind-dev` that
  `libgstreamer1.0-dev` needs, so apt removes the one for the other,
  GStreamer's pkg-config file stops resolving, and the next configure leaves
  the media backends out without a word (tried on 2026-11-17). The script
  now uses a private copy of the headers when no libc++ is installed:
  `--fetch-libcxx` downloads the libc++ and libc++abi dev packages matching
  clang's version (`apt-get download`) and unpacks them into
  `~/.cache/ultracanvas/libcxx` (`$ULTRACANVAS_LIBCXX_ROOT`) - no root, and
  no installed package changes. The session hook
  (`.claude/hooks/session-start.sh` 1.8.0) fetches it at start: a few
  seconds the first time, nothing after. The script's messages no longer
  recommend the install that breaks GStreamer. Checked: the private headers
  reproduce PR #973's Windows-only error (`call to 'to_string' is
  ambiguous`), which libstdc++ compiles.
