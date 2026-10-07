// Apps/UltraWeb/ui/UltraWebWindow.h
// The UltraWeb browser window: an address bar, the app area a WebAssembly
// app builds its UI into (UC_ROOT_HANDLE of the element ABI), and a status
// bar. One app at a time in this version; tabs come with the HTML reader.
// Version: 0.1.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "../host/UltraWebGuest.h"
#include "../host/UltraWebLoader.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasWindow.h"

#include <memory>
#include <string>

namespace UltraWeb {

class UltraWebWindow {
public:
    UltraWebWindow();
    ~UltraWebWindow();

    bool Initialize();
    void Show();
    // Load and run what an address names (about:demo, a path, a URL).
    void Navigate(const std::string& address);

private:
    void ShowApp(const LoadedApp& app);
    void ShowError(const std::string& title, const std::string& detail);
    void ClearAppArea();
    void SetStatus(const std::string& text);
    void SetTitleFor(const std::string& address);
    void LayoutForSize(float width, float height);

    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> page_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> address_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> goButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> reloadButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> appArea_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> status_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> errorTitle_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> errorDetail_;

    UltraWebLoader loader_;
    std::unique_ptr<UltraWebGuest> guest_;
    std::string currentAddress_;
    // Deferred work (a guest failure reported on a later turn) checks this
    // before touching the window.
    std::shared_ptr<bool> alive_;
};

} // namespace UltraWeb
