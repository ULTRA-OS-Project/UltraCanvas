// UltraCanvas/Plugins/Vector/UltraCanvasXARConverter.cpp
// XAR (Xara) Vector Format Converter - Specification-Compliant Implementation
// Version: 2.0.0
// Last Modified: 2025-01-20
// Author: UltraCanvas Framework

#include "UltraCanvasXARConverter.h"
#include "UltraCanvasVectorStorage.h"
#include "UltraCanvasVectorPathOps.h"
#include <fstream>
#include <sstream>
#include <cstring>
#include <zlib.h>
#include <stack>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <map>
#include <variant>

namespace UltraCanvas {
    namespace VectorConverter {

        using namespace VectorStorage;
        using namespace XARTags;
        using namespace XARPathVerbs;
        using namespace XARCoordUtils;
        using namespace XARColourUtils;

// ===== XAR CONVERTER IMPLEMENTATION CLASS =====

        class XARConverter::Impl {
        public:
            Impl() = default;
            ~Impl() = default;

            // Import from file
            std::shared_ptr<VectorDocument> ImportFromFile(
                    const std::string& filename,
                    const ConversionOptions& options,
                    const XARConversionOptions& xarOptions);

            // Import from memory
            std::shared_ptr<VectorDocument> ImportFromMemory(
                    const uint8_t* data, size_t size,
                    const ConversionOptions& options,
                    const XARConversionOptions& xarOptions);

            // Export to file
            bool ExportToFile(
                    const VectorDocument& document,
                    const std::string& filename,
                    const ConversionOptions& options,
                    const XARConversionOptions& xarOptions);

            // Export to memory
            std::vector<uint8_t> ExportToMemory(
                    const VectorDocument& document,
                    const ConversionOptions& options,
                    const XARConversionOptions& xarOptions);

        private:
            // ===== IMPORT STATE =====
            struct ImportState {
                std::shared_ptr<VectorDocument> document;
                std::shared_ptr<VectorLayer> currentLayer;
                std::shared_ptr<VectorGroup> currentGroup;
                std::stack<std::shared_ptr<VectorGroup>> groupStack;
                std::shared_ptr<VectorPath> currentPath;

                // Current attributes
                VectorStyle currentStyle;
                Matrix3x3 currentTransform;

                // Defined resources
                std::map<uint32_t, std::shared_ptr<VectorElement>> objectRefs;
                std::map<uint32_t, Color> namedColours;
                std::map<uint32_t, std::vector<uint8_t>> bitmapData;
                std::map<std::string, std::string> fontMap;

                uint32_t nextRefId = 1;

                void Reset() {
                    document.reset();
                    currentLayer.reset();
                    currentGroup.reset();
                    while (!groupStack.empty()) groupStack.pop();
                    currentPath.reset();
                    currentStyle = VectorStyle();
                    currentTransform = Matrix3x3::Identity();
                    objectRefs.clear();
                    namedColours.clear();
                    bitmapData.clear();
                    fontMap.clear();
                    nextRefId = 1;
                }
            } importState;

            // ===== EXPORT STATE =====
            struct ExportState {
                std::map<const VectorElement*, uint32_t> elementRefs;
                std::map<size_t, uint32_t> gradientRefs;
                std::map<size_t, uint32_t> patternRefs;
                std::map<Color, uint32_t> colourRefs;
                uint32_t nextRefId = 1;
                uint32_t nextColourId = 1;

                void Reset() {
                    elementRefs.clear();
                    gradientRefs.clear();
                    patternRefs.clear();
                    colourRefs.clear();
                    nextRefId = 1;
                    nextColourId = 1;
                }
            } exportState;

            // ===== COMPRESSION STATE =====
            bool compressionEnabled = false;
            bool inCompressedBlock = false;
            std::vector<uint8_t> compressionBuffer;
            size_t uncompressedSize = 0;

            // Options
            ConversionOptions currentOptions;
            XARConversionOptions currentXarOptions;

            // ===== READING HELPERS =====
            bool ReadFileHeader(std::istream& stream, XARFileHeader& header);
            bool ReadRecord(std::istream& stream, XARRecordHeader& header, std::vector<uint8_t>& data);
            bool ProcessRecord(uint32_t tag, const std::vector<uint8_t>& data);

            // ===== RECORD PROCESSORS =====
            void ProcessDocumentStructure(uint32_t tag, const std::vector<uint8_t>& data);
            void ProcessLayer(const std::vector<uint8_t>& data);
            void ProcessGroup(uint32_t tag);
            void ProcessPath(uint32_t tag, const std::vector<uint8_t>& data);
            void ProcessRectangle(uint32_t tag, const std::vector<uint8_t>& data);
            void ProcessEllipse(uint32_t tag, const std::vector<uint8_t>& data);
            void ProcessPolygon(uint32_t tag, const std::vector<uint8_t>& data);
            void ProcessText(uint32_t tag, const std::vector<uint8_t>& data);
            void ProcessBitmap(uint32_t tag, const std::vector<uint8_t>& data);

            // Attribute processors
            void ProcessLineAttribute(uint32_t tag, const std::vector<uint8_t>& data);
            void ProcessFillAttribute(uint32_t tag, const std::vector<uint8_t>& data);
            void ProcessTransparency(uint32_t tag, const std::vector<uint8_t>& data);
            void ProcessTextAttribute(uint32_t tag, const std::vector<uint8_t>& data);
            void ProcessTransform(const std::vector<uint8_t>& data);

            // Effect processors
            void ProcessFeather(const std::vector<uint8_t>& data);
            void ProcessShadow(uint32_t tag, const std::vector<uint8_t>& data);
            void ProcessBevel(uint32_t tag, const std::vector<uint8_t>& data);
            void ProcessContour(uint32_t tag, const std::vector<uint8_t>& data);
            void ProcessBlend(uint32_t tag, const std::vector<uint8_t>& data);
            void ProcessMould(uint32_t tag, const std::vector<uint8_t>& data);

            // Colour processors
            void ProcessDefineColour(uint32_t tag, const std::vector<uint8_t>& data);
            void ProcessBitmapDefinition(uint32_t tag, const std::vector<uint8_t>& data);

            // ===== PATH PARSING =====
            void ParsePathData(const std::vector<uint8_t>& data, bool relative, VectorPath& path);

            // ===== COMPRESSION =====
            std::vector<uint8_t> CompressData(const std::vector<uint8_t>& data);
            std::vector<uint8_t> DecompressData(const std::vector<uint8_t>& compressed, size_t uncompSize);

            // ===== UTILITY FUNCTIONS =====
            void AddElementToCurrentContainer(std::shared_ptr<VectorElement> element);
            void LogWarning(const std::string& message);
            void ReportProgress(float progress);
        };

// ===== PUBLIC INTERFACE =====

        XARConverter::XARConverter() : impl(std::make_unique<Impl>()) {}
        XARConverter::~XARConverter() = default;

        FormatCapabilities XARConverter::GetCapabilities() const {
            FormatCapabilities caps;

            // Basic shapes
            caps.SupportsRectangle = true;
            caps.SupportsCircle = true;
            caps.SupportsEllipse = true;
            caps.SupportsLine = true;
            caps.SupportsPolyline = true;
            caps.SupportsPolygon = true;
            caps.SupportsPath = true;

            // Path features
            caps.SupportsCubicBezier = true;
            caps.SupportsQuadraticBezier = true;
            caps.SupportsArc = true;
            caps.SupportsCompoundPaths = true;

            // Text
            caps.SupportsText = true;
            caps.SupportsTextPath = true;
            caps.SupportsRichText = true;
            caps.SupportsEmbeddedFonts = true;

            // Fills & Strokes
            caps.SupportsSolidFill = true;
            caps.SupportsLinearGradient = true;
            caps.SupportsRadialGradient = true;
            caps.SupportsConicalGradient = true;   // XAR specialty
            caps.SupportsMeshGradient = false;      // Not directly, but has 3/4 colour fills
            caps.SupportsPattern = true;
            caps.SupportsDashing = true;
            caps.SupportsVariableStrokeWidth = true;

            // Effects
            caps.SupportsOpacity = true;
            caps.SupportsBlendModes = true;
            caps.SupportsFilters = true;
            caps.SupportsClipping = true;
            caps.SupportsMasking = true;
            caps.SupportsDropShadow = true;

            // Structure
            caps.SupportsGroups = true;
            caps.SupportsLayers = true;
            caps.SupportsSymbols = true;
            caps.SupportsPages = true;

            // Advanced XAR features
            caps.SupportsNonDestructiveEffects = true;

            return caps;
        }

        std::shared_ptr<VectorDocument> XARConverter::Import(
                const std::string& filename,
                const ConversionOptions& options) {
            return impl->ImportFromFile(filename, options, xarOptions);
        }

        std::shared_ptr<VectorDocument> XARConverter::ImportFromString(
                const std::string& data,
                const ConversionOptions& options) {
            return impl->ImportFromMemory(
                    reinterpret_cast<const uint8_t*>(data.data()),
                    data.size(), options, xarOptions);
        }

        std::shared_ptr<VectorDocument> XARConverter::ImportFromStream(
                std::istream& stream,
                const ConversionOptions& options) {
            // Read entire stream into memory
            stream.seekg(0, std::ios::end);
            size_t size = stream.tellg();
            stream.seekg(0, std::ios::beg);

            std::vector<uint8_t> data(size);
            stream.read(reinterpret_cast<char*>(data.data()), size);

            return impl->ImportFromMemory(data.data(), size, options, xarOptions);
        }

