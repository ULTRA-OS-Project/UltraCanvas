// Plugins/Vector/XAR/UltraCanvasXARPlugin.cpp
// Xara XAR vector graphics format plugin implementation for UltraCanvas
// Version: 1.1.0
// Last Modified: 2025-06-22
// Author: UltraCanvas Framework

#include "UltraCanvasXARPlugin.h"
#include <fstream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <zlib.h>

namespace UltraCanvas {

    static constexpr float PI = 3.14159265358979323846f;

// ===== XAR NODE =====

    Rect2Df XARNode::CalculateBounds() const {
        if (children.empty()) return bounds;
        Rect2Df totalBounds = bounds;
        for (const auto& child : children) {
            Rect2Df childBounds = child->CalculateBounds();
            if (childBounds.width > 0 && childBounds.height > 0) {
                if (totalBounds.width == 0 && totalBounds.height == 0) {
                    totalBounds = childBounds;
                } else {
                    float minX = std::min(totalBounds.x, childBounds.x);
                    float minY = std::min(totalBounds.y, childBounds.y);
                    float maxX = std::max(totalBounds.x + totalBounds.width, childBounds.x + childBounds.width);
                    float maxY = std::max(totalBounds.y + totalBounds.height, childBounds.y + childBounds.height);
                    totalBounds = Rect2Df(minX, minY, maxX - minX, maxY - minY);
                }
            }
        }
        return totalBounds;
    }

// ===== XAR PATH NODE =====

    void XARPathNode::Render(IRenderContext* ctx, float scale) {
        if (commands.empty()) { XARNode::Render(ctx, scale); return; }

        ctx->PushState();
        RenderPath(ctx, scale);

        if (isFilled && hasFill) {
            switch (fill.type) {
                case XARFillType::Flat:
                    ctx->SetFillPaint(fill.startColor);
                    break;
                case XARFillType::LinearGradient: {
                    Point2Df start = MillipointsToPixels(fill.startPoint, scale);
                    Point2Df end = MillipointsToPixels(fill.endPoint, scale);
                    std::vector<GradientStop> stops;
                    if (!fill.gradientStops.empty()) {
                        stops = fill.gradientStops;
                    } else {
                        stops.push_back(GradientStop(0.0f, fill.startColor));
                        stops.push_back(GradientStop(1.0f, fill.endColor));
                    }
                    auto gradient = ctx->CreateLinearGradientPattern(start.x, start.y, end.x, end.y, stops);
                    if (gradient) ctx->SetFillPaint(gradient);
                    break;
                }
                case XARFillType::CircularGradient:
                case XARFillType::EllipticalGradient: {
                    Point2Df center = MillipointsToPixels(fill.startPoint, scale);
                    Point2Df edge = MillipointsToPixels(fill.endPoint, scale);
                    float radius = std::sqrt((edge.x-center.x)*(edge.x-center.x) + (edge.y-center.y)*(edge.y-center.y));
                    std::vector<GradientStop> stops;
                    if (!fill.gradientStops.empty()) stops = fill.gradientStops;
                    else { stops.push_back(GradientStop(0.0f, fill.startColor)); stops.push_back(GradientStop(1.0f, fill.endColor)); }
                    auto gradient = ctx->CreateRadialGradientPattern(center.x, center.y, 0, center.x, center.y, radius, stops);
                    if (gradient) ctx->SetFillPaint(gradient);
                    break;
                }
                default: ctx->SetFillPaint(fill.startColor); break;
            }
            ctx->FillPathPreserve();
        }

        if (isStroked && hasLine) {
            ctx->SetStrokePaint(line.color);
            ctx->SetStrokeWidth(line.GetWidthInPixels() * scale);
            ctx->SetLineCap(line.cap);
            ctx->SetLineJoin(line.join);
            ctx->SetMiterLimit(line.mitreLimit);
            ctx->StrokePathPreserve();
        }

        ctx->ClearPath();
        ctx->PopState();
        XARNode::Render(ctx, scale);
    }

    void XARPathNode::RenderPath(IRenderContext* ctx, float scale) const {
        ctx->ClearPath();
        for (const auto& cmd : commands) {
            switch (cmd.verb) {
                case XARPathVerb::MoveTo:
                    if (!cmd.points.empty()) { Point2Df pt = MillipointsToPixels(cmd.points[0], scale); ctx->MoveTo(pt.x, pt.y); }
                    break;
                case XARPathVerb::LineTo:
                    if (!cmd.points.empty()) { Point2Df pt = MillipointsToPixels(cmd.points[0], scale); ctx->LineTo(pt.x, pt.y); }
                    break;
                case XARPathVerb::BezierTo:
                    if (cmd.points.size() >= 3) {
                        Point2Df cp1 = MillipointsToPixels(cmd.points[0], scale);
                        Point2Df cp2 = MillipointsToPixels(cmd.points[1], scale);
                        Point2Df end = MillipointsToPixels(cmd.points[2], scale);
                        ctx->BezierCurveTo(cp1.x, cp1.y, cp2.x, cp2.y, end.x, end.y);
                    }
                    break;
                case XARPathVerb::ClosePath: ctx->ClosePath(); break;
            }
        }
    }

// ===== XAR RECTANGLE NODE =====

