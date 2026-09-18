// UltraCanvas/Plugins/Vector/UltraCanvasXARConverter.cpp
// XAR (Xara) Vector Format Converter - Specification-Compliant Implementation
// Version: 2.0.0
// Last Modified: 2025-01-20
// Author: UltraCanvas Framework

#include "UltraCanvasXARConverter.h"
#include "DataFormats/UltraCanvasVectorStorage.h"
#include "DataFormats/UltraCanvasVectorPathOps.h"
#ifdef ULTRACANVAS_HAS_XAR_PLUGIN
#include "XAR/UltraCanvasXARPlugin.h"
#endif
#include <fstream>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <sstream>
#include <cstring>
#include <zlib.h>
#include <stack>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <map>
#include <variant>

namespace UltraCanvas {
    namespace VectorConverter {

        using namespace VectorStorage;
        using namespace XARTags;
        using namespace XARPathVerbs;
        using namespace XARCoordUtils;
        using namespace XARColourUtils;

// ===== XAR CONVERTER IMPLEMENTATION CLASS =====

        class XARConverter::Impl {
        public:
            Impl() = default;
            ~Impl() = default;

            // Import from file
            std::shared_ptr<VectorDocument> ImportFromFile(
                    const std::string& filename,
                    const ConversionOptions& options,
                    const XARConversionOptions& xarOptions);

            // Import from memory
            std::shared_ptr<VectorDocument> ImportFromMemory(
                    const uint8_t* data, size_t size,
                    const ConversionOptions& options,
                    const XARConversionOptions& xarOptions);

            // Export to file
            bool ExportToFile(
                    const VectorDocument& document,
                    const std::string& filename,
                    const ConversionOptions& options,
                    const XARConversionOptions& xarOptions);

            // Export to memory
            std::vector<uint8_t> ExportToMemory(
                    const VectorDocument& document,
                    const ConversionOptions& options,
                    const XARConversionOptions& xarOptions);

        private:
            // ===== EXPORT STATE =====
            struct ExportState {
                std::map<const VectorElement*, uint32_t> elementRefs;
                std::map<size_t, uint32_t> gradientRefs;
                std::map<size_t, uint32_t> patternRefs;
                std::map<Color, uint32_t> colourRefs;
                uint32_t nextRefId = 1;
                uint32_t nextColourId = 1;
                void Reset() {
                    elementRefs.clear();
                    gradientRefs.clear();
                    patternRefs.clear();
                    colourRefs.clear();
                    nextRefId = 1;
                    nextColourId = 1;
                }
            } exportState;

            // Options
            ConversionOptions currentOptions;
            XARConversionOptions currentXarOptions;

            void LogWarning(const std::string& message);
            void ReportProgress(float progress);
        };

// ===== PUBLIC INTERFACE =====

        XARConverter::XARConverter() : impl(std::make_unique<Impl>()) {}
        XARConverter::~XARConverter() = default;

        // What the reader (the XAR plugin's XARDocument, translated to the
        // model) and the writer implement - not what the Xara format can
        // hold. Multi-stage linear / circular / conical fills, flat and
        // gradient transparency with its mixes, shadows and feathers round
        // trip. Arrowheads and width profiles are written as filled shapes
        // (Xara shows them, the reader gets shapes back). Bitmap / fractal
        // fills, bevel, contour, blend, mould, ClipView, live effects,
        // brushes and pages are read-and-dropped or not written, and a
        // caller choosing a target format by capability must know it.
        FormatCapabilities XARConverter::GetCapabilities() const {
            FormatCapabilities caps;

            // Basic shapes: rectangles and ellipses natively when axis-
            // aligned, everything else as a path.
            caps.SupportsRectangle = true;
            caps.SupportsCircle = true;
            caps.SupportsEllipse = true;
            caps.SupportsLine = true;
            caps.SupportsPolyline = true;
            caps.SupportsPolygon = true;
            caps.SupportsPath = true;

            // Path features: PathOps normalises quadratics and arcs to
            // cubics on the way out, so they survive as geometry.
            caps.SupportsCubicBezier = true;
            caps.SupportsQuadraticBezier = true;
            caps.SupportsArc = true;
            caps.SupportsCompoundPaths = true;

            // Text: stories with styled string chunks; no text on a path,
            // no embedded fonts.
            caps.SupportsText = true;
            caps.SupportsTextPath = false;
            caps.SupportsRichText = true;
            caps.SupportsEmbeddedFonts = false;

            // Fills & strokes: flat colour, linear / radial / conical
            // gradients with any number of stops (multistage records);
            // patterns flatten; a width profile is baked into the outline.
            caps.SupportsSolidFill = true;
            caps.SupportsLinearGradient = true;
            caps.SupportsRadialGradient = true;
            caps.SupportsConicalGradient = true;
            caps.SupportsMeshGradient = false;
            caps.SupportsPattern = false;
            caps.SupportsDashing = true;
            caps.SupportsVariableStrokeWidth = true;
            caps.MaxGradientStops = SIZE_MAX;

            // Effects: flat and gradient transparency with Xara's mixes
            // (the blend modes), shadows and feathers.
            caps.SupportsOpacity = true;
            caps.SupportsBlendModes = true;
            caps.SupportsFilters = false;
            caps.SupportsClipping = false;
            caps.SupportsMasking = false;
            caps.SupportsDropShadow = true;

            // Structure: layers and groups; symbols flatten; one page.
            caps.SupportsGroups = true;
            caps.SupportsLayers = true;
            caps.SupportsSymbols = false;
            caps.SupportsPages = false;

            caps.SupportsNonDestructiveEffects = true;

            return caps;
        }

        std::shared_ptr<VectorDocument> XARConverter::Import(
                const std::string& filename,
                const ConversionOptions& options) {
            return impl->ImportFromFile(filename, options, xarOptions);
        }

        std::shared_ptr<VectorDocument> XARConverter::ImportFromString(
                const std::string& data,
                const ConversionOptions& options) {
            return impl->ImportFromMemory(
                    reinterpret_cast<const uint8_t*>(data.data()),
                    data.size(), options, xarOptions);
        }

        std::shared_ptr<VectorDocument> XARConverter::ImportFromStream(
                std::istream& stream,
                const ConversionOptions& options) {
            // Read entire stream into memory
            stream.seekg(0, std::ios::end);
            size_t size = stream.tellg();
            stream.seekg(0, std::ios::beg);

            std::vector<uint8_t> data(size);
            stream.read(reinterpret_cast<char*>(data.data()), size);

            return impl->ImportFromMemory(data.data(), size, options, xarOptions);
        }

        bool XARConverter::Export(
                const VectorDocument& document,
                const std::string& filename,
                const ConversionOptions& options) {
            return impl->ExportToFile(document, filename, options, xarOptions);
        }

        std::string XARConverter::ExportToString(
                const VectorDocument& document,
                const ConversionOptions& options) {
            auto data = impl->ExportToMemory(document, options, xarOptions);
            return std::string(data.begin(), data.end());
        }

        bool XARConverter::ExportToStream(
                const VectorDocument& document,
                std::ostream& stream,
                const ConversionOptions& options) {
            auto data = impl->ExportToMemory(document, options, xarOptions);
            stream.write(reinterpret_cast<const char*>(data.data()), data.size());
            return stream.good();
        }

        bool XARConverter::ValidateFile(const std::string& filename) const {
            std::ifstream file(filename, std::ios::binary);
            if (!file.is_open()) return false;

            uint8_t signature[8];
            file.read(reinterpret_cast<char*>(signature), sizeof(signature));

            return std::memcmp(signature, XAR_SIGNATURE, sizeof(XAR_SIGNATURE)) == 0;
        }

        bool XARConverter::ValidateData(const std::string& data) const {
            if (data.size() < sizeof(XAR_SIGNATURE)) return false;
            return std::memcmp(data.data(), XAR_SIGNATURE, sizeof(XAR_SIGNATURE)) == 0;
        }

// ===== SPEC-CORRECT TAG NUMBERS =====
//
// Tags from the Xar Format Specification Appendix A, matching the XARTag
// enum in Plugins/Vector/XAR/UltraCanvasXARPlugin.h. (The legacy XARTags
// namespace in UltraCanvasXARConverter.h predates the spec-verified reader
// and carries wrong numbers; both the writer below and the reader here use
// these constants instead.)

        namespace {
            namespace XarOut {
                constexpr uint32_t Up = 0;
                constexpr uint32_t Down = 1;
                constexpr uint32_t FileHeader = 2;
                constexpr uint32_t EndOfFile = 3;
                constexpr uint32_t StartCompression = 30;
                constexpr uint32_t EndCompression = 31;
                constexpr uint32_t Document = 40;
                constexpr uint32_t Chapter = 41;
                constexpr uint32_t Spread = 42;
                constexpr uint32_t Layer = 43;
                constexpr uint32_t SpreadInformation = 45;
                constexpr uint32_t LayerDetails = 48;
                constexpr uint32_t DefineRGBColour = 50;
                constexpr uint32_t DefineComplexColour = 51;
                constexpr uint32_t Path = 100;
                constexpr uint32_t PathFilled = 101;
                constexpr uint32_t PathStroked = 102;
                constexpr uint32_t PathFilledStroked = 103;
                constexpr uint32_t Group = 104;
                constexpr uint32_t FlatFill = 150;
                constexpr uint32_t LineColour = 151;
                constexpr uint32_t LineWidth = 152;
                constexpr uint32_t LinearFill = 153;
                constexpr uint32_t CircularFill = 154;
                constexpr uint32_t EllipticalFill = 155;
                constexpr uint32_t ConicalFill = 156;
                constexpr uint32_t FlatTransparentFill = 166;
                constexpr uint32_t LinearTransparentFill = 167;
                constexpr uint32_t CircularTransparentFill = 168;
                constexpr uint32_t ConicalTransparentFill = 170;
                constexpr uint32_t LineTransparency = 173;
                constexpr uint32_t ArrowHead = 185;      // the end of the line
                constexpr uint32_t ArrowTail = 186;      // the start
                constexpr uint32_t UserValue = 189;
                constexpr uint32_t StartCap = 174;
                constexpr uint32_t EndCap = 175;
                constexpr uint32_t JoinStyle = 176;
                constexpr uint32_t MitreLimit = 177;
                constexpr uint32_t DashStyle = 183;
                constexpr uint32_t DefineDash = 184;
                constexpr uint32_t DefineDashScaled = 188;
                constexpr uint32_t FlatFillNone = 190;
                constexpr uint32_t LineColourNone = 193;
                constexpr uint32_t EllipseSimple = 1000;
                constexpr uint32_t RectangleSimple = 1100;
                constexpr uint32_t RectangleSimpleRounded = 1104;
                constexpr uint32_t FontDefTrueType = 2000;
                constexpr uint32_t TextStorySimple = 2100;
                constexpr uint32_t TextLine = 2200;
                constexpr uint32_t TextString = 2201;
                constexpr uint32_t TextEOL = 2203;
                constexpr uint32_t TextJustificationLeft = 2902;
                constexpr uint32_t TextJustificationCentre = 2903;
                constexpr uint32_t TextJustificationRight = 2904;
                constexpr uint32_t TextFontSize = 2906;
                constexpr uint32_t TextFontTypeface = 2907;
                constexpr uint32_t TextBoldOn = 2908;
                constexpr uint32_t TextBoldOff = 2909;
                constexpr uint32_t TextItalicOn = 2910;
                constexpr uint32_t TextItalicOff = 2911;
                constexpr uint32_t TextUnderlineOn = 2912;
                constexpr uint32_t TextUnderlineOff = 2913;
                constexpr uint32_t ShadowController = 4050;
                constexpr uint32_t LinearFillMultistage = 4075;
                constexpr uint32_t CircularFillMultistage = 4076;
                constexpr uint32_t ConicalFillMultistage = 4078;
                constexpr uint32_t Feather = 4086;
            }

