// UltraCanvas/include/Plugins/Vector/UltraCanvasXARConverter.h
// XAR (Xara) Vector Format Converter - Specification-Compliant Implementation
// Version: 2.0.0
// Last Modified: 2025-01-20
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasVectorStorage.h"
#include "UltraCanvasVectorConverter.h"
#include <cstdint>
#include <vector>
#include <map>
#include <stack>
#include <memory>
#include <functional>

namespace UltraCanvas {
    namespace VectorConverter {

// ===== XAR FILE FORMAT CONSTANTS (Per Official Specification) =====

// XAR File Signature: "XARA" followed by 0xA3 0xA3 0x0D 0x0A
        constexpr uint8_t XAR_SIGNATURE[8] = {0x58, 0x41, 0x52, 0x41, 0xA3, 0xA3, 0x0D, 0x0A};

// Coordinate System: XAR uses millipoints (1/72000 inch)
// 1 point = 72 millipoints per inch, so 1 millipoint = 1/72000 inch
        constexpr float XAR_MILLIPOINTS_PER_POINT = 1000.0f;  // 72000/72 = 1000 millipoints per point
        constexpr float XAR_MILLIPOINTS_PER_INCH = 72000.0f;

// ===== OFFICIAL XAR TAG VALUES (Appendix A of XAR Specification) =====

        namespace XARTags {
            // File Structure Tags (0-99)
            constexpr uint32_t TAG_UNDEFINED                    = 0;
            constexpr uint32_t TAG_UP                           = 1;
            constexpr uint32_t TAG_DOWN                         = 2;
            constexpr uint32_t TAG_FILEHEADER                   = 3;
            constexpr uint32_t TAG_ENDOFFILE                    = 4;

            // Compression Tags (30-39)
            constexpr uint32_t TAG_STARTCOMPRESSION             = 30;
            constexpr uint32_t TAG_ENDCOMPRESSION               = 31;

            // Document Structure Tags (40-99)
            constexpr uint32_t TAG_DOCUMENT                     = 40;
            constexpr uint32_t TAG_CHAPTER                      = 41;
            constexpr uint32_t TAG_SPREAD                       = 42;
            constexpr uint32_t TAG_LAYER                        = 43;
            constexpr uint32_t TAG_PAGE                         = 44;
            constexpr uint32_t TAG_SPREADINFORMATION            = 45;
            constexpr uint32_t TAG_GRIDRULERSETTINGS            = 46;
            constexpr uint32_t TAG_GRIDRULERORIGIN              = 47;
            constexpr uint32_t TAG_LAYERDETAILS                 = 48;
            constexpr uint32_t TAG_GUIDELAYERDETAILS            = 49;
            constexpr uint32_t TAG_SPREADSCALING                = 54;
            constexpr uint32_t TAG_SPREADSCALING_ACTIVE         = 55;

            // Colour Tags (1000-1099)
            constexpr uint32_t TAG_DEFINERGBCOLOUR              = 1000;
            constexpr uint32_t TAG_DEFINECOMPLEXCOLOUR          = 1001;

            // Path Tags (1100-1199)
            constexpr uint32_t TAG_PATH                         = 1100;
            constexpr uint32_t TAG_PATH_FILLED                  = 1101;
            constexpr uint32_t TAG_PATH_STROKED                 = 1102;
            constexpr uint32_t TAG_PATH_FILLED_STROKED          = 1103;
            constexpr uint32_t TAG_PATH_RELATIVE                = 1105;
            constexpr uint32_t TAG_PATH_RELATIVE_FILLED         = 1106;
            constexpr uint32_t TAG_PATH_RELATIVE_STROKED        = 1107;
            constexpr uint32_t TAG_PATH_RELATIVE_FILLED_STROKED = 1108;

            // Group Tags (1200-1249)
            constexpr uint32_t TAG_GROUP                        = 1200;

            // Blend Tags (1250-1299)
            constexpr uint32_t TAG_BLEND                        = 1250;
            constexpr uint32_t TAG_BLENDER                      = 1251;
            constexpr uint32_t TAG_BLENDINKONLINE               = 1252;
            constexpr uint32_t TAG_BLENDPATH                    = 1253;

