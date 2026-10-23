- **Converting a CorelDRAW file to SVG no longer risks the SVG already
  there.** `UltraCanvasCDRPlugin::ExportToSVG` truncated each target page
  file before writing it, so a conversion that failed partway - a full disk,
  a network share that went away - left an empty or cut-short file in place
  of the one it replaced. Each page is now written through
  `WriteFileAtomically`: beside the target first, moved over it only once
  complete; a failure leaves the old file and no temporary one behind. The
  error says why (`FileWriteFailureReason`: the folder does not exist, the
  file is read-only, access is denied, the disk is full, or the system's
  error) instead of only "Cannot write file: <path>". `CDRPluginRenderTest`
  covers exporting over a file and into a missing folder. The EPS plugin was
  checked too: it only reads, so it has nothing to change.
