// UltraCanvasLinuxClipboard.cpp
// Complete X11-specific clipboard implementation for Linux
// Version: 1.1.0
// Last Modified: 2025-08-14
// Author: UltraCanvas Framework

#include "UltraCanvasLinuxClipboard.h"
#include "UltraCanvasApplication.h"
#include <iostream>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <unistd.h>
#include <sys/select.h>
#include <chrono>

namespace UltraCanvas {

// ===== CONSTRUCTOR & DESTRUCTOR =====
    UltraCanvasLinuxClipboard::UltraCanvasLinuxClipboard()
            : display(nullptr)
            , window(0)
            , clipboardChanged(false)
            , selectionReady(false)
            , ownsClipboard(false)
            , ownsPrimary(false) {

        lastChangeCheck = std::chrono::steady_clock::now();
    }

    UltraCanvasLinuxClipboard::~UltraCanvasLinuxClipboard() {
        Shutdown();
    }

// ===== INITIALIZATION =====
    bool UltraCanvasLinuxClipboard::Initialize() {
        std::cout << "UltraCanvas: Initializing Linux clipboard..." << std::endl;

        // Get X11 display from the main application
        if (!GetDisplayFromApplication()) {
            LogError("Initialize", "Failed to get X11 display from application");
            return false;
        }

        // Create helper window for clipboard operations
        window = CreateHelperWindow();
        if (window == 0) {
            LogError("Initialize", "Failed to create helper window");
            return false;
        }

        // Initialize X11 atoms
        InitializeAtoms();

        // Get initial clipboard state
        std::string initialText;
        if (GetClipboardText(initialText)) {
            lastClipboardText = initialText;
        }

        std::cout << "UltraCanvas: Linux clipboard initialized successfully" << std::endl;
        return true;
    }

    void UltraCanvasLinuxClipboard::Shutdown() {
        if (display && window) {
            XDestroyWindow(display, window);
            window = 0;
        }

        display = nullptr;
        ownsClipboard = false;
        ownsPrimary = false;
        clipboardTextData.clear();
        std::cout << "UltraCanvas: Linux clipboard shut down" << std::endl;
    }

    bool UltraCanvasLinuxClipboard::GetDisplayFromApplication() {
        // Get the display from UltraCanvasLinuxApplication
        UltraCanvasApplication* app = UltraCanvasApplication::GetInstance();
        if (!app) {
            std::cerr << "UltraCanvas: No Linux application instance found" << std::endl;
            return false;
        }

        display = app->GetDisplay();
        if (!display) {
            std::cerr << "UltraCanvas: No X11 display available" << std::endl;
            return false;
        }

        return true;
    }

    Window UltraCanvasLinuxClipboard::CreateHelperWindow() {
        if (!display) return 0;

        int screen = DefaultScreen(display);
        Window root = RootWindow(display, screen);

        // Create a simple input-only window for clipboard handling
        Window helperWindow = XCreateSimpleWindow(
                display, root,
                0, 0, 1, 1, 0,
                BlackPixel(display, screen),
                WhitePixel(display, screen)
        );

        if (helperWindow == 0) {
            LogError("CreateHelperWindow", "XCreateSimpleWindow failed");
            return 0;
        }

        // Set window properties
        XStoreName(display, helperWindow, "UltraCanvas Clipboard Helper");

        // Select events we need for clipboard handling
        XSelectInput(display, helperWindow, PropertyChangeMask | SelectionNotify | SelectionRequest | SelectionClear);

        return helperWindow;
    }

    void UltraCanvasLinuxClipboard::InitializeAtoms() {
        atomClipboard = XInternAtom(display, "CLIPBOARD", False);
        atomPrimary = XInternAtom(display, "PRIMARY", False);
        atomTargets = XInternAtom(display, "TARGETS", False);
        atomText = XInternAtom(display, "TEXT", False);
        atomUtf8String = XInternAtom(display, "UTF8_STRING", False);
        atomString = XInternAtom(display, "STRING", False);
        atomTextPlain = XInternAtom(display, "text/plain", False);
        atomTextPlainUtf8 = XInternAtom(display, "text/plain;charset=utf-8", False);
        atomImagePng = XInternAtom(display, "image/png", False);
        atomImageJpeg = XInternAtom(display, "image/jpeg", False);
        atomImageBmp = XInternAtom(display, "image/bmp", False);
        atomTextUriList = XInternAtom(display, "text/uri-list", False);
        atomApplicationOctetStream = XInternAtom(display, "application/octet-stream", False);
    }

// ===== CLIPBOARD OPERATIONS =====
    bool UltraCanvasLinuxClipboard::GetClipboardText(std::string& text) {
        return ReadTextFromClipboard(atomClipboard, text);
    }

