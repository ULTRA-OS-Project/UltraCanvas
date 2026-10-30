- **SmartHome linked the UltraDatabase archive next to the shared core.**
  `SmartHome/CMakeLists.txt` linked `uc-database` directly while also linking
  `libUltraCanvas`, which on a shared core already carries the whole archive
  (see *MODULE HOMES* in `UltraCanvas/CMakeLists.txt`). SmartHome Center and
  the SmartHome tests therefore got a second copy of UltraDatabase's code and
  global state - two driver registries, two connection tables - and on
  Windows risked "multiple definition" link errors. SmartHome now links the
  `UltraDatabase` home, which resolves to the shared core (or to the archive
  on a static core), like every other consumer.
