- **An eSCL scanner is listed by its own name, not its model.** Discovery
  named a scanner by its TXT record's `ty`, so two scanners of one model were
  listed under the same name and could not be told apart. It now uses the
  DNS-SD instance name, which is unique on the network - as the IPP backend
  does for printers - and keeps the model in `model`.
- **And that name is no longer the full service name.** A scanner without a
  `ty` fell back to the mDNS plugin's `dn`, so it was listed as
  "Office Scanner._uscan._tcp.local" - and on macOS as
  `Office\032Scanner._uscan._tcp.local.`. It is now "Office Scanner", cut out
  and unescaped by `EsclInstanceFromServiceName`, with the full name kept as
  the `mdns-name` attribute. The cutting and unescaping moved out of the IPP
  backend into `UltraCanvasIODeviceDnsSd.h`, which both backends now use.
