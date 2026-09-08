// UltraCanvasVectorConverter.h  
// Vector Graphics Format Converter Interface for UltraCanvas
// Version: 1.0.0
// Last Modified: 2025-01-20
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasVectorStorage.h"
#include <string>
#include <memory>
#include <map>
#include <functional>
#include <iostream>

namespace UltraCanvas {
namespace VectorConverter {

// ===== FORMAT IDENTIFIERS =====

enum class VectorFormat {
    Unknown = 0,
    
    // Standard formats
    SVG,           // Scalable Vector Graphics
    PDF,           // Portable Document Format (vector parts)
    EPS,           // Encapsulated PostScript
    AI,            // Adobe Illustrator
    DXF,           // AutoCAD Drawing Exchange Format
    DWG,           // AutoCAD Drawing
    
    // Proprietary formats
    XAR,           // Xara/CorelXARA format
    CDR,           // CorelDRAW
    WMF,           // Windows Metafile
    EMF,           // Enhanced Metafile
    CGM,           // Computer Graphics Metafile
    
    // Modern formats
    SVGZ,          // Compressed SVG
    VML,           // Vector Markup Language
    DrawIO,        // draw.io/diagrams.net format
    
    // Special formats
    XAML,          // Windows Presentation Foundation
    Canvas2D,      // HTML5 Canvas commands
    PostScript,    // PostScript
    
    // Internal format
    UltraCanvas    // Native UltraCanvas format
};

// ===== CONVERSION OPTIONS =====

struct ConversionOptions {
    // Quality settings
    float CurveTolerance = 0.1f;          // Curve flattening tolerance
    float SimplificationTolerance = 0.0f;  // Path simplification (0 = disabled)
    int ColorDepth = 32;                   // Bits per pixel for rasterization fallback
    
    // Text handling
    enum class TextHandling {
        Preserve,       // Keep as text (if supported)
        ConvertToPath,  // Convert text to paths
        Rasterize       // Rasterize text (fallback)
    } TextMode = TextHandling::Preserve;
    
    bool EmbedFonts = false;              // Embed fonts if possible
    bool SubsetFonts = true;               // Subset embedded fonts
    
    // Gradient handling
    enum class GradientHandling {
        Preserve,       // Keep gradients (if supported)
        Rasterize,      // Convert to raster
        Discretize      // Convert to discrete color steps
    } GradientMode = GradientHandling::Preserve;
    
    int GradientSteps = 256;              // Steps for discretization
    
    // Filter/Effect handling
    enum class EffectHandling {
        Preserve,       // Keep effects (if supported)
        Rasterize,      // Rasterize affected elements
        Remove          // Remove effects
    } EffectMode = EffectHandling::Preserve;
    
    // Compatibility
    std::string TargetVersion = "auto";    // Target format version
    bool MaxCompatibility = false;         // Use most compatible subset
    
    // Optimization
    bool OptimizePaths = true;            // Optimize path data
    bool RemoveHidden = true;             // Remove invisible elements
    bool MergeStyles = true;              // Merge duplicate styles
    bool CompressOutput = false;          // Compress if format supports it
    
    // Coordinate system
    bool PreserveViewBox = true;          // Maintain coordinate system
    float TargetDPI = 96.0f;              // Target resolution
    
    // Metadata
    bool PreserveMetadata = true;         // Keep document metadata
    bool PreserveComments = false;        // Keep comments
    bool PreserveIds = true;              // Preserve element IDs
    
    // Error handling
    enum class ErrorMode {
        Strict,         // Fail on any error
        Warning,        // Log warnings, continue
        Silent          // Ignore errors
    } ErrorHandling = ErrorMode::Warning;
    
    // Callbacks
    std::function<void(const std::string&)> WarningCallback;
    std::function<void(float)> ProgressCallback;  // 0.0 to 1.0
};

// ===== FORMAT CAPABILITIES =====

struct FormatCapabilities {
    // Basic shapes
    bool SupportsRectangle = true;
    bool SupportsCircle = true;
    bool SupportsEllipse = true;
    bool SupportsLine = true;
    bool SupportsPolyline = true;
    bool SupportsPolygon = true;
    bool SupportsPath = true;
    
