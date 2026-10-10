- **Files dragged to another file display of the same window land where they
  are released.** A drag that leaves an `UltraCanvasFilerWidget` keeps going
  over the rest of the window, and a release over a second file display - the
  other pane of UltraFiler's split view - handed it the files as a `Drop`
  event. The display copied them into the folder it showed, whatever was
  under the pointer: files released on a folder there were copied, out of
  sight, into the wrong folder, without the question a move asks, and the
  user saw nothing happen. The second display now takes them directly: into
  the folder under the pointer, or over its empty space into the folder it
  shows, moved or copied by the same rule as a drop on a folder of the display
  the drag started in (`SetDropOnFolderCopies`, Ctrl copies, Shift moves), and
  after the question its `SetDropConfirmation` asks for that verb. A drop from
  another application onto a folder tile now goes into that folder too
  (still a copy). Files on a remote drive go up to the folder under the
  pointer, and a drive's entries dropped on a local folder come down into it.
- **A drag over a file display shows where it would land.** While a drag that
  started elsewhere - another display of the window, or another program - is
  over the display, the folder under the pointer is framed like the drop target
  of the display's own drag, and over empty space the display itself is
  framed, its folder being where the drop would go. New
  `UltraCanvasFilerWidget::GetIncomingDropFolder()` names that folder (empty
  while no such drag is over the display).
- **Elements of the window are told about a file drag passing over them.**
  The display a drag started in now sends the element under the cursor
  `DragEnter`, `DragOver` (with the dragged paths in `droppedFiles`) and
  `DragLeave`, as a drag from another program does, so a folder tree
  highlights the row a drop would land on while the drag is still in the air.
  The release still reaches it as a `Drop` event.
- New `FilerPaneDragTest` (Xvfb): two displays side by side, a file dragged
  from one onto a folder of the other, onto its empty space, with Ctrl, and
  under the move confirmation (asked, cancelled, answered with Move); and an
  element the drag passes over is told it entered, was passed over and left.