            // Xara's default arrowheads by reference, as the Xara LX sources
            // number them (Appendix B of the format specification): -1
            // straight arrow, -2 angled arrow, -3 rounded arrow, -4 spot, -5
            // diamond, -6 arrow feather, -7 arrow feather 2, -8 hollow
            // diamond. The repo's Xara samples carry no arrowheads, so this
            // numbering is not verified against a Designer export here; a
            // TAG_DEFINEARROW (positive reference) reads as the triangle.
            int32_t NativeArrowRef(ArrowheadKind k) {
                switch (k) {
                    case ArrowheadKind::Triangle: return -1;
                    case ArrowheadKind::AngledArrow: return -2;
                    case ArrowheadKind::RoundedArrow: return -3;
                    case ArrowheadKind::Circle: return -4;
                    case ArrowheadKind::Diamond: return -5;
                    case ArrowheadKind::Feather: return -6;
                    case ArrowheadKind::Feather2: return -7;
                    case ArrowheadKind::HollowDiamond: return -8;
                    default: return 0;
                }
            }
            ArrowheadKind KindFromArrowRef(int32_t ref) {
                switch (ref) {
                    case -1: return ArrowheadKind::Triangle;
                    case -2: return ArrowheadKind::AngledArrow;
                    case -3: return ArrowheadKind::RoundedArrow;
                    case -4: return ArrowheadKind::Circle;
                    case -5: return ArrowheadKind::Diamond;
                    case -6: return ArrowheadKind::Feather;
                    case -7: return ArrowheadKind::Feather2;
                    case -8: return ArrowheadKind::HollowDiamond;
                    case 0: return ArrowheadKind::NoArrowhead;
                    default: return ArrowheadKind::Triangle;   // a custom definition
                }
            }

            // The user values this converter writes on objects: what it baked
            // into shapes, so the reader can rebuild the stroke.
            constexpr const char* kLineGalleryKey = "UltraCanvas.LineGallery";
            constexpr const char* kArrowScaleKey = "UltraCanvas.ArrowScale";

            std::map<std::string, std::string> ParseMarker(const std::string& value) {
                std::map<std::string, std::string> out;
                size_t pos = 0;
                while (pos <= value.size()) {
                    size_t semi = value.find(';', pos);
                    if (semi == std::string::npos) semi = value.size();
                    const std::string item = value.substr(pos, semi - pos);
                    const size_t eq = item.find('=');
                    if (eq != std::string::npos) out[item.substr(0, eq)] = item.substr(eq + 1);
                    pos = semi + 1;
                }
                return out;
            }
            double MarkerNumber(const std::map<std::string, std::string>& m, const char* key, double fallback) {
                auto it = m.find(key);
                if (it == m.end() || it->second.empty()) return fallback;
                return std::atof(it->second.c_str());
            }

        }   // anonymous namespace

// ===== IMPLEMENTATION: IMPORT =====
//
// Reading is the XAR plugin's job: XARDocument is the spec-verified parser
// (compressed and uncompressed files, every fill and transparency record,
// shadow, feather, text stories). The translator below walks its node tree
// into the vector model, so the converter and the viewer read one grammar.

#ifdef ULTRACANVAS_HAS_XAR_PLUGIN
        namespace {
            class XarTranslator {
            public:
                XarTranslator(UltraCanvas::XARDocument& document,
                              std::function<void(const std::string&)> warnFn)
                        : xd(document), warn(std::move(warnFn)) {}

                std::shared_ptr<VectorDocument> Run() {
                    auto doc = std::make_shared<VectorDocument>();
                    // The plugin's "pixels" are 72 dpi points.
                    pageW = xd.GetPageWidth(0);
                    pageH = xd.GetPageHeight(0);
                    if (pageW <= 0) pageW = 595;
                    if (pageH <= 0) pageH = 842;
                    doc->Size = Size2Dd{pageW, pageH};
                    doc->ViewBox = Rect2Dd{0, 0, pageW, pageH};
                    if (!xd.GetProducer().empty()) doc->Metadata["producer"] = xd.GetProducer();

                    std::vector<UltraCanvas::XARNodePtr> spreads;
                    Collect(xd.GetRoot(), UltraCanvas::XARNodeType::Spread, spreads);
                    if (spreads.size() > 1)
                        warn("XAR import: the drawing has " + std::to_string(spreads.size()) +
                             " pages; only the first is imported");
                    UltraCanvas::XARNodePtr scope = spreads.empty() ? xd.GetRoot() : spreads.front();
                    if (!scope) return doc;

                    std::shared_ptr<VectorLayer> loose;   // objects outside any layer
                    for (const auto& child : scope->children) {
                        if (!child) continue;
                        if (child->type == UltraCanvas::XARNodeType::Layer) {
                            ImportLayer(std::static_pointer_cast<UltraCanvas::XARLayerNode>(child), *doc);
                        } else if (child->type == UltraCanvas::XARNodeType::Page ||
                                   child->type == UltraCanvas::XARNodeType::Unknown) {
                            continue;
                        } else {
                            if (!loose) loose = doc->AddLayer("Layer 1");
                            Translate(child, *loose);
                        }
                    }
                    if (spreads.empty() && doc->Layers.empty()) {
                        // No spread at all: take every layer anywhere.
                        std::vector<UltraCanvas::XARNodePtr> layers;
                        Collect(xd.GetRoot(), UltraCanvas::XARNodeType::Layer, layers);
                        for (const auto& l : layers)
                            ImportLayer(std::static_pointer_cast<UltraCanvas::XARLayerNode>(l), *doc);
                    }
                    ReportSkipped();
                    return doc;
                }

            private:
                UltraCanvas::XARDocument& xd;
                std::function<void(const std::string&)> warn;
                double pageW = 595, pageH = 842;
                std::map<std::string, int> skipped;
                int guideLayers = 0;

                // ----- coordinates: millipoints Y-up to points Y-down -----
                Point2Dd Pt(const Point2Di& mp) const {
                    return Point2Dd(mp.x / 1000.0, pageH - mp.y / 1000.0);
                }
                Point2Dd PtD(double xMp, double yMp) const {
                    return Point2Dd(xMp / 1000.0, pageH - yMp / 1000.0);
                }
                static double Len(int32_t mp) { return mp / 1000.0; }
                static Point2Dd Through(const UltraCanvas::XARMatrix& m, double x, double y) {
                    return Point2Dd(m.a * x + m.c * y + m.e, m.b * x + m.d * y + m.f);
                }

                static void Collect(const UltraCanvas::XARNodePtr& node, UltraCanvas::XARNodeType type,
                                    std::vector<UltraCanvas::XARNodePtr>& out) {
                    if (!node) return;
                    if (node->type == type) out.push_back(node);
                    for (const auto& c : node->children) Collect(c, type, out);
                }

                void Skip(const std::string& what) { ++skipped[what]; }
                void ReportSkipped() {
                    if (guideLayers > 0)
                        warn("XAR import: " + std::to_string(guideLayers) + " guide layer(s) not imported");
                    if (skipped.empty()) return;
                    std::ostringstream msg;
                    msg << "XAR import: not representable, approximated or dropped:";
                    for (const auto& [what, n] : skipped) msg << " " << what << " (x" << n << ")";
                    warn(msg.str());
                }

                // ----- tree -----
                void ImportLayer(const std::shared_ptr<UltraCanvas::XARLayerNode>& ln, VectorDocument& doc) {
                    if (ln->isGuide) { ++guideLayers; return; }
                    auto layer = doc.AddLayer(ln->name.empty() ? "Layer " + std::to_string(doc.Layers.size() + 1) : ln->name);
                    layer->Visible = ln->visible;
                    layer->Locked = ln->locked;
                    layer->Plottable = ln->printable;
                    for (const auto& c : ln->children) Translate(c, *layer);
                }

                void TranslateChildren(const UltraCanvas::XARNodePtr& n, VectorGroup& into) {
                    for (const auto& c : n->children) Translate(c, into);
                }

                // The node's children as one element: the single child
                // itself, or a group of them.
                std::shared_ptr<VectorElement> ChildrenAsElement(const UltraCanvas::XARNodePtr& n) {
                    auto g = std::make_shared<VectorGroup>();
                    TranslateChildren(n, *g);
                    if (g->Children.empty()) return nullptr;
                    if (g->Children.size() == 1) {
                        auto only = g->Children.front();
                        g->Children.clear();
                        only->Parent.reset();
                        return only;
                    }
                    return g;
                }

                void Translate(const UltraCanvas::XARNodePtr& n, VectorGroup& into) {
                    if (!n) return;
                    using UltraCanvas::XARNodeType;
                    std::shared_ptr<VectorElement> made;
                    switch (n->type) {
                        case XARNodeType::Layer:
                        case XARNodeType::Group: {
                            auto marker = n->userValues.find(kLineGalleryKey);
                            if (marker != n->userValues.end()) {
                                made = RebuildLineGallery(n, marker->second);
                                if (!made) return;
                                break;
                            }
                            auto g = std::make_shared<VectorGroup>();
                            TranslateChildren(n, *g);
                            if (n->hasTransparency) ApplyTransparency(n->transparency, g->Style);
                            made = g;
                            break;
                        }
                        case XARNodeType::Shadow: {
                            auto target = ChildrenAsElement(n);
                            if (!target) return;
                            const auto& sh = static_cast<const UltraCanvas::XARShadowNode&>(*n);
                            ShadowEffect e;
                            e.Kind = sh.shadowType == 0 ? ShadowKind::Floor
                                   : sh.shadowType == 2 ? ShadowKind::Glow : ShadowKind::Wall;
                            e.Offset = Point2Dd(Len(sh.offsetX), -Len(sh.offsetY));
                            e.Blur = static_cast<float>(Len(sh.blurRadius));
                            e.Colour = Color(sh.shadowColor.r, sh.shadowColor.g, sh.shadowColor.b, 255);
                            e.Darkness = sh.shadowColor.a / 255.0f;
                            target->Effects.Shadow = e;
                            made = target;
                            break;
                        }
                        case XARNodeType::Feather: {
                            // A feather with children of its own feathers them;
                            // as an attribute child it is picked up by the object.
                            auto target = ChildrenAsElement(n);
                            if (!target) return;
                            target->Effects.Feather = FeatherEffect{static_cast<float>(Len(
                                    static_cast<const UltraCanvas::XARFeatherNode&>(*n).featherRadius))};
                            made = target;
                            break;
                        }
                        case XARNodeType::Path:
                            made = MakePath(static_cast<const UltraCanvas::XARPathNode&>(*n));
                            break;
                        case XARNodeType::Rectangle:
                            made = MakeRect(static_cast<const UltraCanvas::XARRectangleNode&>(*n));
                            break;
                        case XARNodeType::Ellipse:
                            made = MakeEllipse(static_cast<const UltraCanvas::XAREllipseNode&>(*n));
                            break;
                        case XARNodeType::Polygon:
                            made = MakePolygon(static_cast<const UltraCanvas::XARPolygonNode&>(*n));
                            break;
                        case XARNodeType::TextStory:
                            made = MakeText(static_cast<const UltraCanvas::XARTextStoryNode&>(*n));
                            break;
                        case XARNodeType::Bitmap:
                        case XARNodeType::ContonedBitmap:
                            made = MakeImage(static_cast<const UltraCanvas::XARBitmapNode&>(*n));
                            break;
                        case XARNodeType::ClipView:
                            Skip("ClipView (contents kept, clip dropped)");
                            made = ChildrenAsElement(n);
                            break;
                        case XARNodeType::Bevel: Skip("bevel"); made = ChildrenAsElement(n); break;
                        case XARNodeType::Contour: Skip("contour"); made = ChildrenAsElement(n); break;
                        case XARNodeType::Blend: Skip("blend"); made = ChildrenAsElement(n); break;
                        case XARNodeType::Mould: Skip("mould"); made = ChildrenAsElement(n); break;
                        case XARNodeType::LiveEffect: Skip("live effect"); made = ChildrenAsElement(n); break;
                        case XARNodeType::Brush: Skip("brush"); made = ChildrenAsElement(n); break;
                        case XARNodeType::Text:
                        case XARNodeType::TextLine:
                        case XARNodeType::TextString:
                        case XARNodeType::TextKern:
                            return;   // parts of a story, consumed by MakeText
                        default:
                            TranslateChildren(n, into);
                            return;
                    }
                    if (!made) return;
                    ApplyFeatherChild(n, *made);
                    ApplyArrowScale(n, *made);
                    into.AddChild(made);
                }

                void ApplyArrowScale(const UltraCanvas::XARNodePtr& n, VectorElement& e) {
                    auto it = n->userValues.find(kArrowScaleKey);
                    if (it == n->userValues.end() || !e.Style.Stroke.has_value()) return;
                    const auto m = ParseMarker(it->second);
                    e.Style.Stroke->StartArrow.Scale = static_cast<float>(MarkerNumber(m, "start", 1.0));
                    e.Style.Stroke->EndArrow.Scale = static_cast<float>(MarkerNumber(m, "end", 1.0));
                }

