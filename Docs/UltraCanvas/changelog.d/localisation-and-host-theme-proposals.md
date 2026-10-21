- **Two design documents for the next framework-wide jobs.**
  `Docs/UltraCanvas/UltraCanvasLocalisationProposal.md` proposes the
  localisation system: `Tr` / `TrC` / `TrN` / `TrF` with the English text
  as the key, one JSON catalogue per language and domain under
  `media/locale/<lang>/<Domain>.json` with a per-user override under the
  UltraCanvas settings folder, the language chosen once for every
  application in UOS-Settings (`Language.conf`, the system's language by
  default), locale number, date and file-size formatting, and the
  extraction, check, wrapping and UltraAI-draft scripts that make the 30
  applications' migration mechanical.
  `Docs/UltraCanvas/UltraCanvasHostThemeProposal.md` proposes following the
  host's look: `HostAppearance` read from the Windows registry, AppKit,
  XSETTINGS / GSettings / kdeglobals, the browser's media queries and
  Android's configuration, with the change events each platform sends;
  fonts and text sizes in `UltraCanvasTheme` by text role; `FromHost` with
  the accent derived under the contrast rules the theme test enforces; a
  theme policy on the application that follows the host by default;
  `FromTheme` for the 42 style structs that lack one; `Appearance.conf`
  and a UOS-Settings Appearance > Look page; and a watcher on the settings
  folder that makes those shared settings live in running applications.
  Both are recorded in `scripts/doc_examples_baseline.txt` until their
  APIs exist.
