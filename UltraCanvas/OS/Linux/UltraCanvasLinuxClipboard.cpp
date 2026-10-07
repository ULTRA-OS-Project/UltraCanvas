// OS/Linux/UltraCanvasLinuxClipboard.cpp
// X11-specific clipboard implementation for Linux
// Version: 1.1.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework

#include "UltraCanvasLinuxClipboard.h"
#include "UltraCanvasApplication.h"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <unistd.h>
#include <sys/select.h>
#include <poll.h>
#include <cerrno>
#include "UltraCanvasDebug.h"
#ifdef ULTRACANVAS_HAS_XFIXES
#include <X11/extensions/Xfixes.h>
#endif

namespace UltraCanvas {

// ===== CONSTRUCTOR & DESTRUCTOR =====
    UltraCanvasLinuxClipboard* UltraCanvasLinuxClipboard::instance = nullptr;

    UltraCanvasLinuxClipboard::UltraCanvasLinuxClipboard()
            : display(nullptr)
            , window(0)
            , clipboardChanged(false)
            , selectionReady(false) {

        lastChangeCheck = std::chrono::steady_clock::now();
        instance = this;
    }

    UltraCanvasLinuxClipboard::~UltraCanvasLinuxClipboard() {
        Shutdown();
        instance = nullptr;
    }

// ===== INITIALIZATION =====
    bool UltraCanvasLinuxClipboard::Initialize() {
        debugOutput << "UltraCanvas: Initializing Linux clipboard..." << std::endl;

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

        debugOutput << "UltraCanvas: Linux clipboard initialized successfully" << std::endl;
        return true;
    }

    void UltraCanvasLinuxClipboard::Shutdown() {
        StopChangeListener();
        if (display && window) {
            XDestroyWindow(display, window);
            window = 0;
        }

        display = nullptr;
        debugOutput << "UltraCanvas: Linux clipboard shut down" << std::endl;
    }

