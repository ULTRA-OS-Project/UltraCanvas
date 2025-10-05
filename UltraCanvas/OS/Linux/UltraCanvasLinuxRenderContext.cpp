// OS/Linux/UltraCanvasLinuxRenderContext.cpp
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
#include <stdexcept>

namespace UltraCanvas {

    LinuxRenderContext::LinuxRenderContext(cairo_t *cairoContext, cairo_surface_t *cairo_surface, int width, int height, bool enableDoubleBuffering)
        : originalWindowSurface(cairo_surface),
        originalWindowContext(cairoContext), cairo(cairoContext), pangoContext(nullptr), fontMap(nullptr), destroying(false) {

        std::cout << "LinuxRenderContext: Initializing with cairo context: " << cairoContext << std::endl;

        // Validate Cairo context first
        if (!cairo) {
            std::cerr << "ERROR: LinuxRenderContext created with null Cairo context!" << std::endl;
            throw std::runtime_error("LinuxRenderContext: Cairo context is null");
        }

        // Check Cairo context status
        cairo_status_t status = cairo_status(cairo);
        if (status != CAIRO_STATUS_SUCCESS) {
            std::cerr << "ERROR: LinuxRenderContext: Cairo context is invalid: " << cairo_status_to_string(status)
                      << std::endl;
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
            cairo_font_options_t *fontOptions = cairo_font_options_create();
            cairo_get_font_options(cairo, fontOptions);
            pango_cairo_context_set_font_options(pangoContext, fontOptions);
            cairo_font_options_destroy(fontOptions);

            std::cout << "LinuxRenderContext: Pango initialization complete" << std::endl;

        } catch (const std::exception &e) {
            std::cerr << "ERROR: Exception during Pango initialization: " << e.what() << std::endl;

            // Cleanup on failure
            if (pangoContext) {
                g_object_unref(pangoContext);
                pangoContext = nullptr;
            }
            throw;
        }

        if (enableDoubleBuffering) {
            EnableDoubleBuffering(width, height);
        }
        // Initialize default state
        ResetState();
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

        doubleBuffer.Cleanup();

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
        } else {
            std::cout << "LinuxRenderContext::PopState() stateStack empty!" << std::endl;
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
        cairo_translate(cairo, x, y);
        currentState.translation.x += x;
        currentState.translation.y += y;
    }

    void LinuxRenderContext::Rotate(float angle) {
        cairo_rotate(cairo, angle);
        currentState.rotation += angle;
    }

    void LinuxRenderContext::Scale(float sx, float sy) {
        cairo_scale(cairo, sx, sy);
        currentState.scale.x *= sx;
        currentState.scale.y *= sy;
    }

    void LinuxRenderContext::SetTransform(float a, float b, float c, float d, float e, float f) {
        cairo_matrix_t matrix;
        cairo_matrix_init(&matrix, a, b, c, d, e, f);
        cairo_set_matrix(cairo, &matrix);
    }

    void LinuxRenderContext::Transform(float a, float b, float c, float d, float e, float f) {
        cairo_matrix_t matrix;
        cairo_matrix_init(&matrix, a, b, c, d, e, f);
        cairo_transform(cairo, &matrix);
    }

    void LinuxRenderContext::ResetTransform() {
        cairo_identity_matrix(cairo);
        currentState.translation = Point2Df(0, 0);
        currentState.rotation = 0;
        currentState.scale = Point2Df(1, 1);
    }


// ===== CLIPPING =====
    void LinuxRenderContext::SetClipRect(float x, float y, float w, float h) {
        std::cout << "LinuxRenderContext::SetClipRect - setting clip to "
                  << x << "," << y << " " << w << "x" << h << std::endl;

        // Clear any existing clip region first
        cairo_reset_clip(cairo);

        // Set new clip region
        cairo_rectangle(cairo, x, y, w, h);
        cairo_clip(cairo);
        currentState.clipRect = Rect2Df(x,y,w,h);
        std::cout << "LinuxRenderContext::SetClipRect - clip region set successfully" << std::endl;
    }

    void LinuxRenderContext::ClearClipRect() {
//        std::cout << "LinuxRenderContext::ClearClipRect - clearing clip region" << std::endl;

        // Reset the clip region to cover the entire surface
        cairo_reset_clip(cairo);
        currentState.clipRect = Rect2Df(0,0,0,0);

//        std::cout << "LinuxRenderContext::ClearClipRect - clip region cleared successfully" << std::endl;
    }

    void LinuxRenderContext::ClipRect(float x, float y, float w, float h) {
        cairo_rectangle(cairo, x, y, w, h);
        cairo_clip(cairo);
        currentState.clipRect = Rect2Df(x, y, w, h);
    }

    void LinuxRenderContext::ClipPath() {
        cairo_clip(cairo);
    }


// ===== BASIC DRAWING =====
    void LinuxRenderContext::FillRectangle(float x, float y, float w, float h) {
//        std::cout << "LinuxRenderContext::FillRectangle this=" << this << " cairo=" << cairo << std::endl;

        // *** CRITICAL FIX: Apply fill style explicitly ***
        //ApplyFillStyle(currentState.style);

        cairo_rectangle(cairo, x, y, w, h);
        cairo_fill(cairo);

//        std::cout << "LinuxRenderContext::FillRectangle: Complete" << std::endl;
    }

    void LinuxRenderContext::DrawRectangle(float x, float y, float w, float h) {
//        std::cout << "LinuxRenderContext::DrawRectangle this=" << this << " cairo=" << cairo << std::endl;

        // *** CRITICAL FIX: Apply stroke style explicitly ***
        //ApplyStrokeStyle(currentState.style);

        cairo_rectangle(cairo, x, y, w, h);
        cairo_stroke(cairo);

//        std::cout << "LinuxRenderContext::DrawRectangle: Complete" << std::endl;
    }

    void LinuxRenderContext::FillRoundedRectangle(float x, float y, float w, float h, float radius) {
//        std::cout << "LinuxRenderContext::FillRoundedRectangle" << std::endl;

        // *** Apply fill style ***
        //ApplyFillStyle(currentState.style);

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
//        std::cout << "LinuxRenderContext::DrawRoundedRectangle" << std::endl;

        // *** Apply stroke style ***
        //ApplyStrokeStyle(currentState.style);

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
//        std::cout << "LinuxRenderContext::FillCircle" << std::endl;

        // *** Apply fill style ***
        //ApplyFillStyle(currentState.style);

        cairo_arc(cairo, x, y, radius, 0, 2 * M_PI);
        cairo_fill(cairo);
    }

    void LinuxRenderContext::DrawCircle(float x, float y, float radius) {
//        std::cout << "LinuxRenderContext::DrawCircle" << std::endl;

        // *** Apply stroke style ***
        //ApplyStrokeStyle(currentState.style);

        cairo_arc(cairo, x, y, radius, 0, 2 * M_PI);
        cairo_stroke(cairo);
    }

    void LinuxRenderContext::DrawLine(float start_x, float start_y, float end_x, float end_y) {
//        std::cout << "LinuxRenderContext::DrawLine" << std::endl;

        // *** Apply stroke style ***
        //ApplyStrokeStyle(currentState.style);

        cairo_move_to(cairo, start_x, start_y);
        cairo_line_to(cairo, end_x, end_y);
        cairo_stroke(cairo);
    }