        bool XARConverter::Export(
                const VectorDocument& document,
                const std::string& filename,
                const ConversionOptions& options) {
            return impl->ExportToFile(document, filename, options, xarOptions);
        }

        std::string XARConverter::ExportToString(
                const VectorDocument& document,
                const ConversionOptions& options) {
            auto data = impl->ExportToMemory(document, options, xarOptions);
            return std::string(data.begin(), data.end());
        }

        bool XARConverter::ExportToStream(
                const VectorDocument& document,
                std::ostream& stream,
                const ConversionOptions& options) {
            auto data = impl->ExportToMemory(document, options, xarOptions);
            stream.write(reinterpret_cast<const char*>(data.data()), data.size());
            return stream.good();
        }

        bool XARConverter::ValidateFile(const std::string& filename) const {
            std::ifstream file(filename, std::ios::binary);
            if (!file.is_open()) return false;

            uint8_t signature[8];
            file.read(reinterpret_cast<char*>(signature), sizeof(signature));

            return std::memcmp(signature, XAR_SIGNATURE, sizeof(XAR_SIGNATURE)) == 0;
        }

        bool XARConverter::ValidateData(const std::string& data) const {
            if (data.size() < sizeof(XAR_SIGNATURE)) return false;
            return std::memcmp(data.data(), XAR_SIGNATURE, sizeof(XAR_SIGNATURE)) == 0;
        }

// ===== IMPLEMENTATION: IMPORT =====

        std::shared_ptr<VectorDocument> XARConverter::Impl::ImportFromFile(
                const std::string& filename,
                const ConversionOptions& options,
                const XARConversionOptions& xarOptions) {

            std::ifstream file(filename, std::ios::binary);
            if (!file.is_open()) {
                LogWarning("Failed to open XAR file: " + filename);
                return nullptr;
            }

            // Get file size for progress reporting
            file.seekg(0, std::ios::end);
            size_t fileSize = file.tellg();
            file.seekg(0, std::ios::beg);

            currentOptions = options;
            currentXarOptions = xarOptions;
            importState.Reset();

            // Read and validate header
            XARFileHeader header;
            if (!ReadFileHeader(file, header)) {
                LogWarning("Invalid XAR file header");
                return nullptr;
            }

            // Initialize document
            importState.document = std::make_shared<VectorDocument>();
            importState.currentLayer = importState.document->AddLayer("Default Layer");

            // Read records
            XARRecordHeader recordHeader;
            std::vector<uint8_t> recordData;
            size_t bytesRead = sizeof(XARFileHeader);

            while (file.good() && !file.eof()) {
                if (!ReadRecord(file, recordHeader, recordData)) {
                    break;
                }

                bytesRead += sizeof(XARRecordHeader) + recordHeader.Size;
                ReportProgress(static_cast<float>(bytesRead) / fileSize);

                if (!ProcessRecord(recordHeader.Tag, recordData)) {
                    if (currentOptions.ErrorHandling == ConversionOptions::ErrorMode::Strict) {
                        return nullptr;
                    }
                }

                // Check for end of file
                if (recordHeader.Tag == TAG_ENDOFFILE) {
                    break;
                }
            }

            ReportProgress(1.0f);
            return importState.document;
        }

        std::shared_ptr<VectorDocument> XARConverter::Impl::ImportFromMemory(
                const uint8_t* data, size_t size,
                const ConversionOptions& options,
                const XARConversionOptions& xarOptions) {

            std::istringstream stream(std::string(reinterpret_cast<const char*>(data), size),
                                      std::ios::binary);

            currentOptions = options;
            currentXarOptions = xarOptions;
            importState.Reset();

            // Read and validate header
            XARFileHeader header;
            if (!ReadFileHeader(stream, header)) {
                LogWarning("Invalid XAR file header");
                return nullptr;
            }

            // Initialize document
            importState.document = std::make_shared<VectorDocument>();
            importState.currentLayer = importState.document->AddLayer("Default Layer");

            // Read records
            XARRecordHeader recordHeader;
            std::vector<uint8_t> recordData;

            while (stream.good() && !stream.eof()) {
                if (!ReadRecord(stream, recordHeader, recordData)) {
                    break;
                }

                if (!ProcessRecord(recordHeader.Tag, recordData)) {
                    if (currentOptions.ErrorHandling == ConversionOptions::ErrorMode::Strict) {
                        return nullptr;
                    }
                }

                if (recordHeader.Tag == TAG_ENDOFFILE) {
                    break;
                }
            }

            return importState.document;
        }

// ===== FILE READING =====

        bool XARConverter::Impl::ReadFileHeader(std::istream& stream, XARFileHeader& header) {
            stream.read(reinterpret_cast<char*>(&header), sizeof(header));

            if (!stream.good()) return false;

            // Validate signature
            if (std::memcmp(header.Signature, XAR_SIGNATURE, sizeof(XAR_SIGNATURE)) != 0) {
                return false;
            }

            // Set document size if available
            if (importState.document) {
                // XAR doesn't store document dimensions in header, set defaults
                importState.document->Size = Size2Dd{595.0f, 842.0f};  // A4 in points
            }

            return true;
        }

        bool XARConverter::Impl::ReadRecord(std::istream& stream,
                                            XARRecordHeader& header,
                                            std::vector<uint8_t>& data) {
            stream.read(reinterpret_cast<char*>(&header), sizeof(header));
            if (!stream.good()) return false;

            data.clear();
            if (header.Size > 0) {
                data.resize(header.Size);
                stream.read(reinterpret_cast<char*>(data.data()), header.Size);
                if (!stream.good()) return false;
            }

            // Handle compression
            if (header.Tag == TAG_STARTCOMPRESSION) {
                inCompressedBlock = true;
                compressionBuffer.clear();
                if (data.size() >= sizeof(uint32_t)) {
                    uncompressedSize = *reinterpret_cast<const uint32_t*>(data.data());
                }
                return true;
            }

            if (header.Tag == TAG_ENDCOMPRESSION) {
                inCompressedBlock = false;
                if (!compressionBuffer.empty()) {
                    data = DecompressData(compressionBuffer, uncompressedSize);
                    compressionBuffer.clear();
                }
                return true;
            }

            if (inCompressedBlock) {
                compressionBuffer.insert(compressionBuffer.end(), data.begin(), data.end());
                // Don't process yet - accumulate compressed data
                header.Tag = TAG_UNDEFINED;  // Mark as no-op
                return true;
            }

            return true;
        }

// ===== RECORD PROCESSING =====

        bool XARConverter::Impl::ProcessRecord(uint32_t tag, const std::vector<uint8_t>& data) {
            try {
                // Navigation tags
                if (tag == TAG_UP) {
                    if (!importState.groupStack.empty()) {
                        importState.currentGroup = importState.groupStack.top();
                        importState.groupStack.pop();
                    }
                    return true;
                }

                if (tag == TAG_DOWN) {
                    // Going down in tree - current element becomes container
                    return true;
                }

                // Document structure
                if (tag == TAG_DOCUMENT || tag == TAG_CHAPTER ||
                    tag == TAG_SPREAD || tag == TAG_PAGE) {
                    ProcessDocumentStructure(tag, data);
                    return true;
                }

                if (tag == TAG_LAYER || tag == TAG_LAYERDETAILS) {
                    ProcessLayer(data);
                    return true;
                }

                // Groups
                if (tag == TAG_GROUP) {
                    ProcessGroup(tag);
                    return true;
                }

                // Paths
                if (tag >= TAG_PATH && tag <= TAG_PATH_RELATIVE_FILLED_STROKED) {
                    ProcessPath(tag, data);
                    return true;
                }

                // Rectangles
                if (tag >= TAG_RECTANGLE_SIMPLE && tag <= TAG_RECTANGLE_COMPLEX_ROUNDED_STELLATED_REFORMED) {
                    ProcessRectangle(tag, data);
                    return true;
                }

                // Ellipses
                if (tag >= TAG_ELLIPSE_SIMPLE && tag <= TAG_ELLIPSE_COMPLEX) {
                    ProcessEllipse(tag, data);
                    return true;
                }

                // Polygons (QuickShapes)
                if (tag >= TAG_POLYGON_COMPLEX && tag <= TAG_POLYGON_COMPLEX_ROUNDED_STELLATED_REFORMED) {
                    ProcessPolygon(tag, data);
                    return true;
                }

                // Text
                if (tag >= TAG_TEXT_STORY_SIMPLE && tag <= TAG_TEXT_TAB) {
                    ProcessText(tag, data);
                    return true;
                }

                // Bitmaps
                if (tag == TAG_NODE_BITMAP || tag == TAG_NODE_CONTONEDBITMAP) {
                    ProcessBitmap(tag, data);
                    return true;
                }

                // Line/Stroke attributes
                if (tag >= TAG_LINECOLOUR && tag <= TAG_ENDARROW) {
                    ProcessLineAttribute(tag, data);
                    return true;
                }

                // Fill attributes
                if (tag >= TAG_FLATFILL && tag <= TAG_SQUAREFILL) {
                    ProcessFillAttribute(tag, data);
                    return true;
                }

                // Transparency attributes
                if (tag >= TAG_FLATTRANSPARENTFILL && tag <= TAG_SQUARETRANSPARENTFILL) {
                    ProcessTransparency(tag, data);
                    return true;
                }

                // Text attributes
                if (tag >= TAG_FONTDEFAULT && tag <= TAG_LINESPACING) {
                    ProcessTextAttribute(tag, data);
                    return true;
                }

                // Feather
                if (tag == TAG_FEATHER || tag == TAG_FEATHEREFFECT) {
                    ProcessFeather(data);
                    return true;
                }

                // Shadow
                if (tag == TAG_SHADOW || tag == TAG_SHADOWCONTROLLER) {
                    ProcessShadow(tag, data);
                    return true;
                }

                // Bevel
                if (tag >= TAG_BEVELATTR && tag <= TAG_BEVELTRAPEZOID) {
                    ProcessBevel(tag, data);
                    return true;
                }

                // Contour
                if (tag == TAG_CONTOUR || tag == TAG_CONTOURCONTROLLER) {
                    ProcessContour(tag, data);
                    return true;
                }

                // Blend
                if (tag >= TAG_BLEND && tag <= TAG_BLENDPATH) {
                    ProcessBlend(tag, data);
                    return true;
                }

                // Mould
                if (tag >= TAG_MOULD_ENVELOPE && tag <= TAG_MOULDPATH) {
                    ProcessMould(tag, data);
                    return true;
                }

                // Colour definitions
                if (tag == TAG_DEFINERGBCOLOUR || tag == TAG_DEFINECOMPLEXCOLOUR) {
                    ProcessDefineColour(tag, data);
                    return true;
                }

                // Bitmap definitions
                if (tag >= TAG_DEFINEBITMAP_JPEG && tag <= TAG_DEFINEBITMAP_PNG_ALPHA) {
                    ProcessBitmapDefinition(tag, data);
                    return true;
                }

                // End of file
                if (tag == TAG_ENDOFFILE) {
                    return true;
                }

                // Unknown tag - skip
                return true;

            } catch (const std::exception& e) {
                LogWarning(std::string("Error processing tag ") + std::to_string(tag) + ": " + e.what());
                return false;
            }
        }

// ===== DOCUMENT STRUCTURE =====

