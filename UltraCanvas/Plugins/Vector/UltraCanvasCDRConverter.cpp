// UltraCanvas/Plugins/Vector/UltraCanvasCDRConverter.cpp
// CDR (CorelDRAW) writer - see the header for scope.
//
// The emitted structure, validated record-by-record against libcdr's parser
// (CDRParser.cpp, version-700 branches):
//
//   RIFF <size> "CDR7"
//     vrsn                u16 700
//     LIST <size> "doc "
//       mcfg              page width, height (1/254000 inch units)
//       fild ...          fill definitions   (id, type 1 solid, RGB colour)
//       outl ...          outline definitions (id, line state, colour, dash)
//       LIST <size> "page"
//         LIST <size> "obj "     one per leaf shape, REVERSE z-order
//           trfd          identity transform (coordinates are pre-baked)
//           loda          chunk type 3 (line and curve): points + type bytes,
//                         fill/outline references, optional fill opacity
//       xtra              empty trailer so the last object flushes
//
// Coordinates are 32-bit signed units of 1/254000 inch in a page-centred,
// Y-up system; the document tree is flattened with transforms baked into
// the points (same walk as the XAR and EPS writers, sharing PathOps).
// Objects are written topmost-first because libcdr draws them in reverse.
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "UltraCanvasCDRConverter.h"
#include "UltraCanvasVectorStorage.h"
#include "UltraCanvasVectorPathOps.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <variant>

namespace UltraCanvas {
    namespace VectorConverter {

        using namespace VectorStorage;

        namespace {

            using namespace PathOps;

            constexpr double kUnitsPerPoint = 254000.0 / 72.0;

            // Little-endian byte builder.
            struct CdrBuf {
                std::vector<uint8_t> bytes;

                void U8(uint8_t v) { bytes.push_back(v); }
                void U16(uint16_t v) {
                    bytes.push_back(static_cast<uint8_t>(v));
                    bytes.push_back(static_cast<uint8_t>(v >> 8));
                }
                void U32(uint32_t v) {
                    bytes.push_back(static_cast<uint8_t>(v));
                    bytes.push_back(static_cast<uint8_t>(v >> 8));
                    bytes.push_back(static_cast<uint8_t>(v >> 16));
                    bytes.push_back(static_cast<uint8_t>(v >> 24));
                }
                void I32(int32_t v) { U32(static_cast<uint32_t>(v)); }
                void F64(double v) {
                    uint64_t bits;
                    std::memcpy(&bits, &v, sizeof(bits));
                    for (int i = 0; i < 8; ++i) {
                        bytes.push_back(static_cast<uint8_t>(bits >> (8 * i)));
                    }
                }
                void Pad(size_t n) { bytes.insert(bytes.end(), n, 0); }
                void Raw(const std::vector<uint8_t>& v) {
                    bytes.insert(bytes.end(), v.begin(), v.end());
                }
            };

            // fourCC + u32 length + body (+ even padding; the parser skips
            // stray zero bytes between records, per the RIFF convention).
            void AppendChunk(std::vector<uint8_t>& out, const char* tag,
                             const std::vector<uint8_t>& body) {
                out.insert(out.end(), tag, tag + 4);
                uint32_t len = static_cast<uint32_t>(body.size());
                out.push_back(static_cast<uint8_t>(len));
                out.push_back(static_cast<uint8_t>(len >> 8));
                out.push_back(static_cast<uint8_t>(len >> 16));
                out.push_back(static_cast<uint8_t>(len >> 24));
                out.insert(out.end(), body.begin(), body.end());
                if (body.size() % 2) out.push_back(0);
            }

            void AppendList(std::vector<uint8_t>& out, const char* listType,
                            const std::vector<uint8_t>& contents) {
                std::vector<uint8_t> body;
                body.insert(body.end(), listType, listType + 4);
                body.insert(body.end(), contents.begin(), contents.end());
                AppendChunk(out, "LIST", body);
            }

            class CdrEmitter {
            public:
                CdrEmitter(const VectorDocument& document,
                           std::function<void(const std::string&)> warnFn)
                        : doc(document), warn(std::move(warnFn)) {}

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

