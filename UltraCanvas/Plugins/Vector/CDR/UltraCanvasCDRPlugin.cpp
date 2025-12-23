// Plugins/Vector/CDR/UltraCanvasCDRPlugin.cpp
// CorelDRAW CDR/CMX file format plugin implementation
// Version: 1.1.0
// Last Modified: 2025-12-15
// Author: UltraCanvas Framework

#include "UltraCanvasCDRPlugin.h"
#include "UltraCanvasCDRPluginImpl.h"
#include "UltraCanvasUtils.h"
#include <iostream>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <regex>

namespace UltraCanvas {

// ===== UNIT CONVERSION CONSTANTS =====
// libcdr uses inches as base unit, convert to pixels (assuming 96 DPI)
    constexpr float INCHES_TO_PIXELS = 96.0f;
    constexpr float POINTS_TO_PIXELS = 96.0f / 72.0f;
    constexpr float CM_TO_PIXELS = 96.0f / 2.54f;
    constexpr float MM_TO_PIXELS = 96.0f / 25.4f;

// ===== HELPER FUNCTIONS =====
    static float ConvertToPixels(double value, const char* unit) {
        if (!unit) return static_cast<float>(value * INCHES_TO_PIXELS);

        std::string unitStr(unit);
        if (unitStr == "in" || unitStr == "inch") {
            return static_cast<float>(value * INCHES_TO_PIXELS);
        } else if (unitStr == "pt" || unitStr == "point") {
            return static_cast<float>(value * POINTS_TO_PIXELS);
        } else if (unitStr == "cm") {
            return static_cast<float>(value * CM_TO_PIXELS);
        } else if (unitStr == "mm") {
            return static_cast<float>(value * MM_TO_PIXELS);
        } else if (unitStr == "px" || unitStr == "pixel") {
            return static_cast<float>(value);
        }
        return static_cast<float>(value * INCHES_TO_PIXELS); // Default to inches
    }

// ===== ULTRACANVAS CDR PAINTER IMPLEMENTATION =====

    UltraCanvasCDRPainterImpl::UltraCanvasCDRPainterImpl() {
        document = std::make_shared<CDRDocument>();

        // Initialize with default style
        CDRStyleState defaultStyle;
        styleStack.push(defaultStyle);
    }

    UltraCanvasCDRPainterImpl::~UltraCanvasCDRPainterImpl() = default;

    float UltraCanvasCDRPainterImpl::ParseUnit(const librevenge::RVNGProperty* prop, float defaultValue) {
        if (!prop) return defaultValue;

        // RVNGProperty stores values in inches by default
        return static_cast<float>(prop->getDouble() * INCHES_TO_PIXELS);
    }

    Color UltraCanvasCDRPainterImpl::ParseColorValue(const std::string& colorStr) {
        Color color = Colors::Black;

        // Parse #RRGGBB or #RRGGBBAA format
        if (colorStr.length() >= 7 && colorStr[0] == '#') {
            unsigned int r = 0, g = 0, b = 0, a = 255;

            if (colorStr.length() >= 9) {
                sscanf(colorStr.c_str(), "#%02x%02x%02x%02x", &r, &g, &b, &a);
            } else {
                sscanf(colorStr.c_str(), "#%02x%02x%02x", &r, &g, &b);
            }

            color = Color(r, g, b, a);
        }
            // Handle rgb() format
        else if (colorStr.find("rgb(") == 0) {
            unsigned int r = 0, g = 0, b = 0;
            sscanf(colorStr.c_str(), "rgb(%u,%u,%u)", &r, &g, &b);
            color = Color(r, g, b, 255);
        }
            // Handle named colors (basic set)
        else if (colorStr == "black") color = Colors::Black;
        else if (colorStr == "white") color = Colors::White;
        else if (colorStr == "red") color = Colors::Red;
        else if (colorStr == "green") color = Colors::Green;
        else if (colorStr == "blue") color = Colors::Blue;
        else if (colorStr == "transparent" || colorStr == "none") color = Colors::Transparent;

        return color;
    }

    Color UltraCanvasCDRPainterImpl::ParseColor(const librevenge::RVNGPropertyList& propList,
                                            const char* prefix) {
        Color color = Colors::Black;

        std::string colorKey = std::string(prefix) + "-color";
        std::string opacityKey = std::string(prefix) + "-opacity";

        const librevenge::RVNGProperty* colorProp = propList[colorKey.c_str()];
        if (colorProp) {
            std::string colorStr = colorProp->getStr().cstr();
            color = ParseColorValue(colorStr);
        }

        // Apply opacity
        const librevenge::RVNGProperty* opacityProp = propList[opacityKey.c_str()];
        if (opacityProp) {
            float opacity = static_cast<float>(opacityProp->getDouble());
            color.a = static_cast<uint8_t>(color.a * opacity);
        }

        return color;
    }

    void UltraCanvasCDRPainterImpl::AddDrawCommand(std::function<void(IRenderContext*)> cmd) {
        if (currentPage) {
            currentPage->drawCommands.push_back(std::move(cmd));
        }
    }

    void UltraCanvasCDRPainterImpl::ApplyStyleToContext(IRenderContext* ctx, const CDRStyleState& style) {
        // Apply fill - handled in FillAndStroke with bounds
        if (style.hasFill && style.fillGradientId.empty()) {
            Color fillColor = style.fillColor;
            fillColor.a = static_cast<uint8_t>(fillColor.a * style.fillOpacity * style.opacity);
            ctx->SetFillPaint(fillColor);
        }

        // Apply stroke - handled in FillAndStroke with bounds for gradients
        if (style.hasStroke && style.strokeGradientId.empty()) {
            Color strokeColor = style.strokeColor;
            strokeColor.a = static_cast<uint8_t>(strokeColor.a * style.strokeOpacity * style.opacity);
            ctx->SetStrokePaint(strokeColor);
        }

        // Apply stroke properties
        if (style.hasStroke) {
            ctx->SetStrokeWidth(style.strokeWidth);
            ctx->SetLineCap(style.lineCap);
            ctx->SetLineJoin(style.lineJoin);
            ctx->SetMiterLimit(style.miterLimit);

            if (!style.dashPattern.dashes.empty()) {
                ctx->SetLineDash(style.dashPattern);
            }
        }

        // Apply global opacity
        ctx->SetAlpha(style.opacity);
    }

    void UltraCanvasCDRPainterImpl::FillAndStroke(IRenderContext* ctx, const CDRStyleState& style) {
        // Get path bounds for gradient calculation
        float bx = 0, by = 0, bw = 0, bh = 0;
        ctx->GetPathExtents(bx, by, bw, bh);
        Rect2Df bounds(bx, by, bw, bh);

        if (style.hasFill) {
            if (!style.fillGradientId.empty()) {
                // Apply gradient fill
                auto gradientPattern = CreateGradientPattern(ctx, style.fillGradientId, bounds);
                if (gradientPattern) {
                    ctx->SetFillPaint(gradientPattern);
                }
            }
            ctx->FillPathPreserve();
        }

        if (style.hasStroke) {
            if (!style.strokeGradientId.empty()) {
                // Apply gradient stroke
                auto gradientPattern = CreateGradientPattern(ctx, style.strokeGradientId, bounds);
                if (gradientPattern) {
                    ctx->SetStrokePaint(gradientPattern);
                }
            }
            ctx->StrokePathPreserve();
        }

        ctx->ClearPath();
    }


    std::shared_ptr<IPaintPattern> UltraCanvasCDRPainterImpl::CreateGradientPattern(
            IRenderContext* ctx,
            const std::string& gradientId,
            const Rect2Df& bounds) {

        auto it = document->gradients.find(gradientId);
        if (it == document->gradients.end() || it->second.stops.empty()) {
            return nullptr;
        }

        const CDRGradient& gradient = it->second;

        if (gradient.type == CDRGradientType::Linear) {
            float x1, y1, x2, y2;

            if (gradient.useObjectBounds) {
                // Transform normalized coordinates to object bounds
                x1 = bounds.x + gradient.x1 * bounds.width;
                y1 = bounds.y + gradient.y1 * bounds.height;
                x2 = bounds.x + gradient.x2 * bounds.width;
                y2 = bounds.y + gradient.y2 * bounds.height;
            } else {
                x1 = gradient.x1;
                y1 = gradient.y1;
                x2 = gradient.x2;
                y2 = gradient.y2;
            }

            return ctx->CreateLinearGradientPattern(x1, y1, x2, y2, gradient.stops);

        } else if (gradient.type == CDRGradientType::Radial) {
            float cx, cy, r;
            float fx, fy;

            if (gradient.useObjectBounds) {
                cx = bounds.x + gradient.cx * bounds.width;
                cy = bounds.y + gradient.cy * bounds.height;
                fx = bounds.x + gradient.fx * bounds.width;
                fy = bounds.y + gradient.fy * bounds.height;
                r = gradient.radius * std::max(bounds.width, bounds.height);
            } else {
                cx = gradient.cx;
                cy = gradient.cy;
                fx = gradient.fx;
                fy = gradient.fy;
                r = gradient.radius;
            }

            // CreateRadialGradientPattern(fx, fy, r1, cx, cy, r2, stops)
            // Inner circle at focal point with radius 0, outer at center with full radius
            return ctx->CreateRadialGradientPattern(fx, fy, 0.0f, cx, cy, r, gradient.stops);

        } else if (gradient.type == CDRGradientType::Conical) {
            // Conical gradients are not directly supported - approximate with radial
            float cx = bounds.x + gradient.cx * bounds.width;
            float cy = bounds.y + gradient.cy * bounds.height;
            float r = gradient.radius * std::max(bounds.width, bounds.height);

            return ctx->CreateRadialGradientPattern(cx, cy, 0.0f, cx, cy, r, gradient.stops);
        }

        return nullptr;
    }

// ===== STYLE LOOKUP HELPERS =====

