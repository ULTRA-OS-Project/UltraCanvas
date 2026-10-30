// OS/Linux/Wayland/UltraCanvasWaylandClipboard.cpp
// The clipboard on Wayland. See the header.
// Version: 0.1.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework

#include "UltraCanvasWaylandClipboard.h"
#include "UltraCanvasWaylandApplication.h"
#include "../UltraCanvasLinuxUriList.h"
#include "UltraCanvasDebug.h"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>

namespace UltraCanvas {

    namespace {
        const char* kTextUtf8 = "text/plain;charset=utf-8";
        const char* kTextPlain = "text/plain";
        const char* kUtf8String = "UTF8_STRING";
        const char* kString = "STRING";
        const char* kText = "TEXT";
        const char* kHtml = "text/html";
        const char* kUriList = "text/uri-list";
        const char* kGnomeCopiedFiles = "x-special/gnome-copied-files";
        const char* kKdeCutSelection = "application/x-kde-cutselection";
        const char* kPasswordHint = "x-kde-passwordManagerHint";

        std::vector<uint8_t> Bytes(const std::string& text) {
            return std::vector<uint8_t>(text.begin(), text.end());
        }

        bool IsTextType(const std::string& mime) {
            return mime == kTextUtf8 || mime == kTextPlain || mime == kUtf8String || mime == kString || mime == kText;
        }
    } // namespace

    // ===== THE LISTENERS =====
    struct WaylandClipboardListeners {
        static UltraCanvasWaylandClipboard* Self(void* data) { return static_cast<UltraCanvasWaylandClipboard*>(data); }

        // wl_data_offer
        static void OfferMime(void* data, wl_data_offer* offer, const char* mime) {
            auto* self = Self(data);
            for (auto& o : self->offers) {
                if (o->offer == offer) {
                    o->mimeTypes.push_back(mime ? mime : "");
                    return;
                }
            }
        }
        static void OfferSourceActions(void*, wl_data_offer*, uint32_t) {}
        static void OfferAction(void*, wl_data_offer*, uint32_t) {}

        // wl_data_device
        static void DeviceDataOffer(void* data, wl_data_device*, wl_data_offer* offer);
        static void DeviceEnter(void* data, wl_data_device*, uint32_t, wl_surface*, wl_fixed_t, wl_fixed_t, wl_data_offer* offer) {
            // A drag over one of our windows: not taken yet (phase 1); the
            // offer is kept until the drag leaves so it can be destroyed.
            auto* self = Self(data);
            if (auto* taken = self->TakeOffer(offer)) {
                self->dragOffer.reset(taken);
            }
        }
        static void DeviceLeave(void* data, wl_data_device*) {
            auto* self = Self(data);
            if (self->dragOffer) {
                wl_data_offer_destroy(self->dragOffer->offer);
                self->dragOffer.reset();
            }
        }
        static void DeviceMotion(void*, wl_data_device*, uint32_t, wl_fixed_t, wl_fixed_t) {}
        static void DeviceDrop(void*, wl_data_device*) {}
        static void DeviceSelection(void* data, wl_data_device*, wl_data_offer* offer) {
            auto* self = Self(data);
            if (self->selection) {
                wl_data_offer_destroy(self->selection->offer);
                self->selection.reset();
            }
            if (auto* taken = self->TakeOffer(offer)) {
                self->selection.reset(taken);
            }
            self->changed = true;
        }

        // wl_data_source
        static void SourceTarget(void*, wl_data_source*, const char*) {}
        static void SourceSend(void* data, wl_data_source* source, const char* mime, int32_t fd) {
            auto* self = Self(data);
            if (source == self->source && mime) {
                self->Send(mime, fd);
            } else {
                close(fd);
            }
        }
        static void SourceCancelled(void* data, wl_data_source* source) {
            // Another program took the clipboard: the source is done with.
            auto* self = Self(data);
            if (source == self->source) {
                self->DropSource();
            } else {
                wl_data_source_destroy(source);
            }
        }
        static void SourceDndDropPerformed(void*, wl_data_source*) {}
        static void SourceDndFinished(void*, wl_data_source*) {}
        static void SourceAction(void*, wl_data_source*, uint32_t) {}
    };

