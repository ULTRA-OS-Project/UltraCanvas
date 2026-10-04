- **An eSCL scanner that does not name its model is shown by its own name.**
  Discovery fell back to the mDNS plugin's `dn`, which is the full service
  name, so such a scanner was listed as "Office Scanner._uscan._tcp.local" -
  and on macOS as `Office\032Scanner._uscan._tcp.local.`. It is now
  "Office Scanner", cut out and unescaped by `EsclInstanceFromServiceName`,
  with the full name kept as the `mdns-name` attribute. The cutting and
  unescaping moved out of the IPP backend into `UltraCanvasIODeviceDnsSd.h`,
  which both backends now use.