    CDRParagraphStyle* UltraCanvasCDRPainterImpl::GetParagraphStyle(const std::string& name) {
        auto it = paragraphStyles.find(name);
        if (it != paragraphStyles.end()) {
            return &it->second;
        }
        return nullptr;
    }

    CDRCharacterStyle* UltraCanvasCDRPainterImpl::GetCharacterStyle(const std::string& name) {
        auto it = characterStyles.find(name);
        if (it != characterStyles.end()) {
            return &it->second;
        }
        return nullptr;
    }

    void UltraCanvasCDRPainterImpl::ApplyParagraphStyleToTextStyle(const CDRParagraphStyle& paraStyle) {
        // Apply paragraph background color if set
        if (paraStyle.hasBackground) {
            // Could be used for text background highlighting
        }

        // Store current paragraph style for text rendering
        currentParagraphStyle = paraStyle;
    }

    void UltraCanvasCDRPainterImpl::ApplyCharacterStyleToTextStyle(const CDRCharacterStyle& charStyle) {
        textStyle.fontFamily = charStyle.fontFamily;
        textStyle.fontSize = charStyle.fontSize;
        textStyle.fontWeight = charStyle.fontWeight;
        textStyle.fontSlant = charStyle.fontSlant;
        textStyle.fillColor = charStyle.textColor;
        textStyle.hasFill = true;
    }

// ===== STYLE DEFINITION INTERFACE =====

    void UltraCanvasCDRPainterImpl::defineParagraphStyle(const librevenge::RVNGPropertyList& propList) {
        const librevenge::RVNGProperty* nameProp = propList["librevenge:name"];
        if (!nameProp) return;

        std::string styleName = nameProp->getStr().cstr();

        CDRParagraphStyle paraStyle;
        paraStyle.name = styleName;

        // Text alignment
        const librevenge::RVNGProperty* alignProp = propList["fo:text-align"];
        if (alignProp) {
            std::string align = alignProp->getStr().cstr();
            if (align == "center") paraStyle.textAlign = CDRParagraphStyle::Alignment::Center;
            else if (align == "right" || align == "end") paraStyle.textAlign = CDRParagraphStyle::Alignment::Right;
            else if (align == "justify") paraStyle.textAlign = CDRParagraphStyle::Alignment::Justify;
            else paraStyle.textAlign = CDRParagraphStyle::Alignment::Left;
        }

        // Line height / spacing
        const librevenge::RVNGProperty* lineHeightProp = propList["fo:line-height"];
        if (lineHeightProp) {
            paraStyle.lineHeight = static_cast<float>(lineHeightProp->getDouble());
        }

        // Margins
        const librevenge::RVNGProperty* marginTopProp = propList["fo:margin-top"];
        if (marginTopProp) {
            paraStyle.marginTop = ParseUnit(marginTopProp, 0.0f);
        }

        const librevenge::RVNGProperty* marginBottomProp = propList["fo:margin-bottom"];
        if (marginBottomProp) {
            paraStyle.marginBottom = ParseUnit(marginBottomProp, 0.0f);
        }

        const librevenge::RVNGProperty* marginLeftProp = propList["fo:margin-left"];
        if (marginLeftProp) {
            paraStyle.marginLeft = ParseUnit(marginLeftProp, 0.0f);
        }

        const librevenge::RVNGProperty* marginRightProp = propList["fo:margin-right"];
        if (marginRightProp) {
            paraStyle.marginRight = ParseUnit(marginRightProp, 0.0f);
        }

        // First line indent
        const librevenge::RVNGProperty* textIndentProp = propList["fo:text-indent"];
        if (textIndentProp) {
            paraStyle.textIndent = ParseUnit(textIndentProp, 0.0f);
        }

        // Background color
        const librevenge::RVNGProperty* bgColorProp = propList["fo:background-color"];
        if (bgColorProp) {
            std::string bgColorStr = bgColorProp->getStr().cstr();
            if (bgColorStr != "transparent" && bgColorStr != "none") {
                paraStyle.backgroundColor = ParseColorValue(bgColorStr);
                paraStyle.hasBackground = true;
            }
        }

        // Store the paragraph style in the map
        paragraphStyles[styleName] = paraStyle;
    }

    void UltraCanvasCDRPainterImpl::defineCharacterStyle(const librevenge::RVNGPropertyList& propList) {
        const librevenge::RVNGProperty* nameProp = propList["librevenge:name"];
        if (!nameProp) return;

        std::string styleName = nameProp->getStr().cstr();

        CDRCharacterStyle charStyle;
        charStyle.name = styleName;

        // Font family
        const librevenge::RVNGProperty* fontNameProp = propList["style:font-name"];
        if (fontNameProp) {
            charStyle.fontFamily = fontNameProp->getStr().cstr();
        }

        // Font size
        const librevenge::RVNGProperty* fontSizeProp = propList["fo:font-size"];
        if (fontSizeProp) {
            charStyle.fontSize = ParseUnit(fontSizeProp, 12.0f);
        }

        // Font weight (bold)
        const librevenge::RVNGProperty* fontWeightProp = propList["fo:font-weight"];
        if (fontWeightProp) {
            std::string weight = fontWeightProp->getStr().cstr();
            if (weight == "bold" || weight == "700" || weight == "800" || weight == "900") {
                charStyle.fontWeight = FontWeight::Bold;
            } else {
                charStyle.fontWeight = FontWeight::Normal;
            }
        }

        // Font style (italic)
        const librevenge::RVNGProperty* fontStyleProp = propList["fo:font-style"];
        if (fontStyleProp) {
            std::string style = fontStyleProp->getStr().cstr();
            if (style == "italic" || style == "oblique") {
                charStyle.fontSlant = FontSlant::Italic;
            } else {
                charStyle.fontSlant = FontSlant::Normal;
            }
        }

        // Text color
        const librevenge::RVNGProperty* colorProp = propList["fo:color"];
        if (colorProp) {
            std::string colorStr = colorProp->getStr().cstr();
            charStyle.textColor = ParseColorValue(colorStr);
        }

        // Text decoration - underline
        const librevenge::RVNGProperty* underlineProp = propList["style:text-underline-style"];
        if (underlineProp) {
            std::string underlineStyle = underlineProp->getStr().cstr();
            charStyle.underline = (underlineStyle != "none");
        }

        // Text decoration - strikethrough
        const librevenge::RVNGProperty* strikethroughProp = propList["style:text-line-through-style"];
        if (strikethroughProp) {
            std::string strikeStyle = strikethroughProp->getStr().cstr();
            charStyle.strikethrough = (strikeStyle != "none");
        }

        // Letter spacing
        const librevenge::RVNGProperty* letterSpacingProp = propList["fo:letter-spacing"];
        if (letterSpacingProp) {
            charStyle.letterSpacing = ParseUnit(letterSpacingProp, 0.0f);
        }

        // Text transform
        const librevenge::RVNGProperty* textTransformProp = propList["fo:text-transform"];
        if (textTransformProp) {
            std::string transform = textTransformProp->getStr().cstr();
            if (transform == "uppercase") charStyle.textTransform = CDRCharacterStyle::TextTransform::Uppercase;
            else if (transform == "lowercase") charStyle.textTransform = CDRCharacterStyle::TextTransform::Lowercase;
            else if (transform == "capitalize") charStyle.textTransform = CDRCharacterStyle::TextTransform::Capitalize;
            else charStyle.textTransform = CDRCharacterStyle::TextTransform::TransformNone;
        }

        // Store the character style in the map
        characterStyles[styleName] = charStyle;
    }

// ===== DOCUMENT INTERFACE =====

    void UltraCanvasCDRPainterImpl::startDocument(const librevenge::RVNGPropertyList& propList) {
        document->pages.clear();
        document->gradients.clear();
        document->images.clear();
        paragraphStyles.clear();
        characterStyles.clear();
    }

    void UltraCanvasCDRPainterImpl::endDocument() {
        // Document parsing complete
    }

    void UltraCanvasCDRPainterImpl::setDocumentMetaData(const librevenge::RVNGPropertyList& propList) {
        const librevenge::RVNGProperty* titleProp = propList["dc:title"];
        if (titleProp) {
            document->title = titleProp->getStr().cstr();
        }

        const librevenge::RVNGProperty* authorProp = propList["dc:creator"];
        if (authorProp) {
            document->author = authorProp->getStr().cstr();
        }
    }

