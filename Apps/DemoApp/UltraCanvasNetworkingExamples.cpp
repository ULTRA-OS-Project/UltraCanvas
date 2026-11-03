// Apps/DemoApp/UltraCanvasNetworkingExamples.cpp
// UltraNet module demo page. Like the FileLoader module page, a horizontal
// tab bar splits the module into three views:
//   * Overview  – the short intro and the UltraNet architecture diagram.
//   * Details   – the full README documentation.
//   * Examples  – the live remote-resource loader: enter an https:// URL in
//                 the input, press Load, see the remote image rendered in the
//                 preview pane. Exercises the public surface end-to-end:
//                   UltraCanvasFileLoader::LoadFile(url)
//                     -> UltraNet_HttpGet (when ULTRACANVAS_HAS_NET is defined)
//                     -> UCImageRaster::LoadFromMemory
//                     -> UltraCanvasImageElement::LoadFromImage
// Version: 0.3.0 - the shared page look (UltraCanvasDemoPageKit): a page title
//                  and description, the tabs in a card that fills the area,
//                  the loader's texts sized to fit and its preview filling the tab
// Version: 0.2.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraCanvasApplication.h"
#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasConfig.h"   // GetResourcesDir
#include "UltraCanvasUtils.h"    // NormalizePath, LoadFile

#ifdef ULTRACANVAS_HAS_NET
#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetHttp.h>
#endif

#include <chrono>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include "UltraCanvasPathUtf8.h"

namespace UltraCanvas {

namespace {

    // ---- Overview tab: intro + architecture diagram. ----
    std::shared_ptr<UltraCanvasUIElement> BuildOverviewTab() {
        const std::string base    = NormalizePath(GetResourcesDir() + "Docs/Modules/UltraNet/");
        const std::string svgName = "UltraNet.svg";

        std::string combined;
        std::string intro = LoadFile(base + "intro.md");
        if (intro.find_first_not_of(" \t\r\n") != std::string::npos) {
            combined += intro + "\n\n";
        }
        if (std::ifstream(UltraCanvas::PathFromUtf8(base + svgName)).good()) {
            combined += "![UltraNet architecture](" + svgName + ")\n\n";
        }

        auto text = std::make_shared<UltraCanvasTextArea>("UltraNetOverview");
        text->size.width  = CSSLayout::Dimension::Pct(100);
        text->size.height = CSSLayout::Dimension::Pct(100);
        text->SetMarkdownBaseDirectory(base);
        text->SetText(combined);
        text->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
        text->SetReadOnly(true);
        text->SetWordWrap(true);
        text->SetCursorPosition(LineColumnIndex::INVALID);
        text->SetPadding(0, 5, 0, 7);
        return text;
    }

    // ---- Details tab: the full README documentation. ----
    std::shared_ptr<UltraCanvasUIElement> BuildDetailsTab() {
        const std::string base = NormalizePath(GetResourcesDir() + "Docs/Modules/UltraNet/");

        auto text = std::make_shared<UltraCanvasTextArea>("UltraNetDetails");
        text->size.width  = CSSLayout::Dimension::Pct(100);
        text->size.height = CSSLayout::Dimension::Pct(100);
        text->SetMarkdownBaseDirectory(base);
        text->SetText(LoadFile(base + "README.md"));
        text->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
        text->SetReadOnly(true);
        text->SetWordWrap(true);
        text->SetCursorPosition(LineColumnIndex::INVALID);
        text->SetPadding(0, 5, 0, 7);
        return text;
    }

    // ---- Examples tab: the live remote-resource loader. ----
    std::shared_ptr<UltraCanvasUIElement> BuildExamplesTab() {
        // The tab gives this column its size; the preview takes the height
        // the texts and the URL row leave.
        auto container = std::make_shared<UltraCanvasContainer>(
            "NetworkingExamples", 0, 0, 1000, 700);
        container->layout.SetFlexColumn().SetFlexGap(8)
                         .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        container->SetPadding(10, 12, 10, 12);

        // ===== TITLE =====
        auto title = DemoKit::AddHeading(container, "NetTitle", "UltraNet — Remote Resource Loader");
        title->SetFontSize(16);

        DemoKit::AddText(container, "NetSubtitle",
                         "Loads a remote image through UltraCanvasFileLoader::LoadFile, "
                         "which dispatches to UltraNet_HttpGet for http:// and https:// URLs.");

        // ===== INFO LINE — what's the UltraNet backend doing? =====
        auto backendLabel = DemoKit::AddCaption(container, "NetBackend", "");
        backendLabel->SetFontSize(11);
#ifdef ULTRACANVAS_HAS_NET
        backendLabel->SetText("Backend: " + UltraNet_GetBackendInfo());
#else
        backendLabel->SetText("Backend: UltraNet not compiled in "
                              "(rebuild with -DULTRACANVAS_ENABLE_NET=ON)");
#endif

        // ===== URL INPUT + LOAD BUTTON ROW =====
        auto urlRow = DemoKit::AddRow(container, "NetUrlRow", 10);
        urlRow->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
        DemoKit::Add(urlRow, DemoKit::CreateText("UrlLabel", "URL:"));

        // The input takes the row's spare width, at least 300 px before the
        // button wraps under it.
        auto urlInput = std::make_shared<UltraCanvasTextInput>(
            "NetUrl", 0, 0, 720, 30);
        urlInput->SetPlaceholder("https://example.com/image.png");
        urlInput->SetText("https://www.gstatic.com/webp/gallery/1.png");
        DemoKit::AddStretched(urlRow, urlInput);
        urlInput->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(300));

