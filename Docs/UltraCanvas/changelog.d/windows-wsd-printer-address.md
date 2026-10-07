- **Windows: a WSD print queue gets its printer's address, and a printer
  that is already a queue is no longer listed twice.**
  - A WSD port carries no address the spooler gives out. The spooler backend
    now reads it from Plug and Play: the queue's device node and the WSD
    device it prints to share a device container, and the WSD device's PnP-X
    `IpAddress` (or the hosts of its WS-Discovery `XAddrs` or location) name
    the printer. A queue in the computer's own container gives none.
  - So the IPP supply-level fallback now covers WSD queues too, tried at
    `/ipp/print`, `/ipp` and `/` on 631 like a Standard TCP/IP port.
  - The IPP backend skips a printer it finds over DNS-SD when a Windows
    queue already prints to that host, the way it defers to CUPS on Linux and
    macOS. A new `Internal::WindowsQueuePrinterHosts()` lists those hosts
    (IPP, Standard TCP/IP and WSD ports). The match is on the mDNS host name
    and its first-seen address.
  - The matching is the new platform-neutral `IppHostsForWindowsQueue()`,
    `IppPrinterIsWindowsQueue()`, `IppUriHost()` and `IppNormalizeHost()`.
    `IODevicePrinterIPPTest` covers them with 19 new checks. The SetupAPI walk
    only runs on Windows and is cross-compiled, not yet run on real hardware.
- **IODeviceManager README: the *Printer Status* example compiles.** It used
  `printer` without declaring it, which `check_doc_examples.py` reported. It
  now gets the printer the way the printing example above it does.
