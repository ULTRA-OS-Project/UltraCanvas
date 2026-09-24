- **3D models on a comma-decimal desktop: FBX and DirectX .x did not load,
  PLY and DXF came out wrong.** Six model readers still parsed numbers with
  `atof` / `strtod`, which follow `LC_NUMERIC`. The Linux backend calls
  `setlocale(LC_ALL, "")` for keyboard input, so on a German, French or
  Italian desktop the '.' in "1.5" was not a decimal point. The Filer's 3D
  thumbnails and detail view, like every other viewer, showed the text FBX
  and the .x samples as nothing at all, and the PLY and DXF samples with
  their geometry scrambled. The same defect was fixed for OBJ and X3D in
  0.9.42; the six readers it did not reach are fixed here:
  - PLY (`AsciiTokens`), DXF 3D (`Tag::Number`), COLLADA (`ReadFloatChild`)
    and STEP (`UltraCanvasStepFile`) read through `TryParseFloat`; the FBX
    and DirectX .x tokenizers through `ParseFloatClassic`, which for .x
    also bounds the scan by the buffer instead of letting `strtod` run past
    the end of a truncated file.
  - The STEP converter's two `snprintf` calls only build in-memory lookup
    keys, so they are marked `locale-ok` rather than changed.
  - `scripts/locale_numbers_baseline.txt` loses the seven entries.
  - New test `ModelLocaleDecimalTest` loads every text-based sample in
    `media/3D` (PLY, DXF, COLLADA, FBX, STEP, .x, OBJ, X3D, VRML) in "C" and
    in a comma-decimal locale, and requires the same mesh vertex for vertex.
    Against the old readers it fails six checks: FBX and .x fail to load,
    PLY and DXF differ. The standalone model tests link
    `UltraCanvasTextUtils.cpp` for the helpers.