    PangoLayout* LinuxRenderContext::CreatePangoLayout(PangoFontDescription *desc, int w, int h) {
        PangoLayout *layout = pango_layout_new(pangoContext);
        if (!layout) {
            std::cerr << "ERROR: Failed to create Pango layout" << std::endl;
            return nullptr;
        }

        pango_layout_set_font_description(layout, desc);
        if (w > 0 && h > 0) {
            pango_layout_set_width(layout, w * PANGO_SCALE);
            pango_layout_set_height(layout, h * PANGO_SCALE);

            PangoAlignment alignment = PANGO_ALIGN_LEFT;

            switch (currentState.textStyle.alignment) {
                case TextAlignment::Center:
                    alignment = PANGO_ALIGN_CENTER;
                    break;
                case TextAlignment::Right:
                    alignment = PANGO_ALIGN_RIGHT;
                    break;
                case TextAlignment::Justify:
                    alignment = PANGO_ALIGN_LEFT;
                    break;
                default:
                    alignment = PANGO_ALIGN_LEFT;
                    break;
            }
            pango_layout_set_alignment(layout, alignment);
            if (currentState.textStyle.wrap) {
                pango_layout_set_ellipsize(layout, PangoEllipsizeMode::PANGO_ELLIPSIZE_NONE);
                pango_layout_set_wrap(layout, PangoWrapMode::PANGO_WRAP_WORD_CHAR);
            } else {
                pango_layout_set_ellipsize(layout, PangoEllipsizeMode::PANGO_ELLIPSIZE_END);
            }
        }

        return layout;
    }
// ===== TEXT RENDERING (FIXED) =====
    void LinuxRenderContext::DrawText(const std::string &text, float x, float y) {
        // Comprehensive null checks
        if (text.empty()) {
            return; // Nothing to draw
        }

        try {
            //std::cout << "DrawText: Rendering '" << text << "' at (" << position.x << "," << position.y << ")" << std::endl;
            PangoFontDescription *desc = CreatePangoFont(currentState.fontStyle);
            if (!desc) {
                std::cerr << "ERROR: Failed to create Pango font description" << std::endl;
                return;
            }
            PangoLayout *layout = CreatePangoLayout(desc);
            if (!layout) {
                pango_font_description_free(desc);
                std::cerr << "ERROR: Failed to create Pango layout" << std::endl;
                return;
            }

            pango_layout_set_text(layout, text.c_str(), -1);

            //SetCairoColor(currentState.textStyle.textColor);

            cairo_move_to(cairo, x, y);
            pango_cairo_show_layout(cairo, layout);

            // Cleanup
            pango_font_description_free(desc);
            g_object_unref(layout);

//            std::cout << "DrawText: Completed successfully" << std::endl;

        } catch (const std::exception &e) {
            std::cerr << "ERROR: Exception in DrawText: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "ERROR: Unknown exception in DrawText" << std::endl;
        }
    }

    void LinuxRenderContext::DrawTextInRect(const std::string &text, float x, float y, float w, float h) {
        if (text.empty()) return;

        try {
            PangoFontDescription *desc = CreatePangoFont(currentState.fontStyle);
            if (!desc) {
                std::cerr << "ERROR: Failed to create Pango font description" << std::endl;
                return;
            }
            PangoLayout *layout = CreatePangoLayout(desc, w, h);
            if (!layout) {
                pango_font_description_free(desc);
                std::cerr << "ERROR: Failed to create Pango layout" << std::endl;
                return;
            }
            pango_layout_set_text(layout, text.c_str(), -1);

            if (currentState.textStyle.verticalAlignement == TextVerticalAlignement::Middle) {
                int w1, h1;
                pango_layout_get_pixel_size(layout, &w1, &h1);
                cairo_move_to(cairo, x, y + ((h - h1) / 2));
//                cairo_move_to(cairo, x, y);
            } else {
                cairo_move_to(cairo, x, y);
            }
            //SetCairoColor(currentState.textStyle.textColor);
            pango_cairo_show_layout(cairo, layout);

            pango_font_description_free(desc);
            g_object_unref(layout);

        } catch (...) {
            std::cerr << "ERROR: Exception in DrawTextInRect" << std::endl;
        }
    }

