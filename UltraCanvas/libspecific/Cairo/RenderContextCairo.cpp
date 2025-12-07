// libspecific/Cairo/RenderContextCairo.cpp
// Cairo support implementation for UltraCanvas Framework
// Version: 1.0.2 - Fixed null pointer crashes and Pango initialization
// Last Modified: 2025-07-14
// Author: UltraCanvas Framework

#include "RenderContextCairo.h"
#include <cstring>
#include <cmath>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace UltraCanvas {

    RenderContextCairo::RenderContextCairo(cairo_surface_t *surf, int width, int height, bool enableDoubleBuffering)
        : targetSurface(nullptr), surfaceWidth(0), surfaceHeight(0), stagingSurface(nullptr), pangoContext(nullptr), cairo(nullptr), targetContext(nullptr), destroying(false) {

        SetTargetSurface(surf, width, height);

        // Initialize Pango for text rendering with proper error checking
        try {
            std::cout << "RenderContextCairo: Initializing Pango..." << std::endl;

            auto fontMap = pango_cairo_font_map_get_default();
            if (!fontMap) {
                std::cerr << "ERROR: Failed to get default Pango font map" << std::endl;
                throw std::runtime_error("RenderContextCairo: Failed to get Pango font map");
            }
            std::cout << "RenderContextCairo: Got Pango font map: " << fontMap << std::endl;

            pangoContext = pango_font_map_create_context(fontMap);
            if (!pangoContext) {
                std::cerr << "ERROR: Failed to create Pango context" << std::endl;
                throw std::runtime_error("RenderContextCairo: Failed to create Pango context");
            }
            std::cout << "RenderContextCairo: Created Pango context: " << pangoContext << std::endl;

            // Associate Pango context with Cairo context
            pango_cairo_context_set_resolution(pangoContext, 96.0);  // Standard DPI

            // Get and set font options from Cairo
            cairo_font_options_t *fontOptions = cairo_font_options_create();
            cairo_get_font_options(targetContext, fontOptions);
            pango_cairo_context_set_font_options(pangoContext, fontOptions);
            cairo_font_options_destroy(fontOptions);

            std::cout << "RenderContextCairo: Pango initialization complete" << std::endl;

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
            CreateStagingSurface();
            SwitchToSurface(stagingSurface);
        }
        // Initialize default state
        ResetState();
        std::cout << "RenderContextCairo: Initialization complete" << std::endl;
    }

    RenderContextCairo::~RenderContextCairo() {
        std::cout << "RenderContextCairo: Destroying..." << std::endl;
        destroying = true;

        // Clear the state stack to prevent any pending cairo operations
        stateStack.clear();

        // Clean up Pango context
        if (pangoContext) {
            g_object_unref(pangoContext);
            pangoContext = nullptr;
        }

        // Null the cairo pointer to prevent any accidental access
        // Note: We don't own the cairo context, so don't destroy it
        if (stagingSurface) {
            cairo_surface_destroy(stagingSurface);
        }
        cairo_destroy(targetContext);
        cairo_destroy(cairo);

        std::cout << "RenderContextCairo: Destruction complete" << std::endl;
    }

    void RenderContextCairo::SetTargetSurface(cairo_surface_t* surf, int w, int h) {
        cairo_status_t status = cairo_surface_status(surf);
        if (status != CAIRO_STATUS_SUCCESS) {
            std::cerr << "ERROR: RenderContextCairo: Cairo target surface is invalid: " << cairo_status_to_string(status)
                      << std::endl;
            throw std::runtime_error("RenderContextCairo: Invalid target Cairo surface");
        }

        if (targetContext) {
            cairo_destroy(targetContext);
        }
        if (cairo) {
            cairo_destroy(cairo);
        }

        surfaceWidth = w;
        surfaceHeight = h;
        targetSurface = surf;
        targetContext = cairo_create(targetSurface);

        // Check Cairo context status
        status = cairo_status(targetContext);
        if (status != CAIRO_STATUS_SUCCESS) {
            std::cerr << "ERROR: RenderContextCairo: Cairo target context is invalid: " << cairo_status_to_string(status)
                      << std::endl;
            throw std::runtime_error("RenderContextCairo: Invalid target Cairo context");
        }

        cairo = cairo_create(targetSurface);
        status = cairo_status(cairo);
        if (status != CAIRO_STATUS_SUCCESS) {
            std::cerr << "ERROR: RenderContextCairo: Cairo context is invalid: " << cairo_status_to_string(status)
                      << std::endl;
            throw std::runtime_error("RenderContextCairo: Invalid Cairo context");
        }
    }

    bool RenderContextCairo::CreateStagingSurface() {
        // Create image surface for staging (back buffer)
        stagingSurface = cairo_surface_create_similar(targetSurface, CAIRO_CONTENT_COLOR_ALPHA, surfaceWidth, surfaceHeight);

        if (cairo_surface_status(stagingSurface) != CAIRO_STATUS_SUCCESS) {
            std::cerr << "RenderContextCairo: Failed to create staging surface" << std::endl;
            return false;
        }

        return true;
    }

    bool RenderContextCairo::ResizeStagingSurface(int newWidth, int newHeight) {
        std::lock_guard<std::mutex> lock(cairoMutex);

        if (newWidth <= 0 || newHeight <= 0 || !stagingSurface) {
            return false;
        }

        if (newWidth == surfaceWidth && newHeight == surfaceHeight) {
            return true; // No change needed
        }

        // Update dimensions

        int oldSurfaceWidth = surfaceWidth;
        int oldSurfaceHeight = surfaceHeight;

        surfaceWidth = newWidth;
        surfaceHeight = newHeight;

        auto oldStagingSurface = stagingSurface;

        // Recreate staging surface with new dimensions
        if (!CreateStagingSurface()) {
            return false;
        }
        SwitchToSurface(stagingSurface);

        int copyWidth = std::min(surfaceWidth, oldSurfaceWidth);
        int copyHeight = std::min(surfaceHeight, oldSurfaceHeight);
        if (copyWidth > 0 && copyHeight > 0) {
            // Actually perform the copy operation
            cairo_save(cairo);
            cairo_set_source_surface(cairo, oldStagingSurface, 0, 0);
            cairo_rectangle(cairo, 0, 0, copyWidth, copyHeight);
            cairo_clip(cairo);
            cairo_paint(cairo);
            cairo_restore(cairo);

        }

        cairo_surface_destroy(oldStagingSurface);

        std::cout << "ResizeStagingSurface: Resized to " << newWidth << "x" << newHeight << std::endl;
        return true;
    }

    void RenderContextCairo::SwitchToSurface(cairo_surface_t* surf) {
        if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
            std::cerr << "SwitchToSurface: Invalid surface" << std::endl;
            return;
        }

        if (cairo) {
            cairo_destroy(cairo);
        }
        cairo = cairo_create(surf);

        if (cairo_status(cairo) != CAIRO_STATUS_SUCCESS) {
            std::cerr << "SwitchToSurface: Invalid context" << std::endl;
        }
        ResetState();
    }

