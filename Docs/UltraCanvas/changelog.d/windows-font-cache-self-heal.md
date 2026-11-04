- **Windows: the system fonts stayed missing behind an empty font cache.**
  fontconfig trusts its cache of a folder as long as the folder's time stamp
  is unchanged, and `C:\Windows\Fonts` seldom changes: a cache that once came
  out empty - a scan that failed, another program's fontconfig writing to the
  same cache folder - kept every later start at the bundled fonts, with the
  folder named in the configuration. The background scan now counts the fonts
  it has from the Windows font folder; with none while the folder holds font
  files, it reads the folder again past its cache, and as a last resort adds
  its files one by one. An error in one element of the scan's configuration no
  longer discards the rest of it. The debug log gives the counts at each step.