    void XARRectangleNode::Render(IRenderContext* ctx, float scale) {
        ctx->PushState();
        transform.ApplyToContext(ctx);
        Point2Df c = MillipointsToPixels(centre, scale);
        Point2Df major = MillipointsToPixels(majorAxis, scale);
        Point2Df minor = MillipointsToPixels(minorAxis, scale);
        float halfWidth = std::sqrt(major.x*major.x + major.y*major.y);
        float halfHeight = std::sqrt(minor.x*minor.x + minor.y*minor.y);
        float x = c.x - halfWidth, y = c.y - halfHeight, w = halfWidth*2, h = halfHeight*2;

        if (hasFill) {
            ctx->SetFillPaint(fill.startColor);
            if (cornerRadius > 0) ctx->FillRoundedRectangle(x, y, w, h, cornerRadius*scale);
            else ctx->FillRectangle(x, y, w, h);
        }
        if (hasLine) {
            ctx->SetStrokePaint(line.color);
            ctx->SetStrokeWidth(line.GetWidthInPixels() * scale);
            if (cornerRadius > 0) ctx->DrawRoundedRectangle(x, y, w, h, cornerRadius*scale);
            else ctx->DrawRectangle(x, y, w, h);
        }
        ctx->PopState();
        XARNode::Render(ctx, scale);
    }

// ===== XAR ELLIPSE NODE =====

    void XAREllipseNode::Render(IRenderContext* ctx, float scale) {
        ctx->PushState();
        transform.ApplyToContext(ctx);
        Point2Df c = MillipointsToPixels(centre, scale);
        Point2Df major = MillipointsToPixels(majorAxis, scale);
        Point2Df minor = MillipointsToPixels(minorAxis, scale);
        float radiusX = std::sqrt(major.x*major.x + major.y*major.y);
        float radiusY = std::sqrt(minor.x*minor.x + minor.y*minor.y);

        if (hasFill) { ctx->SetFillPaint(fill.startColor); ctx->FillEllipse(c.x, c.y, radiusX, radiusY); }
        if (hasLine) { ctx->SetStrokePaint(line.color); ctx->SetStrokeWidth(line.GetWidthInPixels()*scale); ctx->DrawEllipse(c.x, c.y, radiusX, radiusY); }
        ctx->PopState();
        XARNode::Render(ctx, scale);
    }

// ===== XAR POLYGON NODE =====

    std::vector<Point2Df> XARPolygonNode::GeneratePolygonPoints(float scale) const {
        std::vector<Point2Df> points;
        Point2Df c = MillipointsToPixels(centre, scale);
        Point2Df major = MillipointsToPixels(majorAxis, scale);
        Point2Df minor = MillipointsToPixels(minorAxis, scale);
        float radiusX = std::sqrt(major.x*major.x + major.y*major.y);
        float radiusY = std::sqrt(minor.x*minor.x + minor.y*minor.y);
        float angleStep = 2.0f * PI / numSides;
        float startAngle = std::atan2(major.y, major.x);

        for (int i = 0; i < numSides; ++i) {
            float angle = startAngle + i * angleStep;
            if (isStellated && stellationRadius > 0) {
                points.push_back(Point2Df(c.x + radiusX*std::cos(angle), c.y + radiusY*std::sin(angle)));
                float innerAngle = angle + angleStep*0.5f + stellationOffset;
                float innerRadius = stellationRadius * radiusX;
                points.push_back(Point2Df(c.x + innerRadius*std::cos(innerAngle), c.y + innerRadius*radiusY/radiusX*std::sin(innerAngle)));
            } else {
                points.push_back(Point2Df(c.x + radiusX*std::cos(angle), c.y + radiusY*std::sin(angle)));
            }
        }
        return points;
    }

