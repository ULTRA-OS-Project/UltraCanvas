- **UltraCanvasNetworkPlaces: computers announced through WS-Discovery.**
  `ListNetworkComputers` listed only what the Windows network provider knows
  (the NetBIOS / SMB 1 browser list), so on a current network - Windows with
  SMB 1 off, NAS boxes running `wsdd` - it showed fewer computers than
  Explorer. It now also asks Function Discovery's WS-Discovery provider,
  alongside the provider's list and for at most six seconds, and keeps the
  devices that say they are a computer (`pub:Computer`, or the PnP-X category
  *Computers*); one that gives no name is listed by its IPv4 address. The
  Function Discovery interfaces are declared in the backend: mingw-w64's
  `functiondiscoveryapi.h` puts `IFunctionInstance`'s slots in the wrong
  order. Nothing new is linked (ole32 already is).
