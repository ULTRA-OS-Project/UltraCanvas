// UltraCanvasLinuxRenderContext.cpp - FIXED VERSION
// Linux platform implementation for UltraCanvas Framework using Cairo and X11
// Version: 1.0.2 - Fixed null pointer crashes and Pango initialization
// Last Modified: 2025-07-14
// Author: UltraCanvas Framework

#include "UltraCanvasLinuxRenderContext.h"
#include "UltraCanvasLinuxImageLoader.h"
#include <cstring>
#include <cmath>
#include <sstream>
#include <algorithm>
#include <iostream>

namespace UltraCanvas {

    LinuxRenderContext::LinuxRenderContext(cairo_t* cairoContext)
            : cairo(cairoContext)
            , pangoContext(nullptr)
            , fontMap(nullptr)
            , destroying(false)
            , contextValid(false){

        std::cout << "LinuxRenderContext: Initializing with cairo context: " << cairoContext << std::endl;

        // Validate Cairo context first
        if (!cairo) {
            std::cerr << "ERROR: LinuxRenderContext created with null Cairo context!" << std::endl;
            throw std::runtime_error("LinuxRenderContext: Cairo context is null");
        }

        // Check Cairo context status
        cairo_status_t status = cairo_status(cairo);
        if (status != CAIRO_STATUS_SUCCESS) {
            std::cerr << "ERROR: LinuxRenderContext: Cairo context is invalid: " << cairo_status_to_string(status) << std::endl;
            throw std::runtime_error("LinuxRenderContext: Invalid Cairo context");
        }

        // Initialize Pango for text rendering with proper error checking
        try {
            std::cout << "LinuxRenderContext: Initializing Pango..." << std::endl;

            fontMap = pango_cairo_font_map_get_default();
            if (!fontMap) {
                std::cerr << "ERROR: Failed to get default Pango font map" << std::endl;
                throw std::runtime_error("LinuxRenderContext: Failed to get Pango font map");
            }
            std::cout << "LinuxRenderContext: Got Pango font map: " << fontMap << std::endl;

            pangoContext = pango_font_map_create_context(fontMap);
            if (!pangoContext) {
                std::cerr << "ERROR: Failed to create Pango context" << std::endl;
                throw std::runtime_error("LinuxRenderContext: Failed to create Pango context");
            }
            std::cout << "LinuxRenderContext: Created Pango context: " << pangoContext << std::endl;

            // Associate Pango context with Cairo context
            pango_cairo_context_set_resolution(pangoContext, 96.0);  // Standard DPI

            // Get and set font options from Cairo
            cairo_font_options_t* fontOptions = cairo_font_options_create();
            cairo_get_font_options(cairo, fontOptions);
            pango_cairo_context_set_font_options(pangoContext, fontOptions);
            cairo_font_options_destroy(fontOptions);

            std::cout << "LinuxRenderContext: Pango initialization complete" << std::endl;

        } catch (const std::exception& e) {
            std::cerr << "ERROR: Exception during Pango initialization: " << e.what() << std::endl;

            // Cleanup on failure
            if (pangoContext) {
                g_object_unref(pangoContext);
                pangoContext = nullptr;
            }
            throw;
        }

        // Initialize default state
        ResetState();
        contextValid = true;
        std::cout << "LinuxRenderContext: Initialization complete" << std::endl;
    }

    LinuxRenderContext::~LinuxRenderContext() {
        std::cout << "LinuxRenderContext: Destroying..." << std::endl;
        destroying = true;

        // Clear any pending cairo operations first
        if (cairo) {
            cairo_surface_flush(cairo_get_target(cairo));
        }

        // Clear the state stack to prevent any pending cairo operations
        stateStack.clear();

        // Clean up Pango context
        if (pangoContext) {
            std::cout << "LinuxRenderContext: Unreferencing Pango context" << std::endl;
            g_object_unref(pangoContext);
            pangoContext = nullptr;
        }

        // Null the cairo pointer to prevent any accidental access
        // Note: We don't own the cairo context, so don't destroy it
        cairo = nullptr;

        std::cout << "LinuxRenderContext: Destruction complete" << std::endl;
    }
// ===== STATE MANAGEMENT =====
    void LinuxRenderContext::PushState() {
        stateStack.push_back(currentState);
        cairo_save(cairo);
    }

    void LinuxRenderContext::PopState() {
        if (!stateStack.empty()) {
            currentState = stateStack.back();
            stateStack.pop_back();
        }
        cairo_restore(cairo);
    }

    void LinuxRenderContext::ResetState() {
        currentState = RenderState();
        stateStack.clear();
        if (cairo) {
            cairo_identity_matrix(cairo);
            cairo_reset_clip(cairo);
        }
    }

// ===== TRANSFORMATION =====
    void LinuxRenderContext::Translate(float x, float y) {
        if (!cairo) return;
        cairo_translate(cairo, x, y);
        currentState.translation.x += x;
        currentState.translation.y += y;
    }

    void LinuxRenderContext::Rotate(float angle) {
        if (!cairo) return;
        cairo_rotate(cairo, angle);
        currentState.rotation += angle;
    }

