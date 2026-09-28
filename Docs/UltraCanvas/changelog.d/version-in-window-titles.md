- **Every UltraCanvas app shows its version in its main window title**
  (`UltraMail 0.10.5`, `UltraPaint 0.2.8`, `picture.png - UltraPaint 0.2.8`, ...),
  so a screenshot tells which build it came from. The DemoApp window reads
  `UltraCanvas Framework <version> - Component Demonstration`. Each app takes
  the number from its own changelog through `cmake/UltraCanvasVersion.cmake`;
  UltraMail, UltraSocial, EmailCleaner, AnchorPoint, UltraFIBU and UltraWin
  Manager gain the `<APP>_VERSION` compile definition for it, and UltraViewer's
  now carries its own version instead of the framework's. `AGENTS.md`
  (*Versioning*) makes the title a rule for new apps.