    bool UltraCanvasLinuxClipboard::GetDisplayFromApplication() {
        // Get the display from UltraCanvasLinuxApplication
        // This assumes the application is already initialized
        UltraCanvasApplication* app = UltraCanvasApplication::GetInstance();
        if (!app) {
            debugOutput << "UltraCanvas: No Linux application instance found" << std::endl;
            return false;
        }

        display = app->GetDisplay();
        if (!display) {
            debugOutput << "UltraCanvas: No X11 display available" << std::endl;
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
        atomGnomeCopiedFiles = XInternAtom(display, "x-special/gnome-copied-files", False);
        atomKdeCutSelection = XInternAtom(display, "application/x-kde-cutselection", False);
        atomPasswordManagerHint = XInternAtom(display, "x-kde-passwordManagerHint", False);
    }

// ===== CLIPBOARD OPERATIONS =====
    bool UltraCanvasLinuxClipboard::GetClipboardText(std::string& text) {
        return ReadTextFromClipboard(atomClipboard, text);
    }

    bool UltraCanvasLinuxClipboard::SetClipboardText(const std::string& text) {
        debugOutput << "UltraCanvas: Setting clipboard text: \"" << text.substr(0, 50) << "...\"" << std::endl;

        bool success = WriteTextToClipboard(atomClipboard, text);
        if (success) {
            debugOutput << "UltraCanvas: Successfully acquired ownership of CLIPBOARD" << std::endl;
        } else {
            debugOutput << "UltraCanvas: Failed to set clipboard text" << std::endl;
        }

        return success;
    }

    // Not logged, unlike SetClipboardText: the text is a password.
    bool UltraCanvasLinuxClipboard::SetClipboardSecretText(const std::string& text) {
        return WriteTextToClipboard(atomClipboard, text, true);
    }

    // The marker is a target of its own whose content is "secret". Our own
    // copy is answered from what we offer; another program's by asking it for
    // that target, which an owner without the marker refuses at once.
    bool UltraCanvasLinuxClipboard::IsClipboardMarkedSecret() {
        if (!display || !window) return false;
        std::vector<uint8_t> hint;
        if (ownsClipboard && XGetSelectionOwner(display, atomClipboard) == window) {
            for (const auto& offer : offeredTargets) {
                if (offer.first == atomPasswordManagerHint) hint = offer.second;
            }
        } else {
            std::string format;
            if (!ReadClipboardData(atomClipboard, atomPasswordManagerHint, hint, format)) return false;
        }
        std::string value(hint.begin(), hint.end());
        while (!value.empty() && (value.back() == '\0' || std::isspace(static_cast<unsigned char>(value.back())))) {
            value.pop_back();
        }
        return value == "secret";
    }

    // text/html next to the text flavours: browsers, office suites and mail
    // clients paste the HTML, everything else the text.
    bool UltraCanvasLinuxClipboard::SetClipboardHtml(const std::string& html, const std::string& plainText) {
        if (!display) return false;
        std::vector<uint8_t> text(plainText.begin(), plainText.end());
        std::vector<std::pair<Atom, std::vector<uint8_t>>> offers;
        offers.emplace_back(XInternAtom(display, "text/html", False), std::vector<uint8_t>(html.begin(), html.end()));
        offers.emplace_back(atomUtf8String, text);
        offers.emplace_back(atomTextPlainUtf8, text);
        offers.emplace_back(atomTextPlain, text);
        offers.emplace_back(atomString, text);
        offers.emplace_back(atomText, std::move(text));
        return WriteClipboardTargets(atomClipboard, std::move(offers));
    }

    bool UltraCanvasLinuxClipboard::GetClipboardHtml(std::string& html) {
        if (!display) return false;
        std::vector<uint8_t> data;
        std::string format;
        if (!ReadClipboardData(atomClipboard, XInternAtom(display, "text/html", False), data, format) || data.empty()) {
            return false;
        }
        // Firefox offers it as UTF-16 with a byte order mark.
        if (data.size() >= 2 && ((data[0] == 0xFF && data[1] == 0xFE) || (data[0] == 0xFE && data[1] == 0xFF))) {
            const bool little = data[0] == 0xFF;
            html.clear();
            for (size_t i = 2; i + 1 < data.size(); i += 2) {
                uint32_t unit = little ? (data[i] | (data[i + 1] << 8)) : ((data[i] << 8) | data[i + 1]);
                if (unit >= 0xD800 && unit <= 0xDBFF && i + 3 < data.size()) {
                    const uint32_t low = little ? (data[i + 2] | (data[i + 3] << 8)) : ((data[i + 2] << 8) | data[i + 3]);
                    if (low >= 0xDC00 && low <= 0xDFFF) {
                        unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                        i += 2;
                    }
                }
                if (unit == 0) break;
                if (unit < 0x80) html += static_cast<char>(unit);
                else if (unit < 0x800) {
                    html += static_cast<char>(0xC0 | (unit >> 6));
                    html += static_cast<char>(0x80 | (unit & 0x3F));
                } else if (unit < 0x10000) {
                    html += static_cast<char>(0xE0 | (unit >> 12));
                    html += static_cast<char>(0x80 | ((unit >> 6) & 0x3F));
                    html += static_cast<char>(0x80 | (unit & 0x3F));
                } else {
                    html += static_cast<char>(0xF0 | (unit >> 18));
                    html += static_cast<char>(0x80 | ((unit >> 12) & 0x3F));
                    html += static_cast<char>(0x80 | ((unit >> 6) & 0x3F));
                    html += static_cast<char>(0x80 | (unit & 0x3F));
                }
            }
        } else {
            html.assign(reinterpret_cast<const char*>(data.data()), data.size());
            while (!html.empty() && html.back() == '\0') html.pop_back();
        }
        return !html.empty();
    }

    bool UltraCanvasLinuxClipboard::GetClipboardImage(std::vector<uint8_t>& imageData, std::string& format) {
        return ReadImageFromClipboard(atomClipboard, imageData, format);
    }

    bool UltraCanvasLinuxClipboard::SetClipboardImage(const std::vector<uint8_t>& imageData, const std::string& format) {
        return WriteImageToClipboard(atomClipboard, imageData, format);
    }

    bool UltraCanvasLinuxClipboard::GetClipboardFiles(std::vector<std::string>& filePaths) {
        bool cutOperation = false;
        return ReadFilesFromClipboard(atomClipboard, filePaths, cutOperation);
    }

    bool UltraCanvasLinuxClipboard::SetClipboardFiles(const std::vector<std::string>& filePaths) {
        return WriteFilesToClipboard(atomClipboard, filePaths, false);
    }

    bool UltraCanvasLinuxClipboard::GetClipboardFiles(std::vector<std::string>& filePaths, bool& cutOperation) {
        return ReadFilesFromClipboard(atomClipboard, filePaths, cutOperation);
    }

    bool UltraCanvasLinuxClipboard::SetClipboardFiles(const std::vector<std::string>& filePaths, bool cutOperation) {
        return WriteFilesToClipboard(atomClipboard, filePaths, cutOperation);
    }

// ===== MONITORING =====
    // XFixes reports every new owner of CLIPBOARD (and the owner going away)
    // on a connection of its own, so this thread never touches the
    // application's display. It only counts; HasClipboardChanged reads the count.
    void UltraCanvasLinuxClipboard::StartChangeListener() {
#ifdef ULTRACANVAS_HAS_XFIXES
        if (!display) return;
        Display* d = XOpenDisplay(DisplayString(display));
        int eventBase = 0, errorBase = 0;
        if (!d || !XFixesQueryExtension(d, &eventBase, &errorBase) || ::pipe(changeWakePipe) != 0) {
            if (d) XCloseDisplay(d);
            debugOutput << "UltraCanvas: no XFixes; clipboard changes are noticed by their text" << std::endl;
            return;
        }
        XFixesSelectSelectionInput(d, DefaultRootWindow(d), XInternAtom(d, "CLIPBOARD", False),
                                   XFixesSetSelectionOwnerNotifyMask |
                                   XFixesSelectionWindowDestroyNotifyMask |
                                   XFixesSelectionClientCloseNotifyMask);
        XFlush(d);
        changeDisplay = d;
        changeListenerAlive = true;
        const int wakeFd = changeWakePipe[0];
        changeThread = std::thread([this, d, eventBase, wakeFd]() {
            for (;;) {
                while (XPending(d) > 0) {
                    XEvent event;
                    XNextEvent(d, &event);
                    if (event.type == eventBase + XFixesSelectionNotify) ownerChanges.fetch_add(1);
                }
                pollfd fds[2];
                fds[0].fd = ConnectionNumber(d);
                fds[0].events = POLLIN;
                fds[0].revents = 0;
                fds[1].fd = wakeFd;
                fds[1].events = POLLIN;
                fds[1].revents = 0;
                const int n = ::poll(fds, 2, -1);
                if (n < 0) {
                    if (errno == EINTR) continue;
                    break;
                }
                if (fds[1].revents & POLLIN) break;
                if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) break;
            }
            changeListenerAlive = false;
        });
#endif
    }