    void XARPolygonNode::Render(IRenderContext* ctx, float scale) {
        ctx->PushState();
        transform.ApplyToContext(ctx);
        auto points = GeneratePolygonPoints(scale);
        if (!points.empty()) {
            ctx->ClearPath();
            ctx->MoveTo(points[0].x, points[0].y);
            for (size_t i = 1; i < points.size(); ++i) ctx->LineTo(points[i].x, points[i].y);
            ctx->ClosePath();
            if (hasFill) { ctx->SetFillPaint(fill.startColor); ctx->FillPathPreserve(); }
            if (hasLine) { ctx->SetStrokePaint(line.color); ctx->SetStrokeWidth(line.GetWidthInPixels()*scale); ctx->StrokePathPreserve(); }
            ctx->ClearPath();
        }
        ctx->PopState();
        XARNode::Render(ctx, scale);
    }

// ===== XAR GROUP NODE =====

    void XARGroupNode::Render(IRenderContext* ctx, float scale) {
        ctx->PushState();
        if (hasTransparency) ctx->SetAlpha(1.0f - static_cast<float>(transparency.startTransparency)/255.0f);
        XARNode::Render(ctx, scale);
        ctx->PopState();
    }

// ===== XAR TEXT NODE =====

    void XARTextNode::Render(IRenderContext* ctx, float scale) {
        if (text.empty()) { XARNode::Render(ctx, scale); return; }
        ctx->PushState();
        transform.ApplyToContext(ctx);
        FontWeight weight = textAttr.bold ? FontWeight::Bold : FontWeight::Normal;
        FontSlant slant = textAttr.italic ? FontSlant::Italic : FontSlant::Normal;
        ctx->SetFontFace(textAttr.fontName.empty() ? "Sans" : textAttr.fontName, weight, slant);
        ctx->SetFontSize(textAttr.GetFontSizeInPixels() * scale);
        Point2Df pos = MillipointsToPixels(position, scale);
        if (hasFill) { ctx->SetFillPaint(fill.startColor); ctx->FillText(text, pos.x, pos.y); }
        ctx->PopState();
        XARNode::Render(ctx, scale);
    }

// ===== XAR DOCUMENT =====

    XARDocument::XARDocument() { root = std::make_shared<XARNode>(); root->type = XARNodeType::Document; }
    XARDocument::~XARDocument() = default;

    bool XARDocument::LoadFromFile(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return false;
        size_t fileSize = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> buffer(fileSize);
        if (!file.read(reinterpret_cast<char*>(buffer.data()), fileSize)) return false;
        return LoadFromMemory(buffer.data(), buffer.size());
    }

    bool XARDocument::LoadFromMemory(const uint8_t* data, size_t size) {
        if (!data || size < 16) return false;
        size_t offset = 0;
        if (!ParseHeader(data, size, offset)) return false;
        if (!ParseRecords(data, size, offset)) return false;
        if (width == 0 || height == 0) { Rect2Df b = root->CalculateBounds(); width = b.width; height = b.height; }
        return true;
    }

    bool XARDocument::ParseHeader(const uint8_t* data, size_t size, size_t& offset) {
        if (offset + 8 > size) return false;
        uint32_t magic1 = ReadUInt32(data, offset);
        uint32_t magic2 = ReadUInt32(data, offset);
        return magic1 == XARConstants::MAGIC_XARA && magic2 == XARConstants::MAGIC_SIGNATURE;
    }

    bool XARDocument::ParseRecords(const uint8_t* data, size_t size, size_t& offset) {
        while (offset < size) {
            XARRecord record;
            if (!ReadRecord(data, size, offset, record)) break;
            currentSequenceNumber++;
            if (record.tag == XARTag::TAG_ENDOFFILE) break;
            ProcessRecord(record);
        }
        return true;
    }

    bool XARDocument::ReadRecord(const uint8_t* data, size_t size, size_t& offset, XARRecord& record) {
        if (offset + 8 > size) return false;
        record.tag = static_cast<XARTag>(ReadUInt32(data, offset));
        record.size = ReadUInt32(data, offset);
        if (offset + record.size > size) return false;
        if (record.size > 0) { record.data.assign(data + offset, data + offset + record.size); offset += record.size; }
        return true;
    }

    bool XARDocument::DecompressZlib(const uint8_t* compressedData, size_t compressedSize, std::vector<uint8_t>& decompressedData) {
        decompressedData.resize(compressedSize * 10);
        z_stream stream; memset(&stream, 0, sizeof(stream));
        stream.next_in = const_cast<uint8_t*>(compressedData);
        stream.avail_in = static_cast<uInt>(compressedSize);
        stream.next_out = decompressedData.data();
        stream.avail_out = static_cast<uInt>(decompressedData.size());
        if (inflateInit(&stream) != Z_OK) return false;
        int result = inflate(&stream, Z_FINISH);
        while (result == Z_BUF_ERROR || result == Z_OK) {
            decompressedData.resize(decompressedData.size() * 2);
            stream.next_out = decompressedData.data() + stream.total_out;
            stream.avail_out = static_cast<uInt>(decompressedData.size() - stream.total_out);
            result = inflate(&stream, Z_FINISH);
        }
        if (result != Z_STREAM_END) { inflateEnd(&stream); return false; }
        decompressedData.resize(stream.total_out);
        inflateEnd(&stream);
        return true;
    }

