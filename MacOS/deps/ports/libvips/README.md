# libvips (overlay)

vcpkg's own `libvips` port, with features for the meson options it does not
expose. Its portfile turns every libvips meson option off unless a feature of
the same name is selected - including the loaders libvips builds in by default
(`nsgif`, `ppm`, `analyze`, `radiance`), so they are features here too.

`portfile.cmake` is vcpkg's unchanged (vcpkg commit in `scripts/macos-deps.sh`);
only `vcpkg.json` adds features. When vcpkg's port gains them, drop this overlay.
