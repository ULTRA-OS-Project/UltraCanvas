// UltraCanvas/OS/Linux/UltraCanvasLinuxDragDrop.cpp
// X11 XDnD (Drag and Drop) protocol implementation
// Implements XDND version 5: receives file drops from external applications
// (target side) and drags files out to other applications (source side).
// Version: 1.2.0
// Last Modified: 2026-07-20
// Author: UltraCanvas Framework

#include "UltraCanvasLinuxDragDrop.h"
#include <X11/Xcursor/Xcursor.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

// ===== CONSTRUCTOR & DESTRUCTOR =====

    UltraCanvasLinuxDragDrop* UltraCanvasLinuxDragDrop::activeSource = nullptr;

    UltraCanvasLinuxDragDrop::UltraCanvasLinuxDragDrop() = default;

    UltraCanvasLinuxDragDrop::~UltraCanvasLinuxDragDrop() {
        Shutdown();
    }

// ===== INITIALIZATION =====

    bool UltraCanvasLinuxDragDrop::Initialize(Display* disp, Window win) {
        if (!disp || win == 0) {
            debugOutput << "UltraCanvas XDnD: Invalid display or window" << std::endl;
            return false;
        }

        display = disp;
        window = win;

        // Initialize XDnD atoms
        xdndAware     = XInternAtom(display, "XdndAware",     False);
        xdndEnter     = XInternAtom(display, "XdndEnter",     False);
        xdndPosition  = XInternAtom(display, "XdndPosition",  False);
        xdndStatus    = XInternAtom(display, "XdndStatus",    False);
        xdndLeave     = XInternAtom(display, "XdndLeave",     False);
        xdndDrop      = XInternAtom(display, "XdndDrop",      False);
        xdndFinished  = XInternAtom(display, "XdndFinished",  False);
        xdndActionCopy = XInternAtom(display, "XdndActionCopy", False);
        xdndActionMove = XInternAtom(display, "XdndActionMove", False);
        xdndProxy     = XInternAtom(display, "XdndProxy",     False);
        xdndTypeList  = XInternAtom(display, "XdndTypeList",  False);
        xdndSelection = XInternAtom(display, "XdndSelection", False);
        atomTargets   = XInternAtom(display, "TARGETS",       False);

        // Dedicated property for receiving selection data during drop
        // Using a custom property avoids conflicts when xdndSelection is used
        // as both selection AND property (which some X servers reject)
        xdndSelectionProperty = XInternAtom(display, "ULTRACANVAS_XDND_DATA", False);

        // Supported MIME types for drop
        textUriList = XInternAtom(display, "text/uri-list", False);
        textPlain   = XInternAtom(display, "text/plain",   False);
        textPlainUtf8 = XInternAtom(display, "text/plain;charset=utf-8", False);

        // Advertise XDnD support on the window — version 5
        Atom xdndVersion = 5;
        XChangeProperty(display, window, xdndAware, XA_ATOM, 32,
                        PropModeReplace,
                        reinterpret_cast<unsigned char*>(&xdndVersion), 1);

        debugOutput << "UltraCanvas XDnD: Drag-and-drop initialized for window "
                  << window << std::endl;
        return true;
    }

    void UltraCanvasLinuxDragDrop::Shutdown() {
        if (activeSource == this) {
            if (sourceDragActive) ReleaseSourceGrab();
            activeSource = nullptr;
        }
        sourceDragActive = false;
        sourceAwaitingFinish = false;
        sourceOnFinished = nullptr;
        if (display) {
            if (sourceCursorCopy != None) { XFreeCursor(display, sourceCursorCopy); sourceCursorCopy = None; }
            if (sourceCursorMove != None) { XFreeCursor(display, sourceCursorMove); sourceCursorMove = None; }
            if (sourceCursorNo   != None) { XFreeCursor(display, sourceCursorNo);   sourceCursorNo   = None; }
        }
        if (display && window) {
            // Remove XdndAware property
            XDeleteProperty(display, window, xdndAware);
        }
        display = nullptr;
        window = 0;
        isDragActive = false;
        dragSourceWindow = None;
    }