                    for (const auto& layer : doc.Layers) {
                        if (layer && layer->Visible) {
                            for (const auto& child : layer->Children) {
                                if (child) CollectElement(*child, layer->Style,
                                                          Matrix3x3::Identity());
                            }
                        }
                    }

                    // page LIST: objects topmost-first (libcdr draws in
                    // reverse order).
                    std::vector<uint8_t> pageContents;
                    for (auto it = objects.rbegin(); it != objects.rend(); ++it) {
                        std::vector<uint8_t> objContents;
                        AppendChunk(objContents, "trfd", IdentityTrfd());
                        AppendChunk(objContents, "loda", *it);
                        AppendList(pageContents, "obj ", objContents);
                    }

                    std::vector<uint8_t> docContents;
                    CdrBuf mcfg;
                    mcfg.I32(Units(pageW));
                    mcfg.I32(Units(pageH));
                    AppendChunk(docContents, "mcfg", mcfg.bytes);
                    for (const auto& f : fildChunks) AppendChunk(docContents, "fild", f);
                    for (const auto& o : outlChunks) AppendChunk(docContents, "outl", o);
                    AppendList(docContents, "page", pageContents);
                    // A trailing record at document level makes the collector
                    // flush the last object (flushing happens when a record
                    // arrives at or above the object's tree level).
                    AppendChunk(docContents, "xtra", {});

                    std::vector<uint8_t> riffContents;
                    CdrBuf vrsn;
                    vrsn.U16(700);
                    AppendChunk(riffContents, "vrsn", vrsn.bytes);
                    AppendList(riffContents, "doc ", docContents);

                    std::vector<uint8_t> out;
                    AppendList(out, "CDR7", riffContents);
                    // The outermost chunk is "RIFF", not "LIST".
                    out[0] = 'R'; out[1] = 'I'; out[2] = 'F'; out[3] = 'F';
                    return out;
                }

            private:
                const VectorDocument& doc;
                std::function<void(const std::string&)> warn;
                double pageW = 0, pageH = 0;                     // points
                std::map<uint32_t, uint32_t> fillIds;            // rgb -> fild id
                std::map<std::string, uint32_t> strokeIds;       // key -> outl id
                std::vector<std::vector<uint8_t>> fildChunks;
                std::vector<std::vector<uint8_t>> outlChunks;
                std::vector<std::vector<uint8_t>> objects;       // loda bodies, doc order
                bool warnedGradient = false;

                static int32_t Units(double pt) {
                    return static_cast<int32_t>(std::lround(pt * kUnitsPerPoint));
                }
                // Document space (points, Y down, top-left origin) -> CDR
                // object space (1/254000 inch, Y up, page-centred).
                int32_t CX(double xPt) const {
                    return static_cast<int32_t>(std::lround((xPt - pageW / 2) * kUnitsPerPoint));
                }
                int32_t CY(double yPt) const {
                    return static_cast<int32_t>(std::lround((pageH / 2 - yPt) * kUnitsPerPoint));
                }
                static double AvgScale(const Matrix3x3& m) {
                    double det = std::fabs(static_cast<double>(m.m[0][0]) * m.m[1][1] -
                                           static_cast<double>(m.m[0][1]) * m.m[1][0]);
                    return det > 0 ? std::sqrt(det) : 1.0;
                }

                static std::vector<uint8_t> IdentityTrfd() {
                    CdrBuf b;
                    b.U32(72);   // chunk length
                    b.U32(1);    // one argument
                    b.U32(12);   // offset of the argument-offset table
                    b.U32(16);   // offset of the argument
                    b.U16(0x08); // transform argument
                    b.Pad(6);
                    b.F64(1.0); b.F64(0.0); b.F64(0.0);   // v0 v1 x0
                    b.F64(0.0); b.F64(1.0); b.F64(0.0);   // v3 v4 y0
                    return b.bytes;
                }

                // ===== STYLE DEFINITIONS =====

                static uint32_t Rgb(const Color& c) {
                    return (static_cast<uint32_t>(c.r) << 16) |
                           (static_cast<uint32_t>(c.g) << 8) | c.b;
                }

