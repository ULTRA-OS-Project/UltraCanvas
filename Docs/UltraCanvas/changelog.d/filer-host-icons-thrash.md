- **Host OS file icons no longer flicker in a window that shows more types
  than the icon cache holds.** With *Display > File icons = Host OS*, a
  maximised UltraFiler window on a folder of DLLs never settled: every tile
  flipped between the shell's icon and the simple one for as long as the
  window was open, while a smaller window on the same folder was fine. Two
  causes, both in the framework. The cache's eviction dropped the least
  recently *drawn* icons once over budget, without knowing they were still
  on screen - an icon dropped while drawn is asked for again on the next
  frame, and its answer evicts the next drawn one, without end. Eviction
  now keeps whatever this frame or the last one drew, however far over
  budget a screen full of distinct types puts the cache (the ceiling is met
  again when they scroll away). And `HostFileIconKey` keyed programs,
  libraries and shortcuts per *file* rather than per type, so ninety DLLs
  held ninety copies of one 256-pixel picture - three times the budget.
  Every backend resolves the host's icon from the name alone (the shell with
  `SHGFI_USEFILEATTRIBUTES`, the desktops by MIME type or UTType), so a
  `.dll` keys as `ext:dll` like any other type now; a program's own icon is
  still the native module's answer and is unaffected.
- **On Windows a host icon is held at the size it is drawn.** The shell's
  jumbo list answers 256 pixels a side whatever was asked, so a medium tile
  asking for 128 held a quarter-megabyte pixmap and drew it at a quarter of
  the area. The Windows backend resamples the icon down to the size asked
  for (never up), which holds four times as many types in the same cache.
- **A stale Windows version resource fails CI.** `UltraTexter.rc` /
  `.manifest` and `UltraFiler.rc` / `.manifest` carry a literal copy of the
  version that `set-version.sh` writes, and the configure-time warning for
  one that fell behind its changelog was not enough: UltraFiler 1.72.4
  shipped with a 1.72.3 version resource. `scripts/check_changelog.py`
  compares them now and `changelog.yml` runs on a change to them.
