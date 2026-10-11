- **A menu opened for a widget never covers it.** New
  `UltraCanvasMenu::OpenMenuAnchored(anchor, window, settings, gap,
  align)` takes the widget's bounds in the window - a button, a chip, a
  field - and opens the menu `gap` pixels below it, or, when it does not fit
  below and does above, ending `gap` pixels above it, as GTK's popovers and
  Windows' `TrackPopupMenuEx` with an exclude rectangle place theirs. The
  menu measures itself, so a caller no longer works out its height to open
  it upwards (UltraMail's attachment chips did, counted two rows and missed
  the panel's padding and border, and the menu covered the top of the chip).
  New `MenuAnchorAlign::Right` lines the menu's right edge up with the
  widget's. `OpenMenu` at a point behaves as before: below the point, or
  ending at it.
  - A menubar's drop-downs are anchored to their item: a bar at the foot of
    a window opens them upwards instead of over itself.
  - Anchored now: the breadcrumb's drop-downs and overflow list, the
    tabbed container's overflow list, the spinner's value list, the video
    recorder's camera list and the DemoApp's Dark and Flat menu buttons.
- **Menu icons follow the theme.** Menu icons are mostly line glyphs drawn in
  black or grey ("symbolic" icons, like those in `media/icons/menu`), which
  vanished on a dark menu. With the new `MenuStyle::iconTint` at its default,
  `MenuIconTint::Symbolic`, an icon drawn in one shade of grey is painted
  in the entry's text colour, as its label is - light on a dark menu, the
  hover text colour while hovered, the disabled colour when disabled - so one
  set of icons serves both themes. A coloured icon (an application's, a file
  type's) keeps its colours, and so does one in black and white (Texter's
  half-filled theme-switch disc). `MenuIconTint::Original` draws every icon as it
  is; `MenuIconTint::AllIcons` tints coloured ones too. `MenuStyle::tintIcons`
  (framework changelog: `menu-flat-icons`) is the same as `AllIcons`, and
  `Dark()` no longer turns it on: its grey icons take the light text colour
  by the symbolic default, and an application's coloured icon in a dark
  menu - UltraFiler's *Open with* - keeps its colours.
- **Closing all of a menu's submenus no longer frees one mid-close.**
  `UltraCanvasMenu::CloseAllSubmenus()` - run when a popup menu closes for
  Escape or by hand, and on a menubar - walked its list of open submenus
  while closing each, and a closing submenu erases itself from that list and
  drops the parent's reference to it: the loop ran on over an invalidated
  list and the submenu was freed inside its own `CloseMenu()`. It closes a
  copy of the list now.
- `MenuAndTabBehaviourTest` checks an anchored menu below its widget, above
  it with no room below (ending the gap above it), lined up by its right
  edge, a point-opened menu ending at its point, a bottom menubar opening
  upwards and closing it again, and on a dark menu a black icon painted light, a red one kept,
  a disabled one greyed, a black-and-white one kept and
  `MenuIconTint::Original` leaving black black.
