- **UltraCanvasMediaViewer shows presentations.** A `.pptx` (and `.pptm`,
  `.ppsx`, `.ppsm`, `.potx`, `.potm`) or an `.odp` (and `.otp`, `.fodp`) opens
  in the read-only text view as the outline of its slides, read by
  `UltraCanvasFileLoader::LoadTextDocument` and rendered as Markdown, the way
  a `.tex` is (`IsPresentationFile`, classified before text so a flat `.fodp`
  is not shown as XML). The info bar says *PRESENTATION*. A
  password-protected deck is locked like a PDF (`MediaLock::Presentation`):
  the note, **Unlock**, a wrong password told from a deck that will not open,
  the password kept for the folder. `MediaViewerToolsTest` walks a plain
  `.pptx`, `.odp` and `.fodp`, and a protected `.pptx` and `.odp`.
- **Markdown view: a backslash escape inside bold, italic or strike-through
  is resolved**, as outside them (`UltraCanvasTextArea`'s hybrid Markdown):
  `*\[Picture: chart]*`, which `UCRichDocument::ToMarkdown` writes for an
  italic "[Picture: chart]", showed its backslash; and an escaped marker no
  longer closes the run. Inline code keeps its backslashes. `InlineMathTest`
  checks the runs and the cursor map.