    void UltraCanvasCDRPainterImpl::defineEmbeddedFont(const librevenge::RVNGPropertyList& propList) {
        // Store embedded font definition for later use
        // Currently not implemented - would need font embedding support
    }

// ===== PAGE INTERFACE =====

    void UltraCanvasCDRPainterImpl::startPage(const librevenge::RVNGPropertyList& propList) {
        CDRPage page;

        // Parse page dimensions
        const librevenge::RVNGProperty* widthProp = propList["svg:width"];
        const librevenge::RVNGProperty* heightProp = propList["svg:height"];

        page.width = ParseUnit(widthProp, 800.0f);
        page.height = ParseUnit(heightProp, 600.0f);

        // Update document dimensions from first page
        if (document->pages.empty()) {
            document->documentWidth = page.width;
            document->documentHeight = page.height;
        }

        document->pages.push_back(page);
        currentPage = &document->pages.back();
    }

    void UltraCanvasCDRPainterImpl::endPage() {
        currentPage = nullptr;
    }

// ===== LAYER INTERFACE =====

    void UltraCanvasCDRPainterImpl::startLayer(const librevenge::RVNGPropertyList& propList) {
        // Layers in CDR can have opacity - push state
        AddDrawCommand([](IRenderContext* ctx) {
            ctx->PushState();
        });

        const librevenge::RVNGProperty* opacityProp = propList["draw:opacity"];
        if (opacityProp) {
            float opacity = static_cast<float>(opacityProp->getDouble());
            AddDrawCommand([opacity](IRenderContext* ctx) {
                ctx->SetAlpha(opacity);
            });
        }
    }

    void UltraCanvasCDRPainterImpl::endLayer() {
        AddDrawCommand([](IRenderContext* ctx) {
            ctx->PopState();
        });
    }

    void UltraCanvasCDRPainterImpl::startEmbeddedGraphics(const librevenge::RVNGPropertyList& propList) {
        // Similar to layer
        AddDrawCommand([](IRenderContext* ctx) {
            ctx->PushState();
        });
    }

    void UltraCanvasCDRPainterImpl::endEmbeddedGraphics() {
        AddDrawCommand([](IRenderContext* ctx) {
            ctx->PopState();
        });
    }

// ===== MASTER PAGE INTERFACE =====

    void UltraCanvasCDRPainterImpl::startMasterPage(const librevenge::RVNGPropertyList& propList) {
        // Master pages are template pages - currently render inline
    }

    void UltraCanvasCDRPainterImpl::endMasterPage() {
        // End of master page
    }

// ===== STYLE INTERFACE =====

// ===== STYLE INTERFACE =====

    void UltraCanvasCDRPainterImpl::setStyle(const librevenge::RVNGPropertyList& propList) {
        CDRStyleState style = styleStack.top();

        // Parse fill properties
        const librevenge::RVNGProperty* fillProp = propList["draw:fill"];
        if (fillProp) {
            std::string fillType = fillProp->getStr().cstr();
            if (fillType == "none") {
                style.hasFill = false;
                style.fillGradientId.clear();
            } else if (fillType == "solid") {
                style.hasFill = true;
                style.fillColor = ParseColor(propList, "draw:fill");
                style.fillGradientId.clear();
            } else if (fillType == "gradient") {
                style.hasFill = true;
                style.fillGradientId = ParseGradientStyle(propList, "draw:fill");
            }
        }

        // Parse stroke properties
        const librevenge::RVNGProperty* strokeProp = propList["draw:stroke"];
        if (strokeProp) {
            std::string strokeType = strokeProp->getStr().cstr();
            if (strokeType == "none") {
                style.hasStroke = false;
                style.strokeGradientId.clear();
            } else if (strokeType == "solid" || strokeType == "dash") {
                style.hasStroke = true;
                style.strokeColor = ParseColor(propList, "svg:stroke");
                style.strokeGradientId.clear();
            } else if (strokeType == "gradient") {
                style.hasStroke = true;
                style.strokeGradientId = ParseGradientStyle(propList, "svg:stroke");
            }
        }

        // Stroke width
        const librevenge::RVNGProperty* strokeWidthProp = propList["svg:stroke-width"];
        if (strokeWidthProp) {
            style.strokeWidth = ParseUnit(strokeWidthProp, 1.0f);
        }

        // Stroke line cap
        const librevenge::RVNGProperty* lineCapProp = propList["svg:stroke-linecap"];
        if (lineCapProp) {
            std::string lineCap = lineCapProp->getStr().cstr();
            if (lineCap == "butt") style.lineCap = LineCap::Butt;
            else if (lineCap == "round") style.lineCap = LineCap::Round;
            else if (lineCap == "square") style.lineCap = LineCap::Square;
        }

        // Stroke line join
        const librevenge::RVNGProperty* lineJoinProp = propList["svg:stroke-linejoin"];
        if (lineJoinProp) {
            std::string lineJoin = lineJoinProp->getStr().cstr();
            if (lineJoin == "miter") style.lineJoin = LineJoin::Miter;
            else if (lineJoin == "round") style.lineJoin = LineJoin::Round;
            else if (lineJoin == "bevel") style.lineJoin = LineJoin::Bevel;
        }

        // Miter limit
        const librevenge::RVNGProperty* miterProp = propList["svg:stroke-miterlimit"];
        if (miterProp) {
            style.miterLimit = static_cast<float>(miterProp->getDouble());
        }

        // Dash pattern
        const librevenge::RVNGProperty* dashProp = propList["svg:stroke-dasharray"];
        if (dashProp) {
            std::string dashStr = dashProp->getStr().cstr();
            if (dashStr != "none") {
                style.dashPattern.dashes.clear();
                std::istringstream iss(dashStr);
                std::string token;
                while (std::getline(iss, token, ',')) {
                    try {
                        style.dashPattern.dashes.push_back(std::stof(token));
                    } catch (...) {}
                }
            }
        }

        // Opacity
        const librevenge::RVNGProperty* opacityProp = propList["draw:opacity"];
        if (opacityProp) {
            style.opacity = static_cast<float>(opacityProp->getDouble());
        }

        // Fill opacity
        const librevenge::RVNGProperty* fillOpacityProp = propList["draw:fill-opacity"];
        if (fillOpacityProp) {
            style.fillOpacity = static_cast<float>(fillOpacityProp->getDouble());
        }

        // Stroke opacity
        const librevenge::RVNGProperty* strokeOpacityProp = propList["svg:stroke-opacity"];
        if (strokeOpacityProp) {
            style.strokeOpacity = static_cast<float>(strokeOpacityProp->getDouble());
        }

        // Update current style
        styleStack.pop();
        styleStack.push(style);
    }


