- **A second start's files go to the running copy:
  `UltraCanvasSingleInstance::MakeOpenRequest` / `ListenOpen`.** The document
  applications - Texter, UltraFiler, UltraViewer, UltraPaint, ArtCreator,
  UltraVideo and AnchorPoint - now run one copy per user too: opening a file
  from the file manager while one of them runs opens it in that copy, the way
  the application opens a file itself, instead of starting another. The paths
  are made absolute in the starting process, since the running copy was
  started from another folder; a URL passes through unchanged. See
  `Docs/UltraCanvas/UltraCanvasSingleInstance.md`.