// ===== EVENT HANDLING =====

    bool UltraCanvasLinuxDragDrop::HandleXEvent(const XEvent& event) {
        // Only handle ClientMessage and SelectionNotify — never KeyPress, etc.
        if (event.type == ClientMessage) {
            const XClientMessageEvent& cm = event.xclient;

            // Guard: Only process if we're properly initialized
            if (!display || window == 0) return false;

            if (cm.message_type == xdndEnter) {
                // Validate source window: must be non-zero and not our own window.
                // This prevents XIM or other subsystems from accidentally triggering
                // drag-drop when their ClientMessage atoms happen to match XDnD atoms.
                Window sourceWin = static_cast<Window>(cm.data.l[0]);
                if (sourceWin == 0 || sourceWin == window) return false;
                HandleXdndEnter(cm);
                return true;
            }
            if (cm.message_type == xdndPosition) {
                // Only process if we have an active drag from a valid source
                if (dragSourceWindow == None) return false;
                HandleXdndPosition(cm);
                return true;
            }
            if (cm.message_type == xdndLeave) {
                // Only process if we have an active drag
                if (dragSourceWindow == None) return false;
                HandleXdndLeave(cm);
                return true;
            }
            if (cm.message_type == xdndDrop) {
                // Only process if we have an active drag
                if (dragSourceWindow == None) return false;
                HandleXdndDrop(cm);
                return true;
            }
        }

        if (event.type == SelectionNotify) {
            // Only handle if we're expecting selection data (active drag in progress)
            if (isDragActive &&
                (event.xselection.selection == xdndSelection ||
                 event.xselection.property == xdndSelectionProperty)) {
                HandleSelectionNotify(event.xselection);
                return true;
            }
        }

        return false;
    }

// ===== XDND PROTOCOL HANDLERS =====

    void UltraCanvasLinuxDragDrop::HandleXdndEnter(const XClientMessageEvent& cm) {
        dragSourceWindow = static_cast<Window>(cm.data.l[0]);
        isDragActive = true;
        acceptDrop = false;
        sourceTypes.clear();

        // data.l[1] contains:
        //   bit 0: set if more than 3 types (use XdndTypeList property)
        //   bits 24-31: XDnD version used by source
        bool moreTypes = (cm.data.l[1] & 1);

        if (moreTypes) {
            // Source has more than 3 types — read from XdndTypeList property
            FetchTypeListProperty(dragSourceWindow);
        } else {
            // Up to 3 types are stored directly in data.l[2..4]
            for (int i = 2; i <= 4; i++) {
                Atom type = static_cast<Atom>(cm.data.l[i]);
                if (type != None) {
                    sourceTypes.push_back(type);
                }
            }
        }

        // Check if source offers a file type we support
        acceptDrop = SupportsFileType(sourceTypes);

        debugOutput << "UltraCanvas XDnD: DragEnter from window " << dragSourceWindow
                  << " — " << sourceTypes.size() << " types offered, "
                  << (acceptDrop ? "accepting" : "rejecting") << std::endl;

        if (onDragEnter) {
            onDragEnter(dragX, dragY);
        }
    }

    void UltraCanvasLinuxDragDrop::HandleXdndPosition(const XClientMessageEvent& cm) {
        // data.l[0] = source window
        // data.l[2] = (x << 16) | y — position in root window coordinates
        int rootX = (cm.data.l[2] >> 16) & 0xFFFF;
        int rootY = cm.data.l[2] & 0xFFFF;

        // Convert root coordinates to window-local coordinates
        Window child;
        int localX = 0, localY = 0;
        XTranslateCoordinates(display, DefaultRootWindow(display), window,
                              rootX, rootY, &localX, &localY, &child);

        dragX = localX;
        dragY = localY;

        if (onDragOver) {
            onDragOver(dragX, dragY);
        }

        // Send XdndStatus back to the source
        SendXdndStatus(dragSourceWindow, acceptDrop);
    }

    void UltraCanvasLinuxDragDrop::HandleXdndLeave(const XClientMessageEvent& cm) {
        debugOutput << "UltraCanvas XDnD: DragLeave" << std::endl;

        isDragActive = false;
        dragSourceWindow = None;

        if (onDragLeave) {
            onDragLeave(dragX, dragY);
        }
    }

    void UltraCanvasLinuxDragDrop::HandleXdndDrop(const XClientMessageEvent& cm) {
        if (!acceptDrop) {
            // We can't accept this drop — send finished with rejected
            SendXdndFinished(dragSourceWindow, false);
            isDragActive = false;
            dragSourceWindow = None;
            return;
        }

        // Request the selection data in text/uri-list format.
        // Use xdndSelectionProperty as the target property — using xdndSelection
        // as both selection AND property can fail on some X servers.
        XConvertSelection(display, xdndSelection, textUriList,
                          xdndSelectionProperty, window, CurrentTime);
        XFlush(display);
    }

    void UltraCanvasLinuxDragDrop::HandleSelectionNotify(const XSelectionEvent& sel) {
        if (sel.property == None) {
            // Selection conversion failed
            debugOutput << "UltraCanvas XDnD: Selection conversion failed" << std::endl;
            SendXdndFinished(dragSourceWindow, false);
            isDragActive = false;
            dragSourceWindow = None;
            return;
        }

        // Read the selection data from the window property
        Atom actualType;
        int actualFormat;
        unsigned long itemCount, bytesRemaining;
        unsigned char* data = nullptr;

        int result = XGetWindowProperty(display, window, sel.property,
                                        0, 65536, True, AnyPropertyType,
                                        &actualType, &actualFormat,
                                        &itemCount, &bytesRemaining,
                                        &data);

        if (result == Success && data && itemCount > 0) {
            // Calculate actual byte length based on the property format.
            // XGetWindowProperty returns itemCount in units of the format size:
            //   format 8  → itemCount is byte count
            //   format 16 → itemCount is count of 16-bit values
            //   format 32 → itemCount is count of long values (4 or 8 bytes)
            unsigned long byteCount = itemCount;
            if (actualFormat == 16) {
                byteCount = itemCount * 2;
            } else if (actualFormat == 32) {
                byteCount = itemCount * sizeof(long);
            }

            std::string uriList(reinterpret_cast<char*>(data), byteCount);
            XFree(data);

            // Parse the URI list into individual file paths
            std::vector<std::string> filePaths = ParseUriList(uriList);

            debugOutput << "UltraCanvas XDnD: Dropped " << filePaths.size()
                      << " file(s)" << std::endl;
            for (const auto& path : filePaths) {
                debugOutput << "  → " << path << std::endl;
            }

            // Notify via callback
            if (onFileDrop && !filePaths.empty()) {
                onFileDrop(filePaths, dragX, dragY);
            }

            SendXdndFinished(dragSourceWindow, true);
        } else {
            if (data) XFree(data);
            debugOutput << "UltraCanvas XDnD: Failed to read selection data"
                      << " (result=" << result
                      << ", itemCount=" << itemCount << ")" << std::endl;
            SendXdndFinished(dragSourceWindow, false);
        }

        isDragActive = false;
        dragSourceWindow = None;
    }