        // As wide as its text, at least the 100 px it had.
        auto loadButton = std::make_shared<UltraCanvasButton>(
            "NetLoad", 0, 0, 0, 30, "Load");
        {
            CSSLayout::BoxConstraints limits;
            limits.minWidth = CSSLayout::Dimension::Px(100);
            loadButton->boxConstraints = limits;
        }
        DemoKit::Add(urlRow, loadButton);

        // ===== STATUS LINE =====
        auto status = DemoKit::AddText(container, "NetStatus", "Ready.");

        // ===== IMAGE PREVIEW =====
        // Fills the rest of the tab from a zero basis; never squeezed below
        // 200 px (the tab scrolls instead).
        auto preview = std::make_shared<UltraCanvasImageElement>(
            "NetPreview", 0, 0, 800, 480);
        preview->SetFitMode(ImageFitMode::Contain);
        preview->size.width = CSSLayout::Dimension::Auto();
        preview->size.height = CSSLayout::Dimension::Auto();
        DemoKit::Add(container, preview, 1);
        preview->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
        {
            CSSLayout::BoxConstraints limits;
            limits.minHeight = CSSLayout::Dimension::Px(200);
            preview->boxConstraints = limits;
        }

        // ===== LOAD HANDLER =====
        // Fully asynchronous: UltraNet_HttpRequestAsync runs the HTTP fetch on
        // the curl_multi worker thread; the completion callback marshals the
        // result back to the UI thread via UltraCanvasApplicationBase::
        // PostToUIThread so the widget mutations stay on the right thread.
        // The UI never blocks while the network is in flight.
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

#ifdef ULTRACANVAS_HAS_NET
                const auto t0 = std::chrono::steady_clock::now();
                UltraNetHttpRequest req;
                req.url    = url;
                req.method = UltraNetHttpMethod::Get;

                UltraNet_HttpRequestAsync(req,
                    [status, preview, t0, url](const UltraNetResponse& resp) {
                        // Worker thread. Copy what we'll need; the UltraNetResponse
                        // moves into the lambda capture below.
                        auto r = std::make_shared<UltraNetResponse>(resp);
                        const auto dt =
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - t0).count();

                        auto* app = UltraCanvasApplicationBase::GetCurrent();
                        if (!app) return;     // tearing down; abandon update
                        app->PostToUIThread(
                            [status, preview, r, dt, url]() {
                                if (r->statusCode <= 0) {
                                    status->SetText("Status: FAILED — " +
                                        (r->statusMessage.empty()
                                            ? std::string{"transport error"}
                                            : r->statusMessage));
                                    status->SetTextColor(Color(180, 0, 0, 255));
                                    return;
                                }
                                if (r->statusCode >= 400) {
                                    status->SetText("Status: HTTP " +
                                        std::to_string(r->statusCode));
                                    status->SetTextColor(Color(180, 0, 0, 255));
                                    return;
                                }
                                auto img = UCImageRaster::LoadFromMemory(r->body);
                                if (!img || !img->IsValid()) {
                                    status->SetText(
                                        "Status: download OK but image decode failed");
                                    status->SetTextColor(Color(180, 100, 0, 255));
                                    return;
                                }
                                preview->LoadFromImage(img);
                                std::ostringstream os;
                                os << "Status: " << r->body.size() << " bytes in "
                                   << dt << " ms  ·  Content-Type: "
                                   << (r->contentType.empty()
                                           ? "(unknown)" : r->contentType)
                                   << "  ·  Image "
                                   << img->GetWidth() << "x" << img->GetHeight();
                                status->SetText(os.str());
                                status->SetTextColor(Color(0, 130, 0, 255));
                            });
                    });
#else
                status->SetText("Status: UltraNet not compiled in "
                                "(rebuild with -DULTRACANVAS_ENABLE_NET=ON)");
                status->SetTextColor(Color(180, 100, 0, 255));
#endif
            };
        loadButton->SetOnClick(doLoad);

        return container;
    }

} // anonymous namespace

// ===== ULTRANET MODULE DEMO =====
std::shared_ptr<UltraCanvasUIElement>
UltraCanvasDemoApplication::CreateNetworkingExamples() {
    auto root = DemoKit::CreatePage("UltraNetExamples", "Ultra Net",
            "The Ultra Net module: an overview, its documentation and a live remote-resource "
            "loader (UltraCanvasFileLoader::LoadFile + UltraNet_HttpGet + UCImageRaster::LoadFromMemory).");
    // The tabs' text areas and the loader size and scroll themselves: the
    // page is the area's height and the tabs' card takes what the title leaves.
    root->SetFillsArea(true);

    auto card = DemoKit::CreateCard("UltraNetCard");
    card->SetPadding(4);
    DemoKit::Add(root, card, 1);
    card->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0)).SetFlexShrink(1);

    auto tabs = std::make_shared<UltraCanvasTabbedContainer>("UltraNetTabs", 0, 0, 0, 0);
    tabs->SetTabPosition(TabPosition::Top);
    tabs->SetTabStyle(TabStyle::Modern);
    tabs->SetCloseMode(TabCloseMode::NoClose);
    tabs->SetTabHeight(30);

    tabs->AddTab("Overview", BuildOverviewTab());
    tabs->AddTab("Details",  BuildDetailsTab());
    tabs->AddTab("Examples", BuildExamplesTab());
    tabs->SetActiveTab(0);

    DemoKit::Add(card, tabs, 1);
    tabs->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0)).SetFlexShrink(1);
    return root;
}

} // namespace UltraCanvas