        void XARConverter::Impl::ProcessDocumentStructure(uint32_t tag, const std::vector<uint8_t>& data) {
            if (tag == TAG_SPREADINFORMATION && data.size() >= 16) {
                // Extract page size from spread information
                size_t offset = 0;
                XARCoord lo = *reinterpret_cast<const XARCoord*>(data.data() + offset);
                offset += sizeof(XARCoord);
                XARCoord hi = *reinterpret_cast<const XARCoord*>(data.data() + offset);

                Point2Dd loPoint = FromXARCoord(lo);
                Point2Dd hiPoint = FromXARCoord(hi);

                importState.document->Size.width = hiPoint.x - loPoint.x;
                importState.document->Size.height = hiPoint.y - loPoint.y;
                importState.document->ViewBox = Rect2Dd{loPoint.x, loPoint.y,
                                                        importState.document->Size.width,
                                                        importState.document->Size.height};
            }
        }

        void XARConverter::Impl::ProcessLayer(const std::vector<uint8_t>& data) {
            auto layer = std::make_shared<VectorLayer>();
            layer->Type = VectorElementType::Layer;

            // Extract layer name if present (null-terminated string)
            if (!data.empty()) {
                size_t nameEnd = 0;
                while (nameEnd < data.size() && data[nameEnd] != 0) nameEnd++;
                layer->Name = std::string(reinterpret_cast<const char*>(data.data()), nameEnd);
            } else {
                layer->Name = "Layer " + std::to_string(importState.document->Layers.size() + 1);
            }

            // Extract layer flags if present
            if (data.size() > layer->Name.size() + 1) {
                size_t offset = layer->Name.size() + 1;
                if (offset + 4 <= data.size()) {
                    uint32_t flags = *reinterpret_cast<const uint32_t*>(data.data() + offset);
                    layer->Visible = (flags & 0x01) != 0;
                    layer->Locked = (flags & 0x02) != 0;
                }
            }

            importState.document->Layers.push_back(layer);
            importState.currentLayer = layer;
        }

        void XARConverter::Impl::ProcessGroup(uint32_t tag) {
            auto group = std::make_shared<VectorGroup>();
            group->Type = VectorElementType::Group;
            group->Style = importState.currentStyle;

            AddElementToCurrentContainer(group);

            // Push current group and make this the new current
            if (importState.currentGroup) {
                importState.groupStack.push(importState.currentGroup);
            }
            importState.currentGroup = group;
        }

// ===== PATH PROCESSING =====

        void XARConverter::Impl::ProcessPath(uint32_t tag, const std::vector<uint8_t>& data) {
            auto path = std::make_shared<VectorPath>();
            path->Type = VectorElementType::Path;

            bool relative = (tag >= TAG_PATH_RELATIVE && tag <= TAG_PATH_RELATIVE_FILLED_STROKED);
            bool filled = (tag == TAG_PATH_FILLED || tag == TAG_PATH_FILLED_STROKED ||
                           tag == TAG_PATH_RELATIVE_FILLED || tag == TAG_PATH_RELATIVE_FILLED_STROKED);
            bool stroked = (tag == TAG_PATH_STROKED || tag == TAG_PATH_FILLED_STROKED ||
                            tag == TAG_PATH_RELATIVE_STROKED || tag == TAG_PATH_RELATIVE_FILLED_STROKED);

            ParsePathData(data, relative, *path);

            // Apply current style
            path->Style = importState.currentStyle;

            // Modify style based on path type
            if (!filled) {
                path->Style.Fill.reset();
            }
            if (!stroked) {
                path->Style.Stroke.reset();
            }

            // Apply current transform
            if (importState.currentTransform.Determinant() != 0) {
                path->Transform = importState.currentTransform;
            }

            AddElementToCurrentContainer(path);
        }

        void XARConverter::Impl::ParsePathData(const std::vector<uint8_t>& data,
                                               bool relative,
                                               VectorPath& path) {
            if (data.size() < 4) return;

            size_t offset = 0;

            // Read number of elements
            uint32_t numElements = *reinterpret_cast<const uint32_t*>(data.data() + offset);
            offset += sizeof(uint32_t);

            Point2Dd currentPoint{0, 0};
            Point2Dd subpathStart{0, 0};

            // XAR stores verbs and coordinates separately
            // First: verb array (numElements bytes)
            // Then: coordinate array (numElements * sizeof(XARCoord))

            if (data.size() < offset + numElements + numElements * sizeof(XARCoord)) {
                LogWarning("Path data too short");
                return;
            }

            const uint8_t* verbs = data.data() + offset;
            offset += numElements;

            const XARCoord* coords = reinterpret_cast<const XARCoord*>(data.data() + offset);

            size_t coordIndex = 0;

            for (uint32_t i = 0; i < numElements && coordIndex < numElements; ) {
                uint8_t verb = verbs[i];

                // Check verb type (lower 3 bits determine type)
                uint8_t verbType = verb & 0x07;
                bool isControlPoint = (verb & PATHFLAG_CONTROL) != 0;

                if (verbType == (VERB_MOVETO & 0x07)) {
                    // MoveTo
                    Point2Dd pt = FromXARCoord(coords[coordIndex++]);
                    if (relative && i > 0) {
                        pt.x += currentPoint.x;
                        pt.y += currentPoint.y;
                    }
                    path.MoveTo(pt.x, pt.y);
                    currentPoint = pt;
                    subpathStart = pt;
                    i++;
                }
                else if (verbType == (VERB_LINETO & 0x07)) {
                    // LineTo
                    Point2Dd pt = FromXARCoord(coords[coordIndex++]);
                    if (relative) {
                        pt.x += currentPoint.x;
                        pt.y += currentPoint.y;
                    }
                    path.LineTo(pt.x, pt.y);
                    currentPoint = pt;
                    i++;
                }
                else if (verbType == (VERB_CURVETO & 0x07)) {
                    // CurveTo - need 3 points (2 control + 1 end)
                    if (coordIndex + 2 < numElements) {
                        Point2Dd c1 = FromXARCoord(coords[coordIndex++]);
                        Point2Dd c2 = FromXARCoord(coords[coordIndex++]);
                        Point2Dd end = FromXARCoord(coords[coordIndex++]);

                        if (relative) {
                            c1.x += currentPoint.x;
                            c1.y += currentPoint.y;
                            c2.x += currentPoint.x;
                            c2.y += currentPoint.y;
                            end.x += currentPoint.x;
                            end.y += currentPoint.y;
                        }

                        path.CurveTo(c1.x, c1.y, c2.x, c2.y, end.x, end.y);
                        currentPoint = end;
                        i += 3;  // Skip control points in verb array too
                    } else {
                        i++;
                    }
                }
                else if (verbType == (VERB_CLOSEPATH & 0x07)) {
                    // ClosePath
                    path.ClosePath();
                    currentPoint = subpathStart;
                    i++;
                }
                else {
                    // Unknown verb - skip
                    if (!isControlPoint) {
                        coordIndex++;
                    }
                    i++;
                }
            }
        }

// ===== SHAPE PROCESSING =====

        void XARConverter::Impl::ProcessRectangle(uint32_t tag, const std::vector<uint8_t>& data) {
            if (data.size() < 2 * sizeof(XARCoord)) return;

            auto rect = std::make_shared<VectorRect>();
            rect->Type = VectorElementType::Rectangle;

            size_t offset = 0;

            // Read bounds
            XARCoord lo = *reinterpret_cast<const XARCoord*>(data.data() + offset);
            offset += sizeof(XARCoord);
            XARCoord hi = *reinterpret_cast<const XARCoord*>(data.data() + offset);
            offset += sizeof(XARCoord);

            Point2Dd loPoint = FromXARCoord(lo);
            Point2Dd hiPoint = FromXARCoord(hi);

            rect->Bounds = Rect2Dd{loPoint.x, loPoint.y,
                                   hiPoint.x - loPoint.x,
                                   hiPoint.y - loPoint.y};

            // Check for rounded corners
            bool rounded = (tag >= TAG_RECTANGLE_SIMPLE_ROUNDED &&
                            tag <= TAG_RECTANGLE_COMPLEX_ROUNDED_STELLATED_REFORMED);

            if (rounded && offset + sizeof(int32_t) <= data.size()) {
                int32_t radius = *reinterpret_cast<const int32_t*>(data.data() + offset);
                rect->RadiusX = rect->RadiusY = static_cast<float>(radius) / XAR_MILLIPOINTS_PER_POINT;
            }

            rect->Style = importState.currentStyle;
            if (importState.currentTransform.Determinant() != 0) {
                rect->Transform = importState.currentTransform;
            }

            AddElementToCurrentContainer(rect);
        }