    void UltraCanvasLinuxClipboard::StopChangeListener() {
        if (changeThread.joinable()) {
            const char byte = 1;
            if (::write(changeWakePipe[1], &byte, 1) < 0) {
                // The thread is gone already; join below returns at once.
            }
            changeThread.join();
        }
        if (changeDisplay) XCloseDisplay(changeDisplay);
        changeDisplay = nullptr;
        for (int& fd : changeWakePipe) {
            if (fd >= 0) ::close(fd);
            fd = -1;
        }
        changeListenerAlive = false;
    }

    bool UltraCanvasLinuxClipboard::HasClipboardOwner() {
        return display && XGetSelectionOwner(display, atomClipboard) != None;
    }

    bool UltraCanvasLinuxClipboard::HasClipboardChanged() {
        // Whoever asks is watching the clipboard: start listening for owners.
        if (!changeListenerTried) {
            changeListenerTried = true;
            StartChangeListener();
        }
        if (changeListenerAlive) {
            const uint64_t changes = ownerChanges.load();
            if (changes != ownerChangesSeen) {
                ownerChangesSeen = changes;
                clipboardChanged = true;
            }
            return clipboardChanged;
        }

        // Check if enough time has passed since last check
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastChangeCheck);

        if (elapsed.count() < 100) { // Check at most every 100ms
            return clipboardChanged;
        }

