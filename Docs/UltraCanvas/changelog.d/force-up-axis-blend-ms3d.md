- **The .blend and .ms3d readers ignored `ConversionOptions::ForceUpAxis`.**
  Every other model reader turns the scene to the up axis a caller asks for.
  These two returned their own (Z-up for Blender, Y-up for MilkShape), so a
  `.blend` asked for Y-up through `LoadModelDocument` still arrived Z-up.
  Both now call `ModelDocument::ConvertUpAxis` as the others do. The rotation
  goes on the root nodes, and for MilkShape that includes the root joints, so
  a skinned mesh and its skeleton turn together.
  - `ModelFormatsPluginTest` now asks every geometry sample in `media/3D` for
    each up axis in turn. It checks that the document reports the axis it was
    asked for, and that the height measured along Y in one equals the height
    measured along Z in the other, so a reader cannot pass by relabelling.
    Against the old readers it fails for `.blend` and `.ms3d` and for nothing
    else.
