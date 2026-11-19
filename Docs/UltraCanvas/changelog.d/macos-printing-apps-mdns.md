- **Every macOS app that prints carries UltraNet's `mdns` plug-in.** Texter,
  UltraFiler and UltraCanvasDemo print through the framework's print dialog,
  whose choice is looked up among IODeviceManager's printers. Their bundles
  carried no UltraNet plug-in, so there IODeviceManager listed only CUPS's
  queues: the network printers CUPS does not reach (IPP enumeration) and
  the demo's eSCL scanners, both found by browsing DNS-SD through `mdns`,
  were missing, while Linux and Windows give every app every plug-in from
  one shared folder. The macOS print panel itself offers CUPS's queues, which
  the CUPS backend finds without it. `package-macos.sh` now names `mdns` for
  them in `ultranet_plugins_of`, beside DeviceExplorer (`mdns`) and
  UltraMail (all), and `AGENTS.md` makes it the rule for any app that prints.
