- **An application's preferences in one file: `UltraCanvasAppSettings`.**
  `<settings folder>/<App>/settings.json`, read with `Load()` and written
  whole with `Save()`; typed values (`GetString`, `GetInt`, `GetBool`,
  `GetNumber`, `GetList`, `Set`, `Remove`) with a fallback each, dotted
  keys as nested objects (`"window.width"`), and a recent list on any key
  (`Remember` puts an entry first, once, and keeps the newest eight;
  `Forget` drops one). A missing or broken file is the defaults. UltraPress's
  settings file is one; UltraFIBU's list of the bookkeepings opened last
  moved into one. `AppSettingsTest` covers it
  ([doc](UltraCanvasAppSettings.md)).
