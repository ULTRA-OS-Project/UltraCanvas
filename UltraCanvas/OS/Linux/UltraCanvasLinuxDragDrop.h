// UltraCanvas/OS/Linux/UltraCanvasLinuxDragDrop.h
// X11 XDnD (Drag and Drop) protocol handler for external file drops
// Version: 1.0.0
// Last Modified: 2026-02-26
// Author: UltraCanvas Framework
#pragma once

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <string>
#include <vector>
#include <functional>

namespace UltraCanvas {

    // Callback types for drag-and-drop events
    using FileDropCallback = std::function<void(const std::vector<std::string>& filePaths)>;
    using DragEnterCallback = std::function<void()>;
    using DragLeaveCallback = std::function<void()>;
    using DragOverCallback = std::function<void(int x, int y)>;

    // =========================================================================
    // XDnD Protocol Handler
    //
    // Implements the XDND protocol (version 5) for receiving file drops
    // from external applications (file managers, etc.) onto UltraCanvas windows.
    //
    // Usage:
    //   1. Call Initialize() after X11 display and window are created
    //   2. Call HandleXEvent() from the window's event handler for ClientMessage
    //      and SelectionNotify events
    //   3. Set callbacks to receive drop notifications
    // =========================================================================

    class UltraCanvasLinuxDragDrop {
    public:
        UltraCanvasLinuxDragDrop();
        ~UltraCanvasLinuxDragDrop();

        // ===== INITIALIZATION =====

        /// Initialize XDnD for a specific window
        /// @param display X11 display connection
        /// @param window X11 window to enable drop on
        /// @return true if XDnD was set up successfully
        bool Initialize(Display* display, Window window);

        /// Shutdown and clean up
        void Shutdown();

        // ===== EVENT HANDLING =====

        /// Handle an X11 event — returns true if the event was consumed
        /// @param event X11 event to process
        /// @return true if event was an XDnD event and was handled
        bool HandleXEvent(const XEvent& event);

        // ===== STATE QUERIES =====

        /// Check if a drag operation is currently in progress over this window
        bool IsDragActive() const { return isDragActive; }

        /// Get the current drag position (valid only during drag)
        int GetDragX() const { return dragX; }
        int GetDragY() const { return dragY; }

        // ===== CALLBACKS =====

        /// Called when files are dropped onto the window
        FileDropCallback onFileDrop;

        /// Called when an external drag enters the window
        DragEnterCallback onDragEnter;

        /// Called when an external drag leaves the window
        DragLeaveCallback onDragLeave;

        /// Called repeatedly as the drag moves over the window
        DragOverCallback onDragOver;

    private:
        // ===== XDND PROTOCOL HANDLERS =====
        void HandleXdndEnter(const XClientMessageEvent& cm);
        void HandleXdndPosition(const XClientMessageEvent& cm);
        void HandleXdndLeave(const XClientMessageEvent& cm);
        void HandleXdndDrop(const XClientMessageEvent& cm);
        void HandleSelectionNotify(const XSelectionEvent& sel);

        // ===== HELPER METHODS =====
        void SendXdndStatus(Window sourceWindow, bool accept);
        void SendXdndFinished(Window sourceWindow, bool accepted);
        std::vector<std::string> ParseUriList(const std::string& uriList);
        std::string DecodeUri(const std::string& uri);
        bool SupportsFileType(const std::vector<Atom>& typeList);
        void FetchTypeListProperty(Window sourceWindow);

        // ===== X11 STATE =====
        Display* display = nullptr;
        Window window = 0;

        // ===== XDND ATOMS =====
        Atom xdndAware = None;
        Atom xdndEnter = None;
        Atom xdndPosition = None;
        Atom xdndStatus = None;
        Atom xdndLeave = None;
        Atom xdndDrop = None;
        Atom xdndFinished = None;
        Atom xdndActionCopy = None;
        Atom xdndTypeList = None;
        Atom xdndSelection = None;

        // Supported drop types
        Atom textUriList = None;     // text/uri-list (file paths)
        Atom textPlain = None;       // text/plain (fallback)

        // ===== DRAG STATE =====
        bool isDragActive = false;
        Window dragSourceWindow = None;
        int dragX = 0;
        int dragY = 0;
        bool acceptDrop = false;

        // Types offered by the drag source
        std::vector<Atom> sourceTypes;
    };

} // namespace UltraCanvas