    // Path features
    bool SupportsCubicBezier = true;
    bool SupportsQuadraticBezier = true;
    bool SupportsArc = true;
    bool SupportsCompoundPaths = true;
    
    // Text
    bool SupportsText = true;
    bool SupportsTextPath = false;
    bool SupportsRichText = false;
    bool SupportsEmbeddedFonts = false;
    bool SupportsOpenTypeFeatures = false;
    
    // Fills & Strokes
    bool SupportsSolidFill = true;
    bool SupportsLinearGradient = true;
    bool SupportsRadialGradient = true;
    bool SupportsConicalGradient = false;
    bool SupportsMeshGradient = false;
    bool SupportsPattern = true;
    bool SupportsDashing = true;
    bool SupportsVariableStrokeWidth = false;
    
    // Effects
    bool SupportsOpacity = true;
    bool SupportsBlendModes = false;
    bool SupportsFilters = false;
    bool SupportsClipping = true;
    bool SupportsMasking = false;
    bool SupportsDropShadow = false;
    
    // Structure
    bool SupportsGroups = true;
    bool SupportsLayers = false;
    bool SupportsSymbols = false;
    bool SupportsPages = false;
    
    // Animation
    bool SupportsAnimation = false;
    bool SupportsInteractivity = false;
    
    // Advanced
    bool Supports3D = false;
    bool SupportsPerspective = false;
    bool SupportsNonDestructiveEffects = false;
    
    // Limits
    size_t MaxPathComplexity = SIZE_MAX;
    size_t MaxGradientStops = SIZE_MAX;
    size_t MaxLayerDepth = SIZE_MAX;
    float MaxDocumentSize = INFINITY;
};

// ===== CONVERTER INTERFACE =====

class IVectorFormatConverter {
public:
    virtual ~IVectorFormatConverter() = default;
    
    // Format information
    virtual VectorFormat GetFormat() const = 0;
    virtual std::string GetFormatName() const = 0;
    virtual std::string GetFormatVersion() const = 0;
    virtual std::vector<std::string> GetFileExtensions() const = 0;
    virtual std::string GetMimeType() const = 0;
    
    // Capabilities
    virtual FormatCapabilities GetCapabilities() const = 0;
    virtual bool CanImport() const = 0;
    virtual bool CanExport() const = 0;
    
    // Import/Export
    virtual std::shared_ptr<VectorStorage::VectorDocument> Import(
        const std::string& filename,
        const ConversionOptions& options = ConversionOptions()
    ) = 0;
    
    virtual std::shared_ptr<VectorStorage::VectorDocument> ImportFromString(
        const std::string& data,
        const ConversionOptions& options = ConversionOptions()
    ) = 0;
    
    virtual std::shared_ptr<VectorStorage::VectorDocument> ImportFromStream(
        std::istream& stream,
        const ConversionOptions& options = ConversionOptions()
    ) = 0;
    
    virtual bool Export(
        const VectorStorage::VectorDocument& document,
        const std::string& filename,
        const ConversionOptions& options = ConversionOptions()
    ) = 0;
    
    virtual std::string ExportToString(
        const VectorStorage::VectorDocument& document,
        const ConversionOptions& options = ConversionOptions()
    ) = 0;
    
    virtual bool ExportToStream(
        const VectorStorage::VectorDocument& document,
        std::ostream& stream,
        const ConversionOptions& options = ConversionOptions()
    ) = 0;
    
    // Validation
    virtual bool ValidateFile(const std::string& filename) const = 0;
    virtual bool ValidateData(const std::string& data) const = 0;
};

// Format dispatch by extension lives in UltraCanvasVectorFormatsPlugin
// (CreateConverterForExtension); the graphics plugin registry is the
// factory. A registry keyed by VectorFormat, multi-step conversion paths
// and a pre-conversion loss report were sketched here early on but never
// built - see Docs/Research/UltraCanvasVectorModelProposal.md.

// ===== FORMAT-SPECIFIC CONVERTERS =====

// SVG Converter
class SVGConverter : public IVectorFormatConverter {
public:
    VectorFormat GetFormat() const override { return VectorFormat::SVG; }
    std::string GetFormatName() const override { return "Scalable Vector Graphics"; }
    std::string GetFormatVersion() const override { return "1.1"; }
    std::vector<std::string> GetFileExtensions() const override { return {".svg", ".svgz"}; }
    std::string GetMimeType() const override { return "image/svg+xml"; }
    
