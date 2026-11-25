// Apps/MobilePC/MobilePCHostedApps.cpp
// UltraFiler, Texter, UltraViewer, UltraPaint and ArtCreator as hosted applications: each one's
// window classes, compiled into libMobilePC.so, started by the entry point its
// own main() calls after the application is initialised - without main()'s
// single-instance lock, its signal handlers or a loop of its own, which the
// host provides once for all of them. Each stands in for its program by name
// (HostedAppInfo::programs), so UltraDesktop's taskbar button that starts
// "UltraFiler", and UltraFiler's double-click that starts "UltraViewer" with a
// picture, start the hosted one.
// Version: 0.3.0 - ArtCreator logs each file it was handed, read or not (ObjectCountOf)
// Version: 0.2.0 - UltraFiler; every application answers its program's name
// Version: 0.1.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework / ULTRA OS

#include "MobilePCHostedApps.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasHostedApps.h"
#include "UltraCanvasUtils.h"
#include "UltraLocalisation/UltraLocalisation.h"

#ifdef MOBILEPC_WITH_TEXTER
#include "TexterWindowManager.h"
#include "UltraCanvasTextEditor.h"
#endif
#ifdef MOBILEPC_WITH_ULTRAVIEWER
#include "UltraViewerWindow.h"
#endif
#ifdef MOBILEPC_WITH_ULTRAPAINT
#include "UltraPaintWindow.h"
#endif
#ifdef MOBILEPC_WITH_ARTCREATOR
#include "ArtCreatorWindow.h"

#include <android/log.h>
#endif
#ifdef MOBILEPC_WITH_ULTRAFILER
#include "UltraCanvasAlert.h"
#include "UltraCanvasAndroidPermissions.h"
#include "UltraFilerSettingsDialog.h"
#include "UltraFilerWindow.h"