    void LinuxRenderContext::Scale(float sx, float sy) {
        if (!cairo) return;
        cairo_scale(cairo, sx, sy);
        currentState.scale.x *= sx;
        currentState.scale.y *= sy;
    }

    void LinuxRenderContext::SetTransform(float a, float b, float c, float d, float e, float f) {
        if (!cairo) return;
        cairo_matrix_t matrix;
        cairo_matrix_init(&matrix, a, b, c, d, e, f);
        cairo_set_matrix(cairo, &matrix);
    }

    void LinuxRenderContext::ResetTransform() {
        if (!cairo) return;
        cairo_identity_matrix(cairo);
        currentState.translation = Point2Df(0, 0);
        currentState.rotation = 0;
        currentState.scale = Point2Df(1, 1);
    }

// ===== CLIPPING =====
    void LinuxRenderContext::SetClipRect(float x, float y, float w, float h) {
        if (!cairo) {
            std::cout << "LinuxRenderContext::SetClipRect - no cairo context!" << std::endl;
            return;
        }

        std::cout << "LinuxRenderContext::SetClipRect - setting clip to "
                  << x << "," << y << " " << w << "x" << h << std::endl;

        // Clear any existing clip region first
        cairo_reset_clip(cairo);

        // Set new clip region
        cairo_rectangle(cairo, x, y, w, h);
        cairo_clip(cairo);

        std::cout << "LinuxRenderContext::SetClipRect - clip region set successfully" << std::endl;
    }

    void LinuxRenderContext::ClearClipRect() {
        if (!cairo) {
            std::cout << "LinuxRenderContext::ClearClipRect - no cairo context!" << std::endl;
            return;
        }

        std::cout << "LinuxRenderContext::ClearClipRect - clearing clip region" << std::endl;

        // Reset the clip region to cover the entire surface
        cairo_reset_clip(cairo);

        std::cout << "LinuxRenderContext::ClearClipRect - clip region cleared successfully" << std::endl;
    }

//    void LinuxRenderContext::SetClipRect(const Rect2Df& rect) {
//        if (!cairo) return;
//        cairo_reset_clip(cairo);
//        cairo_rectangle(cairo, x, y, width, height);
//        cairo_clip(cairo);
//        currentState.clipRect = rect;
//    }
//
//    void LinuxRenderContext::ClearClipRect() {
//        if (!cairo) return;
//        cairo_reset_clip(cairo);
//        currentState.clipRect = Rect2Df(0, 0, 10000, 10000);
//    }

    void LinuxRenderContext::IntersectClipRect(float x, float y, float w, float h) {
        if (!cairo) return;
        cairo_rectangle(cairo, x, y, w, h);
        cairo_clip(cairo);
        currentState.clipRect = Rect2Df (x,y,w,h);
    }

// ===== STYLE MANAGEMENT =====
    void LinuxRenderContext::SetDrawingStyle(const DrawingStyle& style) {
        currentState.style = style;
        ApplyDrawingStyle(style);
    }

    const DrawingStyle& LinuxRenderContext::GetDrawingStyle() const {
        return currentState.style;
    }

    void LinuxRenderContext::SetTextStyle(const TextStyle& style) {
        currentState.textStyle = style;
        ApplyTextStyle(style);
    }

    const TextStyle& LinuxRenderContext::GetTextStyle() const {
        return currentState.textStyle;
    }

    void LinuxRenderContext::SetGlobalAlpha(float alpha) {
        currentState.globalAlpha = alpha;
    }

    float LinuxRenderContext::GetGlobalAlpha() const {
        return currentState.globalAlpha;
    }

// ===== BASIC DRAWING =====
    void LinuxRenderContext::FillRectangle(float x, float y, float w, float h) {
        if (!cairo) return;
        if (!ValidateContext()) {
            std::cerr << "LinuxRenderContext::FillRectangle: Invalid Cairo context" << std::endl;
            return;
        }

        std::cout << "LinuxRenderContext::FillRectangle this=" << this << " cairo=" << cairo << std::endl;

        // *** CRITICAL FIX: Apply fill style explicitly ***
        ApplyFillStyle(currentState.style);

        cairo_rectangle(cairo, x, y, w, h);
        cairo_fill(cairo);

        std::cout << "LinuxRenderContext::FillRectangle: Complete" << std::endl;
    }

    void LinuxRenderContext::DrawRectangle(float x, float y, float w, float h) {
        if (!cairo) return;
        if (!ValidateContext()) {
            std::cerr << "LinuxRenderContext::DrawRectangle: Invalid Cairo context" << std::endl;
            return;
        }

        std::cout << "LinuxRenderContext::DrawRectangle this=" << this << " cairo=" << cairo << std::endl;

        // *** CRITICAL FIX: Apply stroke style explicitly ***
        ApplyStrokeStyle(currentState.style);

        cairo_rectangle(cairo, x, y, w, h);
        cairo_stroke(cairo);

        std::cout << "LinuxRenderContext::DrawRectangle: Complete" << std::endl;
    }

