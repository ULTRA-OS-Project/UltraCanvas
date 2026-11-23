// Tests/MediaViewerWordDocumentTest.cpp
// A word-processing document (.odt, .docx, .doc) opens in the media viewer -
// and so in UltraFiler's preview pane - as its pages in the document view, as
// print lays them out: a .docx, an .odt of two pages, a Word 97 .doc. A
// password-protected .odt is locked until Unlock: a wrong password is refused,
// the right one opens it. A build without the PDF view lists none of them.
// Runs headless under Xvfb; skips when there is no display.
// Version: 1.0.0
// Last Modified: 2026-11-23
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasMediaViewer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasWindow.h"
#include "DisplayRequired.h"
#ifdef ULTRACANVAS_PLUGIN_PDF
#include "Plugins/Documents/UltraCanvasPDFView.h"
#endif

#include <cstdlib>
#include <iostream>
#include <string>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

std::string Fixture(const char* name) { return std::string(WORDVIEW_FIXTURE_DIR) + "/" + name; }

// The Office fixtures' password: non-ASCII, so its UTF-8 counts.
const std::string kPassword = "\xC3\x9Cltra-P\xC3\xA4ssw\xC3\xB6rt 1";

std::string InfoText(UltraCanvasMediaViewer& viewer) {
    auto* label = dynamic_cast<UltraCanvasLabel*>(viewer.FindChildById("MV_InfoLabel"));
    return label ? label->GetText() : std::string();
}

// Where a display was asked for (DisplayTest::DisplayRequired) a skip would
// read as a pass, so it fails instead.
int Skip(const std::string& why) {
    if (DisplayTest::DisplayRequired()) {
        std::cout << "FAIL: a display was required, and " << why << "\n";
        return 1;
    }
    std::cout << "SKIP: " << why << "\n";
    return 0;
}

} // namespace

int main() {
    std::cout << "MediaViewerWordDocumentTest\n";

#ifndef ULTRACANVAS_PLUGIN_PDF
    std::cout << "\nNo PDF view in this build\n";
    Check(!UltraCanvasMediaViewer::IsWordProcessingFile("a.docx") &&
          !UltraCanvasMediaViewer::IsSupportedMedia("a.docx"),
          "a .docx is not offered: there is no view to show its pages in");
    std::cout << (g_failures ? "\nFAILED\n" : "\nAll passed\n");
    return g_failures ? 1 : 0;
#else
    std::cout << "\nWhich files\n";
    for (const char* name : {"a.odt", "b.DOCX", "c.doc"})
        Check(UltraCanvasMediaViewer::IsWordProcessingFile(name) &&
              UltraCanvasMediaViewer::IsSupportedMedia(name) &&
              UltraCanvasMediaViewer::ClassifyFile(name) == MediaKind::Document,
              std::string(name) + " is a document the viewer shows as pages");
    Check(!UltraCanvasMediaViewer::IsWordProcessingFile("d.tex") &&
          UltraCanvasMediaViewer::ClassifyFile("d.tex") == MediaKind::Text,
          "a .tex stays with the text view");
    Check(!UltraCanvasMediaViewer::IsWordProcessingFile("e.pdf"), "a PDF is not one of them");

    if (!std::getenv("DISPLAY")) return Skip("there is no DISPLAY");
    UltraCanvasApplication app;
    if (!app.Initialize("MediaViewerWordDocumentTest")) return Skip("the application would not initialise");
    WindowConfig cfg;
    cfg.title = "MediaViewerWordDocumentTest";
    cfg.width = 800;
    cfg.height = 600;
    auto window = CreateWindow(cfg);
    if (!window) return Skip("the window could not be created");
    auto viewer = CreateMediaViewer("Viewer", 0, 0, 800, 600);
    window->AddChild(viewer);
    window->Show();
    auto* pages = dynamic_cast<UltraCanvasPDFView*>(viewer->FindChildById("MV_PDFView"));
    Check(pages != nullptr, "the viewer has its document view");
    if (!pages) return 1;

    auto open = [&](const char* name) {
        viewer->OpenFile(Fixture(name));
        window->UpdateAndRender();
    };

    std::cout << "\nA .docx\n";
    open("word-shapes.docx");
    Check(pages->IsVisible() && pages->HasDocument() && pages->GetPageCount() >= 1,
          "its pages are in the document view");
    Check(!viewer->IsLocked(), "nothing to unlock");
    Check(InfoText(*viewer).find("DOCUMENT") != std::string::npos, "the info bar calls it a document");

    std::cout << "\nAn .odt of two pages\n";
    open("word97-formatting.odt");
    Check(pages->HasDocument() && pages->GetPageCount() == 2, "both pages are there");

    std::cout << "\nA Word 97 .doc\n";
    open("word-gradients.doc");
    Check(pages->HasDocument() && pages->GetPageCount() >= 1, "its pages are in the document view");

    std::cout << "\nA password-protected .odt\n";
    open("odf-encrypted.odt");
    Check(viewer->IsLocked() && !pages->HasDocument(), "it is locked, and shows no pages");
    std::string error;
    Check(viewer->UnlockWithPassword("not it", error) == MediaUnlockResult::WrongPassword,
          "a wrong password is refused");
    Check(viewer->IsLocked(), "and it stays locked");
    error.clear();
    const MediaUnlockResult result = viewer->UnlockWithPassword(kPassword, error);
    Check(result == MediaUnlockResult::Unlocked, "the right password opens it" + (error.empty() ? "" : " (" + error + ")"));
    window->UpdateAndRender();
    Check(!viewer->IsLocked() && viewer->IsUnlocked() && pages->IsVisible() && pages->HasDocument() &&
          pages->GetPageCount() >= 1,
          "its pages are shown, marked as unlocked");

    window->Close();
    std::cout << (g_failures ? "\nFAILED: " + std::to_string(g_failures) + "\n" : "\nAll passed\n");
    return g_failures ? 1 : 0;
#endif
}
