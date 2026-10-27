- **A device whose certificate changed was reached on the old TLS session.**
  UltraNet 0.5.0's per-thread connection pool for the synchronous HTTP
  verbs shared libcurl's TLS session cache too, so the next call after a
  server changed its key resumed the old session and never saw the new
  certificate: a scanner trusted on first use could not be re-learned after
  its key was forgotten (`IODeviceScannerESCLLiveTest` failed on every Linux
  row of CI since 0.9.414), and a relay pinned by key would have stayed on
  the old one. Connections and DNS answers stay shared; TLS sessions do not.