    void LinuxRenderContext::FillRoundedRectangle(float x, float y, float w, float h, float radius) {
        if (!cairo) return;

        std::cout << "LinuxRenderContext::FillRoundedRectangle" << std::endl;

        // *** Apply fill style ***
        ApplyFillStyle(currentState.style);

        // Create rounded rectangle path
        cairo_new_sub_path(cairo);
        cairo_arc(cairo, x + w - radius, y + radius, radius, -M_PI_2, 0);
        cairo_arc(cairo, x + w - radius, y + h - radius, radius, 0, M_PI_2);
        cairo_arc(cairo, x + radius, y + h - radius, radius, M_PI_2, M_PI);
        cairo_arc(cairo, x + radius, y + radius, radius, M_PI, 3 * M_PI_2);
        cairo_close_path(cairo);

        cairo_fill(cairo);
    }

    void LinuxRenderContext::DrawRoundedRectangle(float x, float y, float w, float h, float radius) {
        if (!cairo) return;

        std::cout << "LinuxRenderContext::DrawRoundedRectangle" << std::endl;

        // *** Apply stroke style ***
        ApplyStrokeStyle(currentState.style);

        // Create rounded rectangle path
        cairo_new_sub_path(cairo);
        cairo_arc(cairo, x + w - radius, y + radius, radius, -M_PI_2, 0);
        cairo_arc(cairo, x + w - radius, y + h - radius, radius, 0, M_PI_2);
        cairo_arc(cairo, x + radius, y + h - radius, radius, M_PI_2, M_PI);
        cairo_arc(cairo, x + radius, y + radius, radius, M_PI, 3 * M_PI_2);
        cairo_close_path(cairo);

        cairo_stroke(cairo);
    }

    void LinuxRenderContext::FillCircle(float x, float y, float radius) {
        if (!cairo) return;

        std::cout << "LinuxRenderContext::FillCircle" << std::endl;

        // *** Apply fill style ***
        ApplyFillStyle(currentState.style);

        cairo_arc(cairo, x, y, radius, 0, 2 * M_PI);
        cairo_fill(cairo);
    }

    void LinuxRenderContext::DrawCircle(float x, float y, float radius) {
        if (!cairo) return;

        std::cout << "LinuxRenderContext::DrawCircle" << std::endl;

        // *** Apply stroke style ***
        ApplyStrokeStyle(currentState.style);

        cairo_arc(cairo, x, y, radius, 0, 2 * M_PI);
        cairo_stroke(cairo);
    }

    void LinuxRenderContext::DrawLine(float start_x, float start_y, float end_x, float end_y) {
        if (!cairo) return;

//        std::cout << "LinuxRenderContext::DrawLine" << std::endl;

        // *** Apply stroke style ***
        ApplyStrokeStyle(currentState.style);

        cairo_move_to(cairo, start_x, start_y);
        cairo_line_to(cairo, end_x, end_y);
        cairo_stroke(cairo);
    }

// ===== TEXT RENDERING (FIXED) =====
    void LinuxRenderContext::DrawText(const std::string& text, float x, float y) {
        // Comprehensive null checks
        if (!cairo) {
            std::cerr << "ERROR: DrawText called with null Cairo context" << std::endl;
            return;
        }

        if (!pangoContext) {
            std::cerr << "ERROR: DrawText called with null Pango context" << std::endl;
            return;
        }

        if (text.empty()) {
            return; // Nothing to draw
        }

        if (!ValidateContext()) {
            std::cerr << "LinuxRenderContext::FillRectangle: Invalid Cairo context" << std::endl;
            return;
        }

        try {
            //std::cout << "DrawText: Rendering '" << text << "' at (" << position.x << "," << position.y << ")" << std::endl;

            ApplyTextStyle(currentState.textStyle);

            PangoLayout* layout = pango_layout_new(pangoContext);
            if (!layout) {
                std::cerr << "ERROR: Failed to create Pango layout" << std::endl;
                return;
            }

            PangoFontDescription* desc = CreatePangoFont(currentState.textStyle);
            if (!desc) {
                std::cerr << "ERROR: Failed to create Pango font description" << std::endl;
                g_object_unref(layout);
                return;
            }

            pango_layout_set_font_description(layout, desc);
            pango_layout_set_text(layout, text.c_str(), -1);

            cairo_move_to(cairo, x, y);
            pango_cairo_show_layout(cairo, layout);

            // Cleanup
            pango_font_description_free(desc);
            g_object_unref(layout);

            std::cout << "DrawText: Completed successfully" << std::endl;

        } catch (const std::exception& e) {
            std::cerr << "ERROR: Exception in DrawText: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "ERROR: Unknown exception in DrawText" << std::endl;
        }
    }