    std::string UltraCanvasCDRPainterImpl::ParseGradientStyle(const librevenge::RVNGPropertyList& propList,
                                                              const char* prefix) {
        CDRGradient gradient;

        // Parse gradient type
        std::string gradientTypeKey = std::string(prefix) + "-gradient-type";
        const librevenge::RVNGProperty* typeProp = propList["draw:fill-gradient-name"];
        if (!typeProp) {
            typeProp = propList["draw:gradient-style"];
        }

        if (typeProp) {
            std::string typeStr = typeProp->getStr().cstr();
            if (typeStr == "linear") {
                gradient.type = CDRGradientType::Linear;
            } else if (typeStr == "radial" || typeStr == "ellipsoid") {
                gradient.type = CDRGradientType::Radial;
            } else if (typeStr == "axial") {
                gradient.type = CDRGradientType::Linear;
            } else if (typeStr == "square" || typeStr == "rectangular") {
                gradient.type = CDRGradientType::Radial;
            } else if (typeStr == "conical") {
                gradient.type = CDRGradientType::Conical;
            }
        }

        // Parse gradient angle
        const librevenge::RVNGProperty* angleProp = propList["draw:gradient-angle"];
        if (!angleProp) angleProp = propList["draw:angle"];
        if (angleProp) {
            gradient.angle = static_cast<float>(angleProp->getDouble());
        }

        // Parse gradient center for radial gradients
        const librevenge::RVNGProperty* cxProp = propList["draw:gradient-cx"];
        const librevenge::RVNGProperty* cyProp = propList["draw:gradient-cy"];
        if (cxProp) {
            gradient.cx = static_cast<float>(cxProp->getDouble());
            if (gradient.cx > 1.0f) gradient.cx /= 100.0f; // Convert percentage
        }
        if (cyProp) {
            gradient.cy = static_cast<float>(cyProp->getDouble());
            if (gradient.cy > 1.0f) gradient.cy /= 100.0f;
        }
        gradient.fx = gradient.cx;
        gradient.fy = gradient.cy;

        // Parse gradient border/radius
        const librevenge::RVNGProperty* borderProp = propList["draw:gradient-border"];
        if (borderProp) {
            float border = static_cast<float>(borderProp->getDouble());
            if (border > 1.0f) border /= 100.0f;
            gradient.radius = 0.5f * (1.0f - border);
        }

        // Calculate linear gradient coordinates from angle
        if (gradient.type == CDRGradientType::Linear) {
            float radians = gradient.angle * static_cast<float>(M_PI) / 180.0f;
            gradient.x1 = 0.5f - 0.5f * std::cos(radians);
            gradient.y1 = 0.5f - 0.5f * std::sin(radians);
            gradient.x2 = 0.5f + 0.5f * std::cos(radians);
            gradient.y2 = 0.5f + 0.5f * std::sin(radians);
        }

        // Parse gradient stops from start/end colors
        const librevenge::RVNGProperty* startColorProp = propList["draw:gradient-start-color"];
        const librevenge::RVNGProperty* endColorProp = propList["draw:gradient-end-color"];
        const librevenge::RVNGProperty* startIntensityProp = propList["draw:gradient-start-intensity"];
        const librevenge::RVNGProperty* endIntensityProp = propList["draw:gradient-end-intensity"];

        Color startColor = Colors::White;
        Color endColor = Colors::Black;
        float startIntensity = 1.0f;
        float endIntensity = 1.0f;

        if (startColorProp) {
            startColor = ParseColorValue(startColorProp->getStr().cstr());
        }
        if (endColorProp) {
            endColor = ParseColorValue(endColorProp->getStr().cstr());
        }
        if (startIntensityProp) {
            startIntensity = static_cast<float>(startIntensityProp->getDouble());
            if (startIntensity > 1.0f) startIntensity /= 100.0f;
        }
        if (endIntensityProp) {
            endIntensity = static_cast<float>(endIntensityProp->getDouble());
            if (endIntensity > 1.0f) endIntensity /= 100.0f;
        }

        // Apply intensity to colors
        startColor.r = static_cast<uint8_t>(startColor.r * startIntensity);
        startColor.g = static_cast<uint8_t>(startColor.g * startIntensity);
        startColor.b = static_cast<uint8_t>(startColor.b * startIntensity);

        endColor.r = static_cast<uint8_t>(endColor.r * endIntensity);
        endColor.g = static_cast<uint8_t>(endColor.g * endIntensity);
        endColor.b = static_cast<uint8_t>(endColor.b * endIntensity);

        gradient.stops.push_back(GradientStop(0.0f, startColor));
        gradient.stops.push_back(GradientStop(1.0f, endColor));

        // Parse additional gradient stops if available (svg:stop elements)
        const librevenge::RVNGPropertyListVector* stopsList = propList.child("svg:linearGradient");
        if (!stopsList) {
            stopsList = propList.child("svg:radialGradient");
        }
        if (stopsList && stopsList->count() > 0) {
            gradient.stops.clear();
            for (unsigned long i = 0; i < stopsList->count(); ++i) {
                const librevenge::RVNGPropertyList& stop = (*stopsList)[i];
                float offset = 0.0f;
                Color stopColor = Colors::Black;

                const librevenge::RVNGProperty* offsetProp = stop["svg:offset"];
                if (offsetProp) {
                    offset = static_cast<float>(offsetProp->getDouble());
                    if (offset > 1.0f) offset /= 100.0f;
                }

                const librevenge::RVNGProperty* stopColorProp = stop["svg:stop-color"];
                if (stopColorProp) {
                    stopColor = ParseColorValue(stopColorProp->getStr().cstr());
                }

                const librevenge::RVNGProperty* stopOpacityProp = stop["svg:stop-opacity"];
                if (stopOpacityProp) {
                    stopColor.a = static_cast<uint8_t>(255.0f * stopOpacityProp->getDouble());
                }

                gradient.stops.push_back(GradientStop(offset, stopColor));
            }
        }

        // Sort stops by position
        std::sort(gradient.stops.begin(), gradient.stops.end(),
                  [](const GradientStop& a, const GradientStop& b) {
                      return a.position < b.position;
                  });

        // Generate unique gradient ID and store
        std::string gradientId = "gradient_" + std::to_string(document->gradients.size());
        document->gradients[gradientId] = gradient;

        return gradientId;
    }

// ===== DRAWING INTERFACE =====

    void UltraCanvasCDRPainterImpl::drawRectangle(const librevenge::RVNGPropertyList& propList) {
        float x = ParseUnit(propList["svg:x"]);
        float y = ParseUnit(propList["svg:y"]);
        float width = ParseUnit(propList["svg:width"]);
        float height = ParseUnit(propList["svg:height"]);
        float rx = ParseUnit(propList["svg:rx"]);
        float ry = ParseUnit(propList["svg:ry"]);

        CDRStyleState style = styleStack.top();

        AddDrawCommand([=,this](IRenderContext* ctx) {
            ctx->PushState();
            ApplyStyleToContext(ctx, style);

            ctx->ClearPath();
            if (rx > 0 || ry > 0) {
                ctx->RoundedRect(x, y, width, height, std::max(rx, ry));
            } else {
                ctx->Rect(x, y, width, height);
            }

            FillAndStroke(ctx, style);
            ctx->PopState();
        });
    }

    void UltraCanvasCDRPainterImpl::drawEllipse(const librevenge::RVNGPropertyList& propList) {
        float cx = ParseUnit(propList["svg:cx"]);
        float cy = ParseUnit(propList["svg:cy"]);
        float rx = ParseUnit(propList["svg:rx"]);
        float ry = ParseUnit(propList["svg:ry"]);

        CDRStyleState style = styleStack.top();

        AddDrawCommand([=,this](IRenderContext* ctx) {
            ctx->PushState();
            ApplyStyleToContext(ctx, style);

            ctx->ClearPath();
            ctx->Ellipse(cx, cy, rx, ry, 0, 0, 2 * M_PI);

            FillAndStroke(ctx, style);
            ctx->PopState();
        });
    }

    void UltraCanvasCDRPainterImpl::drawPolyline(const librevenge::RVNGPropertyList& propList) {
        const librevenge::RVNGPropertyListVector* points = propList.child("svg:points");
        if (!points || points->count() < 2) return;

        std::vector<Point2Df> pointList;
        for (unsigned long i = 0; i < points->count(); ++i) {
            const librevenge::RVNGPropertyList& point = (*points)[i];
            float x = ParseUnit(point["svg:x"]);
            float y = ParseUnit(point["svg:y"]);
            pointList.push_back({x, y});
        }

        CDRStyleState style = styleStack.top();

        AddDrawCommand([=, this](IRenderContext* ctx) {
            ctx->PushState();
            ApplyStyleToContext(ctx, style);

            ctx->ClearPath();
            if (!pointList.empty()) {
                ctx->MoveTo(pointList[0].x, pointList[0].y);
                for (size_t i = 1; i < pointList.size(); ++i) {
                    ctx->LineTo(pointList[i].x, pointList[i].y);
                }
            }

            // Polylines typically only stroke
            if (style.hasStroke) {
                ctx->Stroke();
            }
            ctx->ClearPath();
            ctx->PopState();
        });
    }

    void UltraCanvasCDRPainterImpl::drawPolygon(const librevenge::RVNGPropertyList& propList) {
        const librevenge::RVNGPropertyListVector* points = propList.child("svg:points");
        if (!points || points->count() < 3) return;

        std::vector<Point2Df> pointList;
        for (unsigned long i = 0; i < points->count(); ++i) {
            const librevenge::RVNGPropertyList& point = (*points)[i];
            float x = ParseUnit(point["svg:x"]);
            float y = ParseUnit(point["svg:y"]);
            pointList.push_back({x, y});
        }

        CDRStyleState style = styleStack.top();

        AddDrawCommand([=, this](IRenderContext* ctx) {
            ctx->PushState();
            ApplyStyleToContext(ctx, style);

            ctx->ClearPath();
            if (!pointList.empty()) {
                ctx->MoveTo(pointList[0].x, pointList[0].y);
                for (size_t i = 1; i < pointList.size(); ++i) {
                    ctx->LineTo(pointList[i].x, pointList[i].y);
                }
                ctx->ClosePath();
            }

            FillAndStroke(ctx, style);
            ctx->PopState();
        });
    }