        void XARConverter::Impl::ProcessEllipse(uint32_t tag, const std::vector<uint8_t>& data) {
            if (data.size() < 3 * sizeof(XARCoord)) return;

            size_t offset = 0;

            // Read centre and axes
            XARCoord centre = *reinterpret_cast<const XARCoord*>(data.data() + offset);
            offset += sizeof(XARCoord);
            XARCoord majorAxis = *reinterpret_cast<const XARCoord*>(data.data() + offset);
            offset += sizeof(XARCoord);
            XARCoord minorAxis = *reinterpret_cast<const XARCoord*>(data.data() + offset);

            Point2Dd centrePoint = FromXARCoord(centre);
            Point2Dd majorPoint = FromXARCoord(majorAxis);
            Point2Dd minorPoint = FromXARCoord(minorAxis);

            // Calculate radii from axis endpoints
            float rx = std::sqrt(std::pow(majorPoint.x - centrePoint.x, 2) +
                                 std::pow(majorPoint.y - centrePoint.y, 2));
            float ry = std::sqrt(std::pow(minorPoint.x - centrePoint.x, 2) +
                                 std::pow(minorPoint.y - centrePoint.y, 2));

            if (std::abs(rx - ry) < 0.01f) {
                // Circle
                auto circle = std::make_shared<VectorCircle>();
                circle->Type = VectorElementType::Circle;
                circle->Center = centrePoint;
                circle->Radius = rx;
                circle->Style = importState.currentStyle;
                if (importState.currentTransform.Determinant() != 0) {
                    circle->Transform = importState.currentTransform;
                }
                AddElementToCurrentContainer(circle);
            } else {
                // Ellipse
                auto ellipse = std::make_shared<VectorEllipse>();
                ellipse->Type = VectorElementType::Ellipse;
                ellipse->Center = centrePoint;
                ellipse->RadiusX = rx;
                ellipse->RadiusY = ry;
                ellipse->Style = importState.currentStyle;
                if (importState.currentTransform.Determinant() != 0) {
                    ellipse->Transform = importState.currentTransform;
                }
                AddElementToCurrentContainer(ellipse);
            }
        }

        void XARConverter::Impl::ProcessPolygon(uint32_t tag, const std::vector<uint8_t>& data) {
            // QuickShape polygon - convert to path
            if (data.size() < sizeof(uint32_t) + 2 * sizeof(XARCoord)) return;

            size_t offset = 0;

            // Read number of sides
            uint32_t numSides = *reinterpret_cast<const uint32_t*>(data.data() + offset);
            offset += sizeof(uint32_t);

            // Read centre
            XARCoord centre = *reinterpret_cast<const XARCoord*>(data.data() + offset);
            offset += sizeof(XARCoord);

            // Read major axis
            XARCoord majorAxis = *reinterpret_cast<const XARCoord*>(data.data() + offset);
            offset += sizeof(XARCoord);

            Point2Dd centrePoint = FromXARCoord(centre);
            Point2Dd majorPoint = FromXARCoord(majorAxis);

            float radius = std::sqrt(std::pow(majorPoint.x - centrePoint.x, 2) +
                                     std::pow(majorPoint.y - centrePoint.y, 2));
            float startAngle = std::atan2(majorPoint.y - centrePoint.y,
                                          majorPoint.x - centrePoint.x);

            // Check for stellated polygon
            bool stellated = (tag == TAG_POLYGON_COMPLEX_STELLATED ||
                              tag == TAG_POLYGON_COMPLEX_STELLATED_REFORMED ||
                              tag == TAG_POLYGON_COMPLEX_ROUNDED_STELLATED ||
                              tag == TAG_POLYGON_COMPLEX_ROUNDED_STELLATED_REFORMED);

            float innerRadius = radius * 0.5f;  // Default
            if (stellated && offset + sizeof(int32_t) <= data.size()) {
                int32_t innerRad = *reinterpret_cast<const int32_t*>(data.data() + offset);
                innerRadius = static_cast<float>(innerRad) / XAR_MILLIPOINTS_PER_POINT;
            }

            // Create polygon as path
            auto path = std::make_shared<VectorPath>();
            path->Type = VectorElementType::Path;

            float angleStep = 2.0f * 3.14159265f / numSides;

            if (stellated) {
                // Star shape
                for (uint32_t i = 0; i < numSides; i++) {
                    float outerAngle = startAngle + i * angleStep;
                    float innerAngle = outerAngle + angleStep * 0.5f;

                    float outerX = centrePoint.x + radius * std::cos(outerAngle);
                    float outerY = centrePoint.y + radius * std::sin(outerAngle);
                    float innerX = centrePoint.x + innerRadius * std::cos(innerAngle);
                    float innerY = centrePoint.y + innerRadius * std::sin(innerAngle);

                    if (i == 0) {
                        path->MoveTo(outerX, outerY);
                    } else {
                        path->LineTo(outerX, outerY);
                    }
                    path->LineTo(innerX, innerY);
                }
            } else {
                // Regular polygon
                for (uint32_t i = 0; i < numSides; i++) {
                    float angle = startAngle + i * angleStep;
                    float x = centrePoint.x + radius * std::cos(angle);
                    float y = centrePoint.y + radius * std::sin(angle);

                    if (i == 0) {
                        path->MoveTo(x, y);
                    } else {
                        path->LineTo(x, y);
                    }
                }
            }

            path->ClosePath();
            path->Style = importState.currentStyle;
            if (importState.currentTransform.Determinant() != 0) {
                path->Transform = importState.currentTransform;
            }

            AddElementToCurrentContainer(path);
        }

// Continue in next part...
// UltraCanvasXARConverter.cpp - Continuation
// Text, Bitmap, Effects, Compression, and Export Implementation

// ===== TEXT PROCESSING =====

        void XARConverter::Impl::ProcessText(uint32_t tag, const std::vector<uint8_t>& data) {
            if (tag == TAG_TEXT_STRING && !data.empty()) {
                auto text = std::make_shared<VectorText>();
                text->Type = VectorElementType::Text;

                // Extract text content (null-terminated string)
                size_t textEnd = 0;
                while (textEnd < data.size() && data[textEnd] != 0) textEnd++;
                std::string content(reinterpret_cast<const char*>(data.data()), textEnd);

                text->SetText(content);

                // Apply current font if available
                if (importState.fontMap.count("current")) {
                    text->BaseStyle.FontFamily = importState.fontMap["current"];
                }

                text->Style = importState.currentStyle;
                if (importState.currentTransform.Determinant() != 0) {
                    text->Transform = importState.currentTransform;
                }

                AddElementToCurrentContainer(text);
            }
            else if (tag >= TAG_TEXT_STORY_SIMPLE && tag <= TAG_TEXT_STORY_COMPLEX_END_RIGHT) {
                // Text story - container for text lines
                auto group = std::make_shared<VectorGroup>();
                group->Type = VectorElementType::Group;

                // Extract position if present
                if (data.size() >= 2 * sizeof(XARCoord)) {
                    XARCoord pos = *reinterpret_cast<const XARCoord*>(data.data());
                    // Store position for child text elements
                }

                AddElementToCurrentContainer(group);

                if (importState.currentGroup) {
                    importState.groupStack.push(importState.currentGroup);
                }
                importState.currentGroup = group;
            }
        }

// ===== BITMAP PROCESSING =====

        void XARConverter::Impl::ProcessBitmap(uint32_t tag, const std::vector<uint8_t>& data) {
            if (data.size() < sizeof(uint32_t) + 4 * sizeof(XARCoord)) return;

            auto image = std::make_shared<VectorImage>();
            image->Type = VectorElementType::Image;

            size_t offset = 0;

            // Read bitmap reference ID
            uint32_t bitmapId = *reinterpret_cast<const uint32_t*>(data.data() + offset);
            offset += sizeof(uint32_t);

            // Read corner coordinates (parallelogram)
            XARCoord corners[4];
            for (int i = 0; i < 4 && offset + sizeof(XARCoord) <= data.size(); i++) {
                corners[i] = *reinterpret_cast<const XARCoord*>(data.data() + offset);
                offset += sizeof(XARCoord);
            }

            // Calculate bounds from corners
            Point2Dd p0 = FromXARCoord(corners[0]);
            Point2Dd p1 = FromXARCoord(corners[1]);
            Point2Dd p2 = FromXARCoord(corners[2]);
            Point2Dd p3 = FromXARCoord(corners[3]);

            float minX = std::min({p0.x, p1.x, p2.x, p3.x});
            float minY = std::min({p0.y, p1.y, p2.y, p3.y});
            float maxX = std::max({p0.x, p1.x, p2.x, p3.x});
            float maxY = std::max({p0.y, p1.y, p2.y, p3.y});

            image->Bounds = Rect2Dd{minX, minY, maxX - minX, maxY - minY};

            // Link to bitmap data if available
            if (importState.bitmapData.count(bitmapId)) {
                image->EmbeddedData = importState.bitmapData[bitmapId];
            }

            image->Style = importState.currentStyle;
            if (importState.currentTransform.Determinant() != 0) {
                image->Transform = importState.currentTransform;
            }

            AddElementToCurrentContainer(image);
        }