// ===== RESPONSE MESSAGES =====

    void UltraCanvasLinuxDragDrop::SendXdndStatus(Window sourceWindow, bool accept) {
        XClientMessageEvent msg;
        std::memset(&msg, 0, sizeof(msg));

        msg.type = ClientMessage;
        msg.display = display;
        msg.window = sourceWindow;
        msg.message_type = xdndStatus;
        msg.format = 32;

        msg.data.l[0] = static_cast<long>(window);          // Target window
        msg.data.l[1] = accept ? 1 : 0;                     // Bit 0: accept, Bit 1: want position updates
        if (accept) {
            msg.data.l[1] |= 2;  // We want XdndPosition events (for drag overlay updates)
        }
        msg.data.l[2] = 0;                                  // Rectangle x,y (0 = whole window)
        msg.data.l[3] = 0;                                  // Rectangle w,h (0 = whole window)
        msg.data.l[4] = accept ? static_cast<long>(xdndActionCopy) : None;  // Accepted action

        XSendEvent(display, sourceWindow, False, NoEventMask,
                   reinterpret_cast<XEvent*>(&msg));
        XFlush(display);
    }

    void UltraCanvasLinuxDragDrop::SendXdndFinished(Window sourceWindow, bool accepted) {
        XClientMessageEvent msg;
        std::memset(&msg, 0, sizeof(msg));

        msg.type = ClientMessage;
        msg.display = display;
        msg.window = sourceWindow;
        msg.message_type = xdndFinished;
        msg.format = 32;

        msg.data.l[0] = static_cast<long>(window);
        msg.data.l[1] = accepted ? 1 : 0;
        msg.data.l[2] = accepted ? static_cast<long>(xdndActionCopy) : None;

        XSendEvent(display, sourceWindow, False, NoEventMask,
                   reinterpret_cast<XEvent*>(&msg));
        XFlush(display);
    }

