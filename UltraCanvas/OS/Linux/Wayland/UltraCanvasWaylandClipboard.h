// OS/Linux/Wayland/UltraCanvasWaylandClipboard.h
// The clipboard on Wayland: the seat's wl_data_device. What another program
// copied arrives as a wl_data_offer naming its MIME types and is read
// through a pipe; what this program copies is a wl_data_source that writes
// into the pipe the taker hands over. The formats are the freedesktop ones
// the X11 backend speaks (text/plain;charset=utf-8, text/html,
// text/uri-list with x-special/gnome-copied-files, image/png, the KDE
// password-manager hint), so a copy pastes between the two sessions alike.
//
// Drag and drop runs on the same wl_data_device. A file drag out of one of
// this program's windows (StartFileDrag, behind the window's
// StartNativeFileDrag) is a wl_data_source offering text/uri-list, started
// with the serial of the button the user holds; the target's choice of
// action (copy or move) and its finish or cancel reach the caller's
// callback. A drag from another program over one of our windows is the
// data device's enter / motion / leave / drop: the offer is accepted for
// text/uri-list, the window gets DragEnter / DragOver / DragLeave events,
// and a drop reads the list through a pipe and delivers a Drop event with
// the file paths, as the X11 backend's XDnD does.
// Version: 0.2.0 - drag and drop: file drags out, drops in
// Version: 0.1.0
// Last Modified: 2026-11-04
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVAS_WAYLAND_CLIPBOARD_H
#define ULTRACANVAS_WAYLAND_CLIPBOARD_H

#include "../../../include/UltraCanvasClipboard.h"

#include <wayland-client.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace UltraCanvas {

    class UltraCanvasWaylandWindow;

    class UltraCanvasWaylandClipboard : public UltraCanvasClipboardBackend {
    public:
        UltraCanvasWaylandClipboard();
        // The backend the application's clipboard runs on, for the windows'
        // drags; nullptr before Initialize.
        static UltraCanvasWaylandClipboard* Current();

        // A file drag out of `origin`, with the files as text/uri-list. The
        // compositor takes it only while a button is held (the serial of
        // that press); false when there is none, or no data device.
        // `onFinished(accepted, moved)` when the target took it (moved: the
        // target chose the move action) or the drag was cancelled.
        bool StartFileDrag(UltraCanvasWaylandWindow* origin, const std::vector<std::string>& filePaths,
                           std::function<void(bool accepted, bool moved)> onFinished);
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
        std::unique_ptr<DataOffer> dragOffer;             // a drag over our window
        UltraCanvasWaylandWindow* dragWindow = nullptr;   // ... the window it is over
        bool dragOfferHasFiles = false;                   // ... offers text/uri-list
        double dragX = 0, dragY = 0;                      // ... where, in the window's logical pixels

        // A drag out of our window (StartFileDrag)
        wl_data_source* dragSource = nullptr;
        std::string dragUriList;
        std::function<void(bool, bool)> dragFinished;
        bool dragAccepted = false;      // a target took the type
        bool dragDropped = false;       // the target got the drop
        uint32_t dragAction = 0;        // the action the target chose
        void EndDrag(bool finished);
        void DeliverDragEvent(int type, bool withFiles, const std::vector<std::string>& files);

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
        bool ReadOffer(DataOffer* offer, const std::string& mimeType, std::vector<uint8_t>& out);
        bool ReadFirst(const std::vector<std::string>& mimeTypes, std::vector<uint8_t>& out, std::string* chosen = nullptr);
        bool Has(const std::string& mimeType) const;
        void Send(const std::string& mimeType, int fd);

        static constexpr int kReadTimeoutMs = 3000;
        static constexpr int kWriteTimeoutMs = 10000;
        static constexpr size_t kMaxClipboardSize = 128u * 1024u * 1024u;
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_WAYLAND_CLIPBOARD_H