// ===== STATE MANAGEMENT =====
    void RenderContextCairo::PushState() {
        stateStack.push_back(currentState);
        cairo_save(cairo);
    }

    void RenderContextCairo::PopState() {
        if (!stateStack.empty()) {
            currentState = stateStack.back();
            stateStack.pop_back();
        } else {
            std::cout << "RenderContextCairo::PopState() stateStack empty!" << std::endl;
        }
        cairo_restore(cairo);
    }

    void RenderContextCairo::ResetState() {
        currentState = RenderState();
        stateStack.clear();
        if (cairo) {
            cairo_identity_matrix(cairo);
            cairo_reset_clip(cairo);
        }
    }

// ===== TRANSFORMATION =====
    void RenderContextCairo::Translate(float x, float y) {
        if (x != 0 || y != 0) {
            cairo_translate(cairo, x, y);
            currentState.translation.x += x;
            currentState.translation.y += y;
        }
    }

    void RenderContextCairo::Rotate(float angle) {
        cairo_rotate(cairo, angle);
        currentState.rotation += angle;
    }

    void RenderContextCairo::Scale(float sx, float sy) {
        cairo_scale(cairo, sx, sy);
        currentState.scale.x *= sx;
        currentState.scale.y *= sy;
    }

    void RenderContextCairo::SetTransform(float a, float b, float c, float d, float e, float f) {
        cairo_matrix_t matrix;
        cairo_matrix_init(&matrix, a, b, c, d, e, f);
        cairo_set_matrix(cairo, &matrix);
    }

    void RenderContextCairo::Transform(float a, float b, float c, float d, float e, float f) {
        cairo_matrix_t matrix;
        cairo_matrix_init(&matrix, a, b, c, d, e, f);
        cairo_transform(cairo, &matrix);
    }

    void RenderContextCairo::ResetTransform() {
        cairo_identity_matrix(cairo);
        currentState.translation = Point2Df(0, 0);
        currentState.rotation = 0;
        currentState.scale = Point2Df(1, 1);
    }

    void RenderContextCairo::ClearClipRect() {
        std::cout << "RenderContextCairo::ClearClipRect - clearing clip region" << std::endl;

        // Reset the clip region to cover the entire surface
        cairo_reset_clip(cairo);
//        std::cout << "RenderContextCairo::ClearClipRect - clip region cleared successfully" << std::endl;
    }

    void RenderContextCairo::ClipRect(float x, float y, float w, float h) {
//        std::cout << "RenderContextCairo::ClipRect - setting clip to "
//                  << x << "," << y << " " << w << "x" << h << std::endl;
        cairo_rectangle(cairo, x, y, w, h);
        cairo_clip(cairo);
    }

    void RenderContextCairo::ClipPath() {
        cairo_clip(cairo);
    }

    void RenderContextCairo::ClipRoundedRectangle(
            float x, float y, float width, float height,
            float borderTopLeftRadius, float borderTopRightRadius,
            float borderBottomRightRadius, float borderBottomLeftRadius) {
        // Clamp radii to prevent overlapping
        float maxRadiusX = width / 2.0;
        float maxRadiusY = height / 2.0;

        float topLeftRadius = std::min({borderTopLeftRadius, maxRadiusX, maxRadiusY});
        float topRightRadius = std::min({borderTopRightRadius, maxRadiusX, maxRadiusY});
        float bottomRightRadius = std::min({borderBottomRightRadius, maxRadiusX, maxRadiusY});
        float bottomLeftRadius = std::min({borderBottomLeftRadius, maxRadiusX, maxRadiusY});

        // Adjust if corners overlap
        float topScale = 1.0;
        float bottomScale = 1.0;
        float leftScale = 1.0;
        float rightScale = 1.0;

        if (topLeftRadius + topRightRadius > width) {
            topScale = width / (topLeftRadius + topRightRadius);
        }
        if (bottomLeftRadius + bottomRightRadius > width) {
            bottomScale = width / (bottomLeftRadius + bottomRightRadius);
        }
        if (topLeftRadius + bottomLeftRadius > height) {
            leftScale = height / (topLeftRadius + bottomLeftRadius);
        }
        if (topRightRadius + bottomRightRadius > height) {
            rightScale = height / (topRightRadius + bottomRightRadius);
        }

        float scale = std::min({topScale, bottomScale, leftScale, rightScale});
        topLeftRadius *= scale;
        topRightRadius *= scale;
        bottomRightRadius *= scale;
        bottomLeftRadius *= scale;

        cairo_save(cairo);

        // Create the rounded rectangle path
        cairo_new_path(cairo);

        // Top left corner
        if (topLeftRadius > 0) {
            cairo_arc(cairo, x + topLeftRadius, y + topLeftRadius,
                      topLeftRadius, M_PI, 3 * M_PI / 2);
        } else {
            cairo_move_to(cairo, x, y);
            cairo_line_to(cairo, x + width - topRightRadius, y);
        }

        // Top right corner
        if (topRightRadius > 0) {
            cairo_arc(cairo, x + width - topRightRadius, y + topRightRadius,
                      topRightRadius, 3 * M_PI / 2, 0);
        } else {
            cairo_line_to(cairo, x + width, y + height - bottomRightRadius);
        }

        // Bottom right corner
        if (bottomRightRadius > 0) {
            cairo_arc(cairo, x + width - bottomRightRadius, y + height - bottomRightRadius,
                      bottomRightRadius, 0, M_PI / 2);
        } else {
            cairo_line_to(cairo, x + bottomLeftRadius, y + height);
        }

        // Bottom left corner
        if (bottomLeftRadius > 0) {
            cairo_arc(cairo, x + bottomLeftRadius, y + height - bottomLeftRadius,
                      bottomLeftRadius, M_PI / 2, M_PI);
        } else {
            cairo_line_to(cairo, x, y + topLeftRadius);
        }

        cairo_close_path(cairo);

        // Clip to the rounded rectangle for borders
        cairo_clip(cairo);
    }