    namespace {
        const wl_data_offer_listener kOfferListener = {
                WaylandClipboardListeners::OfferMime,
                WaylandClipboardListeners::OfferSourceActions,
                WaylandClipboardListeners::OfferAction,
        };
        const wl_data_device_listener kDeviceListener = {
                WaylandClipboardListeners::DeviceDataOffer,
                WaylandClipboardListeners::DeviceEnter,
                WaylandClipboardListeners::DeviceLeave,
                WaylandClipboardListeners::DeviceMotion,
                WaylandClipboardListeners::DeviceDrop,
                WaylandClipboardListeners::DeviceSelection,
        };
        const wl_data_source_listener kSourceListener = {
                WaylandClipboardListeners::SourceTarget,
                WaylandClipboardListeners::SourceSend,
                WaylandClipboardListeners::SourceCancelled,
                WaylandClipboardListeners::SourceDndDropPerformed,
                WaylandClipboardListeners::SourceDndFinished,
                WaylandClipboardListeners::SourceAction,
        };
    } // namespace

    void WaylandClipboardListeners::DeviceDataOffer(void* data, wl_data_device*, wl_data_offer* offer) {
        auto* self = Self(data);
        auto entry = std::make_unique<UltraCanvasWaylandClipboard::DataOffer>();
        entry->offer = offer;
        self->offers.push_back(std::move(entry));
        wl_data_offer_add_listener(offer, &kOfferListener, self);
    }

    // ===== LIFETIME =====
    UltraCanvasWaylandClipboard::UltraCanvasWaylandClipboard() = default;

    UltraCanvasWaylandClipboard::~UltraCanvasWaylandClipboard() {
        Shutdown();
    }

    bool UltraCanvasWaylandClipboard::Initialize() {
        auto* app = UltraCanvasWaylandApplication::GetInstance();
        if (!app) return false;
        device = app->GetDataDevice();
        if (!device) {
            debugOutput << "UltraCanvas Wayland: the compositor offers no wl_data_device_manager - no clipboard" << std::endl;
            return false;
        }
        if (!listening) {
            wl_data_device_add_listener(device, &kDeviceListener, this);
            listening = true;
            // The current selection, if there is one, arrives with this
            wl_display_roundtrip(app->GetDisplay());
        }
        return true;
    }

    void UltraCanvasWaylandClipboard::Shutdown() {
        DropSource();
        if (selection) {
            wl_data_offer_destroy(selection->offer);
            selection.reset();
        }
        if (dragOffer) {
            wl_data_offer_destroy(dragOffer->offer);
            dragOffer.reset();
        }
        for (auto& o : offers) wl_data_offer_destroy(o->offer);
        offers.clear();
        device = nullptr;   // the application's; it releases it
    }

    UltraCanvasWaylandClipboard::DataOffer* UltraCanvasWaylandClipboard::TakeOffer(wl_data_offer* offer) {
        if (!offer) return nullptr;
        for (auto it = offers.begin(); it != offers.end(); ++it) {
            if ((*it)->offer == offer) {
                DataOffer* taken = it->release();
                offers.erase(it);
                return taken;
            }
        }
        return nullptr;
    }

    void UltraCanvasWaylandClipboard::DropSource() {
        if (source) {
            wl_data_source_destroy(source);
            source = nullptr;
        }
        ownData.clear();
    }

    // ===== OFFERING =====
    bool UltraCanvasWaylandClipboard::Offer(std::vector<Entry> entries) {
        auto* app = UltraCanvasWaylandApplication::GetInstance();
        if (!app || !device || !app->GetDataDeviceManager() || entries.empty()) return false;
        DropSource();
        source = wl_data_device_manager_create_data_source(app->GetDataDeviceManager());
        if (!source) return false;
        wl_data_source_add_listener(source, &kSourceListener, this);
        ownData = std::move(entries);
        for (const auto& entry : ownData) {
            wl_data_source_offer(source, entry.first.c_str());
        }
        // The compositor takes a selection only from a program that had
        // input recently: the serial of the last key or button.
        wl_data_device_set_selection(device, source, app->GetLastInputSerial());
        app->Flush();
        changed = true;
        return true;
    }

