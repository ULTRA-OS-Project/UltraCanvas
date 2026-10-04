# The libraries the macOS apps bundle, built by vcpkg for the oldest macOS
# they support (see MacOS/deps/README.md). Shared, so the suite's apps load
# one copy from UltraCanvas/Frameworks/; release only, which is all that ships.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_BUILD_TYPE release)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES x86_64)
# Keep in step with MACOSX_DEPLOYMENT_TARGET in .github/workflows/build.yml.
set(VCPKG_OSX_DEPLOYMENT_TARGET 14.0)
