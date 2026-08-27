// Tests/VectorFormatsPluginTest.cpp
// Tests for the vector formats graphics plugin: every format of the
// converter matrix must be reachable through the plugin registry - saving
// via SaveGraphicsFile for all ten writers, loading via LoadGraphicsFile
// for the formats with a reader - and the readers must round-trip the
// writers' output with the geometry and styles intact. The supported-format
// inventory must report the plugin's load/save capabilities per extension.
//
// Usage: VectorFormatsPluginTest [basename]
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "../UltraCanvas/Plugins/Vector/UltraCanvasVectorFormatsPlugin.h"
#include "../UltraCanvas/Plugins/Vector/UltraCanvasCADConverters.h"
#include "../UltraCanvas/Plugins/Vector/UltraCanvasVectorStorage.h"
#include "UltraCanvasSupportedFormats.h"
#include "UltraCanvasImage.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

using namespace UltraCanvas;
using namespace UltraCanvas::VectorStorage;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what.c_str());
    } else {
        std::printf("  ok: %s\n", what.c_str());
    }
}

std::shared_ptr<VectorDocument> BuildTestDocument() {
    auto doc = std::make_shared<VectorDocument>();
    doc->Size = Size2Dd{400, 300};
    auto layer = doc->AddLayer("Artwork");

    auto rect = std::make_shared<VectorRect>();
    rect->Bounds = Rect2Dd{40, 40, 100, 60};
    rect->Style.Fill = Color(255, 0, 0, 255);
    StrokeData rectStroke;
    rectStroke.Fill = Color(0, 0, 255, 255);
    rectStroke.Width = 2.0f;
    rect->Style.Stroke = rectStroke;
    layer->AddChild(rect);

    auto dashLine = std::make_shared<VectorLine>();
    dashLine->Start = Point2Dd(40, 130);
    dashLine->End = Point2Dd(150, 130);
    StrokeData dashStroke;
    dashStroke.Fill = Color(0, 0, 0, 255);
    dashStroke.Width = 2.0f;
    dashStroke.DashArray = {6.0, 3.0};
    dashLine->Style.Stroke = dashStroke;
    layer->AddChild(dashLine);

    auto path = std::make_shared<VectorPath>();
    path->MoveTo(200, 140);
    path->CurveTo(240, 120, 280, 120, 300, 160);
    path->CurveTo(280, 200, 240, 200, 200, 160);
    path->ClosePath();
    path->Style.Fill = Color(90, 40, 160, 255);
    layer->AddChild(path);

    auto text = std::make_shared<VectorText>();
    text->Position = Point2Dd(40, 250);
    text->BaseStyle.FontFamily = "Liberation Sans";
    text->BaseStyle.FontSize = 18.0f;
    text->SetText("Hello Plugin");
    text->Style.Fill = Color(20, 20, 20, 255);
    layer->AddChild(text);

    return doc;
}

// Every drawable in the imported document, layers/groups flattened.
void CollectDrawables(const VectorElement& e,
                      std::vector<const VectorElement*>& out) {
    if (const auto* g = dynamic_cast<const VectorGroup*>(&e)) {
        for (const auto& child : g->Children) {
            if (child) CollectDrawables(*child, out);
        }
        return;
    }
    out.push_back(&e);
}

std::vector<const VectorElement*> Drawables(const VectorDocument& doc) {
    std::vector<const VectorElement*> out;
    for (const auto& layer : doc.Layers) {
        if (layer) CollectDrawables(*layer, out);
    }
    return out;
}

bool HasFillColor(const VectorDocument& doc, const Color& want, int tol) {
    for (const VectorElement* e : Drawables(doc)) {
        if (!e->Style.Fill) continue;
        if (const Color* c = std::get_if<Color>(&*e->Style.Fill)) {
            if (std::abs(c->r - want.r) <= tol && std::abs(c->g - want.g) <= tol &&
                std::abs(c->b - want.b) <= tol) {
                return true;
            }
        }
    }
    return false;
}

const VectorText* FindText(const VectorDocument& doc) {
    for (const VectorElement* e : Drawables(doc)) {
        if (e->Type == VectorElementType::Text) {
            return static_cast<const VectorText*>(e);
        }
    }
    return nullptr;
}

bool HasDash(const VectorDocument& doc) {
    for (const VectorElement* e : Drawables(doc)) {
        if (e->Style.Stroke && !e->Style.Stroke->DashArray.empty()) return true;
    }
    return false;
}

}   // namespace

