- **A copy too large for one X request travels in pieces, both ways (X11).**
  ICCCM's INCR transfer: the owner answers with an `INCR` marker and writes
  the copy piece by piece, each once the requestor has deleted the one
  before, and an empty piece ends it. The X11 clipboard did neither half.
  A large picture copied in GIMP or a browser was read back as the marker's
  few bytes. A large copy made here went out in one request, which an X
  server refuses when the request exceeds its largest: 256 KB without the
  BIG-REQUESTS extension, 16 MB with it on Xvfb.
  - Reading follows the pieces to the end. Each piece has 3 seconds to
    arrive, and a copy may be up to 128 MB (up from 10 MB). A larger one is
    refused rather than cut short: one too large for a single property used
    to come back silently truncated.
  - A copy larger than 256 KB is served in 256 KB pieces, as GTK and Qt do.
    A requestor that stops taking pieces is given up on after 10 seconds.
    The application's event loop passes the requestor's property changes to
    the clipboard (`ProcessClipboardPropertyEvent`).
  - While it waits for an answer, the clipboard takes only its own events
    off the X queue. It used to discard every other event that arrived in
    the meantime - a key press, an expose, a window's message.
  - A late notice of losing the clipboard, handled after the clipboard was
    taken back, no longer clears the copy made since.
  - `Tests/ClipboardIncrTest.cpp` checks both directions against a second
    X connection written from the ICCCM, and against `xclip` when it is
    installed.