            // Mould Tags (1300-1349)
            constexpr uint32_t TAG_MOULD_ENVELOPE               = 1300;
            constexpr uint32_t TAG_MOULD_PERSPECTIVE            = 1301;
            constexpr uint32_t TAG_MOULDGROUP                   = 1302;
            constexpr uint32_t TAG_MOULDPATH                    = 1303;

            // Regular Shapes / QuickShapes (1900-1999)
            constexpr uint32_t TAG_REGULAR_SHAPE_PHASE_1        = 1900;
            constexpr uint32_t TAG_REGULAR_SHAPE_PHASE_2        = 1901;
            constexpr uint32_t TAG_QUICKSHAPE_ARC               = 1910;
            constexpr uint32_t TAG_QUICKSHAPE_SIMPLE            = 1911;
            constexpr uint32_t TAG_QUICKSHAPE_COMPLEX           = 1912;

            // Rectangle Tags (2100-2199)
            constexpr uint32_t TAG_RECTANGLE_SIMPLE             = 2100;
            constexpr uint32_t TAG_RECTANGLE_SIMPLE_REFORMED    = 2101;
            constexpr uint32_t TAG_RECTANGLE_SIMPLE_STELLATED   = 2102;
            constexpr uint32_t TAG_RECTANGLE_SIMPLE_STELLATED_REFORMED = 2103;
            constexpr uint32_t TAG_RECTANGLE_SIMPLE_ROUNDED     = 2104;
            constexpr uint32_t TAG_RECTANGLE_SIMPLE_ROUNDED_REFORMED   = 2105;
            constexpr uint32_t TAG_RECTANGLE_SIMPLE_ROUNDED_STELLATED  = 2106;
            constexpr uint32_t TAG_RECTANGLE_SIMPLE_ROUNDED_STELLATED_REFORMED = 2107;
            constexpr uint32_t TAG_RECTANGLE_COMPLEX            = 2108;
            constexpr uint32_t TAG_RECTANGLE_COMPLEX_REFORMED   = 2109;
            constexpr uint32_t TAG_RECTANGLE_COMPLEX_STELLATED  = 2110;
            constexpr uint32_t TAG_RECTANGLE_COMPLEX_STELLATED_REFORMED = 2111;
            constexpr uint32_t TAG_RECTANGLE_COMPLEX_ROUNDED    = 2112;
            constexpr uint32_t TAG_RECTANGLE_COMPLEX_ROUNDED_REFORMED   = 2113;
            constexpr uint32_t TAG_RECTANGLE_COMPLEX_ROUNDED_STELLATED  = 2114;
            constexpr uint32_t TAG_RECTANGLE_COMPLEX_ROUNDED_STELLATED_REFORMED = 2115;

            // Ellipse Tags (2200-2299)
            constexpr uint32_t TAG_ELLIPSE_SIMPLE               = 2200;
            constexpr uint32_t TAG_ELLIPSE_COMPLEX              = 2201;

            // Polygon Tags (2300-2399)
            constexpr uint32_t TAG_POLYGON_COMPLEX              = 2300;
            constexpr uint32_t TAG_POLYGON_COMPLEX_REFORMED     = 2301;
            constexpr uint32_t TAG_POLYGON_COMPLEX_STELLATED    = 2302;
            constexpr uint32_t TAG_POLYGON_COMPLEX_STELLATED_REFORMED = 2303;
            constexpr uint32_t TAG_POLYGON_COMPLEX_ROUNDED      = 2304;
            constexpr uint32_t TAG_POLYGON_COMPLEX_ROUNDED_REFORMED   = 2305;
            constexpr uint32_t TAG_POLYGON_COMPLEX_ROUNDED_STELLATED  = 2306;
            constexpr uint32_t TAG_POLYGON_COMPLEX_ROUNDED_STELLATED_REFORMED = 2307;