    bool UltraCanvasLinuxClipboard::SetClipboardText(const std::string& text) {
        return WriteTextToClipboard(atomClipboard, text);
    }

    bool UltraCanvasLinuxClipboard::GetClipboardImage(std::vector<uint8_t>& imageData, std::string& format) {
        return ReadImageFromClipboard(atomClipboard, imageData, format);
    }

    bool UltraCanvasLinuxClipboard::SetClipboardImage(const std::vector<uint8_t>& imageData, const std::string& format) {
        return WriteImageToClipboard(atomClipboard, imageData, format);
    }

    bool UltraCanvasLinuxClipboard::GetClipboardFiles(std::vector<std::string>& filePaths) {
        return ReadFilesFromClipboard(atomClipboard, filePaths);
    }

    bool UltraCanvasLinuxClipboard::SetClipboardFiles(const std::vector<std::string>& filePaths) {
        return WriteFilesToClipboard(atomClipboard, filePaths);
    }

// ===== MONITORING =====
    bool UltraCanvasLinuxClipboard::HasClipboardChanged() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastChangeCheck);

        if (elapsed.count() > 100) { // Check every 100ms
            std::string currentText;
            if (GetClipboardText(currentText)) {
                if (currentText != lastClipboardText) {
                    clipboardChanged = true;
                    lastClipboardText = currentText;
                }
            }
            lastChangeCheck = now;
        }

        return clipboardChanged;
    }

    void UltraCanvasLinuxClipboard::ResetChangeState() {
        clipboardChanged = false;
    }

// ===== FORMAT DETECTION =====
    std::vector<std::string> UltraCanvasLinuxClipboard::GetAvailableFormats() {
        std::vector<std::string> formats;

        std::vector<uint8_t> targetsData;
        std::string targetsFormat;

        if (ReadClipboardData(atomClipboard, atomTargets, targetsData, targetsFormat)) {
            // Parse the TARGETS list
            if (targetsData.size() % sizeof(Atom) == 0) {
                size_t numAtoms = targetsData.size() / sizeof(Atom);
                Atom* atoms = reinterpret_cast<Atom*>(targetsData.data());

                for (size_t i = 0; i < numAtoms; ++i) {
                    std::string formatName = AtomToString(atoms[i]);
                    if (!formatName.empty()) {
                        formats.push_back(formatName);
                    }
                }
            }
        }

        return formats;
    }

    bool UltraCanvasLinuxClipboard::IsFormatAvailable(const std::string& format) {
        auto formats = GetAvailableFormats();
        return std::find(formats.begin(), formats.end(), format) != formats.end();
    }

// ===== EVENT PROCESSING =====
    void UltraCanvasLinuxClipboard::ProcessEvents() {
        if (!display || !window) return;

        // Process any pending X11 events related to our clipboard window
        XEvent event;
        while (XCheckWindowEvent(display, window,
                                 SelectionRequest | SelectionClear | SelectionNotify | PropertyChangeMask,
                                 &event)) {
            HandleSelectionEvent(event);
        }
    }

