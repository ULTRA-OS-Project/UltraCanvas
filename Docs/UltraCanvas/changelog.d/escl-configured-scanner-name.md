- **A scanner named in `ULTRACANVAS_ESCL_SCANNERS` is called what it is.**
  It has no DNS-SD instance name, so it was listed under its URL - and a
  certificate trusted for it read *(name not known)* in UOS-Settings. Once its
  `ScannerCapabilities` are read it takes the make and model they report
  ("Acme MegaScan 42") as its name, and its model and serial number are filled
  in, as an IPP printer configured by address already took its
  `printer-name`. A scanner found by DNS-SD keeps its instance name.