// ===== HELPER METHODS =====

    void UltraCanvasLinuxDragDrop::FetchTypeListProperty(Window sourceWindow) {
        Atom actualType;
        int actualFormat;
        unsigned long itemCount, bytesRemaining;
        unsigned char* data = nullptr;

        int result = XGetWindowProperty(display, sourceWindow, xdndTypeList,
                                        0, 256, False, XA_ATOM,
                                        &actualType, &actualFormat,
                                        &itemCount, &bytesRemaining,
                                        &data);

        if (result == Success && data) {
            Atom* atoms = reinterpret_cast<Atom*>(data);
            for (unsigned long i = 0; i < itemCount; i++) {
                sourceTypes.push_back(atoms[i]);
            }
            XFree(data);
        } else {
            if (data) XFree(data);
        }
    }

    bool UltraCanvasLinuxDragDrop::SupportsFileType(const std::vector<Atom>& typeList) {
        for (const Atom& type : typeList) {
            if (type == textUriList || type == textPlain) {
                return true;
            }
        }
        return false;
    }

    std::vector<std::string> UltraCanvasLinuxDragDrop::ParseUriList(const std::string& uriList) {
        std::vector<std::string> paths;
        std::istringstream stream(uriList);
        std::string line;

        while (std::getline(stream, line)) {
            // Remove trailing \r if present (uri-list uses \r\n line endings)
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            // Skip empty lines and comments
            if (line.empty() || line[0] == '#') continue;

            // Decode the URI to a local file path
            std::string path = DecodeUri(line);
            if (!path.empty()) {
                paths.push_back(path);
            }
        }

        return paths;
    }

    std::string UltraCanvasLinuxDragDrop::DecodeUri(const std::string& uri) {
        // Strip "file://" prefix
        const std::string filePrefix = "file://";

        std::string path;
        if (uri.compare(0, filePrefix.size(), filePrefix) == 0) {
            path = uri.substr(filePrefix.size());
        } else if (uri.compare(0, 5, "file:") == 0) {
            // Some applications use "file:" without "//"
            path = uri.substr(5);
        } else {
            // Not a file URI — skip
            return "";
        }

        // Handle "file://hostname/path" by stripping hostname (usually localhost)
        // Standard form is "file:///path" (empty hostname)
        // Non-standard "file://localhost/path" also seen
        if (!path.empty() && path[0] != '/') {
            // There's a hostname before the path
            size_t slashPos = path.find('/');
            if (slashPos != std::string::npos) {
                path = path.substr(slashPos);
            } else {
                return ""; // No path after hostname
            }
        }

        // Decode percent-encoded characters (%20 → space, etc.)
        std::string decoded;
        decoded.reserve(path.size());

        for (size_t i = 0; i < path.size(); i++) {
            if (path[i] == '%' && i + 2 < path.size()) {
                char hex[3] = { path[i + 1], path[i + 2], '\0' };
                char* end = nullptr;
                long value = std::strtol(hex, &end, 16);
                if (end == hex + 2 && value >= 0 && value <= 255) {
                    decoded += static_cast<char>(value);
                    i += 2;
                    continue;
                }
            }
            decoded += path[i];
        }

        return decoded;
    }