// ===== TEXT OPERATIONS =====
    bool UltraCanvasLinuxClipboard::ReadTextFromClipboard(Atom selection, std::string& text) {
        // If we own the selection, return our data directly
        if ((selection == atomClipboard && ownsClipboard) ||
            (selection == atomPrimary && ownsPrimary)) {
            text = clipboardTextData;
            return !text.empty();
        }

        // Try different text formats in order of preference
        std::vector<uint8_t> data;
        std::string format;

        // Try UTF8_STRING first (preferred)
        if (ReadClipboardData(selection, atomUtf8String, data, format)) {
            text = std::string(reinterpret_cast<const char*>(data.data()), data.size());
            return true;
        }

        // Try text/plain;charset=utf-8
        if (ReadClipboardData(selection, atomTextPlainUtf8, data, format)) {
            text = std::string(reinterpret_cast<const char*>(data.data()), data.size());
            return true;
        }

        // Try text/plain
        if (ReadClipboardData(selection, atomTextPlain, data, format)) {
            text = std::string(reinterpret_cast<const char*>(data.data()), data.size());
            return true;
        }

        // Try STRING as fallback
        if (ReadClipboardData(selection, atomString, data, format)) {
            text = std::string(reinterpret_cast<const char*>(data.data()), data.size());
            return true;
        }

        // Try TEXT as last resort
        if (ReadClipboardData(selection, atomText, data, format)) {
            text = std::string(reinterpret_cast<const char*>(data.data()), data.size());
            return true;
        }

        return false;
    }

    bool UltraCanvasLinuxClipboard::WriteTextToClipboard(Atom selection, const std::string& text) {
        if (!display || !window) {
            LogError("WriteTextToClipboard", "No display or window available");
            return false;
        }

        if (text.empty()) {
            LogError("WriteTextToClipboard", "Empty text provided");
            return false;
        }

        std::cout << "UltraCanvas: Setting clipboard text: \"" << text.substr(0, 50)
                  << (text.length() > 50 ? "..." : "") << "\"" << std::endl;

        // Store the text data for selection requests
        clipboardTextData = text;

        // Take ownership of the selection
        XSetSelectionOwner(display, selection, window, CurrentTime);
        XFlush(display);

        // Verify we own the selection
        Window owner = XGetSelectionOwner(display, selection);
        bool success = (owner == window);

        if (success) {
            if (selection == atomClipboard) {
                ownsClipboard = true;
            } else if (selection == atomPrimary) {
                ownsPrimary = true;
            }

            std::cout << "UltraCanvas: Successfully acquired ownership of "
                      << AtomToString(selection) << std::endl;

            // Process any immediate selection requests
            ProcessSelectionEvents();
        } else {
            std::cerr << "UltraCanvas: Failed to acquire ownership of "
                      << AtomToString(selection) << " (current owner: " << owner << ")" << std::endl;
        }

        return success;
    }

// ===== IMAGE OPERATIONS =====
    bool UltraCanvasLinuxClipboard::ReadImageFromClipboard(Atom selection, std::vector<uint8_t>& imageData, std::string& format) {
        // Try different image formats in order of preference
        std::vector<Atom> imageFormats = {atomImagePng, atomImageJpeg, atomImageBmp};

        std::string atomFormat;
        for (Atom imageFormat : imageFormats) {
            if (ReadClipboardData(selection, imageFormat, imageData, atomFormat)) {
                format = AtomToString(imageFormat);
                return true;
            }
        }

        return false;
    }

    bool UltraCanvasLinuxClipboard::WriteImageToClipboard(Atom selection, const std::vector<uint8_t>& imageData, const std::string& format) {
        if (imageData.empty()) {
            LogError("WriteImageToClipboard", "Empty image data provided");
            return false;
        }

        Atom targetAtom = StringToAtom(format, true);
        if (targetAtom == None) {
            LogError("WriteImageToClipboard", "Invalid format: " + format);
            return false;
        }

        return WriteClipboardData(selection, targetAtom, imageData);
    }

// ===== FILE OPERATIONS =====
    bool UltraCanvasLinuxClipboard::ReadFilesFromClipboard(Atom selection, std::vector<std::string>& filePaths) {
        std::vector<uint8_t> data;
        std::string format;

        if (ReadClipboardData(selection, atomTextUriList, data, format)) {
            std::string uriList(reinterpret_cast<const char*>(data.data()), data.size());

            // Parse URI list (one URI per line)
            std::istringstream stream(uriList);
            std::string line;

            while (std::getline(stream, line)) {
                if (line.empty() || line[0] == '#') continue; // Skip empty lines and comments

                // Remove trailing whitespace
                while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
                    line.pop_back();
                }

                // Convert file:// URI to path
                if (line.substr(0, 7) == "file://") {
                    std::string path = line.substr(7);
                    // Simple URL decode (replace %20 with space, etc.)
                    size_t pos = 0;
                    while ((pos = path.find("%20", pos)) != std::string::npos) {
                        path.replace(pos, 3, " ");
                        pos += 1;
                    }
                    filePaths.push_back(path);
                }
            }

            return !filePaths.empty();
        }

        return false;
    }

    bool UltraCanvasLinuxClipboard::WriteFilesToClipboard(Atom selection, const std::vector<std::string>& filePaths) {
        if (filePaths.empty()) {
            LogError("WriteFilesToClipboard", "Empty file list provided");
            return false;
        }

        // Create URI list
        std::string uriList;
        for (const std::string& path : filePaths) {
            uriList += "file://" + path + "\n";
        }

        std::vector<uint8_t> data(uriList.begin(), uriList.end());
        return WriteClipboardData(selection, atomTextUriList, data);
    }