    void LinuxRenderContext::DrawTextInRect(const std::string& text, float x, float y, float w, float h) {
        if (!cairo || !pangoContext || text.empty()) return;

        if (!ValidateContext()) {
            std::cerr << "LinuxRenderContext::FillRectangle: Invalid Cairo context" << std::endl;
            return;
        }

        try {
            ApplyTextStyle(currentState.textStyle);

            PangoLayout* layout = pango_layout_new(pangoContext);
            if (!layout) return;

            PangoFontDescription* desc = CreatePangoFont(currentState.textStyle);
            if (!desc) {
                g_object_unref(layout);
                return;
            }

            pango_layout_set_font_description(layout, desc);
            pango_layout_set_text(layout, text.c_str(), -1);
            pango_layout_set_width(layout, w * PANGO_SCALE);
            pango_layout_set_height(layout, h * PANGO_SCALE);

            // Set alignment
            PangoAlignment alignment = PANGO_ALIGN_LEFT;
            switch (currentState.textStyle.alignment) {
                case TextAlign::Center: alignment = PANGO_ALIGN_CENTER; break;
                case TextAlign::Right: alignment = PANGO_ALIGN_RIGHT; break;
                case TextAlign::Justify: alignment = PANGO_ALIGN_LEFT; break;
                default: alignment = PANGO_ALIGN_LEFT; break;
            }
            pango_layout_set_alignment(layout, alignment);
            if (currentState.textStyle.baseline == TextBaseline::Middle) {
                int w1, h1;
                pango_layout_get_pixel_size(layout, &w1, &h1);
                cairo_move_to(cairo, x, y + ((h - h1)/2));
            } else {
                cairo_move_to(cairo, x, y);
            }
            pango_cairo_show_layout(cairo, layout);

            pango_font_description_free(desc);
            g_object_unref(layout);

        } catch (...) {
            std::cerr << "ERROR: Exception in DrawTextInRect" << std::endl;
        }
    }

    bool LinuxRenderContext::MeasureText(const std::string& text, int& w, int& h) {
        w = 0;
        h = 0;
        if (!pangoContext || text.empty()) {
            return false;
        }

        try {
            PangoLayout* layout = pango_layout_new(pangoContext);
            if (!layout) return false;

            PangoFontDescription* desc = CreatePangoFont(currentState.textStyle);
            if (!desc) {
                g_object_unref(layout);
                return false;
            }

            pango_layout_set_font_description(layout, desc);
            pango_layout_set_text(layout, text.c_str(), -1);

            int width, height;
            pango_layout_get_pixel_size(layout, &width, &height);

            pango_font_description_free(desc);
            g_object_unref(layout);
            w = width;
            h = height;
            return true;

        } catch (...) {
            std::cerr << "ERROR: Exception in MeasureText" << std::endl;
            return false;
        }
    }

// ===== UTILITY FUNCTIONS =====
    void LinuxRenderContext::Clear(const Color& color) {
        if (!cairo) return;
        cairo_save(cairo);
        cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
        SetCairoColor(color);
        cairo_paint(cairo);
        cairo_restore(cairo);
    }

    void LinuxRenderContext::Flush() {
        if (!cairo) return;
        cairo_surface_flush(cairo_get_target(cairo));
    }

    void* LinuxRenderContext::GetNativeContext() {
        return cairo;
    }

// ===== INTERNAL HELPER METHODS (FIXED) =====
    void LinuxRenderContext::ApplyDrawingStyle(const DrawingStyle& style) {
        if (!cairo) return;

//        std::cout << "LinuxRenderContext::ApplyDrawingStyle - hasStroke=" << (style.hasStroke ? "true" : "false")
//                  << " fillMode=" << (int)style.fillMode << std::endl;

        // *** NEW APPROACH: Only set stroke properties, don't set colors ***
        // Colors will be set explicitly by ApplyFillStyle() and ApplyStrokeStyle()

        if (style.hasStroke) {
//            std::cout << "ApplyDrawingStyle: Setting stroke properties" << std::endl;

            cairo_set_line_width(cairo, style.strokeWidth);

            // Set line cap
            cairo_line_cap_t cap = CAIRO_LINE_CAP_BUTT;
            switch (style.lineCap) {
                case LineCap::Round: cap = CAIRO_LINE_CAP_ROUND; break;
                case LineCap::Square: cap = CAIRO_LINE_CAP_SQUARE; break;
                default: cap = CAIRO_LINE_CAP_BUTT; break;
            }
            cairo_set_line_cap(cairo, cap);

            // Set line join
            cairo_line_join_t join = CAIRO_LINE_JOIN_MITER;
            switch (style.lineJoin) {
                case LineJoin::Round: join = CAIRO_LINE_JOIN_ROUND; break;
                case LineJoin::Bevel: join = CAIRO_LINE_JOIN_BEVEL; break;
                default: join = CAIRO_LINE_JOIN_MITER; break;
            }
            cairo_set_line_join(cairo, join);

//            std::cout << "ApplyDrawingStyle: Stroke properties set - width=" << style.strokeWidth << std::endl;
        }

        // *** CRITICAL CHANGE: Don't set any colors here ***
        // Let the specific operations (fill/stroke) set their own colors

//        std::cout << "ApplyDrawingStyle: Complete (no color override)" << std::endl;
    }

    void LinuxRenderContext::ApplyTextStyle(const TextStyle& style) {
        if (!cairo) return;
        SetCairoColor(style.textColor);
    }

