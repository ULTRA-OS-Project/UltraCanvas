- **The DirectX .x reader delivered every model as its mirror image.**
  Direct3D's space is left-handed (Z points away from the viewer) and the
  `ModelDocument` is right-handed (Z points toward the viewer). The reader
  copied the numbers across unchanged, so a file arrived mirrored in Z. On
  the symmetric E-45 sample this looked like a half-turn: the nose pointed +Z
  where the OBJ, DXF and MS3D exports point it -Z. In text, markings or any
  asymmetric model it is plainly backwards. The reader now converts as
  Direct3D-to-OpenGL importers do. It negates Z in every position and
  normal, conjugates each frame matrix by the same reflection so the
  hierarchy composes as before, and reverses every face's corners, because a
  reflection alone would turn each face inside out. X and Y do not move, so a
  Y-up file stays Y-up, and the document now states `RightHanded` because it
  is.
  - An exporter's own root-frame reflection and the face winding it reversed
    to match still cancel after the conversion. The winding check against
    the file's own MeshNormals still passes on the E-45 export (93 of 93 and
    925 of 937 faces). Blender's Z-up `(x, y, z)` now arrives as
    `(x, z, -y)`, the axis change Blender's own OBJ export makes. The E-45
    `.x` now lands vertex for vertex where the MS3D sample does.
  - `ModelXFileTest` gains three chirality cases. A point at Direct3D's
    z = +3 lands at -3. A mesh under a rotating frame lands where Direct3D
    draws it, mirrored, which catches negating the vertices without
    converting the frames. A triangle facing a Direct3D camera still faces
    the document's camera. The old reader fails all three, and a reader that
    skips the frame conjugation fails the second. The vertex-colour case now
    finds its vertices by position, because reversing the winding changes
    the order a face visits them in.
