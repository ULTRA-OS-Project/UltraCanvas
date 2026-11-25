- **No more crash while thumbnails are made.** UltraFiler could crash ("exception
  0xC0000005 ... libcairo-2.dll") in a folder with documents or slides, as
  their thumbnails were drawn while the window drew its own text. The text of
  each thread is now kept apart (framework: text layouts per thread).
