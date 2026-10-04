- **DemoApp: a long page description is no longer cut off in the header.**
  The header showed each page's description on one line and cut it off with
  an ellipsis. 44 of the demo's pages have descriptions too long for the
  1400 px window, and Media Viewer's lost about three quarters of its text.
  The title now wraps, and the header grows to fit the lines and shrinks
  back on a page with a short description. The window's column measured the
  header against an "at most this wide" width, under which a flex row keeps
  its items at their one-line width and height, so the header now takes a
  definite 100 % width. The documentation and source buttons no longer
  shrink: on the longest descriptions they had been squashed to a few
  pixels.
- **DemoApp: the Media Viewer page shows its whole introduction.** The
  three-line introduction sat in a box two lines tall and lost its first and
  last lines, and the frame's caption read "Media Viewer wid…".