                // A group this converter wrote around a baked line gallery: the
                // first child is the object, the rest the shapes Xara shows;
                // the stroke comes back from the marker and, for a brush, the
                // stamp from the first stamped copy placed back into its own
                // space.
                std::shared_ptr<VectorElement> RebuildLineGallery(const UltraCanvas::XARNodePtr& n, const std::string& marker) {
                    auto tmp = std::make_shared<VectorGroup>();
                    TranslateChildren(n, *tmp);
                    if (tmp->Children.empty()) return nullptr;
                    auto element = tmp->Children.front();
                    const auto m = ParseMarker(marker);
                    StrokeData st;
                    Color c(0, 0, 0, 255);
                    auto ci = m.find("colour");
                    if (ci != m.end() && ci->second.size() == 6) {
                        const unsigned v = static_cast<unsigned>(std::strtoul(ci->second.c_str(), nullptr, 16));
                        c = Color((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF, static_cast<uint8_t>(MarkerNumber(m, "alpha", 255)));
                    }
                    st.Fill = c;
                    st.Width = static_cast<float>(MarkerNumber(m, "width", 1.0));
                    st.Opacity = static_cast<float>(MarkerNumber(m, "opacity", 1.0));
                    st.LineCap = static_cast<StrokeLineCap>(static_cast<int>(MarkerNumber(m, "cap", 0)));
                    st.LineJoin = static_cast<StrokeLineJoin>(static_cast<int>(MarkerNumber(m, "join", 0)));
                    st.MiterLimit = static_cast<float>(MarkerNumber(m, "mitre", 4.0));
                    auto kind = [&](const char* key) {
                        const int k = static_cast<int>(MarkerNumber(m, key, 0));
                        return static_cast<ArrowheadKind>(std::max(0, std::min(ArrowheadKindCount - 1, k)));
                    };
                    st.StartArrow.Kind = kind("startArrow");
                    st.StartArrow.Scale = static_cast<float>(MarkerNumber(m, "startScale", 1.0));
                    st.EndArrow.Kind = kind("endArrow");
                    st.EndArrow.Scale = static_cast<float>(MarkerNumber(m, "endScale", 1.0));
                    auto list = [&](const char* key) {
                        std::vector<std::string> items;
                        auto it = m.find(key);
                        if (it == m.end()) return items;
                        size_t pos = 0;
                        while (pos <= it->second.size()) {
                            size_t comma = it->second.find(',', pos);
                            if (comma == std::string::npos) comma = it->second.size();
                            items.push_back(it->second.substr(pos, comma - pos));
                            pos = comma + 1;
                        }
                        return items;
                    };
                    for (const auto& d : list("dash")) if (!d.empty()) st.DashArray.push_back(std::atof(d.c_str()));
                    for (const auto& p : list("profile")) {
                        const size_t colon = p.find(':');
                        if (colon == std::string::npos) continue;
                        st.WidthProfile.push_back({static_cast<float>(std::atof(p.substr(0, colon).c_str())),
                                                   static_cast<float>(std::atof(p.substr(colon + 1).c_str()))});
                    }
                    if (MarkerNumber(m, "brush", 0) > 0 && tmp->Children.size() >= 2) {
                        // The stamps group is the last child; its first copy,
                        // placed back through the inverse of its placement, is
                        // the stamp in its own space.
                        auto stamps = std::dynamic_pointer_cast<VectorGroup>(tmp->Children.back());
                        std::shared_ptr<VectorGroup> copy;
                        if (stamps && !stamps->Children.empty()) copy = std::dynamic_pointer_cast<VectorGroup>(stamps->Children.front());
                        const auto mm = list("stampm");
                        if (copy && mm.size() == 6) {
                            const Matrix3x3 placement = Matrix3x3::FromValues(std::atof(mm[0].c_str()), std::atof(mm[1].c_str()),
                                                                              std::atof(mm[2].c_str()), std::atof(mm[3].c_str()),
                                                                              std::atof(mm[4].c_str()), std::atof(mm[5].c_str()));
                            stamps->Children.erase(stamps->Children.begin());
                            copy->Parent.reset();
                            copy->Transform = placement.Inverse();
                            BrushData b;
                            b.Stamp = copy;
                            b.Spacing = static_cast<float>(MarkerNumber(m, "spacing", 1.0));
                            b.Scale = static_cast<float>(MarkerNumber(m, "scale", 1.0));
                            b.Rotate = MarkerNumber(m, "rotate", 1) > 0;
                            st.Brush = b;
                        } else {
                            Skip("brush stroke (stamps kept as shapes)");
                            return tmp;   // keep what Xara shows
                        }
                    }
                    element->Style.Stroke = st;
                    element->Parent.reset();
                    return element;
                }

                // An object's own children are its attribute-derived nodes;
                // a feather among them is the object's.
                void ApplyFeatherChild(const UltraCanvas::XARNodePtr& n, VectorElement& e) {
                    for (const auto& c : n->children) {
                        if (c && c->type == UltraCanvas::XARNodeType::Feather && c->children.empty()) {
                            e.Effects.Feather = FeatherEffect{static_cast<float>(Len(
                                    static_cast<const UltraCanvas::XARFeatherNode&>(*c).featherRadius))};
                        }
                    }
                }

                // ----- shapes -----
                std::shared_ptr<VectorPath> MakePath(const UltraCanvas::XARPathNode& n) {
                    auto path = std::make_shared<VectorPath>();
                    auto map = [&](const Point2Di& mp) {
                        if (n.hasTransform) {
                            const Point2Dd t = Through(n.transform, mp.x, mp.y);
                            return PtD(t.x, t.y);
                        }
                        return Pt(mp);
                    };
                    for (const auto& cmd : n.commands) {
                        switch (cmd.verb) {
                            case UltraCanvas::XARPathVerb::MoveTo:
                                if (!cmd.points.empty()) { const Point2Dd p = map(cmd.points[0]); path->MoveTo(static_cast<float>(p.x), static_cast<float>(p.y)); }
                                break;
                            case UltraCanvas::XARPathVerb::LineTo:
                                if (!cmd.points.empty()) { const Point2Dd p = map(cmd.points[0]); path->LineTo(static_cast<float>(p.x), static_cast<float>(p.y)); }
                                break;
                            case UltraCanvas::XARPathVerb::BezierTo:
                                if (cmd.points.size() >= 3) {
                                    const Point2Dd a = map(cmd.points[0]), b = map(cmd.points[1]), c = map(cmd.points[2]);
                                    path->CurveTo(static_cast<float>(a.x), static_cast<float>(a.y),
                                                  static_cast<float>(b.x), static_cast<float>(b.y),
                                                  static_cast<float>(c.x), static_cast<float>(c.y));
                                }
                                break;
                            case UltraCanvas::XARPathVerb::ClosePath:
                                path->ClosePath();
                                break;
                        }
                    }
                    if (path->Path.commands.empty()) return nullptr;
                    ApplyAttributes(n, path->Style, n.isFilled, n.isStroked);
                    return path;
                }

                static bool Aligned(const Point2Di& major, const Point2Di& minor) {
                    return major.y == 0 && minor.x == 0;
                }

                // Unit-square / unit-circle geometry placed by two axis vectors
                // from a centre (millipoints), optionally through a matrix.
                std::shared_ptr<VectorPath> PlacedPath(const std::vector<PathOps::FlatSeg>& unit,
                                                       const Point2Di& centre, const Point2Di& major,
                                                       const Point2Di& minor, const UltraCanvas::XARMatrix* extra) {
                    auto path = std::make_shared<VectorPath>();
                    std::vector<PathOps::FlatSeg> segs = unit;
                    for (auto& seg : segs)
                        for (auto& p : seg.p) {
                            double x = centre.x + major.x * p.x + minor.x * p.y;
                            double y = centre.y + major.y * p.x + minor.y * p.y;
                            if (extra) { const Point2Dd t = Through(*extra, x, y); x = t.x; y = t.y; }
                            p = PtD(x, y);
                        }
                    path->Path = PathOps::SegsToPathData(segs);
                    return path;
                }

                std::shared_ptr<VectorElement> MakeRect(const UltraCanvas::XARRectangleNode& n) {
                    const bool identity = n.transform.IsIdentity();
                    if (n.isSimple && Aligned(n.majorAxis, n.minorAxis)) {
                        auto r = std::make_shared<VectorRect>();
                        const double hw = std::fabs(Len(n.majorAxis.x)), hh = std::fabs(Len(n.minorAxis.y));
                        const Point2Dd c = Pt(n.centre);
                        r->Bounds = Rect2Dd(c.x - hw, c.y - hh, 2 * hw, 2 * hh);
                        if (n.isRounded && n.cornerRadius > 0) r->RadiusX = r->RadiusY = static_cast<float>(Len(n.cornerRadius));
                        ApplyAttributes(n, r->Style, true, true);
                        return r;
                    }
                    // Anything else becomes a path: the unit square through the
                    // axes (simple) or the half sizes and matrix (complex).
                    const double rx = n.isRounded ? Len(n.cornerRadius) : 0.0;
                    std::vector<PathOps::FlatSeg> unit;
                    if (rx > 0) {
                        const double hw = std::fabs(n.isSimple ? std::hypot(n.majorAxis.x, n.majorAxis.y) : n.halfWidth) / 1000.0;
                        const double hh = std::fabs(n.isSimple ? std::hypot(n.minorAxis.x, n.minorAxis.y) : n.halfHeight) / 1000.0;
                        // Rounded corners in the unit square: the radius as a fraction of the half sizes.
                        unit = PathOps::RoundedRectSegs(Rect2Dd(-1, -1, 2, 2), hw > 0 ? rx / hw : 0, hh > 0 ? rx / hh : 0);
                    } else {
                        unit = PathOps::RectSegs(Rect2Dd(-1, -1, 2, 2));
                    }
                    auto path = PlacedPath(unit, n.centre, n.majorAxis, n.minorAxis, identity ? nullptr : &n.transform);
                    ApplyAttributes(n, path->Style, true, true);
                    return path;
                }

                std::shared_ptr<VectorElement> MakeEllipse(const UltraCanvas::XAREllipseNode& n) {
                    if (n.isSimple && Aligned(n.majorAxis, n.minorAxis)) {
                        const double rx = std::fabs(Len(n.majorAxis.x)), ry = std::fabs(Len(n.minorAxis.y));
                        const Point2Dd c = Pt(n.centre);
                        if (std::fabs(rx - ry) < 1e-6) {
                            auto circle = std::make_shared<VectorCircle>();
                            circle->Center = c;
                            circle->Radius = static_cast<float>(rx);
                            ApplyAttributes(n, circle->Style, true, true);
                            return circle;
                        }
                        auto e = std::make_shared<VectorEllipse>();
                        e->Center = c;
                        e->RadiusX = static_cast<float>(rx);
                        e->RadiusY = static_cast<float>(ry);
                        ApplyAttributes(n, e->Style, true, true);
                        return e;
                    }
                    auto path = PlacedPath(PathOps::EllipseSegs(Point2Dd(0, 0), 1, 1), n.centre, n.majorAxis, n.minorAxis,
                                           n.transform.IsIdentity() ? nullptr : &n.transform);
                    ApplyAttributes(n, path->Style, true, true);
                    return path;
                }

                std::shared_ptr<VectorElement> MakePolygon(const UltraCanvas::XARPolygonNode& n) {
                    // GeneratePolygonPoints applies the node transform and gives
                    // Y-up points; flip against the page.
                    auto pts = n.GeneratePolygonPoints(1.0f);
                    if (pts.size() < 3) return nullptr;
                    auto poly = std::make_shared<VectorPolygon>();
                    for (const auto& p : pts) poly->Points.emplace_back(p.x, pageH - p.y);
                    if (n.isRounded && n.curvature != 0.0f) Skip("rounded quick shape corners");
                    ApplyAttributes(n, poly->Style, true, true);
                    return poly;
                }

                std::shared_ptr<VectorElement> MakeText(const UltraCanvas::XARTextStoryNode& n) {
                    using UltraCanvas::XARNodeType;
                    auto text = std::make_shared<VectorText>();
                    Point2Dd origin = Pt(n.position);
                    if (n.hasTransform) {
                        const Point2Dd t = Through(n.transform, n.position.x, n.position.y);
                        origin = PtD(t.x, t.y);
                        if (std::fabs(n.transform.b) > 1e-9 || std::fabs(n.transform.c) > 1e-9) Skip("rotated text (set upright)");
                    }
                    text->Position = origin;
                    const UltraCanvas::XARTextStringNode* firstString = nullptr;
                    int lineIndex = 0;
                    for (const auto& lc : n.children) {
                        if (!lc || lc->type != XARNodeType::TextLine) continue;
                        const auto& line = static_cast<const UltraCanvas::XARTextLineNode&>(*lc);
                        bool firstInLine = true;
                        for (const auto& sc : line.children) {
                            if (!sc || sc->type != XARNodeType::TextString) continue;
                            const auto& str = static_cast<const UltraCanvas::XARTextStringNode&>(*sc);
                            if (str.text.empty()) continue;
                            if (!firstString) firstString = &str;
                            TextSpanData span;
                            span.Text = str.text;
                            span.Style.FontFamily = str.textAttr.fontName;
                            span.Style.FontSize = static_cast<float>(Len(str.textAttr.fontSize));
                            if (span.Style.FontSize <= 0) span.Style.FontSize = 12.0f;
                            span.Style.Weight = str.textAttr.bold ? FontWeight::Bold : FontWeight::Normal;
                            span.Style.Slant = str.textAttr.italic ? FontSlant::Italic : FontSlant::Normal;
                            span.Style.Underline = str.textAttr.underline;
                            if (firstInLine) {
                                double y;
                                if (line.hasYOffset) y = pageH - (n.position.y + line.yOffsetMP) / 1000.0;
                                else y = origin.y + lineIndex * span.Style.FontSize * 1.15;
                                span.Position = Point2Dd(origin.x + Len(line.leftIndentMP), y);
                                firstInLine = false;
                            }
                            text->Spans.push_back(span);
                        }
                        ++lineIndex;
                    }
                    if (text->Spans.empty()) return nullptr;
                    if (firstString) {
                        text->BaseStyle = text->Spans.front().Style;
                        switch (firstString->textAttr.justification) {
                            case UltraCanvas::XARTextAttribute::Justification::Centre: text->BaseStyle.Anchor = TextAnchor::Middle; break;
                            case UltraCanvas::XARTextAttribute::Justification::Right: text->BaseStyle.Anchor = TextAnchor::End; break;
                            default: text->BaseStyle.Anchor = TextAnchor::Start; break;
                        }
                        text->Style.Fill = firstString->hasFill ? firstString->fill.startColor : Color(0, 0, 0, 255);
                    }
                    if (n.hasTransparency) ApplyTransparency(n.transparency, text->Style);
                    return text;
                }

                std::shared_ptr<VectorElement> MakeImage(const UltraCanvas::XARBitmapNode& n) {
                    auto img = std::make_shared<VectorImage>();
                    const Point2Dd bl = Pt(n.bottomLeft), br = Pt(n.bottomRight), tl = Pt(n.topLeft);
                    const double w = std::hypot(br.x - bl.x, br.y - bl.y);
                    const double h = std::hypot(tl.x - bl.x, tl.y - bl.y);
                    if (std::fabs(br.y - bl.y) > 1e-6 || std::fabs(tl.x - bl.x) > 1e-6) Skip("rotated or sheared bitmap (placed upright)");
                    img->Bounds = Rect2Dd(std::min(bl.x, tl.x), std::min(bl.y, tl.y), w, h);
                    if (auto* def = xd.GetBitmap(n.bitmapRef)) {
                        img->EmbeddedData = def->data;
                        switch (def->format) {
                            case UltraCanvas::XARBitmapDefinition::Format::JPEG:
                            case UltraCanvas::XARBitmapDefinition::Format::JPEG8BPP: img->MimeType = "image/jpeg"; break;
                            case UltraCanvas::XARBitmapDefinition::Format::BMP: img->MimeType = "image/bmp"; break;
                            case UltraCanvas::XARBitmapDefinition::Format::GIF: img->MimeType = "image/gif"; break;
                            default: img->MimeType = "image/png"; break;
                        }
                    }
                    if (n.isContoned) Skip("contone bitmap tint");
                    if (n.hasTransparency) ApplyTransparency(n.transparency, img->Style);
                    return img;
                }

                // ----- attributes -----
                void ApplyAttributes(const UltraCanvas::XARNode& n, VectorStyle& st, bool allowFill, bool allowStroke) {
                    if (allowFill && n.hasFill) st.Fill = FillFrom(n.fill);
                    else st.Fill.reset();
                    if (allowStroke && n.hasLine && n.line.hasColor) st.Stroke = StrokeFrom(n.line);
                    else st.Stroke.reset();
                    if (n.hasTransparency) ApplyTransparency(n.transparency, st);
                }

                std::vector<GradientStop> StopsFrom(const UltraCanvas::XARFillAttribute& f) {
                    std::vector<GradientStop> stops;
                    if (f.stops.size() >= 2) {
                        for (const auto& s : f.stops) stops.emplace_back(s.position, s.color);
                    } else {
                        stops.emplace_back(0.0, f.startColor);
                        stops.emplace_back(1.0, f.endColor);
                    }
                    return stops;
                }

                FillData FillFrom(const UltraCanvas::XARFillAttribute& f) {
                    using UltraCanvas::XARFillType;
                    switch (f.type) {
                        case XARFillType::NoneFill:
                            return std::monostate{};
                        case XARFillType::Flat:
                            return f.startColor;
                        case XARFillType::LinearGradient: {
                            LinearGradientData g;
                            g.Units = GradientUnits::UserSpaceOnUse;
                            g.Start = Pt(f.startPoint);
                            g.End = Pt(f.endPoint);
                            g.Stops = StopsFrom(f);
                            return GradientData{g};
                        }
                        case XARFillType::CircularGradient:
                        case XARFillType::EllipticalGradient: {
                            RadialGradientData g;
                            g.Units = GradientUnits::UserSpaceOnUse;
                            g.Center = g.FocalPoint = Pt(f.startPoint);
                            g.Radius = static_cast<float>(std::hypot(Len(f.endPoint.x - f.startPoint.x), Len(f.endPoint.y - f.startPoint.y)));
                            g.Stops = StopsFrom(f);
                            if (f.type == XARFillType::EllipticalGradient) Skip("elliptical fill (read as circular)");
                            return GradientData{g};
                        }
                        case XARFillType::ConicalGradient: {
                            ConicalGradientData g;
                            g.Units = GradientUnits::UserSpaceOnUse;
                            g.Center = Pt(f.startPoint);
                            const double a = std::atan2(-static_cast<double>(f.endPoint.y - f.startPoint.y),
                                                        static_cast<double>(f.endPoint.x - f.startPoint.x));
                            g.StartAngle = static_cast<float>(a * 180.0 / M_PI);
                            g.EndAngle = g.StartAngle + 360.0f;
                            g.Stops = StopsFrom(f);
                            return GradientData{g};
                        }
                        case XARFillType::Bitmap:
                        case XARFillType::ContoneBitmap:
                            Skip("bitmap fill (read as flat grey)");
                            return Color(160, 160, 160, 255);
                        default:
                            Skip("fractal / noise / diamond / multi-colour fill (read as its first colour)");
                            return f.startColor;
                    }
                }

                std::optional<StrokeData> StrokeFrom(const UltraCanvas::XARLineAttribute& l) {
                    StrokeData st;
                    st.Fill = l.color;
                    st.Width = static_cast<float>(Len(l.width));
                    if (st.Width <= 0) st.Width = 0.25f;   // Xara's hairline
                    st.LineCap = l.cap == LineCap::Round ? StrokeLineCap::Round
                               : l.cap == LineCap::Square ? StrokeLineCap::Square : StrokeLineCap::Butt;
                    st.LineJoin = l.join == LineJoin::Round ? StrokeLineJoin::Round
                                : l.join == LineJoin::Bevel ? StrokeLineJoin::Bevel : StrokeLineJoin::Miter;
                    st.MiterLimit = l.mitreLimit;
                    st.DashArray = l.dashPattern;
                    if (l.lineTransparency > 0) st.Opacity = 1.0f - l.lineTransparency / 255.0f;
                    if (l.startArrowRef != 0) st.StartArrow.Kind = KindFromArrowRef(l.startArrowRef);
                    if (l.endArrowRef != 0) st.EndArrow.Kind = KindFromArrowRef(l.endArrowRef);
                    if (l.startArrowRef > 0 || l.endArrowRef > 0) Skip("custom arrowhead definition (read as the triangle)");
                    return st;
                }

                static TransparencyMix MixFrom(UltraCanvas::XARTransparencyMix m) {
                    using UltraCanvas::XARTransparencyMix;
                    switch (m) {
                        case XARTransparencyMix::Stained: return TransparencyMix::StainedGlass;
                        case XARTransparencyMix::Bleach: return TransparencyMix::Bleach;
                        case XARTransparencyMix::Contrast: return TransparencyMix::Contrast;
                        case XARTransparencyMix::Saturation: return TransparencyMix::Saturation;
                        case XARTransparencyMix::Darken: return TransparencyMix::Darken;
                        case XARTransparencyMix::Lighten: return TransparencyMix::Lighten;
                        case XARTransparencyMix::Brightness: return TransparencyMix::Brightness;
                        case XARTransparencyMix::Luminosity: return TransparencyMix::Luminosity;
                        case XARTransparencyMix::Hue: return TransparencyMix::Hue;
                        default: return TransparencyMix::Mix;
                    }
                }

                void ApplyTransparency(const UltraCanvas::XARTransparencyAttribute& t, VectorStyle& st) {
                    using UltraCanvas::XARTransparencyType;
                    if (t.type == XARTransparencyType::NoTrans) return;
                    const TransparencyMix mix = MixFrom(t.mix);
                    TransparencyData d;
                    d.Mix = mix;
                    switch (t.type) {
                        case XARTransparencyType::Flat:
                            if (mix == TransparencyMix::Mix) {
                                st.Opacity = 1.0f - t.startTransparency / 255.0f;
                                return;
                            }
                            d.Shape = TransparencyShape::Flat;
                            d.Level = t.startTransparency / 255.0f;
                            break;
                        case XARTransparencyType::LinearGradient:
                        case XARTransparencyType::CircularGradient:
                        case XARTransparencyType::EllipticalGradient:
                        case XARTransparencyType::ConicalGradient:
                            d.Shape = t.type == XARTransparencyType::LinearGradient ? TransparencyShape::Linear
                                    : t.type == XARTransparencyType::ConicalGradient ? TransparencyShape::Conical
                                    : TransparencyShape::Radial;
                            d.Start = Pt(t.startPoint);
                            d.End = Pt(t.endPoint);
                            if (t.stops.size() >= 2) {
                                for (const auto& s : t.stops) d.Stops.push_back({s.position, s.level / 255.0f});
                            } else {
                                d.Stops.push_back({0.0, t.startTransparency / 255.0f});
                                d.Stops.push_back({1.0, t.endTransparency / 255.0f});
                            }
                            if (t.type == XARTransparencyType::EllipticalGradient) Skip("elliptical transparency (read as circular)");
                            break;
                        default:
                            Skip("bitmap / fractal / multi-colour transparency (read as flat)");
                            d.Shape = TransparencyShape::Flat;
                            d.Level = t.startTransparency / 255.0f;
                            break;
                    }
                    st.Transparency = d;
                }
            };
        }   // anonymous namespace
#endif   // ULTRACANVAS_HAS_XAR_PLUGIN

