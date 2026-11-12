- **Two long texts that began alike were drawn as one.** The render context's
  text-layout cache keyed a layout on the first 300 characters of its text
  (`RenderContextCairo::GenerateTextCacheKey`), so a second text that matched
  the first that far got the first one's layout - drawn and measured as the
  first. The key now carries the whole text, and a cache entry's charged size
  counts its text, so a long one weighs against the 20 MB budget by its length.
  `TextLayoutCacheTest` checks two texts alike in their first 320 characters.
- **Hardware information: three out-of-bounds or dangling accesses.**
  - **Windows, USB:** a device property was read up to a NUL that SetupAPI
    does not write when the value fills the buffer, so the read ran on past
    the stack buffer. It now stops at the bytes SetupAPI reports, a value
    longer than 512 characters is read again at its size instead of coming
    back empty, and a property that is not a string reads as none.
  - **Linux, `pci.ids` / `usb.ids`:** `LoadIdsFile` checked `strtoul`'s end
    pointer after the temporary substring it pointed into was destroyed
    (found by AddressSanitizer).
- **UltraMessage on Windows: a listener closing mid-accept no longer leaves
  the kernel writing into a dead stack frame.** `Listener::Accept` cancelled
  a pending `ConnectNamedPipe` and returned at once, while the cancellation
  still owned the `OVERLAPPED` on its stack; it now waits for the
  cancellation to complete, as reads and writes already did.
