- **UltraCanvasNetworkPlaces: the network's computers and shares, and WSL.**
  Below a share (`\\NAS\photos`) or a WSL distribution (`\\wsl$\Ubuntu`)
  everything is an ordinary path, but nothing said which computers, shares or
  distributions there are. New `UltraCanvasNetworkPlaces.h`:
  `ListNetworkComputers` (the Windows network's workgroup or domain, and the
  servers of the user's connections), `ListNetworkShares` (a computer's
  folders, hidden and administrative shares left out), `ListWslDistributions`
  (from the registry, starting none) and `IsNetworkServerPath` for a bare
  `\\NAS`, which no folder call can list. The Windows backend loads
  `mpr.dll` and `netapi32.dll` from System32 when first asked instead of
  importing them - an installed package can carry an ImageMagick coder named
  `mpr.dll`. Elsewhere the lists are empty. `NetworkPlacesTest` covers the
  path rules.