#include <filesystem>
#endif

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace MobilePC {

    namespace {
        std::string AppIcon(const char* name) {
            return NormalizePath(GetResourcesDir() + "media/appicon/" + name);
        }

#ifdef MOBILEPC_WITH_TEXTER
        // Texter's windows, kept as its main() keeps them (one manager for the
        // program), made at the first start.
        TexterWindowManager* Texter() {
            static std::unique_ptr<TexterWindowManager> manager;
            if (!manager) {
                TextEditorConfig config;
                config.title = "UltraTexter " + UltraCanvasTextEditor::version;
                config.showMenuBar = true;
                config.showToolbar = true;
                config.showStatusBar = true;
                config.showLineNumbers = true;
                manager = std::make_unique<TexterWindowManager>(UltraCanvasApplication::GetInstance(), config);
            }
            return manager.get();
        }
#endif

#ifdef MOBILEPC_WITH_ULTRAVIEWER
        // UltraViewer has no list of its windows of its own: each launch is
        // one more viewer, kept for the session (a closed viewer's object is
        // small and its window is gone).
        std::vector<std::unique_ptr<UltraViewerWindow>>& Viewers() {
            static std::vector<std::unique_ptr<UltraViewerWindow>> viewers;
            return viewers;
        }
#endif

#ifdef MOBILEPC_WITH_ARTCREATOR
        // Each file ArtCreator was handed, as it read it - its objects, or
        // that it is not open (the reader refused it and said so in a
        // dialog) - straight to logcat: the emulator test
        // (scripts/android-mobilepc-smoke.sh) waits for the line.
        void LogArtCreatorFiles(const std::shared_ptr<ArtCreatorWindow>& window,
                                const std::vector<std::string>& files) {
            for (const std::string& file : files) {
                const int objects = window ? window->ObjectCountOf(file) : -1;
                const std::string line = objects >= 0
                        ? "MobilePC: artcreator opened " + file + " (" + std::to_string(objects) + " objects)"
                        : "MobilePC: artcreator did not open " + file;
                __android_log_write(objects >= 0 ? ANDROID_LOG_INFO : ANDROID_LOG_WARN, "UltraCanvas", line.c_str());
            }
        }
#endif

#ifdef MOBILEPC_WITH_ULTRAFILER
        // UltraFiler is one window per user on a desktop
        // (UltraCanvasSingleInstance): a later start's folders open in tabs of
        // the window open already. Here the same, by hand - a closed window's
        // object is let go at the next start, and a new one is made.
        std::vector<std::unique_ptr<UltraFilerWindow>>& Filers() {
            static std::vector<std::unique_ptr<UltraFilerWindow>> filers;
            return filers;
        }

        // The phone's own files - /storage/emulated/0 - where a desktop's
        // UltraFiler would open the home folder; "" when there is none.
        std::string PhoneStorage() {
            const std::string path = AndroidPermissions::SharedStoragePath();
            std::error_code ec;
            if (path.empty() || !std::filesystem::is_directory(PathFromUtf8(path), ec)) return std::string();
            return path;
        }

        // Without access to every file UltraFiler sees the phone's storage
        // folders but few files in them. Asked once a run, at UltraFiler's
        // first start, on the TV where the user is looking; the switch itself
        // is Android's, on the phone's screen.
        void OfferPhoneFiles() {
            static bool offered = false;
            if (offered || AndroidPermissions::HasAllFilesAccess()) return;
            offered = true;
            UltraCanvasAlert::Confirm(
                    Tr("UltraFiler can show all of the phone's files - pictures, downloads, "
                       "documents - once MobilePC may read and change them. Allow it on the "
                       "phone's screen now?"),
                    Tr("The phone's files"), [](bool allow) {
                        if (!allow) return;
                        const auto asked = AndroidPermissions::RequestAllFilesAccess();
                        if (asked == AndroidPermissions::FilesAccessRequest::SettingsOpened) {
                            UltraCanvasAlert::Info(
                                    Tr("Switch on the access for MobilePC on the phone's screen and come "
                                       "back. UltraFiler then shows the files: open the folder again or "
                                       "use Refresh."),
                                    Tr("The phone's files"));
                        } else if (asked == AndroidPermissions::FilesAccessRequest::Unavailable) {
                            UltraCanvasAlert::Info(Tr("This phone offers no way to allow it."),
                                                   Tr("The phone's files"));
                        }
                    });
        }

        // Its command line, as its main() reads it: folders, and --ram-disc.
        bool LaunchFiler(const std::vector<std::string>& arguments) {
            std::vector<std::string> folders;
            bool ramDisc = false;
            for (const std::string& argument : arguments) {
                if (argument == "--ram-disc") ramDisc = true;
                else if (!argument.empty() && argument[0] != '-') folders.push_back(argument);
            }
            auto& filers = Filers();
            filers.erase(std::remove_if(filers.begin(), filers.end(),
                                        [](const std::unique_ptr<UltraFilerWindow>& f) { return !f->IsOpen(); }),
                         filers.end());
            if (!filers.empty()) {
                UltraFilerWindow& running = *filers.back();
                // As a second start hands over: the folders in new tabs, the
                // window forward (OpenFolders with none just brings it).
                if (!folders.empty() || !ramDisc) running.OpenFolders(folders);
                if (ramDisc) running.OpenRamDisc();
                return true;
            }
            auto filer = std::make_unique<UltraFilerWindow>();
            if (!filer->Initialize(folders.empty() ? PhoneStorage() : folders.front())) return false;
            filer->Show();
            if (folders.size() > 1) filer->OpenFolders({folders.begin() + 1, folders.end()});
            if (ramDisc) filer->OpenRamDisc();
            filers.push_back(std::move(filer));
            OfferPhoneFiles();
            return true;
        }
#endif
    }

    void RegisterHostedApps() {
#ifdef MOBILEPC_WITH_TEXTER
        UltraCanvasHostedApps::Register({
            "texter", "UltraTexter", Tr("Write and edit texts and code"),
            AppIcon("Texter.png"), {"Office", "TextEditor", "Development"},
            [](const std::vector<std::string>& files) {
                TexterWindowManager* texter = Texter();
                if (texter->GetWindowCount() == 0) {
                    // As main() starts it, without the splash: the last
                    // session's tabs, then the crash-recovery question.
                    auto window = texter->CreateNewWindow();
                    if (!window) return false;
                    window->RestoreSessionDocuments();
                    window->PromptCrashRecovery();
                    for (const std::string& file : files) window->OpenFile(file);
                    return true;
                }
                // Running already: as a second start of the program - its
                // files open in the window worked in last, which comes forward.
                texter->OpenHandedOverFiles(files);
                return true;
            },
            {"UltraTexter", "Texter"}});
#endif
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
            },
            {"UltraViewer"}});
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
            },
            {"UltraPaint"}});
#endif
#ifdef MOBILEPC_WITH_ARTCREATOR
        UltraCanvasHostedApps::Register({
            "artcreator", "ArtCreator", Tr("Vector drawings and illustrations"),
            AppIcon("ArtCreator.png"), {"Graphics", "VectorGraphics"},
            [](const std::vector<std::string>& files) {
                std::shared_ptr<ArtCreatorWindow> window;
                if (!files.empty()) window = ArtCreatorWindow::OpenInRunningWindow(files);
                if (!window) window = ArtCreatorWindow::OpenWindow(files);
                if (!window) return false;
                LogArtCreatorFiles(window, files);
                return true;
            },
            {"ArtCreator"}});
#endif
#ifdef MOBILEPC_WITH_ULTRAFILER
        UltraCanvasHostedApps::Register({
            "ultrafiler", "UltraFiler", Tr("Files and folders"),
            AppIcon("UltraFiler.png"), {"System", "FileManager"},
            LaunchFiler, {"UltraFiler"}});
#endif
    }

    void ShutdownHostedApps() {
#ifdef MOBILEPC_WITH_ULTRAFILER
        Filers().clear();
        UltraFilerSettingsDialog::Shutdown();
#endif
    }

} // namespace MobilePC