    bool LinuxRenderContext::MeasureText(const std::string &text, int &w, int &h) {
        w = 0;
        h = 0;
        if (!pangoContext || text.empty()) {
            return false;
        }

        try {
            PangoFontDescription *desc = CreatePangoFont(currentState.fontStyle);
            if (!desc) {
                std::cerr << "ERROR: Failed to create Pango font description" << std::endl;
                return false;
            }
            PangoLayout *layout = CreatePangoLayout(desc);
            if (!layout) {
                pango_font_description_free(desc);
                std::cerr << "ERROR: Failed to create Pango layout" << std::endl;
                return false;
            }

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

    int LinuxRenderContext::GetTextIndexForXY(const std::string &text, int x, int y, int w, int h) {
        int index = 0, trailing;
        if (!pangoContext || text.empty()) {
            return -1;
        }

        try {
            PangoFontDescription *desc = CreatePangoFont(currentState.fontStyle);
            if (!desc) {
                std::cerr << "ERROR: Failed to create Pango font description" << std::endl;
                return -1;
            }
            PangoLayout *layout = CreatePangoLayout(desc, w, h);
            if (!layout) {
                pango_font_description_free(desc);
                std::cerr << "ERROR: Failed to create Pango layout" << std::endl;
                return -1;
            }

            pango_layout_set_text(layout, text.c_str(), -1);

            if (!pango_layout_xy_to_index(layout, x * PANGO_SCALE, y * PANGO_SCALE, &index, &trailing)) {
                index = -1;
            }

            pango_font_description_free(desc);
            g_object_unref(layout);
            return index;

        } catch (...) {
            std::cerr << "ERROR: Exception in TextXYToIndex" << std::endl;
            return -1;
        }
    }

// ===== UTILITY FUNCTIONS =====
    void LinuxRenderContext::Clear(const Color &color) {
        cairo_save(cairo);
        cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
        SetCairoColor(color);
        cairo_paint(cairo);
        cairo_restore(cairo);
    }

    void LinuxRenderContext::Flush() {
        cairo_surface_flush(cairo_get_target(cairo));
        if (doubleBufferingEnabled && doubleBuffer.IsValid()) {
            std::cout << "Flush: Swapping double buffer" << std::endl;
            doubleBuffer.SwapBuffers();
        }
    }

    void *LinuxRenderContext::GetNativeContext() {
        return cairo;
    }

    void LinuxRenderContext::SetTextStyle(const TextStyle &style) {
        currentState.textStyle = style;
        SetCairoColor(style.textColor);
        //ApplyTextStyle(style);
    }

    const TextStyle &LinuxRenderContext::GetTextStyle() const {
        return currentState.textStyle;
    }

    float LinuxRenderContext::GetAlpha() const {
        return currentState.globalAlpha;
    }

// ===== INTERNAL HELPER METHODS (FIXED) =====
//    void LinuxRenderContext::ApplyDrawingStyle(const DrawingStyle &style) {
////        std::cout << "LinuxRenderContext::ApplyDrawingStyle - hasStroke=" << (style.hasStroke ? "true" : "false")
////                  << " fillMode=" << (int)style.fillMode << std::endl;
//
//        // *** NEW APPROACH: Only set stroke properties, don't set colors ***
//        // Colors will be set explicitly by ApplyFillStyle() and ApplyStrokeStyle()
//
//        if (style.hasStroke) {
////            std::cout << "ApplyDrawingStyle: Setting stroke properties" << std::endl;
//
//            cairo_set_line_width(cairo, style.strokeWidth);
//
//            // Set line cap
//            cairo_line_cap_t cap = CAIRO_LINE_CAP_BUTT;
//            switch (style.lineCap) {
//                case LineCap::Round:
//                    cap = CAIRO_LINE_CAP_ROUND;
//                    break;
//                case LineCap::Square:
//                    cap = CAIRO_LINE_CAP_SQUARE;
//                    break;
//                default:
//                    cap = CAIRO_LINE_CAP_BUTT;
//                    break;
//            }
//            cairo_set_line_cap(cairo, cap);
//
//            // Set line join
//            cairo_line_join_t join = CAIRO_LINE_JOIN_MITER;
//            switch (style.lineJoin) {
//                case LineJoin::Round:
//                    join = CAIRO_LINE_JOIN_ROUND;
//                    break;
//                case LineJoin::Bevel:
//                    join = CAIRO_LINE_JOIN_BEVEL;
//                    break;
//                default:
//                    join = CAIRO_LINE_JOIN_MITER;
//                    break;
//            }
//            cairo_set_line_join(cairo, join);
//
////            std::cout << "ApplyDrawingStyle: Stroke properties set - width=" << style.strokeWidth << std::endl;
//        }
//
//        // *** CRITICAL CHANGE: Don't set any colors here ***
//        // Let the specific operations (fill/stroke) set their own colors
//
////        std::cout << "ApplyDrawingStyle: Complete (no color)" << std::endl;
//    }

//    void LinuxRenderContext::ApplyTextStyle(const TextStyle &style) {
//        SetCairoColor(style.textColor);
//    }

//    void LinuxRenderContext::ApplyStrokeStyle(const DrawingStyle &style) {
////        std::cout << "ApplyStrokeStyle: Setting stroke color=(" << (int) style.strokeColor.r
////                  << "," << (int) style.strokeColor.g << "," << (int) style.strokeColor.b
////                  << "," << (int) style.strokeColor.a << ")" << std::endl;
//
//        // Set stroke properties (in case they weren't set by ApplyDrawingStyle)
//        cairo_set_line_width(cairo, style.strokeWidth);
//
//        // Set stroke color
//        SetCairoColor(style.strokeColor);
//
////        std::cout << "ApplyStrokeStyle: Complete" << std::endl;
//    }

    PangoFontDescription *LinuxRenderContext::CreatePangoFont(const FontStyle &style) {
        try {
            PangoFontDescription *desc = pango_font_description_new();
            if (!desc) {
                std::cerr << "ERROR: Failed to create Pango font description" << std::endl;
                return nullptr;
            }

            // Use default font if family is empty
            const char *fontFamily = style.fontFamily.empty() ? "Arial" : style.fontFamily.c_str();
            pango_font_description_set_family(desc, fontFamily);

            // Ensure reasonable font size
            double fontSize = (style.fontSize > 0) ? style.fontSize : 12.0;
            pango_font_description_set_size(desc, fontSize * PANGO_SCALE);

            // Set weight
            PangoWeight weight = PANGO_WEIGHT_NORMAL;
            switch (style.fontWeight) {
                case FontWeight::Light:
                    weight = PANGO_WEIGHT_LIGHT;
                    break;
                case FontWeight::Bold:
                    weight = PANGO_WEIGHT_BOLD;
                    break;
                case FontWeight::ExtraBold:
                    weight = PANGO_WEIGHT_ULTRABOLD;
                    break;
                default:
                    weight = PANGO_WEIGHT_NORMAL;
                    break;
            }
            pango_font_description_set_weight(desc, weight);

            // Set style
            PangoStyle pangoStyle = PANGO_STYLE_NORMAL;
            switch (style.fontSlant) {
                case FontSlant::Italic:
                    pangoStyle = PANGO_STYLE_ITALIC;
                    break;
                case FontSlant::Oblique:
                    pangoStyle = PANGO_STYLE_OBLIQUE;
                    break;
                default:
                    pangoStyle = PANGO_STYLE_NORMAL;
                    break;
            }
            pango_font_description_set_style(desc, pangoStyle);

            return desc;

        } catch (...) {
            std::cerr << "ERROR: Exception in CreatePangoFont" << std::endl;
            return nullptr;
        }
    }

    void LinuxRenderContext::SetCairoColor(const Color &color) {
        try {
            cairo_set_source_rgba(cairo,
                                  color.r / 255.0f,
                                  color.g / 255.0f,
                                  color.b / 255.0f,
                                  color.a / 255.0f * currentState.globalAlpha);
//            std::cout << "LinuxRenderContext::SetCairoColor r=" << (int) color.r << " g=" << (int) color.g << " b="
//                      << (int) color.b << std::endl;
        } catch (...) {
            std::cerr << "ERROR: Exception in SetCairoColor" << std::endl;
        }
    }


    void LinuxRenderContext::FillEllipse(float x, float y, float w, float h) {
        cairo_save(cairo);
        cairo_translate(cairo, x + w / 2, y + h / 2);
        cairo_scale(cairo, w / 2, h / 2);
        cairo_arc(cairo, 0, 0, 1, 0, 2 * M_PI);
        cairo_restore(cairo);
        cairo_fill(cairo);
    }

    void LinuxRenderContext::DrawEllipse(float x, float y, float w, float h) {
        cairo_save(cairo);
        cairo_translate(cairo, x + w / 2, y + h / 2);
        cairo_scale(cairo, w / 2, h / 2);
        cairo_arc(cairo, 0, 0, 1, 0, 2 * M_PI);
        cairo_restore(cairo);
        cairo_stroke(cairo);
    }

    void LinuxRenderContext::FillLinePath(const std::vector<Point2Df> &points) {
        if (points.empty()) return;

        cairo_move_to(cairo, points[0].x, points[0].y);
        for (size_t i = 1; i < points.size(); ++i) {
            cairo_line_to(cairo, points[i].x, points[i].y);
        }
        cairo_close_path(cairo);
        cairo_fill(cairo);
    }

    void LinuxRenderContext::DrawLinePath(const std::vector<Point2Df> &points, bool closePath) {
        if (points.empty()) return;

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
        //ApplyStrokeStyle(currentState.style);
        cairo_arc(cairo, x, y, radius, startAngle, endAngle);
        cairo_stroke(cairo);
    }

    void LinuxRenderContext::FillArc(float x, float y, float radius, float startAngle, float endAngle) {
        //ApplyFillStyle(currentState.style);
        cairo_move_to(cairo, x, y);
        cairo_arc(cairo, x, y, radius, startAngle, endAngle);
        cairo_close_path(cairo);
        cairo_fill(cairo);
    }

//    void LinuxRenderContext::PathStroke() {
//        ApplyStrokeStyle(currentState.style);
//        cairo_stroke(cairo);
//    }
//    void LinuxRenderContext::PathFill() {
//        ApplyFillStyle(currentState.style);
//        cairo_fill(cairo);
//    }

    void LinuxRenderContext::DrawBezier(const Point2Df &start, const Point2Df &cp1, const Point2Df &cp2,
                                        const Point2Df &end) {
//        ApplyStrokeStyle(currentState.style);
        cairo_move_to(cairo, start.x, start.y);
        cairo_curve_to(cairo, cp1.x, cp1.y, cp2.x, cp2.y, end.x, end.y);
        cairo_stroke(cairo);
    }


    // Path Methods
    void LinuxRenderContext::ClearPath() {
        cairo_new_path(cairo);
    }

    void LinuxRenderContext::ClosePath() {
        cairo_close_path(cairo);
    }

    void LinuxRenderContext::MoveTo(float x, float y) {
        cairo_move_to(cairo, x, y);
    }

    void LinuxRenderContext::RelMoveTo(float x, float y) {
        cairo_rel_move_to(cairo, x, y);
    }

    void LinuxRenderContext::LineTo(float x, float y) {
        cairo_line_to(cairo, x, y);
    }

    void LinuxRenderContext::RelLineTo(float x, float y) {
        cairo_rel_line_to(cairo, x, y);
    }

    void LinuxRenderContext::QuadraticCurveTo(float cpx, float cpy, float x, float y) {
        double cx, cy;
        cairo_get_current_point(cairo, &cx, &cy);

        // Convert quadratic to cubic bezier
        float cp1x = cx + 2.0f/3.0f * (cpx - cx);
        float cp1y = cy + 2.0f/3.0f * (cpy - cy);
        float cp2x = x + 2.0f/3.0f * (cpx - x);
        float cp2y = y + 2.0f/3.0f * (cpy - y);

        cairo_curve_to(cairo, cp1x, cp1y, cp2x, cp2y, x, y);
    }

    void LinuxRenderContext::BezierCurveTo(float cp1x, float cp1y, float cp2x, float cp2y, float x, float y) {
        cairo_curve_to(cairo, cp1x, cp1y, cp2x, cp2y, x, y);
    }

    void LinuxRenderContext::RelBezierCurveTo(float cp1x, float cp1y, float cp2x, float cp2y, float x, float y) {
        cairo_rel_curve_to(cairo, cp1x, cp1y, cp2x, cp2y, x, y);
    }

    void LinuxRenderContext::Arc(float cx, float cy, float radius, float startAngle, float endAngle) {
        cairo_arc(cairo, cx, cy, radius, startAngle, endAngle);
    }

    void LinuxRenderContext::ArcTo(float x1, float y1, float x2, float y2, float radius) {
        // Cairo doesn't have arc_to, so we approximate
        double cx, cy;
        cairo_get_current_point(cairo, &cx, &cy);

        // Calculate the center of the arc
        float dx1 = x1 - cx;
        float dy1 = y1 - cy;
        float dx2 = x2 - x1;
        float dy2 = y2 - y1;

        float a1 = atan2(dy1, dx1);
        float a2 = atan2(dy2, dx2);

        cairo_arc(cairo, x1, y1, radius, a1, a2);
        cairo_line_to(cairo, x2, y2);
    }

    void LinuxRenderContext::Ellipse(float cx, float cy, float rx, float ry, float rotation,
                 float startAngle, float endAngle) {
        cairo_save(cairo);
        cairo_translate(cairo, cx, cy);
        cairo_rotate(cairo, rotation);
        cairo_scale(cairo, rx, ry);
        cairo_arc(cairo, 0, 0, 1, startAngle, endAngle);
        cairo_restore(cairo);
    }

    void LinuxRenderContext::Rect(float x, float y, float width, float height) {
        cairo_rectangle(cairo, x, y, width, height);
    }

    void LinuxRenderContext::RoundedRect(float x, float y, float width, float height, float radius) {
        cairo_new_sub_path(cairo);
        cairo_arc(cairo, x + width - radius, y + radius, radius, -M_PI/2, 0);
        cairo_arc(cairo, x + width - radius, y + height - radius, radius, 0, M_PI/2);
        cairo_arc(cairo, x + radius, y + height - radius, radius, M_PI/2, M_PI);
        cairo_arc(cairo, x + radius, y + radius, radius, M_PI, 3*M_PI/2);
        cairo_close_path(cairo);
    }

    void LinuxRenderContext::Circle(float x, float y, float radius) {
        cairo_arc(cairo, x, y, radius, 0, 2 * M_PI);
    }

    // Drawing Methods
    void LinuxRenderContext::FillPath() {
//        if (currentGradient) {
//            cairo_set_source(cairo, currentGradient);
//        }
        cairo_fill_preserve(cairo);
    }

    void LinuxRenderContext::StrokePath() {
//        if (currentGradient) {
//            cairo_set_source(cairo, currentGradient);
//        }
        cairo_stroke_preserve(cairo);
    }

    void LinuxRenderContext::GetPathExtents(float &x, float &y, float &width, float &height) {
        double x2, y2, x1, y1;
        cairo_path_extents(cairo, &x1, &y1, &x2, &y2);
        x = x1;
        y = y1;
        width = std::abs(x2 - x1);
        height = std::abs(y2 - y1);
    }

// Cached Gradient Pattern Methods
    std::unique_ptr<IDrawingPattern> LinuxRenderContext::CreateLinearGradientPattern(float x1, float y1, float x2, float y2,
                                      const std::vector<GradientStop>& stops) {
        cairo_pattern_t* pattern = cairo_pattern_create_linear(x1, y1, x2, y2);

        for (const auto& stop : stops) {
            cairo_pattern_add_color_stop_rgba(pattern, stop.position,
                stop.color.r / 255.0, stop.color.g / 255.0,
                stop.color.b / 255.0, stop.color.a / 255.0);
        }

        cairo_pattern_set_extend(pattern, CAIRO_EXTEND_PAD);
        if (cairo_pattern_status(pattern) == CAIRO_STATUS_SUCCESS) {
            return std::make_unique<LinuxDrawingPattern>(pattern);
        } else {
            return std::make_unique<LinuxDrawingPattern>(nullptr);
        }
    }

    std::unique_ptr<IDrawingPattern> LinuxRenderContext::CreateRadialGradientPattern(float cx1, float cy1, float r1,
                                  float cx2, float cy2, float r2,
                                  const std::vector<GradientStop>& stops) {
        cairo_pattern_t* pattern = cairo_pattern_create_radial(cx1, cy1, r1, cx2, cy2, r2);

        for (const auto& stop : stops) {
            cairo_pattern_add_color_stop_rgba(pattern, stop.position,
                stop.color.r / 255.0, stop.color.g / 255.0,
                stop.color.b / 255.0, stop.color.a / 255.0);
        }

        cairo_pattern_set_extend(pattern, CAIRO_EXTEND_PAD);

        if (cairo_pattern_status(pattern) == CAIRO_STATUS_SUCCESS) {
            return std::make_unique<LinuxDrawingPattern>(pattern);
        } else {
            return std::make_unique<LinuxDrawingPattern>(nullptr);
        }
    }

    void LinuxRenderContext::PaintWithPattern(std::unique_ptr<IDrawingPattern> pattern) {
        auto ptr = pattern.get();
        if (ptr) {
            auto handle = static_cast<cairo_pattern_t*>(ptr->GetHandle());
            if (handle) {
                cairo_set_source(cairo, handle);
            }
        }
    }

    // Style Methods
    void LinuxRenderContext::PaintWithColor(const UltraCanvas::Color &color) {
        SetCairoColor(color);
    }

    void LinuxRenderContext::SetStrokeWidth(float width) {
//        currentState.style.strokeWidth = width;
        cairo_set_line_width(cairo, width);
    }

    void LinuxRenderContext::SetLineCap(LineCap cap) {
//        currentState.style.lineCap = cap;
        cairo_line_cap_t cairoCap = CAIRO_LINE_CAP_BUTT;
        switch (cap) {
            case LineCap::Round: cairoCap = CAIRO_LINE_CAP_ROUND; break;
            case LineCap::Square: cairoCap = CAIRO_LINE_CAP_SQUARE; break;
            default: break;
        }
        cairo_set_line_cap(cairo, cairoCap);
    }

    void LinuxRenderContext::SetLineJoin(LineJoin join) {
        cairo_line_join_t cairoJoin = CAIRO_LINE_JOIN_MITER;
        switch (join) {
            case LineJoin::Round: cairoJoin = CAIRO_LINE_JOIN_ROUND; break;
            case LineJoin::Bevel: cairoJoin = CAIRO_LINE_JOIN_BEVEL; break;
            default: break;
        }
        cairo_set_line_join(cairo, cairoJoin);
    }

    void LinuxRenderContext::SetMiterLimit(float limit) {
        cairo_set_miter_limit(cairo, limit);
    }

    void LinuxRenderContext::SetLineDash(const std::vector<float>& pattern, float offset) {
        if (pattern.empty()) {
            cairo_set_dash(cairo, nullptr, 0, 0);
        } else {
            std::vector<double> dashes(pattern.begin(), pattern.end());
            cairo_set_dash(cairo, dashes.data(), dashes.size(), offset);
        }
    }

    void LinuxRenderContext::SetAlpha(float alpha) {
        // Get current source and modify alpha
        double r, g, b, a;
        cairo_pattern_t* pattern = cairo_get_source(cairo);
        if (cairo_pattern_get_rgba(pattern, &r, &g, &b, &a) == CAIRO_STATUS_SUCCESS) {
            cairo_set_source_rgba(cairo, r, g, b, alpha);
        }
    }

    // Text Methods
    void LinuxRenderContext::SetFontFace(const std::string& family, FontWeight fw, FontSlant fs) {
        cairo_select_font_face(cairo, family.c_str(),
                               fs == FontSlant::Oblique ? CAIRO_FONT_SLANT_OBLIQUE : (fs == FontSlant::Italic ? CAIRO_FONT_SLANT_ITALIC : CAIRO_FONT_SLANT_NORMAL),
            fw == FontWeight::Bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
        currentState.fontStyle.fontFamily = family;
        currentState.fontStyle.fontWeight = fw;
        currentState.fontStyle.fontSlant = fs;
    }

    void LinuxRenderContext::SetFontSize(float size) {
        cairo_set_font_size(cairo, size);
        currentState.fontStyle.fontSize = size;
    }

    void LinuxRenderContext::SetFontWeight(UltraCanvas::FontWeight fw) {
        SetFontFace(currentState.fontStyle.fontFamily, fw, currentState.fontStyle.fontSlant);
    }

    void LinuxRenderContext::SetFontSlant(UltraCanvas::FontSlant fs) {
        SetFontFace(currentState.fontStyle.fontFamily, currentState.fontStyle.fontWeight, fs);
    }

    void LinuxRenderContext::SetTextAlignment(TextAlignment align) {
        currentState.textStyle.alignment = align;
    }

    void LinuxRenderContext::FillText(const std::string& text, float x, float y) {
        cairo_move_to(cairo, x, y);
        cairo_show_text(cairo, text.c_str());
    }

    void LinuxRenderContext::StrokeText(const std::string& text, float x, float y) {
        cairo_move_to(cairo, x, y);
        cairo_text_path(cairo, text.c_str());
        cairo_stroke(cairo);
    }

    // Image rendering stubs
    // ===== IMAGE RENDERING IMPLEMENTATION =====

    void LinuxRenderContext::DrawImage(const std::string &imagePath, float x, float y) {
        if (imagePath.empty()) {
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

        } catch (const std::exception &e) {
            std::cerr << "LinuxRenderContext::DrawImage: Exception loading image: " << e.what() << std::endl;
        }
    }

    void LinuxRenderContext::DrawImage(const std::string &imagePath, float x, float y, float w, float h) {
        if (imagePath.empty()) {
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

        } catch (const std::exception &e) {
            std::cerr << "LinuxRenderContext::DrawImage: Exception loading image: " << e.what() << std::endl;
        }
    }

    void LinuxRenderContext::DrawImage(const std::string &imagePath, const Rect2Df &srcRect, const Rect2Df &destRect) {
        if (imagePath.empty()) {
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

        } catch (const std::exception &e) {
            std::cerr << "LinuxRenderContext::DrawImage: Exception loading image: " << e.what() << std::endl;
        }
    }

    // ===== ADDITIONAL HELPER METHODS FOR IMAGE RENDERING =====

    bool LinuxRenderContext::IsImageFormatSupported(const std::string &filePath) {
        std::string ext = GetFileExtension(filePath);
        return LinuxImageLoader::IsFormatSupported(ext);
    }

    bool LinuxRenderContext::GetImageDimensions(const std::string &imagePath, int &w, int &h) {
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
        } catch (const std::exception &e) {
            std::cerr << "LinuxRenderContext::GetImageDimensions: Exception: " << e.what() << std::endl;
        }

        return false;
    }

    void LinuxRenderContext::DrawImageWithFilter(const std::string &imagePath, float x, float y, float w, float h,
                                                 cairo_filter_t filter) {
        if (imagePath.empty()) return;

        try {
            ImageLoadResult result = LinuxImageLoader::LoadImage(imagePath);
            if (!result.success) return;

            cairo_save(cairo);

            // Set up scaling pattern with specified filter
            cairo_pattern_t *pattern = cairo_pattern_create_for_surface(result.surface);
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

        } catch (const std::exception &e) {
            std::cerr << "LinuxRenderContext::DrawImageWithFilter: Exception: " << e.what() << std::endl;
        }
    }

    void LinuxRenderContext::DrawImageTiled(const std::string &imagePath, float x, float y, float w, float h) {
        if (imagePath.empty()) return;

        try {
            ImageLoadResult result = LinuxImageLoader::LoadImage(imagePath);
            if (!result.success) return;

            cairo_save(cairo);

            // Create repeating pattern
            cairo_pattern_t *pattern = cairo_pattern_create_for_surface(result.surface);
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

        } catch (const std::exception &e) {
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

    void LinuxRenderContext::UpdateContext(cairo_t *newCairoContext) {
        std::cout << "LinuxRenderContext: Updating Cairo context..." << std::endl;

        if (!newCairoContext) {
            std::cerr << "ERROR: LinuxRenderContext: New Cairo context is null!" << std::endl;
            return;
        }
        cairo_status_t status = cairo_status(newCairoContext);
        if (status != CAIRO_STATUS_SUCCESS) {
            std::cerr << "ERROR: LinuxRenderContext: New Cairo context is invalid: "
                      << cairo_status_to_string(status) << std::endl;
            throw std::runtime_error("LinuxRenderContext: New Cairo context is invalid");
            return;
        }

        // Update the cairo pointer
        cairo = newCairoContext;

        // Re-associate Pango context with new Cairo context
        if (pangoContext) {
            pango_cairo_context_set_resolution(pangoContext, 96.0);

            cairo_font_options_t *fontOptions = cairo_font_options_create();
            cairo_get_font_options(cairo, fontOptions);
            pango_cairo_context_set_font_options(pangoContext, fontOptions);
            cairo_font_options_destroy(fontOptions);
        }

        // Reset state
        ResetState();

        std::cout << "LinuxRenderContext: Cairo context updated successfully" << std::endl;
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

//    void LinuxRenderContext::SetPixel(const Point2Di& point, const Color& color) {
//        SetCairoColor(color);
//        cairo_rectangle(cairo, point.x, point.y, 1, 1);
//        cairo_fill(cairo);
//    }
//
//    Color LinuxRenderContext::GetPixel(const Point2Di& point) {
//        // Cairo doesn't provide direct pixel access
//        return Colors::Black;
//    }

    // Add to LinuxRenderContext class:
    IPixelBuffer *LinuxRenderContext::SavePixelRegion(const Rect2Di &region) {
        if (region.width <= 0 || region.height <= 0) {
            std::cerr << "SavePixelRegion: Invalid parameters" << std::endl;
            return nullptr;
        }
        X11PixelBuffer *buf = nullptr;
        bool success = false;
        std::lock_guard<std::mutex> lock(cairoMutex);

        try {
            cairo_surface_t *surface = cairo_get_target(cairo);
            if (!surface) {
                std::cerr << "SavePixelRegion: No surface available" << std::endl;
                return nullptr;
            }

            cairo_surface_flush(surface);
            cairo_surface_type_t surfaceType = cairo_surface_get_type(surface);

            if (surfaceType == CAIRO_SURFACE_TYPE_IMAGE) {
                // For image surfaces, use traditional buffer (already fast)
                buf = new X11PixelBuffer(region.width, region.height, false);
                success = SaveImageSurface(surface, region, *buf);
            } else if (surfaceType == CAIRO_SURFACE_TYPE_XLIB) {
                buf = new X11PixelBuffer(region.width, region.height, true);
                // For Xlib surfaces, use zero-copy XImage approach
                success = SaveXlibSurface(surface, region, *buf);
            } else {
                std::cerr << "SavePixelRegion: Unsupported surface type" << std::endl;
                return nullptr;
            }

        } catch (const std::exception &e) {
            std::cerr << "SavePixelRegion: Exception: " << e.what() << std::endl;
            delete buf;
            return nullptr;
        }
        if (!success) {
            std::cerr << "SavePixelRegion: Failed" << std::endl;
            delete buf;
            return nullptr;
        }
        return buf;
    }

    bool LinuxRenderContext::RestorePixelRegion(const Rect2Di &region, IPixelBuffer *buf) {
        if (region.width <= 0 || region.height <= 0 || !buf->IsValid()) {
            std::cerr << "RestorePixelRegionZeroCopy: Invalid parameters" << std::endl;
            return false;
        }

        std::lock_guard<std::mutex> lock(cairoMutex);
        X11PixelBuffer *buffer = reinterpret_cast<X11PixelBuffer *>(buf);
        try {
            cairo_surface_t *surface = cairo_get_target(cairo);
            if (!surface) {
                std::cerr << "RestorePixelRegionZeroCopy: No surface available" << std::endl;
                return false;
            }

            cairo_surface_type_t surfaceType = cairo_surface_get_type(surface);

            if (surfaceType == CAIRO_SURFACE_TYPE_IMAGE) {
                return RestoreImageSurface(surface, region, *buffer);
            } else if (surfaceType == CAIRO_SURFACE_TYPE_XLIB) {
                return RestoreXlibSurface(surface, region, *buffer);
            } else {
                std::cerr << "RestorePixelRegionZeroCopy: Unsupported surface type" << std::endl;
                return false;
            }

        } catch (const std::exception &e) {
            std::cerr << "RestorePixelRegionZeroCopy: Exception: " << e.what() << std::endl;
            return false;
        }
    }

    // ===== ZERO-COPY XLIB IMPLEMENTATIONS =====

    bool LinuxRenderContext::SaveXlibSurface(cairo_surface_t *surface, const Rect2Di &region,
                                             X11PixelBuffer &buffer) {
        Display *display = cairo_xlib_surface_get_display(surface);
        Drawable drawable = cairo_xlib_surface_get_drawable(surface);

        if (!display || !drawable) {
            std::cerr << "SaveFromXlibSurfaceZeroCopy: Invalid X11 objects" << std::endl;
            return false;
        }

        int x = std::max(0, static_cast<int>(region.x));
        int y = std::max(0, static_cast<int>(region.y));
        int width = static_cast<int>(region.width);
        int height = static_cast<int>(region.height);

        if (width <= 0 || height <= 0) {
            std::cerr << "SaveFromXlibSurfaceZeroCopy: Invalid region dimensions" << std::endl;
            return false;
        }

        // Get XImage directly from X server - ZERO COPY!
        XImage *ximage = XGetImage(display, drawable, x, y, width, height, AllPlanes, ZPixmap);
        if (!ximage) {
            std::cerr << "SaveFromXlibSurfaceZeroCopy: XGetImage failed" << std::endl;
            return false;
        }

        // Create XImageBuffer wrapper
        auto ximg_buffer = std::make_unique<XImageBuffer>();
        ximg_buffer->ximage = ximage;
        ximg_buffer->display = display;
        ximg_buffer->width = width;
        ximg_buffer->height = height;
        ximg_buffer->size_bytes = width * height * sizeof(uint32_t);

        // Set up direct pixel pointer based on format
        if (ximage->bits_per_pixel == 32 && ximage->byte_order == LSBFirst) {
            // Direct access - no conversion needed!
            ximg_buffer->pixels = reinterpret_cast<uint32_t *>(ximage->data);
        } else {
            // Need format conversion - fall back to traditional buffer
            std::cerr << "SaveFromXlibSurfaceZeroCopy: Format conversion needed, falling back to copy" << std::endl;

            std::vector<uint32_t> traditional_buf(width * height);

            // Convert to standard format
            if (ximage->bits_per_pixel == 32) {
                // Handle big endian
                for (int row = 0; row < height; ++row) {
                    uint32_t *srcRow = reinterpret_cast<uint32_t *>(ximage->data + row * ximage->bytes_per_line);
                    uint32_t *dstRow = traditional_buf.data() + row * width;
                    for (int col = 0; col < width; ++col) {
                        dstRow[col] = __builtin_bswap32(srcRow[col]);
                    }
                }
            } else if (ximage->bits_per_pixel == 24) {
                // Convert 24-bit to 32-bit
                for (int row = 0; row < height; ++row) {
                    unsigned char *srcRow = reinterpret_cast<unsigned char *>(ximage->data +
                                                                              row * ximage->bytes_per_line);
                    uint32_t *dstRow = traditional_buf.data() + row * width;

                    for (int col = 0; col < width; ++col) {
                        unsigned char *pixel = srcRow + col * 3;
                        if (ximage->byte_order == LSBFirst) {
                            dstRow[col] = 0xFF000000 | (pixel[2] << 16) | (pixel[1] << 8) | pixel[0];
                        } else {
                            dstRow[col] = 0xFF000000 | (pixel[0] << 16) | (pixel[1] << 8) | pixel[2];
                        }
                    }
                }
            }

            XDestroyImage(ximage);
            //buffer = X11PixelBuffer(width, height);
            buffer.traditional_buffer = std::move(traditional_buf);
            return true;
        }

        // Success - zero copy!
        buffer.ximage_buffer = std::move(ximg_buffer);

        std::cout << "SaveFromXlibSurfaceZeroCopy: ZERO-COPY save " << width << "x" << height
                  << " region, " << buffer.GetSizeInBytes() << " bytes (direct XImage pointer)" << std::endl;
        return true;
    }

    bool LinuxRenderContext::RestoreXlibSurface(cairo_surface_t *surface, const Rect2Di &region,
                                                X11PixelBuffer &buffer) {
        Display *display = cairo_xlib_surface_get_display(surface);
        Drawable drawable = cairo_xlib_surface_get_drawable(surface);

        if (!display || !drawable) {
            std::cerr << "RestoreXlibSurface: Invalid X11 objects" << std::endl;
            return false;
        }

        int x = static_cast<int>(region.x);
        int y = static_cast<int>(region.y);
        int width = static_cast<int>(region.width);
        int height = static_cast<int>(region.height);

        if (width != buffer.width || height != buffer.height) {
            std::cerr << "RestoreXlibSurface: Size mismatch" << std::endl;
            return false;
        }

        GC gc = XCreateGC(display, drawable, 0, nullptr);
        if (!gc) {
            std::cerr << "RestoreXlibSurface: XCreateGC failed" << std::endl;
            return false;
        }

        bool success = false;

        if (buffer.is_ximage_backed && buffer.ximage_buffer && buffer.ximage_buffer->IsValid()) {
            // ZERO-COPY: Use XImage directly!
            XImage *ximage = buffer.ximage_buffer->ximage;

            std::cout << "RestoreXlibSurface: Using ZERO-COPY XImage restore" << std::endl;

            int result = XPutImage(display, drawable, gc, ximage, 0, 0, x, y, width, height);
            success = (result != BadMatch && result != BadDrawable && result != BadGC && result != BadValue);

            if (!success) {
                std::cerr << "RestoreXlibSurface: XPutImage failed with error " << result << std::endl;
            }
        } else {
            // Traditional buffer - need to create temporary XImage
            std::cout << "RestoreXlibSurface: Using traditional buffer restore" << std::endl;

            Visual *visual = cairo_xlib_surface_get_visual(surface);
            int depth = cairo_xlib_surface_get_depth(surface);

            XImage *ximage = XCreateImage(display, visual, depth, ZPixmap, 0, nullptr,
                                          width, height, 32, 0);
            if (ximage) {
                ximage->data = reinterpret_cast<char *>(const_cast<uint32_t *>(buffer.GetPixelData()));

                int result = XPutImage(display, drawable, gc, ximage, 0, 0, x, y, width, height);
                success = (result != BadMatch && result != BadDrawable && result != BadGC && result != BadValue);

                // Don't let XDestroyImage free our data
                ximage->data = nullptr;
                XDestroyImage(ximage);
            }
        }

        XFreeGC(display, gc);

        if (success) {
            XFlush(display);
            cairo_surface_mark_dirty_rectangle(surface, x, y, width, height);

            std::cout << "RestoreXlibSurface: Restored " << width << "x" << height
                      << " region at (" << x << "," << y << ")" << std::endl;
        }

        return success;
    }

    // ===== COMPATIBILITY FUNCTIONS FOR IMAGE SURFACES =====

    bool LinuxRenderContext::SaveImageSurface(cairo_surface_t *surface, const Rect2Di &region,
                                              X11PixelBuffer &buffer) {

        unsigned char *surfaceData = cairo_image_surface_get_data(surface);
        if (!surfaceData) {
            std::cerr << "SaveImageSurface: No surface data available" << std::endl;
            return false;
        }

        int surfaceStride = cairo_image_surface_get_stride(surface);
        int surfaceWidth = cairo_image_surface_get_width(surface);
        int surfaceHeight = cairo_image_surface_get_height(surface);
        int width = region.width;
        int height = region.height;
        int x = region.x;
        int y = region.y;
        buffer.traditional_buffer.clear();
        buffer.traditional_buffer.reserve(width * height);
        buffer.is_ximage_backed = false;

        for (int row = 0; row < height; ++row) {
            int srcY = y + row;
            if (srcY >= 0 && srcY < surfaceHeight) {
                unsigned char *rowData = surfaceData + (srcY * surfaceStride);
                uint32_t *pixelData = reinterpret_cast<uint32_t *>(rowData);

                for (int col = 0; col < width; ++col) {
                    int srcX = x + col;
                    if (srcX >= 0 && srcX < surfaceWidth) {
                        buffer.traditional_buffer.push_back(pixelData[srcX]);
                    } else {
                        buffer.traditional_buffer.push_back(0); // Transparent pixel for out-of-bounds
                    }
                }
            } else {
                // Fill row with transparent pixels
                for (int col = 0; col < width; ++col) {
                    buffer.traditional_buffer.push_back(0);
                }
            }
        }

        return true;
    }


    bool LinuxRenderContext::RestoreImageSurface(cairo_surface_t *surface, const Rect2Di &region,
                                                 X11PixelBuffer &buffer) {
        if (!buffer.IsValid()) return false;

        uint32_t *pixels = buffer.GetPixelData();
        if (!pixels) return false;

        // Use existing fast image surface restore
        // Get surface data and properties
        unsigned char *data = cairo_image_surface_get_data(surface);
        if (!data) {
            std::cerr << "RestoreToImageSurfaceFast: No surface data" << std::endl;
            return false;
        }

        int surfaceWidth = cairo_image_surface_get_width(surface);
        int surfaceHeight = cairo_image_surface_get_height(surface);
        int stride = cairo_image_surface_get_stride(surface);

        // Validate region and buffer
        int x = region.x;
        int y = region.y;
        int width = region.width;
        int height = region.height;

        if (x < 0 || y < 0 || x + width > surfaceWidth || y + height > surfaceHeight) {
            std::cerr << "RestoreToImageSurfaceFast: Region outside bounds" << std::endl;
            return false;
        }

        if (buffer.traditional_buffer.size() != static_cast<size_t>(width * height)) {
            std::cerr << "RestoreToImageSurfaceFast: Buffer size mismatch" << std::endl;
            return false;
        }

        // Mark surface as dirty before modification
        cairo_surface_mark_dirty_rectangle(surface, x, y, width, height);

        // Fast row-by-row copy using memcpy
        for (int row = 0; row < height; ++row) {
            // Calculate source and destination pointers
            const uint32_t *srcRow = pixels + row * width;
            uint32_t *dstRow = reinterpret_cast<uint32_t *>(data + (y + row) * stride) + x;

            // Fast bulk copy of entire row
            std::memcpy(dstRow, srcRow, width * sizeof(uint32_t));
        }

        // Mark the modified region as dirty
        cairo_surface_mark_dirty_rectangle(surface, x, y, width, height);

        std::cout << "RestoreToImageSurfaceFast: Restored " << width << "x" << height
                  << " region at (" << x << "," << y << ")" << std::endl;
        return true;
    }

    bool LinuxRenderContext::PaintPixelBuffer(int x, int y, int width, int height, uint32_t* pixels) {
        if (!pixels) return false;
        if (!doubleBufferingEnabled) {
            std::cout << "PaintPixelBuffer: Double buffering disabled, no image surface" << std::endl;
            return false;
        }
        // Use existing fast image surface restore
        // Get surface data and properties
        cairo_surface_t * surface = cairo_get_target(cairo);
        unsigned char *data = cairo_image_surface_get_data(surface);
        if (!data) {
            std::cerr << "PaintPixelBuffer: No surface data" << std::endl;
            return false;
        }
        int surfaceWidth = cairo_image_surface_get_width(surface);
        int surfaceHeight = cairo_image_surface_get_height(surface);
        int stride = cairo_image_surface_get_stride(surface);

        if (x < 0 || y < 0 || x + width > surfaceWidth || y + height > surfaceHeight) {
            std::cerr << "PaintPixelBuffer: Region outside bounds" << std::endl;
            return false;
        }

        // Mark surface as dirty before modification
        cairo_surface_mark_dirty_rectangle(surface, x, y, width, height);

        // Fast row-by-row copy using memcpy
        for (int row = 0; row < height; ++row) {
            // Calculate source and destination pointers
            const uint32_t *srcRow = pixels + row * width;
            uint32_t *dstRow = reinterpret_cast<uint32_t *>(data + (y + row) * stride) + x;

            // Fast bulk copy of entire row
            std::memcpy(dstRow, srcRow, width * sizeof(uint32_t));
        }

        std::cout << "PaintPixelBuffer: Painted " << width << "x" << height
                  << " region at (" << x << "," << y << ")" << std::endl;
        return true;
    }

//    bool LinuxRenderContext::SaveRegionAsImage(const Rect2Di& region, const std::string& filename) {
//        std::vector<uint32_t> buffer;
//        if (!SavePixelRegion(region, buffer)) {
//            return false;
//        }
//
//        if (buffer.empty()) {
//            std::cerr << "SaveRegionAsImage: No data to save" << std::endl;
//            return false;
//        }
//
//        std::lock_guard<std::mutex> lock(cairoMutex);
//
//        try {
//            int width = static_cast<int>(region.width);
//            int height = static_cast<int>(region.height);
//            int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
//
//            // Create surface directly from buffer data (avoiding copy)
//            std::vector<unsigned char> imageData(stride * height);
//
//            // Fast conversion using memcpy for row data
//            for (int y = 0; y < height; ++y) {
//                const uint32_t* srcRow = buffer.data() + y * width;
//                uint32_t* dstRow = reinterpret_cast<uint32_t*>(imageData.data() + y * stride);
//                std::memcpy(dstRow, srcRow, width * sizeof(uint32_t));
//            }
//
//            cairo_surface_t* pngSurface = cairo_image_surface_create_for_data(
//                    imageData.data(), CAIRO_FORMAT_ARGB32, width, height, stride);
//
//            if (cairo_surface_status(pngSurface) == CAIRO_STATUS_SUCCESS) {
//                cairo_status_t writeStatus = cairo_surface_write_to_png(pngSurface, filename.c_str());
//                if (writeStatus == CAIRO_STATUS_SUCCESS) {
//                    std::cout << "SaveRegionAsImage: Saved region as " << filename << std::endl;
//                    cairo_surface_destroy(pngSurface);
//                    return true;
//                } else {
//                    std::cerr << "SaveRegionAsImage: PNG write failed: "
//                              << cairo_status_to_string(writeStatus) << std::endl;
//                }
//            } else {
//                std::cerr << "SaveRegionAsImage: Surface creation failed: "
//                          << cairo_status_to_string(cairo_surface_status(pngSurface)) << std::endl;
//            }
//
//            cairo_surface_destroy(pngSurface);
//            return false;
//
//        } catch (const std::exception& e) {
//            std::cerr << "SaveRegionAsImage: Exception: " << e.what() << std::endl;
//            return false;
//        }
//  }

// ===== DOUBLE BUFFERING CONTROL =====

    bool LinuxRenderContext::EnableDoubleBuffering(int width, int height) {
        if (doubleBufferingEnabled) {
            return true;
        }
        // Initialize double buffer
        if (!doubleBuffer.Initialize(width, height, originalWindowSurface)) {
            std::cerr << "EnableDoubleBuffering: Failed to initialize double buffer" << std::endl;
            return false;
        }

        // Switch to staging surface for rendering
        SwitchToStagingSurface();

        doubleBufferingEnabled = true;
        std::cout << "EnableDoubleBuffering: Double buffering enabled for " << width << "x" << height << std::endl;
        return true;
    }

    void LinuxRenderContext::DisableDoubleBuffering() {
        if (!doubleBufferingEnabled) {
            return;
        }

        // Switch back to original window surface
        SwitchToWindowSurface();

        // Cleanup double buffer
        doubleBuffer.Cleanup();

        doubleBufferingEnabled = false;
        std::cout << "DisableDoubleBuffering: Double buffering disabled" << std::endl;
    }

    void LinuxRenderContext::OnWindowResize(int newWidth, int newHeight) {
        if (doubleBufferingEnabled) {
            std::cout << "OnWindowResize: Resizing double buffer to " << newWidth << "x" << newHeight << std::endl;
            // Resize the double buffer
            if (doubleBuffer.Resize(newWidth, newHeight)) {
                // Switch to new staging surface
                SwitchToStagingSurface();
                std::cout << "OnWindowResize: Double buffer resized successfully" << std::endl;
            } else {
                std::cerr << "OnWindowResize: Failed to resize double buffer" << std::endl;
                throw std::runtime_error("OnWindowResize: Failed to resize double buffer");
            }
        }
        ResetState();
    }


    // ===== PRIVATE HELPER METHODS =====
    void LinuxRenderContext::SwitchToStagingSurface() {
        if (!doubleBuffer.IsValid()) {
            std::cerr << "SwitchToStagingSurface: Invalid double buffer" << std::endl;
            return;
        }

        // Get staging surface from double buffer
        cairo_t* stagingContext = static_cast<cairo_t*>(doubleBuffer.GetStagingContext());

        if (!stagingContext) {
            std::cerr << "SwitchToStagingSurface: Failed to get staging context" << std::endl;
            return;
        }

        // Update the base class to use staging surface
        // Note: This assumes LinuxRenderContext has a method to update its Cairo context
        // You may need to modify LinuxRenderContext to support this
        UpdateContext(stagingContext);

        std::cout << "SwitchToStagingSurface: Switched to staging surface for rendering" << std::endl;
    }

    void LinuxRenderContext::SwitchToWindowSurface() {
        if (!originalWindowContext) {
            std::cerr << "SwitchToWindowSurface: No original context available" << std::endl;
            return;
        }

        // Switch back to original window surface
        UpdateContext(originalWindowContext);

        std::cout << "SwitchToWindowSurface: Switched back to window surface" << std::endl;
    }

    XImageBuffer::XImageBuffer(XImageBuffer &&other) noexcept {
        ximage = other.ximage;
        pixels = other.pixels;
        width = other.width;
        height = other.height;
        size_bytes = other.size_bytes;
        display = other.display;

        other.ximage = nullptr;
        other.pixels = nullptr;
        other.width = other.height = 0;
        other.size_bytes = 0;
        other.display = nullptr;
    }

    XImageBuffer &XImageBuffer::operator=(XImageBuffer &&other) noexcept {
        if (this != &other) {
            // Clean up current
            if (ximage) {
                XDestroyImage(ximage);
            }

            // Take ownership
            ximage = other.ximage;
            pixels = other.pixels;
            width = other.width;
            height = other.height;
            size_bytes = other.size_bytes;
            display = other.display;

            // Clear other
            other.ximage = nullptr;
            other.pixels = nullptr;
            other.width = other.height = 0;
            other.size_bytes = 0;
            other.display = nullptr;
        }
        return *this;
    }

    bool X11PixelBuffer::IsValid() const {
        if (is_ximage_backed) {
            return ximage_buffer && ximage_buffer->IsValid();
        } else {
            return width > 0 && height > 0 && !traditional_buffer.empty();
        }
    }

    size_t X11PixelBuffer::GetSizeInBytes() const {
        if (is_ximage_backed && ximage_buffer) {
            return ximage_buffer->size_bytes;
        } else {
            return traditional_buffer.size() * sizeof(uint32_t);
        }
    }

    uint32_t *X11PixelBuffer::GetPixelData() {
        if (is_ximage_backed && ximage_buffer) {
            return ximage_buffer->pixels;
        } else if (!traditional_buffer.empty()) {
            return traditional_buffer.data();
        }
        return nullptr;
    }

    void X11PixelBuffer::Clear() {
        traditional_buffer.clear();
        ximage_buffer.reset();
        width = height = 0;
        is_ximage_backed = false;
    }

    std::vector<uint32_t> X11PixelBuffer::ToTraditionalBuffer() {
        if (is_ximage_backed && ximage_buffer && ximage_buffer->IsValid()) {
            std::vector<uint32_t> result(width * height);
            std::memcpy(result.data(), ximage_buffer->pixels, width * height * sizeof(uint32_t));
            return result;
        } else {
            return traditional_buffer;
        }
    }
} // namespace UltraCanvas