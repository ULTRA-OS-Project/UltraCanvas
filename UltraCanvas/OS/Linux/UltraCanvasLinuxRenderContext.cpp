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
        currentState.translation = Point2D(0, 0);
        currentState.rotation = 0;
        currentState.scale = Point2D(1, 1);
    }

// ===== CLIPPING =====
    void LinuxRenderContext::SetClipRect(const Rect2D& rect) {
        if (!cairo) return;
        cairo_reset_clip(cairo);
        cairo_rectangle(cairo, rect.x, rect.y, rect.width, rect.height);
        cairo_clip(cairo);
        currentState.clipRect = rect;
    }

    void LinuxRenderContext::ClearClipRect() {
        if (!cairo) return;
        cairo_reset_clip(cairo);
        currentState.clipRect = Rect2D(0, 0, 10000, 10000);
    }

    void LinuxRenderContext::IntersectClipRect(const Rect2D& rect) {
        if (!cairo) return;
        cairo_rectangle(cairo, rect.x, rect.y, rect.width, rect.height);
        cairo_clip(cairo);
        currentState.clipRect = rect;
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
    void LinuxRenderContext::DrawLine(const Point2D& start, const Point2D& end) {
        if (!cairo) return;
        ApplyDrawingStyle(currentState.style);
        cairo_move_to(cairo, start.x, start.y);
        cairo_line_to(cairo, end.x, end.y);
        cairo_stroke(cairo);
    }

    void LinuxRenderContext::DrawRectangle(const Rect2D& rect) {
        if (!cairo) return;
        if (!ValidateContext()) {
            std::cerr << "LinuxRenderContext::FillRectangle: Invalid Cairo context" << std::endl;
            return;
        }
        ApplyDrawingStyle(currentState.style);
        cairo_rectangle(cairo, rect.x, rect.y, rect.width, rect.height);
        cairo_stroke(cairo);
    }

    void LinuxRenderContext::FillRectangle(const Rect2D& rect) {
        if (!cairo) return;
        if (!ValidateContext()) {
            std::cerr << "LinuxRenderContext::FillRectangle: Invalid Cairo context" << std::endl;
            return;
        }
        ApplyDrawingStyle(currentState.style);
        std::cout << "LinuxRenderContext::FillRectangle this=" << this << " cairo=" << cairo << std::endl;
        cairo_rectangle(cairo, rect.x, rect.y, rect.width, rect.height);
        cairo_fill(cairo);
    }

    void LinuxRenderContext::DrawRoundedRectangle(const Rect2D& rect, float radius) {
        if (!cairo) return;
        ApplyDrawingStyle(currentState.style);

        // Create rounded rectangle path
        cairo_new_sub_path(cairo);
        cairo_arc(cairo, rect.x + rect.width - radius, rect.y + radius, radius, -M_PI_2, 0);
        cairo_arc(cairo, rect.x + rect.width - radius, rect.y + rect.height - radius, radius, 0, M_PI_2);
        cairo_arc(cairo, rect.x + radius, rect.y + rect.height - radius, radius, M_PI_2, M_PI);
        cairo_arc(cairo, rect.x + radius, rect.y + radius, radius, M_PI, 3 * M_PI_2);
        cairo_close_path(cairo);

        cairo_stroke(cairo);
    }

    void LinuxRenderContext::FillRoundedRectangle(const Rect2D& rect, float radius) {
        if (!cairo) return;
        ApplyDrawingStyle(currentState.style);

        // Create rounded rectangle path
        cairo_new_sub_path(cairo);
        cairo_arc(cairo, rect.x + rect.width - radius, rect.y + radius, radius, -M_PI_2, 0);
        cairo_arc(cairo, rect.x + rect.width - radius, rect.y + rect.height - radius, radius, 0, M_PI_2);
        cairo_arc(cairo, rect.x + radius, rect.y + rect.height - radius, radius, M_PI_2, M_PI);
        cairo_arc(cairo, rect.x + radius, rect.y + radius, radius, M_PI, 3 * M_PI_2);
        cairo_close_path(cairo);

        cairo_fill(cairo);
    }

    void LinuxRenderContext::DrawCircle(const Point2D& center, float radius) {
        if (!cairo) return;
        ApplyDrawingStyle(currentState.style);
        cairo_arc(cairo, center.x, center.y, radius, 0, 2 * M_PI);
        cairo_stroke(cairo);
    }

    void LinuxRenderContext::FillCircle(const Point2D& center, float radius) {
        if (!cairo) return;
        ApplyDrawingStyle(currentState.style);
        cairo_arc(cairo, center.x, center.y, radius, 0, 2 * M_PI);
        cairo_fill(cairo);
    }

// ===== TEXT RENDERING (FIXED) =====
    void LinuxRenderContext::DrawText(const std::string& text, const Point2D& position) {
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

            cairo_move_to(cairo, position.x, position.y);
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

    void LinuxRenderContext::DrawTextInRect(const std::string& text, const Rect2D& rect) {
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
            pango_layout_set_width(layout, rect.width * PANGO_SCALE);
            pango_layout_set_height(layout, rect.height * PANGO_SCALE);

            // Set alignment
            PangoAlignment alignment = PANGO_ALIGN_LEFT;
            switch (currentState.textStyle.alignment) {
                case TextAlign::Center: alignment = PANGO_ALIGN_CENTER; break;
                case TextAlign::Right: alignment = PANGO_ALIGN_RIGHT; break;
                case TextAlign::Justify: alignment = PANGO_ALIGN_LEFT; break;
                default: alignment = PANGO_ALIGN_LEFT; break;
            }
            pango_layout_set_alignment(layout, alignment);

            cairo_move_to(cairo, rect.x, rect.y);
            pango_cairo_show_layout(cairo, layout);

            pango_font_description_free(desc);
            g_object_unref(layout);

        } catch (...) {
            std::cerr << "ERROR: Exception in DrawTextInRect" << std::endl;
        }
    }

    Point2D LinuxRenderContext::MeasureText(const std::string& text) {
        if (!pangoContext || text.empty()) {
            return Point2D(0, 0);
        }

        try {
            PangoLayout* layout = pango_layout_new(pangoContext);
            if (!layout) return Point2D(0, 0);

            PangoFontDescription* desc = CreatePangoFont(currentState.textStyle);
            if (!desc) {
                g_object_unref(layout);
                return Point2D(0, 0);
            }

            pango_layout_set_font_description(layout, desc);
            pango_layout_set_text(layout, text.c_str(), -1);

            int width, height;
            pango_layout_get_pixel_size(layout, &width, &height);

            pango_font_description_free(desc);
            g_object_unref(layout);

            return Point2D(width, height);

        } catch (...) {
            std::cerr << "ERROR: Exception in MeasureText" << std::endl;
            return Point2D(0, 0);
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

        // Set stroke properties
        if (style.hasStroke) {
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

            // Set stroke color for stroke operations
            SetCairoColor(style.strokeColor);
        } else {
            // Set fill color - THIS WAS MISSING!
            SetCairoColor(style.fillColor);
        }
    }

    void LinuxRenderContext::ApplyTextStyle(const TextStyle& style) {
        if (!cairo) return;
        SetCairoColor(style.textColor);
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
        } catch (...) {
            std::cerr << "ERROR: Exception in SetCairoColor" << std::endl;
        }
    }

    // Stub implementations for unimplemented methods
    void LinuxRenderContext::DrawEllipse(const Rect2D& rect) {
        if (!cairo) return;
        ApplyDrawingStyle(currentState.style);
        cairo_save(cairo);
        cairo_translate(cairo, rect.x + rect.width/2, rect.y + rect.height/2);
        cairo_scale(cairo, rect.width/2, rect.height/2);
        cairo_arc(cairo, 0, 0, 1, 0, 2 * M_PI);
        cairo_restore(cairo);
        cairo_stroke(cairo);
    }

    void LinuxRenderContext::FillEllipse(const Rect2D& rect) {
        if (!cairo) return;
        ApplyDrawingStyle(currentState.style);
        cairo_save(cairo);
        cairo_translate(cairo, rect.x + rect.width/2, rect.y + rect.height/2);
        cairo_scale(cairo, rect.width/2, rect.height/2);
        cairo_arc(cairo, 0, 0, 1, 0, 2 * M_PI);
        cairo_restore(cairo);
        cairo_fill(cairo);
    }

    void LinuxRenderContext::DrawArc(const Point2D& center, float radius, float startAngle, float endAngle) {
        if (!cairo) return;
        ApplyDrawingStyle(currentState.style);
        cairo_arc(cairo, center.x, center.y, radius, startAngle, endAngle);
        cairo_stroke(cairo);
    }

    void LinuxRenderContext::FillArc(const Point2D& center, float radius, float startAngle, float endAngle) {
        if (!cairo) return;
        ApplyDrawingStyle(currentState.style);
        cairo_move_to(cairo, center.x, center.y);
        cairo_arc(cairo, center.x, center.y, radius, startAngle, endAngle);
        cairo_close_path(cairo);
        cairo_fill(cairo);
    }

    void LinuxRenderContext::DrawBezier(const Point2D& start, const Point2D& cp1, const Point2D& cp2, const Point2D& end) {
        if (!cairo) return;
        ApplyDrawingStyle(currentState.style);
        cairo_move_to(cairo, start.x, start.y);
        cairo_curve_to(cairo, cp1.x, cp1.y, cp2.x, cp2.y, end.x, end.y);
        cairo_stroke(cairo);
    }

    void LinuxRenderContext::DrawPath(const std::vector<Point2D>& points, bool closePath) {
        if (!cairo || points.empty()) return;

        ApplyDrawingStyle(currentState.style);
        cairo_move_to(cairo, points[0].x, points[0].y);

        for (size_t i = 1; i < points.size(); ++i) {
            cairo_line_to(cairo, points[i].x, points[i].y);
        }

        if (closePath) {
            cairo_close_path(cairo);
        }

        cairo_stroke(cairo);
    }

    void LinuxRenderContext::FillPath(const std::vector<Point2D>& points) {
        if (!cairo || points.empty()) return;

        ApplyDrawingStyle(currentState.style);
        cairo_move_to(cairo, points[0].x, points[0].y);

        for (size_t i = 1; i < points.size(); ++i) {
            cairo_line_to(cairo, points[i].x, points[i].y);
        }

        cairo_close_path(cairo);
        cairo_fill(cairo);
    }

    // Image rendering stubs
    // ===== IMAGE RENDERING IMPLEMENTATION =====

    void LinuxRenderContext::DrawImage(const std::string& imagePath, const Point2D& position) {
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
                cairo_set_source_surface(cairo, result.surface, position.x, position.y);
                cairo_paint_with_alpha(cairo, currentState.globalAlpha);
            } else {
                // Direct drawing for better performance when no transparency
                cairo_set_source_surface(cairo, result.surface, position.x, position.y);
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

    void LinuxRenderContext::DrawImage(const std::string& imagePath, const Rect2D& destRect) {
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
            float scaleX = destRect.width / static_cast<float>(result.width);
            float scaleY = destRect.height / static_cast<float>(result.height);

            // Apply transformations
            cairo_translate(cairo, destRect.x, destRect.y);
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

    void LinuxRenderContext::DrawImage(const std::string& imagePath, const Rect2D& srcRect, const Rect2D& destRect) {
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

    Point2D LinuxRenderContext::GetImageDimensions(const std::string& imagePath) {
        if (imagePath.empty()) {
            return Point2D(0, 0);
        }

        try {
            ImageLoadResult result = LinuxImageLoader::LoadImage(imagePath);
            if (result.success) {
                Point2D dimensions(result.width, result.height);
                cairo_surface_destroy(result.surface);
                return dimensions;
            }
        } catch (const std::exception& e) {
            std::cerr << "LinuxRenderContext::GetImageDimensions: Exception: " << e.what() << std::endl;
        }

        return Point2D(0, 0);
    }

    void LinuxRenderContext::DrawImageWithFilter(const std::string& imagePath, const Rect2D& destRect,
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
                                    static_cast<double>(result.width) / destRect.width,
                                    static_cast<double>(result.height) / destRect.height);
            cairo_matrix_translate(&matrix, -destRect.x, -destRect.y);
            cairo_pattern_set_matrix(pattern, &matrix);

            // Set source and paint
            cairo_set_source(cairo, pattern);
            cairo_rectangle(cairo, destRect.x, destRect.y, destRect.width, destRect.height);

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

    void LinuxRenderContext::DrawImageTiled(const std::string& imagePath, const Rect2D& fillRect) {
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
            cairo_rectangle(cairo, fillRect.x, fillRect.y, fillRect.width, fillRect.height);

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

//    void LinuxRenderContext::DrawImage(const std::string& imagePath, const Point2D& position) {
//        // Placeholder - would need image loading library
//    }
//
//    void LinuxRenderContext::DrawImage(const std::string& imagePath, const Rect2D& destRect) {
//        // Placeholder - would need image loading library
//    }
//
//    void LinuxRenderContext::DrawImage(const std::string& imagePath, const Rect2D& srcRect, const Rect2D& destRect) {
//        // Placeholder - would need image loading library
//    }

    void LinuxRenderContext::SetPixel(const Point2D& point, const Color& color) {
        if (!cairo) return;
        SetCairoColor(color);
        cairo_rectangle(cairo, point.x, point.y, 1, 1);
        cairo_fill(cairo);
    }

    Color LinuxRenderContext::GetPixel(const Point2D& point) {
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