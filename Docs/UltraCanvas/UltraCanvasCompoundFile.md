# UltraCanvasCompoundFile — OLE2 compound files

<!-- doc-check: std::string path; -->

`UltraCanvasCompoundFile.h` reads the container the legacy Microsoft Office
formats are stored in: Word 97-2003 (`.doc`), Excel 5.0-2003 (`.xls`),
PowerPoint (`.ppt`), Outlook (`.msg`). A compound file ("structured storage",
[MS-CFB]) is a small file system in one file: a FAT of fixed-size sectors
holding named streams, with small streams kept in a finer-grained mini stream.

```cpp
#include "UltraCanvasCompoundFile.h"

if (UltraCanvas::UCCompoundFileReader::HasSignature(path)) {   // reads 8 bytes
    UltraCanvas::UCCompoundFileReader file;
    if (file.Open(path)) {
        std::vector<uint8_t> workbook;
        if (file.ReadStream("Workbook", workbook)) {
            // the stream's bytes
        }
        for (const std::string& name : file.StreamNames()) {
            (void)name;   // the streams in the root storage, UTF-8
        }
    } else {
        std::string why = file.GetLastError();   // "Not an OLE2 compound file", ...
    }
}
```

`Open` takes a UTF-8 path on every platform; `OpenFromMemory` takes the bytes
of a container already in hand. Names compare without regard to ASCII case, as
the format specifies. `ReadStream` and `HasStream` look in the root storage;
when a writer left the directory tree empty (or it is damaged), the first
stream of that name anywhere is taken instead.

## Implementation notes

- Version 3 (512-byte sectors) and version 4 (4096-byte sectors, 64-bit stream
  sizes) are both read; the FAT is gathered from the header's 109 entries and
  any further DIFAT sectors.
- Every chain is followed with a length guard, so a FAT that loops ends the
  read instead of hanging it, and every sector offset is checked against the
  file size.
- The root's children are found by walking the directory's red-black tree
  with a visited set, which keeps a stream of an embedded object (an Excel
  chart inside a Word file has a `Workbook` of its own) from being taken for
  the document's.
- The whole file is read into memory: these containers are documents, not
  archives.

## Where it is used

The `.doc` importer (`Plugins/Documents/Word/UltraCanvasDocLegacyFormat.cpp`)
and the [`.xls` reader](UltraCanvasSpreadsheetXls.md).

## Test

Covered by `Tests/SpreadsheetXlsFileTest.cpp` (an `.xls`, a `.doc`, and a file
that is no compound file) and, through the `.doc` importer, by
`Tests/WordFormatsTest.cpp`.