    void UltraCanvasCDRPainterImpl::drawPath(const librevenge::RVNGPropertyList& propList) {
        const librevenge::RVNGPropertyListVector* pathData = propList.child("svg:d");
        if (!pathData) return;

        // Parse path commands
        std::vector<std::tuple<char, std::vector<float>>> commands;

        for (unsigned long i = 0; i < pathData->count(); ++i) {
            const librevenge::RVNGPropertyList& segment = (*pathData)[i];
            const librevenge::RVNGProperty* typeProp = segment["librevenge:path-action"];
            if (!typeProp) continue;

            char type = typeProp->getStr().cstr()[0];
            std::vector<float> params;

            // Extract coordinates based on command type
            switch (type) {
                case 'M': case 'm':
                case 'L': case 'l':
                case 'T': case 't':
                    params.push_back(ParseUnit(segment["svg:x"]));
                    params.push_back(ParseUnit(segment["svg:y"]));
                    break;

                case 'H': case 'h':
                    params.push_back(ParseUnit(segment["svg:x"]));
                    break;

                case 'V': case 'v':
                    params.push_back(ParseUnit(segment["svg:y"]));
                    break;

                case 'C': case 'c':
                    params.push_back(ParseUnit(segment["svg:x1"]));
                    params.push_back(ParseUnit(segment["svg:y1"]));
                    params.push_back(ParseUnit(segment["svg:x2"]));
                    params.push_back(ParseUnit(segment["svg:y2"]));
                    params.push_back(ParseUnit(segment["svg:x"]));
                    params.push_back(ParseUnit(segment["svg:y"]));
                    break;

                case 'S': case 's':
                    params.push_back(ParseUnit(segment["svg:x2"]));
                    params.push_back(ParseUnit(segment["svg:y2"]));
                    params.push_back(ParseUnit(segment["svg:x"]));
                    params.push_back(ParseUnit(segment["svg:y"]));
                    break;

                case 'Q': case 'q':
                    params.push_back(ParseUnit(segment["svg:x1"]));
                    params.push_back(ParseUnit(segment["svg:y1"]));
                    params.push_back(ParseUnit(segment["svg:x"]));
                    params.push_back(ParseUnit(segment["svg:y"]));
                    break;

                case 'A': case 'a':
                    params.push_back(ParseUnit(segment["svg:rx"]));
                    params.push_back(ParseUnit(segment["svg:ry"]));
                    {
                        const librevenge::RVNGProperty* rotProp = segment["librevenge:rotate"];
                        params.push_back(rotProp ? static_cast<float>(rotProp->getDouble()) : 0.0f);
                    }
                    {
                        const librevenge::RVNGProperty* largeProp = segment["librevenge:large-arc"];
                        params.push_back(largeProp ? static_cast<float>(largeProp->getInt()) : 0.0f);
                    }
                    {
                        const librevenge::RVNGProperty* sweepProp = segment["librevenge:sweep"];
                        params.push_back(sweepProp ? static_cast<float>(sweepProp->getInt()) : 0.0f);
                    }
                    params.push_back(ParseUnit(segment["svg:x"]));
                    params.push_back(ParseUnit(segment["svg:y"]));
                    break;

                case 'Z': case 'z':
                    // No parameters
                    break;
            }

            commands.push_back({type, params});
        }

        CDRStyleState style = styleStack.top();

        AddDrawCommand([=, this](IRenderContext* ctx) {
            ctx->PushState();
            ApplyStyleToContext(ctx, style);

            ctx->ClearPath();

            float currentX = 0, currentY = 0;
            float startX = 0, startY = 0;
            float lastCpX = 0, lastCpY = 0;

            for (const auto& [type, params] : commands) {
                switch (type) {
                    case 'M':
                        if (params.size() >= 2) {
                            currentX = params[0];
                            currentY = params[1];
                            startX = currentX;
                            startY = currentY;
                            ctx->MoveTo(currentX, currentY);
                        }
                        break;

                    case 'm':
                        if (params.size() >= 2) {
                            currentX += params[0];
                            currentY += params[1];
                            startX = currentX;
                            startY = currentY;
                            ctx->MoveTo(currentX, currentY);
                        }
                        break;

                    case 'L':
                        if (params.size() >= 2) {
                            currentX = params[0];
                            currentY = params[1];
                            ctx->LineTo(currentX, currentY);
                        }
                        break;

                    case 'l':
                        if (params.size() >= 2) {
                            currentX += params[0];
                            currentY += params[1];
                            ctx->LineTo(currentX, currentY);
                        }
                        break;

                    case 'H':
                        if (params.size() >= 1) {
                            currentX = params[0];
                            ctx->LineTo(currentX, currentY);
                        }
                        break;

                    case 'h':
                        if (params.size() >= 1) {
                            currentX += params[0];
                            ctx->LineTo(currentX, currentY);
                        }
                        break;

                    case 'V':
                        if (params.size() >= 1) {
                            currentY = params[0];
                            ctx->LineTo(currentX, currentY);
                        }
                        break;

                    case 'v':
                        if (params.size() >= 1) {
                            currentY += params[0];
                            ctx->LineTo(currentX, currentY);
                        }
                        break;

                    case 'C':
                        if (params.size() >= 6) {
                            ctx->BezierCurveTo(params[0], params[1],
                                               params[2], params[3],
                                               params[4], params[5]);
                            lastCpX = params[2];
                            lastCpY = params[3];
                            currentX = params[4];
                            currentY = params[5];
                        }
                        break;

                    case 'c':
                        if (params.size() >= 6) {
                            ctx->BezierCurveTo(currentX + params[0], currentY + params[1],
                                               currentX + params[2], currentY + params[3],
                                               currentX + params[4], currentY + params[5]);
                            lastCpX = currentX + params[2];
                            lastCpY = currentY + params[3];
                            currentX += params[4];
                            currentY += params[5];
                        }
                        break;

                    case 'S':
                        if (params.size() >= 4) {
                            float cp1x = 2 * currentX - lastCpX;
                            float cp1y = 2 * currentY - lastCpY;
                            ctx->BezierCurveTo(cp1x, cp1y,
                                               params[0], params[1],
                                               params[2], params[3]);
                            lastCpX = params[0];
                            lastCpY = params[1];
                            currentX = params[2];
                            currentY = params[3];
                        }
                        break;

                    case 's':
                        if (params.size() >= 4) {
                            float cp1x = 2 * currentX - lastCpX;
                            float cp1y = 2 * currentY - lastCpY;
                            ctx->BezierCurveTo(cp1x, cp1y,
                                               currentX + params[0], currentY + params[1],
                                               currentX + params[2], currentY + params[3]);
                            lastCpX = currentX + params[0];
                            lastCpY = currentY + params[1];
                            currentX += params[2];
                            currentY += params[3];
                        }
                        break;

                    case 'Q':
                        if (params.size() >= 4) {
                            ctx->QuadraticCurveTo(params[0], params[1],
                                                  params[2], params[3]);
                            lastCpX = params[0];
                            lastCpY = params[1];
                            currentX = params[2];
                            currentY = params[3];
                        }
                        break;

                    case 'q':
                        if (params.size() >= 4) {
                            ctx->QuadraticCurveTo(currentX + params[0], currentY + params[1],
                                                  currentX + params[2], currentY + params[3]);
                            lastCpX = currentX + params[0];
                            lastCpY = currentY + params[1];
                            currentX += params[2];
                            currentY += params[3];
                        }
                        break;

                    case 'A': case 'a':
                        // Arc commands - convert to bezier approximation
                        if (params.size() >= 7) {
                            float endX = (type == 'A') ? params[5] : currentX + params[5];
                            float endY = (type == 'A') ? params[6] : currentY + params[6];

                            // Simplified: draw line to endpoint
                            // TODO: Implement proper arc-to-bezier conversion
                            ctx->LineTo(endX, endY);

                            currentX = endX;
                            currentY = endY;
                        }
                        break;

                    case 'Z': case 'z':
                        ctx->ClosePath();
                        currentX = startX;
                        currentY = startY;
                        break;
                }
            }

            FillAndStroke(ctx, style);
            ctx->PopState();
        });
    }

    void UltraCanvasCDRPainterImpl::drawConnector(const librevenge::RVNGPropertyList& propList) {
        // Connectors are special path objects - delegate to drawPath
        drawPath(propList);
    }

// ===== GRAPHIC OBJECT INTERFACE =====