                static void WriteColor(CdrBuf& b, uint32_t rgb) {
                    b.U16(0x05);   // colour model: RGB
                    b.U16(0);      // palette
                    b.U32(0);
                    b.U32(rgb);
                }

                uint32_t FillId(uint32_t rgb) {
                    auto it = fillIds.find(rgb);
                    if (it != fillIds.end()) return it->second;
                    uint32_t id = static_cast<uint32_t>(fildChunks.size()) + 1;
                    CdrBuf b;
                    b.U32(id);
                    b.U16(1);      // solid
                    b.Pad(2);
                    WriteColor(b, rgb);
                    fildChunks.push_back(std::move(b.bytes));
                    fillIds[rgb] = id;
                    return id;
                }

                uint32_t StrokeId(const StrokeData& st, double scale) {
                    Color sc(0, 0, 0, 255);
                    if (const Color* c = std::get_if<Color>(&st.Fill)) sc = *c;
                    else warn("CDR export: non-solid stroke paint replaced with black");
                    double widthPt = st.Width * scale;

                    // Dash lengths are stored in line-width multiples.
                    std::vector<uint16_t> dash;
                    for (size_t i = 0; i < st.DashArray.size() && dash.size() < 10; ++i) {
                        double units = widthPt > 0 ? st.DashArray[i] * scale / widthPt : 1.0;
                        dash.push_back(static_cast<uint16_t>(
                                std::max(1.0, std::min(65535.0, std::round(units)))));
                    }
                    if (dash.size() % 2) dash.push_back(dash.back());

                    uint16_t cap = st.LineCap == StrokeLineCap::Round ? 1
                                 : st.LineCap == StrokeLineCap::Square ? 2 : 0;
                    uint16_t join = st.LineJoin == StrokeLineJoin::Round ? 1
                                  : st.LineJoin == StrokeLineJoin::Bevel ? 2 : 0;

                    std::ostringstream key;
                    key << Rgb(sc) << ":" << Units(widthPt) << ":" << cap << ":" << join;
                    for (uint16_t d : dash) key << ":" << d;
                    auto it = strokeIds.find(key.str());
                    if (it != strokeIds.end()) return it->second;

                    uint32_t id = static_cast<uint32_t>(outlChunks.size()) + 1;
                    CdrBuf b;
                    b.U32(id);
                    b.U16(dash.empty() ? 0x02 : 0x06);   // solid / dashed
                    b.U16(cap);
                    b.U16(join);
                    b.Pad(2);
                    b.I32(Units(widthPt));
                    b.U16(100);    // stretch, percent
                    b.Pad(2);
                    b.I32(0);      // angle
                    b.Pad(52);
                    WriteColor(b, Rgb(sc));
                    b.Pad(16);
                    b.U16(static_cast<uint16_t>(dash.size()));
                    for (uint16_t d : dash) b.U16(d);
                    b.Pad(22 - 2 * dash.size());   // dash area is 22 bytes
                    b.U32(0);      // start marker
                    b.U32(0);      // end marker
                    outlChunks.push_back(std::move(b.bytes));
                    strokeIds[key.str()] = id;
                    return id;
                }

                static bool HasVisibleFill(const VectorStyle& s) {
                    return s.Fill.has_value() &&
                           !std::holds_alternative<std::monostate>(*s.Fill);
                }
                static bool HasVisibleStroke(const VectorStyle& s) {
                    return s.Stroke.has_value() && s.Stroke->Width > 0 &&
                           !std::holds_alternative<std::monostate>(s.Stroke->Fill);
                }

