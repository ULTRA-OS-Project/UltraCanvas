2026-04-16 1.0.19
- UpdateGeometry for visible childs in container only
- 
2026-04-16 1.0.18
- Revert back to the Sans font for Windows insetad of detecting default font. It detected the "Segoe UI" and this shit font can't be vertically centerted without special patches especially for that font, it always shifted down a little (even in browsers)
- Fixed bug with high CPU usage and slow cursor movement on the big files with very long lines.

2026-04-16 1.0.17
- Add on-the-fly submenu regeneration to UltraCanvasMenu
- Fix Windows high CPU usage when app is idle.
- Fix main-row digit/punctuation key mappings on Linux and macOS
- Fix vertical text position issues in Windows (was shifted down a little)

2026-04-16 1.0.16
- Full rework of display and rendering the text. Use Pango layout to format text. Allow to use variable height lines

2026-04-06 1.0.15
- Add platform-native system font detection, replace hardcoded "Sans" defaults

