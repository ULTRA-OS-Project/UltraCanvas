- **S/MIME signatures from real authorities are vouched for on Windows and
  macOS too.** The certificate check uses the operating system's trusted
  roots there (Windows' certificate stores, macOS's keychains), as it used
  Linux's; until now a signature from a public certificate authority read
  "not vouched for here" on those two systems.