// ===== CORE SELECTION HANDLING =====
    bool UltraCanvasLinuxClipboard::ReadClipboardData(Atom selection, Atom target, std::vector<uint8_t>& data, std::string& format) {
        if (!display || !window) return false;

        // Clear previous results
        selectionData.clear();
        selectionFormat.clear();
        selectionReady = false;

        // Create a unique property name for this request
        Atom property = XInternAtom(display, "ULTRACANVAS_SELECTION_DATA", False);

        // Request the selection
        XConvertSelection(display, selection, target, property, window, CurrentTime);
        XFlush(display);

        // Wait for the response
        if (WaitForSelectionNotify(data, format)) {
            return !data.empty();
        }

        return false;
    }

    bool UltraCanvasLinuxClipboard::WriteClipboardData(Atom selection, Atom target, const std::vector<uint8_t>& data) {
        if (!display || !window) return false;

        if (data.size() > MAX_CLIPBOARD_SIZE) {
            LogError("WriteClipboardData", "Data too large: " + std::to_string(data.size()) + " bytes");
            return false;
        }

        // Store the data for later retrieval
        selectionData = data;
        selectionFormat = AtomToString(target);

        // Set ourselves as the selection owner
        XSetSelectionOwner(display, selection, window, CurrentTime);
        XFlush(display);

        // Verify we own the selection
        Window owner = XGetSelectionOwner(display, selection);
        if (owner != window) {
            LogError("WriteClipboardData", "Failed to acquire selection ownership");
            return false;
        }

        return true;
    }

    bool UltraCanvasLinuxClipboard::WaitForSelectionNotify(std::vector<uint8_t>& data, std::string& format) {
        // Use the unified event processing function with timeout for selection notify
        if (ProcessSelectionEventsWithTimeout(SELECTION_TIMEOUT_MS, true)) {
            data = selectionData;
            format = selectionFormat;
            return !data.empty();
        }

        LogError("WaitForSelectionNotify", "Timeout waiting for selection");
        return false;
    }

    void UltraCanvasLinuxClipboard::ProcessSelectionEvents() {
        // Use the unified event processing function for immediate processing
        ProcessSelectionEventsWithTimeout(100, false); // 100ms for immediate requests
    }

    bool UltraCanvasLinuxClipboard::ProcessSelectionEventsWithTimeout(int timeoutMs, bool waitForSelectionReady) {
        XFlush(display);

        auto startTime = std::chrono::steady_clock::now();

        while (true) {
            // Check for timeout
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime);
            if (elapsed.count() > timeoutMs) {
                return false; // Timeout reached
            }

            // If waiting for selection ready and it's ready, exit successfully
            if (waitForSelectionReady && selectionReady) {
                return true;
            }

            // Process pending events
            if (XPending(display) > 0) {
                XEvent event;
                XNextEvent(display, &event);
                if (event.xany.window == window) {
                    HandleSelectionEvent(event);

                    // If we were waiting for selection ready and now it's ready, exit
                    if (waitForSelectionReady && selectionReady) {
                        return true;
                    }
                }
            } else {
                // No events pending
                if (!waitForSelectionReady) {
                    // For immediate processing, exit when no more events
                    return true;
                }

                // Brief sleep to avoid busy waiting
                usleep(1000); // 1ms
            }
        }
    }

