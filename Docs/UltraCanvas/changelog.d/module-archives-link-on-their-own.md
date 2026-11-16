- **Every module archive is now held to carrying its own dependencies.** With
  a static core a module's public name resolves to its archive alone, so a
  symbol that archive forgot to ask for has to come from somewhere - and in an
  ordinary build it does, because every application links several archives at
  once and they cover for each other. The gap shows only in a program whose
  link line is short enough to miss it. That is how `uc-net` reached a clean
  build with `UltraNetMime` calling `Trim()` and `Base64Encode()` from an
  archive it had never named, and `ultradbadmin-relay` failed to link months
  later. The new `ultracanvas-module-link-probes` target builds one probe per
  archive: an empty `main`, that archive forced in whole, nothing else of
  UltraCanvas on the line. Every object's undefined symbols then have to
  resolve from the archive's own declared dependencies, so a gap fails in CI -
  the static row of the Element modules workflow - rather than in somebody's
  application. Twelve archives pass today. `uc-rtc` is left out, because its
  engine really does call the device glue compiled into the core, and a shared
  core gets no probes at all: there every archive is absorbed into
  `libultracanvas` and is *meant* to leave its shared home to supply what it
  uses. Backing the `uc-net` fix out again reports `UltraCanvas::Trim`,
  `Base64Encode` **and** `Base64Decode` - one more than the relay had got far
  enough to meet.