    void UltraCanvasWaylandClipboard::Send(const std::string& mimeType, int fd) {
        const std::vector<uint8_t>* bytes = nullptr;
        for (const auto& entry : ownData) {
            if (entry.first == mimeType) {
                bytes = &entry.second;
                break;
            }
        }
        if (bytes) {
            // The taker reads as we write; a taker that stops reading is
            // given up on after a while rather than hanging the UI.
            fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kWriteTimeoutMs);
            size_t written = 0;
            while (written < bytes->size()) {
                pollfd pfd{ fd, POLLOUT, 0 };
                const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
                if (left.count() <= 0 || poll(&pfd, 1, static_cast<int>(left.count())) <= 0) break;
                if (pfd.revents & (POLLERR | POLLHUP)) break;
                const ssize_t n = write(fd, bytes->data() + written, bytes->size() - written);
                if (n < 0) {
                    if (errno == EAGAIN || errno == EINTR) continue;
                    break;
                }
                written += static_cast<size_t>(n);
            }
        }
        close(fd);
    }

    // ===== READING =====
    bool UltraCanvasWaylandClipboard::Has(const std::string& mimeType) const {
        if (source) {
            return std::any_of(ownData.begin(), ownData.end(), [&](const Entry& e) { return e.first == mimeType; });
        }
        if (!selection) return false;
        return std::find(selection->mimeTypes.begin(), selection->mimeTypes.end(), mimeType) != selection->mimeTypes.end();
    }

    bool UltraCanvasWaylandClipboard::Read(const std::string& mimeType, std::vector<uint8_t>& out) {
        out.clear();
        // Our own: no round trip through the compositor
        if (source) {
            for (const auto& entry : ownData) {
                if (entry.first == mimeType) {
                    out = entry.second;
                    return true;
                }
            }
            return false;
        }
        auto* app = UltraCanvasWaylandApplication::GetInstance();
        if (!app || !selection || !Has(mimeType)) return false;
        int fds[2];
        if (pipe2(fds, O_CLOEXEC) != 0) return false;
        wl_data_offer_receive(selection->offer, mimeType.c_str(), fds[1]);
        close(fds[1]);
        app->Flush();
        fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kReadTimeoutMs);
        bool complete = false;
        uint8_t chunk[65536];
        while (true) {
            pollfd pfd{ fds[0], POLLIN, 0 };
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (left.count() <= 0) break;
            const int ready = poll(&pfd, 1, static_cast<int>(left.count()));
            if (ready < 0 && errno == EINTR) continue;
            if (ready <= 0) break;
            const ssize_t n = read(fds[0], chunk, sizeof(chunk));
            if (n == 0) {
                complete = true;
                break;
            }
            if (n < 0) {
                if (errno == EAGAIN || errno == EINTR) continue;
                break;
            }
            if (out.size() + static_cast<size_t>(n) > kMaxClipboardSize) break;
            out.insert(out.end(), chunk, chunk + n);
        }
        close(fds[0]);
        if (!complete) {
            debugOutput << "UltraCanvas Wayland: reading " << mimeType << " from the clipboard timed out" << std::endl;
            out.clear();
        }
        return complete;
    }

    bool UltraCanvasWaylandClipboard::ReadFirst(const std::vector<std::string>& mimeTypes, std::vector<uint8_t>& out,
                                                std::string* chosen) {
        for (const auto& mime : mimeTypes) {
            if (Has(mime) && Read(mime, out)) {
                if (chosen) *chosen = mime;
                return true;
            }
        }
        return false;
    }

    // ===== TEXT =====
    bool UltraCanvasWaylandClipboard::GetClipboardText(std::string& text) {
        std::vector<uint8_t> bytes;
        if (!ReadFirst({ kTextUtf8, kUtf8String, kTextPlain, kText, kString }, bytes)) return false;
        text.assign(bytes.begin(), bytes.end());
        return true;
    }

    bool UltraCanvasWaylandClipboard::SetClipboardText(const std::string& text) {
        const auto bytes = Bytes(text);
        return Offer({ { kTextUtf8, bytes }, { kUtf8String, bytes }, { kTextPlain, bytes }, { kText, bytes }, { kString, bytes } });
    }

    bool UltraCanvasWaylandClipboard::SetClipboardHtml(const std::string& html, const std::string& plainText) {
        const auto bytes = Bytes(plainText);
        return Offer({ { kHtml, Bytes(html) }, { kTextUtf8, bytes }, { kUtf8String, bytes },
                       { kTextPlain, bytes }, { kText, bytes }, { kString, bytes } });
    }

    bool UltraCanvasWaylandClipboard::GetClipboardHtml(std::string& html) {
        std::vector<uint8_t> bytes;
        if (!ReadFirst({ kHtml }, bytes) || bytes.empty()) return false;
        html.assign(bytes.begin(), bytes.end());
        return true;
    }

    bool UltraCanvasWaylandClipboard::SetClipboardSecretText(const std::string& text) {
        const auto bytes = Bytes(text);
        return Offer({ { kPasswordHint, Bytes("secret") }, { kTextUtf8, bytes }, { kUtf8String, bytes },
                       { kTextPlain, bytes }, { kText, bytes }, { kString, bytes } });
    }

    bool UltraCanvasWaylandClipboard::IsClipboardMarkedSecret() {
        if (!Has(kPasswordHint)) return false;
        std::vector<uint8_t> bytes;
        if (!Read(kPasswordHint, bytes)) return true;   // the hint alone is the mark
        const std::string value(bytes.begin(), bytes.end());
        return value != "" && value != "0" && value != "false";
    }

    bool UltraCanvasWaylandClipboard::HasClipboardOwner() {
        return source != nullptr || selection != nullptr;
    }

    // ===== IMAGES =====
    bool UltraCanvasWaylandClipboard::GetClipboardImage(std::vector<uint8_t>& imageData, std::string& format) {
        std::vector<std::string> candidates;
        const std::vector<std::string> formats = GetAvailableFormats();
        for (const auto& mime : formats) {
            if (mime.compare(0, 6, "image/") == 0) candidates.push_back(mime);
        }
        // PNG first, as every other backend prefers it
        std::stable_sort(candidates.begin(), candidates.end(), [](const std::string& a, const std::string& b) {
            return (a == "image/png") > (b == "image/png");
        });
        std::string chosen;
        if (!ReadFirst(candidates, imageData, &chosen) || imageData.empty()) return false;
        format = chosen.substr(6);
        return true;
    }

    bool UltraCanvasWaylandClipboard::SetClipboardImage(const std::vector<uint8_t>& imageData, const std::string& format) {
        if (imageData.empty()) return false;
        std::string mime = format;
        if (mime.compare(0, 6, "image/") != 0) mime = "image/" + (mime.empty() ? std::string("png") : mime);
        return Offer({ { mime, imageData } });
    }

    // ===== FILES =====
    bool UltraCanvasWaylandClipboard::GetClipboardFiles(std::vector<std::string>& filePaths) {
        bool cut = false;
        return GetClipboardFiles(filePaths, cut);
    }

    bool UltraCanvasWaylandClipboard::SetClipboardFiles(const std::vector<std::string>& filePaths) {
        return SetClipboardFiles(filePaths, false);
    }

    bool UltraCanvasWaylandClipboard::GetClipboardFiles(std::vector<std::string>& filePaths, bool& cutOperation) {
        cutOperation = false;
        filePaths.clear();
        std::vector<uint8_t> bytes;
        // x-special/gnome-copied-files: the verb on the first line, then the URIs
        if (Has(kGnomeCopiedFiles) && Read(kGnomeCopiedFiles, bytes) && !bytes.empty()) {
            std::string payload(bytes.begin(), bytes.end());
            const size_t newline = payload.find('\n');
            const std::string verb = payload.substr(0, newline);
            cutOperation = verb == "cut";
            filePaths = LinuxUriList::ParsePaths(newline == std::string::npos ? "" : payload.substr(newline + 1));
            return !filePaths.empty();
        }
        if (Has(kUriList) && Read(kUriList, bytes) && !bytes.empty()) {
            filePaths = LinuxUriList::ParsePaths(std::string(bytes.begin(), bytes.end()));
            if (Has(kKdeCutSelection)) {
                std::vector<uint8_t> flag;
                cutOperation = Read(kKdeCutSelection, flag) && !flag.empty() && flag[0] == '1';
            }
            return !filePaths.empty();
        }
        return false;
    }

    bool UltraCanvasWaylandClipboard::SetClipboardFiles(const std::vector<std::string>& filePaths, bool cutOperation) {
        if (filePaths.empty()) return false;
        std::string uriList;
        std::string gnomeList = cutOperation ? "cut" : "copy";
        std::string plain;
        for (const auto& path : filePaths) {
            const std::string uri = LinuxUriList::EncodeFileUri(path);
            uriList += uri + "\r\n";
            gnomeList += "\n" + uri;
            if (!plain.empty()) plain += "\n";
            plain += path;
        }
        const auto plainBytes = Bytes(plain);
        return Offer({ { kUriList, Bytes(uriList) }, { kGnomeCopiedFiles, Bytes(gnomeList) },
                       { kKdeCutSelection, Bytes(cutOperation ? "1" : "0") },
                       { kTextUtf8, plainBytes }, { kUtf8String, plainBytes }, { kTextPlain, plainBytes } });
    }

    // ===== MONITORING AND FORMATS =====
    bool UltraCanvasWaylandClipboard::HasClipboardChanged() {
        return changed;
    }

    void UltraCanvasWaylandClipboard::ResetChangeState() {
        changed = false;
    }

    std::vector<std::string> UltraCanvasWaylandClipboard::GetAvailableFormats() {
        std::vector<std::string> formats;
        if (source) {
            for (const auto& entry : ownData) formats.push_back(entry.first);
        } else if (selection) {
            formats = selection->mimeTypes;
        }
        return formats;
    }

    bool UltraCanvasWaylandClipboard::IsFormatAvailable(const std::string& format) {
        if (Has(format)) return true;
        // "text" in any of its spellings
        if (IsTextType(format)) {
            for (const char* mime : { kTextUtf8, kUtf8String, kTextPlain, kText, kString }) {
                if (Has(mime)) return true;
            }
        }
        return false;
    }

} // namespace UltraCanvas
