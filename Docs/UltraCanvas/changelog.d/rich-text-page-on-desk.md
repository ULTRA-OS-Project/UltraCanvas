- **A white page on a grey desk.** In page view the light theme drew the page
  in the well's light grey on the panel's white - the paper darker than the
  desk around it. `RichTextEditStyle::FromTheme` now takes the lighter of the
  well and the panel for the page (white in the light theme) and the panel a
  shade darker for the desk (a mid-light grey; darker still in a dark
  theme), so the page stands out as paper does. A theme can set either
  through the element roles `rich-text.page` and the new `rich-text.desk`.
