// Apps/Texter/TexterWindowManager.h
// UltraTexter's windows: every editor window of the program, the callbacks
// that tie them together (New Window, a tab dragged out to a window or a new
// one, the last window worked in), and the files a later start hands over.
// Its own header so that a host carrying Texter in its process (MobilePC,
// Apps/MobilePC/MobilePCHostedApps.cpp) keeps the windows as main() does.
// Version: 1.0.0 - out of main.cpp, unchanged
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasApplication.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasTextEditor.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

class TexterWindowManager {
private:
    UltraCanvasApplication* app;
    std::vector<std::shared_ptr<UltraCanvasTextEditor>> editors;
    TextEditorConfig baseConfig;
    // The window the user worked in last: where handed-over files go.
    std::weak_ptr<UltraCanvasTextEditor> lastActiveEditor;

    void WireEditorCallbacks(std::shared_ptr<UltraCanvasTextEditor> editor) {
        auto* editorPtr = editor.get();
        std::weak_ptr<UltraCanvasTextEditor> weakEditor = editor;

        // Allow auto-close when last document is closed and other windows exist
        editorPtr->canCloseEmptyWindow = [this]() {
            return static_cast<int>(editors.size()) > 1;
        };

        // New Window menu item
        editorPtr->onNewWindowRequest = [this]() {
            CreateNewWindow();
        };

        // Tab dragged out: move to target window or create a new one
        editorPtr->onTabDraggedOut = [this, editorPtr](std::shared_ptr<DocumentTab> doc, int screenX, int screenY) {
            auto target = FindEditorAtScreenPoint(screenX, screenY, editorPtr);
            if (target) {
                target->AcceptDocument(doc);
            } else {
                CreateWindowWithDocument(doc, screenX, screenY);
            }

            if (editorPtr->GetDocumentCount() == 0) {
                editorPtr->PerformClose();
            }
        };

        // View > Theme writes Appearance.conf and the theme follows: every
        // window takes it from the theme itself, so there is nothing to sync.

        // Focus: remember the window the user works in
        editorPtr->onWindowFocus = [this, weakEditor]() {
            lastActiveEditor = weakEditor;
        };

        // File saved
        editorPtr->onFileSaved = [](const std::string& path, int tabIndex) {
            debugOutput << "File saved: " << path << std::endl;
        };

        // Window close: remove from manager
        editorPtr->onWindowClosed = [this, weakEditor]() {
            auto ed = weakEditor.lock();
            if (!ed) return;

            auto it = std::find(editors.begin(), editors.end(), ed);
            if (it != editors.end()) {
                editors.erase(it);
            }
        };
    }

    // Find which editor window (if any) is at the given screen coordinates,
    // excluding the source editor.
    std::shared_ptr<UltraCanvasTextEditor> FindEditorAtScreenPoint(int screenX, int screenY,
                                                                    UltraCanvasTextEditor* exclude) {
        for (auto& ed : editors) {
            if (ed.get() == exclude) continue;

            int wx, wy;
            ed->GetWindowPosition(wx, wy);
            int ww = ed->GetWidth();
            int wh = ed->GetHeight();

            if (screenX >= wx && screenX < wx + ww &&
                screenY >= wy && screenY < wy + wh) {
                return ed;
            }
        }
        return nullptr;
    }

public:
    TexterWindowManager(UltraCanvasApplication* appPtr, const TextEditorConfig& config)
        : app(appPtr), baseConfig(config) {}

    std::shared_ptr<UltraCanvasTextEditor> CreateNewWindow(int screenX = -1, int screenY = -1) {
        auto editor = CreateTextEditor(baseConfig);

        if (!editor) {
            debugOutput << "Failed to create text editor window" << std::endl;
            return nullptr;
        }

        // Wire callbacks
        WireEditorCallbacks(editor);

        editors.push_back(editor);
        return editor;
    }

    std::shared_ptr<UltraCanvasTextEditor> CreateWindowWithDocument(
            std::shared_ptr<DocumentTab> doc, int screenX, int screenY) {
        auto editor = CreateNewWindow(screenX, screenY);
        if (!editor) return nullptr;

        // The constructor created an empty "Untitled" tab — extract it
        // so only the transferred document remains
        auto emptyDoc = editor->ExtractDocument(0);

        // Accept the transferred document
        editor->AcceptDocument(doc);

        return editor;
    }

    int GetWindowCount() const { return static_cast<int>(editors.size()); }

    // Files a later start handed over (UltraCanvasSingleInstance::ListenOpen),
    // absolute and UTF-8: opened as File > Open opens several - each in a tab
    // of the window the user worked in last - except that a file already open
    // in any window is switched to there instead of opened a second time. The
    // window that shows the last of them comes forward.
    void OpenHandedOverFiles(const std::vector<std::string>& paths) {
        std::shared_ptr<UltraCanvasTextEditor> target = lastActiveEditor.lock();
        if (!target || std::find(editors.begin(), editors.end(), target) == editors.end()) {
            target = editors.empty() ? nullptr : editors.front();
        }
        if (!target) target = CreateNewWindow();
        if (!target) return;

        std::shared_ptr<UltraCanvasTextEditor> shown;
        for (const auto& path : paths) {
            std::shared_ptr<UltraCanvasTextEditor> editor = target;
            for (const auto& ed : editors) {
                if (ed->HasOpenFile(path)) {
                    editor = ed;
                    break;
                }
            }
            debugOutput << "Opening handed-over file: " << path << std::endl;
            if (editor->OpenFile(path) >= 0) shown = editor;
        }
        if (!shown) shown = target;
        if (shown->IsMinimized()) shown->Restore();
        shown->RaiseAndFocus();
    }
};


} // namespace UltraCanvas
