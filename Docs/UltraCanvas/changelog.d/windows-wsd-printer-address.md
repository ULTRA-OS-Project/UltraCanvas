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
    and every address the printer answered from.
  - The matching is the new platform-neutral `IppHostsForWindowsQueue()`,
    `IppPrinterIsWindowsQueue()`, `IppUriHost()` and `IppNormalizeHost()`.
    `IODevicePrinterIPPTest` covers them with 19 new checks. The SetupAPI walk
    only runs on Windows and is cross-compiled, not yet run on real hardware.
- **IODeviceManager README: the *Printer Status* example compiles.** It used
  `printer` without declaring it, which `check_doc_examples.py` reported. It
  now gets the printer the way the printing example above it does.
- **The mDNS plugin reports every address a service answers from**, not
  only the first (plugin 0.3.0). `attributes["ip"]` now lists all of them,
  IPv4 first, and a service answered more than once is one entry.
  - Avahi reported a service once per interface and IP version, as separate
    entries, each with one address. Each is now resolved to its own family's
    address, and the answers are merged into one entry.
  - Win32 reported the IPv6 address only when there was no IPv4 one. It now
    reports both.
  - Bonjour reported no address at all. It now asks for both families
    (`DNSServiceGetAddrInfo`), waiting at most a second for the first answer
    and 150 ms more for the other family.
  - The merging is the new `Mdns::AddAddress()` and `Mdns::MergeAnswer()`,
    tested in `MdnsNamesTest`. The Bonjour branch was only syntax-checked
    here; macOS CI compiles it.
