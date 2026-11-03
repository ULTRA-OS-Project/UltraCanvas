// OS/Linux/Wayland/UltraCanvasWaylandClipboard.h
// The clipboard on Wayland: the seat's wl_data_device. What another program
// copied arrives as a wl_data_offer naming its MIME types and is read
// through a pipe; what this program copies is a wl_data_source that writes
// into the pipe the taker hands over. The formats are the freedesktop ones
// the X11 backend speaks (text/plain;charset=utf-8, text/html,
// text/uri-list with x-special/gnome-copied-files, image/png, the KDE
// password-manager hint), so a copy pastes between the two sessions alike.
// Version: 0.1.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVAS_WAYLAND_CLIPBOARD_H
#define ULTRACANVAS_WAYLAND_CLIPBOARD_H

#include "../../../include/UltraCanvasClipboard.h"

#include <wayland-client.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace UltraCanvas {

    class UltraCanvasWaylandClipboard : public UltraCanvasClipboardBackend {
    public:
        UltraCanvasWaylandClipboard();
        ~UltraCanvasWaylandClipboard() override;

        bool Initialize() override;
        void Shutdown() override;

        bool GetClipboardText(std::string& text) override;
        bool SetClipboardText(const std::string& text) override;
        bool GetClipboardImage(std::vector<uint8_t>& imageData, std::string& format) override;
        bool SetClipboardImage(const std::vector<uint8_t>& imageData, const std::string& format) override;
        bool GetClipboardFiles(std::vector<std::string>& filePaths) override;
        bool SetClipboardFiles(const std::vector<std::string>& filePaths) override;
        bool GetClipboardFiles(std::vector<std::string>& filePaths, bool& cutOperation) override;
        bool SetClipboardFiles(const std::vector<std::string>& filePaths, bool cutOperation) override;
        bool SetClipboardHtml(const std::string& html, const std::string& plainText) override;
        bool GetClipboardHtml(std::string& html) override;
        bool SetClipboardSecretText(const std::string& text) override;
        bool IsClipboardMarkedSecret() override;
        bool HasClipboardOwner() override;

        bool HasClipboardChanged() override;
        void ResetChangeState() override;

        std::vector<std::string> GetAvailableFormats() override;
        bool IsFormatAvailable(const std::string& format) override;

    private:
        friend struct WaylandClipboardListeners;

        // An offer the compositor announced: the other program's formats.
        struct DataOffer {
            wl_data_offer* offer = nullptr;
            std::vector<std::string> mimeTypes;
        };
        std::vector<std::unique_ptr<DataOffer>> offers;   // announced, not yet placed
        std::unique_ptr<DataOffer> selection;             // the clipboard's
        std::unique_ptr<DataOffer> dragOffer;             // a drag over our window (not taken)

        // What this program put on the clipboard, by MIME type, served to
        // whoever asks - this program included, without a round trip.
        using Entry = std::pair<std::string, std::vector<uint8_t>>;
        wl_data_source* source = nullptr;
        std::vector<Entry> ownData;
        bool changed = false;

        wl_data_device* device = nullptr;
        bool listening = false;

        DataOffer* TakeOffer(wl_data_offer* offer);
        void DropSource();
        bool Offer(std::vector<Entry> entries);
        bool Read(const std::string& mimeType, std::vector<uint8_t>& out);
        bool ReadFirst(const std::vector<std::string>& mimeTypes, std::vector<uint8_t>& out, std::string* chosen = nullptr);
        bool Has(const std::string& mimeType) const;
        void Send(const std::string& mimeType, int fd);

        static constexpr int kReadTimeoutMs = 3000;
        static constexpr int kWriteTimeoutMs = 10000;
        static constexpr size_t kMaxClipboardSize = 128u * 1024u * 1024u;
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_WAYLAND_CLIPBOARD_H