            // Bitmap Tags (2400-2499)
            constexpr uint32_t TAG_NODE_BITMAP                  = 2400;
            constexpr uint32_t TAG_NODE_CONTONEDBITMAP          = 2401;

            // Text Tags (2500-2599)
            constexpr uint32_t TAG_TEXT_STORY_SIMPLE            = 2500;
            constexpr uint32_t TAG_TEXT_STORY_COMPLEX           = 2501;
            constexpr uint32_t TAG_TEXT_STORY_SIMPLE_START_LEFT = 2502;
            constexpr uint32_t TAG_TEXT_STORY_SIMPLE_START_RIGHT= 2503;
            constexpr uint32_t TAG_TEXT_STORY_SIMPLE_END_LEFT   = 2504;
            constexpr uint32_t TAG_TEXT_STORY_SIMPLE_END_RIGHT  = 2505;
            constexpr uint32_t TAG_TEXT_STORY_COMPLEX_START_LEFT= 2506;
            constexpr uint32_t TAG_TEXT_STORY_COMPLEX_START_RIGHT = 2507;
            constexpr uint32_t TAG_TEXT_STORY_COMPLEX_END_LEFT  = 2508;
            constexpr uint32_t TAG_TEXT_STORY_COMPLEX_END_RIGHT = 2509;
            constexpr uint32_t TAG_TEXT_LINE                    = 2510;
            constexpr uint32_t TAG_TEXT_STRING                  = 2511;
            constexpr uint32_t TAG_TEXT_CHAR                    = 2512;
            constexpr uint32_t TAG_TEXT_EOL                     = 2513;
            constexpr uint32_t TAG_TEXT_KERN                    = 2514;
            constexpr uint32_t TAG_TEXT_CARET                   = 2515;
            constexpr uint32_t TAG_TEXT_LINE_INFO               = 2516;
            constexpr uint32_t TAG_TEXT_TAB                     = 2517;

            // Attribute Tags - Line/Stroke (3000-3099)
            constexpr uint32_t TAG_LINECOLOUR                   = 3000;
            constexpr uint32_t TAG_LINEWIDTH                    = 3001;
            constexpr uint32_t TAG_LINETRANSPARENCY             = 3002;
            constexpr uint32_t TAG_STARTCAP                     = 3003;
            constexpr uint32_t TAG_ENDCAP                       = 3004;
            constexpr uint32_t TAG_JOINSTYLE                    = 3005;
            constexpr uint32_t TAG_MITRELIMIT                   = 3006;
            constexpr uint32_t TAG_WINDINGRULE                  = 3007;
            constexpr uint32_t TAG_DASHSTYLE                    = 3008;
            constexpr uint32_t TAG_STARTARROW                   = 3009;
            constexpr uint32_t TAG_ENDARROW                     = 3010;

            // Attribute Tags - Fill (3100-3199)
            constexpr uint32_t TAG_FLATFILL                     = 3100;
            constexpr uint32_t TAG_LINECOLOUR_FLAT              = 3101;
            constexpr uint32_t TAG_LINEARFILL                   = 3102;
            constexpr uint32_t TAG_LINEARFILL3POINT             = 3103;
            constexpr uint32_t TAG_LINEARFILLMULTISTAGE         = 3104;
            constexpr uint32_t TAG_LINEARFILLMULTISTAGE3POINT   = 3105;
            constexpr uint32_t TAG_CIRCULARFILL                 = 3106;
            constexpr uint32_t TAG_ELLIPTICALFILL               = 3107;
            constexpr uint32_t TAG_CONICALFILL                  = 3108;
            constexpr uint32_t TAG_BITMAPFILL                   = 3109;
            constexpr uint32_t TAG_FRACTALFILL                  = 3110;
            constexpr uint32_t TAG_THREECOLFILL                 = 3111;
            constexpr uint32_t TAG_FOURCOLFILL                  = 3112;
            constexpr uint32_t TAG_NOISEFILL                    = 3113;
            constexpr uint32_t TAG_FILLEFFECT_FADE              = 3114;
            constexpr uint32_t TAG_FILLEFFECT_RAINBOW           = 3115;
            constexpr uint32_t TAG_FILLEFFECT_ALTRAINBOW        = 3116;
            constexpr uint32_t TAG_FILL_REPEATING               = 3117;
            constexpr uint32_t TAG_FILL_NONREPEATING            = 3118;
            constexpr uint32_t TAG_FILL_REPEATINGINVERTED       = 3119;
            constexpr uint32_t TAG_SQUAREFILL                   = 3120;