    void LinuxRenderContext::ApplyFillStyle(const DrawingStyle& style) {
        if (!cairo) return;

//        std::cout << "ApplyFillStyle: Setting fill color=(" << (int)style.fillColor.r
//                  << "," << (int)style.fillColor.g << "," << (int)style.fillColor.b
//                  << "," << (int)style.fillColor.a << ")" << std::endl;

        // Handle different fill modes
        switch (style.fillMode) {
            case FillMode::Solid:
                SetCairoColor(style.fillColor);
                break;

            case FillMode::Gradient:
                // For now, use solid color - gradient implementation would go here
                SetCairoColor(style.fillColor);
                std::cout << "ApplyFillStyle: Gradient mode not fully implemented, using solid color" << std::endl;
                break;

            case FillMode::Pattern:
                // For now, use solid color - pattern implementation would go here
                SetCairoColor(style.fillColor);
                std::cout << "ApplyFillStyle: Pattern mode not fully implemented, using solid color" << std::endl;
                break;

            case FillMode::NoneFill:
            default:
                std::cout << "ApplyFillStyle: No fill mode specified" << std::endl;
                break;
        }

//        std::cout << "ApplyFillStyle: Complete" << std::endl;
    }

    void LinuxRenderContext::ApplyStrokeStyle(const DrawingStyle& style) {
        if (!cairo) return;

        std::cout << "ApplyStrokeStyle: Setting stroke color=(" << (int)style.strokeColor.r
                  << "," << (int)style.strokeColor.g << "," << (int)style.strokeColor.b
                  << "," << (int)style.strokeColor.a << ")" << std::endl;

        // Set stroke properties (in case they weren't set by ApplyDrawingStyle)
        cairo_set_line_width(cairo, style.strokeWidth);

        // Set stroke color
        SetCairoColor(style.strokeColor);

        std::cout << "ApplyStrokeStyle: Complete" << std::endl;
    }

    PangoFontDescription* LinuxRenderContext::CreatePangoFont(const TextStyle& style) {
        try {
            PangoFontDescription* desc = pango_font_description_new();
            if (!desc) {
                std::cerr << "ERROR: Failed to create Pango font description" << std::endl;
                return nullptr;
            }

            // Use default font if family is empty
            const char* fontFamily = style.fontFamily.empty() ? "Arial" : style.fontFamily.c_str();
            pango_font_description_set_family(desc, fontFamily);

            // Ensure reasonable font size
            double fontSize = (style.fontSize > 0) ? style.fontSize : 12.0;
            pango_font_description_set_size(desc, fontSize * PANGO_SCALE);

            // Set weight
            PangoWeight weight = PANGO_WEIGHT_NORMAL;
            switch (style.fontWeight) {
                case FontWeight::Light: weight = PANGO_WEIGHT_LIGHT; break;
                case FontWeight::Bold: weight = PANGO_WEIGHT_BOLD; break;
                case FontWeight::ExtraBold: weight = PANGO_WEIGHT_ULTRABOLD; break;
                default: weight = PANGO_WEIGHT_NORMAL; break;
            }
            pango_font_description_set_weight(desc, weight);

            // Set style
            PangoStyle pangoStyle = PANGO_STYLE_NORMAL;
            switch (style.fontStyle) {
                case FontStyle::Italic: pangoStyle = PANGO_STYLE_ITALIC; break;
                case FontStyle::Oblique: pangoStyle = PANGO_STYLE_OBLIQUE; break;
                default: pangoStyle = PANGO_STYLE_NORMAL; break;
            }
            pango_font_description_set_style(desc, pangoStyle);

            return desc;

        } catch (...) {
            std::cerr << "ERROR: Exception in CreatePangoFont" << std::endl;
            return nullptr;
        }
    }

    void LinuxRenderContext::SetCairoColor(const Color& color) {
        if (!cairo) return;

        try {
            cairo_set_source_rgba(cairo,
                                  color.r / 255.0f,
                                  color.g / 255.0f,
                                  color.b / 255.0f,
                                  color.a / 255.0f * currentState.globalAlpha);
            std::cout << "LinuxRenderContext::SetCairoColor r=" << (int)color.r << " g=" << (int)color.g << " b=" << (int)color.b << std::endl;
        } catch (...) {
            std::cerr << "ERROR: Exception in SetCairoColor" << std::endl;
        }
    }


    void LinuxRenderContext::FillEllipse(float x, float y, float w, float h) {
        if (!cairo) return;

        std::cout << "LinuxRenderContext::FillEllipse" << std::endl;

        // *** Apply fill style ***
        ApplyFillStyle(currentState.style);

        cairo_save(cairo);
        cairo_translate(cairo, x + w/2, y + h/2);
        cairo_scale(cairo, w/2, h/2);
        cairo_arc(cairo, 0, 0, 1, 0, 2 * M_PI);
        cairo_restore(cairo);
        cairo_fill(cairo);
    }

    void LinuxRenderContext::DrawEllipse(float x, float y, float w, float h) {
        if (!cairo) return;

        std::cout << "LinuxRenderContext::DrawEllipse" << std::endl;

        // *** Apply stroke style ***
        ApplyStrokeStyle(currentState.style);

        cairo_save(cairo);
        cairo_translate(cairo, x + w/2, y + h/2);
        cairo_scale(cairo, w/2, h/2);
        cairo_arc(cairo, 0, 0, 1, 0, 2 * M_PI);
        cairo_restore(cairo);
        cairo_stroke(cairo);
    }

