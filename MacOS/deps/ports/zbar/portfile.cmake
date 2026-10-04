# zbar is not in vcpkg. Only libzbar is built: the QR decoder behind
# UltraCanvas's zbar support. No video capture, no zbarcam/zbarimg front ends,
# no language bindings.
#
# From git at the release's commit (the release tarball's host is not reachable
# from every build machine), so autoreconf generates configure.
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/mchehab/zbar
    REF bb05ec54eec57f8397cb13fb9161372a281a1219 # 0.23.93
    PATCHES
        # Upstream pull request 299 (commit 3fa414aa), which Homebrew applies
        # too: the image scanner's pointer arithmetic wraps around, undefined
        # behaviour that recent Clang turns into a crash on any image taller
        # than one pixel - i.e. on every QR code the apps decode.
        fix-pointer-wraparound-ub.patch
)

vcpkg_make_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    AUTORECONF
    OPTIONS
        --disable-video
        --disable-doc
        --disable-nls
        --without-python
        --without-qt
        --without-qt6
        --without-gtk
        --without-x
        --without-xshm
        --without-xv
        --without-imagemagick
        --without-graphicsmagick
        --without-java
        --without-dbus
        --without-jpeg
        --without-gir
        --without-npapi
)

vcpkg_make_install()
vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share" "${CURRENT_PACKAGES_DIR}/share/doc" "${CURRENT_PACKAGES_DIR}/bin")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.md")
