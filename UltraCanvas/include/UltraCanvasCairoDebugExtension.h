// include/UltraCanvasCairoDebugExtension.h
// Cairo transformation matrix debugging for UltraCanvas elements
// Version: 1.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasElementDebug.h"
#include "UltraCanvasRenderContext.h"

// Platform-specific includes for Cairo
#ifdef __linux__
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#endif

#include <string>
#include <sstream>
#include <iomanip>

namespace UltraCanvas {

// ===== CAIRO MATRIX INFORMATION STRUCTURE =====
    struct CairoMatrixInfo {
        double xx, yx, xy, yy, x0, y0;  // Cairo matrix components
        bool isIdentity;
        bool isValid;

        // Derived transformation information
        double scaleX, scaleY;
        double rotation;
        double translationX, translationY;
        double skewX, skewY;

        CairoMatrixInfo() : xx(1), yx(0), xy(0), yy(1), x0(0), y0(0),
                            isIdentity(true), isValid(false),
                            scaleX(1), scaleY(1), rotation(0),
                            translationX(0), translationY(0),
                            skewX(0), skewY(0) {}
    };

// ===== CAIRO DEBUG EXTENSION CLASS =====
    class UltraCanvasCairoDebugExtension {
    public:
        // Main function to get Cairo matrix information
        static CairoMatrixInfo GetCurrentCairoMatrix();

        // Format matrix information as string
        static std::string FormatCairoMatrix(const CairoMatrixInfo& matrix, bool verbose = false);

        // Draw Cairo transformation debug overlay
        static std::string DrawCairoTransformDebug(UltraCanvasUIElement* element, const DebugRenderSettings& settings);

        // Draw Cairo matrix visualization
        static void DrawMatrixVisualization(const CairoMatrixInfo& matrix, const Point2Di& position,
                                            const DebugRenderSettings& settings);

        // Validate current Cairo transformation
        static bool ValidateCairoTransformation(std::string& errorMessage);

        // Get transformation stack depth
        static int GetCairoStackDepth();

    private:
        // Platform-specific Cairo context access
        static cairo_t* GetCurrentCairoContext();

        // Decompose matrix into individual transformation components
        static void DecomposeMatrix(CairoMatrixInfo& matrix);

        // Check if matrix is identity
        static bool IsIdentityMatrix(const CairoMatrixInfo& matrix);

        // Convert radians to degrees
        static double RadiansToDegrees(double radians) {
            return radians * 180.0 / M_PI;
        }
    };

// ===== IMPLEMENTATION =====

#ifdef __linux__
// Linux-specific implementation using Cairo directly
    cairo_t* UltraCanvasCairoDebugExtension::GetCurrentCairoContext() {
        // Try to access the current render context
        auto* renderContext = GetRenderContext();
        if (!renderContext) return nullptr;

        // This is platform-specific - would need to cast to RenderContextCairo
        // and access the cairo context member
        // For now, return nullptr as we need proper interface design
        return ((RenderContextCairo*)renderContext)->GetCairo();
    }

    CairoMatrixInfo UltraCanvasCairoDebugExtension::GetCurrentCairoMatrix() {
        CairoMatrixInfo matrixInfo;

        cairo_t* cairo = GetCurrentCairoContext();
        if (!cairo) {
            return matrixInfo; // Returns invalid matrix
        }

        // Get the current transformation matrix from Cairo
        cairo_matrix_t matrix;
        cairo_get_matrix(cairo, &matrix);

        // Fill in the matrix information
        matrixInfo.xx = matrix.xx;
        matrixInfo.yx = matrix.yx;
        matrixInfo.xy = matrix.xy;
        matrixInfo.yy = matrix.yy;
        matrixInfo.x0 = matrix.x0;
        matrixInfo.y0 = matrix.y0;
        matrixInfo.isValid = true;

        // Decompose the matrix into individual components
        DecomposeMatrix(matrixInfo);
        matrixInfo.isIdentity = IsIdentityMatrix(matrixInfo);

        return matrixInfo;
    }
#else
    // Fallback implementation for non-Cairo platforms
CairoMatrixInfo UltraCanvasCairoDebugExtension::GetCurrentCairoMatrix() {
    return CairoMatrixInfo(); // Returns invalid matrix
}

cairo_t* UltraCanvasCairoDebugExtension::GetCurrentCairoContext() {
    return nullptr;
}
#endif

    void UltraCanvasCairoDebugExtension::DecomposeMatrix(CairoMatrixInfo& matrix) {
        // Extract scale factors
        matrix.scaleX = std::sqrt(matrix.xx * matrix.xx + matrix.yx * matrix.yx);
        matrix.scaleY = std::sqrt(matrix.xy * matrix.xy + matrix.yy * matrix.yy);

        // Extract rotation (in radians)
        matrix.rotation = std::atan2(matrix.yx, matrix.xx);

        // Extract translation
        matrix.translationX = matrix.x0;
        matrix.translationY = matrix.y0;

        // Extract skew
        matrix.skewX = std::atan2(-matrix.xy, matrix.yy) - matrix.rotation;
        matrix.skewY = std::atan2(matrix.yx, matrix.xx) - matrix.rotation;
    }

