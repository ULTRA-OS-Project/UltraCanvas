- **Appearance.conf: a second choice made quickly no longer fails to save
  (Windows).** `AppearanceSettings::Save` writes a temporary file and renames
  it over Appearance.conf. Windows refuses that rename while another program
  has the file open, and every running application opens it the moment it
  changes (the settings watcher), as do the virus scanner and the search
  indexer. So switching light / dark a second time soon after the first could
  end in UOS-Settings' "Could not write ...\Appearance.conf." and no
  application followed. The rename is now tried again for up to half a
  second, then the file is written in place. Running applications already
  follow a saved change at once (native folder watch on Windows and Linux).