        std::shared_ptr<VectorDocument> XARConverter::Impl::ImportFromFile(
                const std::string& filename,
                const ConversionOptions& options,
                const XARConversionOptions& xarOptions) {
            currentOptions = options;
            currentXarOptions = xarOptions;
#ifdef ULTRACANVAS_HAS_XAR_PLUGIN
            UltraCanvas::XARDocument xd;
            if (!xd.LoadFromFile(filename)) {
                LogWarning("Failed to read XAR file: " + filename);
                for (const auto& w : xd.GetDiagnostics().warnings) LogWarning("XAR: " + w);
                return nullptr;
            }
            for (const auto& w : xd.GetDiagnostics().warnings) LogWarning("XAR: " + w);
            XarTranslator translator(xd, [this](const std::string& msg) { LogWarning(msg); });
            auto document = translator.Run();
            ReportProgress(1.0f);
            return document;
#else
            LogWarning("XAR import needs the XAR plugin (ULTRACANVAS_PLUGIN_XAR); " + filename + " not read");
            return nullptr;
#endif
        }

        std::shared_ptr<VectorDocument> XARConverter::Impl::ImportFromMemory(
                const uint8_t* data, size_t size,
                const ConversionOptions& options,
                const XARConversionOptions& xarOptions) {
            currentOptions = options;
            currentXarOptions = xarOptions;
#ifdef ULTRACANVAS_HAS_XAR_PLUGIN
            UltraCanvas::XARDocument xd;
            if (!xd.LoadFromMemory(data, size)) {
                LogWarning("Failed to parse XAR data");
                for (const auto& w : xd.GetDiagnostics().warnings) LogWarning("XAR: " + w);
                return nullptr;
            }
            for (const auto& w : xd.GetDiagnostics().warnings) LogWarning("XAR: " + w);
            XarTranslator translator(xd, [this](const std::string& msg) { LogWarning(msg); });
            auto document = translator.Run();
            ReportProgress(1.0f);
            return document;
#else
            (void)data; (void)size;
            LogWarning("XAR import needs the XAR plugin (ULTRACANVAS_PLUGIN_XAR); data not read");
            return nullptr;
#endif
        }

// ===== UTILITY FUNCTIONS =====