    void LinuxRenderContext::FillPath(const std::vector<Point2Df>& points) {
        if (!cairo || points.empty()) return;

        std::cout << "LinuxRenderContext::FillPath" << std::endl;

        // *** Apply fill style ***
        ApplyFillStyle(currentState.style);

        cairo_move_to(cairo, points[0].x, points[0].y);
        for (size_t i = 1; i < points.size(); ++i) {
            cairo_line_to(cairo, points[i].x, points[i].y);
        }
        cairo_close_path(cairo);
        cairo_fill(cairo);
    }

    void LinuxRenderContext::DrawPath(const std::vector<Point2Df>& points, bool closePath) {
        if (!cairo || points.empty()) return;

        std::cout << "LinuxRenderContext::DrawPath" << std::endl;

        // *** Apply stroke style ***
        ApplyStrokeStyle(currentState.style);

        cairo_move_to(cairo, points[0].x, points[0].y);
        for (size_t i = 1; i < points.size(); ++i) {
            cairo_line_to(cairo, points[i].x, points[i].y);
        }

        if (closePath) {
            cairo_close_path(cairo);
        }

        cairo_stroke(cairo);
    }


    void LinuxRenderContext::DrawArc(float x, float y, float radius, float startAngle, float endAngle) {
        if (!cairo) return;
        ApplyStrokeStyle(currentState.style);
        cairo_arc(cairo, x, y, radius, startAngle, endAngle);
        cairo_stroke(cairo);
    }

    void LinuxRenderContext::FillArc(float x, float y, float radius, float startAngle, float endAngle) {
        if (!cairo) return;
        ApplyFillStyle(currentState.style);
        cairo_move_to(cairo, x, y);
        cairo_arc(cairo, x, y, radius, startAngle, endAngle);
        cairo_close_path(cairo);
        cairo_fill(cairo);
    }

    void LinuxRenderContext::DrawBezier(const Point2Df& start, const Point2Df& cp1, const Point2Df& cp2, const Point2Df& end) {
        if (!cairo) return;
        ApplyStrokeStyle(currentState.style);
        cairo_move_to(cairo, start.x, start.y);
        cairo_curve_to(cairo, cp1.x, cp1.y, cp2.x, cp2.y, end.x, end.y);
        cairo_stroke(cairo);
    }

    // Image rendering stubs
    // ===== IMAGE RENDERING IMPLEMENTATION =====

    void LinuxRenderContext::DrawImage(const std::string& imagePath, float x, float y) {
        if (!cairo || imagePath.empty()) {
            std::cerr << "LinuxRenderContext::DrawImage: Invalid parameters" << std::endl;
            return;
        }

        try {
            // Load the image
            ImageLoadResult result = LinuxImageLoader::LoadImage(imagePath);

            if (!result.success) {
                std::cerr << "LinuxRenderContext::DrawImage: Failed to load image '"
                          << imagePath << "': " << result.errorMessage << std::endl;
                return;
            }

            // Save current cairo state
            cairo_save(cairo);

            // Apply global alpha
            if (currentState.globalAlpha < 1.0f) {
                cairo_set_source_surface(cairo, result.surface, x, y);
                cairo_paint_with_alpha(cairo, currentState.globalAlpha);
            } else {
                // Direct drawing for better performance when no transparency
                cairo_set_source_surface(cairo, result.surface, x, y);
                cairo_paint(cairo);
            }

            // Restore cairo state
            cairo_restore(cairo);

            // Release surface reference (if not cached, this will destroy it)
            cairo_surface_destroy(result.surface);

        } catch (const std::exception& e) {
            std::cerr << "LinuxRenderContext::DrawImage: Exception loading image: " << e.what() << std::endl;
        }
    }

    void LinuxRenderContext::DrawImage(const std::string& imagePath, float x, float y, float w, float h) {
        if (!cairo || imagePath.empty()) {
            std::cerr << "LinuxRenderContext::DrawImage: Invalid parameters" << std::endl;
            return;
        }

        try {
            // Load the image
            ImageLoadResult result = LinuxImageLoader::LoadImage(imagePath);

            if (!result.success) {
                std::cerr << "LinuxRenderContext::DrawImage: Failed to load image '"
                          << imagePath << "': " << result.errorMessage << std::endl;
                return;
            }

            // Save current cairo state
            cairo_save(cairo);

            // Calculate scaling factors
            float scaleX = w / static_cast<float>(result.width);
            float scaleY = h / static_cast<float>(result.height);

            // Apply transformations
            cairo_translate(cairo, x, y);
            cairo_scale(cairo, scaleX, scaleY);

            // Set the image as source and paint
            cairo_set_source_surface(cairo, result.surface, 0, 0);

            // Apply clipping to ensure we don't draw outside the destination rectangle
            cairo_rectangle(cairo, 0, 0, result.width, result.height);
            cairo_clip(cairo);

            if (currentState.globalAlpha < 1.0f) {
                cairo_paint_with_alpha(cairo, currentState.globalAlpha);
            } else {
                cairo_paint(cairo);
            }

            // Restore cairo state
            cairo_restore(cairo);

            // Release surface reference
            cairo_surface_destroy(result.surface);

        } catch (const std::exception& e) {
            std::cerr << "LinuxRenderContext::DrawImage: Exception loading image: " << e.what() << std::endl;
        }
    }

