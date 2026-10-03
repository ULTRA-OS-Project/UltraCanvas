- **A link's address as a tooltip, by choice.**
  - `UltraCanvasLabel::SetShowLinkTooltips(bool)`: while the pointer is on a
    text link, the label shows that link's href in a tooltip and hides it as
    the pointer leaves the link (off by default).
  - `HTML::BuildOptions::linkTooltips` (default on): text links and linked
    pictures show their href as a tooltip. An app that shows the address
    elsewhere - a status line fed by `onLinkHovered` - turns it off. Before,
    linked pictures always had the tooltip and text links never did.
