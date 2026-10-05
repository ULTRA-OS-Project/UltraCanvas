- **A trusted device's certificate is kept with the device's name.**
  `DeviceCertificates.conf` lines now read
  `host:port=sha256//... Office Printer`, the name being the one the eSCL or
  IPP backend discovered the device under (`Internal::NoteDeviceTlsName`).
  A key learned before the name was known takes it on when the device is
  next listed, or - for a printer named only by its address - once it has
  described itself. `IODeviceTrustedCertificate` gains `name`, and lines
  without one, as the first version wrote them, still read.
