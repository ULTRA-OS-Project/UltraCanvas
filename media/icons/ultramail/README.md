# UltraMail icons

The interface icons of `Apps/UltraMail`, loaded by name through
`Theme::MailIcon` in `Apps/UltraMail/ui/UltraMailTheme.h`. All are drawn on
a 16 px grid.

- **Buttons and tools**: one colour, a 1.5 px stroke with round caps and
  joins. A button draws them as a mask in its text colour
  (`Theme::SetButtonIcon`, `Theme::MakeToolButton`), so the colour in the
  file does not matter.
  - Formatting bar: `bold`, `italic`, `underline`, `strikethrough`,
    `text-colour`, `align-left`, `align-centre`, `align-right`,
    `list-bullets`, `list-numbers`, `quote-in`, `quote-out`, `rule`, `link`,
    `picture`; the compose window's mode switch: `plain-text`, `formatted`.
  - Actions: `send`, `attach`, `cloud`, `check` (save, confirm),
    `arrow-right` (continue), `trash`, `edit`, `code` (HTML source),
    `info`, `unlock`, `user-add`, `group-add`, `signature`, `globe`
    (browser sign-in).
- **Dialog headers** (`Theme::MakeDialogHeader`, drawn tinted in the accent
  colour): `account-add`, `envelope`, `lock`, `globe`, `server`,
  `hourglass`, `signature`.
- **Settings tree** (`section-mail`, `section-reading`, `section-privacy`,
  `section-display`, `section-warnings`): one muted grey, drawn as they are.

The packaged app icon is `media/appicon/UltraMail.png`, not here.
