- **UltraNet and UltraWin are module homes like the others, and the net tests
  carry no second UltraNet.** The two modules the shared core had folded in
  since they existed kept their archives under the public names, so every
  consumer special-cased the core's type (`_uc_core_shared` in UltraMail,
  UltraSocial, UltraCanvasStart, UltraWeb and UltraCloud) and the test
  binaries that linked the archive beside the shared core took the module
  from the archive: UltraNetTests held 130 `UltraNet_*` functions of its own
  and UltraNetApiStatus 111, a second copy of the module and its global state
  next to the core's. The archives are `uc-net` and `uc-win` now, `UltraNet`
  and `UltraWin` are INTERFACE homes that resolve to the core or, under a
  static core, to the archive, and every consumer links the name; the
  conditionals are gone. The one place an archive is still named is the
  rescanned link group a *static* core needs for the mutual
  UltraNet/UltraCanvas references (`Tests/UltraNet`, `Tests/UltraSocial`).