    void XARDocument::ProcessRecord(const XARRecord& record) {
        switch (record.tag) {
            case XARTag::TAG_DOWN: contextStack.push(currentContext); break;
            case XARTag::TAG_UP: if (!contextStack.empty()) { currentContext = contextStack.top(); contextStack.pop(); } PopNode(); break;
            case XARTag::TAG_SPREAD: case XARTag::TAG_SPREAD_PHASE2: ParseSpreadRecord(record); break;
            case XARTag::TAG_SPREADINFORMATION: ParseSpreadInfoRecord(record); break;
            case XARTag::TAG_LAYER: ParseLayerRecord(record); break;
            case XARTag::TAG_PATH: case XARTag::TAG_PATH_FILLED: case XARTag::TAG_PATH_STROKED: case XARTag::TAG_PATH_FILLED_STROKED:
                ParsePathRecord(record, record.tag != XARTag::TAG_PATH_STROKED, record.tag != XARTag::TAG_PATH_FILLED, false); break;
            case XARTag::TAG_PATH_RELATIVE: case XARTag::TAG_PATH_RELATIVE_FILLED: case XARTag::TAG_PATH_RELATIVE_STROKED: case XARTag::TAG_PATH_RELATIVE_FILLED_STROKED:
                ParsePathRecord(record, record.tag != XARTag::TAG_PATH_RELATIVE_STROKED, record.tag != XARTag::TAG_PATH_RELATIVE_FILLED, true); break;
            case XARTag::TAG_RECTANGLE: case XARTag::TAG_RECTANGLE_SIMPLE: case XARTag::TAG_RECTANGLE_SIMPLE_ROUNDED:
            case XARTag::TAG_RECTANGLE_COMPLEX: case XARTag::TAG_RECTANGLE_COMPLEX_ROUNDED: ParseRectangleRecord(record); break;
            case XARTag::TAG_ELLIPSE: case XARTag::TAG_ELLIPSE_SIMPLE: case XARTag::TAG_ELLIPSE_COMPLEX: ParseEllipseRecord(record); break;
            case XARTag::TAG_POLYGON: case XARTag::TAG_POLYGON_COMPLEX: case XARTag::TAG_POLYGON_COMPLEX_ROUNDED:
            case XARTag::TAG_POLYGON_COMPLEX_STELLATED: case XARTag::TAG_POLYGON_COMPLEX_ROUNDED_STELLATED: ParsePolygonRecord(record); break;
            case XARTag::TAG_GROUP: case XARTag::TAG_GROUPA: ParseGroupRecord(record); break;
            case XARTag::TAG_FLATFILL: case XARTag::TAG_FLATFILL_NONE: case XARTag::TAG_FLATFILL_BLACK: case XARTag::TAG_FLATFILL_WHITE:
            case XARTag::TAG_LINEARGRADIENTFILL: case XARTag::TAG_CIRCULARGRADIENTFILL: case XARTag::TAG_ELLIPTICALGRADIENTFILL:
            case XARTag::TAG_CONICALGRADIENTFILL: ParseFillRecord(record); break;
            case XARTag::TAG_LINEWIDTH: case XARTag::TAG_LINECOLOUR: case XARTag::TAG_LINECOLOUR_NONE:
            case XARTag::TAG_LINECOLOUR_BLACK: case XARTag::TAG_LINECOLOUR_WHITE: case XARTag::TAG_LINECAP:
            case XARTag::TAG_LINEJOIN: case XARTag::TAG_MITRELIMIT: ParseLineRecord(record); break;
            case XARTag::TAG_DEFINERGBCOLOUR: case XARTag::TAG_DEFINECOMPLEXCOLOUR: ParseColorRecord(record); break;
            case XARTag::TAG_TEXT_STRING: ParseTextRecord(record); break;
            default: break;
        }
    }

