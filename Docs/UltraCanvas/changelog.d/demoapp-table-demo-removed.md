- DemoApp: `UltraCanvasTableDemo.cpp` is gone. It was in no build (not in the
  DemoApp's source list), nothing called its `CreateDomainTableDemo`, and it
  no longer compiled: it called an `UltraCanvasMenu::ShowAt` that does not
  exist.
