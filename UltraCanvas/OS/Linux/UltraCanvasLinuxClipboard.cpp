// UltraCanvasLinuxClipboard.cpp
// X11-specific clipboard implementation for Linux
// Version: 1.0.0
// Last Modified: 2025-08-13
// Author: UltraCanvas Framework

#include "UltraCanvasLinuxClipboard.h"
#include "UltraCanvasApplication.h"
#include <iostream>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <unistd.h>
#include <sys/select.h>

namespace UltraCanvas {

// ===== CONSTRUCTOR & DESTRUCTOR =====
UltraCanvasLinuxClipboard::UltraCanvasLinuxClipboard()
    : display(nullptr)
    , window(0)
    , clipboardChanged(false)
    , selectionReady(false) {
    
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
    std::cout << "UltraCanvas: Linux clipboard shut down" << std::endl;
}

bool UltraCanvasLinuxClipboard::GetDisplayFromApplication() {
    // Get the display from UltraCanvasLinuxApplication
    // This assumes the application is already initialized
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
    XSelectInput(display, helperWindow, PropertyChangeMask | SelectionNotify | SelectionRequest);
    
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

// ===== TEXT OPERATIONS =====
bool UltraCanvasLinuxClipboard::ReadTextFromClipboard(Atom selection, std::string& text) {
    // Try UTF8_STRING first, then STRING as fallback
    std::vector<uint8_t> data;
    std::string format;
    
    if (ReadClipboardData(selection, atomUtf8String, data, format) ||
        ReadClipboardData(selection, atomString, data, format) ||
        ReadClipboardData(selection, atomTextPlain, data, format)) {
        
        text = std::string(reinterpret_cast<const char*>(data.data()), data.size());
        return true;
    }
    
    return false;
}

bool UltraCanvasLinuxClipboard::WriteTextToClipboard(Atom selection, const std::string& text) {
    std::vector<uint8_t> data(text.begin(), text.end());
    return WriteClipboardData(selection, atomUtf8String, data);
}

// ===== IMAGE OPERATIONS =====
bool UltraCanvasLinuxClipboard::ReadImageFromClipboard(Atom selection, std::vector<uint8_t>& imageData, std::string& format) {
    // Try different image formats
    std::vector<Atom> imageFormats = {atomImagePng, atomImageJpeg, atomImageBmp};
    
    for (Atom imageFormat : imageFormats) {
        if (ReadClipboardData(selection, imageFormat, imageData, format)) {
            format = AtomToString(imageFormat);
            return true;
        }
    }
    
    return false;
}

bool UltraCanvasLinuxClipboard::WriteImageToClipboard(Atom selection, const std::vector<uint8_t>& imageData, const std::string& format) {
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
            
            // Convert file:// URI to path
            if (line.substr(0, 7) == "file://") {
                std::string path = line.substr(7);
                // URL decode if needed
                filePaths.push_back(path);
            }
        }
        
        return !filePaths.empty();
    }
    
    return false;
}

bool UltraCanvasLinuxClipboard::WriteFilesToClipboard(Atom selection, const std::vector<std::string>& filePaths) {
    if (filePaths.empty()) return false;
    
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
    
    // Request the selection
    XConvertSelection(display, selection, target, target, window, CurrentTime);
    XFlush(display);
    
    // Wait for SelectionNotify event
    return WaitForSelectionNotify(data, format);
}

bool UltraCanvasLinuxClipboard::WriteClipboardData(Atom selection, Atom target, const std::vector<uint8_t>& data) {
    if (!display || !window) return false;
    
    // Set ourselves as the selection owner
    XSetSelectionOwner(display, selection, window, CurrentTime);
    
    // Verify we own the selection
    Window owner = XGetSelectionOwner(display, selection);
    if (owner != window) {
        LogError("WriteClipboardData", "Failed to acquire selection ownership");
        return false;
    }
    
    // Store the data for later retrieval
    selectionData = data;
    selectionFormat = AtomToString(target);
    
    return true;
}

bool UltraCanvasLinuxClipboard::WaitForSelectionNotify(std::vector<uint8_t>& data, std::string& format) {
    selectionReady = false;
    
    auto startTime = std::chrono::steady_clock::now();
    
    while (!selectionReady) {
        // Check for timeout
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime);
        if (elapsed.count() > SELECTION_TIMEOUT_MS) {
            LogError("WaitForSelectionNotify", "Timeout waiting for selection");
            return false;
        }
        
        // Process X events
        if (XPending(display) > 0) {
            XEvent event;
            XNextEvent(display, &event);
            
            if (event.type == SelectionNotify && event.xselection.requestor == window) {
                if (ProcessSelectionNotify(event)) {
                    break;
                }
            }
        }
        
        // Small delay to avoid busy waiting
        usleep(1000); // 1ms
    }
    
    data = selectionData;
    format = selectionFormat;
    return true;
}

bool UltraCanvasLinuxClipboard::ProcessSelectionNotify(XEvent& event) {
    XSelectionEvent* selEvent = &event.xselection;
    
    if (selEvent->property == None) {
        LogError("ProcessSelectionNotify", "Selection conversion failed");
        selectionReady = true;
        return false;
    }
    
    // Get the property data
    Atom actualType;
    int actualFormat;
    unsigned long numItems;
    unsigned long bytesAfter;
    unsigned char* prop = nullptr;
    
    int result = XGetWindowProperty(
        display, window, selEvent->property,
        0, MAX_CLIPBOARD_SIZE / 4, False, AnyPropertyType,
        &actualType, &actualFormat, &numItems, &bytesAfter, &prop
    );
    
    if (result != Success || !prop) {
        LogError("ProcessSelectionNotify", "Failed to get window property");
        selectionReady = true;
        return false;
    }
    
    // Calculate data size
    size_t dataSize = numItems * (actualFormat / 8);
    
    // Copy the data
    selectionData.clear();
    selectionData.resize(dataSize);
    std::memcpy(selectionData.data(), prop, dataSize);
    
    selectionFormat = AtomToString(actualType);
    
    // Clean up
    XFree(prop);
    XDeleteProperty(display, window, selEvent->property);
    
    selectionReady = true;
    return true;
}

// ===== UTILITY METHODS =====
std::string UltraCanvasLinuxClipboard::AtomToString(Atom atom) {
    if (atom == None) return "";
    
    char* atomName = XGetAtomName(display, atom);
    if (!atomName) return "";
    
    std::string result(atomName);
    XFree(atomName);
    return result;
}

Atom UltraCanvasLinuxClipboard::StringToAtom(const std::string& str, bool createIfMissing) {
    if (str.empty()) return None;
    return XInternAtom(display, str.c_str(), createIfMissing ? False : True);
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

} // namespace UltraCanvas

