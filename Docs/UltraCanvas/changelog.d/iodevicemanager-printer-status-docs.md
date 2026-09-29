- **IODeviceManager's README documents printer status.** A new *Printer
  Status* section shows `PrinterDevice::GetStatus()` and `GetSupplyLevels()`.
  It says that both need an open session and return nothing otherwise, and
  which backends report supply levels: CUPS and IPP do; the Windows spooler
  has none to give, so its list is empty rather than zero. DeviceExplorer
  0.2.0 shows both for the selected printer.