    void XARDocument::ParsePathRecord(const XARRecord& record, bool filled, bool stroked, bool relative) {
        if (record.data.empty()) return;
        auto pathNode = std::make_shared<XARPathNode>();
        pathNode->isFilled = filled; pathNode->isStroked = stroked;
        pathNode->fill = currentContext.fill; pathNode->line = currentContext.line;
        pathNode->hasFill = filled; pathNode->hasLine = stroked;
        const uint8_t* data = record.data.data(); size_t offset = 0;
        if (offset + 4 > record.data.size()) { CurrentNode()->AddChild(pathNode); return; }
        int32_t numCoords = ReadInt32(data, offset);
        std::vector<uint8_t> verbs;
        for (int32_t i = 0; i < numCoords && offset < record.data.size(); ++i) verbs.push_back(ReadByte(data, offset));
        while (offset % 4 != 0 && offset < record.data.size()) offset++;
        Point2Di lastCoord(0, 0); size_t verbIndex = 0;
        while (verbIndex < verbs.size() && offset + 8 <= record.data.size()) {
            uint8_t verb = verbs[verbIndex]; XARPathCommand cmd;
            if ((verb & 0x06) == 0x06) cmd.verb = XARPathVerb::MoveTo;
            else if ((verb & 0x06) == 0x02) cmd.verb = XARPathVerb::LineTo;
            else if (verb & 0x04) cmd.verb = XARPathVerb::BezierTo;
            else cmd.verb = XARPathVerb::MoveTo;
            Point2Di coord;
            if (relative) { int32_t dx = ReadInt32(data, offset), dy = ReadInt32(data, offset); coord = Point2Di(lastCoord.x + dx, lastCoord.y + dy); }
            else coord = ReadCoord(data, offset);
            cmd.points.push_back(coord); lastCoord = coord;
            if (cmd.verb == XARPathVerb::BezierTo) {
                for (int i = 0; i < 2 && offset + 8 <= record.data.size(); ++i) {
                    Point2Di cp;
                    if (relative) { int32_t dx = ReadInt32(data, offset), dy = ReadInt32(data, offset); cp = Point2Di(lastCoord.x + dx, lastCoord.y + dy); }
                    else cp = ReadCoord(data, offset);
                    cmd.points.push_back(cp); lastCoord = cp; verbIndex++;
                }
            }
            pathNode->commands.push_back(cmd);
            if (verb & 0x01) { XARPathCommand closeCmd; closeCmd.verb = XARPathVerb::ClosePath; pathNode->commands.push_back(closeCmd); }
            verbIndex++;
        }
        CurrentNode()->AddChild(pathNode);
    }

    void XARDocument::ParseRectangleRecord(const XARRecord& record) {
        auto rectNode = std::make_shared<XARRectangleNode>();
        rectNode->fill = currentContext.fill; rectNode->line = currentContext.line;
        rectNode->hasFill = true; rectNode->hasLine = true;
        const uint8_t* data = record.data.data(); size_t offset = 0;
        bool isSimple = (record.tag == XARTag::TAG_RECTANGLE_SIMPLE || record.tag == XARTag::TAG_RECTANGLE_SIMPLE_ROUNDED);
        if (isSimple && offset + 24 <= record.data.size()) {
            rectNode->centre = ReadCoord(data, offset);
            rectNode->majorAxis = ReadCoord(data, offset);
            rectNode->minorAxis = ReadCoord(data, offset);
        } else if (offset + 8 <= record.data.size()) {
            rectNode->centre = ReadCoord(data, offset);
            if (offset + 48 <= record.data.size()) rectNode->transform = ReadMatrix(data, offset);
            rectNode->majorAxis = Point2Di(72000, 0); rectNode->minorAxis = Point2Di(0, 72000);
        }
        rectNode->isSimple = isSimple;
        CurrentNode()->AddChild(rectNode);
    }

    void XARDocument::ParseEllipseRecord(const XARRecord& record) {
        auto ellipseNode = std::make_shared<XAREllipseNode>();
        ellipseNode->fill = currentContext.fill; ellipseNode->line = currentContext.line;
        ellipseNode->hasFill = true; ellipseNode->hasLine = true;
        const uint8_t* data = record.data.data(); size_t offset = 0;
        if (offset + 24 <= record.data.size()) {
            ellipseNode->centre = ReadCoord(data, offset);
            ellipseNode->majorAxis = ReadCoord(data, offset);
            ellipseNode->minorAxis = ReadCoord(data, offset);
        }
        CurrentNode()->AddChild(ellipseNode);
    }

    void XARDocument::ParsePolygonRecord(const XARRecord& record) {
        auto polyNode = std::make_shared<XARPolygonNode>();
        polyNode->fill = currentContext.fill; polyNode->line = currentContext.line;
        polyNode->hasFill = true; polyNode->hasLine = true;
        const uint8_t* data = record.data.data(); size_t offset = 0;
        if (offset + 4 <= record.data.size()) polyNode->numSides = ReadInt32(data, offset);
        if (offset + 24 <= record.data.size()) {
            polyNode->centre = ReadCoord(data, offset);
            polyNode->majorAxis = ReadCoord(data, offset);
            polyNode->minorAxis = ReadCoord(data, offset);
        }
        CurrentNode()->AddChild(polyNode);
    }

