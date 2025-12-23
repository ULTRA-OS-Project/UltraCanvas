// UltraCanvas/Plugins/Vector/UltraCanvasXARConverter.cpp
// XAR (Xara) Vector Format Converter - Specification-Compliant Implementation
// Version: 2.0.0
// Last Modified: 2025-01-20
// Author: UltraCanvas Framework

#include "UltraCanvasXARConverter.h"
#include "UltraCanvasVectorStorage.h"
#include <fstream>
#include <sstream>
#include <cstring>
#include <zlib.h>
#include <stack>
#include <algorithm>
#include <chrono>

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

            // ===== WRITING HELPERS =====
            void WriteFileHeader(std::ostream& stream, uint32_t fileSize);
            void WriteRecord(std::ostream& stream, uint32_t tag, const std::vector<uint8_t>& data);
            void WriteRecord(std::ostream& stream, uint32_t tag);  // Empty record

            // Element writers
            void WriteDocument(std::ostream& stream, const VectorDocument& document);
            void WriteLayer(std::ostream& stream, const VectorLayer& layer);
            void WriteElement(std::ostream& stream, const VectorElement& element);
            void WriteRect(std::ostream& stream, const VectorRect& rect);
            void WriteCircle(std::ostream& stream, const VectorCircle& circle);
            void WriteEllipse(std::ostream& stream, const VectorEllipse& ellipse);
            void WritePath(std::ostream& stream, const VectorPath& path);
            void WriteText(std::ostream& stream, const VectorText& text);
            void WriteGroup(std::ostream& stream, const VectorGroup& group);

            // Attribute writers
            void WriteStyle(std::ostream& stream, const VectorStyle& style);
            void WriteFill(std::ostream& stream, const FillData& fill);
            void WriteStroke(std::ostream& stream, const StrokeData& stroke);
            void WriteTransform(std::ostream& stream, const Matrix3x3& transform);

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
                importState.document->Size = Size2Df{595.0f, 842.0f};  // A4 in points
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

                Point2Df loPoint = FromXARCoord(lo);
                Point2Df hiPoint = FromXARCoord(hi);

                importState.document->Size.width = hiPoint.x - loPoint.x;
                importState.document->Size.height = hiPoint.y - loPoint.y;
                importState.document->ViewBox = Rect2Df{loPoint.x, loPoint.y,
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

            Point2Df currentPoint{0, 0};
            Point2Df subpathStart{0, 0};

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
                    Point2Df pt = FromXARCoord(coords[coordIndex++]);
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
                    Point2Df pt = FromXARCoord(coords[coordIndex++]);
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
                        Point2Df c1 = FromXARCoord(coords[coordIndex++]);
                        Point2Df c2 = FromXARCoord(coords[coordIndex++]);
                        Point2Df end = FromXARCoord(coords[coordIndex++]);

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

            Point2Df loPoint = FromXARCoord(lo);
            Point2Df hiPoint = FromXARCoord(hi);

            rect->Bounds = Rect2Df{loPoint.x, loPoint.y,
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

            Point2Df centrePoint = FromXARCoord(centre);
            Point2Df majorPoint = FromXARCoord(majorAxis);
            Point2Df minorPoint = FromXARCoord(minorAxis);

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

            Point2Df centrePoint = FromXARCoord(centre);
            Point2Df majorPoint = FromXARCoord(majorAxis);

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
            Point2Df p0 = FromXARCoord(corners[0]);
            Point2Df p1 = FromXARCoord(corners[1]);
            Point2Df p2 = FromXARCoord(corners[2]);
            Point2Df p3 = FromXARCoord(corners[3]);

            float minX = std::min({p0.x, p1.x, p2.x, p3.x});
            float minY = std::min({p0.y, p1.y, p2.y, p3.y});
            float maxX = std::max({p0.x, p1.x, p2.x, p3.x});
            float maxY = std::max({p0.y, p1.y, p2.y, p3.y});

            image->Bounds = Rect2Df{minX, minY, maxX - minX, maxY - minY};

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

// ===== EFFECT PROCESSING =====

        void XARConverter::Impl::ProcessFeather(const std::vector<uint8_t>& data) {
            if (data.size() < sizeof(XARFeatherData)) return;

            XARFeatherData featherData = *reinterpret_cast<const XARFeatherData*>(data.data());

            // Store feather as shadow-like effect with blur
            float featherSize = static_cast<float>(featherData.FeatherSize) / XAR_MILLIPOINTS_PER_POINT;

            importState.currentStyle.ShadowBlur = featherSize;
            // Feather doesn't have offset or color - it's just edge blur
        }

        void XARConverter::Impl::ProcessShadow(uint32_t tag, const std::vector<uint8_t>& data) {
            if (data.size() < 2 * sizeof(XARCoord) + sizeof(XARColourRGB) + sizeof(int32_t)) return;

            size_t offset = 0;

            // Read shadow offset
            XARCoord shadowOffset = *reinterpret_cast<const XARCoord*>(data.data() + offset);
            offset += sizeof(XARCoord);

            // Read blur amount
            int32_t blur = *reinterpret_cast<const int32_t*>(data.data() + offset);
            offset += sizeof(int32_t);

            // Read shadow colour
            XARColourRGB shadowColour = *reinterpret_cast<const XARColourRGB*>(data.data() + offset);

            importState.currentStyle.ShadowOffset = FromXARCoord(shadowOffset);
            importState.currentStyle.ShadowBlur = static_cast<float>(blur) / XAR_MILLIPOINTS_PER_POINT;
            importState.currentStyle.ShadowColor = FromXARColour(shadowColour);
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

        bool XARConverter::Impl::ExportToFile(
                const VectorDocument& document,
                const std::string& filename,
                const ConversionOptions& options,
                const XARConversionOptions& xarOptions) {

            std::ofstream file(filename, std::ios::binary);
            if (!file.is_open()) {
                LogWarning("Failed to create XAR file: " + filename);
                return false;
            }

            currentOptions = options;
            currentXarOptions = xarOptions;
            exportState.Reset();

            // Write to memory first to calculate size
            std::ostringstream contentStream(std::ios::binary);
            WriteDocument(contentStream, document);
            std::string content = contentStream.str();

            // Calculate total file size
            uint32_t fileSize = sizeof(XARFileHeader) + static_cast<uint32_t>(content.size());

            // Write header
            WriteFileHeader(file, fileSize);

            // Write content
            file.write(content.data(), content.size());

            return file.good();
        }

        std::vector<uint8_t> XARConverter::Impl::ExportToMemory(
                const VectorDocument& document,
                const ConversionOptions& options,
                const XARConversionOptions& xarOptions) {

            currentOptions = options;
            currentXarOptions = xarOptions;
            exportState.Reset();

            // Write content to stream
            std::ostringstream contentStream(std::ios::binary);
            WriteDocument(contentStream, document);
            std::string content = contentStream.str();

            // Prepare header
            std::ostringstream headerStream(std::ios::binary);
            uint32_t fileSize = sizeof(XARFileHeader) + static_cast<uint32_t>(content.size());
            WriteFileHeader(headerStream, fileSize);
            std::string header = headerStream.str();

            // Combine
            std::vector<uint8_t> result;
            result.reserve(header.size() + content.size());
            result.insert(result.end(), header.begin(), header.end());
            result.insert(result.end(), content.begin(), content.end());

            return result;
        }

        void XARConverter::Impl::WriteFileHeader(std::ostream& stream, uint32_t fileSize) {
            XARFileHeader header;
            std::memcpy(header.Signature, XAR_SIGNATURE, sizeof(XAR_SIGNATURE));
            header.FileSize = fileSize;
            header.Version = 1;
            header.BuildNumber = 1;
            header.PreCompFlags = 0;
            header.Checksum = 0;  // Optional - would need CRC32 implementation

            stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
        }

        void XARConverter::Impl::WriteRecord(std::ostream& stream, uint32_t tag,
                                             const std::vector<uint8_t>& data) {
            XARRecordHeader header;
            header.Tag = tag;
            header.Size = static_cast<uint32_t>(data.size());

            stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
            if (!data.empty()) {
                stream.write(reinterpret_cast<const char*>(data.data()), data.size());
            }
        }

        void XARConverter::Impl::WriteRecord(std::ostream& stream, uint32_t tag) {
            WriteRecord(stream, tag, std::vector<uint8_t>());
        }

        void XARConverter::Impl::WriteDocument(std::ostream& stream, const VectorDocument& document) {
            // File header record
            WriteRecord(stream, TAG_FILEHEADER);

            // Document start
            WriteRecord(stream, TAG_DOCUMENT);
            WriteRecord(stream, TAG_DOWN);

            // Spread information (page size)
            {
                std::vector<uint8_t> data(4 * sizeof(XARCoord));
                size_t offset = 0;

                XARCoord lo = ToXARCoord(Point2Df{0, 0});
                XARCoord hi = ToXARCoord(Point2Df{document.Size.width, document.Size.height});

                std::memcpy(data.data() + offset, &lo, sizeof(XARCoord));
                offset += sizeof(XARCoord);
                std::memcpy(data.data() + offset, &hi, sizeof(XARCoord));

                WriteRecord(stream, TAG_SPREADINFORMATION, data);
            }

            // Write layers
            for (const auto& layer : document.Layers) {
                WriteLayer(stream, *layer);
            }

            // Document end
            WriteRecord(stream, TAG_UP);

            // End of file
            WriteRecord(stream, TAG_ENDOFFILE);
        }

        void XARConverter::Impl::WriteLayer(std::ostream& stream, const VectorLayer& layer) {
            // Layer record with name
            std::vector<uint8_t> layerData;
            layerData.insert(layerData.end(), layer.Name.begin(), layer.Name.end());
            layerData.push_back(0);  // Null terminator

            // Flags
            uint32_t flags = 0;
            if (layer.Visible) flags |= 0x01;
            if (layer.Locked) flags |= 0x02;

            size_t prevSize = layerData.size();
            layerData.resize(prevSize + sizeof(uint32_t));
            std::memcpy(layerData.data() + prevSize, &flags, sizeof(uint32_t));

            WriteRecord(stream, TAG_LAYER, layerData);
            WriteRecord(stream, TAG_DOWN);

            // Write all children
            for (const auto& child : layer.Children) {
                WriteElement(stream, *child);
            }

            WriteRecord(stream, TAG_UP);
        }

        void XARConverter::Impl::WriteElement(std::ostream& stream, const VectorElement& element) {
            // Write style attributes first
            WriteStyle(stream, element.Style);

            // Write transform if present
            if (element.Transform.has_value()) {
                WriteTransform(stream, element.Transform.value());
            }

            switch (element.Type) {
                case VectorElementType::Rectangle:
                case VectorElementType::RoundedRectangle:
                    WriteRect(stream, static_cast<const VectorRect&>(element));
                    break;

                case VectorElementType::Circle:
                    WriteCircle(stream, static_cast<const VectorCircle&>(element));
                    break;

                case VectorElementType::Ellipse:
                    WriteEllipse(stream, static_cast<const VectorEllipse&>(element));
                    break;

                case VectorElementType::Path:
                    WritePath(stream, static_cast<const VectorPath&>(element));
                    break;

                case VectorElementType::Text:
                    WriteText(stream, static_cast<const VectorText&>(element));
                    break;

                case VectorElementType::Group:
                case VectorElementType::Layer:
                    WriteGroup(stream, static_cast<const VectorGroup&>(element));
                    break;

                default:
                    // Unsupported element type - skip
                    break;
            }
        }

        void XARConverter::Impl::WriteRect(std::ostream& stream, const VectorRect& rect) {
            std::vector<uint8_t> data;

            XARCoord lo = ToXARCoord(Point2Df{rect.Bounds.x, rect.Bounds.y});
            XARCoord hi = ToXARCoord(Point2Df{rect.Bounds.x + rect.Bounds.width,
                                              rect.Bounds.y + rect.Bounds.height});

            data.resize(2 * sizeof(XARCoord));
            std::memcpy(data.data(), &lo, sizeof(XARCoord));
            std::memcpy(data.data() + sizeof(XARCoord), &hi, sizeof(XARCoord));

            uint32_t tag = TAG_RECTANGLE_SIMPLE;

            if (rect.RadiusX > 0 || rect.RadiusY > 0) {
                tag = TAG_RECTANGLE_SIMPLE_ROUNDED;
                int32_t radius = static_cast<int32_t>(rect.RadiusX * XAR_MILLIPOINTS_PER_POINT);
                data.resize(data.size() + sizeof(int32_t));
                std::memcpy(data.data() + 2 * sizeof(XARCoord), &radius, sizeof(int32_t));
            }

            WriteRecord(stream, tag, data);
        }

        void XARConverter::Impl::WriteCircle(std::ostream& stream, const VectorCircle& circle) {
            std::vector<uint8_t> data(3 * sizeof(XARCoord));

            XARCoord centre = ToXARCoord(circle.Center);
            XARCoord majorAxis = ToXARCoord(Point2Df{circle.Center.x + circle.Radius, circle.Center.y});
            XARCoord minorAxis = ToXARCoord(Point2Df{circle.Center.x, circle.Center.y + circle.Radius});

            std::memcpy(data.data(), &centre, sizeof(XARCoord));
            std::memcpy(data.data() + sizeof(XARCoord), &majorAxis, sizeof(XARCoord));
            std::memcpy(data.data() + 2 * sizeof(XARCoord), &minorAxis, sizeof(XARCoord));

            WriteRecord(stream, TAG_ELLIPSE_SIMPLE, data);
        }

        void XARConverter::Impl::WriteEllipse(std::ostream& stream, const VectorEllipse& ellipse) {
            std::vector<uint8_t> data(3 * sizeof(XARCoord));

            XARCoord centre = ToXARCoord(ellipse.Center);
            XARCoord majorAxis = ToXARCoord(Point2Df{ellipse.Center.x + ellipse.RadiusX, ellipse.Center.y});
            XARCoord minorAxis = ToXARCoord(Point2Df{ellipse.Center.x, ellipse.Center.y + ellipse.RadiusY});

            std::memcpy(data.data(), &centre, sizeof(XARCoord));
            std::memcpy(data.data() + sizeof(XARCoord), &majorAxis, sizeof(XARCoord));
            std::memcpy(data.data() + 2 * sizeof(XARCoord), &minorAxis, sizeof(XARCoord));

            WriteRecord(stream, TAG_ELLIPSE_COMPLEX, data);
        }

        void XARConverter::Impl::WritePath(std::ostream& stream, const VectorPath& path) {
            const PathData& pathData = path.Path;
            if (pathData.Commands.empty()) return;

            // Count elements
            uint32_t numElements = 0;
            for (const auto& cmd : pathData.Commands) {
                switch (cmd.Type) {
                    case PathCommandType::MoveTo:
                    case PathCommandType::LineTo:
                        numElements++;
                        break;
                    case PathCommandType::CurveTo:
                        numElements += 3;  // 2 control + 1 end
                        break;
                    case PathCommandType::QuadraticTo:
                        numElements += 2;  // 1 control + 1 end
                        break;
                    case PathCommandType::ClosePath:
                        numElements++;
                        break;
                    default:
                        break;
                }
            }

            // Build data buffer: count + verbs + coordinates
            std::vector<uint8_t> data;
            data.resize(sizeof(uint32_t) + numElements + numElements * sizeof(XARCoord));

            size_t offset = 0;
            std::memcpy(data.data() + offset, &numElements, sizeof(uint32_t));
            offset += sizeof(uint32_t);

            // Write verbs
            uint8_t* verbs = data.data() + offset;
            offset += numElements;

            // Write coordinates
            XARCoord* coords = reinterpret_cast<XARCoord*>(data.data() + offset);

            size_t verbIdx = 0;
            size_t coordIdx = 0;

            for (const auto& cmd : pathData.Commands) {
                switch (cmd.Type) {
                    case PathCommandType::MoveTo:
                        verbs[verbIdx++] = VERB_MOVETO;
                        coords[coordIdx++] = ToXARCoord(Point2Df{cmd.Parameters[0], cmd.Parameters[1]});
                        break;

                    case PathCommandType::LineTo:
                        verbs[verbIdx++] = VERB_LINETO;
                        coords[coordIdx++] = ToXARCoord(Point2Df{cmd.Parameters[0], cmd.Parameters[1]});
                        break;

                    case PathCommandType::CurveTo:
                        verbs[verbIdx++] = VERB_CURVETO | PATHFLAG_CONTROL;
                        coords[coordIdx++] = ToXARCoord(Point2Df{cmd.Parameters[0], cmd.Parameters[1]});
                        verbs[verbIdx++] = VERB_CURVETO | PATHFLAG_CONTROL;
                        coords[coordIdx++] = ToXARCoord(Point2Df{cmd.Parameters[2], cmd.Parameters[3]});
                        verbs[verbIdx++] = VERB_CURVETO;
                        coords[coordIdx++] = ToXARCoord(Point2Df{cmd.Parameters[4], cmd.Parameters[5]});
                        break;

                    case PathCommandType::ClosePath:
                        verbs[verbIdx++] = VERB_CLOSEPATH;
                        coords[coordIdx++] = XARCoord{0, 0};  // Placeholder
                        break;

                    default:
                        break;
                }
            }

            // Determine path type
            bool hasFill = path.Style.Fill.has_value();
            bool hasStroke = path.Style.Stroke.has_value();

            uint32_t tag = TAG_PATH;
            if (hasFill && hasStroke) tag = TAG_PATH_FILLED_STROKED;
            else if (hasFill) tag = TAG_PATH_FILLED;
            else if (hasStroke) tag = TAG_PATH_STROKED;

            WriteRecord(stream, tag, data);
        }

        void XARConverter::Impl::WriteText(std::ostream& stream, const VectorText& text) {
            // Write font attributes
            if (!text.BaseStyle.FontFamily.empty()) {
                std::vector<uint8_t> fontData(text.BaseStyle.FontFamily.begin(),
                                              text.BaseStyle.FontFamily.end());
                fontData.push_back(0);
                WriteRecord(stream, TAG_FONTNAME, fontData);
            }

            // Write font size
            {
                std::vector<uint8_t> sizeData(sizeof(int32_t));
                int32_t size = static_cast<int32_t>(text.BaseStyle.FontSize * XAR_MILLIPOINTS_PER_POINT);
                std::memcpy(sizeData.data(), &size, sizeof(int32_t));
                WriteRecord(stream, TAG_FONTSIZE, sizeData);
            }

            // Write text content
            std::string plainText = text.GetPlainText();
            std::vector<uint8_t> textData(plainText.begin(), plainText.end());
            textData.push_back(0);
            WriteRecord(stream, TAG_TEXT_STRING, textData);
        }

        void XARConverter::Impl::WriteGroup(std::ostream& stream, const VectorGroup& group) {
            WriteRecord(stream, TAG_GROUP);
            WriteRecord(stream, TAG_DOWN);

            for (const auto& child : group.Children) {
                WriteElement(stream, *child);
            }

            WriteRecord(stream, TAG_UP);
        }

        void XARConverter::Impl::WriteStyle(std::ostream& stream, const VectorStyle& style) {
            // Write fill
            if (style.Fill.has_value()) {
                WriteFill(stream, style.Fill.value());
            }

            // Write stroke
            if (style.Stroke.has_value()) {
                WriteStroke(stream, style.Stroke.value());
            }

            // Write opacity as flat transparency
            if (style.Opacity < 1.0f) {
                std::vector<uint8_t> data(1);
                data[0] = static_cast<uint8_t>((1.0f - style.Opacity) * 255);
                WriteRecord(stream, TAG_FLATTRANSPARENTFILL, data);
            }
        }

        void XARConverter::Impl::WriteFill(std::ostream& stream, const FillData& fill) {
            if (auto* color = std::get_if<Color>(&fill)) {
                std::vector<uint8_t> data(sizeof(XARColourRGB));
                XARColourRGB xarColor = ToXARColour(*color);
                std::memcpy(data.data(), &xarColor, sizeof(XARColourRGB));
                WriteRecord(stream, TAG_FLATFILL, data);
            }
            else if (auto* gradient = std::get_if<GradientData>(&fill)) {
                if (auto* linear = std::get_if<LinearGradientData>(gradient)) {
                    std::vector<uint8_t> data(sizeof(XARLinearFillData));
                    XARLinearFillData fillData;
                    fillData.StartPoint = ToXARCoord(linear->Start);
                    fillData.EndPoint = ToXARCoord(linear->End);
                    fillData.EndPoint2 = fillData.EndPoint;

                    if (!linear->Stops.empty()) {
                        fillData.StartColour = ToXARColour(linear->Stops.front().StopColor);
                        fillData.EndColour = ToXARColour(linear->Stops.back().StopColor);
                    }

                    std::memcpy(data.data(), &fillData, sizeof(XARLinearFillData));
                    WriteRecord(stream, TAG_LINEARFILL, data);
                }
                else if (auto* radial = std::get_if<RadialGradientData>(gradient)) {
                    std::vector<uint8_t> data(sizeof(XARRadialFillData));
                    XARRadialFillData fillData;
                    fillData.CentrePoint = ToXARCoord(radial->Center);
                    fillData.MajorAxes = ToXARCoord(Point2Df{radial->Center.x + radial->Radius, radial->Center.y});
                    fillData.MinorAxes = ToXARCoord(Point2Df{radial->Center.x, radial->Center.y + radial->Radius});

                    if (!radial->Stops.empty()) {
                        fillData.StartColour = ToXARColour(radial->Stops.front().StopColor);
                        fillData.EndColour = ToXARColour(radial->Stops.back().StopColor);
                    }

                    std::memcpy(data.data(), &fillData, sizeof(XARRadialFillData));
                    WriteRecord(stream, TAG_CIRCULARFILL, data);
                }
                else if (auto* conical = std::get_if<ConicalGradientData>(gradient)) {
                    std::vector<uint8_t> data(sizeof(XARConicalFillData));
                    XARConicalFillData fillData;
                    fillData.CentrePoint = ToXARCoord(conical->Center);

                    float endX = conical->Center.x + 100 * std::cos(conical->StartAngle);
                    float endY = conical->Center.y + 100 * std::sin(conical->StartAngle);
                    fillData.EndPoint = ToXARCoord(Point2Df{endX, endY});

                    if (!conical->Stops.empty()) {
                        fillData.StartColour = ToXARColour(conical->Stops.front().StopColor);
                        fillData.EndColour = ToXARColour(conical->Stops.back().StopColor);
                    }

                    std::memcpy(data.data(), &fillData, sizeof(XARConicalFillData));
                    WriteRecord(stream, TAG_CONICALFILL, data);
                }
            }
        }

        void XARConverter::Impl::WriteStroke(std::ostream& stream, const StrokeData& stroke) {
            // Write stroke colour
            if (auto* color = std::get_if<Color>(&stroke.Fill)) {
                std::vector<uint8_t> data(sizeof(XARColourRGB));
                XARColourRGB xarColor = ToXARColour(*color);
                std::memcpy(data.data(), &xarColor, sizeof(XARColourRGB));
                WriteRecord(stream, TAG_LINECOLOUR, data);
            }

            // Write stroke width
            {
                std::vector<uint8_t> data(sizeof(int32_t));
                int32_t width = static_cast<int32_t>(stroke.Width * XAR_MILLIPOINTS_PER_POINT);
                std::memcpy(data.data(), &width, sizeof(int32_t));
                WriteRecord(stream, TAG_LINEWIDTH, data);
            }

            // Write line cap
            {
                std::vector<uint8_t> data(1);
                switch (stroke.LineCap) {
                    case StrokeLineCap::Butt: data[0] = 0; break;
                    case StrokeLineCap::Round: data[0] = 1; break;
                    case StrokeLineCap::Square: data[0] = 2; break;
                }
                WriteRecord(stream, TAG_STARTCAP, data);
            }

            // Write line join
            {
                std::vector<uint8_t> data(1);
                switch (stroke.LineJoin) {
                    case StrokeLineJoin::Miter: data[0] = 0; break;
                    case StrokeLineJoin::Round: data[0] = 1; break;
                    case StrokeLineJoin::Bevel: data[0] = 2; break;
                    default: data[0] = 0; break;
                }
                WriteRecord(stream, TAG_JOINSTYLE, data);
            }

            // Write dash pattern if present
            if (!stroke.DashArray.empty()) {
                std::vector<uint8_t> data;
                data.resize(sizeof(uint32_t) + stroke.DashArray.size() * sizeof(int32_t) + sizeof(int32_t));

                size_t offset = 0;
                uint32_t numDashes = static_cast<uint32_t>(stroke.DashArray.size());
                std::memcpy(data.data() + offset, &numDashes, sizeof(uint32_t));
                offset += sizeof(uint32_t);

                for (float dash : stroke.DashArray) {
                    int32_t dashValue = static_cast<int32_t>(dash * XAR_MILLIPOINTS_PER_POINT);
                    std::memcpy(data.data() + offset, &dashValue, sizeof(int32_t));
                    offset += sizeof(int32_t);
                }

                int32_t dashOffset = static_cast<int32_t>(stroke.DashOffset * XAR_MILLIPOINTS_PER_POINT);
                std::memcpy(data.data() + offset, &dashOffset, sizeof(int32_t));

                WriteRecord(stream, TAG_DASHSTYLE, data);
            }
        }

        void XARConverter::Impl::WriteTransform(std::ostream& stream, const Matrix3x3& transform) {
            std::vector<uint8_t> data(sizeof(XARMatrix));
            XARMatrix xarMatrix = ToXARMatrix(transform);
            std::memcpy(data.data(), &xarMatrix, sizeof(XARMatrix));
            // XAR doesn't have a dedicated transform tag - transforms are applied per-object
            // This would typically be handled by transforming coordinates directly
        }

    } // namespace VectorConverter
} // namespace UltraCanvas