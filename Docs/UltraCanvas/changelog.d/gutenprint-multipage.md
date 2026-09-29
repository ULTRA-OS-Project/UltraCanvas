- **GutenPrint printed only the first page of every multi-page job.** The CUPS
  raster writer that feeds GutenPrint's filter put its `RaS3` sync word before
  every page, where the format has it once, at the start of the stream. The
  filter read the second one as the start of page 2's header, found every
  field four bytes out of step, and stopped there - with exit status 0 and no
  message, so the job reported success. Checked against GutenPrint's own
  `rastertogutenprint`: one page printed from a three-page job before the
  fix, three after.
  - The sync word is its own call now, `AppendCupsRasterSync`, made once per
    stream; `WriteCupsRasterPageHeader` became `AppendCupsRasterPageHeader`
    and writes the header alone. Renamed rather than quietly changed, so a
    caller still expecting the sync word fails to compile instead of
    printing garbage.
  - `Tests/IODevicePrinterTest` now builds a three-page stream and reads it
    back the way the filter does. Nothing tested the writer before.
- **The CUPS backend's comment on device ids described something that never
  happened.** It said the printer's UUID made the same printer found by the
  IPP backend collapse into one entry; `cupsGetDests2` does not return
  `printer-uuid`, so ids are `cups:<queue>` and the IPP backend avoids the
  double listing from its side. The comment now says so.
