# The libraries UltraCanvas links on iOS, for iPhone and iPad hardware
# (the iphoneos SDK).
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

# Meson must not run the test programs it builds for iOS on the Mac
# (ios-meson-cross.txt says why).
set(VCPKG_MESON_CROSS_FILE "${CMAKE_CURRENT_LIST_DIR}/ios-meson-cross.txt")