        void XARConverter::Impl::LogWarning(const std::string& message) {
            if (currentOptions.WarningCallback) {
                currentOptions.WarningCallback(message);
            }
            if (currentXarOptions.WarningCallback) {
                currentXarOptions.WarningCallback(message);
            }
        }

        void XARConverter::Impl::ReportProgress(float progress) {
            if (currentOptions.ProgressCallback) {
                currentOptions.ProgressCallback(progress);
            }
            if (currentXarOptions.ProgressCallback) {
                currentXarOptions.ProgressCallback(progress);
            }
        }

// ===== EXPORT IMPLEMENTATION =====
//
// The emitter below writes the uncompressed XAR record grammar exactly as the
// spec-verified reader in Plugins/Vector/XAR/UltraCanvasXARPlugin.cpp consumes
// it: 8-byte signature, then records of (TAG:UINT32, size:UINT32, body), with
// the tree encoded as "object record, TAG_DOWN, child records, TAG_UP".
// Attribute records are emitted as children of the object they style, and
// colour/font definition records are referenced by their 1-based record
// sequence number (every record counts, TAG_UP/TAG_DOWN included).
//
// NOTE: the legacy XARTags namespace in UltraCanvasXARConverter.h predates the
// spec-verified reader and carries wrong tag numbers (e.g. TAG_ENDOFFILE=4,
// TAG_DEFINERGBCOLOUR=1000). The writer therefore defines its own constants,
// taken from the Xar Format Specification Appendix A and matching the XARTag
// enum in UltraCanvasXARPlugin.h.

        namespace {
            // Little-endian record body builder.
            struct XarBody {
                std::vector<uint8_t> bytes;

                void U8(uint8_t v) { bytes.push_back(v); }
                void U32(uint32_t v) {
                    bytes.push_back(static_cast<uint8_t>(v));
                    bytes.push_back(static_cast<uint8_t>(v >> 8));
                    bytes.push_back(static_cast<uint8_t>(v >> 16));
                    bytes.push_back(static_cast<uint8_t>(v >> 24));
                }
                void I32(int32_t v) { U32(static_cast<uint32_t>(v)); }
                void F64(double v) {
                    uint64_t bits = 0;
                    std::memcpy(&bits, &v, sizeof(bits));
                    for (int i = 0; i < 8; ++i) bytes.push_back(static_cast<uint8_t>(bits >> (8 * i)));
                }
                void Ascii(const std::string& s) {
                    bytes.insert(bytes.end(), s.begin(), s.end());
                    bytes.push_back(0);
                }
                // UTF-8 in, UTF-16LE + 0x0000 terminator out.
                void Utf16(const std::string& utf8) {
                    size_t i = 0, n = utf8.size();
                    while (i < n) {
                        uint32_t cp = static_cast<uint8_t>(utf8[i]);
                        size_t extra = 0;
                        if (cp >= 0xF0) { cp &= 0x07; extra = 3; }
                        else if (cp >= 0xE0) { cp &= 0x0F; extra = 2; }
                        else if (cp >= 0xC0) { cp &= 0x1F; extra = 1; }
                        if (i + extra >= n && extra > 0) break;
                        for (size_t k = 0; k < extra; ++k) {
                            cp = (cp << 6) | (static_cast<uint8_t>(utf8[i + 1 + k]) & 0x3F);
                        }
                        i += 1 + extra;
                        if (cp >= 0x10000) {
                            cp -= 0x10000;
                            uint16_t hi = static_cast<uint16_t>(0xD800 | (cp >> 10));
                            uint16_t lo = static_cast<uint16_t>(0xDC00 | (cp & 0x3FF));
                            bytes.push_back(static_cast<uint8_t>(hi));
                            bytes.push_back(static_cast<uint8_t>(hi >> 8));
                            bytes.push_back(static_cast<uint8_t>(lo));
                            bytes.push_back(static_cast<uint8_t>(lo >> 8));
                        } else {
                            bytes.push_back(static_cast<uint8_t>(cp));
                            bytes.push_back(static_cast<uint8_t>(cp >> 8));
                        }
                    }
                    bytes.push_back(0);
                    bytes.push_back(0);
                }
            };

            // Paths are normalised to absolute move/line/cubic segments by the
            // shared PathOps helpers (UltraCanvasVectorPathOps.h).
            using namespace PathOps;
            using XarPathSeg = PathOps::FlatSeg;

            class XarEmitter {
            public:
                XarEmitter(const VectorDocument& document,
                           std::function<void(const std::string&)> warnFn,
                           bool preserveEffects = true)
                        : doc(document), warn(std::move(warnFn)), effectsOn(preserveEffects) {}

                std::vector<uint8_t> Build() {
                    pageW = doc.Size.width;
                    pageH = doc.Size.height;
                    if (pageW <= 0 || pageH <= 0) {
                        Rect2Dd bbox = doc.GetBoundingBox();
                        pageW = bbox.x + bbox.width;
                        pageH = bbox.y + bbox.height;
                        if (pageW <= 0) pageW = 595;   // A4 fallback
                        if (pageH <= 0) pageH = 842;
                    }

                    const uint8_t signature[8] = {0x58, 0x41, 0x52, 0x41, 0xA3, 0xA3, 0x0D, 0x0A};
                    out.assign(signature, signature + 8);

                    XarBody fh;
                    fh.bytes.push_back('C'); fh.bytes.push_back('X'); fh.bytes.push_back('N');
                    fh.U32(0);   // file size, patched below
                    fh.U32(0);   // web link
                    fh.U32(0);   // refinement flags
                    fh.Ascii("UltraCanvas");
                    fh.Ascii("1.0");
                    fh.Ascii("");
                    Rec(XarOut::FileHeader, fh);

                    Rec(XarOut::Document);
                    Down();
                    Rec(XarOut::Chapter);
                    Down();
                    Rec(XarOut::Spread);
                    Down();

                    XarBody si;
                    si.I32(Mp(pageW));
                    si.I32(Mp(pageH));
                    si.I32(0);   // margin
                    si.I32(0);   // bleed
                    si.U8(0);    // flags
                    Rec(XarOut::SpreadInformation, si);

                    for (const auto& layer : doc.Layers) {
                        if (layer) EmitLayer(*layer);
                    }

                    Up();   // spread
                    Up();   // chapter
                    Up();   // document
                    Rec(XarOut::EndOfFile);

                    // Patch the file-size hint inside the FILEHEADER body:
                    // 8 signature + 4 tag + 4 size + 3 "CXN" = offset 19.
                    uint32_t total = static_cast<uint32_t>(out.size());
                    out[19] = static_cast<uint8_t>(total);
                    out[20] = static_cast<uint8_t>(total >> 8);
                    out[21] = static_cast<uint8_t>(total >> 16);
                    out[22] = static_cast<uint8_t>(total >> 24);
                    return out;
                }

            private:
                const VectorDocument& doc;
                std::function<void(const std::string&)> warn;
                std::vector<uint8_t> out;
                uint32_t seq = 0;
                double pageW = 0, pageH = 0;                 // points
                std::map<uint32_t, uint32_t> colourRefs;     // 0xRRGGBB -> record seq
                std::map<std::string, uint32_t> fontRefs;    // family -> record seq
                std::map<std::string, uint32_t> dashRefs;    // pattern -> record seq
                bool effectsOn = true;                       // XARConversionOptions::PreserveEffects
                const VectorEffects* activeEffects = nullptr; // the element whose attributes are being written
                Rect2Dd attrBounds;                          // its untransformed bounds, for bbox gradients
                bool galleryWarned = false;

                // ===== RECORD PRIMITIVES =====

                uint32_t Rec(uint32_t tag, const XarBody& body = XarBody()) {
                    ++seq;
                    XarBody hdr;
                    hdr.U32(tag);
                    hdr.U32(static_cast<uint32_t>(body.bytes.size()));
                    out.insert(out.end(), hdr.bytes.begin(), hdr.bytes.end());
                    out.insert(out.end(), body.bytes.begin(), body.bytes.end());
                    return seq;
                }
                void Down() { Rec(XarOut::Down); }
                void Up() { Rec(XarOut::Up); }

                // ===== COORDINATES =====
                // Document space is points, Y down; XAR is millipoints, Y up.

                static int32_t Mp(double pt) {
                    return static_cast<int32_t>(std::lround(pt * 1000.0));
                }
                void Coord(XarBody& b, const Point2Dd& pPt) const {
                    b.I32(Mp(pPt.x));
                    b.I32(Mp(pageH - pPt.y));
                }
                static void Vec(XarBody& b, double dxPt, double dyPt) {
                    b.I32(Mp(dxPt));
                    b.I32(Mp(-dyPt));
                }
                static bool AxisAligned(const Matrix3x3& m) {
                    return std::fabs(m.m[0][1]) < 1e-6 && std::fabs(m.m[1][0]) < 1e-6;
                }
                static double AvgScale(const Matrix3x3& m) {
                    double det = std::fabs(static_cast<double>(m.m[0][0]) * m.m[1][1] -
                                           static_cast<double>(m.m[0][1]) * m.m[1][0]);
                    return det > 0 ? std::sqrt(det) : 1.0;
                }

                // ===== REFERENCED DEFINITIONS =====
                // Definitions are emitted lazily, immediately before the first
                // record that references them; the reader keys both colours and
                // fonts by record sequence number, position-independent.

                int32_t ColourRef(const Color& c) {
                    uint32_t key = (static_cast<uint32_t>(c.r) << 16) |
                                   (static_cast<uint32_t>(c.g) << 8) | c.b;
                    auto it = colourRefs.find(key);
                    if (it != colourRefs.end()) return static_cast<int32_t>(it->second);
                    XarBody b;
                    b.U8(c.r); b.U8(c.g); b.U8(c.b);
                    uint32_t ref = Rec(XarOut::DefineRGBColour, b);
                    colourRefs[key] = ref;
                    return static_cast<int32_t>(ref);
                }

                int32_t FontRef(const std::string& family) {
                    auto it = fontRefs.find(family);
                    if (it != fontRefs.end()) return static_cast<int32_t>(it->second);
                    XarBody b;
                    b.Utf16(family);
                    b.Utf16(family);
                    for (int i = 0; i < 10; ++i) b.U8(0);   // panose
                    uint32_t ref = Rec(XarOut::FontDefTrueType, b);
                    fontRefs[family] = ref;
                    return static_cast<int32_t>(ref);
                }

                int32_t DashRef(const std::vector<double>& dash, double scale) {
                    std::ostringstream key;
                    for (double d : dash) key << d * scale << ",";
                    auto it = dashRefs.find(key.str());
                    if (it != dashRefs.end()) return static_cast<int32_t>(it->second);
                    XarBody b;
                    b.I32(static_cast<int32_t>(dash.size()));
                    for (double d : dash) b.I32(Mp(d * scale));
                    uint32_t ref = Rec(XarOut::DefineDash, b);
                    dashRefs[key.str()] = ref;
                    return static_cast<int32_t>(ref);
                }

                // ===== TREE =====

                void EmitLayer(const VectorLayer& layer) {
                    Rec(XarOut::Layer);
                    Down();
                    XarBody ld;
                    uint8_t flags = 0;
                    if (layer.Visible) flags |= 0x1;
                    if (layer.Locked) flags |= 0x2;
                    flags |= 0x4;   // printable
                    ld.U8(flags);
                    ld.Utf16(layer.Name.empty() ? std::string("Layer 1") : layer.Name);
                    Rec(XarOut::LayerDetails, ld);

                    for (const auto& child : layer.Children) {
                        if (child) EmitElement(*child, layer.Style, Matrix3x3::Identity());
                    }
                    Up();
                }

