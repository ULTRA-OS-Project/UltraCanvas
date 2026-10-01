- **The Windows spooler backend reports ink and toner levels.**
  `PrinterDevice::GetSupplyLevels()` (and the `supplies` in `GetStatus()`)
  used to be empty on Windows, because the spooler has no supply-level API.
  The backend now asks the printer's driver over its bidirectional channel:
  `IBidiSpl` `GetAll` on `\Printer.Consumables`, which drivers with a status
  monitor answer with each consumable's level, colour and type.
  - A driver without bidi support, or a printer that does not answer, still
    gives an empty list, never a made-up 0 %.
  - A level outside 0–100 stays *not reported* (-1).
  - The parsing is the new platform-neutral `IOSupplyLevelsFromBidi()`, tested
    in `IODevicePrinterTest` on every platform. The COM call only runs on
    Windows and is not yet verified against real hardware.
- **…and when the driver says nothing, asks the printer over IPP.** A queue
  that prints to a network address now gets its levels from the printer
  itself (builds with UltraNet).
  - The address comes from the queue's port: an IPP port's URL, or a
    Standard TCP/IP port's host (from the port monitor, or a name such as
    `IP_10.0.0.5`). It is tried at `/ipp/print`, `/ipp` and `/` on 631.
  - A host that refuses the connection is not tried on its other paths. A
    printer that does not answer is left alone for 60 s, because
    `GetStatus()` and `GetSupplyLevels()` would otherwise each wait out the
    5 s connect timeout.
  - The guesses are the new `IppUrisForWindowsPort()`, tested in
    `IODevicePrinterIPPTest`. The query is `Internal::QueryIppSupplyLevels()`,
    now also what the IPP backend's own `GetSupplyLevels()` uses, and
    `IODevicePrinterIPPLiveTest` checks it against `ippeveprinter`. That
    includes the difference the fallback relies on: a wrong path on a live
    host is not reported as unreachable.
  - USB and WSD queues get the driver's answer only.