        void XARConverter::Impl::ProcessBitmapDefinition(uint32_t tag, const std::vector<uint8_t>& data) {
            if (data.size() < sizeof(uint32_t)) return;

            size_t offset = 0;

            // Read bitmap reference ID
            uint32_t bitmapId = *reinterpret_cast<const uint32_t*>(data.data() + offset);
            offset += sizeof(uint32_t);

            // Read bitmap dimensions (may vary by format)
            uint32_t width = 0, height = 0;
            if (offset + 2 * sizeof(uint32_t) <= data.size()) {
                width = *reinterpret_cast<const uint32_t*>(data.data() + offset);
                offset += sizeof(uint32_t);
                height = *reinterpret_cast<const uint32_t*>(data.data() + offset);
                offset += sizeof(uint32_t);
            }

            // Store remaining data as bitmap content
            if (offset < data.size()) {
                std::vector<uint8_t> bitmapContent(data.begin() + offset, data.end());
                importState.bitmapData[bitmapId] = std::move(bitmapContent);
            }
        }

// ===== COLOUR DEFINITIONS =====

        void XARConverter::Impl::ProcessDefineColour(uint32_t tag, const std::vector<uint8_t>& data) {
            if (data.size() < sizeof(uint32_t) + sizeof(XARColourRGB)) return;

            size_t offset = 0;

            // Read colour reference ID
            uint32_t colourId = *reinterpret_cast<const uint32_t*>(data.data() + offset);
            offset += sizeof(uint32_t);

            // Read colour value
            XARColourRGB color = *reinterpret_cast<const XARColourRGB*>(data.data() + offset);

            importState.namedColours[colourId] = FromXARColour(color);
        }

// ===== ATTRIBUTE PROCESSING =====
// The legacy import path predates the spec-verified reader in the XAR plugin
// (which is what actually parses XAR files at runtime); its attribute
// handlers were declared but never implemented. Kept as no-ops so imported
// geometry still arrives, just unstyled.

        void XARConverter::Impl::ProcessLineAttribute(uint32_t tag, const std::vector<uint8_t>& data) {
            (void)tag; (void)data;
        }

        void XARConverter::Impl::ProcessFillAttribute(uint32_t tag, const std::vector<uint8_t>& data) {
            (void)tag; (void)data;
        }

        void XARConverter::Impl::ProcessTransparency(uint32_t tag, const std::vector<uint8_t>& data) {
            (void)tag; (void)data;
        }

        void XARConverter::Impl::ProcessTextAttribute(uint32_t tag, const std::vector<uint8_t>& data) {
            (void)tag; (void)data;
        }

// ===== EFFECT PROCESSING =====

        void XARConverter::Impl::ProcessFeather(const std::vector<uint8_t>& data) {
            (void)data;
            // VectorStyle has no feather/blur channel; the XAR plugin renders
            // feather natively, so the converter just notes it.
            LogWarning("Feather effect detected but not representable in VectorStorage - ignored");
        }

        void XARConverter::Impl::ProcessShadow(uint32_t tag, const std::vector<uint8_t>& data) {
            (void)tag; (void)data;
            // VectorStyle has no shadow channel; the XAR plugin renders
            // shadows natively, so the converter just notes it.
            LogWarning("Shadow effect detected but not representable in VectorStorage - ignored");
        }

        void XARConverter::Impl::ProcessBevel(uint32_t tag, const std::vector<uint8_t>& data) {
            // Bevel effects are complex - store as metadata for now
            // Full implementation would require 3D rendering capabilities
            LogWarning("Bevel effect detected but not fully supported - will render flat");
        }

        void XARConverter::Impl::ProcessContour(uint32_t tag, const std::vector<uint8_t>& data) {
            // Contour effects create offset paths
            // Store parameters for potential future implementation
            LogWarning("Contour effect detected but not fully supported");
        }

        void XARConverter::Impl::ProcessBlend(uint32_t tag, const std::vector<uint8_t>& data) {
            // Blend creates intermediate shapes between two objects
            // Would require interpolation implementation
            LogWarning("Blend effect detected but not fully supported");
        }

        void XARConverter::Impl::ProcessMould(uint32_t tag, const std::vector<uint8_t>& data) {
            // Mould (envelope/perspective) warps shapes
            // Would require mesh deformation implementation
            LogWarning("Mould/Envelope effect detected but not fully supported");
        }

// ===== COMPRESSION =====

        std::vector<uint8_t> XARConverter::Impl::CompressData(const std::vector<uint8_t>& data) {
            if (!currentXarOptions.UseCompression || data.empty()) {
                return data;
            }

            z_stream stream;
            stream.zalloc = Z_NULL;
            stream.zfree = Z_NULL;
            stream.opaque = Z_NULL;

            if (deflateInit(&stream, Z_DEFAULT_COMPRESSION) != Z_OK) {
                return data;  // Return uncompressed on error
            }

            stream.avail_in = static_cast<uInt>(data.size());
            stream.next_in = const_cast<uint8_t*>(data.data());

            std::vector<uint8_t> compressed;
            compressed.resize(deflateBound(&stream, static_cast<uLong>(data.size())));

            stream.avail_out = static_cast<uInt>(compressed.size());
            stream.next_out = compressed.data();

            int ret = deflate(&stream, Z_FINISH);
            deflateEnd(&stream);

            if (ret != Z_STREAM_END) {
                return data;  // Return uncompressed on error
            }

            compressed.resize(stream.total_out);
            return compressed;
        }

        std::vector<uint8_t> XARConverter::Impl::DecompressData(const std::vector<uint8_t>& compressed,
                                                                size_t uncompSize) {
            if (compressed.empty()) return {};

            z_stream stream;
            stream.zalloc = Z_NULL;
            stream.zfree = Z_NULL;
            stream.opaque = Z_NULL;

            if (inflateInit(&stream) != Z_OK) {
                return {};
            }

            stream.avail_in = static_cast<uInt>(compressed.size());
            stream.next_in = const_cast<uint8_t*>(compressed.data());

            std::vector<uint8_t> decompressed(uncompSize);
            stream.avail_out = static_cast<uInt>(decompressed.size());
            stream.next_out = decompressed.data();

            int ret = inflate(&stream, Z_FINISH);
            inflateEnd(&stream);

            if (ret != Z_STREAM_END) {
                LogWarning("Decompression failed");
                return {};
            }

            return decompressed;
        }

// ===== UTILITY FUNCTIONS =====

        void XARConverter::Impl::AddElementToCurrentContainer(std::shared_ptr<VectorElement> element) {
            if (importState.currentGroup) {
                importState.currentGroup->AddChild(element);
            } else if (importState.currentLayer) {
                importState.currentLayer->AddChild(element);
            }

            // Store reference
            importState.objectRefs[importState.nextRefId++] = element;
        }

        void XARConverter::Impl::LogWarning(const std::string& message) {
            if (currentOptions.WarningCallback) {
                currentOptions.WarningCallback(message);
            }
            if (currentXarOptions.WarningCallback) {
                currentXarOptions.WarningCallback(message);
            }
        }

        void XARConverter::Impl::ReportProgress(float progress) {
            if (currentOptions.ProgressCallback) {
                currentOptions.ProgressCallback(progress);
            }
            if (currentXarOptions.ProgressCallback) {
                currentXarOptions.ProgressCallback(progress);
            }
        }

// ===== EXPORT IMPLEMENTATION =====
//
// The emitter below writes the uncompressed XAR record grammar exactly as the
// spec-verified reader in Plugins/Vector/XAR/UltraCanvasXARPlugin.cpp consumes
// it: 8-byte signature, then records of (TAG:UINT32, size:UINT32, body), with
// the tree encoded as "object record, TAG_DOWN, child records, TAG_UP".
// Attribute records are emitted as children of the object they style, and
// colour/font definition records are referenced by their 1-based record
// sequence number (every record counts, TAG_UP/TAG_DOWN included).
//
// NOTE: the legacy XARTags namespace in UltraCanvasXARConverter.h predates the
// spec-verified reader and carries wrong tag numbers (e.g. TAG_ENDOFFILE=4,
// TAG_DEFINERGBCOLOUR=1000). The writer therefore defines its own constants,
// taken from the Xar Format Specification Appendix A and matching the XARTag
// enum in UltraCanvasXARPlugin.h.

