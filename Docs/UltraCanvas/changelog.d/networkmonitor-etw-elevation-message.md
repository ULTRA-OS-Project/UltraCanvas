- **NetworkMonitor (Windows): the two ETW sources say what to do when they
  are refused.** Started unelevated, the DNS client source and the kernel
  network event source reported "Starting the ETW session: error 5 (an
  elevated token is needed ...)". The access-denied case now reads "Needs an
  elevated run: Windows lets only an administrator trace the DNS client, so
  no process can be put to its queries until the monitor is run as
  administrator" (and the kernel source's counterpart, naming the snapshot
  differ as what still reports); any other failure keeps its error number.
