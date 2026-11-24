// Apps/MobilePC/MobilePCHostedApps.cpp
// UltraViewer, UltraPaint and ArtCreator as hosted applications: each one's
// window classes, compiled into libMobilePC.so, started by the entry point its
// own main() calls after the application is initialised - without main()'s
// single-instance lock, its signal handlers or a loop of its own, which the
// host provides once for all of them.
// Version: 0.1.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework / ULTRA OS

#include "MobilePCHostedApps.h"

#include "UltraCanvasConfig.h"
#include "UltraCanvasHostedApps.h"
#include "UltraCanvasUtils.h"
#include "UltraLocalisation/UltraLocalisation.h"

#ifdef MOBILEPC_WITH_ULTRAVIEWER
#include "UltraViewerWindow.h"
#endif
#ifdef MOBILEPC_WITH_ULTRAPAINT
#include "UltraPaintWindow.h"
#endif
#ifdef MOBILEPC_WITH_ARTCREATOR
#include "ArtCreatorWindow.h"
#endif

#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace MobilePC {

    namespace {
        std::string AppIcon(const char* name) {
            return NormalizePath(GetResourcesDir() + "media/appicon/" + name);
        }

#ifdef MOBILEPC_WITH_ULTRAVIEWER
        // UltraViewer has no list of its windows of its own: each launch is
        // one more viewer, kept for the session (a closed viewer's object is
        // small and its window is gone).
        std::vector<std::unique_ptr<UltraViewerWindow>>& Viewers() {
            static std::vector<std::unique_ptr<UltraViewerWindow>> viewers;
            return viewers;
        }
#endif
    }

    void RegisterHostedApps() {
#ifdef MOBILEPC_WITH_ULTRAVIEWER
        UltraCanvasHostedApps::Register({
            "ultraviewer", "UltraViewer", Tr("Pictures, videos, music and documents"),
            AppIcon("UltraViewer.png"), {"Graphics", "Viewer", "AudioVideo"},
            [](const std::vector<std::string>& files) {
                auto viewer = std::make_unique<UltraViewerWindow>();
                if (!viewer->Initialize(files)) return false;
                viewer->Show();
                Viewers().push_back(std::move(viewer));
                return true;
            }});
#endif
#ifdef MOBILEPC_WITH_ULTRAPAINT
        UltraCanvasHostedApps::Register({
            "ultrapaint", "UltraPaint", Tr("Paint and edit pictures"),
            AppIcon("UltraPaint.png"), {"Graphics", "RasterGraphics"},
            [](const std::vector<std::string>& files) {
                // A file goes into the window open already, as a second
                // start of the program would hand it there.
                if (!files.empty() && UltraPaintWindow::OpenInRunningWindow(files)) return true;
                return UltraPaintWindow::OpenWindow(files) != nullptr;
            }});
#endif
#ifdef MOBILEPC_WITH_ARTCREATOR
        UltraCanvasHostedApps::Register({
            "artcreator", "ArtCreator", Tr("Vector drawings and illustrations"),
            AppIcon("ArtCreator.png"), {"Graphics", "VectorGraphics"},
            [](const std::vector<std::string>& files) {
                if (!files.empty() && ArtCreatorWindow::OpenInRunningWindow(files)) return true;
                return ArtCreatorWindow::OpenWindow(files) != nullptr;
            }});
#endif
    }

} // namespace MobilePC