                Color FlattenFill(const FillData& fill) {
                    if (const Color* c = std::get_if<Color>(&fill)) return *c;
                    if (const GradientData* g = std::get_if<GradientData>(&fill)) {
                        if (!warnedGradient) {
                            warnedGradient = true;
                            warn("CDR export: gradients are not written yet; "
                                 "filling with the blend of the end stops");
                        }
                        const std::vector<GradientStop>* stops = nullptr;
                        if (const auto* lg = std::get_if<LinearGradientData>(g)) stops = &lg->Stops;
                        else if (const auto* rg = std::get_if<RadialGradientData>(g)) stops = &rg->Stops;
                        else if (const auto* cg = std::get_if<ConicalGradientData>(g)) stops = &cg->Stops;
                        if (stops && !stops->empty()) {
                            const Color& c0 = stops->front().color;
                            const Color& c1 = stops->back().color;
                            return Color(static_cast<uint8_t>((c0.r + c1.r) / 2),
                                         static_cast<uint8_t>((c0.g + c1.g) / 2),
                                         static_cast<uint8_t>((c0.b + c1.b) / 2),
                                         static_cast<uint8_t>((c0.a + c1.a) / 2));
                        }
                        return Color(128, 128, 128, 255);
                    }
                    warn("CDR export: pattern/reference fills are not supported, "
                         "filling flat black");
                    return Color(0, 0, 0, 255);
                }

                // ===== TREE =====

                void CollectElement(const VectorElement& e, const VectorStyle& inherited,
                                    const Matrix3x3& parentCtm) {
                    if (!e.Style.Visible || !e.Style.Display) return;

                    VectorStyle eff = e.Style;
                    eff.Inherit(inherited);
                    Matrix3x3 ctm = e.Transform ? parentCtm * (*e.Transform) : parentCtm;

                    switch (e.Type) {
                        case VectorElementType::Group:
                        case VectorElementType::Symbol:
                        case VectorElementType::Layer: {
                            const auto& g = static_cast<const VectorGroup&>(e);
                            for (const auto& child : g.Children) {
                                if (child) CollectElement(*child, eff, ctm);
                            }
                            break;
                        }
                        case VectorElementType::Rectangle:
                        case VectorElementType::RoundedRectangle: {
                            const auto& r = static_cast<const VectorRect&>(e);
                            double rx = std::min<double>(r.RadiusX, r.Bounds.width / 2);
                            double ry = std::min<double>(r.RadiusY, r.Bounds.height / 2);
                            if (rx <= 0 && ry > 0) rx = ry;
                            if (ry <= 0 && rx > 0) ry = rx;
                            auto segs = (rx > 0) ? RoundedRectSegs(r.Bounds, rx, ry)
                                                 : RectSegs(r.Bounds);
                            AddObject(segs, eff, ctm, true);
                            break;
                        }
                        case VectorElementType::Circle: {
                            const auto& c = static_cast<const VectorCircle&>(e);
                            AddObject(EllipseSegs(c.Center, c.Radius, c.Radius),
                                      eff, ctm, true);
                            break;
                        }
                        case VectorElementType::Ellipse: {
                            const auto& el = static_cast<const VectorEllipse&>(e);
                            AddObject(EllipseSegs(el.Center, el.RadiusX, el.RadiusY),
                                      eff, ctm, true);
                            break;
                        }
                        case VectorElementType::Line: {
                            const auto& ln = static_cast<const VectorLine&>(e);
                            std::vector<FlatSeg> segs;
                            segs.push_back({FlatSeg::Move, {ln.Start}, false});
                            segs.push_back({FlatSeg::Line, {ln.End}, false});
                            AddObject(segs, eff, ctm, false);
                            break;
                        }
                        case VectorElementType::Polyline: {
                            const auto& pl = static_cast<const VectorPolyline&>(e);
                            AddPoly(pl.Points, false, eff, ctm);
                            break;
                        }
                        case VectorElementType::Polygon: {
                            const auto& pg = static_cast<const VectorPolygon&>(e);
                            AddPoly(pg.Points, true, eff, ctm);
                            break;
                        }
                        case VectorElementType::Path: {
                            const auto& p = static_cast<const VectorPath&>(e);
                            AddObject(NormalizePath(p.Path), eff, ctm, true);
                            break;
                        }
                        case VectorElementType::Text:
                            warn("CDR export: text is not written yet, skipped");
                            break;
                        default:
                            warn("CDR export: element type not supported, skipped (type " +
                                 std::to_string(static_cast<int>(e.Type)) + ")");
                            break;
                    }
                }

