- **The Windows CI legs survive a slow MSYS2 mirror.** The setup action did
  the package database sync, the upgrade and the install itself, with no
  retry and no mirror setting, and pacman abandons a mirror that stays
  below 1 byte/s for 10 s - on 2026-10-08 that failed both Windows legs of
  #740 in the same minute, before a line was compiled. The action now only
  unpacks MSYS2; a step of the workflow's own syncs, upgrades and installs
  with pacman's download timeout disabled and up to five attempts per
  phase, and keeps the downloaded packages in a cache of its own, pruned to
  one version per package.