                void EmitElement(const VectorElement& e, const VectorStyle& inherited,
                                 const Matrix3x3& parentCtm) {
                    if (!e.Style.Visible || !e.Style.Display) return;

                    VectorStyle eff = e.Style;
                    eff.Inherit(inherited);
                    Matrix3x3 ctm = e.Transform ? parentCtm * (*e.Transform) : parentCtm;

                    // A shadow is a controller group around the object.
                    const bool shadow = effectsOn && e.Effects.Shadow.has_value() && e.Effects.Shadow->Darkness > 0;
                    if (shadow) { EmitShadowController(*e.Effects.Shadow, ctm); Down(); }
                    // The line gallery: Xara's own arrowheads are line
                    // attributes; a width profile, a brush or another arrowhead
                    // kind is baked into shapes after the object, and the
                    // whole thing is a group carrying a user value the reader
                    // rebuilds the stroke from. A profile or brush replaces the
                    // plain stroke.
                    const StrokeData* st = HasVisibleStroke(eff) ? &*eff.Stroke : nullptr;
                    const bool bakedArrows = st && ((st->StartArrow.IsSet() && NativeArrowRef(st->StartArrow.Kind) == 0) ||
                                                    (st->EndArrow.IsSet() && NativeArrowRef(st->EndArrow.Kind) == 0));
                    const bool baked = st && (bakedArrows || st->HasWidthProfile() || (st->HasBrush() && st->Brush->Stamp));
                    VectorStyle drawStyle = eff;
                    if (st && (st->HasWidthProfile() || st->HasBrush())) drawStyle.Stroke.reset();
                    std::vector<Matrix3x3> stampPlacements;
                    std::vector<std::vector<Point2Dd>> stampLines;
                    if (baked) {
                        if (st->HasBrush()) {
                            PathData outline;
                            if (BuildOutlinePath(e, outline))
                                for (const auto& sub : FlattenPathData(outline)) {
                                    std::vector<Matrix3x3> placements;
                                    if (StampPlacements(sub.Points, *st, placements)) {
                                        stampLines.push_back(sub.Points);
                                        stampPlacements.insert(stampPlacements.end(), placements.begin(), placements.end());
                                    }
                                }
                        }
                        Rec(XarOut::Group);
                        Down();
                        std::optional<Matrix3x3> firstStamp;
                        if (!stampPlacements.empty()) firstStamp = ctm * stampPlacements.front();
                        EmitUserValue(kLineGalleryKey, LineGalleryMarker(*st, eff, ctm, firstStamp ? &*firstStamp : nullptr));
                    }
                    activeEffects = effectsOn ? &e.Effects : nullptr;
                    // A shape left with neither fill nor stroke (its stroke is
                    // baked below) has no record of its own - unless the baked
                    // group needs its outline back.
                    const bool container = e.Type == VectorElementType::Group || e.Type == VectorElementType::Symbol ||
                                           e.Type == VectorElementType::Layer || e.Type == VectorElementType::Text ||
                                           e.Type == VectorElementType::Image;
                    const bool invisible = !container && !baked && !HasVisibleFill(drawStyle) && !HasVisibleStroke(drawStyle);

                    switch (invisible ? VectorElementType::NoneType : e.Type) {
                        case VectorElementType::NoneType:
                            break;
                        case VectorElementType::Group:
                        case VectorElementType::Symbol: {
                            const auto& g = static_cast<const VectorGroup&>(e);
                            Rec(XarOut::Group);
                            Down();
                            for (const auto& child : g.Children) {
                                if (child) EmitElement(*child, eff, ctm);
                            }
                            Up();
                            break;
                        }
                        case VectorElementType::Layer: {
                            // Nested layers degrade to groups.
                            const auto& g = static_cast<const VectorGroup&>(e);
                            Rec(XarOut::Group);
                            Down();
                            for (const auto& child : g.Children) {
                                if (child) EmitElement(*child, eff, ctm);
                            }
                            Up();
                            break;
                        }
                        case VectorElementType::Rectangle:
                        case VectorElementType::RoundedRectangle:
                            EmitRect(static_cast<const VectorRect&>(e), drawStyle, ctm);
                            break;
                        case VectorElementType::Circle: {
                            const auto& c = static_cast<const VectorCircle&>(e);
                            EmitEllipseShape(c.Center, c.Radius, c.Radius, drawStyle, ctm);
                            break;
                        }
                        case VectorElementType::Ellipse: {
                            const auto& el = static_cast<const VectorEllipse&>(e);
                            EmitEllipseShape(el.Center, el.RadiusX, el.RadiusY, drawStyle, ctm);
                            break;
                        }
                        case VectorElementType::Line: {
                            const auto& ln = static_cast<const VectorLine&>(e);
                            std::vector<XarPathSeg> segs;
                            segs.push_back({XarPathSeg::Move, {ln.Start}, false});
                            segs.push_back({XarPathSeg::Line, {ln.End}, false});
                            EmitPathRecord(segs, drawStyle, ctm, false);
                            break;
                        }
                        case VectorElementType::Polyline:
                            EmitPolySegs(static_cast<const VectorPolyline&>(e).Points, false, drawStyle, ctm);
                            break;
                        case VectorElementType::Polygon:
                            EmitPolySegs(static_cast<const VectorPolygon&>(e).Points, true, drawStyle, ctm);
                            break;
                        case VectorElementType::Path: {
                            const auto& p = static_cast<const VectorPath&>(e);
                            auto segs = NormalizePath(p.Path);
                            EmitPathRecord(segs, drawStyle, ctm, true);
                            break;
                        }
                        case VectorElementType::Text:
                            EmitText(static_cast<const VectorText&>(e), drawStyle, ctm);
                            break;
                        default:
                            warn("XAR export: element type not supported, skipped (type " +
                                 std::to_string(static_cast<int>(e.Type)) + ")");
                            break;
                    }
                    activeEffects = nullptr;
                    if (baked) {
                        EmitBakedGallery(e, eff, ctm, bakedArrows);
                        if (!stampPlacements.empty()) {
                            Rec(XarOut::Group);
                            Down();
                            for (const auto& placement : stampPlacements) EmitElement(*st->Brush->Stamp, VectorStyle(), ctm * placement);
                            Up();
                        }
                        Up();
                    }
                    if (shadow) Up();
                }

                // ===== EFFECTS =====

                // TAG_SHADOWCONTROLLER, the group that holds a shadowed object.
                // Layout as the XAR plugin reads it (verified there against
                // Designer output): BYTE type (0 floor, 1 wall, 2 glow),
                // INT32 penumbra width, INT32 offset X, INT32 offset Y (all
                // millipoints, Y up), INT32 wall angle (microradians), INT32
                // darkness (percent), then two scale / height fields.
                void EmitShadowController(const ShadowEffect& sh, const Matrix3x3& ctm) {
                    XarBody b;
                    b.U8(sh.Kind == ShadowKind::Floor ? 0 : sh.Kind == ShadowKind::Glow ? 2 : 1);
                    const double k = AvgScale(ctm);
                    b.I32(Mp(sh.Blur * k));
                    b.I32(Mp(sh.Offset.x * k));
                    b.I32(Mp(-sh.Offset.y * k));
                    b.I32(0);
                    b.I32(static_cast<int32_t>(std::lround(std::max(0.0f, std::min(1.0f, sh.Darkness)) * 100.0f)));
                    b.I32(0);
                    b.I32(0);
                    Rec(XarOut::ShadowController, b);
                }

                // TAG_USERVALUE: STRING key, STRING value, an attribute of the
                // object being written; Xara keeps it.
                void EmitUserValue(const std::string& key, const std::string& value) {
                    XarBody b;
                    b.Utf16(key);
                    b.Utf16(value);
                    Rec(XarOut::UserValue, b);
                }

                static std::string Num(double v) {
                    char buf[48];
                    std::snprintf(buf, sizeof(buf), "%.6g", v);
                    return buf;
                }
                static std::string HexColour(const Color& c) {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "%02x%02x%02x", c.r, c.g, c.b);
                    return buf;
                }

                // What the line gallery bakes: everything the reader needs to
                // rebuild the stroke, in document points.
                std::string LineGalleryMarker(const StrokeData& st, const VectorStyle& eff, const Matrix3x3& ctm,
                                              const Matrix3x3* stampPlacement) {
                    const double k = AvgScale(ctm);
                    Color c(0, 0, 0, 255);
                    if (const Color* col = std::get_if<Color>(&st.Fill)) c = *col;
                    std::string m = "v=1;width=" + Num(st.Width * k) + ";colour=" + HexColour(c) + ";alpha=" + Num(c.a) +
                                    ";opacity=" + Num(st.Opacity * eff.StrokeOpacity) +
                                    ";cap=" + Num(static_cast<int>(st.LineCap)) + ";join=" + Num(static_cast<int>(st.LineJoin)) +
                                    ";mitre=" + Num(st.MiterLimit) +
                                    ";startArrow=" + Num(static_cast<int>(st.StartArrow.Kind)) + ";startScale=" + Num(st.StartArrow.Scale) +
                                    ";endArrow=" + Num(static_cast<int>(st.EndArrow.Kind)) + ";endScale=" + Num(st.EndArrow.Scale);
                    if (!st.DashArray.empty()) {
                        m += ";dash=";
                        for (size_t i = 0; i < st.DashArray.size(); ++i) m += (i ? "," : "") + Num(st.DashArray[i] * k);
                    }
                    if (st.HasWidthProfile()) {
                        m += ";profile=";
                        for (size_t i = 0; i < st.WidthProfile.size(); ++i)
                            m += (i ? "," : "") + Num(st.WidthProfile[i].T) + ":" + Num(st.WidthProfile[i].Factor);
                    }
                    if (st.HasBrush() && stampPlacement) {
                        const BrushData& b = *st.Brush;
                        m += ";brush=1;spacing=" + Num(b.Spacing) + ";scale=" + Num(b.Scale) + ";rotate=" + (b.Rotate ? "1" : "0");
                        m += ";stampm=";
                        const Matrix3x3& M = *stampPlacement;
                        // Row-major, the FromValues(a, b, c, d, e, f) order.
                        m += Num(M.m[0][0]) + "," + Num(M.m[0][1]) + "," + Num(M.m[1][0]) + "," + Num(M.m[1][1]) + "," +
                             Num(M.m[0][2]) + "," + Num(M.m[1][2]);
                    }
                    return m;
                }

                // The placement of stamp `i` along a polyline, as the renderer
                // stamps it: translate to the point, rotate to the tangent,
                // scale the stamp's height to the line width, centre it.
                static bool StampPlacements(const std::vector<Point2Dd>& pts, const StrokeData& st,
                                            std::vector<Matrix3x3>& out) {
                    const BrushData& b = *st.Brush;
                    const Rect2Dd sb = b.Stamp->GetBoundingBox();
                    if (sb.width <= 0 || sb.height <= 0 || pts.size() < 2) return false;
                    const double k = (std::max(0.5f, st.Width) * std::max(0.01f, b.Scale)) / sb.height;
                    const double step = std::max(0.25, sb.width * k * std::max(0.05f, b.Spacing));
                    std::vector<double> cum(pts.size(), 0.0);
                    for (size_t i = 1; i < pts.size(); ++i)
                        cum[i] = cum[i - 1] + std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
                    const double total = cum.back();
                    if (total <= 1e-9) return false;
                    size_t seg = 1;
                    int stamps = 0;
                    for (double dist = 0; dist <= total + 1e-9 && stamps < 4000; dist += step, ++stamps) {
                        while (seg + 1 < pts.size() && cum[seg] < dist) ++seg;
                        const double segLen = cum[seg] - cum[seg - 1];
                        const double u = segLen > 1e-12 ? std::min(1.0, std::max(0.0, (dist - cum[seg - 1]) / segLen)) : 0.0;
                        const Point2Dd p(pts[seg - 1].x + (pts[seg].x - pts[seg - 1].x) * u,
                                         pts[seg - 1].y + (pts[seg].y - pts[seg - 1].y) * u);
                        const double angle = b.Rotate ? std::atan2(pts[seg].y - pts[seg - 1].y, pts[seg].x - pts[seg - 1].x) : 0.0;
                        out.push_back(Matrix3x3::Translate(p.x, p.y) * Matrix3x3::Rotate(angle) * Matrix3x3::Scale(k, k) *
                                      Matrix3x3::Translate(-(sb.x + sb.width / 2), -(sb.y + sb.height / 2)));
                    }
                    return !out.empty();
                }

