- **Network printers and eSCL scanners are found on macOS.** The macOS
  bundle came without UltraNet's `mdns` plug-in, the DNS-SD browser that the
  IPP printer and eSCL scanner discovery runs on, so on a Mac neither found
  anything on the network: printers came only from CUPS, eSCL scanners not
  at all. The bundle carries it now (`package-macos.sh`,
  `ultranet_plugins_of`).