            // Attribute Tags - Transparency (3200-3299)
            constexpr uint32_t TAG_FLATTRANSPARENTFILL          = 3200;
            constexpr uint32_t TAG_LINEARTRANSPARENTFILL        = 3201;
            constexpr uint32_t TAG_LINEARTRANSPARENTFILL3POINT  = 3202;
            constexpr uint32_t TAG_CIRCULARTRANSPARENTFILL      = 3203;
            constexpr uint32_t TAG_ELLIPTICALTRANSPARENTFILL    = 3204;
            constexpr uint32_t TAG_CONICALTRANSPARENTFILL       = 3205;
            constexpr uint32_t TAG_BITMAPTRANSPARENTFILL        = 3206;
            constexpr uint32_t TAG_FRACTALTRANSPARENTFILL       = 3207;
            constexpr uint32_t TAG_NOISETRANSPARENTFILL         = 3208;
            constexpr uint32_t TAG_THREECOLTRANSPARENTFILL      = 3209;
            constexpr uint32_t TAG_FOURCOLTRANSPARENTFILL       = 3210;
            constexpr uint32_t TAG_SQUARETRANSPARENTFILL        = 3211;

            // Attribute Tags - Font/Text Attributes (3300-3399)
            constexpr uint32_t TAG_FONTDEFAULT                  = 3300;
            constexpr uint32_t TAG_FONTNAME                     = 3301;
            constexpr uint32_t TAG_FONTSIZE                     = 3302;
            constexpr uint32_t TAG_FONTBOLD                     = 3303;
            constexpr uint32_t TAG_FONTITALIC                   = 3304;
            constexpr uint32_t TAG_FONTUNDERLINE                = 3305;
            constexpr uint32_t TAG_FONTSCRIPT                   = 3306;
            constexpr uint32_t TAG_JUSTIFICATION                = 3307;
            constexpr uint32_t TAG_TRACKING                     = 3308;
            constexpr uint32_t TAG_ASPECTRATIO                  = 3309;
            constexpr uint32_t TAG_BASELINE                     = 3310;
            constexpr uint32_t TAG_LEFTMARGIN                   = 3311;
            constexpr uint32_t TAG_RIGHTMARGIN                  = 3312;
            constexpr uint32_t TAG_FIRSTINDENT                  = 3313;
            constexpr uint32_t TAG_RULER                        = 3314;
            constexpr uint32_t TAG_LINESPACING                  = 3315;

            // Attribute Tags - Quality/Rendering (3400-3499)
            constexpr uint32_t TAG_QUALITY                      = 3400;

            // Attribute Tags - Feather (3500-3599)
            constexpr uint32_t TAG_FEATHER                      = 3500;

            // Effect Tags (3600-3699)
            constexpr uint32_t TAG_LIVEEFFECT                   = 3600;
            constexpr uint32_t TAG_LOCKEDEFFECT                 = 3601;
            constexpr uint32_t TAG_FEATHEREFFECT                = 3602;

            // Shadow Tags (3700-3799)
            constexpr uint32_t TAG_SHADOWCONTROLLER             = 3700;
            constexpr uint32_t TAG_SHADOW                       = 3701;

            // Bevel Tags (3800-3899)
            constexpr uint32_t TAG_BEVELATTR                    = 3800;
            constexpr uint32_t TAG_BEVEL                        = 3801;
            constexpr uint32_t TAG_BEVELTRAPEZOID               = 3802;

            // Contour Tags (3900-3999)
            constexpr uint32_t TAG_CONTOURCONTROLLER            = 3900;
            constexpr uint32_t TAG_CONTOUR                      = 3901;

