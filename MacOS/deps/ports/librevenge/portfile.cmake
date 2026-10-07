# librevenge is not in vcpkg. The CorelDRAW plug-in's libcdr (built from
# UltraCanvas/third_party/libcdr) needs librevenge-0.0 and librevenge-stream-0.0.
vcpkg_from_sourceforge(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO libwpd/librevenge
    REF librevenge-${VERSION}
    FILENAME "librevenge-${VERSION}.tar.xz"
    SHA512 24f7fceedf45e4907782d36c4cc9e9bad6bfbef97a16487e41ab3ceaa47c8f464826833be9831455f4a7c1567b9307a93e1c85b80cb3b40447be130e0d2d365b
)

vcpkg_make_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        --disable-werror
        --disable-tests
        --disable-fuzzers
        --disable-generators
        --without-docs
)

vcpkg_make_install()
vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share" "${CURRENT_PACKAGES_DIR}/share/doc")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING.MPL" "${SOURCE_PATH}/COPYING.LGPL")
