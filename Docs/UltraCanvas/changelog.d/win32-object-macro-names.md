- **`check_platform_names.py` knows `<windows.h>`'s object-like macros.** A
  new `win32-macro` rule flags a lambda, variable, member, enumerator or
  `#undef` named `near`, `far`, `NEAR`, `FAR`, `IN`, `OUT`, `OPTIONAL`,
  `interface`, `small`, `ERROR`, `DELETE` and the rest of the hand-kept list
  in `scripts/win32_object_macros.txt`: on Windows those names are rewritten
  ("expected unqualified-id"), and a spreadsheet chart helper named `near`
  broke both Windows builds that way. The A/W rule only knew function-like
  macros. Sixteen `near` / `far` identifiers are renamed (`closeTo`,
  `distant`) in the slide editor, the DXF reader, the requirement diagram,
  ArtCreator and nine tests; the `small` and `interface` sites, which build
  today because `WIN32_LEAN_AND_MEAN` keeps the COM and RPC headers out, are
  baselined.