    void LinuxRenderContext::DrawImage(const std::string& imagePath, const Rect2Df& srcRect, const Rect2Df& destRect) {
        if (!cairo || imagePath.empty()) {
            std::cerr << "LinuxRenderContext::DrawImage: Invalid parameters" << std::endl;
            return;
        }

        try {
            // Load the image
            ImageLoadResult result = LinuxImageLoader::LoadImage(imagePath);

            if (!result.success) {
                std::cerr << "LinuxRenderContext::DrawImage: Failed to load image '"
                          << imagePath << "': " << result.errorMessage << std::endl;
                return;
            }

            // Validate source rectangle bounds
            if (srcRect.x < 0 || srcRect.y < 0 ||
                srcRect.x + srcRect.width > result.width ||
                srcRect.y + srcRect.height > result.height) {
                std::cerr << "LinuxRenderContext::DrawImage: Source rectangle out of bounds" << std::endl;
                cairo_surface_destroy(result.surface);
                return;
            }

            // Save current cairo state
            cairo_save(cairo);

            // Calculate scaling factors
            float scaleX = destRect.width / srcRect.width;
            float scaleY = destRect.height / srcRect.height;

            // Set up transformations for source rectangle mapping
            cairo_translate(cairo, destRect.x, destRect.y);
            cairo_scale(cairo, scaleX, scaleY);
            cairo_translate(cairo, -srcRect.x, -srcRect.y);

            // Set the image as source
            cairo_set_source_surface(cairo, result.surface, 0, 0);

            // Create clipping rectangle for the destination area
            cairo_reset_clip(cairo);
            cairo_rectangle(cairo,
                            srcRect.x, srcRect.y,
                            srcRect.width, srcRect.height);
            cairo_clip(cairo);

            // Paint the image
            if (currentState.globalAlpha < 1.0f) {
                cairo_paint_with_alpha(cairo, currentState.globalAlpha);
            } else {
                cairo_paint(cairo);
            }

            // Restore cairo state
            cairo_restore(cairo);

            // Release surface reference
            cairo_surface_destroy(result.surface);

        } catch (const std::exception& e) {
            std::cerr << "LinuxRenderContext::DrawImage: Exception loading image: " << e.what() << std::endl;
        }
    }

    // ===== ADDITIONAL HELPER METHODS FOR IMAGE RENDERING =====

    bool LinuxRenderContext::IsImageFormatSupported(const std::string& filePath) {
        std::string ext = GetFileExtension(filePath);
        return LinuxImageLoader::IsFormatSupported(ext);
    }

    bool LinuxRenderContext::GetImageDimensions(const std::string& imagePath, int &w, int& h) {
        w = 0;
        h = 0;
        if (imagePath.empty()) {
            return false;
        }

        try {
            ImageLoadResult result = LinuxImageLoader::LoadImage(imagePath);
            if (result.success) {
                w = result.width;
                h = result.height;
                cairo_surface_destroy(result.surface);
                return true;
            }
        } catch (const std::exception& e) {
            std::cerr << "LinuxRenderContext::GetImageDimensions: Exception: " << e.what() << std::endl;
        }

        return false;
    }

    void LinuxRenderContext::DrawImageWithFilter(const std::string& imagePath, float x, float y, float w, float h,
                                                 cairo_filter_t filter) {
        if (!cairo || imagePath.empty()) return;

        try {
            ImageLoadResult result = LinuxImageLoader::LoadImage(imagePath);
            if (!result.success) return;

            cairo_save(cairo);

            // Set up scaling pattern with specified filter
            cairo_pattern_t* pattern = cairo_pattern_create_for_surface(result.surface);
            cairo_pattern_set_filter(pattern, filter);

            // Calculate and apply transformation matrix
            cairo_matrix_t matrix;
            cairo_matrix_init_scale(&matrix,
                                    static_cast<double>(result.width) / w,
                                    static_cast<double>(result.height) / h);
            cairo_matrix_translate(&matrix, -x, -y);
            cairo_pattern_set_matrix(pattern, &matrix);

            // Set source and paint
            cairo_set_source(cairo, pattern);
            cairo_rectangle(cairo, x, y, w, h);

            if (currentState.globalAlpha < 1.0f) {
                cairo_clip(cairo);
                cairo_paint_with_alpha(cairo, currentState.globalAlpha);
            } else {
                cairo_fill(cairo);
            }

            cairo_pattern_destroy(pattern);
            cairo_restore(cairo);
            cairo_surface_destroy(result.surface);

        } catch (const std::exception& e) {
            std::cerr << "LinuxRenderContext::DrawImageWithFilter: Exception: " << e.what() << std::endl;
        }
    }