    void XARDocument::ParseGroupRecord(const XARRecord&) { PushNode(std::make_shared<XARGroupNode>()); }
    void XARDocument::ParseLayerRecord(const XARRecord&) { PushNode(std::make_shared<XARLayerNode>()); }
    void XARDocument::ParseSpreadRecord(const XARRecord&) { PushNode(std::make_shared<XARSpreadNode>()); }

    void XARDocument::ParseSpreadInfoRecord(const XARRecord& record) {
        const uint8_t* data = record.data.data(); size_t offset = 0;
        if (record.data.size() >= 16) {
            int32_t sw = ReadInt32(data, offset), sh = ReadInt32(data, offset);
            width = static_cast<float>(sw) * XARConstants::MILLIPOINTS_TO_PIXELS;
            height = static_cast<float>(sh) * XARConstants::MILLIPOINTS_TO_PIXELS;
        }
    }

    void XARDocument::ParseTextRecord(const XARRecord& record) {
        auto textNode = std::make_shared<XARTextNode>();
        textNode->fill = currentContext.fill; textNode->hasFill = true;
        const uint8_t* data = record.data.data(); size_t offset = 0;
        if (record.tag == XARTag::TAG_TEXT_STRING && offset < record.data.size()) textNode->text = ReadString(data, offset);
        CurrentNode()->AddChild(textNode);
    }

    void XARDocument::ParseColorRecord(const XARRecord& record) {
        XARColorDefinition colorDef; colorDef.sequenceNumber = currentSequenceNumber;
        const uint8_t* data = record.data.data(); size_t offset = 0;
        if (record.tag == XARTag::TAG_DEFINERGBCOLOUR && record.data.size() >= 3) colorDef.color = ReadColor(data, offset);
        colors[currentSequenceNumber] = std::move(colorDef);
    }

    void XARDocument::ParseFillRecord(const XARRecord& record) {
        const uint8_t* data = record.data.data(); size_t offset = 0;
        switch (record.tag) {
            case XARTag::TAG_FLATFILL:
                currentContext.fill.type = XARFillType::Flat;
                if (record.data.size() >= 4) { int32_t ref = ReadInt32(data, offset); auto* c = GetColor(ref); if (c) currentContext.fill.startColor = c->color; }
                break;
            case XARTag::TAG_FLATFILL_NONE: currentContext.fill.type = XARFillType::NoneFill; break;
            case XARTag::TAG_FLATFILL_BLACK: currentContext.fill.type = XARFillType::Flat; currentContext.fill.startColor = Color(0,0,0,255); break;
            case XARTag::TAG_FLATFILL_WHITE: currentContext.fill.type = XARFillType::Flat; currentContext.fill.startColor = Color(255,255,255,255); break;
            case XARTag::TAG_LINEARGRADIENTFILL:
                currentContext.fill.type = XARFillType::LinearGradient;
                if (record.data.size() >= 16) { currentContext.fill.startPoint = ReadCoord(data, offset); currentContext.fill.endPoint = ReadCoord(data, offset); }
                break;
            case XARTag::TAG_CIRCULARGRADIENTFILL: currentContext.fill.type = XARFillType::CircularGradient; break;
            case XARTag::TAG_ELLIPTICALGRADIENTFILL: currentContext.fill.type = XARFillType::EllipticalGradient; break;
            case XARTag::TAG_CONICALGRADIENTFILL: currentContext.fill.type = XARFillType::ConicalGradient; break;
            default: break;
        }
    }

    void XARDocument::ParseTransparencyRecord(const XARRecord& record) {
        const uint8_t* data = record.data.data(); size_t offset = 0;
        switch (record.tag) {
            case XARTag::TAG_FLATTRANSPARENTFILL:
                currentContext.transparency.type = XARTransparencyType::Flat;
                if (record.data.size() >= 1) currentContext.transparency.startTransparency = ReadByte(data, offset);
                break;
            default: break;
        }
    }

