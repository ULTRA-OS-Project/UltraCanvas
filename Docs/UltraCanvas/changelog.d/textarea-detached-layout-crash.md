- **The demo's Media Viewer page crashed (ACCESS_VIOLATION on Windows,
  SIGSEGV on Linux).** The viewer fills its Details panel, a Markdown
  `UltraCanvasTextArea`, and scrolls it to the top on every file it loads:
  when the page opens and on every click or arrow key that browses the
  folder. When the viewer is not in a window yet, as the demo does it
  (`OpenFolder` before `AddChild`), the text area has no render context, and
  `ScrollTo` built the line layouts anyway, through a null context.
  `GetActualLineLayout` now builds layouts only when there is a context and
  otherwise returns no layout, which every caller already handles; the first
  `Render` lays the text out. The same function read one element past the end
  of the layout cache for an index equal to the line count; it no longer
  does. New `TextAreaDetachedTest` scrolls a detached Markdown and plain text
  area, then checks both lay out once attached.