// =========================================================================
// ===== XDND SOURCE SIDE (drag files OUT to other applications) =====
// =========================================================================

    std::string UltraCanvasLinuxDragDrop::EncodeFileUri(const std::string& path) {
        // RFC 3986 percent-encoding of a local file path. '/' is kept as the
        // path separator; everything outside the unreserved set is escaped so
        // names with spaces, '#', '%' or non-ASCII (UTF-8) survive the trip.
        static const char* hex = "0123456789ABCDEF";
        std::string uri = "file://";
        for (unsigned char c : path) {
            bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                              (c >= '0' && c <= '9') ||
                              c == '-' || c == '_' || c == '.' || c == '~' ||
                              c == '/';
            if (unreserved) {
                uri += static_cast<char>(c);
            } else {
                uri += '%';
                uri += hex[c >> 4];
                uri += hex[c & 0x0F];
            }
        }
        return uri;
    }

    bool UltraCanvasLinuxDragDrop::BeginSourceDrag(
            const std::vector<std::string>& filePaths,
            FileDragFinishedCallback onFinished) {
        if (!display || window == 0 || filePaths.empty()) return false;
        if (sourceDragActive) return false;

        // A previous drop still being served is finalized now: a new drag
        // replaces the XdndSelection contents anyway.
        if (activeSource && activeSource != this) {
            activeSource->sourceAwaitingFinish = false;
            if (activeSource->sourceDragActive) {
                activeSource->FinishSourceDrag(false, false);
            }
        }
        sourceAwaitingFinish = false;

        // Build the two payloads served from XdndSelection.
        sourceFilePaths = filePaths;
        sourceUriList.clear();
        sourcePlainList.clear();
        for (const std::string& p : filePaths) {
            sourceUriList += EncodeFileUri(p) + "\r\n";
            if (!sourcePlainList.empty()) sourcePlainList += "\n";
            sourcePlainList += p;
        }

        // Own XdndSelection: targets fetch the data from us via
        // ConvertSelection once the drop lands.
        XSetSelectionOwner(display, xdndSelection, window, CurrentTime);
        if (XGetSelectionOwner(display, xdndSelection) != window) {
            debugOutput << "UltraCanvas XDnD: could not own XdndSelection" << std::endl;
            return false;
        }

        // Drag cursors: proper dnd-* themed cursors when available, X font
        // cursors as fallback.
        if (sourceCursorCopy == None) {
            sourceCursorCopy = XcursorLibraryLoadCursor(display, "dnd-copy");
            if (sourceCursorCopy == None)
                sourceCursorCopy = XcursorLibraryLoadCursor(display, "copy");
            if (sourceCursorCopy == None)
                sourceCursorCopy = XCreateFontCursor(display, XC_hand2);
        }
        if (sourceCursorMove == None) {
            sourceCursorMove = XcursorLibraryLoadCursor(display, "dnd-move");
            if (sourceCursorMove == None)
                sourceCursorMove = XcursorLibraryLoadCursor(display, "move");
            if (sourceCursorMove == None)
                sourceCursorMove = XCreateFontCursor(display, XC_fleur);
        }
        if (sourceCursorNo == None) {
            sourceCursorNo = XcursorLibraryLoadCursor(display, "dnd-none");
            if (sourceCursorNo == None)
                sourceCursorNo = XcursorLibraryLoadCursor(display, "forbidden");
            if (sourceCursorNo == None)
                sourceCursorNo = XCreateFontCursor(display, XC_X_cursor);
        }

        // Grab the pointer for the duration of the drag so every motion and
        // the final release come to this window regardless of what the
        // cursor is over.
        int grab = XGrabPointer(display, window, False,
                                ButtonReleaseMask | PointerMotionMask,
                                GrabModeAsync, GrabModeAsync,
                                None, sourceCursorNo, CurrentTime);
        if (grab != GrabSuccess) {
            debugOutput << "UltraCanvas XDnD: XGrabPointer failed (" << grab
                        << ") — drag not started" << std::endl;
            return false;
        }

        sourceDragActive = true;
        sourceOnFinished = std::move(onFinished);
        sourceTarget = None;
        sourceTargetVersion = 5;
        sourceTargetAccepts = false;
        sourceAcceptedAction = None;
        sourceSuggestedAction = xdndActionCopy;
        sourceStatusPending = false;
        sourceHavePendingPos = false;
        sourceDropOnStatus = false;
        sourceCurrentCursor = sourceCursorNo;
        activeSource = this;

        debugOutput << "UltraCanvas XDnD: source drag started with "
                    << filePaths.size() << " file(s)" << std::endl;
        return true;
    }

    bool UltraCanvasLinuxDragDrop::HandleSourceXEvent(const XEvent& event) {
        if (!display) return false;

        // XdndStatus / XdndFinished can arrive after the button was released
        // (drop sent), so accept ClientMessages while awaiting the finish too.
        if (event.type == ClientMessage &&
            (sourceDragActive || sourceAwaitingFinish)) {
            const XClientMessageEvent& cm = event.xclient;
            if (cm.message_type == xdndStatus)   { HandleSourceStatus(cm);   return true; }
            if (cm.message_type == xdndFinished) { HandleSourceFinished(cm); return true; }
        }

        if (!sourceDragActive) return false;

        switch (event.type) {
            case MotionNotify: {
                // Compress: only the newest queued motion matters.
                XEvent next;
                XMotionEvent motion = event.xmotion;
                while (XCheckTypedWindowEvent(display, motion.window,
                                              MotionNotify, &next)) {
                    motion = next.xmotion;
                }
                sourceLastMotionTime = motion.time;
                SourceMotion(motion.x_root, motion.y_root, motion.state);
                return true;
            }
            case ButtonRelease: {
                sourceLastMotionTime = event.xbutton.time;
                ReleaseSourceGrab();
                if (sourceTarget != None && sourceStatusPending) {
                    // The target hasn't answered the last XdndPosition yet —
                    // decide when its XdndStatus arrives.
                    sourceDropOnStatus = true;
                } else if (sourceTarget != None && sourceTargetAccepts) {
                    SendSourceDrop(sourceTarget, event.xbutton.time);
                } else {
                    if (sourceTarget != None) SendSourceLeave(sourceTarget);
                    FinishSourceDrag(false, false);
                }
                return true;
            }
            case KeyPress: {
                KeySym sym = XLookupKeysym(const_cast<XKeyEvent*>(&event.xkey), 0);
                if (sym == XK_Escape) {
                    ReleaseSourceGrab();
                    if (sourceTarget != None) SendSourceLeave(sourceTarget);
                    FinishSourceDrag(false, false);
                }
                // Swallow all keystrokes while dragging.
                return true;
            }
            case KeyRelease:
                return true;
            default:
                return false;
        }
    }

    void UltraCanvasLinuxDragDrop::SourceMotion(int rootX, int rootY,
                                                unsigned int stateMask) {
        // Shift suggests a move, anything else a copy. The target still
        // chooses the final action and reports it via XdndStatus.
        Atom wanted = (stateMask & ShiftMask) ? xdndActionMove : xdndActionCopy;
        bool actionChanged = (wanted != sourceSuggestedAction);
        sourceSuggestedAction = wanted;

        int version = 5;
        Window target = FindXdndTarget(rootX, rootY, version);
        // Dropping a selection onto the very window it is being dragged from
        // is a no-op (the target side rejects its own source anyway).
        if (target == window) target = None;

        if (target != sourceTarget) {
            if (sourceTarget != None) SendSourceLeave(sourceTarget);
            sourceTarget = target;
            sourceTargetVersion = version;
            sourceTargetAccepts = false;
            sourceAcceptedAction = None;
            sourceStatusPending = false;
            sourceHavePendingPos = false;
            if (sourceTarget != None) SendSourceEnter(sourceTarget);
            UpdateSourceCursor();
        }

        if (sourceTarget == None) return;

        if (sourceStatusPending) {
            // XDND flow control: one XdndPosition in flight at a time; the
            // newest position is sent as soon as the status arrives.
            sourceHavePendingPos = true;
            sourcePendingRootX = rootX;
            sourcePendingRootY = rootY;
        } else {
            SendSourcePosition(sourceTarget, rootX, rootY, sourceLastMotionTime);
        }
        if (actionChanged) UpdateSourceCursor();
    }

    Window UltraCanvasLinuxDragDrop::FindXdndTarget(int rootX, int rootY,
                                                    int& outVersion) {
        outVersion = 5;
        Window root = DefaultRootWindow(display);
        Window current = root;

        for (int depth = 0; depth < 64; ++depth) {
            // An XdndProxy property redirects the XDND conversation (used by
            // desktop / tray windows); coordinates keep using the real window.
            Window candidate = current;
            {
                Atom actualType; int actualFormat;
                unsigned long items, remaining;
                unsigned char* data = nullptr;
                if (XGetWindowProperty(display, current, xdndProxy, 0, 1, False,
                                       XA_WINDOW, &actualType, &actualFormat,
                                       &items, &remaining, &data) == Success && data) {
                    if (actualType == XA_WINDOW && items >= 1) {
                        candidate = *reinterpret_cast<Window*>(data);
                    }
                    XFree(data);
                }
            }

            // XdndAware on the candidate ends the search.
            {
                Atom actualType; int actualFormat;
                unsigned long items, remaining;
                unsigned char* data = nullptr;
                if (XGetWindowProperty(display, candidate, xdndAware, 0, 1, False,
                                       AnyPropertyType, &actualType, &actualFormat,
                                       &items, &remaining, &data) == Success && data) {
                    long version = (items >= 1 && actualFormat == 32)
                                   ? *reinterpret_cast<long*>(data) : 0;
                    XFree(data);
                    if (version >= 3) {
                        outVersion = static_cast<int>(std::min<long>(version, 5));
                        return candidate;
                    }
                }
            }

            // Descend into the child that contains the pointer.
            Window child = None;
            int cx = 0, cy = 0;
            if (!XTranslateCoordinates(display, root, current, rootX, rootY,
                                       &cx, &cy, &child)) {
                return None;
            }
            if (child == None) return None;
            current = child;
        }
        return None;
    }

    void UltraCanvasLinuxDragDrop::SendSourceEnter(Window target) {
        XClientMessageEvent msg;
        std::memset(&msg, 0, sizeof(msg));
        msg.type = ClientMessage;
        msg.display = display;
        msg.window = target;
        msg.message_type = xdndEnter;
        msg.format = 32;
        msg.data.l[0] = static_cast<long>(window);
        // Bits 24-31: protocol version; bit 0 clear = all types fit below.
        msg.data.l[1] = static_cast<long>(std::min(sourceTargetVersion, 5)) << 24;
        msg.data.l[2] = static_cast<long>(textUriList);
        msg.data.l[3] = static_cast<long>(textPlainUtf8);
        msg.data.l[4] = static_cast<long>(textPlain);
        XSendEvent(display, target, False, NoEventMask,
                   reinterpret_cast<XEvent*>(&msg));
        XFlush(display);
    }

    void UltraCanvasLinuxDragDrop::SendSourcePosition(Window target, int rootX,
                                                      int rootY, Time time) {
        XClientMessageEvent msg;
        std::memset(&msg, 0, sizeof(msg));
        msg.type = ClientMessage;
        msg.display = display;
        msg.window = target;
        msg.message_type = xdndPosition;
        msg.format = 32;
        msg.data.l[0] = static_cast<long>(window);
        msg.data.l[2] = (static_cast<long>(rootX) << 16) | (rootY & 0xFFFF);
        msg.data.l[3] = static_cast<long>(time);
        msg.data.l[4] = static_cast<long>(sourceSuggestedAction);
        XSendEvent(display, target, False, NoEventMask,
                   reinterpret_cast<XEvent*>(&msg));
        XFlush(display);
        sourceStatusPending = true;
    }

    void UltraCanvasLinuxDragDrop::SendSourceLeave(Window target) {
        XClientMessageEvent msg;
        std::memset(&msg, 0, sizeof(msg));
        msg.type = ClientMessage;
        msg.display = display;
        msg.window = target;
        msg.message_type = xdndLeave;
        msg.format = 32;
        msg.data.l[0] = static_cast<long>(window);
        XSendEvent(display, target, False, NoEventMask,
                   reinterpret_cast<XEvent*>(&msg));
        XFlush(display);
    }

    void UltraCanvasLinuxDragDrop::SendSourceDrop(Window target, Time time) {
        XClientMessageEvent msg;
        std::memset(&msg, 0, sizeof(msg));
        msg.type = ClientMessage;
        msg.display = display;
        msg.window = target;
        msg.message_type = xdndDrop;
        msg.format = 32;
        msg.data.l[0] = static_cast<long>(window);
        msg.data.l[2] = static_cast<long>(time);
        XSendEvent(display, target, False, NoEventMask,
                   reinterpret_cast<XEvent*>(&msg));
        XFlush(display);

        // The drag gesture is over; the selection stays owned and served
        // until the target confirms with XdndFinished (or a new drag starts).
        bool moved = (sourceAcceptedAction == xdndActionMove);
        sourceAwaitingFinish = true;
        FinishSourceDrag(true, moved);

        debugOutput << "UltraCanvas XDnD: drop sent to window " << target
                    << (moved ? " (move)" : " (copy)") << std::endl;
    }

    void UltraCanvasLinuxDragDrop::HandleSourceStatus(const XClientMessageEvent& cm) {
        if (static_cast<Window>(cm.data.l[0]) != sourceTarget) return;

        sourceTargetAccepts = (cm.data.l[1] & 1) != 0;
        sourceAcceptedAction = sourceTargetAccepts
                ? static_cast<Atom>(cm.data.l[4]) : None;
        if (sourceTargetAccepts && sourceAcceptedAction == None) {
            sourceAcceptedAction = xdndActionCopy;
        }

        sourceStatusPending = false;

        if (sourceDropOnStatus) {
            // The button was already released; this status decides the drop.
            sourceDropOnStatus = false;
            if (sourceTargetAccepts) {
                SendSourceDrop(sourceTarget, sourceLastMotionTime);
            } else {
                SendSourceLeave(sourceTarget);
                FinishSourceDrag(false, false);
            }
            return;
        }

        if (sourceDragActive && sourceHavePendingPos && sourceTarget != None) {
            sourceHavePendingPos = false;
            SendSourcePosition(sourceTarget, sourcePendingRootX,
                               sourcePendingRootY, sourceLastMotionTime);
        }
        UpdateSourceCursor();
    }

    void UltraCanvasLinuxDragDrop::HandleSourceFinished(const XClientMessageEvent& cm) {
        (void)cm;
        sourceAwaitingFinish = false;
        if (!sourceDragActive && activeSource == this) {
            activeSource = nullptr;
        }
        debugOutput << "UltraCanvas XDnD: target finished the drop" << std::endl;
    }

    void UltraCanvasLinuxDragDrop::UpdateSourceCursor() {
        if (!sourceDragActive) return;
        Cursor wanted = sourceCursorNo;
        if (sourceTarget != None && sourceTargetAccepts) {
            wanted = (sourceAcceptedAction == xdndActionMove ||
                      (sourceAcceptedAction == None &&
                       sourceSuggestedAction == xdndActionMove))
                     ? sourceCursorMove : sourceCursorCopy;
        }
        if (wanted == sourceCurrentCursor) return;
        sourceCurrentCursor = wanted;
        XChangeActivePointerGrab(display,
                                 ButtonReleaseMask | PointerMotionMask,
                                 wanted, CurrentTime);
    }

    void UltraCanvasLinuxDragDrop::ReleaseSourceGrab() {
        XUngrabPointer(display, CurrentTime);
        XFlush(display);
    }

    void UltraCanvasLinuxDragDrop::FinishSourceDrag(bool accepted, bool moved) {
        sourceDragActive = false;
        sourceTarget = None;
        sourceTargetAccepts = false;
        sourceStatusPending = false;
        sourceHavePendingPos = false;
        sourceDropOnStatus = false;

        // Keep serving the selection while a drop is being finalized;
        // otherwise this source is done.
        if (!sourceAwaitingFinish && activeSource == this) {
            activeSource = nullptr;
        }

        if (sourceOnFinished) {
            auto cb = std::move(sourceOnFinished);
            sourceOnFinished = nullptr;
            cb(accepted, moved);
        }
    }

    bool UltraCanvasLinuxDragDrop::AnswerSourceSelectionRequest(
            const XSelectionRequestEvent& request) {
        XSelectionEvent response;
        std::memset(&response, 0, sizeof(response));
        response.type = SelectionNotify;
        response.display = request.display;
        response.requestor = request.requestor;
        response.selection = request.selection;
        response.target = request.target;
        response.property = request.property != None ? request.property
                                                     : request.target;
        response.time = request.time;

        bool served = true;
        if (request.target == atomTargets) {
            Atom targets[] = { atomTargets, textUriList, textPlainUtf8, textPlain };
            XChangeProperty(display, request.requestor, response.property,
                            XA_ATOM, 32, PropModeReplace,
                            reinterpret_cast<unsigned char*>(targets),
                            sizeof(targets) / sizeof(Atom));
        } else if (request.target == textUriList) {
            XChangeProperty(display, request.requestor, response.property,
                            textUriList, 8, PropModeReplace,
                            reinterpret_cast<const unsigned char*>(sourceUriList.data()),
                            static_cast<int>(sourceUriList.size()));
        } else if (request.target == textPlain || request.target == textPlainUtf8) {
            XChangeProperty(display, request.requestor, response.property,
                            request.target, 8, PropModeReplace,
                            reinterpret_cast<const unsigned char*>(sourcePlainList.data()),
                            static_cast<int>(sourcePlainList.size()));
        } else {
            response.property = None;
            served = false;
        }

        XSendEvent(display, request.requestor, False, NoEventMask,
                   reinterpret_cast<XEvent*>(&response));
        XFlush(display);
        return served;
    }

    bool UltraCanvasLinuxDragDrop::HandleSourceSelectionEvent(const XEvent& event) {
        UltraCanvasLinuxDragDrop* source = activeSource;
        if (!source || !source->display) return false;

        if (event.type == SelectionRequest &&
            event.xselectionrequest.selection == source->xdndSelection &&
            event.xselectionrequest.owner == source->window) {
            source->AnswerSourceSelectionRequest(event.xselectionrequest);
            return true;
        }

        if (event.type == SelectionClear &&
            event.xselectionclear.selection == source->xdndSelection &&
            event.xselectionclear.window == source->window) {
            // Another application (or another window of ours) started a drag:
            // this source's data is obsolete.
            if (source->sourceDragActive) {
                source->ReleaseSourceGrab();
                if (source->sourceTarget != None) {
                    source->SendSourceLeave(source->sourceTarget);
                }
                source->sourceAwaitingFinish = false;
                source->FinishSourceDrag(false, false);
            } else {
                source->sourceAwaitingFinish = false;
                activeSource = nullptr;
            }
            return true;
        }

        return false;
    }

} // namespace UltraCanvas