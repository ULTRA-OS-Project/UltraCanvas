// Apps/DemoApp/UltraCanvasNetworkingExamples.cpp
// Demonstrates the UltraNet / FileLoader / UltraCanvas integration: enter
// an https:// URL in the input, press Load, see the remote image rendered
// in the preview pane. Exercises the public surface end-to-end:
//   UltraCanvasFileLoader::LoadFile(url)
//     -> UltraNet_HttpGet (when ULTRACANVAS_HAS_NET is defined)
//     -> UCImageRaster::LoadFromMemory
//     -> UltraCanvasImageElement::LoadFromImage
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraCanvasDemo.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"

#ifdef ULTRACANVAS_HAS_NET
#include <UltraNet/UltraNetCore.h>
#endif

#include <chrono>
#include <sstream>

namespace UltraCanvas {

std::shared_ptr<UltraCanvasUIElement>
UltraCanvasDemoApplication::CreateNetworkingExamples() {
    auto container = std::make_shared<UltraCanvasContainer>(
        "NetworkingExamples", 0, 0, 1000, 700);
    container->SetPadding(0, 0, 10, 0);

    // ===== TITLE =====
    auto title = std::make_shared<UltraCanvasLabel>(
        "NetTitle", 10, 10, 800, 30);
    title->SetText("UltraNet — Remote Resource Loader");
    title->SetFontSize(20);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Color(50, 50, 150, 255));
    container->AddChild(title);

    auto subtitle = std::make_shared<UltraCanvasLabel>(
        "NetSubtitle", 10, 45, 900, 25);
    subtitle->SetText("Loads a remote image through UltraCanvasFileLoader::LoadFile, "
                      "which dispatches to UltraNet_HttpGet for http:// and https:// URLs.");
    subtitle->SetFontSize(12);
    subtitle->SetTextColor(Color(80, 80, 80, 255));
    container->AddChild(subtitle);

    // ===== INFO LINE — what's the UltraNet backend doing? =====
    auto backendLabel = std::make_shared<UltraCanvasLabel>(
        "NetBackend", 10, 80, 980, 20);
    backendLabel->SetFontSize(11);
    backendLabel->SetTextColor(Color(120, 120, 120, 255));
#ifdef ULTRACANVAS_HAS_NET
    backendLabel->SetText("Backend: " + UltraNet_GetBackendInfo());
#else
    backendLabel->SetText("Backend: UltraNet not compiled in "
                          "(rebuild with -DULTRACANVAS_ENABLE_NET=ON)");
#endif
    container->AddChild(backendLabel);

    // ===== URL INPUT + LOAD BUTTON ROW =====
    auto urlLabel = std::make_shared<UltraCanvasLabel>(
        "UrlLabel", 10, 120, 60, 30);
    urlLabel->SetText("URL:");
    urlLabel->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
    container->AddChild(urlLabel);

    auto urlInput = std::make_shared<UltraCanvasTextInput>(
        "NetUrl", 70, 120, 720, 30);
    urlInput->SetPlaceholder("https://example.com/image.png");
    urlInput->SetText("https://www.gstatic.com/webp/gallery/1.png");
    container->AddChild(urlInput);

    auto loadButton = std::make_shared<UltraCanvasButton>(
        "NetLoad", 800, 120, 100, 30);
    loadButton->SetText("Load");
    container->AddChild(loadButton);

    // ===== STATUS LINE =====
    auto status = std::make_shared<UltraCanvasLabel>(
        "NetStatus", 10, 160, 980, 25);
    status->SetText("Ready.");
    status->SetFontSize(12);
    container->AddChild(status);

    // ===== IMAGE PREVIEW =====
    auto preview = std::make_shared<UltraCanvasImageElement>(
        "NetPreview", 10, 195, 800, 480);
    preview->SetFitMode(ImageFitMode::Contain);
    container->AddChild(preview);

    // ===== LOAD HANDLER =====
    // Synchronous load on the UI thread — fine for a demo, briefly blocks
    // the UI until the response arrives. A production app would use
    // UltraNet_HttpRequestAsync and marshal the result back to the UI
    // thread; that's a layering improvement tracked separately.
    auto doLoad =
        [urlInput, status, preview]() {
            const std::string url = urlInput->GetText();
            if (url.empty()) {
                status->SetText("Status: please enter a URL.");
                status->SetTextColor(Color(180, 100, 0, 255));
                return;
            }
            status->SetText("Status: loading " + url + " ...");
            status->SetTextColor(Color(80, 80, 80, 255));

            const auto t0 = std::chrono::steady_clock::now();
            FileBytesResult res = UltraCanvasFileLoader::LoadFile(url);
            const auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - t0).count();

            if (!res.success) {
                std::ostringstream os;
                os << "Status: FAILED — " << res.error;
                if (res.httpStatus) os << " (HTTP " << res.httpStatus << ")";
                status->SetText(os.str());
                status->SetTextColor(Color(180, 0, 0, 255));
                return;
            }

            auto img = UCImageRaster::LoadFromMemory(res.bytes);
            if (!img || !img->IsValid()) {
                status->SetText("Status: download OK but image decode failed");
                status->SetTextColor(Color(180, 100, 0, 255));
                return;
            }
            preview->LoadFromImage(img);

            std::ostringstream os;
            os << "Status: " << res.bytes.size() << " bytes in " << dt << " ms"
               << "  ·  Content-Type: "
               << (res.contentType.empty() ? "(unknown)" : res.contentType)
               << "  ·  Image " << img->GetWidth() << "x" << img->GetHeight();
            status->SetText(os.str());
            status->SetTextColor(Color(0, 130, 0, 255));
        };
    loadButton->SetOnClick(doLoad);

    return container;
}

} // namespace UltraCanvas