    bool UltraCanvasCairoDebugExtension::IsIdentityMatrix(const CairoMatrixInfo& matrix) {
        const double epsilon = 1e-6;
        return (std::abs(matrix.xx - 1.0) < epsilon &&
                std::abs(matrix.yy - 1.0) < epsilon &&
                std::abs(matrix.xy) < epsilon &&
                std::abs(matrix.yx) < epsilon &&
                std::abs(matrix.x0) < epsilon &&
                std::abs(matrix.y0) < epsilon);
    }

    std::string UltraCanvasCairoDebugExtension::FormatCairoMatrix(const CairoMatrixInfo& matrix, bool verbose) {
        if (!matrix.isValid) {
            return "Cairo: Not Available";
        }

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);

        if (verbose) {
            ss << "Cairo Matrix:\n";
//            ss << "  Raw: [" << matrix.xx << ", " << matrix.xy << ", " << matrix.x0 << "]\n";
//            ss << "       [" << matrix.yx << ", " << matrix.yy << ", " << matrix.y0 << "]\n";
            ss << "  Translation: (" << matrix.translationX << ", " << matrix.translationY << ")\n";
//            ss << "  Scale: (" << matrix.scaleX << ", " << matrix.scaleY << ")\n";
//            ss << "  Rotation: " << RadiansToDegrees(matrix.rotation) << "°\n";
            if (std::abs(matrix.skewX) > 0.01 || std::abs(matrix.skewY) > 0.01) {
                ss << "  Skew: (" << RadiansToDegrees(matrix.skewX) << "°, " << RadiansToDegrees(matrix.skewY) << "°)\n";
            }
            ss << "  Identity: " << (matrix.isIdentity ? "Yes" : "No");
        } else {
            if (matrix.isIdentity) {
                ss << "Cairo: Identity";
            } else {
                ss << "Cairo: T(" << matrix.translationX << "," << matrix.translationY << ") ";
                if (std::abs(matrix.scaleX - 1.0) > 0.01 || std::abs(matrix.scaleY - 1.0) > 0.01) {
                    ss << "S(" << matrix.scaleX << "," << matrix.scaleY << ") ";
                }
                if (std::abs(matrix.rotation) > 0.01) {
                    ss << "R(" << RadiansToDegrees(matrix.rotation) << "°)";
                }
            }
        }

        return ss.str();
    }

    std::string UltraCanvasCairoDebugExtension::DrawCairoTransformDebug(UltraCanvasUIElement* element,
                                                                 const DebugRenderSettings& settings) {
        if (!element) return "";


        CairoMatrixInfo matrix = GetCurrentCairoMatrix();
        std::string matrixText = FormatCairoMatrix(matrix, false);

        ctx->PushState();
        Rect2Di bounds = element->GetBounds();

        // Position for Cairo debug info (bottom-left of element)
        Point2Di debugPos = Point2Di(bounds.x, bounds.y + bounds.height + 10);

        // Format the matrix information

        // Draw the matrix information
        DebugRenderSettings cairoSettings = settings;
        cairoSettings.textColor = Color(255, 255, 0, 255);      // Yellow text for Cairo info
        cairoSettings.textBackgroundColor = Color(0, 0, 50, 200); // Dark blue background
        cairoSettings.textSize = settings.textSize - 1;          // Slightly smaller text

        UltraCanvasUIElementDebugExtension::DrawDebugText(matrixText, bounds, debugPos, cairoSettings);

        return matrixText;
        // Draw matrix visualization if not identity
//        if (matrix.isValid && !matrix.isIdentity) {
//            DrawMatrixVisualization(matrix, Point2Di(bounds.x + bounds.width + 20, bounds.y), settings);
//        }
    }

    void UltraCanvasCairoDebugExtension::DrawMatrixVisualization(const CairoMatrixInfo& matrix,
                                                                 const Point2Di& position,
                                                                 const DebugRenderSettings& settings) {
        ctx->PushState();

        float gridSize = 50.0f;
        Point2Df center = Point2Df(position.x + gridSize, position.y + gridSize);

        // Draw coordinate system grid
        ctx->PaintWidthColorColor(128, 128, 128, 150));
        ctx->SetStrokeWidth(1.0f);

        // Grid lines
        for (int i = -1; i <= 2; i++) {
            float x = center.x + i * gridSize / 2;
            float y = center.y + i * gridSize / 2;

            // Vertical lines
            ctx->DrawLine(x, center.y - gridSize, x, center.y + gridSize);
            // Horizontal lines
            ctx->DrawLine(center.x - gridSize, y, center.x + gridSize, y);
        }

        // Draw original coordinate system (red)
        ctx->PaintWidthColorColor(255, 100, 100, 200));
        ctx->SetStrokeWidth(2.0f);

        // Original X axis
        ctx->DrawLine(center.x - gridSize / 2, center.y, center.x + gridSize / 2, center.y);
        // Original Y axis
        ctx->DrawLine(center.x, center.y - gridSize / 2, center.x, center.y + gridSize / 2);

        // Draw transformed coordinate system (green)
        ctx->PaintWidthColorColor(100, 255, 100, 200));
        SetStrokeWidth(2.0f);

        // Apply transformation to unit vectors
        float unitScale = gridSize / 4; // Scale down for visualization

        // Transformed X axis (1,0) -> (xx, yx)
        Point2Df transformedX = Point2Df(center.x + matrix.xx * unitScale, center.y + matrix.yx * unitScale);
        ctx->DrawLine(center, transformedX);

        // Transformed Y axis (0,1) -> (xy, yy)
        Point2Df transformedY = Point2Df(center.x + matrix.xy * unitScale, center.y + matrix.yy * unitScale);
        ctx->DrawLine(center, transformedY);

        // Draw arrow heads
