@echo off
REM UltraCanvas Windows build using MSYS2 MinGW64
REM Run this from MSYS2 MinGW64 shell, or ensure C:\msys64\mingw64\bin is on PATH
REM
REM Required MSYS2 packages:
REM   pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja
REM   pacman -S mingw-w64-x86_64-pkgconf mingw-w64-x86_64-cairo mingw-w64-x86_64-pango
REM   pacman -S mingw-w64-x86_64-harfbuzz mingw-w64-x86_64-glib2 mingw-w64-x86_64-freetype
REM   pacman -S mingw-w64-x86_64-tinyxml2 mingw-w64-x86_64-jsoncpp mingw-w64-x86_64-libiconv
REM   pacman -S mingw-w64-x86_64-zlib mingw-w64-x86_64-libvips

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_DEMO_APP=ON -DULTRACANVAS_PLUGIN_CDR=OFF -DULTRACANVAS_PLUGIN_XAR=ON
cmake --build build --parallel
