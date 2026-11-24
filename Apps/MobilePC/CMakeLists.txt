# Apps/MobilePC/CMakeLists.txt
# MobilePC - UltraDesktop on a TV or monitor, run by an Android phone. An
# Android app only: libMobilePC.so, which UltraCanvasActivity loads (its
# manifest's android.app.lib_name), carrying UltraDesktop and the
# applications its Apps window starts, all in one process, on the
# framework's desktop mode (UltraCanvas/OS/Android/UltraCanvasAndroidDesktop.h).
#
# The applications are compiled in from their own folders, each one when the
# core has the widgets it needs; none of them is changed for it - each window
# class is started by the entry its own main() uses (MobilePCHostedApps.cpp).
#
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

if(NOT ANDROID)
    message(STATUS "  [−] MobilePC - SKIPPED (an Android app: configure with the NDK's toolchain)")
    return()
endif()
if(NOT ULTRACANVAS_LIBRARY)
    message(STATUS "  [−] MobilePC - SKIPPED (UltraCanvas UI library not built)")
    return()
endif()

set(_mpc_root ${CMAKE_SOURCE_DIR}/Apps)
set(_mpc_desktop ${_mpc_root}/UltraDesktop/ui)

add_library(MobilePC SHARED
    main.cpp
    MobilePCControlPad.cpp
    MobilePCHostedApps.cpp
    # UltraDesktop, as its own target builds it (Apps/UltraDesktop), less its
    # main.cpp: MobilePC makes the desktop window itself.
    ${_mpc_desktop}/UltraDesktopAppCatalogue.cpp
    ${_mpc_desktop}/UltraDesktopAppCategories.cpp
    ${_mpc_desktop}/UltraDesktopAppStarter.cpp
    ${_mpc_desktop}/UltraDesktopBluetoothPanel.cpp
    ${_mpc_desktop}/UltraDesktopCategoryBoard.cpp
    ${_mpc_desktop}/UltraDesktopClipboardPanel.cpp
    ${_mpc_desktop}/UltraDesktopClockPanel.cpp
    ${_mpc_desktop}/UltraDesktopDevicePanel.cpp
    ${_mpc_desktop}/UltraDesktopPanelTheme.cpp
    ${_mpc_desktop}/UltraDesktopPopupPanel.cpp
    ${_mpc_desktop}/UltraDesktopSettings.cpp
    ${_mpc_desktop}/UltraDesktopStickerboard.cpp
    ${_mpc_desktop}/UltraDesktopDesktopIcons.cpp
    ${_mpc_desktop}/UltraDesktopTasksWindow.cpp
    ${_mpc_desktop}/UltraDesktopVolumePanel.cpp
    ${_mpc_desktop}/UltraDesktopWifiPanel.cpp
    ${_mpc_desktop}/UltraDesktopWindow.cpp
    ${_mpc_desktop}/UltraDesktopWindowPreviews.cpp
)

# The framework's Android folder for UltraCanvasAndroidDesktop.h, which the
# core does not publish among its headers (ChatCube does the same for the
# permission prompts).
target_include_directories(MobilePC PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_mpc_desktop}
    ${CMAKE_SOURCE_DIR}/UltraCanvas/OS/Android)

target_link_libraries(MobilePC PRIVATE ${ULTRACANVAS_LIBRARY})
# The clipboard history UltraDesktop records and its quick panel shows.
if(TARGET UltraClipboardHistory)
    target_link_libraries(MobilePC PRIVATE UltraClipboardHistory)
endif()
if(TARGET fmt::fmt)
    target_link_libraries(MobilePC PRIVATE fmt::fmt)
endif()
target_compile_features(MobilePC PRIVATE cxx_std_20)

# MobilePC versions itself (Docs/MobilePC/CHANGELOG.md); UltraDesktop keeps
# its own number, which its About shows.
target_compile_definitions(MobilePC PRIVATE
    MOBILEPC_VERSION="${MOBILEPC_VERSION}"
    ULTRADESKTOP_VERSION="${ULTRADESKTOP_VERSION}"
    UCAPP_ICON_PATH="media/appicon/MobilePC.png"
    $<$<CONFIG:Debug>:ULTRACANVAS_DEBUG=1>
)

# ===== THE HOSTED APPLICATIONS =====
get_property(_mpc_widgets GLOBAL PROPERTY ULTRACANVAS_WIDGETS_COMPILED_IN)
set(_mpc_apps "")

if("MediaViewer" IN_LIST _mpc_widgets)
    target_sources(MobilePC PRIVATE ${_mpc_root}/UltraViewer/UltraViewerWindow.cpp)
    target_include_directories(MobilePC PRIVATE ${_mpc_root}/UltraViewer)
    target_compile_definitions(MobilePC PRIVATE MOBILEPC_WITH_ULTRAVIEWER=1
                               ULTRAVIEWER_VERSION="${ULTRAVIEWER_VERSION}")
    list(APPEND _mpc_apps UltraViewer)
endif()

if("ColorPaletteManager" IN_LIST _mpc_widgets)
    target_sources(MobilePC PRIVATE
        ${_mpc_root}/UltraPaint/UltraPaintWindow.cpp
        ${_mpc_root}/UltraPaint/UltraPaintTools.cpp
        ${_mpc_root}/UltraPaint/UltraPaintFilters.cpp
        ${_mpc_root}/UltraPaint/UltraPaintDialogs.cpp
        ${_mpc_root}/UltraPaint/UltraPaintSettingsDialog.cpp)
    target_include_directories(MobilePC PRIVATE ${_mpc_root}/UltraPaint)
    target_compile_definitions(MobilePC PRIVATE MOBILEPC_WITH_ULTRAPAINT=1
                               ULTRAPAINT_VERSION="${ULTRAPAINT_VERSION}")
    if(TARGET UltraClipboardHistory)
        target_compile_definitions(MobilePC PRIVATE ULTRAPAINT_HAVE_CLIPBOARD_HISTORY=1)
    endif()
    list(APPEND _mpc_apps UltraPaint)
endif()

if("ColorPaletteManager" IN_LIST _mpc_widgets AND "VectorCanvas" IN_LIST _mpc_widgets)
    target_sources(MobilePC PRIVATE
        ${_mpc_root}/ArtCreator/ArtCreatorWindow.cpp
        ${_mpc_root}/ArtCreator/ArtCreatorCommands.cpp
        ${_mpc_root}/ArtCreator/ArtCreatorTools.cpp
        ${_mpc_root}/ArtCreator/ArtCreatorDialogs.cpp
        ${_mpc_root}/ArtCreator/ArtCreatorIcon.cpp)
    target_include_directories(MobilePC PRIVATE ${_mpc_root}/ArtCreator)
    target_compile_definitions(MobilePC PRIVATE MOBILEPC_WITH_ARTCREATOR=1
                               ARTCREATOR_VERSION="${ARTCREATOR_VERSION}")
    list(APPEND _mpc_apps ArtCreator)
endif()

string(REPLACE ";" ", " _mpc_apps_text "${_mpc_apps}")
message(STATUS "  [✓] MobilePC - ENABLED (Android library libMobilePC.so; with UltraDesktop and ${_mpc_apps_text})")
message(STATUS "      Version ${MOBILEPC_VERSION} (Docs/MobilePC/CHANGELOG.md)")
