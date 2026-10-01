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
