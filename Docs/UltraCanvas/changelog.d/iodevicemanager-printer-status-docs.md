- **IODeviceManager's README documents printer status.** A new *Printer
  Status* section shows `PrinterDevice::GetStatus()` and `GetSupplyLevels()`.
  It says that both need an open session and return nothing otherwise, and
  which backends report supply levels: CUPS, IPP and the Windows spooler
  backend (through the driver) do, and a printer that reports none gives an
  empty list rather than zero. DeviceExplorer 0.2.0 shows both for the
  selected printer.
