# The libraries UltraCanvas links on iOS, for the iOS simulator on an
# Apple-silicon Mac (the iphonesimulator SDK).
# Built by scripts/ios-bootstrap-sysroot.sh (iOSPortInvestigation.md 4.1).
#
# Static: an iOS app may load a dynamic library only from a signed framework
# in its own bundle, and there is one process to share the libraries with, so
# static archives linked into the app are both simpler and smaller. Release
# only, as on macOS.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)

set(VCPKG_CMAKE_SYSTEM_NAME iOS)
set(VCPKG_OSX_ARCHITECTURES arm64)
# The oldest iOS the apps would run on: a placeholder until packaging decides
# (iOSPortInvestigation.md 4.1). Keep the two triplets in step.
set(VCPKG_OSX_DEPLOYMENT_TARGET 16.0)
# The simulator SDK: the same arm64, a different platform in every object
# (LC_BUILD_VERSION), so a device library does not link into a simulator app.
set(VCPKG_OSX_SYSROOT iphonesimulator)

# Meson must not run the test programs it builds for iOS on the Mac
# (ios-meson-cross.txt says why).
set(VCPKG_MESON_CROSS_FILE "${CMAKE_CURRENT_LIST_DIR}/ios-meson-cross.txt")

# The static libintl (gettext) reads the user's languages through
# CoreFoundation, and vcpkg adds the framework to its link for macOS only:
# glib's tools failed to link with undefined _CFLocaleCopyPreferredLanguages
# and friends. Every link of every port gets it here.
set(VCPKG_LINKER_FLAGS "-framework CoreFoundation")

# libcroco's configure insists on GNU ld's -Bsymbolic, which Apple's linker
# lacks; vcpkg turns it off for macOS only ("-Bsymbolic requested but not
# supported by ld"). PORT is set while vcpkg reads a triplet for a port.
if(PORT STREQUAL "libcroco")
    set(VCPKG_MAKE_CONFIGURE_OPTIONS "--disable-Bsymbolic")
endif()