        namespace {
            namespace XarOut {
                constexpr uint32_t Up = 0;
                constexpr uint32_t Down = 1;
                constexpr uint32_t FileHeader = 2;
                constexpr uint32_t EndOfFile = 3;
                constexpr uint32_t Document = 40;
                constexpr uint32_t Chapter = 41;
                constexpr uint32_t Spread = 42;
                constexpr uint32_t Layer = 43;
                constexpr uint32_t SpreadInformation = 45;
                constexpr uint32_t LayerDetails = 48;
                constexpr uint32_t DefineRGBColour = 50;
                constexpr uint32_t Path = 100;
                constexpr uint32_t PathFilled = 101;
                constexpr uint32_t PathStroked = 102;
                constexpr uint32_t PathFilledStroked = 103;
                constexpr uint32_t Group = 104;
                constexpr uint32_t FlatFill = 150;
                constexpr uint32_t LineColour = 151;
                constexpr uint32_t LineWidth = 152;
                constexpr uint32_t LinearFill = 153;
                constexpr uint32_t CircularFill = 154;
                constexpr uint32_t FlatTransparentFill = 166;
                constexpr uint32_t StartCap = 174;
                constexpr uint32_t EndCap = 175;
                constexpr uint32_t JoinStyle = 176;
                constexpr uint32_t MitreLimit = 177;
                constexpr uint32_t FlatFillNone = 190;
                constexpr uint32_t LineColourNone = 193;
                constexpr uint32_t EllipseSimple = 1000;
                constexpr uint32_t RectangleSimple = 1100;
                constexpr uint32_t RectangleSimpleRounded = 1104;
                constexpr uint32_t FontDefTrueType = 2000;
                constexpr uint32_t TextStorySimple = 2100;
                constexpr uint32_t TextLine = 2200;
                constexpr uint32_t TextString = 2201;
                constexpr uint32_t TextEOL = 2203;
                constexpr uint32_t TextJustificationLeft = 2902;
                constexpr uint32_t TextJustificationCentre = 2903;
                constexpr uint32_t TextJustificationRight = 2904;
                constexpr uint32_t TextFontSize = 2906;
                constexpr uint32_t TextFontTypeface = 2907;
                constexpr uint32_t TextBoldOn = 2908;
                constexpr uint32_t TextBoldOff = 2909;
                constexpr uint32_t TextItalicOn = 2910;
                constexpr uint32_t TextItalicOff = 2911;
                constexpr uint32_t TextUnderlineOn = 2912;
                constexpr uint32_t TextUnderlineOff = 2913;
            }

            // Little-endian record body builder.
            struct XarBody {
                std::vector<uint8_t> bytes;

                void U8(uint8_t v) { bytes.push_back(v); }
                void U32(uint32_t v) {
                    bytes.push_back(static_cast<uint8_t>(v));
                    bytes.push_back(static_cast<uint8_t>(v >> 8));
                    bytes.push_back(static_cast<uint8_t>(v >> 16));
                    bytes.push_back(static_cast<uint8_t>(v >> 24));
                }
                void I32(int32_t v) { U32(static_cast<uint32_t>(v)); }
                void Ascii(const std::string& s) {
                    bytes.insert(bytes.end(), s.begin(), s.end());
                    bytes.push_back(0);
                }
                // UTF-8 in, UTF-16LE + 0x0000 terminator out.
                void Utf16(const std::string& utf8) {
                    size_t i = 0, n = utf8.size();
                    while (i < n) {
                        uint32_t cp = static_cast<uint8_t>(utf8[i]);
                        size_t extra = 0;
                        if (cp >= 0xF0) { cp &= 0x07; extra = 3; }
                        else if (cp >= 0xE0) { cp &= 0x0F; extra = 2; }
                        else if (cp >= 0xC0) { cp &= 0x1F; extra = 1; }
                        if (i + extra >= n && extra > 0) break;
                        for (size_t k = 0; k < extra; ++k) {
                            cp = (cp << 6) | (static_cast<uint8_t>(utf8[i + 1 + k]) & 0x3F);
                        }
                        i += 1 + extra;
                        if (cp >= 0x10000) {
                            cp -= 0x10000;
                            uint16_t hi = static_cast<uint16_t>(0xD800 | (cp >> 10));
                            uint16_t lo = static_cast<uint16_t>(0xDC00 | (cp & 0x3FF));
                            bytes.push_back(static_cast<uint8_t>(hi));
                            bytes.push_back(static_cast<uint8_t>(hi >> 8));
                            bytes.push_back(static_cast<uint8_t>(lo));
                            bytes.push_back(static_cast<uint8_t>(lo >> 8));
                        } else {
                            bytes.push_back(static_cast<uint8_t>(cp));
                            bytes.push_back(static_cast<uint8_t>(cp >> 8));
                        }
                    }
                    bytes.push_back(0);
                    bytes.push_back(0);
                }
            };

            // Paths are normalised to absolute move/line/cubic segments by the
            // shared PathOps helpers (UltraCanvasVectorPathOps.h).
            using namespace PathOps;
            using XarPathSeg = PathOps::FlatSeg;

            class XarEmitter {
            public:
                XarEmitter(const VectorDocument& document,
                           std::function<void(const std::string&)> warnFn)
                        : doc(document), warn(std::move(warnFn)) {}

                std::vector<uint8_t> Build() {
                    pageW = doc.Size.width;
                    pageH = doc.Size.height;
                    if (pageW <= 0 || pageH <= 0) {
                        Rect2Dd bbox = doc.GetBoundingBox();
                        pageW = bbox.x + bbox.width;
                        pageH = bbox.y + bbox.height;
                        if (pageW <= 0) pageW = 595;   // A4 fallback
                        if (pageH <= 0) pageH = 842;
                    }

                    const uint8_t signature[8] = {0x58, 0x41, 0x52, 0x41, 0xA3, 0xA3, 0x0D, 0x0A};
                    out.assign(signature, signature + 8);

                    XarBody fh;
                    fh.bytes.push_back('C'); fh.bytes.push_back('X'); fh.bytes.push_back('N');
                    fh.U32(0);   // file size, patched below
                    fh.U32(0);   // web link
                    fh.U32(0);   // refinement flags
                    fh.Ascii("UltraCanvas");
                    fh.Ascii("1.0");
                    fh.Ascii("");
                    Rec(XarOut::FileHeader, fh);

                    Rec(XarOut::Document);
                    Down();
                    Rec(XarOut::Chapter);
                    Down();
                    Rec(XarOut::Spread);
                    Down();

                    XarBody si;
                    si.I32(Mp(pageW));
                    si.I32(Mp(pageH));
                    si.I32(0);   // margin
                    si.I32(0);   // bleed
                    si.U8(0);    // flags
                    Rec(XarOut::SpreadInformation, si);

                    for (const auto& layer : doc.Layers) {
                        if (layer) EmitLayer(*layer);
                    }

                    Up();   // spread
                    Up();   // chapter
                    Up();   // document
                    Rec(XarOut::EndOfFile);

                    // Patch the file-size hint inside the FILEHEADER body:
                    // 8 signature + 4 tag + 4 size + 3 "CXN" = offset 19.
                    uint32_t total = static_cast<uint32_t>(out.size());
                    out[19] = static_cast<uint8_t>(total);
                    out[20] = static_cast<uint8_t>(total >> 8);
                    out[21] = static_cast<uint8_t>(total >> 16);
                    out[22] = static_cast<uint8_t>(total >> 24);
                    return out;
                }

            private:
                const VectorDocument& doc;
                std::function<void(const std::string&)> warn;
                std::vector<uint8_t> out;
                uint32_t seq = 0;
                double pageW = 0, pageH = 0;                 // points
                std::map<uint32_t, uint32_t> colourRefs;     // 0xRRGGBB -> record seq
                std::map<std::string, uint32_t> fontRefs;    // family -> record seq

                // ===== RECORD PRIMITIVES =====

                uint32_t Rec(uint32_t tag, const XarBody& body = XarBody()) {
                    ++seq;
                    XarBody hdr;
                    hdr.U32(tag);
                    hdr.U32(static_cast<uint32_t>(body.bytes.size()));
                    out.insert(out.end(), hdr.bytes.begin(), hdr.bytes.end());
                    out.insert(out.end(), body.bytes.begin(), body.bytes.end());
                    return seq;
                }
                void Down() { Rec(XarOut::Down); }
                void Up() { Rec(XarOut::Up); }

                // ===== COORDINATES =====
                // Document space is points, Y down; XAR is millipoints, Y up.

                static int32_t Mp(double pt) {
                    return static_cast<int32_t>(std::lround(pt * 1000.0));
                }
                void Coord(XarBody& b, const Point2Dd& pPt) const {
                    b.I32(Mp(pPt.x));
                    b.I32(Mp(pageH - pPt.y));
                }
                static void Vec(XarBody& b, double dxPt, double dyPt) {
                    b.I32(Mp(dxPt));
                    b.I32(Mp(-dyPt));
                }
                static bool AxisAligned(const Matrix3x3& m) {
                    return std::fabs(m.m[0][1]) < 1e-6 && std::fabs(m.m[1][0]) < 1e-6;
                }
                static double AvgScale(const Matrix3x3& m) {
                    double det = std::fabs(static_cast<double>(m.m[0][0]) * m.m[1][1] -
                                           static_cast<double>(m.m[0][1]) * m.m[1][0]);
                    return det > 0 ? std::sqrt(det) : 1.0;
                }

                // ===== REFERENCED DEFINITIONS =====
                // Definitions are emitted lazily, immediately before the first
                // record that references them; the reader keys both colours and
                // fonts by record sequence number, position-independent.

                int32_t ColourRef(const Color& c) {
                    uint32_t key = (static_cast<uint32_t>(c.r) << 16) |
                                   (static_cast<uint32_t>(c.g) << 8) | c.b;
                    auto it = colourRefs.find(key);
                    if (it != colourRefs.end()) return static_cast<int32_t>(it->second);
                    XarBody b;
                    b.U8(c.r); b.U8(c.g); b.U8(c.b);
                    uint32_t ref = Rec(XarOut::DefineRGBColour, b);
                    colourRefs[key] = ref;
                    return static_cast<int32_t>(ref);
                }

                int32_t FontRef(const std::string& family) {
                    auto it = fontRefs.find(family);
                    if (it != fontRefs.end()) return static_cast<int32_t>(it->second);
                    XarBody b;
                    b.Utf16(family);
                    b.Utf16(family);
                    for (int i = 0; i < 10; ++i) b.U8(0);   // panose
                    uint32_t ref = Rec(XarOut::FontDefTrueType, b);
                    fontRefs[family] = ref;
                    return static_cast<int32_t>(ref);
                }