            // Bitmap Definition Tags (4000-4099)
            constexpr uint32_t TAG_DEFINEBITMAP_JPEG            = 4000;
            constexpr uint32_t TAG_DEFINEBITMAP_PNG             = 4001;
            constexpr uint32_t TAG_DEFINEBITMAP_JPEG8BPP        = 4002;
            constexpr uint32_t TAG_DEFINEBITMAP_BMP             = 4008;
            constexpr uint32_t TAG_DEFINEBITMAP_GIF             = 4009;
            constexpr uint32_t TAG_DEFINEBITMAP_JPEG_PROG       = 4010;
            constexpr uint32_t TAG_DEFINEBITMAP_PNG_ALPHA       = 4011;

            // Bitmap Reference Tags (4100-4199)
            constexpr uint32_t TAG_BITMAPREF                    = 4100;

            // Named Colour Tags (4200-4299)
            constexpr uint32_t TAG_NAMEDCOLOUR                  = 4200;

            // Guideline Tags (4300-4399)
            constexpr uint32_t TAG_HORIZONTAL_GUIDELINE         = 4300;
            constexpr uint32_t TAG_VERTICAL_GUIDELINE           = 4301;

            // Web Export Tags (4400-4499)
            constexpr uint32_t TAG_WEBURL                       = 4400;

            // Print Tags (4500-4599)
            constexpr uint32_t TAG_PRINTONALLPLATES             = 4500;
            constexpr uint32_t TAG_PRINTCOLOURSASPOT            = 4501;

            // ClipView Tags (4600-4699)
            constexpr uint32_t TAG_CLIPVIEWCONTROLLER           = 4600;
            constexpr uint32_t TAG_CLIPVIEW                     = 4601;

            // Brush Tags (5000-5099)
            constexpr uint32_t TAG_BRUSHATTR                    = 5000;
            constexpr uint32_t TAG_BRUSHDEFINITION              = 5001;
            constexpr uint32_t TAG_BRUSHDATA                    = 5002;
            constexpr uint32_t TAG_MOREBRUSHDATA                = 5003;
            constexpr uint32_t TAG_MOREBRUSHATTR                = 5004;

            // Stroke Provider Tags (5100-5199)
            constexpr uint32_t TAG_STROKETYPE                   = 5100;
            constexpr uint32_t TAG_VARIABLEWIDTHFUNC            = 5101;
            constexpr uint32_t TAG_VARIABLEWIDTHTABLE           = 5102;

            // Node Attribute Tags (5200-5299)
            constexpr uint32_t TAG_NODEATTRIBUTE                = 5200;

            // Sentinel
            constexpr uint32_t TAG_ENDOFTAGS                    = 0xFFFFFFFF;
        }

// ===== XAR PATH VERBS (Path Command Encoding) =====

        namespace XARPathVerbs {
            constexpr uint8_t VERB_MOVETO    = 0x06;  // MoveTo with flags
            constexpr uint8_t VERB_LINETO    = 0x02;  // LineTo
            constexpr uint8_t VERB_CURVETO   = 0x04;  // CurveTo (Bezier)
            constexpr uint8_t VERB_CLOSEPATH = 0x01;  // ClosePath

            // Path verb flags
            constexpr uint8_t PATHFLAG_ENDPOINT = 0x00;  // Endpoint
            constexpr uint8_t PATHFLAG_CONTROL  = 0x08;  // Control point for bezier
        }

// ===== XAR BINARY STRUCTURES =====

#pragma pack(push, 1)

        struct XARFileHeader {
            uint8_t  Signature[8];      // "XARA" 0xA3 0xA3 0x0D 0x0A
            uint32_t FileSize;          // Total file size
            uint32_t Version;           // File format version
            uint32_t BuildNumber;       // Build that created file
            uint32_t PreCompFlags;      // Pre-compression flags
            uint32_t Checksum;          // CRC32 checksum
        };