    void UltraCanvasCDRPainterImpl::drawGraphicObject(const librevenge::RVNGPropertyList& propList) {
        const librevenge::RVNGProperty* dataProp = propList["office:binary-data"];
        if (!dataProp) return;

        float x = ParseUnit(propList["svg:x"]);
        float y = ParseUnit(propList["svg:y"]);
        float width = ParseUnit(propList["svg:width"]);
        float height = ParseUnit(propList["svg:height"]);

        // Get image data (base64 encoded)
        std::string base64Data = dataProp->getStr().cstr();

        // Get MIME type if available
        std::string mimeType = "image/png"; // Default
        const librevenge::RVNGProperty* mimeProp = propList["librevenge:mime-type"];
        if (mimeProp) {
            mimeType = mimeProp->getStr().cstr();
        }

        // Parse transform properties
        bool mirrorHorizontal = false;
        bool mirrorVertical = false;
        float rotation = 0.0f;

        const librevenge::RVNGProperty* mirrorHProp = propList["draw:mirror-horizontal"];
        if (mirrorHProp) {
            std::string val = mirrorHProp->getStr().cstr();
            mirrorHorizontal = (val == "true" || val == "1");
        }

        const librevenge::RVNGProperty* mirrorVProp = propList["draw:mirror-vertical"];
        if (mirrorVProp) {
            std::string val = mirrorVProp->getStr().cstr();
            mirrorVertical = (val == "true" || val == "1");
        }

        const librevenge::RVNGProperty* rotateProp = propList["librevenge:rotate"];
        if (rotateProp) {
            rotation = static_cast<float>(rotateProp->getDouble());
        }

        // Decode base64 data
        std::vector<uint8_t> imageData = Base64Decode(base64Data);

        if (imageData.empty()) {
            // Fallback to placeholder if decode fails
            AddDrawCommand([=](IRenderContext* ctx) {
                ctx->PushState();
                ctx->SetFillPaint(Color(200, 200, 200, 255));
                ctx->SetStrokePaint(Color(100, 100, 100, 255));
                ctx->SetStrokeWidth(1.0f);
                ctx->ClearPath();
                ctx->Rect(x, y, width, height);
                ctx->FillPathPreserve();
                ctx->StrokePathPreserve();
                ctx->ClearPath();
                ctx->SetTextPaint(Colors::Black);
                ctx->SetFontFace("Sans", FontWeight::Normal, FontSlant::Normal);
                ctx->SetFontSize(10.0f);
                ctx->DrawText("IMG?", Point2Di(static_cast<int>(x + width / 2 - 12),
                                               static_cast<int>(y + height / 2 + 4)));
                ctx->PopState();
            });
            return;
        }

        // Store image data in document for later rendering
        std::string imageId = "embedded_img_" + std::to_string(document->images.size());
        document->images[imageId] = imageData;

        // Capture parameters for draw command
        float imgX = x;
        float imgY = y;
        float imgW = width;
        float imgH = height;
        std::vector<uint8_t> capturedImageData = imageData;
        bool capturedMirrorH = mirrorHorizontal;
        bool capturedMirrorV = mirrorVertical;
        float capturedRotation = rotation;

        AddDrawCommand([=, this](IRenderContext* ctx) {
            ctx->PushState();

            // Calculate center point for transforms
            float centerX = imgX + imgW / 2.0f;
            float centerY = imgY + imgH / 2.0f;

            // Apply transforms around center point
            bool hasTransform = (capturedRotation != 0.0f || capturedMirrorH || capturedMirrorV);

            if (hasTransform) {
                // Translate to center
                ctx->Translate(centerX, centerY);

                // Apply rotation (convert degrees to radians)
                if (capturedRotation != 0.0f) {
                    float radians = capturedRotation * static_cast<float>(M_PI) / 180.0f;
                    ctx->Rotate(radians);
                }

                // Apply mirroring via scale
                float scaleX = capturedMirrorH ? -1.0f : 1.0f;
                float scaleY = capturedMirrorV ? -1.0f : 1.0f;
                if (capturedMirrorH || capturedMirrorV) {
                    ctx->Scale(scaleX, scaleY);
                }

                // Translate back (draw centered at origin)
                ctx->Translate(-imgW / 2.0f, -imgH / 2.0f);
            }

            // Load image from memory and draw it
            auto image = UCImage::GetFromMemory(capturedImageData.data(),
                                                capturedImageData.size());
            if (image && image->IsValid()) {
                if (hasTransform) {
                    // Draw at origin since we translated
                    ctx->DrawImage(*image, 0, 0, imgW, imgH, ImageFitMode::Fill);
                } else {
                    ctx->DrawImage(*image, imgX, imgY, imgW, imgH, ImageFitMode::Fill);
                }
            } else {
                // Fallback placeholder if image loading fails
                float drawX = hasTransform ? 0 : imgX;
                float drawY = hasTransform ? 0 : imgY;

                ctx->SetFillPaint(Color(220, 220, 220, 255));
                ctx->SetStrokePaint(Color(150, 150, 150, 255));
                ctx->SetStrokeWidth(1.0f);
                ctx->ClearPath();
                ctx->Rect(drawX, drawY, imgW, imgH);
                ctx->FillPathPreserve();
                ctx->StrokePathPreserve();
                ctx->ClearPath();

                // Draw X pattern to indicate broken image
                ctx->SetStrokePaint(Color(180, 180, 180, 255));
                ctx->DrawLine(drawX, drawY, drawX + imgW, drawY + imgH);
                ctx->DrawLine(drawX + imgW, drawY, drawX, drawY + imgH);
            }

            ctx->PopState();
        });
    }

    // ===== GROUP INTERFACE =====

    void UltraCanvasCDRPainterImpl::openGroup(const librevenge::RVNGPropertyList& propList) {
        // Push style state
        styleStack.push(styleStack.top());

        AddDrawCommand([](IRenderContext* ctx) {
            ctx->PushState();
        });

        // Apply group transform if present
        const librevenge::RVNGProperty* transformProp = propList["draw:transform"];
        if (transformProp) {
            std::string transformStr = transformProp->getStr().cstr();

            // Parse transform string (e.g., "translate(x,y) rotate(angle) scale(sx,sy)")
            std::regex translateRe(R"(translate\s*\(\s*([\d.\-+e]+)\s*,?\s*([\d.\-+e]*)\s*\))");
            std::regex rotateRe(R"(rotate\s*\(\s*([\d.\-+e]+)\s*\))");
            std::regex scaleRe(R"(scale\s*\(\s*([\d.\-+e]+)\s*,?\s*([\d.\-+e]*)\s*\))");
            std::regex matrixRe(R"(matrix\s*\(\s*([\d.\-+e]+)\s*,\s*([\d.\-+e]+)\s*,\s*([\d.\-+e]+)\s*,\s*([\d.\-+e]+)\s*,\s*([\d.\-+e]+)\s*,\s*([\d.\-+e]+)\s*\))");

            std::smatch match;

            // Matrix transform
            if (std::regex_search(transformStr, match, matrixRe)) {
                float a = std::stof(match[1].str());
                float b = std::stof(match[2].str());
                float c = std::stof(match[3].str());
                float d = std::stof(match[4].str());
                float e = std::stof(match[5].str()) * INCHES_TO_PIXELS;
                float f = std::stof(match[6].str()) * INCHES_TO_PIXELS;

                AddDrawCommand([=](IRenderContext* ctx) {
                    ctx->Transform(a, b, c, d, e, f);
                });
            }

            // Translate
            if (std::regex_search(transformStr, match, translateRe)) {
                float tx = std::stof(match[1].str()) * INCHES_TO_PIXELS;
                float ty = match[2].length() > 0 ? std::stof(match[2].str()) * INCHES_TO_PIXELS : 0;

                AddDrawCommand([=](IRenderContext* ctx) {
                    ctx->Translate(tx, ty);
                });
            }

            // Rotate
            if (std::regex_search(transformStr, match, rotateRe)) {
                float angle = std::stof(match[1].str()) * M_PI / 180.0f;

                AddDrawCommand([=](IRenderContext* ctx) {
                    ctx->Rotate(angle);
                });
            }

            // Scale
            if (std::regex_search(transformStr, match, scaleRe)) {
                float sx = std::stof(match[1].str());
                float sy = match[2].length() > 0 ? std::stof(match[2].str()) : sx;

                AddDrawCommand([=](IRenderContext* ctx) {
                    ctx->Scale(sx, sy);
                });
            }
        }
    }

    void UltraCanvasCDRPainterImpl::closeGroup() {
        if (styleStack.size() > 1) {
            styleStack.pop();
        }

        AddDrawCommand([](IRenderContext* ctx) {
            ctx->PopState();
        });
    }

// ===== TEXT INTERFACE =====

    void UltraCanvasCDRPainterImpl::startTextObject(const librevenge::RVNGPropertyList& propList) {
        textX = ParseUnit(propList["svg:x"]);
        textY = ParseUnit(propList["svg:y"]);
        textWidth = ParseUnit(propList["svg:width"]);
        textHeight = ParseUnit(propList["svg:height"]);
        accumulatedText.clear();
        textStyle = styleStack.top();

        // Clear the text style stack and push initial state
        while (!textStyleStack.empty()) {
            textStyleStack.pop();
        }
        textStyleStack.push(textStyle);

        // Reset paragraph style
        currentParagraphStyle = CDRParagraphStyle();
        currentLineY = 0;
    }

    void UltraCanvasCDRPainterImpl::endTextObject() {
        if (accumulatedText.empty()) return;

        std::string text = accumulatedText;
        float x = textX;
        float y = textY;
        CDRStyleState style = textStyle;
        CDRParagraphStyle paraStyle = currentParagraphStyle;

        AddDrawCommand([=](IRenderContext* ctx) {
            ctx->PushState();

            ctx->SetFontFace(style.fontFamily, style.fontWeight, style.fontSlant);
            ctx->SetFontSize(style.fontSize);

            if (style.hasFill) {
                ctx->SetTextPaint(style.fillColor);
            }

            // Apply text alignment offset
            float drawX = x + paraStyle.marginLeft + paraStyle.textIndent;
            float drawY = y + style.fontSize + paraStyle.marginTop;

            ctx->DrawText(text, Point2Di(static_cast<int>(drawX), static_cast<int>(drawY)));

            ctx->PopState();
        });

        accumulatedText.clear();
    }

