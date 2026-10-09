- **CI: the MuPDF source download on Linux is retried.** The Linux install
  step fetched the archive from mupdf.com with one `curl | tar`, so on
  2026-10-09 one connection that timed out after 135 s failed the leg before
  a line was compiled. The download now has what the apt step has: a short
  connect timeout, a stall watchdog, curl's own retries and three attempts
  with a growing pause, and it goes to a file that is unpacked afterwards,
  so a truncated transfer never reaches tar.