                // TAG_FEATHER, an attribute of the object being written.
                void EmitFeather(const Matrix3x3& ctm) {
                    if (!activeEffects || !activeEffects->Feather.has_value() || activeEffects->Feather->Radius <= 0) return;
                    XarBody b;
                    b.I32(Mp(activeEffects->Feather->Radius * AvgScale(ctm)));
                    Rec(XarOut::Feather, b);
                }

                static uint8_t MixByte(TransparencyMix m) {
                    switch (m) {
                        case TransparencyMix::StainedGlass: return 2;
                        case TransparencyMix::Bleach: return 3;
                        case TransparencyMix::Contrast: return 4;
                        case TransparencyMix::Saturation: return 5;
                        case TransparencyMix::Darken: return 6;
                        case TransparencyMix::Lighten: return 7;
                        case TransparencyMix::Brightness: return 8;
                        case TransparencyMix::Luminosity: return 9;
                        case TransparencyMix::Hue: return 10;
                        case TransparencyMix::Mix:
                        default: return 1;
                    }
                }
                static uint8_t LevelByte(double level) {
                    return static_cast<uint8_t>(std::lround(std::max(0.0, std::min(1.0, level)) * 255.0));
                }

                // The transparency attribute: the style's ramp (with its mix),
                // the flat Opacity folded in, or the flat opacity alone.
                void EmitTransparency(const VectorStyle& style, const Matrix3x3& ctm, uint8_t fillAlpha) {
                    const double opacity = style.Opacity * style.FillOpacity * (fillAlpha / 255.0);
                    const TransparencyData* t = style.Transparency.has_value() ? &*style.Transparency : nullptr;
                    if (t && t->IsGradient()) {
                        if (t->Stops.size() > 2) warn("XAR export: only the first and last transparency levels are written");
                        auto level = [&](float l) { return LevelByte(1.0 - (1.0 - l) * opacity); };
                        XarBody b;
                        Coord(b, ctm.Transform(t->Start));
                        Coord(b, ctm.Transform(t->End));
                        b.U8(level(t->Stops.front().Level));
                        b.U8(level(t->Stops.back().Level));
                        b.U8(MixByte(t->Mix));
                        Rec(t->Shape == TransparencyShape::Linear ? XarOut::LinearTransparentFill
                          : t->Shape == TransparencyShape::Conical ? XarOut::ConicalTransparentFill
                          : XarOut::CircularTransparentFill, b);
                        return;
                    }
                    double level = 1.0 - opacity;
                    uint8_t mix = 1;
                    if (t) {
                        level = 1.0 - (1.0 - t->Level) * opacity;
                        mix = MixByte(t->Mix);
                    }
                    if (level > 0.001 || mix != 1) {
                        XarBody b;
                        b.U8(LevelByte(level));
                        b.U8(mix);
                        Rec(XarOut::FlatTransparentFill, b);
                    }
                }

                // Arrowheads and width profiles as filled shapes after the
                // object: Xara shows them, and they read back as shapes.
                void EmitBakedGallery(const VectorElement& e, const VectorStyle& eff, const Matrix3x3& ctm, bool bakedArrows) {
                    const StrokeData& st = *eff.Stroke;
                    if (!bakedArrows && !st.HasWidthProfile()) return;
                    PathData outline;
                    if (!BuildOutlinePath(e, outline)) return;
                    if (!galleryWarned) {
                        galleryWarned = true;
                        warn("XAR export: width profiles, brush stamps and non-Xara arrowheads are written as shapes "
                             "in a group Xara shows; the group's user value lets this converter read the stroke back");
                    }
                    VectorStyle fillStyle;
                    if (std::holds_alternative<Color>(st.Fill) || std::holds_alternative<GradientData>(st.Fill)) fillStyle.Fill = st.Fill;
                    else fillStyle.Fill = Color(0, 0, 0, 255);
                    fillStyle.Opacity = eff.Opacity;
                    fillStyle.FillOpacity = eff.StrokeOpacity * st.Opacity;
                    VectorStyle strokeStyle;
                    StrokeData plain = st;
                    plain.StartArrow = plain.EndArrow = ArrowheadData{};
                    plain.WidthProfile.clear();
                    plain.Brush.reset();
                    plain.DashArray.clear();
                    strokeStyle.Stroke = plain;
                    strokeStyle.Opacity = eff.Opacity;
                    strokeStyle.StrokeOpacity = eff.StrokeOpacity;

                    if (st.HasWidthProfile()) {
                        const PathData band = VariableWidthOutline(outline, st);
                        if (!band.commands.empty()) EmitPathRecord(NormalizePath(band), fillStyle, ctm, true);
                    }
                    if (bakedArrows) {
                        Point2Dd start, startDir, end, endDir;
                        if (PathEndpoints(outline, start, startDir, end, endDir)) {
                            auto emitArrow = [&](const ArrowheadData& a, const Point2Dd& tip, const Point2Dd& dir) {
                                if (!a.IsSet() || NativeArrowRef(a.Kind) != 0) return;
                                bool stroked = false;
                                const PathData shape = ArrowheadOutline(a, tip, dir, st.Width, stroked);
                                if (shape.commands.empty()) return;
                                EmitPathRecord(NormalizePath(shape), stroked ? strokeStyle : fillStyle, ctm, !stroked);
                            };
                            emitArrow(st.StartArrow, start, startDir);
                            emitArrow(st.EndArrow, end, endDir);
                        }
                    }
                }

                // ===== SHAPES =====

                void EmitRect(const VectorRect& r, const VectorStyle& style, const Matrix3x3& ctm) {
                    double rx = std::min<double>(r.RadiusX, r.Bounds.width / 2);
                    double ry = std::min<double>(r.RadiusY, r.Bounds.height / 2);
                    if (rx <= 0 && ry > 0) rx = ry;
                    if (ry <= 0 && rx > 0) ry = rx;

                    if (!AxisAligned(ctm)) {
                        auto segs = (rx > 0)
                                ? RoundedRectSegs(r.Bounds, rx, ry)
                                : RectSegs(r.Bounds);
                        EmitPathRecord(segs, style, ctm, true);
                        return;
                    }

                    Point2Dd p0 = ctm.Transform(Point2Dd(r.Bounds.x, r.Bounds.y));
                    Point2Dd p1 = ctm.Transform(Point2Dd(r.Bounds.x + r.Bounds.width,
                                                         r.Bounds.y + r.Bounds.height));
                    Point2Dd centre((p0.x + p1.x) / 2, (p0.y + p1.y) / 2);
                    double halfW = std::fabs(p1.x - p0.x) / 2;
                    double halfH = std::fabs(p1.y - p0.y) / 2;
                    double sx = std::fabs(ctm.m[0][0]);
                    double sy = std::fabs(ctm.m[1][1]);

                    XarBody b;
                    Coord(b, centre);
                    Vec(b, halfW, 0);
                    Vec(b, 0, -halfH);   // "up" in document space
                    uint32_t tag = XarOut::RectangleSimple;
                    if (rx > 0) {
                        tag = XarOut::RectangleSimpleRounded;
                        b.I32(Mp((rx * sx + ry * sy) / 2));
                    }
                    Rec(tag, b);
                    attrBounds = r.Bounds;
                    EmitShapeAttributes(style, ctm);
                }

                void EmitEllipseShape(const Point2Dd& center, double radX, double radY,
                                      const VectorStyle& style, const Matrix3x3& ctm) {
                    if (!AxisAligned(ctm)) {
                        EmitPathRecord(EllipseSegs(center, radX, radY), style, ctm, true);
                        return;
                    }
                    Point2Dd c = ctm.Transform(center);
                    double rx = radX * std::fabs(ctm.m[0][0]);
                    double ry = radY * std::fabs(ctm.m[1][1]);
                    XarBody b;
                    Coord(b, c);
                    Vec(b, rx, 0);
                    Vec(b, 0, -ry);
                    Rec(XarOut::EllipseSimple, b);
                    attrBounds = Rect2Dd(center.x - radX, center.y - radY, 2 * radX, 2 * radY);
                    EmitShapeAttributes(style, ctm);
                }

                void EmitPolySegs(const std::vector<Point2Dd>& pts, bool closed,
                                  const VectorStyle& style, const Matrix3x3& ctm) {
                    if (pts.size() < 2) return;
                    std::vector<XarPathSeg> segs;
                    segs.push_back({XarPathSeg::Move, {pts[0]}, false});
                    for (size_t i = 1; i < pts.size(); ++i) {
                        segs.push_back({XarPathSeg::Line, {pts[i]}, false});
                    }
                    if (closed) segs.back().closeAfter = true;
                    EmitPathRecord(segs, style, ctm, true);
                }

                // ===== PATHS =====

                void EmitPathRecord(const std::vector<XarPathSeg>& segs, const VectorStyle& style,
                                    const Matrix3x3& ctm, bool fillable) {
                    if (segs.empty()) return;

                    bool filled = fillable && HasVisibleFill(style);
                    bool stroked = HasVisibleStroke(style);
                    uint32_t tag = filled && stroked ? XarOut::PathFilledStroked
                                 : filled           ? XarOut::PathFilled
                                 : stroked          ? XarOut::PathStroked
                                                    : XarOut::Path;

                    std::vector<uint8_t> verbs;
                    std::vector<Point2Dd> coords;
                    for (const auto& s : segs) {
                        switch (s.kind) {
                            case XarPathSeg::Move:
                                verbs.push_back(0x06);
                                coords.push_back(ctm.Transform(s.p[0]));
                                break;
                            case XarPathSeg::Line:
                                verbs.push_back(s.closeAfter ? 0x03 : 0x02);
                                coords.push_back(ctm.Transform(s.p[0]));
                                break;
                            case XarPathSeg::Cubic:
                                verbs.push_back(0x04);
                                verbs.push_back(0x04);
                                verbs.push_back(s.closeAfter ? 0x05 : 0x04);
                                coords.push_back(ctm.Transform(s.p[0]));
                                coords.push_back(ctm.Transform(s.p[1]));
                                coords.push_back(ctm.Transform(s.p[2]));
                                break;
                        }
                    }

                    XarBody b;
                    b.U32(static_cast<uint32_t>(verbs.size()));
                    for (uint8_t v : verbs) b.U8(v);
                    while (b.bytes.size() % 4 != 0) b.U8(0);
                    for (const auto& c : coords) Coord(b, c);
                    Rec(tag, b);
                    // Untransformed extents for object-bounding-box gradients.
                    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
                    for (const auto& s : segs)
                        for (int i = 0; i < (s.kind == XarPathSeg::Cubic ? 3 : 1); ++i) {
                            minX = std::min(minX, s.p[i].x); maxX = std::max(maxX, s.p[i].x);
                            minY = std::min(minY, s.p[i].y); maxY = std::max(maxY, s.p[i].y);
                        }
                    attrBounds = minX <= maxX ? Rect2Dd(minX, minY, maxX - minX, maxY - minY) : Rect2Dd(0, 0, 0, 0);
                    EmitShapeAttributes(style, ctm);
                }

                // ===== ATTRIBUTES =====

                static bool HasVisibleFill(const VectorStyle& s) {
                    return s.Fill.has_value() &&
                           !std::holds_alternative<std::monostate>(*s.Fill);
                }
                static bool HasVisibleStroke(const VectorStyle& s) {
                    return s.Stroke.has_value() && s.Stroke->Width > 0 &&
                           !std::holds_alternative<std::monostate>(s.Stroke->Fill);
                }