    void UltraCanvasCDRPainterImpl::openParagraph(const librevenge::RVNGPropertyList& propList) {
        // Check for named paragraph style reference
        const librevenge::RVNGProperty* styleNameProp = propList["text:style-name"];
        if (styleNameProp) {
            std::string styleName = styleNameProp->getStr().cstr();
            CDRParagraphStyle* namedStyle = GetParagraphStyle(styleName);
            if (namedStyle) {
                currentParagraphStyle = *namedStyle;
                ApplyParagraphStyleToTextStyle(currentParagraphStyle);
            }
        }

        // Override with inline properties if present
        const librevenge::RVNGProperty* alignProp = propList["fo:text-align"];
        if (alignProp) {
            std::string align = alignProp->getStr().cstr();
            if (align == "center") currentParagraphStyle.textAlign = CDRParagraphStyle::Alignment::Center;
            else if (align == "right" || align == "end") currentParagraphStyle.textAlign = CDRParagraphStyle::Alignment::Right;
            else if (align == "justify") currentParagraphStyle.textAlign = CDRParagraphStyle::Alignment::Justify;
            else currentParagraphStyle.textAlign = CDRParagraphStyle::Alignment::Left;
        }

        const librevenge::RVNGProperty* lineHeightProp = propList["fo:line-height"];
        if (lineHeightProp) {
            currentParagraphStyle.lineHeight = static_cast<float>(lineHeightProp->getDouble());
        }

        const librevenge::RVNGProperty* marginTopProp = propList["fo:margin-top"];
        if (marginTopProp) {
            currentParagraphStyle.marginTop = ParseUnit(marginTopProp, 0.0f);
        }

        const librevenge::RVNGProperty* marginBottomProp = propList["fo:margin-bottom"];
        if (marginBottomProp) {
            currentParagraphStyle.marginBottom = ParseUnit(marginBottomProp, 0.0f);
        }

        const librevenge::RVNGProperty* marginLeftProp = propList["fo:margin-left"];
        if (marginLeftProp) {
            currentParagraphStyle.marginLeft = ParseUnit(marginLeftProp, 0.0f);
        }

        const librevenge::RVNGProperty* textIndentProp = propList["fo:text-indent"];
        if (textIndentProp) {
            currentParagraphStyle.textIndent = ParseUnit(textIndentProp, 0.0f);
        }
    }

    void UltraCanvasCDRPainterImpl::closeParagraph() {
        accumulatedText += "\n";
        currentLineY += textStyle.fontSize * currentParagraphStyle.lineHeight;
    }

    void UltraCanvasCDRPainterImpl::openSpan(const librevenge::RVNGPropertyList& propList) {
        // Push current text style onto stack before modifications
        textStyleStack.push(textStyle);

        // Check for named character style reference
        const librevenge::RVNGProperty* styleNameProp = propList["text:style-name"];
        if (styleNameProp) {
            std::string styleName = styleNameProp->getStr().cstr();
            CDRCharacterStyle* namedStyle = GetCharacterStyle(styleName);
            if (namedStyle) {
                ApplyCharacterStyleToTextStyle(*namedStyle);
            }
        }

        // Override with inline properties if present
        const librevenge::RVNGProperty* fontNameProp = propList["style:font-name"];
        if (fontNameProp) {
            textStyle.fontFamily = fontNameProp->getStr().cstr();
        }

        const librevenge::RVNGProperty* fontSizeProp = propList["fo:font-size"];
        if (fontSizeProp) {
            textStyle.fontSize = ParseUnit(fontSizeProp, 12.0f);
        }

        const librevenge::RVNGProperty* fontWeightProp = propList["fo:font-weight"];
        if (fontWeightProp) {
            std::string weight = fontWeightProp->getStr().cstr();
            textStyle.fontWeight = (weight == "bold" || weight == "700" || weight == "800" || weight == "900")
                                   ? FontWeight::Bold : FontWeight::Normal;
        }

        const librevenge::RVNGProperty* fontStyleProp = propList["fo:font-style"];
        if (fontStyleProp) {
            std::string style = fontStyleProp->getStr().cstr();
            textStyle.fontSlant = (style == "italic" || style == "oblique")
                                  ? FontSlant::Italic : FontSlant::Normal;
        }

        const librevenge::RVNGProperty* colorProp = propList["fo:color"];
        if (colorProp) {
            std::string colorStr = colorProp->getStr().cstr();
            textStyle.fillColor = ParseColorValue(colorStr);
            textStyle.hasFill = true;
        }
    }

    void UltraCanvasCDRPainterImpl::closeSpan() {
        // Restore previous text style from stack
        if (!textStyleStack.empty()) {
            textStyle = textStyleStack.top();
            textStyleStack.pop();
        }
    }

    void UltraCanvasCDRPainterImpl::openLink(const librevenge::RVNGPropertyList& propList) {
        // Hyperlinks - currently ignored but could store href for later
    }

    void UltraCanvasCDRPainterImpl::closeLink() {
    }

    void UltraCanvasCDRPainterImpl::insertText(const librevenge::RVNGString& text) {
        accumulatedText += text.cstr();
    }

    void UltraCanvasCDRPainterImpl::insertTab() {
        accumulatedText += "\t";
    }

    void UltraCanvasCDRPainterImpl::insertSpace() {
        accumulatedText += " ";
    }

    void UltraCanvasCDRPainterImpl::insertLineBreak() {
        accumulatedText += "\n";
    }

    void UltraCanvasCDRPainterImpl::insertField(const librevenge::RVNGPropertyList& propList) {
        // Fields like page numbers - insert placeholder
        const librevenge::RVNGProperty* typeProp = propList["librevenge:field-type"];
        if (typeProp) {
            std::string fieldType = typeProp->getStr().cstr();
            if (fieldType == "page-number") {
                accumulatedText += "[PAGE]";
            } else if (fieldType == "page-count") {
                accumulatedText += "[PAGES]";
            } else if (fieldType == "date") {
                accumulatedText += "[DATE]";
            } else if (fieldType == "time") {
                accumulatedText += "[TIME]";
            } else {
                accumulatedText += "[FIELD]";
            }
        } else {
            accumulatedText += "[FIELD]";
        }
    }

// ===== TABLE INTERFACE =====

    void UltraCanvasCDRPainterImpl::startTableObject(const librevenge::RVNGPropertyList& propList) {
        // Tables - currently simplified
    }

    void UltraCanvasCDRPainterImpl::endTableObject() {
    }

    void UltraCanvasCDRPainterImpl::openTableRow(const librevenge::RVNGPropertyList& propList) {
    }

    void UltraCanvasCDRPainterImpl::closeTableRow() {
    }

    void UltraCanvasCDRPainterImpl::openTableCell(const librevenge::RVNGPropertyList& propList) {
    }

    void UltraCanvasCDRPainterImpl::closeTableCell() {
    }

    void UltraCanvasCDRPainterImpl::insertCoveredTableCell(const librevenge::RVNGPropertyList& propList) {
    }

// ===== LIST INTERFACE =====

    void UltraCanvasCDRPainterImpl::openOrderedListLevel(const librevenge::RVNGPropertyList& propList) {
    }

    void UltraCanvasCDRPainterImpl::closeOrderedListLevel() {
    }

    void UltraCanvasCDRPainterImpl::openUnorderedListLevel(const librevenge::RVNGPropertyList& propList) {
    }

    void UltraCanvasCDRPainterImpl::closeUnorderedListLevel() {
    }

    void UltraCanvasCDRPainterImpl::openListElement(const librevenge::RVNGPropertyList& propList) {
    }

    void UltraCanvasCDRPainterImpl::closeListElement() {
    }

// ===== ULTRACANVAS CDR RENDERER IMPLEMENTATION =====

    void UltraCanvasCDRRenderer::CalculateAndSetFitTransform(IRenderContext* ctx, const CDRPage& page) {
        float pageWidth = page.width;
        float pageHeight = page.height;

        float scaleX = 1.0f, scaleY = 1.0f;

        switch (fitMode) {
            case CDRFitMode::FitWidth:
                scaleX = scaleY = viewWidth / pageWidth;
                break;

            case CDRFitMode::FitHeight:
                scaleX = scaleY = viewHeight / pageHeight;
                break;

            case CDRFitMode::FitPage:
                scaleX = viewWidth / pageWidth;
                scaleY = viewHeight / pageHeight;
                scaleX = scaleY = std::min(scaleX, scaleY);
                break;

            case CDRFitMode::FitNone:
            default:
                scaleX = scaleY = scaleLevel;
                break;
        }

        // Center the content
        float scaledWidth = pageWidth * scaleX;
        float scaledHeight = pageHeight * scaleY;
        float centerX = (viewWidth - scaledWidth) / 2.0f;
        float centerY = (viewHeight - scaledHeight) / 2.0f;

        ctx->Translate(centerX + offset.x,
                       centerY + offset.y);
        ctx->Scale(scaleX, scaleY);
    }

    void UltraCanvasCDRRenderer::SetViewport(float w, float h) {
        viewWidth = w;
        viewHeight = h;
    }

    void UltraCanvasCDRRenderer::SetOffset(float x, float y) {
        offset = {x, y};
    }

