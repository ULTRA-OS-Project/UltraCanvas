// UltraCanvas/OS/Linux/UltraCanvasLinuxDragDrop.h
// X11 XDnD (Drag and Drop) protocol handler: receives external file drops
// (target side) and initiates file drags to other applications (source side).
// Version: 1.2.0
// Last Modified: 2026-07-20
// Author: UltraCanvas Framework
#pragma once

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <string>
#include <vector>
#include <functional>

namespace UltraCanvas {

    // Callback types for drag-and-drop events
    using FileDropCallback = std::function<void(const std::vector<std::string>& filePaths, int x, int y)>;
    using DragEnterCallback = std::function<void(int x, int y)>;
    using DragLeaveCallback = std::function<void(int x, int y)>;
    using DragOverCallback = std::function<void(int x, int y)>;
    // Source-side completion: accepted = a target took the drop;
    // moved = the accepted action was XdndActionMove (the target moves the
    // files itself — the source only needs to refresh its view).
    using FileDragFinishedCallback = std::function<void(bool accepted, bool moved)>;

    // =========================================================================
    // XDnD Protocol Handler
    //
    // Implements the XDND protocol (version 5) in both directions:
    //   - Target: receiving file drops from external applications onto
    //     UltraCanvas windows.
    //   - Source: dragging files out of an UltraCanvas window into other
    //     applications (file managers, editors, ...). Started with
    //     BeginSourceDrag() while a mouse button is held; the pointer is
    //     grabbed and MotionNotify / ButtonRelease / KeyPress events must be
    //     routed to HandleSourceXEvent() until the drag ends.
    //
    // Usage (target):
    //   1. Call Initialize() after X11 display and window are created
    //   2. Call HandleXEvent() from the window's event handler for ClientMessage
    //      and SelectionNotify events
    //   3. Set callbacks to receive drop notifications
    //
    // V1.1.0 fixes:
    //   - Added source window validation to prevent Ctrl/XIM false triggers
    //   - Added dedicated selection property atom for reliable drop data retrieval
    //   - Format-aware byte count in SelectionNotify handling
    // V1.2.0:
    //   - XDND source implementation (drag files to other applications)
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
        /// Only processes ClientMessage and SelectionNotify events.
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

        // ===== SOURCE SIDE (drag files OUT of this window) =====

        /// Start dragging files from this window to other applications.
        /// Must be called while a mouse button is physically held down (the
        /// usual "pressed on an item and moved" gesture): the pointer is
        /// grabbed and the drag runs inside the normal event loop until the
        /// button is released (drop) or Escape is pressed (cancel).
        /// @param filePaths absolute paths of the dragged files
        /// @param onFinished optional completion callback (accepted / moved)
        /// @return true when the drag started (pointer grab succeeded)
        bool BeginSourceDrag(const std::vector<std::string>& filePaths,
                             FileDragFinishedCallback onFinished = nullptr);

        /// True while a source drag started by BeginSourceDrag() is running.
        bool IsSourceDragActive() const { return sourceDragActive; }

        /// Route pointer / key / XdndStatus / XdndFinished events to the
        /// active source drag. Returns true when the event was consumed.
        bool HandleSourceXEvent(const XEvent& event);

        /// Serve SelectionRequest / SelectionClear events for the XdndSelection
        /// of whichever handler instance is currently running a source drag
        /// (or still owns the selection after a drop). Called from the
        /// application's global selection-event dispatch.
        static bool HandleSourceSelectionEvent(const XEvent& event);

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
        Atom xdndSelectionProperty = None;   // Dedicated property for receiving selection data

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

        // ===== SOURCE-SIDE INTERNALS =====
        void SourceMotion(int rootX, int rootY, unsigned int stateMask);
        void SendSourceEnter(Window target);
        void SendSourcePosition(Window target, int rootX, int rootY, Time time);
        void SendSourceLeave(Window target);
        void SendSourceDrop(Window target, Time time);
        void HandleSourceStatus(const XClientMessageEvent& cm);
        void HandleSourceFinished(const XClientMessageEvent& cm);
        bool AnswerSourceSelectionRequest(const XSelectionRequestEvent& request);
        void FinishSourceDrag(bool accepted, bool moved);
        void ReleaseSourceGrab();
        void UpdateSourceCursor();
        // Deepest window under the root position carrying XdndAware
        // (honouring XdndProxy); outVersion = target's XDND version.
        Window FindXdndTarget(int rootX, int rootY, int& outVersion);
        static std::string EncodeFileUri(const std::string& path);

        // Additional atoms used by the source side
        Atom xdndActionMove = None;
        Atom xdndProxy = None;
        Atom textPlainUtf8 = None;
        Atom atomTargets = None;

        // Source drag state
        bool sourceDragActive = false;       // grab held, pointer tracked
        bool sourceAwaitingFinish = false;   // drop sent, selection still served
        std::vector<std::string> sourceFilePaths;
        std::string sourceUriList;           // text/uri-list payload
        std::string sourcePlainList;         // text/plain payload (newline paths)
        FileDragFinishedCallback sourceOnFinished;
        Window sourceTarget = None;          // XdndAware window under pointer
        int sourceTargetVersion = 5;
        bool sourceTargetAccepts = false;
        Atom sourceAcceptedAction = None;    // action from last XdndStatus
        Atom sourceSuggestedAction = None;   // copy (default) / move (Shift)
        bool sourceStatusPending = false;    // XdndPosition sent, no status yet
        bool sourceHavePendingPos = false;   // motion arrived while waiting
        bool sourceDropOnStatus = false;     // button released while waiting
        int sourcePendingRootX = 0, sourcePendingRootY = 0;
        Time sourceLastMotionTime = 0;
        Cursor sourceCursorCopy = None;
        Cursor sourceCursorMove = None;
        Cursor sourceCursorNo = None;
        Cursor sourceCurrentCursor = None;

        // The instance currently owning XdndSelection as a drag source.
        static UltraCanvasLinuxDragDrop* activeSource;
    };

} // namespace UltraCanvas