// ===== EVENT HANDLING =====
    void UltraCanvasLinuxClipboard::HandleSelectionEvent(const XEvent& event) {
        switch (event.type) {
            case SelectionRequest:
                HandleSelectionRequest(event.xselectionrequest);
                break;

            case SelectionClear:
                HandleSelectionClear(event.xselectionclear);
                break;

            case SelectionNotify:
                HandleSelectionNotify(event.xselection);
                break;

            default:
                // Not a selection event, ignore
                break;
        }
    }

    void UltraCanvasLinuxClipboard::HandleSelectionRequest(const XSelectionRequestEvent& request) {
        std::cout << "UltraCanvas: Received SelectionRequest for "
                  << AtomToString(request.target) << " on "
                  << AtomToString(request.selection) << std::endl;

        XSelectionEvent response;
        response.type = SelectionNotify;
        response.display = request.display;
        response.requestor = request.requestor;
        response.selection = request.selection;
        response.target = request.target;
        response.property = None; // Will be set if successful
        response.time = request.time;

        // Check if we own this selection
        bool weOwnIt = ((request.selection == atomClipboard && ownsClipboard) ||
                        (request.selection == atomPrimary && ownsPrimary));

        if (!weOwnIt) {
            std::cout << "UltraCanvas: We don't own the requested selection, ignoring" << std::endl;
            XSendEvent(request.display, request.requestor, False, 0, (XEvent*)&response);
            XFlush(request.display);
            return;
        }

        // Handle different target types
        if (request.target == atomTargets) {
            // Client is asking what formats we support
            Atom targets[] = {
                    atomTargets,
                    atomUtf8String,
                    atomString,
                    atomText,
                    atomTextPlain,
                    atomTextPlainUtf8
            };

            XChangeProperty(request.display, request.requestor, request.property,
                            XA_ATOM, 32, PropModeReplace,
                            (unsigned char*)targets, sizeof(targets) / sizeof(Atom));

            response.property = request.property;
            std::cout << "UltraCanvas: Responded with supported targets" << std::endl;
        }
        else if (IsTextFormat(request.target)) {
            // Client wants text data
            const char* data = clipboardTextData.c_str();
            int dataLen = clipboardTextData.length();

            XChangeProperty(request.display, request.requestor, request.property,
                            request.target, 8, PropModeReplace,
                            (unsigned char*)data, dataLen);

            response.property = request.property;
            std::cout << "UltraCanvas: Provided text data (" << dataLen << " bytes)" << std::endl;
        }
        else if (IsImageFormat(request.target) || IsFileFormat(request.target)) {
            // Handle image or file data if we have it stored
            if (!selectionData.empty()) {
                XChangeProperty(request.display, request.requestor, request.property,
                                request.target, 8, PropModeReplace,
                                selectionData.data(), selectionData.size());

                response.property = request.property;
                std::cout << "UltraCanvas: Provided binary data (" << selectionData.size() << " bytes)" << std::endl;
            } else {
                std::cout << "UltraCanvas: No binary data available for request" << std::endl;
            }
        }
        else {
            std::cout << "UltraCanvas: Unsupported target format: "
                      << AtomToString(request.target) << std::endl;
        }

        // Send the response
        XSendEvent(request.display, request.requestor, False, 0, (XEvent*)&response);
        XFlush(request.display);
    }

    void UltraCanvasLinuxClipboard::HandleSelectionClear(const XSelectionClearEvent& clear) {
        std::cout << "UltraCanvas: Lost ownership of "
                  << AtomToString(clear.selection) << std::endl;

        if (clear.selection == atomClipboard) {
            ownsClipboard = false;
        } else if (clear.selection == atomPrimary) {
            ownsPrimary = false;
        }

        // If we've lost all selections, clear our data
        if (!ownsClipboard && !ownsPrimary) {
            clipboardTextData.clear();
            selectionData.clear();
            std::cout << "UltraCanvas: Cleared clipboard data (lost all ownership)" << std::endl;
        }
    }

    void UltraCanvasLinuxClipboard::HandleSelectionNotify(const XSelectionEvent& notify) {
        // This handles responses to our selection requests (when reading)
        if (notify.property != None) {
            // Read the property data
            Atom actualType;
            int actualFormat;
            unsigned long itemCount, bytesAfter;
            unsigned char* data = nullptr;

            int result = XGetWindowProperty(display, window, notify.property,
                                            0, 0x1fffffff, True, AnyPropertyType,
                                            &actualType, &actualFormat,
                                            &itemCount, &bytesAfter, &data);

            if (result == Success && data) {
                selectionData.assign(data, data + itemCount);
                selectionFormat = AtomToString(actualType);
                selectionReady = true;

                XFree(data);
                std::cout << "UltraCanvas: Received selection data ("
                          << itemCount << " bytes, format: " << selectionFormat << ")" << std::endl;
            } else {
                std::cerr << "UltraCanvas: Failed to read selection property" << std::endl;
                selectionReady = true; // Mark as ready even if failed
            }
        } else {
            std::cout << "UltraCanvas: Selection request was denied" << std::endl;
            selectionReady = true;
        }
    }

