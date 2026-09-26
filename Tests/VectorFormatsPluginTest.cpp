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
// Version: 1.1.0
// Last Modified: 2026-09-16
// Author: UltraCanvas Framework

#include "UltraCanvasVectorFormatsPlugin.h"
#include "../UltraCanvas/Plugins/Vector/UltraCanvasCADConverters.h"
#include "DataFormats/UltraCanvasVectorStorage.h"
#include "UltraCanvasSupportedFormats.h"
#include "UltraCanvasVectorPreview.h"
#include "UltraCanvasImage.h"

#include <algorithm>
#include <functional>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

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

    // ===== Capability flags say what the writers implement =====
    {
        auto svg = UltraCanvasVectorFormatsPlugin::CreateConverterForExtension("svg");
        auto xar = UltraCanvasVectorFormatsPlugin::CreateConverterForExtension("xar");
        Check(svg && xar, "svg and xar converters exist");
        if (svg) {
            auto c = svg->GetCapabilities();
            Check(c.SupportsLinearGradient && c.SupportsRadialGradient && c.SupportsPattern,
                  "svg: gradients and patterns are written");
            Check(!c.SupportsClipping && !c.SupportsMasking,
                  "svg: clipping and masking are not claimed until implemented");
        }
        if (xar) {
            auto c = xar->GetCapabilities();
            Check(c.SupportsLayers && c.SupportsGroups && c.SupportsText,
                  "xar: layers, groups and text are written");
            Check(c.MaxGradientStops > 2, "xar: multistage fills keep every stop");
            Check(c.SupportsDropShadow && c.SupportsNonDestructiveEffects && c.SupportsBlendModes &&
                  c.SupportsConicalGradient && c.SupportsVariableStrokeWidth,
                  "xar: shadows, feathers, transparency mixes, conical fills and width profiles are written");
            Check(!c.SupportsFilters && !c.SupportsMasking && !c.SupportsClipping && !c.SupportsPages,
                  "xar: filters, masks, clipping and pages are not claimed");
        }
    }

    // ===== Extension lists come from the converters =====
    // Nobody maintains them: every extension a converter declares is readable
    // exactly when it can import and writable exactly when it can export.
    {
        UltraCanvasVectorFormatsPlugin plugin;
        const auto readable = plugin.GetSupportedExtensions();
        const auto writable = plugin.GetSaveExtensions();
        auto has = [](const std::vector<std::string>& v, const std::string& e) {
            return std::find(v.begin(), v.end(), e) != v.end();
        };
        bool consistent = true;
        for (const std::string ext : {"svg", "svgz", "xar", "web", "eps", "cdr", "pdf", "emf",
                                       "wmf", "ai", "dxf", "dwg", "dwt", "dws", "sv$"}) {
            auto converter = UltraCanvasVectorFormatsPlugin::CreateConverterForExtension(ext);
            if (!converter) { consistent = false; std::printf("    no converter for %s\n", ext.c_str()); continue; }
            if (has(readable, ext) != converter->CanImport() || has(writable, ext) != converter->CanExport()) {
                consistent = false;
                std::printf("    list and converter disagree on %s\n", ext.c_str());
            }
        }
        Check(consistent, "readable/writable lists match each converter's CanImport/CanExport");
        Check(has(readable, "svgz"), ".svgz is readable (declared by the SVG converter)");
#ifdef ULTRACANVAS_HAS_CDR_PLUGIN
        Check(has(readable, "cdr"), ".cdr is readable when the CDR plugin is built");
#else
        Check(!has(readable, "cdr"), ".cdr is not readable without the CDR plugin");
#endif
    }

#ifdef ULTRACANVAS_HAS_CDR_PLUGIN
    // ===== CorelDRAW bitmaps keep their transparency =====
    // CorelDRAW stores a transparent bitmap as a colour image plus an 8-bit
    // mask; libcdr reads only the colour image. detailed.cdr's four card
    // shadows came out as solid black boxes and its logo overlay as an opaque
    // white sheet hiding the cards. The importer puts the masks back, so those
    // five images arrive as RGBA PNG and the opaque background stays a BMP.
    {
        auto converter = UltraCanvasVectorFormatsPlugin::CreateConverterForExtension("cdr");
        VectorConverter::ConversionOptions options;
        auto doc = converter ? converter->Import(std::string(VECTOR_SAMPLES_DIR) + "/CDR/detailed.cdr", options)
                             : nullptr;
        Check(doc != nullptr, "detailed.cdr imports");
        int png = 0, bmp = 0, clipped = 0;
        std::function<void(const std::shared_ptr<VectorElement>&)> walk =
                [&](const std::shared_ptr<VectorElement>& e) {
            if (!e) return;
            if (e->Style.ClipPath && !e->Style.ClipPath->empty() && doc->GetDefinition(*e->Style.ClipPath)) ++clipped;
            if (auto im = std::dynamic_pointer_cast<VectorImage>(e)) {
                if (im->Source.rfind("data:image/png;base64,", 0) == 0) ++png;
                else if (im->Source.rfind("data:image/bmp;base64,", 0) == 0) ++bmp;
            }
            if (auto g = std::dynamic_pointer_cast<VectorGroup>(e))
                for (const auto& c : g->Children) walk(c);
        };
        if (doc) for (const auto& l : doc->Layers) walk(l);
        const std::string counts = " (png=" + std::to_string(png) + ", bmp=" + std::to_string(bmp) +
                                   ", clipped=" + std::to_string(clipped) + ")";
#ifdef ULTRACANVAS_VENDORED_LIBCDR
        // The patched libcdr also draws the 8 PowerClips - the cards'
        // leaves, waves and gloss - as clipped groups, 6 of them holding a
        // masked bitmap of their own.
        Check(png == 11 && bmp == 1, "detailed.cdr: every masked bitmap carries alpha" + counts);
        Check(clipped == 8, "detailed.cdr: the 8 PowerClips arrive clipped to their frames" + counts);
#else
        Check(png == 5 && bmp == 1,
              "detailed.cdr: the 5 masked bitmaps carry alpha, the opaque one stays as it was" + counts);
#endif
    }
