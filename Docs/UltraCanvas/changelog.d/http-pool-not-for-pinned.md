- **Trust on first use works again for printers and scanners.** Since the
  synchronous HTTP verbs keep a connection pool per thread, a request to a
  device whose key was just forgotten resumed the earlier TLS session, which
  shows no certificate: the key could not be learned again, and
  `IODevicePrinterIPPLiveTest` and `IODeviceScannerESCLLiveTest` failed on
  Linux. A request that pins a public key or reads the server's certificate
  (`pinnedPublicKey`, `capturePeerCertificate`) no longer uses the pool, so it
  always makes a full handshake - which a pin promises anyway.
