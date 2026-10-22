- **A save that fails says why.** The vector writers (SVG, PDF, EPS, AI, CDR,
  XAR), the layered-image project file and the `.ucd` writer reported only
  "Failed to create SVG file: <path>", "Could not create <path>" or "Could not
  write <path>", so ArtCreator and UltraPaint showed that and nothing more.
  They now give the reason: the folder does not exist, a folder has the
  file's name, the file is marked read-only, access is denied (locked by
  another program, or no permission), the location is read-only, the disk is
  full, or else the system's own error text and code.
- **Saving a drawing no longer risks the one already there.** The vector
  writers truncated the target before the first byte of the new drawing
  existed, so a save that failed partway - a full disk, a network share that
  went away, a codec error - left the file empty or cut short, and the
  drawing it held was gone. They now write through `WriteFileAtomically`: the
  new file is written beside the old one and replaces it only once it is
  complete, and a save that fails leaves the old file and no temporary one
  behind. They also check that the write finished; a disk that filled up
  partway used to pass as saved. `SVGConverterTest` covers saving over a
  file, a missing folder and a folder in the way.
  - `UltraCanvasFileError.h` 1.2.0: `FileWriteFailureReason(path, errno)` is
    `DescribeFileWriteError`'s answer, else the system error the failing call
    left, for a write that failed although the file still opens;
    `DescribeFileWriteError` names a file marked read-only.
  - `UltraCanvasFileLoader::SaveVectorDocument` reports the destination's
    reason when the writer gave none.