                void AddPoly(const std::vector<Point2Dd>& pts, bool closed,
                             const VectorStyle& style, const Matrix3x3& ctm) {
                    if (pts.size() < 2) return;
                    std::vector<FlatSeg> segs;
                    segs.push_back({FlatSeg::Move, {pts[0]}, false});
                    for (size_t i = 1; i < pts.size(); ++i) {
                        segs.push_back({FlatSeg::Line, {pts[i]}, false});
                    }
                    if (closed) segs.back().closeAfter = true;
                    AddObject(segs, style, ctm, true);
                }

                // ===== OBJECT (loda) =====

                void AddObject(const std::vector<FlatSeg>& segs, const VectorStyle& style,
                               const Matrix3x3& ctm, bool fillable) {
                    if (segs.empty()) return;
                    bool filled = fillable && HasVisibleFill(style);
                    bool stroked = HasVisibleStroke(style);
                    if (!filled && !stroked) return;

                    // Geometry: point list + per-point type bytes.
                    // 0x00 move, 0x40 line, 0xC0 control point, 0x80 cubic
                    // endpoint; bit 0x08 closes the subpath.
                    std::vector<std::pair<int32_t, int32_t>> points;
                    std::vector<uint8_t> types;
                    auto add = [&](const Point2Dd& local, uint8_t type) {
                        Point2Dd p = ctm.Transform(local);
                        points.emplace_back(CX(p.x), CY(p.y));
                        types.push_back(type);
                    };
                    for (const auto& s : segs) {
                        switch (s.kind) {
                            case FlatSeg::Move:
                                add(s.p[0], 0x00);
                                break;
                            case FlatSeg::Line:
                                add(s.p[0], s.closeAfter ? 0x48 : 0x40);
                                break;
                            case FlatSeg::Cubic:
                                add(s.p[0], 0xC0);
                                add(s.p[1], 0xC0);
                                add(s.p[2], s.closeAfter ? 0x88 : 0x80);
                                break;
                        }
                    }
                    if (points.size() > 0xFFFF) {
                        warn("CDR export: path with more than 65535 points truncated");
                        points.resize(0xFFFF);
                        types.resize(0xFFFF);
                    }

                    CdrBuf coords;
                    coords.U16(static_cast<uint16_t>(points.size()));
                    coords.U16(0);
                    for (const auto& p : points) {
                        coords.I32(p.first);
                        coords.I32(p.second);
                    }
                    for (uint8_t t : types) coords.U8(t);

                    // Arguments: geometry, then style references.
                    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> args;
                    args.emplace_back(0x1e, std::move(coords.bytes));
                    if (filled) {
                        Color fc = FlattenFill(*style.Fill);
                        CdrBuf b;
                        b.U32(FillId(Rgb(fc)));
                        args.emplace_back(0x14, std::move(b.bytes));

                        float opacity = style.Opacity * style.FillOpacity *
                                        (static_cast<float>(fc.a) / 255.0f);
                        if (opacity < 0.999f) {
                            CdrBuf ob;
                            ob.Pad(10);
                            ob.U16(static_cast<uint16_t>(std::lround(
                                    std::max(0.0f, std::min(1.0f, opacity)) * 1000.0f)));
                            args.emplace_back(0x1f40, std::move(ob.bytes));
                        }
                    }
                    if (stroked) {
                        CdrBuf b;
                        b.U32(StrokeId(*style.Stroke, AvgScale(ctm)));
                        args.emplace_back(0x0a, std::move(b.bytes));
                    }

                    // Chunk assembly: 20-byte header, argument blocks
                    // (4-aligned), the offset table, then the type table
                    // (stored last-argument-first, as the parser reads it).
                    const uint32_t headerSize = 20;
                    std::vector<uint32_t> offsets;
                    uint32_t pos = headerSize;
                    for (const auto& a : args) {
                        offsets.push_back(pos);
                        pos += static_cast<uint32_t>(a.second.size());
                        pos = (pos + 3) & ~3u;
                    }
                    uint32_t startOfArgs = pos;
                    uint32_t startOfArgTypes = startOfArgs + 4 * static_cast<uint32_t>(args.size());
                    uint32_t chunkLength = startOfArgTypes + 4 * static_cast<uint32_t>(args.size());

                    CdrBuf b;
                    b.U32(chunkLength);
                    b.U32(static_cast<uint32_t>(args.size()));
                    b.U32(startOfArgs);
                    b.U32(startOfArgTypes);
                    b.U32(3);   // chunk type: line and curve
                    for (size_t i = 0; i < args.size(); ++i) {
                        b.Raw(args[i].second);
                        while (b.bytes.size() < (i + 1 < offsets.size()
                                                         ? offsets[i + 1]
                                                         : startOfArgs)) {
                            b.U8(0);
                        }
                    }
                    for (uint32_t off : offsets) b.U32(off);
                    for (size_t i = args.size(); i > 0; --i) b.U32(args[i - 1].first);

                    objects.push_back(std::move(b.bytes));
                }
            };

        }   // anonymous namespace

// ===== PUBLIC INTERFACE =====