    void XARDocument::ParseLineRecord(const XARRecord& record) {
        const uint8_t* data = record.data.data(); size_t offset = 0;
        switch (record.tag) {
            case XARTag::TAG_LINEWIDTH: if (record.data.size() >= 4) currentContext.line.width = ReadInt32(data, offset); break;
            case XARTag::TAG_LINECOLOUR:
                if (record.data.size() >= 4) { int32_t ref = ReadInt32(data, offset); auto* c = GetColor(ref); if (c) currentContext.line.color = c->color; }
                break;
            case XARTag::TAG_LINECOLOUR_NONE: currentContext.line.color.a = 0; break;
            case XARTag::TAG_LINECOLOUR_BLACK: currentContext.line.color = Color(0,0,0,255); break;
            case XARTag::TAG_LINECOLOUR_WHITE: currentContext.line.color = Color(255,255,255,255); break;
            case XARTag::TAG_LINECAP:
                if (record.data.size() >= 1) { uint8_t v = ReadByte(data, offset); currentContext.line.cap = v==1 ? LineCap::Round : (v==2 ? LineCap::Square : LineCap::Butt); }
                break;
            case XARTag::TAG_LINEJOIN:
                if (record.data.size() >= 1) { uint8_t v = ReadByte(data, offset); currentContext.line.join = v==1 ? LineJoin::Round : (v==2 ? LineJoin::Bevel : LineJoin::Miter); }
                break;
            case XARTag::TAG_MITRELIMIT: if (record.data.size() >= 4) currentContext.line.mitreLimit = static_cast<float>(ReadInt32(data, offset)) / 65536.0f; break;
            default: break;
        }
    }

    void XARDocument::ParseFontDefRecord(const XARRecord&) { fonts[currentSequenceNumber] = XARFontDefinition(); }
    void XARDocument::ParseArrowDefRecord(const XARRecord&) { arrows[currentSequenceNumber] = XARArrowDefinition(); }
    void XARDocument::ParseBitmapDefRecord(const XARRecord&) { bitmaps[currentSequenceNumber] = XARBitmapDefinition(); }

// Utility functions
    uint8_t XARDocument::ReadByte(const uint8_t* data, size_t& offset) { return data[offset++]; }
    uint16_t XARDocument::ReadUInt16(const uint8_t* data, size_t& offset) { uint16_t v = data[offset] | (data[offset+1]<<8); offset += 2; return v; }
    int16_t XARDocument::ReadInt16(const uint8_t* data, size_t& offset) { return static_cast<int16_t>(ReadUInt16(data, offset)); }
    uint32_t XARDocument::ReadUInt32(const uint8_t* data, size_t& offset) { uint32_t v = data[offset] | (data[offset+1]<<8) | (data[offset+2]<<16) | (data[offset+3]<<24); offset += 4; return v; }
    int32_t XARDocument::ReadInt32(const uint8_t* data, size_t& offset) { return static_cast<int32_t>(ReadUInt32(data, offset)); }
    double XARDocument::ReadDouble(const uint8_t* data, size_t& offset) { double v; memcpy(&v, data+offset, 8); offset += 8; return v; }
    float XARDocument::ReadFloat(const uint8_t* data, size_t& offset) { float v; memcpy(&v, data+offset, 4); offset += 4; return v; }

    std::string XARDocument::ReadString(const uint8_t* data, size_t& offset) {
        std::string result;
        while (true) { uint16_t ch = ReadUInt16(data, offset); if (ch == 0) break; if (ch < 128) result += static_cast<char>(ch); }
        return result;
    }
    std::string XARDocument::ReadASCIIString(const uint8_t* data, size_t& offset) { std::string r; while (data[offset]) r += data[offset++]; offset++; return r; }
    Point2Di XARDocument::ReadCoord(const uint8_t* data, size_t& offset) { int32_t x = ReadInt32(data, offset), y = ReadInt32(data, offset); return Point2Di(x, y); }
    XARMatrix XARDocument::ReadMatrix(const uint8_t* data, size_t& offset) { XARMatrix m; m.a = ReadDouble(data, offset); m.b = ReadDouble(data, offset); m.c = ReadDouble(data, offset); m.d = ReadDouble(data, offset); m.e = ReadDouble(data, offset); m.f = ReadDouble(data, offset); return m; }
    Color XARDocument::ReadColor(const uint8_t* data, size_t& offset) { return Color(ReadByte(data, offset), ReadByte(data, offset), ReadByte(data, offset), 255); }

    void XARDocument::PushNode(XARNodePtr node) { CurrentNode()->AddChild(node); nodeStack.push(node); }
    void XARDocument::PopNode() { if (!nodeStack.empty()) nodeStack.pop(); }

    XARColorDefinition* XARDocument::GetColor(int32_t ref) {
        if (ref < 0) { static XARColorDefinition black, white; static bool init = false; if (!init) { black.color = Color(0,0,0,255); white.color = Color(255,255,255,255); init = true; } return ref == -1 ? &black : ref == -2 ? &white : nullptr; }
        auto it = colors.find(ref); return it != colors.end() ? &it->second : nullptr;
    }
    XARBitmapDefinition* XARDocument::GetBitmap(int32_t ref) { auto it = bitmaps.find(ref); return it != bitmaps.end() ? &it->second : nullptr; }
    XARFontDefinition* XARDocument::GetFont(int32_t ref) { auto it = fonts.find(ref); return it != fonts.end() ? &it->second : nullptr; }
    XARArrowDefinition* XARDocument::GetArrow(int32_t ref) { auto it = arrows.find(ref); return it != arrows.end() ? &it->second : nullptr; }

