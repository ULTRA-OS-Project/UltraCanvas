@echo off
REM UltraCanvas Windows build - MSYS2 CLANG64 (x86_64) or CLANGARM64 (Windows on ARM)
REM
REM Run this from the MSYS2 CLANG64 shell, or from a command prompt with
REM C:\msys64\clang64\bin on PATH (C:\msys64\clangarm64\bin on ARM). CLANG64 is
REM the environment CI builds (.github/workflows/build.yml, "Setup MSYS2") and
REM the one package-win.sh packages from; the MINGW64 (gcc) environment this
REM file used to name is not supported, and a tree configured there will not
REM link the same libraries the packaging script collects.
REM
REM Required MSYS2 packages (replace clang-x86_64 with clang-aarch64 on ARM):
REM   pacman -S mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-cmake
REM   pacman -S mingw-w64-clang-x86_64-ninja mingw-w64-clang-x86_64-pkgconf
REM   pacman -S mingw-w64-clang-x86_64-cppwinrt
REM   pacman -S mingw-w64-clang-x86_64-cairo mingw-w64-clang-x86_64-pango
REM   pacman -S mingw-w64-clang-x86_64-harfbuzz mingw-w64-clang-x86_64-glib2
REM   pacman -S mingw-w64-clang-x86_64-freetype mingw-w64-clang-x86_64-tinyxml2
REM   pacman -S mingw-w64-clang-x86_64-libiconv mingw-w64-clang-x86_64-zlib
REM   pacman -S mingw-w64-clang-x86_64-libvips mingw-w64-clang-x86_64-fmt
REM   pacman -S mingw-w64-clang-x86_64-glew          (OpenGL surface support)
REM   pacman -S mingw-w64-clang-x86_64-curl-winssl   (UltraNet)
REM Optional, for the plug-ins the CI build enables:
REM   pacman -S mingw-w64-clang-x86_64-libcdr mingw-w64-clang-x86_64-librevenge
REM   pacman -S mingw-w64-clang-x86_64-lcms2 mingw-w64-clang-x86_64-icu mingw-w64-clang-x86_64-boost
REM   pacman -S mingw-w64-clang-x86_64-rust          (Vectorizer)
REM   pacman -S mingw-w64-clang-x86_64-mupdf mingw-w64-clang-x86_64-libmupdf
REM   pacman -S mingw-w64-clang-x86_64-leptonica mingw-w64-clang-x86_64-tesseract-ocr
REM The complete, current list is the "Setup MSYS2 (Windows)" step in
REM .github/workflows/build.yml. The CDR plug-in is ON, as in CI; pass
REM -DULTRACANVAS_PLUGIN_CDR=OFF if libcdr is not installed.

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_DEMO_APP=ON -DULTRACANVAS_PLUGIN_CDR=ON -DULTRACANVAS_PLUGIN_XAR=ON -DULTRACANVAS_ENABLE_NET=ON
cmake --build build --parallel
