- **A Markdown view's links can go without the underline.**
  `MarkdownHybridStyle::linkUnderline` existed but the renderer underlined
  every link regardless; it is honoured now, so a view whose links are file
  names and addresses full of hyphens and underscores (UltraCanvasStart's
  guides) can mark them with the link colour and the hand cursor alone. The
  default stays underlined.