// ===== BASIC DRAWING =====
    void RenderContextCairo::FillRectangle(float x, float y, float w, float h) {
//        std::cout << "RenderContextCairo::FillRectangle this=" << this << " cairo=" << cairo << std::endl;

        // *** CRITICAL FIX: Apply fill style explicitly ***
        //ApplyFillStyle(currentState.style);

        cairo_rectangle(cairo, x, y, w, h);
        Fill();

//        std::cout << "RenderContextCairo::FillRectangle: Complete" << std::endl;
    }

    void RenderContextCairo::DrawRectangle(float x, float y, float w, float h) {
//        std::cout << "RenderContextCairo::DrawRectangle this=" << this << " cairo=" << cairo << std::endl;

        // *** CRITICAL FIX: Apply stroke style explicitly ***
        //ApplyStrokeStyle(currentState.style);

        cairo_rectangle(cairo, x, y, w, h);
        Stroke();

//        std::cout << "RenderContextCairo::DrawRectangle: Complete" << std::endl;
    }

    void RenderContextCairo::FillRoundedRectangle(float x, float y, float w, float h, float radius) {
//        std::cout << "RenderContextCairo::FillRoundedRectangle" << std::endl;

        // *** Apply fill style ***
        //ApplyFillStyle(currentState.style);

        // Create rounded rectangle path
        cairo_new_sub_path(cairo);
        cairo_arc(cairo, x + w - radius, y + radius, radius, -M_PI_2, 0);
        cairo_arc(cairo, x + w - radius, y + h - radius, radius, 0, M_PI_2);
        cairo_arc(cairo, x + radius, y + h - radius, radius, M_PI_2, M_PI);
        cairo_arc(cairo, x + radius, y + radius, radius, M_PI, 3 * M_PI_2);
        cairo_close_path(cairo);

        Fill();
    }

    void RenderContextCairo::DrawRoundedRectangle(float x, float y, float w, float h, float radius) {
//        std::cout << "RenderContextCairo::DrawRoundedRectangle" << std::endl;

        // *** Apply stroke style ***
        //ApplyStrokeStyle(currentState.style);

        // Create rounded rectangle path
        cairo_new_sub_path(cairo);
        cairo_arc(cairo, x + w - radius, y + radius, radius, -M_PI_2, 0);
        cairo_arc(cairo, x + w - radius, y + h - radius, radius, 0, M_PI_2);
        cairo_arc(cairo, x + radius, y + h - radius, radius, M_PI_2, M_PI);
        cairo_arc(cairo, x + radius, y + radius, radius, M_PI, 3 * M_PI_2);
        cairo_close_path(cairo);

        Stroke();
    }

    void RenderContextCairo::FillCircle(float x, float y, float radius) {
//        std::cout << "RenderContextCairo::FillCircle" << std::endl;

        // *** Apply fill style ***
        //ApplyFillStyle(currentState.style);
        cairo_arc(cairo, x, y, radius, 0, 2 * M_PI);
        Fill();
    }

    void RenderContextCairo::DrawCircle(float x, float y, float radius) {
//        std::cout << "RenderContextCairo::DrawCircle" << std::endl;

        // *** Apply stroke style ***
        //ApplyStrokeStyle(currentState.style);
        cairo_arc(cairo, x, y, radius, 0, 2 * M_PI);
        Stroke();
    }

    void RenderContextCairo::DrawLine(float start_x, float start_y, float end_x, float end_y) {
//        std::cout << "RenderContextCairo::DrawLine" << std::endl;

        // *** Apply stroke style ***
        //ApplyStrokeStyle(currentState.style);

        cairo_move_to(cairo, start_x, start_y);
        cairo_line_to(cairo, end_x, end_y);
        Stroke();
    }

    PangoLayout* RenderContextCairo::CreatePangoLayout(PangoFontDescription *desc, int w, int h) {
        PangoLayout *layout = pango_layout_new(pangoContext);
        if (!layout) {
            std::cerr << "ERROR: Failed to create Pango layout" << std::endl;
            return nullptr;
        }

        pango_layout_set_font_description(layout, desc);
        if (w > 0 || h > 0) {
            if (w > 0) {
                pango_layout_set_width(layout, w * PANGO_SCALE);
            }
            if (h > 0) {
                pango_layout_set_height(layout, h * PANGO_SCALE);
            }
            pango_layout_set_line_spacing(layout, currentState.textStyle.lineHeight);

            PangoAlignment alignment = PANGO_ALIGN_LEFT;

            switch (currentState.textStyle.alignment) {
                case TextAlignment::Center:
                    pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
                    break;
                case TextAlignment::Right:
                    pango_layout_set_alignment(layout, PANGO_ALIGN_RIGHT);
                    break;
                case TextAlignment::Justify:
                    pango_layout_set_justify(layout, true);
                    break;
                default:
                    pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
                    break;
            }

            switch (currentState.textStyle.wrap) {
                case TextWrap::WrapNone:
//                    pango_layout_set_wrap(layout, PANGO_WRAP_NONE);
                    pango_layout_set_ellipsize(layout, PangoEllipsizeMode::PANGO_ELLIPSIZE_END);
                    break;
                case TextWrap::WrapWord:
                    pango_layout_set_wrap(layout, PANGO_WRAP_WORD);
                    pango_layout_set_ellipsize(layout, PangoEllipsizeMode::PANGO_ELLIPSIZE_NONE);
                    break;
                case TextWrap::WrapWordChar:
                    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
                    pango_layout_set_ellipsize(layout, PangoEllipsizeMode::PANGO_ELLIPSIZE_NONE);
                    break;
                case TextWrap::WrapChar:
                    pango_layout_set_wrap(layout, PANGO_WRAP_CHAR);
                    pango_layout_set_ellipsize(layout, PangoEllipsizeMode::PANGO_ELLIPSIZE_NONE);
                    break;
            }
        }
        return layout;
    }

    // ===== TEXT RENDERING =====
    void RenderContextCairo::DrawText(const std::string &text, float x, float y) {
        // Comprehensive null checks
        if (text.empty()) {
            return; // Nothing to draw
        }

        try {
            std::cout << "DrawText: Rendering '" << text << "' at (" << x << "," << y << ")" << std::endl;
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

            if (currentState.textStyle.isMarkup) {
                pango_layout_set_markup(layout, text.c_str(), -1);
            } else {
                pango_layout_set_text(layout, text.c_str(), -1);
            }

            ApplyTextSource();

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

    void RenderContextCairo::DrawTextInRect(const std::string &text, float x, float y, float w, float h) {
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
            if (currentState.textStyle.isMarkup) {
                pango_layout_set_markup(layout, text.c_str(), -1);
            } else {
                pango_layout_set_text(layout, text.c_str(), -1);
            }

            if (currentState.textStyle.verticalAlignement == TextVerticalAlignement::Middle) {
                int w1, h1;
                pango_layout_get_pixel_size(layout, &w1, &h1);
                cairo_move_to(cairo, x, y + ((h - h1) / 2));
//                cairo_move_to(cairo, x, y);
            } else {
                cairo_move_to(cairo, x, y);
            }

            ApplyTextSource();

            pango_cairo_show_layout(cairo, layout);

            pango_font_description_free(desc);
            g_object_unref(layout);

        } catch (...) {
            std::cerr << "ERROR: Exception in DrawTextInRect" << std::endl;
        }
    }

    bool RenderContextCairo::GetTextDimensions(const std::string &text, int rectWidth, int rectHeight, int& retWidth, int &retHeight) {
        retWidth = 0;
        retHeight = 0;
        if (!pangoContext || text.empty()) {
            return false;
        }

        try {
            PangoFontDescription *desc = CreatePangoFont(currentState.fontStyle);
            if (!desc) {
                std::cerr << "ERROR: Failed to create Pango font description" << std::endl;
                return false;
            }
            PangoLayout *layout = CreatePangoLayout(desc, rectWidth, rectHeight);
            if (!layout) {
                pango_font_description_free(desc);
                std::cerr << "ERROR: Failed to create Pango layout" << std::endl;
                return false;
            }
            if (currentState.textStyle.isMarkup) {
                pango_layout_set_markup(layout, text.c_str(), -1);
            } else {
                pango_layout_set_text(layout, text.c_str(), -1);
            }

            int width, height;
            pango_layout_get_pixel_size(layout, &width, &height);

            pango_font_description_free(desc);
            g_object_unref(layout);

            retWidth = width;
            retHeight = height;
            return true;

        } catch (...) {
            std::cerr << "ERROR: Exception in GetTextLineDimensions" << std::endl;
            return false;
        }
    }

    bool RenderContextCairo::GetTextLineDimensions(const std::string &text, int &w, int &h) {
        return GetTextDimensions(text, 0, 0, w, h);
    }


    int RenderContextCairo::GetTextIndexForXY(const std::string &text, int x, int y, int w, int h) {
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
    void RenderContextCairo::Clear(const Color &color) {
        cairo_save(cairo);
        cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
        SetCairoColor(color);
        cairo_paint(cairo);
        cairo_restore(cairo);
    }

    void RenderContextCairo::SwapBuffers() {
        std::lock_guard<std::mutex> lock(cairoMutex);
        if (stagingSurface) {
            cairo_surface_flush(stagingSurface);
            // Copy staging surface to window surface
            cairo_set_source_surface(targetContext, stagingSurface, 0, 0);
            cairo_set_operator(targetContext, CAIRO_OPERATOR_SOURCE);
            cairo_paint(targetContext);
        }
    }

    void *RenderContextCairo::GetNativeContext() {
        return cairo;
    }

    void RenderContextCairo::SetTextWrap(UltraCanvas::TextWrap wrap) {
        currentState.textStyle.wrap = wrap;
    }

    void RenderContextCairo::SetTextStyle(const TextStyle &style) {
        currentState.textStyle = style;
        SetCairoColor(style.textColor);
        //ApplyTextStyle(style);
    }

    const TextStyle &RenderContextCairo::GetTextStyle() const {
        return currentState.textStyle;
    }

    float RenderContextCairo::GetAlpha() const {
        return currentState.globalAlpha;
    }

    PangoFontDescription *RenderContextCairo::CreatePangoFont(const FontStyle &style) {
        std::cout << "RenderContextCairo::CreatePangoFont" << std::endl;
        try {
            PangoFontDescription *desc = pango_font_description_new();
            if (!desc) {
                std::cerr << "ERROR: Failed to create Pango font description" << std::endl;
                return nullptr;
            }

            // Use default font if family is empty
            const char *fontFamily = style.fontFamily.empty() ? "Sans" : style.fontFamily.c_str();
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

    void RenderContextCairo::SetCairoColor(const Color &color) {
        try {
            cairo_set_source_rgba(cairo,
                                  color.r / 255.0f,
                                  color.g / 255.0f,
                                  color.b / 255.0f,
                                  color.a / 255.0f * currentState.globalAlpha);
//            std::cout << "RenderContextCairo::SetCairoColor r=" << (int) color.r << " g=" << (int) color.g << " b="
//                      << (int) color.b << std::endl;
        } catch (...) {
            std::cerr << "ERROR: Exception in SetCairoColor" << std::endl;
        }
    }


    void RenderContextCairo::FillEllipse(float x, float y, float w, float h) {

        cairo_save(cairo);
        cairo_translate(cairo, x + w / 2, y + h / 2);
        cairo_scale(cairo, w / 2, h / 2);
        cairo_arc(cairo, 0, 0, 1, 0, 2 * M_PI);
        cairo_restore(cairo);

        Fill();
    }

    void RenderContextCairo::DrawEllipse(float x, float y, float w, float h) {

        cairo_save(cairo);
        cairo_translate(cairo, x + w / 2, y + h / 2);
        cairo_scale(cairo, w / 2, h / 2);
        cairo_arc(cairo, 0, 0, 1, 0, 2 * M_PI);
        cairo_restore(cairo);

        Stroke();
    }

    void RenderContextCairo::FillLinePath(const std::vector<Point2Df> &points) {
        if (points.empty()) return;

        cairo_move_to(cairo, points[0].x, points[0].y);
        for (size_t i = 1; i < points.size(); ++i) {
            cairo_line_to(cairo, points[i].x, points[i].y);
        }
        cairo_close_path(cairo);
        Fill();
    }

    void RenderContextCairo::DrawLinePath(const std::vector<Point2Df> &points, bool closePath) {
        if (points.empty()) return;

        cairo_move_to(cairo, points[0].x, points[0].y);
        for (size_t i = 1; i < points.size(); ++i) {
            cairo_line_to(cairo, points[i].x, points[i].y);
        }

        if (closePath) {
            cairo_close_path(cairo);
        }

        Stroke();
    }


    void RenderContextCairo::DrawArc(float x, float y, float radius, float startAngle, float endAngle) {
        //ApplyStrokeStyle(currentState.style);
        cairo_arc(cairo, x, y, radius, startAngle, endAngle);
        Stroke();
    }

    void RenderContextCairo::FillArc(float x, float y, float radius, float startAngle, float endAngle) {
        //ApplyFillStyle(currentState.style);
        cairo_move_to(cairo, x, y);
        cairo_arc(cairo, x, y, radius, startAngle, endAngle);
        cairo_close_path(cairo);
        Fill();
    }

//    void RenderContextCairo::PathStroke() {
//        ApplyStrokeStyle(currentState.style);
//        cairo_stroke(cairo);
//    }
//    void RenderContextCairo::PathFill() {
//        ApplyFillStyle(currentState.style);
//        cairo_fill(cairo);
//    }

    void RenderContextCairo::DrawBezierCurve(const Point2Df &start, const Point2Df &cp1, const Point2Df &cp2,
                                             const Point2Df &end) {
//        ApplyStrokeStyle(currentState.style);
        cairo_move_to(cairo, start.x, start.y);
        cairo_curve_to(cairo, cp1.x, cp1.y, cp2.x, cp2.y, end.x, end.y);
        Stroke();
    }


    // Path Methods
    void RenderContextCairo::ClearPath() {
        cairo_new_path(cairo);
    }

    void RenderContextCairo::ClosePath() {
        cairo_close_path(cairo);
    }

    void RenderContextCairo::MoveTo(float x, float y) {
        cairo_move_to(cairo, x, y);
    }

    void RenderContextCairo::RelMoveTo(float x, float y) {
        cairo_rel_move_to(cairo, x, y);
    }

    void RenderContextCairo::LineTo(float x, float y) {
        cairo_line_to(cairo, x, y);
    }

    void RenderContextCairo::RelLineTo(float x, float y) {
        cairo_rel_line_to(cairo, x, y);
    }

    void RenderContextCairo::QuadraticCurveTo(float cpx, float cpy, float x, float y) {
        double cx, cy;
        cairo_get_current_point(cairo, &cx, &cy);

        // Convert quadratic to cubic bezier
        float cp1x = cx + 2.0f/3.0f * (cpx - cx);
        float cp1y = cy + 2.0f/3.0f * (cpy - cy);
        float cp2x = x + 2.0f/3.0f * (cpx - x);
        float cp2y = y + 2.0f/3.0f * (cpy - y);

        cairo_curve_to(cairo, cp1x, cp1y, cp2x, cp2y, x, y);
    }

    void RenderContextCairo::BezierCurveTo(float cp1x, float cp1y, float cp2x, float cp2y, float x, float y) {
        cairo_curve_to(cairo, cp1x, cp1y, cp2x, cp2y, x, y);
    }

    void RenderContextCairo::RelBezierCurveTo(float cp1x, float cp1y, float cp2x, float cp2y, float x, float y) {
        cairo_rel_curve_to(cairo, cp1x, cp1y, cp2x, cp2y, x, y);
    }

    void RenderContextCairo::Arc(float cx, float cy, float radius, float startAngle, float endAngle) {
        cairo_arc(cairo, cx, cy, radius, startAngle, endAngle);
    }

    void RenderContextCairo::ArcTo(float x1, float y1, float x2, float y2, float radius) {
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

    void RenderContextCairo::Ellipse(float cx, float cy, float rx, float ry, float rotation,
                 float startAngle, float endAngle) {
        cairo_save(cairo);
        cairo_translate(cairo, cx, cy);
        cairo_rotate(cairo, rotation);
        cairo_scale(cairo, rx, ry);
        cairo_arc(cairo, 0, 0, 1, startAngle, endAngle);
        cairo_restore(cairo);
    }

    void RenderContextCairo::Rect(float x, float y, float width, float height) {
        cairo_rectangle(cairo, x, y, width, height);
    }

    void RenderContextCairo::RoundedRect(float x, float y, float width, float height, float radius) {
        cairo_new_sub_path(cairo);
        cairo_arc(cairo, x + width - radius, y + radius, radius, -M_PI/2, 0);
        cairo_arc(cairo, x + width - radius, y + height - radius, radius, 0, M_PI/2);
        cairo_arc(cairo, x + radius, y + height - radius, radius, M_PI/2, M_PI);
        cairo_arc(cairo, x + radius, y + radius, radius, M_PI, 3*M_PI/2);
        cairo_close_path(cairo);
    }

    void RenderContextCairo::DrawRoundedRectangleWidthBorders(
            float x, float y, float width, float height,
            bool fill,
            float borderLeftWidth, float borderRightWidth,
            float borderTopWidth, float borderBottomWidth,
            const Color& borderLeftColor, const Color& borderRightColor,
            const Color& borderTopColor, const Color& borderBottomColor,
            float borderTopLeftRadius, float borderTopRightRadius,
            float borderBottomRightRadius, float borderBottomLeftRadius,
            const UCDashPattern& borderLeftPattern,
            const UCDashPattern& borderRightPattern,
            const UCDashPattern& borderTopPattern,
            const UCDashPattern& borderBottomPattern
    ) {
        // Clamp radii to prevent overlapping
        float maxRadiusX = width / 2.0;
        float maxRadiusY = height / 2.0;

        float topLeftRadius = std::min({borderTopLeftRadius, maxRadiusX, maxRadiusY});
        float topRightRadius = std::min({borderTopRightRadius, maxRadiusX, maxRadiusY});
        float bottomRightRadius = std::min({borderBottomRightRadius, maxRadiusX, maxRadiusY});
        float bottomLeftRadius = std::min({borderBottomLeftRadius, maxRadiusX, maxRadiusY});

        // Adjust if corners overlap
        float topScale = 1.0;
        float bottomScale = 1.0;
        float leftScale = 1.0;
        float rightScale = 1.0;

        if (topLeftRadius + topRightRadius > width) {
            topScale = width / (topLeftRadius + topRightRadius);
        }
        if (bottomLeftRadius + bottomRightRadius > width) {
            bottomScale = width / (bottomLeftRadius + bottomRightRadius);
        }
        if (topLeftRadius + bottomLeftRadius > height) {
            leftScale = height / (topLeftRadius + bottomLeftRadius);
        }
        if (topRightRadius + bottomRightRadius > height) {
            rightScale = height / (topRightRadius + bottomRightRadius);
        }

        float scale = std::min({topScale, bottomScale, leftScale, rightScale});
        topLeftRadius *= scale;
        topRightRadius *= scale;
        bottomRightRadius *= scale;
        bottomLeftRadius *= scale;

        PushState();

        // Create the rounded rectangle path
        ClearPath();

        // Top left corner
        if (topLeftRadius > 0) {
            Arc(x + topLeftRadius, y + topLeftRadius,
                      topLeftRadius, M_PI, 3 * M_PI / 2);
        } else {
            MoveTo(x, y);
            LineTo(x + width - topRightRadius, y);
        }

        // Top right corner
        if (topRightRadius > 0) {
            Arc(x + width - topRightRadius, y + topRightRadius,
                      topRightRadius, 3 * M_PI / 2, 0);
        } else {
            LineTo(x + width, y + height - bottomRightRadius);
        }

        // Bottom right corner
        if (bottomRightRadius > 0) {
            Arc(x + width - bottomRightRadius, y + height - bottomRightRadius,
                      bottomRightRadius, 0, M_PI / 2);
        } else {
            LineTo(x + bottomLeftRadius, y + height);
        }

        // Bottom left corner
        if (bottomLeftRadius > 0) {
            Arc(x + bottomLeftRadius, y + height - bottomLeftRadius,
                      bottomLeftRadius, M_PI / 2, M_PI);
        } else {
            LineTo(x, y + topLeftRadius);
        }

        ClosePath();

        // Fill background
        if (fill) {
            FillPathPreserve();
        }
        ClipPath();

        // Clip to the rounded rectangle for borders
//        cairo_clip_preserve(cr);
//        cairo_new_path(cr);

        // Draw borders (inset by half the border width for proper positioning)
        // Top border
        if (borderTopWidth > 0) {
            SetStrokeWidth(borderTopWidth);
            if (!borderRightPattern.dashes.empty()) {
                SetLineDash(borderTopPattern);
            } else {
                SetStrokePaint(borderTopColor);
            }
            float yPos = y + borderTopWidth / 2.0;
            DrawLine(x + topLeftRadius, yPos, x + width - topRightRadius, yPos);
//            drawBorderSide(x + topLeftRadius, yPos,
//                           x + width - topRightRadius, yPos,
//                           borderTopWidth, borderTopColor, borderTopPattern);
        }

        // Right border
        if (borderRightWidth > 0) {
            SetStrokeWidth(borderRightWidth);
            if (!borderRightPattern.dashes.empty()) {
                SetLineDash(borderRightPattern);
            } else {
                SetStrokePaint(borderRightColor);
            }
            float xPos = x + width - borderRightWidth / 2.0;
            DrawLine(xPos, y + topRightRadius,
                     xPos, y + height - bottomRightRadius);
        }

        // Bottom border
        if (borderBottomWidth > 0) {
            SetStrokeWidth(borderBottomWidth);
            if (!borderBottomPattern.dashes.empty()) {
                SetLineDash(borderBottomPattern);
            } else {
                SetStrokePaint(borderBottomColor);
            }
            float yPos = y + height - borderBottomWidth / 2.0;
            DrawLine(x + bottomLeftRadius, yPos,
                     x + width - bottomRightRadius, yPos);
        }

        // Left border
        if (borderLeftWidth > 0) {
            float xPos = x + borderLeftWidth / 2.0;
            SetStrokeWidth(borderLeftWidth);
            if (!borderLeftPattern.dashes.empty()) {
                SetLineDash(borderLeftPattern);
            } else {
                SetStrokePaint(borderLeftColor);
            }
            DrawLine(xPos, y + topLeftRadius,
                     xPos, y + height - bottomLeftRadius);
        }

        // Draw rounded corners with borders
        if (topLeftRadius > 0) {
            const Color avgColor = borderLeftColor.Blend(borderTopColor, 0.5);
            float avgWidth = (borderLeftWidth + borderTopWidth) / 2.0;
            SetStrokeWidth(avgWidth);
            SetStrokePaint(avgColor);
            Arc(x + topLeftRadius, y + topLeftRadius, topLeftRadius,
                M_PI, 3 * M_PI / 2);
        }
        if (topRightRadius > 0) {
            const Color avgColor = borderTopColor.Blend(borderRightColor, 0.5);
            float avgWidth = (borderTopWidth + borderRightWidth) / 2.0;
            SetStrokeWidth(avgWidth);
            SetStrokePaint(avgColor);
            Arc(x + width - topRightRadius, y + topRightRadius, topRightRadius,
                3 * M_PI / 2, 2 * M_PI);
        }

        if (bottomRightRadius > 0) {
            const Color avgColor = borderBottomColor.Blend(borderRightColor, 0.5);
            float avgWidth = (borderRightWidth +  borderBottomWidth) / 2.0;
            SetStrokeWidth(avgWidth);
            SetStrokePaint(avgColor);
            Arc(x + width - bottomRightRadius, y + height - bottomRightRadius,
                bottomRightRadius, 0, M_PI / 2);
        }

        if (bottomLeftRadius > 0) {
            const Color avgColor = borderBottomColor.Blend(borderLeftColor, 0.5);
            float avgWidth = (borderBottomWidth + borderLeftWidth) / 2.0;
            SetStrokeWidth(avgWidth);
            SetStrokePaint(avgColor);
            Arc(x + bottomLeftRadius, y + height - bottomLeftRadius, bottomLeftRadius,
                M_PI / 2, M_PI);
        }
        PopState();
    }

    void RenderContextCairo::Circle(float x, float y, float radius) {
        cairo_arc(cairo, x, y, radius, 0, 2 * M_PI);
    }

    void RenderContextCairo::GetPathExtents(float &x, float &y, float &width, float &height) {
        double x2, y2, x1, y1;
        cairo_path_extents(cairo, &x1, &y1, &x2, &y2);
        x = x1;
        y = y1;
        width = std::abs(x2 - x1);
        height = std::abs(y2 - y1);
    }

// Cached Gradient Pattern Methods
    std::shared_ptr<IPaintPattern> RenderContextCairo::CreateLinearGradientPattern(float x1, float y1, float x2, float y2,
                                                                                   const std::vector<GradientStop>& stops) {
        cairo_pattern_t* pattern = cairo_pattern_create_linear(x1, y1, x2, y2);

        for (const auto& stop : stops) {
            cairo_pattern_add_color_stop_rgba(pattern, stop.position,
                stop.color.r / 255.0, stop.color.g / 255.0,
                stop.color.b / 255.0, stop.color.a / 255.0);
        }

        cairo_pattern_set_extend(pattern, CAIRO_EXTEND_PAD);
        if (cairo_pattern_status(pattern) == CAIRO_STATUS_SUCCESS) {
            return std::make_shared<PaintPatternCairo>(pattern);
        } else {
            return std::make_shared<PaintPatternCairo>(nullptr);
        }
    }

    std::shared_ptr<IPaintPattern> RenderContextCairo::CreateRadialGradientPattern(float cx1, float cy1, float r1,
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
            return std::make_shared<PaintPatternCairo>(pattern);
        } else {
            return std::make_shared<PaintPatternCairo>(nullptr);
        }
    }

    void RenderContextCairo::SetFillPaint(std::shared_ptr<IPaintPattern> pattern) {
        currentState.fillSourcePattern = pattern;
        currentState.fillSourceColor = Colors::Transparent;
    }

    void RenderContextCairo::SetFillPaint(const Color& color) {
        currentState.fillSourcePattern = nullptr;
        currentState.fillSourceColor = color;
    }

    void RenderContextCairo::SetStrokePaint(std::shared_ptr<IPaintPattern> pattern) {
        currentState.strokeSourcePattern = pattern;
        currentState.strokeSourceColor = Colors::Transparent;
    }

    void RenderContextCairo::SetStrokePaint(const Color& color) {
        currentState.strokeSourceColor = color;
        currentState.strokeSourcePattern = nullptr;
    }

    void RenderContextCairo::SetTextPaint(std::shared_ptr<IPaintPattern> pattern) {
        currentState.textSourcePattern = pattern;
        currentState.textSourceColor = Colors::Transparent;
    }

    void RenderContextCairo::SetTextPaint(const Color& color) {
        currentState.textSourcePattern = nullptr;
        currentState.textSourceColor = color;
    }

    void RenderContextCairo::ApplySource(const Color& sourceColor, std::shared_ptr<IPaintPattern> sourcePattern) {
        if (sourceColor.a > 0) {
//            if (currentState.currentSourceColor != sourceColor) {
//                currentState.currentSourceColor = sourceColor;
//                SetCairoColor(sourceColor);
//                currentState.currentSourcePattern = nullptr;
//            }
            SetCairoColor(sourceColor);
        } else if (sourcePattern != nullptr) {
            auto handle = static_cast<cairo_pattern_t*>(sourcePattern->GetHandle());
            if (handle) {
                cairo_set_source(cairo, handle);
//                cairo_pattern_t* sourceHandle = nullptr;
//                if (currentState.currentSourcePattern != nullptr) {
//                    sourceHandle = static_cast<cairo_pattern_t*>(currentState.currentSourcePattern->GetHandle());
//                }
//                if (handle != sourceHandle) {
//                    cairo_set_source(cairo, handle);
//                }
            }
//            currentState.currentSourcePattern = sourcePattern;
//            currentState.currentSourceColor = Colors::Transparent;
        }
    }

    void RenderContextCairo::SetStrokeWidth(float width) {
//        currentState.style.strokeWidth = width;
        cairo_set_line_width(cairo, width);
    }

    void RenderContextCairo::SetLineCap(LineCap cap) {
//        currentState.style.lineCap = cap;
        cairo_line_cap_t cairoCap = CAIRO_LINE_CAP_BUTT;
        switch (cap) {
            case LineCap::Round: cairoCap = CAIRO_LINE_CAP_ROUND; break;
            case LineCap::Square: cairoCap = CAIRO_LINE_CAP_SQUARE; break;
            default: break;
        }
        cairo_set_line_cap(cairo, cairoCap);
    }

    void RenderContextCairo::SetLineJoin(LineJoin join) {
        cairo_line_join_t cairoJoin = CAIRO_LINE_JOIN_MITER;
        switch (join) {
            case LineJoin::Round: cairoJoin = CAIRO_LINE_JOIN_ROUND; break;
            case LineJoin::Bevel: cairoJoin = CAIRO_LINE_JOIN_BEVEL; break;
            default: break;
        }
        cairo_set_line_join(cairo, cairoJoin);
    }

    void RenderContextCairo::SetMiterLimit(float limit) {
        cairo_set_miter_limit(cairo, limit);
    }

    void RenderContextCairo::SetLineDash(const UCDashPattern& pattern) {
        if (pattern.dashes.empty()) {
            cairo_set_dash(cairo, nullptr, 0, 0);
        } else {
            cairo_set_dash(cairo, pattern.dashes.data(), pattern.dashes.size(), pattern.offset);
        }
    }

    void RenderContextCairo::SetTextLineHeight(float height) {
        currentState.textStyle.lineHeight = height;
    }

    void RenderContextCairo::SetAlpha(float alpha) {
        // Get current source and modify alpha
        double r, g, b, a;
        cairo_pattern_t* pattern = cairo_get_source(cairo);
        if (cairo_pattern_get_rgba(pattern, &r, &g, &b, &a) == CAIRO_STATUS_SUCCESS) {
            cairo_set_source_rgba(cairo, r, g, b, alpha);
        }
        currentState.globalAlpha = alpha;
    }

    // Text Methods
    void RenderContextCairo::SetFontFace(const std::string& family, FontWeight fw, FontSlant fs) {
        cairo_select_font_face(cairo, family.c_str(),
                               fs == FontSlant::Oblique ? CAIRO_FONT_SLANT_OBLIQUE : (fs == FontSlant::Italic ? CAIRO_FONT_SLANT_ITALIC : CAIRO_FONT_SLANT_NORMAL),
            fw == FontWeight::Bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
        currentState.fontStyle.fontFamily = family;
        currentState.fontStyle.fontWeight = fw;
        currentState.fontStyle.fontSlant = fs;
    }

    void RenderContextCairo::SetFontSize(float size) {
        cairo_set_font_size(cairo, size);
        currentState.fontStyle.fontSize = size;
    }

    void RenderContextCairo::SetFontWeight(UltraCanvas::FontWeight fw) {
        SetFontFace(currentState.fontStyle.fontFamily, fw, currentState.fontStyle.fontSlant);
    }

    void RenderContextCairo::SetFontSlant(UltraCanvas::FontSlant fs) {
        SetFontFace(currentState.fontStyle.fontFamily, currentState.fontStyle.fontWeight, fs);
    }

    void RenderContextCairo::SetTextAlignment(TextAlignment align) {
        currentState.textStyle.alignment = align;
    }

    void RenderContextCairo::SetTextVerticalAlignment(TextVerticalAlignement align) {
        currentState.textStyle.verticalAlignement = align;
    }

    void RenderContextCairo::SetTextIsMarkup(bool isMarkup) {
        currentState.textStyle.isMarkup = isMarkup;
    }

    void RenderContextCairo::FillText(const std::string& text, float x, float y) {
        ApplyFillSource();
        cairo_move_to(cairo, x, y);
        cairo_show_text(cairo, text.c_str());
    }

    void RenderContextCairo::StrokeText(const std::string& text, float x, float y) {
        ApplyStrokeSource();
        cairo_move_to(cairo, x, y);
        cairo_text_path(cairo, text.c_str());
        cairo_stroke(cairo);
    }

    void RenderContextCairo::DrawPixmap(UCPixmapCairo& pixmap, float x, float y, float w, float h, ImageFitMode fitMode) {
        try {
            // Load the image
            // Save current cairo state
            float pixWidth = static_cast<float>(pixmap.GetWidth());
            float pixHeight = static_cast<float>(pixmap.GetHeight());
            if (!w) {
                w = pixWidth;
            }
            if (!h) {
                h = pixHeight;
            }
            float scaleX = 1;
            float scaleY = 1;
            float offsetX = 0;
            float offsetY = 0;
            // Calculate scaling factors
            if (pixHeight != h || pixWidth != w) {
                switch (fitMode) {
                    case ImageFitMode::Contain:
                        scaleX = w / pixWidth;
                        scaleY = h / pixHeight;

                        if (scaleX < scaleY) {
                            scaleY = scaleX;
                            offsetY = (h - (pixHeight * scaleY)) / 2;
                        } else {
                            scaleX = scaleY;
                            offsetX = (w - (pixWidth * scaleX)) / 2;
                        }
                        break;
                    case ImageFitMode::Cover:
                        scaleX = w / pixWidth;
                        scaleY = h / pixHeight;

                        if (scaleX < scaleY) {
                            scaleX = scaleY;
                            offsetX = (w - (pixWidth * scaleX)) / 2;
                        } else {
                            scaleY = scaleX;
                            offsetY = (h - (pixHeight * scaleY)) / 2;
                        }
                        break;
                    case ImageFitMode::NoScale:
                        offsetX = (w - pixWidth) / 2;
                        offsetY = (h - pixHeight) / 2;
                        break;
                    case ImageFitMode::Fill:
                        scaleX = w / pixWidth;
                        scaleY = h / pixHeight;
                        break;
                    case ImageFitMode::ScaleDown:
                        scaleX = w / pixWidth;
                        scaleY = h / pixHeight;
                        if (scaleX < scaleY) {
                            if (scaleX > 1) {
                                scaleX = 1;
                            }
                            scaleY = scaleX;
                        } else {
                            if (scaleY > 1) {
                                scaleY = 1;
                            }
                            scaleX = scaleY;
                        }
                        offsetY = (h - (pixHeight * scaleY)) / 2;
                        offsetX = (w - (pixWidth * scaleX)) / 2;
                        break;
                    default:
                        break;
                }
            }

            // Apply transformations
            cairo_save(cairo);
            cairo_rectangle(cairo, x, y, w, h);
            cairo_clip(cairo);

            cairo_translate(cairo, x + offsetX, y + offsetY);
            // Apply clipping to ensure we don't draw outside the destination rectangle

            if (scaleX != 1.0 || scaleY != 1.0) {
                cairo_scale(cairo, scaleX, scaleY);
            }

            // Set the image as source and paint
            cairo_set_source_surface(cairo, pixmap.GetSurface(), 0, 0);

            if (currentState.globalAlpha < 1.0f) {
                cairo_paint_with_alpha(cairo, currentState.globalAlpha);
            } else {
                cairo_paint(cairo);
            }

            // Restore cairo state
            cairo_restore(cairo);
        } catch (const std::exception &e) {
            std::cerr << "RenderContextCairo::DrawImage: Exception loading image: " << e.what() << std::endl;
        }
    }

    void RenderContextCairo::DrawPartOfPixmap(UCPixmap & pixmap, const Rect2Df &srcRect, const Rect2Df &destRect) {
        try {
            // Validate source rectangle bounds
            if (srcRect.x < 0 || srcRect.y < 0 ||
                srcRect.x + srcRect.width > pixmap.GetWidth() ||
                srcRect.y + srcRect.height > pixmap.GetHeight()) {
                std::cerr << "RenderContextCairo::DrawPartOfPixmap: Source rectangle out of bounds" << std::endl;
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
            cairo_set_source_surface(cairo, pixmap.GetSurface(), 0, 0);

            // Create clipping rectangle for the destination area
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
        } catch (const std::exception &e) {
            std::cerr << "RenderContextCairo::DrawImage: Exception loading image: " << e.what() << std::endl;
        }
    }

    void RenderContextCairo::DrawImageTiled(std::shared_ptr<UCImage> image, float x, float y, float w, float h) {
        try {
            if (!image->IsValid()) return;
            auto pixmap = image->GetPixmap();
            if (!pixmap) return;

            cairo_save(cairo);

            // Create repeating pattern
            cairo_pattern_t *pattern = cairo_pattern_create_for_surface(pixmap->GetSurface());
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

        } catch (const std::exception &e) {
            std::cerr << "RenderContextCairo::DrawImageTiled: Exception: " << e.what() << std::endl;
        }
    }

    void RenderContextCairo::UpdateContext(cairo_t *newCairoContext) {
        std::cout << "RenderContextCairo: Updating Cairo context..." << std::endl;

        if (!newCairoContext) {
            std::cerr << "ERROR: RenderContextCairo: New Cairo context is null!" << std::endl;
            return;
        }
        cairo_status_t status = cairo_status(newCairoContext);
        if (status != CAIRO_STATUS_SUCCESS) {
            std::cerr << "ERROR: RenderContextCairo: New Cairo context is invalid: "
                      << cairo_status_to_string(status) << std::endl;
            throw std::runtime_error("RenderContextCairo: New Cairo context is invalid");
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

        std::cout << "RenderContextCairo: Cairo context updated successfully" << std::endl;
    }

    void RenderContextCairo::FillPathPreserve() {
        ApplySource(currentState.fillSourceColor, currentState.fillSourcePattern);
        cairo_fill_preserve(cairo);
    }

    void RenderContextCairo::StrokePathPreserve() {
        ApplySource(currentState.strokeSourceColor, currentState.strokeSourcePattern);
        cairo_stroke_preserve(cairo);
    }

    void RenderContextCairo::Stroke() {
        ApplySource(currentState.strokeSourceColor, currentState.strokeSourcePattern);
        cairo_stroke(cairo);
    }

    void RenderContextCairo::Fill() {
        ApplySource(currentState.fillSourceColor, currentState.fillSourcePattern);
        cairo_fill(cairo);
    }
} // namespace UltraCanvas