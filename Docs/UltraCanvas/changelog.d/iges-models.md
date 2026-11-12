- **IGES files open and save.** The Models plugin has a second exact B-rep
  format beside STEP: IGES 5.3 (`.igs`, `.iges`), read into and written out of
  `ModelDocument::Brep` by `ModelConverter::IgesConverter`
  (`Plugins/Models/IGES/`), and reached through `LoadModelDocument` /
  `SaveModelDocument` and the FileLoader inventory like every other 3D format.
  - **Both shapes an IGES file takes are read.** Manifold solid B-rep objects
    (186, with shells, faces, loops, edge and vertex lists) arrive as closed
    solids whose faces share their edges; trimmed surfaces (144 with 142
    boundaries), bounded planes and untrimmed bounded surfaces - what most IGES
    files contain - arrive as one open body. Curves: lines, arcs, ellipses,
    copious data, parametric splines (converted exactly to B-splines),
    rational B-splines and composites, each under its transformation matrix.
    Surfaces: planes (108, 190), cylinder, cone, sphere, torus (192-198),
    ruled, revolution, tabulated cylinder and rational B-spline. Colours (the
    standard numbers and colour definitions), name properties, the unit and
    the sender's details come from the file.
  - **A trimming curve in (u, v) has to agree with its edge to be kept.** It
    is lifted onto the surface and checked against the model-space edge at its
    ends and through its length, turned round if it runs the other way, and
    dropped with a warning if it does not lie on the edge at all - so a writer
    that parameterises a surface differently from the reader costs a projection,
    not a wrong face.
  - **Writing keeps a solid a solid.** A closed body is written as a manifold
    solid B-rep object with one vertex list and one edge list, an open one as
    trimmed surfaces with model-space boundaries (and (u, v) boundaries on
    B-spline faces); analytic surfaces stay analytic and NURBS stay NURBS. A
    document holding only meshes is written as a faceted solid, with the same
    warning the STEP writer gives. Every number is built as a dot-decimal
    string, never streamed, so the file is the same on a comma-decimal desktop
    (`ModelLocaleDecimalTest` writes two IGES cases in both locales).
  - Not read, and named in a warning when met: hyperbolic and parabolic arcs
    (read as their chord), offset surfaces (140), bounded surfaces (143),
    subfigure instances (408), and the compressed and binary forms of the file.
- **`ModelIgesTest`** covers the card syntax (redefined delimiters, Hollerith
  strings holding them, D exponents, decks with no line ends or CR LF ends,
  strings split across cards) and measures the readers' results against two
  samples in `media/3D/IGES` that `make_samples.py` builds entity by entity
  without the C++ writer: a block whose meshed volume must be 6000, and a
  plate-with-hole and boss whose area must be the plate less its hole plus the
  boss's side. Every STEP sample also goes STEP -> IGES -> back and must mesh
  to the same area and volume.
- **The 3D model table in `Docs/Modules/FileLoader/formats.md` lists what the
  Models plugin reads and writes.** It still said only STL was implemented and
  that OBJ, FBX, 3DS, PLY and COLLADA were not, which had not been true since
  the Models plugin arrived. The demo app has an IGES page with both samples.