    void LinuxRenderContext::DrawImageTiled(const std::string& imagePath, float x, float y, float w, float h) {
        if (!cairo || imagePath.empty()) return;

        try {
            ImageLoadResult result = LinuxImageLoader::LoadImage(imagePath);
            if (!result.success) return;

            cairo_save(cairo);

            // Create repeating pattern
            cairo_pattern_t* pattern = cairo_pattern_create_for_surface(result.surface);
            cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);

            // Set source and fill the rectangle
            cairo_set_source(cairo, pattern);
            cairo_rectangle(cairo, x, y, w, h);

            if (currentState.globalAlpha < 1.0f) {
                cairo_clip(cairo);
                cairo_paint_with_alpha(cairo, currentState.globalAlpha);
            } else {
                cairo_fill(cairo);
            }

            cairo_pattern_destroy(pattern);
            cairo_restore(cairo);
            cairo_surface_destroy(result.surface);

        } catch (const std::exception& e) {
            std::cerr << "LinuxRenderContext::DrawImageTiled: Exception: " << e.what() << std::endl;
        }
    }

    void LinuxRenderContext::ClearImageCache() {
        LinuxImageLoader::ClearCache();
    }

    void LinuxRenderContext::SetImageCacheSize(size_t maxSizeBytes) {
        LinuxImageLoader::SetMaxCacheSize(maxSizeBytes);
    }

    size_t LinuxRenderContext::GetImageCacheMemoryUsage() {
        return LinuxImageLoader::GetCacheMemoryUsage();
    }

    void LinuxRenderContext::InvalidateContext() {
        std::cout << "LinuxRenderContext: Invalidating Cairo context..." << std::endl;

        // Clear any pending cairo operations ONLY if context is still valid
        if (cairo) {
            cairo_status_t status = cairo_status(cairo);
            if (status == CAIRO_STATUS_SUCCESS) {
                try {
                    cairo_surface_flush(cairo_get_target(cairo));
                } catch (...) {
                    std::cout << "LinuxRenderContext: Exception during surface flush, ignoring..." << std::endl;
                }
            } else {
                std::cout << "LinuxRenderContext: Cairo context already invalid ("
                          << cairo_status_to_string(status) << "), skipping flush" << std::endl;
            }
        }

        // Clear the state stack to prevent any pending cairo operations
        stateStack.clear();

        // Mark context as invalid but don't destroy it (window will handle that)
        contextValid = false;
    }

    void LinuxRenderContext::UpdateContext(cairo_t* newCairoContext) {
        std::cout << "LinuxRenderContext: Updating Cairo context..." << std::endl;

        if (!newCairoContext) {
            std::cerr << "ERROR: LinuxRenderContext: New Cairo context is null!" << std::endl;
            return;
        }

        // Update the cairo pointer
        cairo = newCairoContext;

        // Check new context status
        cairo_status_t status = cairo_status(cairo);
        if (status != CAIRO_STATUS_SUCCESS) {
            std::cerr << "ERROR: LinuxRenderContext: New Cairo context is invalid: "
                      << cairo_status_to_string(status) << std::endl;
            contextValid = false;
            return;
        }

        // Re-associate Pango context with new Cairo context
        if (pangoContext) {
            pango_cairo_context_set_resolution(pangoContext, 96.0);

            cairo_font_options_t* fontOptions = cairo_font_options_create();
            cairo_get_font_options(cairo, fontOptions);
            pango_cairo_context_set_font_options(pangoContext, fontOptions);
            cairo_font_options_destroy(fontOptions);
        }

        // Reset state
        ResetState();
        contextValid = true;

        std::cout << "LinuxRenderContext: Cairo context updated successfully" << std::endl;
    }

// ===== VALIDATION CHECKS =====
    bool LinuxRenderContext::ValidateContext() const {
        if (!contextValid || !cairo) {
            return false;
        }

        cairo_status_t status = cairo_status(cairo);
        return status == CAIRO_STATUS_SUCCESS;
    }

    // ===== PRIVATE HELPER METHOD =====

//    void LinuxRenderContext::DrawImage(const std::string& imagePath, const Point2Df& position) {
//        // Placeholder - would need image loading library
//    }
//
//    void LinuxRenderContext::DrawImage(const std::string& imagePath, const Rect2Df& destRect) {
//        // Placeholder - would need image loading library
//    }
//
//    void LinuxRenderContext::DrawImage(const std::string& imagePath, const Rect2Df& srcRect, const Rect2Df& destRect) {
//        // Placeholder - would need image loading library
//    }

    void LinuxRenderContext::SetPixel(const Point2Df& point, const Color& color) {
        if (!cairo) return;
        SetCairoColor(color);
        cairo_rectangle(cairo, point.x, point.y, 1, 1);
        cairo_fill(cairo);
    }

    Color LinuxRenderContext::GetPixel(const Point2Df& point) {
        // Cairo doesn't provide direct pixel access
        return Colors::Black;
    }

//    std::string GetFileExtension(const std::string& filePath) {
//        size_t dotPos = filePath.find_last_of('.');
//        if (dotPos == std::string::npos) return "";
//
//        std::string ext = filePath.substr(dotPos + 1);
//        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
//        return ext;
//    }

} // namespace UltraCanvas