// ===== UTILITY FUNCTIONS =====
    std::string UltraCanvasLinuxClipboard::AtomToString(Atom atom) {
        if (atom == None) return "None";

        char* name = XGetAtomName(display, atom);
        if (name) {
            std::string result(name);
            XFree(name);
            return result;
        }

        return "Unknown";
    }

    Atom UltraCanvasLinuxClipboard::StringToAtom(const std::string& str, bool createIfMissing) {
        if (str.empty() || !display) return None;

        // XInternAtom: only_if_exists = True means don't create, False means create if missing
        // So we need to invert the createIfMissing logic
        return XInternAtom(display, str.c_str(), createIfMissing ? False : True);
    }

    std::string UltraCanvasLinuxClipboard::FormatToMimeType(const std::string& format) {
        if (format == "UTF8_STRING") return "text/plain;charset=utf-8";
        if (format == "STRING") return "text/plain";
        if (format == "TEXT") return "text/plain";
        if (format == "image/png") return "image/png";
        if (format == "image/jpeg") return "image/jpeg";
        if (format == "image/bmp") return "image/bmp";
        if (format == "text/uri-list") return "text/uri-list";
        return format;
    }

    std::string UltraCanvasLinuxClipboard::MimeTypeToFormat(const std::string& mimeType) {
        if (mimeType == "text/plain;charset=utf-8") return "UTF8_STRING";
        if (mimeType == "text/plain") return "STRING";
        return mimeType;
    }

    bool UltraCanvasLinuxClipboard::IsTextFormat(Atom target) {
        return target == atomText || target == atomUtf8String ||
               target == atomString || target == atomTextPlain ||
               target == atomTextPlainUtf8;
    }

    bool UltraCanvasLinuxClipboard::IsImageFormat(Atom target) {
        return target == atomImagePng || target == atomImageJpeg || target == atomImageBmp;
    }

    bool UltraCanvasLinuxClipboard::IsFileFormat(Atom target) {
        return target == atomTextUriList;
    }

    void UltraCanvasLinuxClipboard::LogError(const std::string& operation, const std::string& details) {
        std::cerr << "UltraCanvas Clipboard Error in " << operation;
        if (!details.empty()) {
            std::cerr << ": " << details;
        }
        std::cerr << std::endl;
    }

    void UltraCanvasLinuxClipboard::LogInfo(const std::string& operation, const std::string& details) {
        std::cout << "UltraCanvas Clipboard Info in " << operation;
        if (!details.empty()) {
            std::cout << ": " << details;
        }
        std::cout << std::endl;
    }

    bool UltraCanvasLinuxClipboard::CheckXError() {
        // Simple X error checking - could be enhanced
        XSync(display, False);
        return true; // Assume no errors for now
    }

// ===== GLOBAL CLIPBOARD FUNCTIONS =====
    static std::unique_ptr<UltraCanvasLinuxClipboard> g_clipboardInstance;

    bool InitializeLinuxClipboard() {
        if (!g_clipboardInstance) {
            g_clipboardInstance = std::make_unique<UltraCanvasLinuxClipboard>();
            return g_clipboardInstance->Initialize();
        }
        return true;
    }

    void CleanupLinuxClipboard() {
        if (g_clipboardInstance) {
            g_clipboardInstance->Shutdown();
            g_clipboardInstance.reset();
        }
    }

    UltraCanvasLinuxClipboard* GetLinuxClipboard() {
        return g_clipboardInstance.get();
    }

    void ProcessClipboardEvents() {
        if (g_clipboardInstance) {
            g_clipboardInstance->ProcessEvents();
        }
    }

} // namespace UltraCanvas