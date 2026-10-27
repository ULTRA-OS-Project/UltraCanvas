- **The synchronous HTTP verbs' connection pool is opt-in, and shares no
  TLS sessions.** UltraNet 0.5.0 gave every synchronous HTTP call a
  per-thread libcurl share that pooled connections, DNS answers and TLS
  sessions for all callers at once. A pooled connection outlives the call
  that made it, and a resumed TLS session skips the certificate, so a
  device restarted, re-keyed or pinned between two calls was reached on the
  old connection or session: a scanner trusted on first use could not be
  re-learned after its key was forgotten, and the Linux rows of CI went red
  at "Run unit tests" from 0.9.414 on. The pool is now
  `UltraNetConfig::reuseHttpConnections`, off unless an application asks
  (UltraMail does, for its JMAP sync), and it never shares TLS sessions.
