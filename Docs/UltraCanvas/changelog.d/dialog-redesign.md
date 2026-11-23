- **Alerts, confirmations and input dialogs have a new look.** Before, the
  internal `UltraCanvasModalDialog` showed a bright square with a letter, its
  buttons centred and alike, and the button row on the message's white. Now:
  - the type badge is a 40 px disc tinted with the type colour, with a drawn
    glyph (`media/icons/dialog/*.svg`) in the full colour; the letter stands
    in when the glyph is not shipped;
  - the type colours follow the theme: Information the accent, Error the
    danger colour, Successful green, Question violet, Warning amber, lighter in
    a dark theme, and a theme can change any with the `dialog.*` roles;
  - an optional bold heading above the message: `DialogConfig::heading`,
    `UltraCanvasModalDialog::SetHeading` / `GetHeading`, and
    `AlertOptions::heading`;
  - the buttons sit at the trailing edge with the affirmative answer last
    (*Cancel OK*, *No Cancel Yes*), the default answer in the accent colour
    even while it is disabled, 32 px high with 7 px corners;
  - the button band is the theme's ground under a hairline.
  - `ModalDialogStyle` gains `iconGlyphSize`, `headingFontSize`,
    `headingTextColor`, `buttonCornerRadius`, `footerColor` and
    `footerLineColor`; its padding, icon, gap and button sizes change to the
    new design (`FromTheme` fills the new colours and the heading size).
  - The DemoApp's Rich alert shows a heading.
