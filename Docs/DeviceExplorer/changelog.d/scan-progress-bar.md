- **A scan shows itself under the device tree.** While DeviceExplorer is
  scanning - at start-up and after *Rescan* - a row at the bottom of the tree
  reads *Scanning for devices…* beside a bar with a segment sliding along it
  (`UltraCanvasBusyIndicator`, `Bar`). It goes away, and its timer stops, as
  soon as the devices are in. The status text in the toolbar no longer says
  *Scanning for devices…* as well: it is empty during the first scan and
  keeps the last count during a rescan.