    void XARDocument::Render(IRenderContext* ctx, float scale) {
        if (!root) return;
        ctx->PushState();
        ctx->Translate(0, height);
        ctx->Scale(1.0f, -1.0f);
        root->Render(ctx, scale);
        ctx->PopState();
    }

// ===== XAR ELEMENT =====

    UltraCanvasXARElement::UltraCanvasXARElement(const std::string& identifier, long id, long x, long y, long w, long h) : UltraCanvasUIElement(identifier, id, x, y, w, h) {}
    bool UltraCanvasXARElement::LoadFromFile(const std::string& filepath) { document = std::make_unique<XARDocument>(); return document->LoadFromFile(filepath); }
    bool UltraCanvasXARElement::LoadFromMemory(const uint8_t* data, size_t size) { document = std::make_unique<XARDocument>(); return document->LoadFromMemory(data, size); }

    void UltraCanvasXARElement::Render(IRenderContext* ctx) {
        if (!document || !ctx) return;
        Rect2Di bounds = GetBounds(); ctx->PushState();
        float docWidth = document->GetWidth(), docHeight = document->GetHeight();
        if (docWidth > 0 && docHeight > 0) {
            float scaleX = static_cast<float>(bounds.width) / docWidth, scaleY = static_cast<float>(bounds.height) / docHeight;
            float renderScale = scale;
            if (preserveAspectRatio) {
                if (docWidth/docHeight > static_cast<float>(bounds.width)/bounds.height) {
                    renderScale = scaleX * scale; ctx->Translate(bounds.x, bounds.y + (bounds.height - docHeight*renderScale)/2);
                } else {
                    renderScale = scaleY * scale; ctx->Translate(bounds.x + (bounds.width - docWidth*renderScale)/2, bounds.y);
                }
            } else { ctx->Translate(bounds.x, bounds.y); ctx->Scale(scaleX*scale, scaleY*scale); renderScale = 1.0f; }
            document->Render(ctx, renderScale);
        }
        ctx->PopState();
    }

// ===== XAR PLUGIN =====

    bool UltraCanvasXARPlugin::CanHandle(const std::string& filePath) const { std::string ext = GetFileExtension(filePath); std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower); return ext == "xar" || ext == "web" || ext == "wix"; }
    bool UltraCanvasXARPlugin::CanHandle(const GraphicsFileInfo& fileInfo) const { return fileInfo.formatType == GraphicsFormatType::Vector && CanHandle(fileInfo.filename); }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasXARPlugin::LoadGraphics(const std::string& filePath) {
        auto element = std::make_shared<UltraCanvasXARElement>("XARElement", 0, 0, 0, 400, 400);
        if (element->LoadFromFile(filePath)) { if (element->GetDocument()) element->SetSize(static_cast<int>(element->GetDocument()->GetWidth()), static_cast<int>(element->GetDocument()->GetHeight())); return element; }
        return nullptr;
    }
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasXARPlugin::LoadGraphics(const GraphicsFileInfo& fileInfo) { return LoadGraphics(fileInfo.filename); }
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasXARPlugin::CreateGraphics(int, int, GraphicsFormatType) { return nullptr; }

    GraphicsFileInfo UltraCanvasXARPlugin::GetFileInfo(const std::string& filePath) {
        GraphicsFileInfo info(filePath); XARDocument doc;
        if (doc.LoadFromFile(filePath)) { info.width = static_cast<int>(doc.GetWidth()); info.height = static_cast<int>(doc.GetHeight()); }
        info.formatType = GraphicsFormatType::Vector; info.supportedManipulations = GetSupportedManipulations(); return info;
    }

    bool UltraCanvasXARPlugin::ValidateFile(const std::string& filePath) {
        std::ifstream file(filePath, std::ios::binary); if (!file.is_open()) return false;
        uint8_t header[8]; file.read(reinterpret_cast<char*>(header), 8); if (file.gcount() < 8) return false;
        uint32_t m1 = header[0] | (header[1]<<8) | (header[2]<<16) | (header[3]<<24);
        uint32_t m2 = header[4] | (header[5]<<8) | (header[6]<<16) | (header[7]<<24);
        return m1 == XARConstants::MAGIC_XARA && m2 == XARConstants::MAGIC_SIGNATURE;
    }

    std::string UltraCanvasXARPlugin::GetFileExtension(const std::string& filePath) const { size_t p = filePath.find_last_of('.'); return p == std::string::npos ? "" : filePath.substr(p + 1); }

} // namespace UltraCanvas