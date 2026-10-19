- **Save dialog: dragging the file icon onto a folder in UltraFiler now sets
  the save folder.** The icon's drag stopped at the dialog's border, so a drop
  on UltraFiler did nothing. Out of the dialog it now carries on as a native
  save drag (`UltraCanvasWindowBase::StartNativeSaveDrag`) that asks the
  window it lands on for a folder. On Windows a private clipboard format is
  answered through `IDataObject::SetData`, and on X11 the drag uses XDS
  (`XdndDirectSave0`). The dialog then opens that folder, and the path, the
  listing and the folder tree all follow. On the receiving side an
  UltraCanvas window delivers the drop at once
  (`UltraCanvasWindowBase::DeliverSaveDrop`, `UCEvent::dropFolderReply`). A
  file display names the folder under the pointer, and a tree names the
  folder of the row (`UltraCanvasTreeView::onDropFolderOfNode`).
  `UltraCanvasFileDragIcon::onDroppedOnFolder` reports the result. macOS has
  no native drag source yet, so there the drag still stays in the dialog.
- **The file dialog shows its folder as a breadcrumb, as UltraFiler does.**
  The path text field is gone. A leading *Computer* lists the drives, and
  each folder segment opens that folder or, from its drop-down, a folder
  inside it. Hovering shows the whole path, and a path is typed in the name
  field.
- **The Save dialog shows the picture's size under the file type.** When
  the caller gives the picture's size, a bitmap type shows the pixels and
  the proportions, for example `640 × 480 pixels (4:3)`. The pixels follow a
  size chosen in Settings. A vector type shows the proportions alone,
  `(4:3)`. Proportions that don't reduce to short numbers are written as a
  decimal, `(1.78:1)`.