                // Emits the attribute children (fill, line, transparency) of the
                // object record written immediately before.
                void EmitShapeAttributes(const VectorStyle& style, const Matrix3x3& ctm) {
                    Down();
                    uint8_t fillAlpha = 255;

                    if (HasVisibleFill(style)) {
                        const FillData& fill = *style.Fill;
                        if (const Color* c = std::get_if<Color>(&fill)) {
                            fillAlpha = c->a;
                            XarBody b;
                            b.I32(ColourRef(*c));
                            Rec(XarOut::FlatFill, b);
                        } else if (const GradientData* g = std::get_if<GradientData>(&fill)) {
                            EmitGradientFill(*g, ctm, attrBounds);
                        } else {
                            warn("XAR export: pattern/reference fills are not supported, "
                                 "filling flat black");
                            XarBody b;
                            b.I32(ColourRef(Color(0, 0, 0, 255)));
                            Rec(XarOut::FlatFill, b);
                        }
                    } else {
                        Rec(XarOut::FlatFillNone);
                    }

                    if (HasVisibleStroke(style)) {
                        const StrokeData& st = *style.Stroke;
                        Color sc(0, 0, 0, 255);
                        if (const Color* c = std::get_if<Color>(&st.Fill)) {
                            sc = *c;
                        } else {
                            warn("XAR export: non-solid stroke paint replaced with black");
                        }
                        XarBody lc;
                        lc.I32(ColourRef(sc));
                        Rec(XarOut::LineColour, lc);

                        XarBody lw;
                        lw.I32(Mp(st.Width * AvgScale(ctm)));
                        Rec(XarOut::LineWidth, lw);

                        uint8_t cap = st.LineCap == StrokeLineCap::Round ? 1
                                    : st.LineCap == StrokeLineCap::Square ? 2 : 0;
                        XarBody cb1; cb1.U8(cap); Rec(XarOut::StartCap, cb1);
                        XarBody cb2; cb2.U8(cap); Rec(XarOut::EndCap, cb2);

                        uint8_t join = st.LineJoin == StrokeLineJoin::Round ? 1
                                     : st.LineJoin == StrokeLineJoin::Bevel ? 2 : 0;
                        XarBody jb; jb.U8(join); Rec(XarOut::JoinStyle, jb);

                        XarBody mb;
                        mb.I32(static_cast<int32_t>(st.MiterLimit * 65536.0f));
                        Rec(XarOut::MitreLimit, mb);

                        if (!st.DashArray.empty()) {
                            XarBody db;
                            db.I32(DashRef(st.DashArray, AvgScale(ctm)));
                            Rec(XarOut::DashStyle, db);
                        }
                        if (st.StartArrow.IsSet() && NativeArrowRef(st.StartArrow.Kind) != 0) {
                            XarBody ab; ab.I32(NativeArrowRef(st.StartArrow.Kind)); Rec(XarOut::ArrowTail, ab);
                        }
                        if (st.EndArrow.IsSet() && NativeArrowRef(st.EndArrow.Kind) != 0) {
                            XarBody ab; ab.I32(NativeArrowRef(st.EndArrow.Kind)); Rec(XarOut::ArrowHead, ab);
                        }
                        if ((st.StartArrow.IsSet() && std::fabs(st.StartArrow.Scale - 1.0f) > 1e-4f) ||
                            (st.EndArrow.IsSet() && std::fabs(st.EndArrow.Scale - 1.0f) > 1e-4f))
                            EmitUserValue(kArrowScaleKey, "start=" + Num(st.StartArrow.Scale) + ";end=" + Num(st.EndArrow.Scale));
                        const float strokeOpacity = style.StrokeOpacity * st.Opacity;
                        if (strokeOpacity < 0.999f) {
                            XarBody lt;
                            lt.U8(LevelByte(1.0 - strokeOpacity));
                            lt.U8(1);
                            Rec(XarOut::LineTransparency, lt);
                        }
                    } else {
                        Rec(XarOut::LineColourNone);
                    }

                    EmitTransparency(style, ctm, fillAlpha);
                    EmitFeather(ctm);
                    Up();
                }

                // A gradient point in document space: bounding-box units
                // resolve against the object's extents, then the gradient's
                // own transform and the CTM apply.
                static Point2Dd GradientPoint(const Point2Dd& p, GradientUnits units,
                                              const std::optional<Matrix3x3>& gradTransform,
                                              const Rect2Dd& bounds, const Matrix3x3& ctm) {
                    Point2Dd q = gradTransform ? gradTransform->Transform(p) : p;
                    if (units == GradientUnits::ObjectBoundingBox)
                        q = Point2Dd(bounds.x + q.x * bounds.width, bounds.y + q.y * bounds.height);
                    return ctm.Transform(q);
                }

                // Two-stop records for two stops; the multistage variants carry
                // the inner stops as (position, colour) pairs.
                void EmitStops(XarBody& b, const std::vector<GradientStop>& stops, bool multistage) {
                    Color c0(0, 0, 0, 255), c1(255, 255, 255, 255);
                    if (!stops.empty()) { c0 = stops.front().color; c1 = stops.back().color; }
                    b.I32(ColourRef(c0));
                    b.I32(ColourRef(c1));
                    if (multistage) {
                        b.I32(static_cast<int32_t>(stops.size() - 2));
                        for (size_t i = 1; i + 1 < stops.size(); ++i) {
                            b.F64(stops[i].position);
                            b.I32(ColourRef(stops[i].color));
                        }
                    }
                }

                void EmitGradientFill(const GradientData& g, const Matrix3x3& ctm, const Rect2Dd& bounds) {
                    if (const LinearGradientData* lg = std::get_if<LinearGradientData>(&g)) {
                        const bool multi = lg->Stops.size() > 2;
                        XarBody b;
                        Coord(b, GradientPoint(lg->Start, lg->Units, lg->Transform, bounds, ctm));
                        Coord(b, GradientPoint(lg->End, lg->Units, lg->Transform, bounds, ctm));
                        EmitStops(b, lg->Stops, multi);
                        Rec(multi ? XarOut::LinearFillMultistage : XarOut::LinearFill, b);
                    } else if (const RadialGradientData* rg = std::get_if<RadialGradientData>(&g)) {
                        const bool multi = rg->Stops.size() > 2;
                        XarBody b;
                        Coord(b, GradientPoint(rg->Center, rg->Units, rg->Transform, bounds, ctm));
                        Coord(b, GradientPoint(Point2Dd(rg->Center.x + rg->Radius, rg->Center.y), rg->Units, rg->Transform, bounds, ctm));
                        EmitStops(b, rg->Stops, multi);
                        Rec(multi ? XarOut::CircularFillMultistage : XarOut::CircularFill, b);
                    } else if (const ConicalGradientData* cg = std::get_if<ConicalGradientData>(&g)) {
                        const bool multi = cg->Stops.size() > 2;
                        const double a = cg->StartAngle * M_PI / 180.0;
                        const double r = cg->Units == GradientUnits::ObjectBoundingBox ? 0.5
                                       : std::max(1.0, std::max(bounds.width, bounds.height) / 2.0);
                        XarBody b;
                        Coord(b, GradientPoint(cg->Center, cg->Units, cg->Transform, bounds, ctm));
                        Coord(b, GradientPoint(Point2Dd(cg->Center.x + r * std::cos(a), cg->Center.y + r * std::sin(a)),
                                               cg->Units, cg->Transform, bounds, ctm));
                        EmitStops(b, cg->Stops, multi);
                        Rec(multi ? XarOut::ConicalFillMultistage : XarOut::ConicalFill, b);
                    } else {
                        warn("XAR export: mesh gradients are not supported, filling flat with grey");
                        XarBody b;
                        b.I32(ColourRef(Color(128, 128, 128, 255)));
                        Rec(XarOut::FlatFill, b);
                    }
                }

                // ===== TEXT =====

                struct TextChunkStyle {
                    std::string family;
                    float size = 12.0f;
                    bool bold = false, italic = false, underline = false;
                };

                static TextChunkStyle ResolveChunkStyle(const VectorTextStyle& s,
                                                        const VectorTextStyle& base) {
                    TextChunkStyle out;
                    out.family = s.FontFamily.empty() ? base.FontFamily : s.FontFamily;
                    out.size = s.FontSize > 0 ? s.FontSize : base.FontSize;
                    out.bold = s.Weight == FontWeight::Bold || s.Weight == FontWeight::ExtraBold;
                    out.italic = s.Slant != FontSlant::Normal;
                    out.underline = s.Underline;
                    return out;
                }

                void EmitTextStyleDelta(const TextChunkStyle& want, TextChunkStyle& have) {
                    if (want.family != have.family && !want.family.empty()) {
                        XarBody b;
                        b.I32(FontRef(want.family));
                        Rec(XarOut::TextFontTypeface, b);
                    }
                    if (want.size != have.size && want.size > 0) {
                        XarBody b;
                        b.I32(Mp(want.size));
                        Rec(XarOut::TextFontSize, b);
                    }
                    if (want.bold != have.bold) Rec(want.bold ? XarOut::TextBoldOn : XarOut::TextBoldOff);
                    if (want.italic != have.italic) Rec(want.italic ? XarOut::TextItalicOn : XarOut::TextItalicOff);
                    if (want.underline != have.underline) {
                        Rec(want.underline ? XarOut::TextUnderlineOn : XarOut::TextUnderlineOff);
                    }
                    have = want;
                }

                void EmitText(const VectorText& text, const VectorStyle& style,
                              const Matrix3x3& ctm) {
                    if (!AxisAligned(ctm)) {
                        warn("XAR export: rotated/skewed text is exported without its "
                             "rotation (story matrices are not written yet)");
                    }

                    XarBody sb;
                    Coord(sb, ctm.Transform(text.Position));
                    sb.U32(0);
                    Rec(XarOut::TextStorySimple, sb);
                    Down();

                    switch (text.BaseStyle.Anchor) {
                        case TextAnchor::Middle: Rec(XarOut::TextJustificationCentre); break;
                        case TextAnchor::End: Rec(XarOut::TextJustificationRight); break;
                        default: Rec(XarOut::TextJustificationLeft); break;
                    }

                    Color tc(0, 0, 0, 255);
                    if (style.Fill.has_value()) {
                        if (const Color* c = std::get_if<Color>(&*style.Fill)) tc = *c;
                    }
                    XarBody fb;
                    fb.I32(ColourRef(tc));
                    Rec(XarOut::FlatFill, fb);
                    Rec(XarOut::LineColourNone);
                    EmitTransparency(style, ctm, tc.a);
                    EmitFeather(ctm);

                    TextChunkStyle storyState;   // reader defaults
                    storyState.size = 0;         // force explicit size on first delta
                    TextChunkStyle baseState = ResolveChunkStyle(text.BaseStyle, text.BaseStyle);
                    EmitTextStyleDelta(baseState, storyState);

                    // Flatten spans into lines on '\n'.
                    struct Chunk { std::string text; TextChunkStyle style; };
                    std::vector<std::vector<Chunk>> lines(1);
                    for (const auto& span : text.Spans) {
                        TextChunkStyle cs = ResolveChunkStyle(span.Style, text.BaseStyle);
                        std::string piece;
                        for (char ch : span.Text) {
                            if (ch == '\n') {
                                if (!piece.empty()) lines.back().push_back({piece, cs});
                                piece.clear();
                                lines.emplace_back();
                            } else {
                                piece.push_back(ch);
                            }
                        }
                        if (!piece.empty()) lines.back().push_back({piece, cs});
                    }

                    for (const auto& line : lines) {
                        Rec(XarOut::TextLine);
                        Down();
                        TextChunkStyle lineState = storyState;
                        for (const auto& chunk : line) {
                            EmitTextStyleDelta(chunk.style, lineState);
                            XarBody b;
                            b.Utf16(chunk.text);
                            Rec(XarOut::TextString, b);
                        }
                        Rec(XarOut::TextEOL);
                        Up();
                    }

                    Up();
                }
            };
        }   // anonymous namespace

        bool XARConverter::Impl::ExportToFile(
                const VectorDocument& document,
                const std::string& filename,
                const ConversionOptions& options,
                const XARConversionOptions& xarOptions) {

            auto data = ExportToMemory(document, options, xarOptions);
            if (data.empty()) return false;

            std::ofstream file(filename, std::ios::binary);
            if (!file.is_open()) {
                LogWarning("Failed to create XAR file: " + filename);
                return false;
            }
            file.write(reinterpret_cast<const char*>(data.data()),
                       static_cast<std::streamsize>(data.size()));
            return file.good();
        }

        std::vector<uint8_t> XARConverter::Impl::ExportToMemory(
                const VectorDocument& document,
                const ConversionOptions& options,
                const XARConversionOptions& xarOptions) {

            currentOptions = options;
            currentXarOptions = xarOptions;
            exportState.Reset();

            XarEmitter emitter(document, [this](const std::string& msg) { LogWarning(msg); },
                               xarOptions.PreserveEffects);
            auto data = emitter.Build();
            ReportProgress(1.0f);
            return data;
        }

    } // namespace VectorConverter
} // namespace UltraCanvas