                // ===== TREE =====

                void EmitLayer(const VectorLayer& layer) {
                    Rec(XarOut::Layer);
                    Down();
                    XarBody ld;
                    uint8_t flags = 0;
                    if (layer.Visible) flags |= 0x1;
                    if (layer.Locked) flags |= 0x2;
                    flags |= 0x4;   // printable
                    ld.U8(flags);
                    ld.Utf16(layer.Name.empty() ? std::string("Layer 1") : layer.Name);
                    Rec(XarOut::LayerDetails, ld);

                    for (const auto& child : layer.Children) {
                        if (child) EmitElement(*child, layer.Style, Matrix3x3::Identity());
                    }
                    Up();
                }

                void EmitElement(const VectorElement& e, const VectorStyle& inherited,
                                 const Matrix3x3& parentCtm) {
                    if (!e.Style.Visible || !e.Style.Display) return;

                    VectorStyle eff = e.Style;
                    eff.Inherit(inherited);
                    Matrix3x3 ctm = e.Transform ? parentCtm * (*e.Transform) : parentCtm;

                    switch (e.Type) {
                        case VectorElementType::Group:
                        case VectorElementType::Symbol: {
                            const auto& g = static_cast<const VectorGroup&>(e);
                            Rec(XarOut::Group);
                            Down();
                            for (const auto& child : g.Children) {
                                if (child) EmitElement(*child, eff, ctm);
                            }
                            Up();
                            break;
                        }
                        case VectorElementType::Layer: {
                            // Nested layers degrade to groups.
                            const auto& g = static_cast<const VectorGroup&>(e);
                            Rec(XarOut::Group);
                            Down();
                            for (const auto& child : g.Children) {
                                if (child) EmitElement(*child, eff, ctm);
                            }
                            Up();
                            break;
                        }
                        case VectorElementType::Rectangle:
                        case VectorElementType::RoundedRectangle:
                            EmitRect(static_cast<const VectorRect&>(e), eff, ctm);
                            break;
                        case VectorElementType::Circle: {
                            const auto& c = static_cast<const VectorCircle&>(e);
                            EmitEllipseShape(c.Center, c.Radius, c.Radius, eff, ctm);
                            break;
                        }
                        case VectorElementType::Ellipse: {
                            const auto& el = static_cast<const VectorEllipse&>(e);
                            EmitEllipseShape(el.Center, el.RadiusX, el.RadiusY, eff, ctm);
                            break;
                        }
                        case VectorElementType::Line: {
                            const auto& ln = static_cast<const VectorLine&>(e);
                            std::vector<XarPathSeg> segs;
                            segs.push_back({XarPathSeg::Move, {ln.Start}, false});
                            segs.push_back({XarPathSeg::Line, {ln.End}, false});
                            EmitPathRecord(segs, eff, ctm, false);
                            break;
                        }
                        case VectorElementType::Polyline:
                            EmitPolySegs(static_cast<const VectorPolyline&>(e).Points, false, eff, ctm);
                            break;
                        case VectorElementType::Polygon:
                            EmitPolySegs(static_cast<const VectorPolygon&>(e).Points, true, eff, ctm);
                            break;
                        case VectorElementType::Path: {
                            const auto& p = static_cast<const VectorPath&>(e);
                            auto segs = NormalizePath(p.Path);
                            EmitPathRecord(segs, eff, ctm, true);
                            break;
                        }
                        case VectorElementType::Text:
                            EmitText(static_cast<const VectorText&>(e), eff, ctm);
                            break;
                        default:
                            warn("XAR export: element type not supported, skipped (type " +
                                 std::to_string(static_cast<int>(e.Type)) + ")");
                            break;
                    }
                }

                // ===== SHAPES =====

                void EmitRect(const VectorRect& r, const VectorStyle& style, const Matrix3x3& ctm) {
                    double rx = std::min<double>(r.RadiusX, r.Bounds.width / 2);
                    double ry = std::min<double>(r.RadiusY, r.Bounds.height / 2);
                    if (rx <= 0 && ry > 0) rx = ry;
                    if (ry <= 0 && rx > 0) ry = rx;

                    if (!AxisAligned(ctm)) {
                        auto segs = (rx > 0)
                                ? RoundedRectSegs(r.Bounds, rx, ry)
                                : RectSegs(r.Bounds);
                        EmitPathRecord(segs, style, ctm, true);
                        return;
                    }

                    Point2Dd p0 = ctm.Transform(Point2Dd(r.Bounds.x, r.Bounds.y));
                    Point2Dd p1 = ctm.Transform(Point2Dd(r.Bounds.x + r.Bounds.width,
                                                         r.Bounds.y + r.Bounds.height));
                    Point2Dd centre((p0.x + p1.x) / 2, (p0.y + p1.y) / 2);
                    double halfW = std::fabs(p1.x - p0.x) / 2;
                    double halfH = std::fabs(p1.y - p0.y) / 2;
                    double sx = std::fabs(ctm.m[0][0]);
                    double sy = std::fabs(ctm.m[1][1]);

                    XarBody b;
                    Coord(b, centre);
                    Vec(b, halfW, 0);
                    Vec(b, 0, -halfH);   // "up" in document space
                    uint32_t tag = XarOut::RectangleSimple;
                    if (rx > 0) {
                        tag = XarOut::RectangleSimpleRounded;
                        b.I32(Mp((rx * sx + ry * sy) / 2));
                    }
                    Rec(tag, b);
                    EmitShapeAttributes(style, ctm);
                }

                void EmitEllipseShape(const Point2Dd& center, double radX, double radY,
                                      const VectorStyle& style, const Matrix3x3& ctm) {
                    if (!AxisAligned(ctm)) {
                        EmitPathRecord(EllipseSegs(center, radX, radY), style, ctm, true);
                        return;
                    }
                    Point2Dd c = ctm.Transform(center);
                    double rx = radX * std::fabs(ctm.m[0][0]);
                    double ry = radY * std::fabs(ctm.m[1][1]);
                    XarBody b;
                    Coord(b, c);
                    Vec(b, rx, 0);
                    Vec(b, 0, -ry);
                    Rec(XarOut::EllipseSimple, b);
                    EmitShapeAttributes(style, ctm);
                }

                void EmitPolySegs(const std::vector<Point2Dd>& pts, bool closed,
                                  const VectorStyle& style, const Matrix3x3& ctm) {
                    if (pts.size() < 2) return;
                    std::vector<XarPathSeg> segs;
                    segs.push_back({XarPathSeg::Move, {pts[0]}, false});
                    for (size_t i = 1; i < pts.size(); ++i) {
                        segs.push_back({XarPathSeg::Line, {pts[i]}, false});
                    }
                    if (closed) segs.back().closeAfter = true;
                    EmitPathRecord(segs, style, ctm, true);
                }

                // ===== PATHS =====

                void EmitPathRecord(const std::vector<XarPathSeg>& segs, const VectorStyle& style,
                                    const Matrix3x3& ctm, bool fillable) {
                    if (segs.empty()) return;

                    bool filled = fillable && HasVisibleFill(style);
                    bool stroked = HasVisibleStroke(style);
                    uint32_t tag = filled && stroked ? XarOut::PathFilledStroked
                                 : filled           ? XarOut::PathFilled
                                 : stroked          ? XarOut::PathStroked
                                                    : XarOut::Path;

                    std::vector<uint8_t> verbs;
                    std::vector<Point2Dd> coords;
                    for (const auto& s : segs) {
                        switch (s.kind) {
                            case XarPathSeg::Move:
                                verbs.push_back(0x06);
                                coords.push_back(ctm.Transform(s.p[0]));
                                break;
                            case XarPathSeg::Line:
                                verbs.push_back(s.closeAfter ? 0x03 : 0x02);
                                coords.push_back(ctm.Transform(s.p[0]));
                                break;
                            case XarPathSeg::Cubic:
                                verbs.push_back(0x04);
                                verbs.push_back(0x04);
                                verbs.push_back(s.closeAfter ? 0x05 : 0x04);
                                coords.push_back(ctm.Transform(s.p[0]));
                                coords.push_back(ctm.Transform(s.p[1]));
                                coords.push_back(ctm.Transform(s.p[2]));
                                break;
                        }
                    }

                    XarBody b;
                    b.U32(static_cast<uint32_t>(verbs.size()));
                    for (uint8_t v : verbs) b.U8(v);
                    while (b.bytes.size() % 4 != 0) b.U8(0);
                    for (const auto& c : coords) Coord(b, c);
                    Rec(tag, b);
                    EmitShapeAttributes(style, ctm);
                }

                // ===== ATTRIBUTES =====

                static bool HasVisibleFill(const VectorStyle& s) {
                    return s.Fill.has_value() &&
                           !std::holds_alternative<std::monostate>(*s.Fill);
                }
                static bool HasVisibleStroke(const VectorStyle& s) {
                    return s.Stroke.has_value() && s.Stroke->Width > 0 &&
                           !std::holds_alternative<std::monostate>(s.Stroke->Fill);
                }