int main(int argc, char** argv) {
    std::string base = argc > 1 ? argv[1] : "vector_plugin_test";
    UCImage::InitializeImageSubsysterm("VectorFormatsPluginTest");
    RegisterVectorFormatsPlugin();

    auto doc = BuildTestDocument();
    auto element = CreateVectorElement("test", 0, 0, 400, 300);
    element->SetDocument(doc);

    // ===== Registry wiring =====
    Check(UltraCanvasGraphicsPluginRegistry::GetPluginByName(
                  "UltraCanvas Vector Formats Plugin") != nullptr,
          "plugin registered");
    auto saveExts = UltraCanvasGraphicsPluginRegistry::GetSupportedSaveExtensions();
    for (const char* ext : {"svg", "xar", "eps", "cdr", "pdf", "emf", "wmf",
                            "ai", "dxf", "dwg"}) {
        Check(std::find(saveExts.begin(), saveExts.end(), ext) != saveExts.end(),
              std::string("registry lists save extension ") + ext);
    }
    Check(CanSaveGraphicsFile("x.dxf"), "CanSaveGraphicsFile(dxf)");
    Check(!CanSaveGraphicsFile("x.docx"), "CanSaveGraphicsFile rejects docx");

    // ===== Supported-format inventory =====
    {
        auto dxf = UltraCanvasSupportedFormats::FindByExtension("dxf");
        Check(dxf && dxf->canLoad && dxf->canSave,
              "inventory: dxf loads and saves");
        auto cdr = UltraCanvasSupportedFormats::FindByExtension("cdr");
        Check(cdr && cdr->canSave, "inventory: cdr saves");
        auto emf = UltraCanvasSupportedFormats::FindByExtension("emf");
        Check(emf && emf->canLoad && emf->canSave,
              "inventory: emf loads and saves");
    }

    // ===== Save through the registry, validate each file =====
    bool haveDwgTool =
            !VectorConverter::DWGConverter::FindDxf2Dwg().empty() &&
            !VectorConverter::DWGConverter::FindDwg2Dxf().empty();
    for (const char* ext : {"svg", "xar", "eps", "cdr", "pdf", "emf", "wmf",
                            "ai", "dxf", "dwg"}) {
        std::string path = base + "." + ext;
        std::remove(path.c_str());
        bool saved = SaveGraphicsFile(element, path);
        if (std::string(ext) == "dwg" && !haveDwgTool) {
            Check(!saved, "dwg save declines cleanly without LibreDWG");
            continue;
        }
        Check(saved, std::string("SaveGraphicsFile(") + ext + ")");
        if (!saved) continue;
        auto converter =
                UltraCanvasVectorFormatsPlugin::CreateConverterForExtension(ext);
        Check(converter && converter->ValidateFile(path),
              std::string("saved ") + ext + " passes format validation");
    }

    // ===== Load back through the registry (formats with readers) =====
    std::vector<std::string> loadable = {"svg", "xar", "emf", "wmf", "dxf"};
    if (haveDwgTool) loadable.push_back("dwg");
    for (const std::string& ext : loadable) {
        std::string path = base + "." + ext;
        auto loaded = LoadGraphicsFile(path);
        auto vecEl = std::dynamic_pointer_cast<UltraCanvasVectorElement>(loaded);
        Check(vecEl && vecEl->HasDocument(),
              "LoadGraphicsFile(" + ext + ") yields a vector element");
        if (!vecEl || !vecEl->HasDocument()) continue;
        const VectorDocument& back = *vecEl->GetDocument();

        Check(std::fabs(back.Size.width - 400) < 2 &&
                      std::fabs(back.Size.height - 300) < 2,
              ext + ": page size survives");
        Check(Drawables(back).size() >= 4, ext + ": all elements survive");
        // The DWG chain passes through LibreDWG, whose DXF output carries
        // only the ACI colour - accept the nearest-palette quantisation.
        int colorTol = ext == "dwg" ? 60 : 4;
        Check(HasFillColor(back, Color(255, 0, 0, 255), colorTol),
              ext + ": red fill survives");
        Check(HasFillColor(back, Color(90, 40, 160, 255), colorTol),
              ext + ": purple bezier fill survives");
        Check(HasDash(back), ext + ": dash pattern survives");
        const VectorText* text = FindText(back);
        Check(text != nullptr, ext + ": text survives");
        if (text) {
            std::string content;
            for (const auto& span : text->Spans) content += span.Text;
            Check(content == "Hello Plugin", ext + ": text content intact");
            Check(std::fabs(text->Position.x - 40) < 2 &&
                          std::fabs(text->Position.y - 250) < 6,
                  ext + ": text anchor position survives");
            Check(std::fabs(text->BaseStyle.FontSize - 18) < 1.5,
                  ext + ": font size survives");
        }
    }
    if (!haveDwgTool) {
        std::printf("  note: LibreDWG tools not found; DWG round trip skipped\n");
    }

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