#endif

    // ===== The DWG family reaches the DWG converter =====
    // A drawing arrives as .dwg, as a template (.dwt), a standards file
    // (.dws) or an automatic save (.sv$) - one format under four names.
    {
        auto loadExts = UltraCanvasGraphicsPluginRegistry::GetSupportedExtensions();
        for (const char* ext : {"dwg", "dwt", "dws", "sv$"}) {
            Check(std::find(loadExts.begin(), loadExts.end(), ext) != loadExts.end(),
                  std::string("registry lists load extension ") + ext);
            auto converter =
                    UltraCanvasVectorFormatsPlugin::CreateConverterForExtension(ext);
            Check(converter &&
                          converter->GetFormat() == VectorConverter::VectorFormat::DWG,
                  std::string(ext) + " dispatches to the DWG converter");
        }
        // .bak is settled by content, so it is claimed for no bare extension
        // and advertised nowhere.
        Check(UltraCanvasVectorFormatsPlugin::CreateConverterForExtension("bak") ==
                      nullptr,
              "bak alone is not claimed as a format");
        Check(std::find(loadExts.begin(), loadExts.end(), "bak") == loadExts.end(),
              "registry does not advertise bak");
        // The registry still reaches the plugin for one: an unadvertised
        // suffix falls through to the plugins' own CanHandle, and the DWG
        // converter answers for the header it finds.
        const std::string bak = base + ".dwg_backup.bak";
        {
            std::ofstream f(bak, std::ios::binary | std::ios::trunc);
            f << "AC1015";   // the version magic alone settles the question
        }
        Check(CanHandleGraphicsFile(bak), "a .bak holding a drawing is recognised");
        const std::string notBak = base + ".editor_backup.bak";
        {
            std::ofstream f(notBak, std::ios::binary | std::ios::trunc);
            f << "# somebody else's backup\n";
        }
        Check(!CanHandleGraphicsFile(notBak),
              "a .bak holding something else is not");
        std::remove(bak.c_str());
        std::remove(notBak.c_str());
    }

    // ===== The core-side preview seam =====
    // Registering the plugin must also lend core its readers, or the media
    // viewer's preview pane and the Filer's thumbnails stay blind to every
    // format core cannot read itself - which is every one of them.
    {
        for (const char* ext : {"dxf", "dwg", "dwt", "dws", "sv$", "xar", "emf", "wmf"}) {
            Check(CanPreviewVectorExtension(ext),
                  std::string("preview seam reads ") + ext);
        }
        Check(!CanPreviewVectorExtension("png"), "preview seam declines png");
        auto seamExts = PreviewableVectorExtensions();
        Check(std::find(seamExts.begin(), seamExts.end(), "dwg") != seamExts.end(),
              "preview seam lists dwg");
    }

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
        for (const char* ext : {"dwt", "dws", "sv$"}) {
            auto f = UltraCanvasSupportedFormats::FindByExtension(ext);
            Check(f && f->canLoad && f->category == MediaFormatCategory::Vector,
                  std::string("inventory: ") + ext + " loads as vector graphics");
        }
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

    // ===== The preview seam on a real drawing =====
    // The DXF the writer just produced, read back through the seam and drawn:
    // the whole path the media viewer's preview pane and the Filer's
    // thumbnails take for a format core has no reader for.
    {
        auto drawing = LoadVectorPreviewDocument(base + ".dxf");
        Check(drawing != nullptr, "preview seam reads a drawing into a document");
        if (drawing) {
            auto pm = RenderVectorDocumentPixmap(*drawing, 160, 120);
            Check(pm && pm->GetWidth() == 160 && pm->GetHeight() == 120,
                  "a document renders to a pixmap of the asked-for size");
        }
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
