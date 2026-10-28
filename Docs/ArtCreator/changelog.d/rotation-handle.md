- **A rotation handle.** The Selector's Rotate handles (a second click on the
  selection) now show where the selection turns about and how far it is
  turned: a handle starts at the rotation centre and points along the
  object's angle. Drag its knob to turn the selection; Ctrl holds it to 15°
  angles. The *Handles* section of the Selector's options chooses its look -
  a **dial** of degree ticks round the centre (the default) or a **stem** to
  just beyond the frame - and keeps it (`handles.rotation` in `config.ini`).
- **One object's own turned frame.** In Rotate mode a single object's frame
  and handles turn with it instead of standing upright around it. While the
  selection turns, the frame and the other handles disappear and only the
  object, the centre and the rotation handle show; the status line reads the
  angle and the turn so far.
- **The rotation centre in the transform bar.** While the Rotate handles show,
  the bar's right end holds the centre's X and Y and a reference point picker:
  its nine points put the centre on a corner, the middle of an edge or the
  middle of the frame, and a centre dragged anywhere else shows as a ring
  pointing its way. The button in front of the fields switches them between
  the centre's place on the page and how far it lies from the middle of the
  selection (`transform.centre` in `config.ini`). Typed rotation and shear
  turn about that centre while the Rotate handles show.
- **The skew handles lean along the object's own edges.** The diamond
  handles of the Rotate frame used to skew along the page's horizontal and
  vertical, whatever the object's angle. Now the top and bottom handles slide
  along the object's top edge and the side handles along its sides, about the
  rotation centre, and the dragged handle stays under the pointer.
- A moved rotation centre now goes with the selection when it is dragged or
  moved from the bar, and stays after a typed change to the same objects.
- Every field of the transform bar is one width, top and bottom row, and a
  held arrow steps the value again and again - 1° or 1 mm (1 px in icon mode)
  at a time - as one undo step.
- **An object's size includes its line.** The selection's box ran down the
  middle of a line, so a thick line stood out past the handles, and the
  transform bar's W and H, the handles' scaling, Align and Distribute measured
  the object without it. They now measure what is drawn: a 160 x 90 pt
  rectangle with a 14 pt line is 174 x 104 pt.
- **The marquee measures lines by their width.** Dragging a box round a thick
  line used to select it as soon as the box held the line's middle, even when
  it cut through the line itself; it now has to hold all of the line, as it
  is drawn.
- **Copying a selection as a picture keeps wide lines and far shadows.** A
  line much wider than its object, or a shadow cast far from it, was cut off
  at the picture's edge (framework: `RasterizeVectorElements`).
