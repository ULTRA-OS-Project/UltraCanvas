- **Windows: "Linux" and "Network" in the folder tree.** Two sections after
  Computer, where Explorer has them. *Linux* lists the WSL distributions
  (`\\wsl$\Ubuntu`), each opened like a drive - and not touched before it is
  opened, since touching one starts it. *Network* lists the computers of the
  workgroup or domain and the servers of your connections; opening one lists
  its shared folders under its row and in the folder display, read off the UI
  thread. Typing `\\NAS` into the path bar opens that computer too, and it is
  kept in the section, so a NAS the network list does not show stays one
  click away. Rests on the framework's new `UltraCanvasNetworkPlaces` - see
  the UltraCanvas changelog.