    void UltraCanvasCDRRenderer::SetFitMode(CDRFitMode mode) {
        fitMode = mode;
    }

    void UltraCanvasCDRRenderer::SetScale(float s) {
        scaleLevel = std::clamp(s, 0.1f, 10.0f);
    }

    void UltraCanvasCDRRenderer::RenderPage(IRenderContext* ctx, int pageIndex) {
        if (!document || pageIndex < 0 || pageIndex >= document->GetPageCount()) return;
        ctx->PushState();

        const CDRPage& page = document->pages[pageIndex];
        CalculateAndSetFitTransform(ctx, page);

        // Execute all draw commands
        for (const auto& cmd : page.drawCommands) {
            cmd(ctx);
        }
        ctx->PopState();
    }

    bool UltraCanvasCDRRenderer::LoadFromMemory(const std::vector<uint8_t>& data) {
        document = UltraCanvasCDRPlugin::ParseCDRMemory(data);

        if (!document || !document->IsValid()) {
            return false;
        }

        return true;
    }

// ===== ULTRACANVAS CDR ELEMENT IMPLEMENTATION =====

    UltraCanvasCDRElement::UltraCanvasCDRElement(const std::string& identifier, long id,
                                                 int x, int y, int width, int height)
            : UltraCanvasUIElement(identifier, id, x, y, width, height) {
    }

    bool UltraCanvasCDRElement::LoadFromFile(const std::string& filePath) {
        bool result = cdrRenderer.LoadFromFile(filePath);

        if (result && onLoadComplete) {
            onLoadComplete();
        }
        if (!result && onLoadError) {
            onLoadError("Failed to parse CDR file: " + filePath);
        }

        RequestRedraw();
        return result;
    }

    bool UltraCanvasCDRElement::LoadFromMemory(const std::vector<uint8_t>& data) {
        bool result = cdrRenderer.LoadFromMemory(data);

        if (result && onLoadComplete) {
            onLoadComplete();
        }
        if (!result && onLoadError) {
            onLoadError("Failed to parse CDR data from memory");
        }

        RequestRedraw();
        return result;
    }

    void UltraCanvasCDRElement::SetCurrentPage(int page) {
        auto newPage = std::clamp(page, 0, cdrRenderer.GetPageCount() - 1);
        if (newPage != currentPageIndex) {
            currentPageIndex = newPage;
            RequestRedraw();
            if (onPageChanged) {
                onPageChanged(currentPageIndex);
            }
        }
    }

    void UltraCanvasCDRElement::SetZoom(float zoom) {
        cdrRenderer.SetScale(zoom);
        RequestRedraw();
    }

    void UltraCanvasCDRElement::SetOffset(float x, float y) {
        cdrRenderer.SetOffset(x, y);
        RequestRedraw();
    }

    void UltraCanvasCDRElement::SetFitMode(CDRFitMode mode) {
        cdrRenderer.SetFitMode(mode);
        RequestRedraw();
    }

    void UltraCanvasCDRElement::RenderPlaceholder(IRenderContext* ctx, const std::string& message) {
        Rect2Di bounds = GetBounds();

        // Background
        ctx->SetFillPaint(Color(240, 240, 240, 255));
        ctx->FillRectangle(bounds);

        // Border
        ctx->SetStrokePaint(Color(180, 180, 180, 255));
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRectangle(bounds);

        // Message
        ctx->SetTextPaint(Color(100, 100, 100, 255));
        ctx->SetFontFace("Sans", FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(14.0f);
        ctx->DrawText(message, Point2Di(bounds.x + 10, bounds.y + bounds.height / 2));
    }

    void UltraCanvasCDRElement::Render(IRenderContext* ctx) {
        if (!IsVisible()) return;

        ctx->PushState();

        // Clip to element bounds
        Rect2Di contentRect = GetContentRect();
        ctx->ClipRect(static_cast<float>(contentRect.x), static_cast<float>(contentRect.y),
                      static_cast<float>(contentRect.width), static_cast<float>(contentRect.height));

        if (!cdrRenderer.IsLoaded()) {
            RenderPlaceholder(ctx, "No CDR document loaded");
            ctx->PopState();
            return;
        }

        // Apply fit/zoom transform
        ctx->PushState();

        cdrRenderer.SetViewport(contentRect.width, contentRect.height);
        cdrRenderer.RenderPage(ctx, currentPageIndex);

        ctx->PopState();
        ctx->PopState();
    }

// ===== CDR PLUGIN IMPLEMENTATION =====

    bool UltraCanvasCDRPlugin::CanHandle(const std::string& filePath) const {
        return IsFileSupported(filePath);
    }

    bool UltraCanvasCDRPlugin::CanHandle(const GraphicsFileInfo& fileInfo) const {
        return fileInfo.formatType == GraphicsFormatType::Vector &&
               CanHandle(fileInfo.filename);
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasCDRPlugin::LoadGraphics(const std::string& filePath) {
        auto element = std::make_shared<UltraCanvasCDRElement>(
                "cdr_" + std::to_string(rand()), rand(), 0, 0, 800, 600);

        if (element->LoadFromFile(filePath)) {
            return element;
        }
        return nullptr;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasCDRPlugin::LoadGraphics(const GraphicsFileInfo& fileInfo) {
        return LoadGraphics(fileInfo.filename);
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasCDRPlugin::CreateGraphics(int width, int height,
                                                                               GraphicsFormatType type) {
        if (type == GraphicsFormatType::Vector) {
            return std::make_shared<UltraCanvasCDRElement>(
                    "cdr_new_" + std::to_string(rand()), rand(), 0, 0, width, height);
        }
        return nullptr;
    }

    GraphicsManipulation UltraCanvasCDRPlugin::GetSupportedManipulations() const {
        return GraphicsManipulation::Move | GraphicsManipulation::Scale |
               GraphicsManipulation::Rotate | GraphicsManipulation::Transform;
    }

    GraphicsFileInfo UltraCanvasCDRPlugin::GetFileInfo(const std::string& filePath) {
        GraphicsFileInfo info(filePath);
        info.formatType = GraphicsFormatType::Vector;
        info.supportedManipulations = GetSupportedManipulations();

        // Try to get document dimensions
        auto doc = ParseCDRFile(filePath);
        if (doc && doc->IsValid()) {
            info.width = static_cast<int>(doc->documentWidth);
            info.height = static_cast<int>(doc->documentHeight);
            info.metadata["pages"] = std::to_string(doc->GetPageCount());
            info.metadata["title"] = doc->title;
            info.metadata["author"] = doc->author;
        }

        return info;
    }

    bool UltraCanvasCDRPlugin::ValidateFile(const std::string& filePath) {
        return IsFileSupported(filePath);
    }

    bool UltraCanvasCDRPlugin::IsFileSupported(const std::string& filePath) {
        librevenge::RVNGFileStream input(filePath.c_str());

        // Check if libcdr can handle this file
        if (libcdr::CDRDocument::isSupported(&input)) {
            return true;
        }

        // Also check CMX format
        if (libcdr::CMXDocument::isSupported(&input)) {
            return true;
        }

        return false;
    }

    std::shared_ptr<CDRDocument> UltraCanvasCDRPlugin::ParseCDRFile(const std::string& filePath) {
        librevenge::RVNGFileStream input(filePath.c_str());

        UltraCanvasCDRPainterImpl painter;

        bool success = false;

        // Try CDR format first
        if (libcdr::CDRDocument::isSupported(&input)) {
            success = libcdr::CDRDocument::parse(&input, &painter);
        }
            // Try CMX format
        else if (libcdr::CMXDocument::isSupported(&input)) {
            success = libcdr::CMXDocument::parse(&input, &painter);
        }

        if (!success) {
            std::cerr << "[UltraCanvasCDRPlugin] Failed to parse file: " << filePath << std::endl;
            return nullptr;
        }

        return painter.GetDocument();
    }

    std::shared_ptr<CDRDocument> UltraCanvasCDRPlugin::ParseCDRMemory(const std::vector<uint8_t>& data) {
        librevenge::RVNGStringStream input(
                reinterpret_cast<const unsigned char*>(data.data()),
                static_cast<unsigned long>(data.size()));

        UltraCanvasCDRPainterImpl painter;

        bool success = false;

        // Try CDR format first
        if (libcdr::CDRDocument::isSupported(&input)) {
            success = libcdr::CDRDocument::parse(&input, &painter);
        }
            // Try CMX format
        else if (libcdr::CMXDocument::isSupported(&input)) {
            success = libcdr::CMXDocument::parse(&input, &painter);
        }

        if (!success) {
            std::cerr << "[UltraCanvasCDRPlugin] Failed to parse CDR data from memory" << std::endl;
            return nullptr;
        }

        return painter.GetDocument();
    }


    bool UltraCanvasCDRRenderer::LoadFromFile(const std::string& filePath) {
        document = UltraCanvasCDRPlugin::ParseCDRFile(filePath);

        if (!document || !document->IsValid()) {
            return false;
        }
        return true;
    }
} // namespace UltraCanvas