//        UltraCanvasElementDebugExtension::DrawArrow(center, transformedX, 6.0f);
//        UltraCanvasElementDebugExtension::DrawArrow(center, transformedY, 6.0f);

        // Draw origin translation
        if (std::abs(matrix.x0) > 0.1 || std::abs(matrix.y0) > 0.1) {
            ctx->PaintWidthColorColor(255, 255, 100, 200)); // Yellow for translation
            SetStrokeWidth(1.0f);

            Point2Df translatedOrigin = Point2Df(center.x + matrix.x0 / 10, center.y + matrix.y0 / 10);
            ctx->DrawLine(center, translatedOrigin);

            // Draw small circle at translated origin
           ctx->PaintWidthColorColor(255, 255, 100, 200));
            DrawFilledCircle(translatedOrigin, 3.0f, Color(255, 255, 100, 200));
        }

        // Label the visualization
        ctx->PaintWidthColorColor(200, 200, 200, 255));
        ctx->SetFont(settings.fontFamily, settings.textSize - 2);
        DrawText("Matrix Viz", Point2Di(position.x, position.y - 5));
    }

    bool UltraCanvasCairoDebugExtension::ValidateCairoTransformation(std::string& errorMessage) {
        CairoMatrixInfo matrix = GetCurrentCairoMatrix();

        if (!matrix.isValid) {
            errorMessage = "Cairo matrix not available";
            return false;
        }

        // Check for problematic transformations
        if (std::abs(matrix.scaleX) < 0.001 || std::abs(matrix.scaleY) < 0.001) {
            errorMessage = "Warning: Near-zero scale factors detected";
            return false;
        }

        if (std::abs(matrix.scaleX) > 1000 || std::abs(matrix.scaleY) > 1000) {
            errorMessage = "Warning: Extremely large scale factors detected";
            return false;
        }

        // Check for NaN or infinite values
        if (!std::isfinite(matrix.xx) || !std::isfinite(matrix.xy) || !std::isfinite(matrix.x0) ||
            !std::isfinite(matrix.yx) || !std::isfinite(matrix.yy) || !std::isfinite(matrix.y0)) {
            errorMessage = "Error: Invalid matrix values (NaN or infinite)";
            return false;
        }

        errorMessage = "Cairo transformation is valid";
        return true;
    }

    int UltraCanvasCairoDebugExtension::GetCairoStackDepth() {
        // This would require keeping track of cairo_save/cairo_restore calls
        // For now, return -1 to indicate unavailable
        return -1;
    }

// ===== INTEGRATION WITH EXISTING DEBUG SYSTEM =====

// Enhanced debug settings that include Cairo matrix information
    struct CairoDebugRenderSettings : public DebugRenderSettings {
        bool showCairoMatrix = true;
        bool showMatrixVisualization = true;
        bool showTransformationStack = false;
        bool validateTransformation = true;
        bool verboseMatrix = false;

        CairoDebugRenderSettings() : DebugRenderSettings() {}
        CairoDebugRenderSettings(const DebugRenderSettings& base) : DebugRenderSettings(base) {}
    };

// Enhanced debug function that includes Cairo matrix info
    inline void RenderElementDebugWithCairo(UltraCanvasUIElement* element) {
        if (!UltraCanvasDebugRenderer::IsDebugEnabled()) return;

        CairoDebugRenderSettings settings;
        settings.showCairoMatrix = true;
        settings.showMatrixVisualization = false;

        // Render basic debug info
        std::string debugInfo = UltraCanvasElementDebugExtension::RenderDebugInfo(element, settings);

        // Add Cairo-specific debug info
        std::string matrixTxt = UltraCanvasCairoDebugExtension::DrawCairoTransformDebug(element, settings);

        std::cout << debugInfo << std::endl << matrixTxt << std::endl;

    }

// Macro for Cairo debug rendering
#define ULTRACANVAS_DEBUG_ELEMENT_WITH_CAIRO() \
    if (UltraCanvas::UltraCanvasDebugRenderer::IsDebugEnabled()) { \
        UltraCanvas::RenderElementDebugWithCairo(this); \
    }

} // namespace UltraCanvas