        FormatCapabilities CDRConverter::GetCapabilities() const {
            FormatCapabilities caps;
            caps.SupportsRectangle = true;
            caps.SupportsCircle = true;
            caps.SupportsEllipse = true;
            caps.SupportsLine = true;
            caps.SupportsPolyline = true;
            caps.SupportsPolygon = true;
            caps.SupportsPath = true;
            caps.SupportsCubicBezier = true;
            caps.SupportsQuadraticBezier = true;   // converted to cubics
            caps.SupportsArc = true;               // converted to cubics
            caps.SupportsCompoundPaths = true;
            caps.SupportsSolidFill = true;
            caps.SupportsOpacity = true;
            caps.SupportsDashing = true;
            caps.SupportsGroups = true;            // flattened
            caps.SupportsLayers = true;            // flattened
            return caps;
        }

        std::shared_ptr<VectorStorage::VectorDocument> CDRConverter::Import(
                const std::string& filename, const ConversionOptions& options) {
            (void)filename;
            if (options.WarningCallback) {
                options.WarningCallback(
                        "CDRConverter cannot import; render CDR files through the "
                        "CDR plugin (UltraCanvasCDRPlugin) instead");
            }
            return nullptr;
        }

        std::shared_ptr<VectorStorage::VectorDocument> CDRConverter::ImportFromString(
                const std::string& data, const ConversionOptions& options) {
            (void)data;
            return Import("", options);
        }

        std::shared_ptr<VectorStorage::VectorDocument> CDRConverter::ImportFromStream(
                std::istream& stream, const ConversionOptions& options) {
            (void)stream;
            return Import("", options);
        }

        bool CDRConverter::Export(
                const VectorStorage::VectorDocument& document,
                const std::string& filename,
                const ConversionOptions& options) {
            std::string data = ExportToString(document, options);
            if (data.empty()) return false;
            std::ofstream file(filename, std::ios::binary);
            if (!file.is_open()) {
                if (options.WarningCallback) {
                    options.WarningCallback("Failed to create CDR file: " + filename);
                }
                return false;
            }
            file.write(data.data(), static_cast<std::streamsize>(data.size()));
            return file.good();
        }

        std::string CDRConverter::ExportToString(
                const VectorStorage::VectorDocument& document,
                const ConversionOptions& options) {
            CdrEmitter emitter(document, [&options](const std::string& msg) {
                if (options.WarningCallback) options.WarningCallback(msg);
            });
            auto data = emitter.Build();
            if (options.ProgressCallback) options.ProgressCallback(1.0f);
            return std::string(data.begin(), data.end());
        }

        bool CDRConverter::ExportToStream(
                const VectorStorage::VectorDocument& document,
                std::ostream& stream,
                const ConversionOptions& options) {
            std::string data = ExportToString(document, options);
            stream.write(data.data(), static_cast<std::streamsize>(data.size()));
            return stream.good();
        }

        bool CDRConverter::ValidateFile(const std::string& filename) const {
            std::ifstream file(filename, std::ios::binary);
            if (!file.is_open()) return false;
            char head[12] = {0};
            file.read(head, 12);
            if (file.gcount() < 12) return false;
            return ValidateData(std::string(head, 12));
        }

        bool CDRConverter::ValidateData(const std::string& data) const {
            return data.size() >= 12 &&
                   data.compare(0, 4, "RIFF") == 0 &&
                   data.compare(8, 3, "CDR") == 0;
        }

    } // namespace VectorConverter
} // namespace UltraCanvas
