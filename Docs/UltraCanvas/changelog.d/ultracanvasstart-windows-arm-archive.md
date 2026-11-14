- **The UltraCanvasStart archive is no longer lost on the Windows ARM leg.**
  `scripts/package-ultracanvasstart.sh` collected the application's DLLs
  with a bash loop of `objdump | sed | tr` through a process substitution -
  five processes per DLL, some five hundred for the 132 it loads - and on
  the Windows ARM runner, where MSYS2 runs x86-64-emulated, that loop once
  ended eight seconds in, silently, with status 0 (2026-11-13, #1235): no
  count, no archive, the step green, and the upload an hour later the one
  that failed. The closure is now read by `scripts/pe_imports.py`, a PE
  import-table reader in Python that handles x86, x86-64 and ARM64 images in
  one process (tested against synthetic PE32 and PE32+ images in
  `scripts/tests/test_pe_imports.py`); the objdump loop stays for a shell
  with no Python, and a closure of zero DLLs is an error. The packaging step
  in `build.yml` checks for the archive itself, runs the script once more
  when it is missing, and fails there with the script's name on a second
  miss, instead of at the upload.
