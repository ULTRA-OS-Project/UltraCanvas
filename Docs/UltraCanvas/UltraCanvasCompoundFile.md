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

Below the root, a path names a stream: storages and the stream separated by
`/`, each name compared the same way. An Outlook `.msg` keeps every
attachment and recipient in a storage of its own:

```cpp
UltraCanvas::UCCompoundFileReader msg;
if (msg.Open(path)) {
    for (const std::string& storage : msg.StorageNamesAt("")) {   // "__attach_version1.0_#00000000", ...
        std::vector<uint8_t> data;
        if (msg.ReadStreamAt(storage + "/__substg1.0_37010102", data)) {
            // an attachment's bytes
        }
        std::vector<std::string> streams = msg.StreamNamesAt(storage);
        bool named = msg.HasStreamAt(storage + "/__substg1.0_3707001F");
        (void)streams; (void)named;
    }
}
```

## Writing one

`UCCompoundFileWriter` builds a compound file in memory - what the
[password-protected `.xlsx` / `.docx` writer](UltraCanvasOfficeEncryption.md)
puts its `EncryptionInfo`, `EncryptedPackage` and `\x06DataSpaces` streams in:

<!-- doc-check: std::vector<uint8_t> info; std::vector<uint8_t> package; std::vector<uint8_t> primary; -->
```cpp
#include "UltraCanvasCompoundFile.h"

UltraCanvas::UCCompoundFileWriter writer;
writer.AddStream("EncryptionInfo", info);
writer.AddStream("EncryptedPackage", package);
// Storages are named on the way, separated by '/'.
writer.AddStream("\x06" "DataSpaces/TransformInfo/StrongEncryptionTransform/\x06" "Primary", primary);
std::vector<uint8_t> file = writer.Build();
```

`AddStream` refuses an empty name, one over 31 characters or not ASCII, and a
stream where a storage of that name already is (or the other way round);
adding a path again replaces the stream. `Build` writes version 3: 512-byte
sectors, every stream under 4096 bytes in the mini stream as the format
requires, a FAT of any size (DIFAT sectors past the header's 109 entries), and
each storage's children in a balanced tree coloured as a valid red-black tree
(the deepest level red when it is not full), ordered as [MS-CFB] 2.6.4 orders
names - which Office checks.

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

The reader: the `.doc` importer
(`Plugins/Documents/Word/UltraCanvasDocLegacyFormat.cpp`), the
[`.xls` reader](UltraCanvasSpreadsheetXls.md), and the decryptor of
[password-protected `.xlsx` and `.docx` files](UltraCanvasOfficeEncryption.md),
which reads their `EncryptionInfo` and `EncryptedPackage` streams, and the
[Outlook `.msg` reader](UltraCanvasOutlookMessage.md), which reads by path below
the root. The writer:
the encryptor of those files, which builds them.

## Test

The reader is covered by `Tests/SpreadsheetXlsFileTest.cpp` (an `.xls`, a
`.doc`, and a file that is no compound file) and, through the `.doc` importer,
by `Tests/WordFormatsTest.cpp`. The writer by `Tests/OfficeEncryptionTest.cpp`:
what it builds read back, streams in the mini stream and in sectors, nested
storages, and a 9 MB file that needs DIFAT sectors; Python's olefile and
msoffcrypto-tool opened its output when it was written. The paths below the
root by `Tests/OutlookMessageTest.cpp`.