                // Emits the attribute children (fill, line, transparency) of the
                // object record written immediately before.
                void EmitShapeAttributes(const VectorStyle& style, const Matrix3x3& ctm) {
                    Down();
                    uint8_t fillAlpha = 255;

                    if (HasVisibleFill(style)) {
                        const FillData& fill = *style.Fill;
                        if (const Color* c = std::get_if<Color>(&fill)) {
                            fillAlpha = c->a;
                            XarBody b;
                            b.I32(ColourRef(*c));
                            Rec(XarOut::FlatFill, b);
                        } else if (const GradientData* g = std::get_if<GradientData>(&fill)) {
                            EmitGradientFill(*g, ctm);
                        } else {
                            warn("XAR export: pattern/reference fills are not supported, "
                                 "filling flat black");
                            XarBody b;
                            b.I32(ColourRef(Color(0, 0, 0, 255)));
                            Rec(XarOut::FlatFill, b);
                        }
                    } else {
                        Rec(XarOut::FlatFillNone);
                    }

                    if (HasVisibleStroke(style)) {
                        const StrokeData& st = *style.Stroke;
                        Color sc(0, 0, 0, 255);
                        if (const Color* c = std::get_if<Color>(&st.Fill)) {
                            sc = *c;
                        } else {
                            warn("XAR export: non-solid stroke paint replaced with black");
                        }
                        XarBody lc;
                        lc.I32(ColourRef(sc));
                        Rec(XarOut::LineColour, lc);

                        XarBody lw;
                        lw.I32(Mp(st.Width * AvgScale(ctm)));
                        Rec(XarOut::LineWidth, lw);

                        uint8_t cap = st.LineCap == StrokeLineCap::Round ? 1
                                    : st.LineCap == StrokeLineCap::Square ? 2 : 0;
                        XarBody cb1; cb1.U8(cap); Rec(XarOut::StartCap, cb1);
                        XarBody cb2; cb2.U8(cap); Rec(XarOut::EndCap, cb2);

                        uint8_t join = st.LineJoin == StrokeLineJoin::Round ? 1
                                     : st.LineJoin == StrokeLineJoin::Bevel ? 2 : 0;
                        XarBody jb; jb.U8(join); Rec(XarOut::JoinStyle, jb);

                        XarBody mb;
                        mb.I32(static_cast<int32_t>(st.MiterLimit * 65536.0f));
                        Rec(XarOut::MitreLimit, mb);

                        if (!st.DashArray.empty()) {
                            warn("XAR export: dash patterns are not written yet, "
                                 "stroke exported solid");
                        }
                    } else {
                        Rec(XarOut::LineColourNone);
                    }

                    float opacity = style.Opacity * style.FillOpacity *
                                    (static_cast<float>(fillAlpha) / 255.0f);
                    if (opacity < 0.999f) {
                        float t = 1.0f - std::max(0.0f, std::min(1.0f, opacity));
                        XarBody b;
                        b.U8(static_cast<uint8_t>(std::lround(t * 255.0f)));
                        b.U8(1);   // mix
                        Rec(XarOut::FlatTransparentFill, b);
                    }
                    Up();
                }

                void EmitGradientFill(const GradientData& g, const Matrix3x3& ctm) {
                    if (const LinearGradientData* lg = std::get_if<LinearGradientData>(&g)) {
                        Color c0(0, 0, 0, 255), c1(255, 255, 255, 255);
                        if (!lg->Stops.empty()) {
                            c0 = lg->Stops.front().color;
                            c1 = lg->Stops.back().color;
                            if (lg->Stops.size() > 2) {
                                warn("XAR export: only first/last gradient stops are written");
                            }
                        }
                        XarBody b;
                        Coord(b, ctm.Transform(lg->Start));
                        Coord(b, ctm.Transform(lg->End));
                        b.I32(ColourRef(c0));
                        b.I32(ColourRef(c1));
                        Rec(XarOut::LinearFill, b);
                    } else if (const RadialGradientData* rg = std::get_if<RadialGradientData>(&g)) {
                        Color c0(0, 0, 0, 255), c1(255, 255, 255, 255);
                        if (!rg->Stops.empty()) {
                            c0 = rg->Stops.front().color;
                            c1 = rg->Stops.back().color;
                            if (rg->Stops.size() > 2) {
                                warn("XAR export: only first/last gradient stops are written");
                            }
                        }
                        XarBody b;
                        Coord(b, ctm.Transform(rg->Center));
                        Coord(b, ctm.Transform(Point2Dd(rg->Center.x + rg->Radius, rg->Center.y)));
                        b.I32(ColourRef(c0));
                        b.I32(ColourRef(c1));
                        Rec(XarOut::CircularFill, b);
                    } else {
                        warn("XAR export: conical/mesh gradients are not supported, "
                             "filling flat with first stop");
                        XarBody b;
                        b.I32(ColourRef(Color(128, 128, 128, 255)));
                        Rec(XarOut::FlatFill, b);
                    }
                }

                // ===== TEXT =====

                struct TextChunkStyle {
                    std::string family;
                    float size = 12.0f;
                    bool bold = false, italic = false, underline = false;
                };

                static TextChunkStyle ResolveChunkStyle(const VectorTextStyle& s,
                                                        const VectorTextStyle& base) {
                    TextChunkStyle out;
                    out.family = s.FontFamily.empty() ? base.FontFamily : s.FontFamily;
                    out.size = s.FontSize > 0 ? s.FontSize : base.FontSize;
                    out.bold = s.Weight == FontWeight::Bold || s.Weight == FontWeight::ExtraBold;
                    out.italic = s.Slant != FontSlant::Normal;
                    out.underline = s.Underline;
                    return out;
                }

                void EmitTextStyleDelta(const TextChunkStyle& want, TextChunkStyle& have) {
                    if (want.family != have.family && !want.family.empty()) {
                        XarBody b;
                        b.I32(FontRef(want.family));
                        Rec(XarOut::TextFontTypeface, b);
                    }
                    if (want.size != have.size && want.size > 0) {
                        XarBody b;
                        b.I32(Mp(want.size));
                        Rec(XarOut::TextFontSize, b);
                    }
                    if (want.bold != have.bold) Rec(want.bold ? XarOut::TextBoldOn : XarOut::TextBoldOff);
                    if (want.italic != have.italic) Rec(want.italic ? XarOut::TextItalicOn : XarOut::TextItalicOff);
                    if (want.underline != have.underline) {
                        Rec(want.underline ? XarOut::TextUnderlineOn : XarOut::TextUnderlineOff);
                    }
                    have = want;
                }

                void EmitText(const VectorText& text, const VectorStyle& style,
                              const Matrix3x3& ctm) {
                    if (!AxisAligned(ctm)) {
                        warn("XAR export: rotated/skewed text is exported without its "
                             "rotation (story matrices are not written yet)");
                    }

                    XarBody sb;
                    Coord(sb, ctm.Transform(text.Position));
                    sb.U32(0);
                    Rec(XarOut::TextStorySimple, sb);
                    Down();

                    switch (text.BaseStyle.Anchor) {
                        case TextAnchor::Middle: Rec(XarOut::TextJustificationCentre); break;
                        case TextAnchor::End: Rec(XarOut::TextJustificationRight); break;
                        default: Rec(XarOut::TextJustificationLeft); break;
                    }

                    Color tc(0, 0, 0, 255);
                    if (style.Fill.has_value()) {
                        if (const Color* c = std::get_if<Color>(&*style.Fill)) tc = *c;
                    }
                    XarBody fb;
                    fb.I32(ColourRef(tc));
                    Rec(XarOut::FlatFill, fb);
                    Rec(XarOut::LineColourNone);

                    TextChunkStyle storyState;   // reader defaults
                    storyState.size = 0;         // force explicit size on first delta
                    TextChunkStyle baseState = ResolveChunkStyle(text.BaseStyle, text.BaseStyle);
                    EmitTextStyleDelta(baseState, storyState);

                    // Flatten spans into lines on '\n'.
                    struct Chunk { std::string text; TextChunkStyle style; };
                    std::vector<std::vector<Chunk>> lines(1);
                    for (const auto& span : text.Spans) {
                        TextChunkStyle cs = ResolveChunkStyle(span.Style, text.BaseStyle);
                        std::string piece;
                        for (char ch : span.Text) {
                            if (ch == '\n') {
                                if (!piece.empty()) lines.back().push_back({piece, cs});
                                piece.clear();
                                lines.emplace_back();
                            } else {
                                piece.push_back(ch);
                            }
                        }
                        if (!piece.empty()) lines.back().push_back({piece, cs});
                    }

                    for (const auto& line : lines) {
                        Rec(XarOut::TextLine);
                        Down();
                        TextChunkStyle lineState = storyState;
                        for (const auto& chunk : line) {
                            EmitTextStyleDelta(chunk.style, lineState);
                            XarBody b;
                            b.Utf16(chunk.text);
                            Rec(XarOut::TextString, b);
                        }
                        Rec(XarOut::TextEOL);
                        Up();
                    }

                    Up();
                }
            };
        }   // anonymous namespace

        bool XARConverter::Impl::ExportToFile(
                const VectorDocument& document,
                const std::string& filename,
                const ConversionOptions& options,
                const XARConversionOptions& xarOptions) {

            auto data = ExportToMemory(document, options, xarOptions);
            if (data.empty()) return false;

            std::ofstream file(filename, std::ios::binary);
            if (!file.is_open()) {
                LogWarning("Failed to create XAR file: " + filename);
                return false;
            }
            file.write(reinterpret_cast<const char*>(data.data()),
                       static_cast<std::streamsize>(data.size()));
            return file.good();
        }

        std::vector<uint8_t> XARConverter::Impl::ExportToMemory(
                const VectorDocument& document,
                const ConversionOptions& options,
                const XARConversionOptions& xarOptions) {

            currentOptions = options;
            currentXarOptions = xarOptions;
            exportState.Reset();

            XarEmitter emitter(document, [this](const std::string& msg) { LogWarning(msg); });
            auto data = emitter.Build();
            ReportProgress(1.0f);
            return data;
        }

    } // namespace VectorConverter
} // namespace UltraCanvas