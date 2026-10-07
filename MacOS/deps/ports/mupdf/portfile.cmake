# MuPDF is not in vcpkg. Built the way Homebrew's formula builds it on macOS:
# a shared libmupdf against the system (here: vcpkg's) freetype, harfbuzz,
# gumbo, libjpeg, openjpeg, jbig2dec, zlib and brotli, keeping the forks MuPDF
# needs (lcms2mt), its own extract and cmark-gfm, and MuJS. No OCR, barcode or
# libarchive support: the apps do not use them through MuPDF.
#
# The source comes from git at the release's commit, not from mupdf.com's
# tarball, so it is pinned without a download hash; the four submodules are
# fetched at the commits the release records for them.
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/ArtifexSoftware/mupdf
    REF 8ad45e92f0935d3d87f1db3f873086472a5e1b24 # 1.28.5
)
foreach(sub IN ITEMS
        "lcms2|thirdparty-lcms2|d69c64417c4a33a4629957fc0e08f4f4c5abcc3d"
        "extract|extract|8750ac39c30a0d65119b426b5a491c5b8e8bf674"
        "mujs|mujs|2c61a7bed7e7625c03c6e82e644a711ac4471c1e"
        "cmark-gfm|thirdparty-cmark-gfm|74e0f5b6b62bbdd1e262c92f4bb2c6c871c6307e")
    string(REPLACE "|" ";" sub "${sub}")
    list(GET sub 0 dir)
    list(GET sub 1 repo)
    list(GET sub 2 ref)
    vcpkg_from_git(
        OUT_SOURCE_PATH sub_path
        URL "https://github.com/ArtifexSoftware/${repo}"
        REF "${ref}"
    )
    file(REMOVE_RECURSE "${SOURCE_PATH}/thirdparty/${dir}")
    file(RENAME "${sub_path}" "${SOURCE_PATH}/thirdparty/${dir}")
endforeach()

# With no SO_VERSION on macOS, install-libs follows the copy with
# `ln -sf libmupdf.dylib libmupdf.dylib`, which replaces the library just
# installed with a link to itself (Homebrew comments the same lines out).
vcpkg_replace_string("${SOURCE_PATH}/Makefile"
    "	ln -sf libmupdf.$(SO)$(SO_VERSION) $(DESTDIR)$(libdir)/libmupdf.$(SO)$(SO_VERSION_MAJOR)\n"
    "")
vcpkg_replace_string("${SOURCE_PATH}/Makefile"
    "	ln -sf libmupdf.$(SO)$(SO_VERSION) $(DESTDIR)$(libdir)/libmupdf.$(SO)\n"
    "")

# On macOS MuPDF's Makerules does not ask pkg-config for the system
# libraries, so their flags are passed in, as Homebrew does.
#
# It does ask pkg-config for one: libcrypto, for PDF digital signatures. With
# only PKG_CONFIG_PATH set, pkg-config still searched its default path, where
# the build machine's Homebrew keeps OpenSSL - so on the Intel runner libmupdf
# linked /usr/local/opt/openssl@3/lib/libcrypto.3.dylib, and package-macos.sh
# refused the suite ("still loads ... from the build machine"). The apps do
# not use MuPDF's signature support, so it is off (HAVE_LIBCRYPTO=no below),
# and PKG_CONFIG_LIBDIR confines every pkg-config query here to vcpkg's
# libraries, so no other probe can reach the build machine either.
vcpkg_find_acquire_program(PKGCONFIG)
set(ENV{PKG_CONFIG_PATH} "${CURRENT_INSTALLED_DIR}/lib/pkgconfig:${CURRENT_INSTALLED_DIR}/share/pkgconfig")
set(ENV{PKG_CONFIG_LIBDIR} "${CURRENT_INSTALLED_DIR}/lib/pkgconfig:${CURRENT_INSTALLED_DIR}/share/pkgconfig")
set(sys_args "")
foreach(lib IN ITEMS
        "FREETYPE|freetype2" "GUMBO|gumbo" "HARFBUZZ|harfbuzz" "LIBJPEG|libjpeg"
        "OPENJPEG|libopenjp2" "ZLIB|zlib" "BROTLI|libbrotlidec libbrotlienc")
    string(REPLACE "|" ";" lib "${lib}")
    list(GET lib 0 name)
    list(GET lib 1 modules)
    separate_arguments(modules UNIX_COMMAND "${modules}")
    foreach(kind IN ITEMS cflags libs)
        execute_process(COMMAND "${PKGCONFIG}" --${kind} ${modules}
            OUTPUT_VARIABLE flags OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE rc)
        if(NOT rc EQUAL 0)
            message(FATAL_ERROR "pkg-config --${kind} ${modules} failed")
        endif()
        string(TOUPPER "${kind}" kind_upper)
        list(APPEND sys_args "SYS_${name}_${kind_upper}=${flags}")
    endforeach()
    list(APPEND sys_args "HAVE_SYS_${name}=yes")
endforeach()

# The deployment target and architecture are set here because MuPDF's own
# Makefile is not driven through vcpkg's toolchain. lcms2mt keeps lcms2's
# function names with different signatures, so libmupdf must not export them:
# an app that also links the real lcms2 (libcdr, libvips) would otherwise bind
# its cmsCreateTransform calls to whichever library the linker saw first.
set(target_flags "-arch ${VCPKG_OSX_ARCHITECTURES} -mmacosx-version-min=${VCPKG_OSX_DEPLOYMENT_TARGET}")
set(lib_ldflags "${target_flags} -install_name @rpath/libmupdf.dylib -Wl,-unexported_symbol,_cms* -Wl,-unexported_symbol,__cms*")

vcpkg_execute_required_process(
    COMMAND make -j${VCPKG_CONCURRENCY}
        build=release
        shared=yes
        verbose=yes
        "prefix=${CURRENT_PACKAGES_DIR}"
        USE_SYSTEM_LIBS=yes
        USE_SYSTEM_JBIG2DEC=yes
        "SYS_JBIG2DEC_CFLAGS=-I${CURRENT_INSTALLED_DIR}/include"
        "SYS_JBIG2DEC_LIBS=-L${CURRENT_INSTALLED_DIR}/lib -ljbig2dec"
        ${sys_args}
        HAVE_X11=no
        HAVE_GLUT=no
        HAVE_CURL=no
        HAVE_LIBCRYPTO=no
        "XCFLAGS=${target_flags}"
        "XLDFLAGS=${target_flags}"
        "XLIB_LDFLAGS=${lib_ldflags}"
        install-libs
    WORKING_DIRECTORY "${SOURCE_PATH}"
    LOGNAME "build-${TARGET_TRIPLET}"
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
