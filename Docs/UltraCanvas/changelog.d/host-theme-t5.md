- **Appearance.conf and the settings watcher (T5 of the host theme
  proposal).** `AppearanceSettings` (`UltraCanvasAppearanceSettings.h`) is
  a person's choice over the desktop for every UltraCanvas application at
  once - `look`, `accent`, `font`, `fontsize`, `scale`, `contrast`, each
  `system` when the host decides - in `Appearance.conf` in the settings
  folder, the `Emoji.conf` pattern (`Parse`, `Serialize`, `ApplyTo`, `Load`,
  `Save`). The application applies it over the host on every read, before
  `ULTRACANVAS_HOST_APPEARANCE`; the source gains `+settings`.
  `UltraCanvasSettingsWatcher` (`UltraCanvasSettingsWatcher.h`), owned by the
  application base, watches the settings folder - natively where the build
  has a backend, a five-second poll otherwise - and a change to
  `Appearance.conf` re-reads the host, so the theme, the elements that
  follow and the host appearance handlers hear it as they hear a change of
  the desktop. UOS-Settings > Appearance > Look writes the file; DemoApp's
  Modules > Theme page shows the roles, the policy and the host, the fonts
  by role, elements following by default and the file. `HostAppearanceTest`
  covers the file and `SettingsWatcherTest` the watcher; `UltraCanvasTheme.md`
  1.14.0 (*The setting*), `UltraCanvasHostAppearance.md`.
