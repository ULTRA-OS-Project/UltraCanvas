- **UltraNet's docs are compiled by the docs check, and the mistakes it
  found in them are fixed.** Nothing compiled the examples of
  `UltraNet/README.md`, and six had drifted from the headers or never built:
  a request body assigned as text (it is bytes), a bearer token written as
  the token object, a size limit and a cancel flag from nowhere, a log
  callback and the names the module reserves written as calls of functions
  no header has. With the examples' variables given their types a seventh
  showed, hidden until then: the SSH tunnel's private key is bytes
  (`std::vector<uint8_t>`), not text. `Docs/Modules/UltraNet/ApiStatus.md`
  named two things of the status tool's own header without saying so. All
  pass now - under clang 18 and under clang 14 with libstdc++ 12, CI's pair
  - and `UltraNet` is in `CHECKED_MODULE_DOCS` (`check_doc_examples.py`
  1.10.1), so `doc-examples.yml` fails a change that breaks one; the
  workflow starts for a change under `Docs/Modules/UltraNet/` and, which it
  did not, under `Docs/Modules/UltraLocalisation/`, a module already checked.
- **Cloud sessions run UltraNet's WebSocket tests and build its mDNS
  plug-in.** `.claude/hooks/session-start.sh` (1.9.0) installs Python's
  `websockets` package with pip, as CI does - Ubuntu 24.04's own is too old
  for the server the tests start - and `libavahi-client-dev`, which CI
  installs and the hook's list lacked. `UltraNetTests` skipped 22 tests in a
  cloud session; 13 of them now run (the twelve WebSocket tests and the mDNS
  plug-in's), and the nine left need what a session does not have: DNS
  outside the sandbox and a started SSH server.