        struct XARRecordHeader {
            uint32_t Tag;               // Record type (XARTags::TAG_*)
            uint32_t Size;              // Record size (excluding header)
        };

// Coordinate in millipoints (INT32)
        struct XARCoord {
            int32_t x;
            int32_t y;
        };

// Transformation matrix (fixed-point 16.16)
        struct XARMatrix {
            int32_t a, b;   // [a c e]
            int32_t c, d;   // [b d f]
            int32_t e, f;   // [0 0 1]
        };

// Colour record structure
        struct XARColourRGB {
            uint8_t Red;
            uint8_t Green;
            uint8_t Blue;
            uint8_t Reserved;
        };

        struct XARColourCMYK {
            uint8_t Cyan;
            uint8_t Magenta;
            uint8_t Yellow;
            uint8_t Key;
        };

// Gradient fill start/end record
        struct XARLinearFillData {
            XARCoord StartPoint;
            XARCoord EndPoint;
            XARCoord EndPoint2;         // For 3-point fill
            XARColourRGB StartColour;
            XARColourRGB EndColour;
        };

        struct XARRadialFillData {
            XARCoord CentrePoint;
            XARCoord MajorAxes;
            XARCoord MinorAxes;
            XARColourRGB StartColour;
            XARColourRGB EndColour;
        };

        struct XARConicalFillData {
            XARCoord CentrePoint;
            XARCoord EndPoint;
            XARColourRGB StartColour;
            XARColourRGB EndColour;
        };

        struct XARThreeColFillData {
            XARCoord Point1;
            XARCoord Point2;
            XARCoord Point3;
            XARColourRGB Colour1;
            XARColourRGB Colour2;
            XARColourRGB Colour3;
        };

        struct XARFourColFillData {
            XARCoord Point1;
            XARCoord Point2;
            XARCoord Point3;
            XARCoord Point4;
            XARColourRGB Colour1;
            XARColourRGB Colour2;
            XARColourRGB Colour3;
            XARColourRGB Colour4;
        };

        struct XARFeatherData {
            int32_t FeatherSize;        // Feather radius in millipoints
            uint8_t FeatherType;        // 0=simple, 1=profile
            uint8_t Reserved[3];
        };

#pragma pack(pop)

// ===== XAR CONVERTER OPTIONS =====

        struct XARConversionOptions {
            bool UseCompression = true;
            bool ProgressiveRendering = true;
            bool PreserveLayers = true;
            bool PreserveEffects = true;
            bool StrictMode = false;
            float FeatherFallbackOpacity = 0.5f;  // For renderers without feather support

            // Callbacks
            std::function<void(const std::string&)> WarningCallback;
            std::function<void(float)> ProgressCallback;
        };

// ===== XAR CONVERTER CLASS =====

        class XARConverter : public IVectorFormatConverter {
        public:
            XARConverter();
            virtual ~XARConverter();

            // IVectorFormatConverter interface
            VectorFormat GetFormat() const override { return VectorFormat::XAR; }
            std::string GetFormatName() const override { return "Xara Format"; }
            std::string GetFormatVersion() const override { return "2.0"; }
            std::vector<std::string> GetFileExtensions() const override { return {".xar", ".xar", ".web"}; }
            std::string GetMimeType() const override { return "application/x-xara"; }

            FormatCapabilities GetCapabilities() const override;
            bool CanImport() const override { return true; }
            bool CanExport() const override { return true; }

            // Import methods
            std::shared_ptr<VectorStorage::VectorDocument> Import(
                    const std::string& filename,
                    const ConversionOptions& options = ConversionOptions()) override;

            std::shared_ptr<VectorStorage::VectorDocument> ImportFromString(
                    const std::string& data,
                    const ConversionOptions& options = ConversionOptions()) override;

            std::shared_ptr<VectorStorage::VectorDocument> ImportFromStream(
                    std::istream& stream,
                    const ConversionOptions& options = ConversionOptions()) override;

            // Export methods
            bool Export(
                    const VectorStorage::VectorDocument& document,
                    const std::string& filename,
                    const ConversionOptions& options = ConversionOptions()) override;

