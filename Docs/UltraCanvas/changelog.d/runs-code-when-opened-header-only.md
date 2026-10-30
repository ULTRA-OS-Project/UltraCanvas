- **`FileAssociations::RunsCodeWhenOpened` is header-only**
  (`UltraCanvasRunsCodeWhenOpened.h`, included by
  `UltraCanvasFileAssociations.h`, so callers change nothing): a headless
  module that does not link the UltraCanvas library asks the same question
  as the UI. UltraMail's threat scanner now does.
