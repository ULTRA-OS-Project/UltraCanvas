- **Filer widget: the hidden-items notice sits in the file area.** "45 items
  are hidden here" and its blue **Show hidden files** button are now one
  centred line on the display's own background, instead of a grey strip that
  read as one more status bar under the files.
- **Filer widget: `onHiddenNoticeSettings`.** Set it and a gear button sits
  right of **Show hidden files**; a click runs the callback, so a host can open
  the settings that decide what the display holds back.
- **Filer widget: `FormatSizeText(bytes)`.** The display's own size
  formatting (`103.6 KB`), public and static, so a host's status bar rounds a
  selection the same way as the info bar under the files.
- **Filer widget: a host can show the info bar's line in its own status
  bar.** `GetSelectionInfo(primary, secondary)` returns the line whether or
  not the bar is up, `onSelectionInfoChanged` fires (posted) when it changes -
  including when background folder sizes or media details arrive - and
  `SetInfoBarMenuItemVisible(false)` drops the Display > Info-Bar switch.