            std::string ExportToString(
                    const VectorStorage::VectorDocument& document,
                    const ConversionOptions& options = ConversionOptions()) override;

            bool ExportToStream(
                    const VectorStorage::VectorDocument& document,
                    std::ostream& stream,
                    const ConversionOptions& options = ConversionOptions()) override;

            // Validation
            bool ValidateFile(const std::string& filename) const override;
            bool ValidateData(const std::string& data) const override;

            // XAR-specific options
            void SetXAROptions(const XARConversionOptions& opts) { xarOptions = opts; }
            const XARConversionOptions& GetXAROptions() const { return xarOptions; }

        private:
            class Impl;
            std::unique_ptr<Impl> impl;
            XARConversionOptions xarOptions;
        };

// ===== COORDINATE CONVERSION UTILITIES =====

        namespace XARCoordUtils {
            // Convert point to XAR millipoints
            inline XARCoord ToXARCoord(const Point2Df& point) {
                return XARCoord{
                        static_cast<int32_t>(point.x * XAR_MILLIPOINTS_PER_POINT),
                        static_cast<int32_t>(point.y * XAR_MILLIPOINTS_PER_POINT)
                };
            }

            // Convert XAR millipoints to point
            inline Point2Df FromXARCoord(const XARCoord& coord) {
                return Point2Df{
                        static_cast<float>(coord.x) / XAR_MILLIPOINTS_PER_POINT,
                        static_cast<float>(coord.y) / XAR_MILLIPOINTS_PER_POINT
                };
            }

            // Convert to 16.16 fixed point
            inline int32_t FloatToFixed16(float value) {
                return static_cast<int32_t>(value * 65536.0f);
            }

            // Convert from 16.16 fixed point
            inline float FixedToFloat16(int32_t value) {
                return static_cast<float>(value) / 65536.0f;
            }

            // Convert matrix
            inline XARMatrix ToXARMatrix(const VectorStorage::Matrix3x3& matrix) {
                return XARMatrix{
                        FloatToFixed16(matrix.m[0][0]),
                        FloatToFixed16(matrix.m[1][0]),
                        FloatToFixed16(matrix.m[0][1]),
                        FloatToFixed16(matrix.m[1][1]),
                        FloatToFixed16(matrix.m[0][2] * XAR_MILLIPOINTS_PER_POINT),
                        FloatToFixed16(matrix.m[1][2] * XAR_MILLIPOINTS_PER_POINT)
                };
            }

            inline VectorStorage::Matrix3x3 FromXARMatrix(const XARMatrix& matrix) {
                return VectorStorage::Matrix3x3::FromValues(
                        FixedToFloat16(matrix.a),
                        FixedToFloat16(matrix.b),
                        FixedToFloat16(matrix.c),
                        FixedToFloat16(matrix.d),
                        FixedToFloat16(matrix.e) / XAR_MILLIPOINTS_PER_POINT,
                        FixedToFloat16(matrix.f) / XAR_MILLIPOINTS_PER_POINT
                );
            }
        }

// ===== COLOUR CONVERSION UTILITIES =====

        namespace XARColourUtils {
            inline XARColourRGB ToXARColour(const Color& color) {
                return XARColourRGB{color.r, color.g, color.b, color.a};
            }

            inline Color FromXARColour(const XARColourRGB& color) {
                return Color(color.Red, color.Green, color.Blue, color.Reserved);
            }

            inline Color FromXARColourCMYK(const XARColourCMYK& cmyk) {
                // CMYK to RGB conversion
                float c = cmyk.Cyan / 255.0f;
                float m = cmyk.Magenta / 255.0f;
                float y = cmyk.Yellow / 255.0f;
                float k = cmyk.Key / 255.0f;

                uint8_t r = static_cast<uint8_t>(255 * (1 - c) * (1 - k));
                uint8_t g = static_cast<uint8_t>(255 * (1 - m) * (1 - k));
                uint8_t b = static_cast<uint8_t>(255 * (1 - y) * (1 - k));

                return Color(r, g, b, 255);
            }
        }

    } // namespace VectorConverter
} // namespace UltraCanvas