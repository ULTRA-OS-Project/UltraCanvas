# UltraCanvasPropertyList — Apple property lists

`UltraCanvasPropertyList.h` reads the two encodings a property list comes in:
the XML form, and the binary `bplist00` form that most shipped `Info.plist`
files actually use. No Apple API, so it reads the same wherever the disk is
mounted.

**It is deliberately not a general plist library.** What the framework needs
from a plist is a handful of named values out of its top-level dictionary —
the name, icon and executable of an application bundle, the address in a
`.webloc` — so that is what it offers: the top level, flattened to text.
Nested dictionaries and arrays are *skipped* rather than half-modelled, and a
caller that needs them should say so rather than work around it.

```cpp
#include "UltraCanvasPropertyList.h"

UCPropertyList plist;
if (UltraCanvas::UCPropertyList::Read(path, plist)) {
    plist.GetString("CFBundleName");
    plist.GetString("CFBundleVersion", "1.0");   // with a fallback
    plist.GetBool("LSUIElement");
    plist.GetInteger("NSAppTransportSecurity");
    plist.Values();                              // everything, as text
}
```

`Read` returns false for a file that is neither encoding, so a file that
merely ends in `.plist` is never mistaken for one. `ReadBytes` does the same
for bytes already in hand — a plist inside an archive, or one fetched over
the network.

Values are stored as text: strings as they stand, integers in decimal,
booleans as `"true"` / `"false"`. `GetBool` also accepts `1` and `YES`, which
older files use.

## Implementation notes

- The **XML** form goes through the tinyxml2 the framework already carries.
- The **binary** form is parsed here: a 32-byte trailer says how wide the
  offsets and object references are, where the offset table is and which
  object is the root; the root must be a dictionary, and its keys and values
  are resolved through that table. ASCII and UTF-16BE strings, integers,
  reals and booleans are read; data, dates and UIDs are left alone.
- Every offset in a binary plist comes out of the file itself, so all of them
  are bounds-checked: a malformed file produces no values, never a read past
  the end.

## Where it is used

[`UltraCanvasMacBundle`](UltraCanvasMacBundle.md), for application bundles and
web locations.

## Test

Covered by `Tests/MacBundleTest.cpp`, which builds an XML plist and a binary
one byte by byte (including the long-string length form) and reads both back.