    FormatCapabilities GetCapabilities() const override;
    bool CanImport() const override { return true; }
    bool CanExport() const override { return true; }
    
    // SVG-specific options
    struct SVGOptions {
        std::string Version = "1.1";  // "1.1" or "2.0"
        bool UseCSS = true;           // Use CSS for styling
        bool Minify = false;          // Minimize output
        bool PrettyPrint = true;      // Format output
        int IndentSize = 2;           // Indentation spaces
        bool UseViewBox = true;       // Include viewBox
        bool IncludeXMLDeclaration = true;
        std::string Encoding = "UTF-8";
    };
    
    void SetSVGOptions(const SVGOptions& options) { svgOptions = options; }
    
    // Implement interface methods
    std::shared_ptr<VectorStorage::VectorDocument> Import(
        const std::string& filename,
        const ConversionOptions& options = ConversionOptions()
    ) override;
    
    std::shared_ptr<VectorStorage::VectorDocument> ImportFromString(
        const std::string& data,
        const ConversionOptions& options = ConversionOptions()
    ) override;
    
    std::shared_ptr<VectorStorage::VectorDocument> ImportFromStream(
        std::istream& stream,
        const ConversionOptions& options = ConversionOptions()
    ) override;
    
    bool Export(
        const VectorStorage::VectorDocument& document,
        const std::string& filename,
        const ConversionOptions& options = ConversionOptions()
    ) override;
    
    std::string ExportToString(
        const VectorStorage::VectorDocument& document,
        const ConversionOptions& options = ConversionOptions()
    ) override;
    
    bool ExportToStream(
        const VectorStorage::VectorDocument& document,
        std::ostream& stream,
        const ConversionOptions& options = ConversionOptions()
    ) override;
    
    bool ValidateFile(const std::string& filename) const override;
    bool ValidateData(const std::string& data) const override;
    
private:
    SVGOptions svgOptions;
};

// The XAR converter is declared in UltraCanvasXARConverter.h (the
// specification-compliant implementation); the legacy duplicate that
// lived here clashed with it at compile time.


// PDF Converter (vector parts only)
class PDFVectorConverter : public IVectorFormatConverter {
public:
    VectorFormat GetFormat() const override { return VectorFormat::PDF; }
    std::string GetFormatName() const override { return "Portable Document Format"; }
    std::string GetFormatVersion() const override { return "1.7"; }
    std::vector<std::string> GetFileExtensions() const override { return {".pdf"}; }
    std::string GetMimeType() const override { return "application/pdf"; }
    
    FormatCapabilities GetCapabilities() const override;
    // Reading PDF is the MuPDF-based PDF plugin's job; this converter only
    // writes. The Import methods warn and return null.
    bool CanImport() const override { return false; }
    bool CanExport() const override { return true; }

    std::shared_ptr<VectorStorage::VectorDocument> Import(
        const std::string& filename,
        const ConversionOptions& options = ConversionOptions()
    ) override;
    
    std::shared_ptr<VectorStorage::VectorDocument> ImportFromString(
        const std::string& data,
        const ConversionOptions& options = ConversionOptions()
    ) override;
    
    std::shared_ptr<VectorStorage::VectorDocument> ImportFromStream(
        std::istream& stream,
        const ConversionOptions& options = ConversionOptions()
    ) override;
    
    bool Export(
        const VectorStorage::VectorDocument& document,
        const std::string& filename,
        const ConversionOptions& options = ConversionOptions()
    ) override;
    
    std::string ExportToString(
        const VectorStorage::VectorDocument& document,
        const ConversionOptions& options = ConversionOptions()
    ) override;
    
    bool ExportToStream(
        const VectorStorage::VectorDocument& document,
        std::ostream& stream,
        const ConversionOptions& options = ConversionOptions()
    ) override;
    
    bool ValidateFile(const std::string& filename) const override;
    bool ValidateData(const std::string& data) const override;
};

} // namespace VectorConverter
} // namespace UltraCanvas
