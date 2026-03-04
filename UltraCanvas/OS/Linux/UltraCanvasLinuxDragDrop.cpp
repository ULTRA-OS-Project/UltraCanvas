// UltraCanvas/OS/Linux/UltraCanvasLinuxDragDrop.cpp
// X11 XDnD (Drag and Drop) protocol implementation
// Implements XDND version 5 for receiving file drops from external applications
// Version: 1.1.0
// Last Modified: 2026-03-04
// Author: UltraCanvas Framework

#include "UltraCanvasLinuxDragDrop.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>

namespace UltraCanvas {

// ===== CONSTRUCTOR & DESTRUCTOR =====

    UltraCanvasLinuxDragDrop::UltraCanvasLinuxDragDrop() = default;

    UltraCanvasLinuxDragDrop::~UltraCanvasLinuxDragDrop() {
        Shutdown();
    }

// ===== INITIALIZATION =====

    bool UltraCanvasLinuxDragDrop::Initialize(Display* disp, Window win) {
        if (!disp || win == 0) {
            std::cerr << "UltraCanvas XDnD: Invalid display or window" << std::endl;
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
        xdndTypeList  = XInternAtom(display, "XdndTypeList",  False);
        xdndSelection = XInternAtom(display, "XdndSelection", False);

        // Dedicated property for receiving selection data during drop
        // Using a custom property avoids conflicts when xdndSelection is used
        // as both selection AND property (which some X servers reject)
        xdndSelectionProperty = XInternAtom(display, "ULTRACANVAS_XDND_DATA", False);

        // Supported MIME types for drop
        textUriList = XInternAtom(display, "text/uri-list", False);
        textPlain   = XInternAtom(display, "text/plain",   False);

        // Advertise XDnD support on the window — version 5
        Atom xdndVersion = 5;
        XChangeProperty(display, window, xdndAware, XA_ATOM, 32,
                        PropModeReplace,
                        reinterpret_cast<unsigned char*>(&xdndVersion), 1);

        std::cout << "UltraCanvas XDnD: Drag-and-drop initialized for window "
                  << window << std::endl;
        return true;
    }

    void UltraCanvasLinuxDragDrop::Shutdown() {
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

        std::cout << "UltraCanvas XDnD: DragEnter from window " << dragSourceWindow
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
        std::cout << "UltraCanvas XDnD: DragLeave" << std::endl;

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
            std::cerr << "UltraCanvas XDnD: Selection conversion failed" << std::endl;
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

            std::cout << "UltraCanvas XDnD: Dropped " << filePaths.size()
                      << " file(s)" << std::endl;
            for (const auto& path : filePaths) {
                std::cout << "  → " << path << std::endl;
            }

            // Notify via callback
            if (onFileDrop && !filePaths.empty()) {
                onFileDrop(filePaths, dragX, dragY);
            }

            SendXdndFinished(dragSourceWindow, true);
        } else {
            if (data) XFree(data);
            std::cerr << "UltraCanvas XDnD: Failed to read selection data"
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

} // namespace UltraCanvas