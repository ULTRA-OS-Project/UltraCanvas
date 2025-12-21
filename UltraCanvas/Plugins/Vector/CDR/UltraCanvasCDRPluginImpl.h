// Plugins/Vector/CDR/UltraCanvasCDRPlugin.h
// CorelDRAW CDR/CMX file format plugin using libcdr and librevenge
// Version: 1.1.0
// Last Modified: 2025-12-15
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_CDR_PLUGIN_IMPL_H
#define ULTRACANVAS_CDR_PLUGIN_IMPL_H

#include "UltraCanvasGraphicsPluginSystem.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"

#include <librevenge/librevenge.h>
#include <librevenge-stream/librevenge-stream.h>
#include <libcdr/libcdr.h>

#include <memory>
#include <string>
#include <vector>
#include <stack>
#include <map>
#include <functional>

namespace UltraCanvas {

// ===== ULTRACANVAS CDR PAINTER =====
// Implements librevenge::RVNGDrawingInterface to capture drawing commands
    class UltraCanvasCDRPainterImpl : public librevenge::RVNGDrawingInterface {
    public:
        UltraCanvasCDRPainterImpl();
        ~UltraCanvasCDRPainterImpl() override;

        // Get the parsed document
        std::shared_ptr<CDRDocument> GetDocument() { return document; }

        // ===== DOCUMENT INTERFACE =====
        void startDocument(const librevenge::RVNGPropertyList& propList) override;
        void endDocument() override;
        void setDocumentMetaData(const librevenge::RVNGPropertyList& propList) override;
        void defineEmbeddedFont(const librevenge::RVNGPropertyList& propList) override;

        // ===== PAGE INTERFACE =====
        void startPage(const librevenge::RVNGPropertyList& propList) override;
        void endPage() override;

        // ===== LAYER INTERFACE =====
        void startLayer(const librevenge::RVNGPropertyList& propList) override;
        void endLayer() override;
        void startEmbeddedGraphics(const librevenge::RVNGPropertyList& propList) override;
        void endEmbeddedGraphics() override;

        // ===== MASTER PAGE INTERFACE =====
        void startMasterPage(const librevenge::RVNGPropertyList& propList) override;
        void endMasterPage() override;

        // ===== STYLE DEFINITION INTERFACE =====
        void defineParagraphStyle(const librevenge::RVNGPropertyList& propList) override;
        void defineCharacterStyle(const librevenge::RVNGPropertyList& propList) override;

        // ===== STYLE INTERFACE =====
        void setStyle(const librevenge::RVNGPropertyList& propList) override;

        // ===== DRAWING INTERFACE =====
        void drawRectangle(const librevenge::RVNGPropertyList& propList) override;
        void drawEllipse(const librevenge::RVNGPropertyList& propList) override;
        void drawPolyline(const librevenge::RVNGPropertyList& propList) override;
        void drawPolygon(const librevenge::RVNGPropertyList& propList) override;
        void drawPath(const librevenge::RVNGPropertyList& propList) override;
        void drawConnector(const librevenge::RVNGPropertyList& propList) override;

        // ===== GRAPHIC OBJECT INTERFACE =====
        void drawGraphicObject(const librevenge::RVNGPropertyList& propList) override;

        // ===== GROUP INTERFACE =====
        void openGroup(const librevenge::RVNGPropertyList& propList) override;
        void closeGroup() override;

        // ===== TEXT INTERFACE =====
        void startTextObject(const librevenge::RVNGPropertyList& propList) override;
        void endTextObject() override;
        void openParagraph(const librevenge::RVNGPropertyList& propList) override;
        void closeParagraph() override;
        void openSpan(const librevenge::RVNGPropertyList& propList) override;
        void closeSpan() override;
        void openLink(const librevenge::RVNGPropertyList& propList) override;
        void closeLink() override;
        void insertText(const librevenge::RVNGString& text) override;
        void insertTab() override;
        void insertSpace() override;
        void insertLineBreak() override;
        void insertField(const librevenge::RVNGPropertyList& propList) override;

        // ===== TABLE INTERFACE =====
        void startTableObject(const librevenge::RVNGPropertyList& propList) override;
        void endTableObject() override;
        void openTableRow(const librevenge::RVNGPropertyList& propList) override;
        void closeTableRow() override;
        void openTableCell(const librevenge::RVNGPropertyList& propList) override;
        void closeTableCell() override;
        void insertCoveredTableCell(const librevenge::RVNGPropertyList& propList) override;

        // ===== ORDERED LIST INTERFACE =====
        void openOrderedListLevel(const librevenge::RVNGPropertyList& propList) override;
        void closeOrderedListLevel() override;
        void openUnorderedListLevel(const librevenge::RVNGPropertyList& propList) override;
        void closeUnorderedListLevel() override;
        void openListElement(const librevenge::RVNGPropertyList& propList) override;
        void closeListElement() override;

    private:
        std::shared_ptr<CDRDocument> document;
        CDRPage* currentPage = nullptr;
        std::stack<CDRStyleState> styleStack;

        // Named style storage maps
        std::map<std::string, CDRParagraphStyle> paragraphStyles;
        std::map<std::string, CDRCharacterStyle> characterStyles;

        // Text accumulation
        float textX = 0, textY = 0;
        float textWidth = 0, textHeight = 0;
        std::string accumulatedText;
        CDRStyleState textStyle;

        // Text style stack for nested spans
        std::stack<CDRStyleState> textStyleStack;

        // Current paragraph style being applied
        CDRParagraphStyle currentParagraphStyle;
        float currentLineY = 0;

        // Helper methods
        Color ParseColor(const librevenge::RVNGPropertyList& propList, const char* prefix);
        Color ParseColorValue(const std::string& colorStr);
        float ParseUnit(const librevenge::RVNGProperty* prop, float defaultValue = 0.0f);
        std::vector<Point2Df> ParsePathData(const librevenge::RVNGPropertyListVector* path);
        void ApplyStyleToContext(IRenderContext* ctx, const CDRStyleState& style);
        void FillAndStroke(IRenderContext* ctx, const CDRStyleState& style);

        std::string ParseGradientStyle(const librevenge::RVNGPropertyList& propList, const char* prefix);
        std::shared_ptr<IPaintPattern> CreateGradientPattern(IRenderContext* ctx,
                                                             const std::string& gradientId,
                                                             const Rect2Df& bounds);
        // Style lookup helpers
        CDRParagraphStyle* GetParagraphStyle(const std::string& name);
        CDRCharacterStyle* GetCharacterStyle(const std::string& name);
        void ApplyParagraphStyleToTextStyle(const CDRParagraphStyle& paraStyle);
        void ApplyCharacterStyleToTextStyle(const CDRCharacterStyle& charStyle);

        // Add draw command to current page
        void AddDrawCommand(std::function<void(IRenderContext*)> cmd);
    };
} // namespace UltraCanvas

#endif // ULTRACANVAS_CDR_PLUGIN_IMPL_H