        lastChangeCheck = now;

        // Get current clipboard text
        std::string currentText;
        if (GetClipboardText(currentText)) {
            if (currentText != lastClipboardText) {
                lastClipboardText = currentText;
                clipboardChanged = true;
                debugOutput << "UltraCanvas: Received selection data (" << currentText.length()
                          << " bytes, format: UTF8_STRING)" << std::endl;
            }
        }

        return clipboardChanged;
    }

    void UltraCanvasLinuxClipboard::ResetChangeState() {
        clipboardChanged = false;
    }

// ===== FORMAT DETECTION =====
    std::vector<std::string> UltraCanvasLinuxClipboard::GetAvailableFormats() {
        std::vector<std::string> formats;

        // Try to get the TARGETS atom to see what formats are available
        std::vector<uint8_t> data;
        std::string format;

        if (ReadClipboardData(atomClipboard, atomTargets, data, format)) {
            // Parse the targets list
            size_t atomCount = data.size() / sizeof(Atom);
            Atom* atoms = reinterpret_cast<Atom*>(data.data());

            for (size_t i = 0; i < atomCount; ++i) {
                std::string atomName = AtomToString(atoms[i]);
                if (!atomName.empty()) {
                    formats.push_back(atomName);
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

    bool UltraCanvasLinuxClipboard::WriteTextToClipboard(Atom selection, const std::string& text, bool secret) {
        std::vector<uint8_t> data(text.begin(), text.end());
        // One string, several targets: requestors ask for whichever text
        // flavour they prefer.
        std::vector<std::pair<Atom, std::vector<uint8_t>>> offers;
        offers.emplace_back(atomUtf8String, data);
        offers.emplace_back(atomTextPlainUtf8, data);
        offers.emplace_back(atomTextPlain, data);
        offers.emplace_back(atomString, data);
        offers.emplace_back(atomText, std::move(data));
        if (secret) {
            const std::string hint = "secret";
            offers.emplace_back(atomPasswordManagerHint, std::vector<uint8_t>(hint.begin(), hint.end()));
        }
        return WriteClipboardTargets(selection, std::move(offers));
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
    std::string UltraCanvasLinuxClipboard::EncodeFileUri(const std::string& path) {
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

    std::string UltraCanvasLinuxClipboard::DecodeFileUri(const std::string& uri) {
        std::string path;
        if (uri.compare(0, 7, "file://") == 0) {
            path = uri.substr(7);
        } else if (uri.compare(0, 5, "file:") == 0) {
            path = uri.substr(5);
        } else if (!uri.empty() && uri[0] == '/') {
            path = uri;   // bare path (some apps put plain paths on the list)
        } else {
            return "";
        }

        // "file://localhost/path" → strip the host part.
        if (!path.empty() && path[0] != '/') {
            size_t slash = path.find('/');
            if (slash == std::string::npos) return "";
            path = path.substr(slash);
        }

        // Percent-decode (%20 → space, UTF-8 bytes, ...).
        std::string decoded;
        decoded.reserve(path.size());
        for (size_t i = 0; i < path.size(); ++i) {
            if (path[i] == '%' && i + 2 < path.size() &&
                std::isxdigit(static_cast<unsigned char>(path[i + 1])) &&
                std::isxdigit(static_cast<unsigned char>(path[i + 2]))) {
                char hexPair[3] = { path[i + 1], path[i + 2], '\0' };
                decoded += static_cast<char>(std::strtol(hexPair, nullptr, 16));
                i += 2;
            } else {
                decoded += path[i];
            }
        }
        return decoded;
    }

    std::vector<std::string> UltraCanvasLinuxClipboard::ParseUriListPaths(const std::string& uriList) {
        std::vector<std::string> paths;
        std::istringstream stream(uriList);
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            std::string path = DecodeFileUri(line);
            if (!path.empty()) paths.push_back(path);
        }
        return paths;
    }

    bool UltraCanvasLinuxClipboard::ReadFilesFromClipboard(Atom selection,
                                                           std::vector<std::string>& filePaths,
                                                           bool& cutOperation) {
        cutOperation = false;
        std::vector<uint8_t> data;
        std::string format;

        // Preferred: x-special/gnome-copied-files — carries the copy/cut verb
        // on the first line, then one URI per line.
        if (ReadClipboardData(selection, atomGnomeCopiedFiles, data, format) && !data.empty()) {
            std::string payload(reinterpret_cast<const char*>(data.data()), data.size());
            std::string verb = payload;
            size_t newline = payload.find('\n');
            std::string rest;
            if (newline != std::string::npos) {
                verb = payload.substr(0, newline);
                rest = payload.substr(newline + 1);
            } else {
                rest.clear();
            }
            if (!verb.empty() && verb.back() == '\r') verb.pop_back();
            // Only "copy" or "cut" is a verb. An owner that answers every
            // target with the same bytes (xclip) hands over a bare URI
            // list here, whose first line is a file.
            if (verb != "copy" && verb != "cut") {
                verb.clear();
                rest = payload;
            }
            cutOperation = (verb == "cut");
            filePaths = ParseUriListPaths(rest);
            if (!filePaths.empty()) return true;
            cutOperation = false;
        }

        // Generic: text/uri-list. The KDE cut marker rides along separately.
        data.clear();
        if (ReadClipboardData(selection, atomTextUriList, data, format) && !data.empty()) {
            std::string uriList(reinterpret_cast<const char*>(data.data()), data.size());
            filePaths = ParseUriListPaths(uriList);
            if (filePaths.empty()) return false;

            std::vector<uint8_t> cutData;
            std::string cutFormat;
            if (ReadClipboardData(selection, atomKdeCutSelection, cutData, cutFormat) &&
                !cutData.empty() && cutData[0] == '1') {
                cutOperation = true;
            }
            return true;
        }

        return false;
    }

    bool UltraCanvasLinuxClipboard::WriteFilesToClipboard(Atom selection,
                                                          const std::vector<std::string>& filePaths,
                                                          bool cutOperation) {
        if (filePaths.empty()) return false;

        // text/uri-list: CRLF-terminated, percent-encoded URIs.
        std::string uriListCrlf;
        // gnome-copied-files: verb line + LF-separated URIs.
        std::string gnomeList = cutOperation ? "cut" : "copy";
        // Plain-text fallback so the copy also pastes into editors/terminals.
        std::string plainPaths;
        for (const std::string& path : filePaths) {
            std::string uri = EncodeFileUri(path);
            uriListCrlf += uri + "\r\n";
            gnomeList += "\n" + uri;
            if (!plainPaths.empty()) plainPaths += "\n";
            plainPaths += path;
        }

        std::vector<std::pair<Atom, std::vector<uint8_t>>> offers;
        offers.emplace_back(atomTextUriList,
                            std::vector<uint8_t>(uriListCrlf.begin(), uriListCrlf.end()));
        offers.emplace_back(atomGnomeCopiedFiles,
                            std::vector<uint8_t>(gnomeList.begin(), gnomeList.end()));
        const char* kdeFlag = cutOperation ? "1" : "0";
        offers.emplace_back(atomKdeCutSelection,
                            std::vector<uint8_t>(kdeFlag, kdeFlag + 1));
        offers.emplace_back(atomUtf8String,
                            std::vector<uint8_t>(plainPaths.begin(), plainPaths.end()));
        offers.emplace_back(atomString,
                            std::vector<uint8_t>(plainPaths.begin(), plainPaths.end()));

        return WriteClipboardTargets(selection, std::move(offers));
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
        std::vector<std::pair<Atom, std::vector<uint8_t>>> offers;
        offers.emplace_back(target, data);
        return WriteClipboardTargets(selection, std::move(offers));
    }

    bool UltraCanvasLinuxClipboard::WriteClipboardTargets(
            Atom selection,
            std::vector<std::pair<Atom, std::vector<uint8_t>>> offers) {
        if (!display || !window || offers.empty()) return false;

        // Set ourselves as the selection owner
        XSetSelectionOwner(display, selection, window, CurrentTime);

        // Verify we own the selection
        Window owner = XGetSelectionOwner(display, selection);
        if (owner != window) {
            LogError("WriteClipboardTargets", "Failed to acquire selection ownership");
            return false;
        }

        // Store the offers; SelectionRequest events are answered from here.
        offeredTargets = std::move(offers);

        if (selection == atomClipboard) {
            ownsClipboard = true;
        } else if (selection == atomPrimary) {
            ownsPrimary = true;
        }

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
                    if (HandleSelectionNotify(event.xselection)) {
                        break;
                    }
                } else if (event.type == SelectionRequest) {
                    // Handle selection requests from other applications
                    HandleSelectionEvent(event.xselectionrequest);
                }
            }

            // Small delay to avoid busy waiting
            usleep(1000); // 1ms
        }

        data = selectionData;
        format = selectionFormat;
        // A failed conversion (owner refused the target) also ends the wait —
        // it must not report the previous read's stale bytes as success.
        return !data.empty();
    }

    bool UltraCanvasLinuxClipboard::HandleSelectionNotify(const XSelectionEvent& selEvent) {
        if (selEvent.property == None) {
            // The owner has nothing in the requested target - an empty
            // clipboard, or text asked of an image. That is an answer, not
            // an error: a clipboard monitor asks every half second and would
            // otherwise fill the log with it.
            selectionData.clear();
            selectionFormat.clear();
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
                display, window, selEvent.property,
                0, MAX_CLIPBOARD_SIZE / 4, False, AnyPropertyType,
                &actualType, &actualFormat, &numItems, &bytesAfter, &prop
        );

        if (result != Success || !prop) {
            LogError("HandleSelectionNotify", "Failed to get window property");
            selectionData.clear();
            selectionFormat.clear();
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
        XDeleteProperty(display, window, selEvent.property);

        selectionReady = true;
        return true;
    }

// ===== CRITICAL FIX: HandleSelectionEvent Implementation =====
    bool UltraCanvasLinuxClipboard::HandleSelectionEvent(const XSelectionRequestEvent& request) {
        XSelectionEvent response;

        // Initialize response
        response.type = SelectionNotify;
        response.display = request.display;
        response.requestor = request.requestor;
        response.selection = request.selection;
        response.target = request.target;
        response.property = request.property;
        response.time = request.time;

        debugOutput << "UltraCanvas: Received SelectionRequest from window " << request.requestor
                  << " for target " << AtomToString(request.target) << std::endl;

        bool success = false;

        // Some (older) requestors leave property None: reply on the target atom.
        if (response.property == None) {
            response.property = request.target;
        }

        if (request.target == atomTargets) {
            // Client wants to know what targets we support
            std::vector<Atom> supportedTargets;
            supportedTargets.push_back(atomTargets);
            for (const auto& offer : offeredTargets) {
                supportedTargets.push_back(offer.first);
            }

            XChangeProperty(
                    display, request.requestor, response.property,
                    XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(supportedTargets.data()),
                    static_cast<int>(supportedTargets.size())
            );

            success = true;
            debugOutput << "UltraCanvas: Provided TARGETS list ("
                        << supportedTargets.size() << " targets)" << std::endl;

        } else {
            // Serve the payload registered for exactly this target; text
            // requests additionally fall back to any offered text flavour
            // (a requestor may ask for TEXT while we offered UTF8_STRING).
            const std::vector<uint8_t>* payload = nullptr;
            for (const auto& offer : offeredTargets) {
                if (offer.first == request.target) {
                    payload = &offer.second;
                    break;
                }
            }
            if (!payload && IsTextFormat(request.target)) {
                for (const auto& offer : offeredTargets) {
                    if (IsTextFormat(offer.first)) {
                        payload = &offer.second;
                        break;
                    }
                }
            }

            if (payload) {
                XChangeProperty(
                        display, request.requestor, response.property,
                        request.target, 8, PropModeReplace,
                        payload->data(), static_cast<int>(payload->size())
                );
                success = true;
                debugOutput << "UltraCanvas: Provided " << AtomToString(request.target)
                            << " data (" << payload->size() << " bytes)" << std::endl;
            } else {
                // Unsupported target or no data
                response.property = None;
                debugOutput << "UltraCanvas: Unsupported target "
                            << AtomToString(request.target)
                            << " or no data available" << std::endl;
            }
        }

        // Send response
        XSendEvent(display, request.requestor, False, NoEventMask, (XEvent*)&response);
        XFlush(display);

        return success;
    }

    void UltraCanvasLinuxClipboard::HandleSelectionClear(const XSelectionClearEvent & clear) {
        debugOutput << "UltraCanvas: Lost ownership of "
                  << AtomToString(clear.selection) << std::endl;

        if (clear.selection == atomClipboard) {
            ownsClipboard = false;
        } else if (clear.selection == atomPrimary) {
            ownsPrimary = false;
        }

        // If we've lost all selections, clear our data
        if (!ownsClipboard && !ownsPrimary) {
            clipboardTextData.clear();
            offeredTargets.clear();
            debugOutput << "UltraCanvas: Cleared clipboard data (lost all ownership)" << std::endl;
        }
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

    std::string UltraCanvasLinuxClipboard::FormatToMimeType(const std::string& format) {
        // Convert internal format names to MIME types
        if (format == "UTF8_STRING" || format == "STRING") return "text/plain";
        if (format == "image/png") return "image/png";
        if (format == "image/jpeg") return "image/jpeg";
        if (format == "text/uri-list") return "text/uri-list";

        return format; // Return as-is if no conversion needed
    }

    std::string UltraCanvasLinuxClipboard::MimeTypeToFormat(const std::string& mimeType) {
        // Convert MIME types to internal format names
        if (mimeType == "text/plain") return "UTF8_STRING";

        return mimeType; // Return as-is if no conversion needed
    }

    bool UltraCanvasLinuxClipboard::IsTextFormat(Atom target) {
        return (target == atomUtf8String ||
                target == atomString ||
                target == atomTextPlain ||
                target == atomTextPlainUtf8 ||
                target == atomText);
    }

    bool UltraCanvasLinuxClipboard::IsImageFormat(Atom target) {
        return (target == atomImagePng ||
                target == atomImageJpeg ||
                target == atomImageBmp);
    }

    bool UltraCanvasLinuxClipboard::IsFileFormat(Atom target) {
        return (target == atomTextUriList);
    }

    void UltraCanvasLinuxClipboard::LogError(const std::string& operation, const std::string& details) {
        debugOutput << "UltraCanvas Clipboard Error [" << operation << "]: " << details << std::endl;
    }

    bool UltraCanvasLinuxClipboard::CheckXError() {
        // Simple X error checking - could be enhanced
        XSync(display, False);
        return true; // For now, assume success
    }

    void UltraCanvasLinuxClipboard::ProcessClipboardEvent(const XEvent& event) {
        if (!instance || !instance->display) return;

        if (event.type == SelectionRequest && event.xselectionrequest.owner == instance->window) {
            instance->HandleSelectionEvent(event.xselectionrequest);
        } else if (event.type == SelectionNotify && event.xselection.requestor == instance->window) {
            instance->HandleSelectionNotify(event.xselection);
        } else if (event.type == SelectionClear) {
            instance->HandleSelectionClear(event.xselectionclear);
        }
    }
} // namespace UltraCanvas