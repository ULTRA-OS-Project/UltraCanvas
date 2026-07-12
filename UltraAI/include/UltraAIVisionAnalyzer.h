// UltraAI/include/UltraAIVisionAnalyzer.h
// Provider-agnostic image analysis interface (caption, OCR, detection,
// classification, visual Q&A).
// Version: 0.1.0
// Last Modified: 2026-05-08
// Author: UltraAI Module
#pragma once

#include "UltraAICommon.h"

#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace UltraAI {

// =====================================================================
// Analysis tasks
// =====================================================================

enum class VisionTask {
    Caption,             // short natural-language description
    DetailedDescription, // long-form narrative description
    Tags,                // list of keywords / labels
    ObjectDetection,     // labeled bounding boxes
    Segmentation,        // labeled masks
    Ocr,                 // text in the image
    DocumentLayout,      // OCR + structural regions (paragraphs, tables)
    FaceDetection,       // bounding boxes (no identity)
    ContentSafety,       // NSFW / violence / etc. scores
    VisualQuestion       // VQA — answer `prompt` about the image
};

// =====================================================================
// Geometry helpers (kept local to avoid pulling in UltraCanvas types)
// =====================================================================

struct BoundingBox {
    // Normalised coordinates [0,1] relative to image dimensions.
    double x = 0.0, y = 0.0, width = 0.0, height = 0.0;
};

struct Polygon {
    std::vector<std::pair<double, double>> points;   // normalised
};

// =====================================================================
// Result records
// =====================================================================

struct DetectedObject {
    std::string label;
    double confidence = 0.0;
    BoundingBox box;
};

struct SegmentationMask {
    std::string label;
    double confidence = 0.0;
    MediaBlob maskImage;          // single-channel mask, same size as input
};

struct OcrSpan {
    std::string text;
    double confidence = 0.0;
    Polygon polygon;
    std::string language;         // BCP-47 if detected
};

struct DocumentRegion {
    enum class Kind { Paragraph, Heading, Table, List, Figure, Caption, Other };
    Kind kind = Kind::Other;
    BoundingBox box;
    std::string text;
    int32_t readingOrder = 0;
};

struct FaceDetection {
    BoundingBox box;
    double confidence = 0.0;
    // Demographic / identity fields are intentionally omitted; add via
    // OptionsMap on the request when a provider supports them.
};

struct SafetyScore {
    std::string category;         // "nsfw", "violence", "hate", ...
    double score = 0.0;           // 0..1
    bool flagged = false;
};

struct Tag {
    std::string label;
    double confidence = 0.0;
};

// =====================================================================
// Request / Response
// =====================================================================

struct VisionAnalyzeRequest {
    std::string model;            // empty -> adapter default
    MediaBlob image;
    std::vector<VisionTask> tasks;

    std::string prompt;           // used for VisualQuestion / DetailedDescription
    std::string language;         // preferred output language (BCP-47)

    int32_t maxObjects = 0;       // 0 -> provider default
    double  minConfidence = 0.0;  // filter low-confidence results

    OptionsMap options;
};

struct VisionAnalyzeResponse {
    std::string model;

    std::string caption;
    std::string detailedDescription;
    std::vector<Tag> tags;
    std::vector<DetectedObject> objects;
    std::vector<SegmentationMask> segments;

    std::string ocrText;
    std::vector<OcrSpan> ocrSpans;
    std::vector<DocumentRegion> documentRegions;

    std::vector<FaceDetection> faces;
    std::vector<SafetyScore> safety;

    std::string vqaAnswer;        // for VisualQuestion

    TokenUsage usage;
    Error error;
};

// =====================================================================
// Capability discovery
// =====================================================================

struct VisionModelInfo {
    std::string id;
    std::string displayName;
    std::vector<VisionTask> supportedTasks;
    std::vector<std::string> supportedLanguages;
    int32_t maxImagePixels = 0;    // 0 -> unspecified
    bool runsLocally = false;
};

struct VisionProviderCapabilities {
    std::string providerId;
    std::vector<VisionModelInfo> models;
};

// =====================================================================
// IVisionAnalyzer — the capability interface
// =====================================================================

class IVisionAnalyzer {
public:
    virtual ~IVisionAnalyzer() = default;

    virtual VisionProviderCapabilities GetCapabilities() const = 0;

    virtual VisionAnalyzeResponse Analyze(
        const VisionAnalyzeRequest& request) = 0;

    virtual std::future<VisionAnalyzeResponse> AnalyzeAsync(
        const VisionAnalyzeRequest& request) = 0;

    virtual void* RawProvider() { return nullptr; }
};

// =====================================================================
// Factory
// =====================================================================

using VisionAnalyzerConfig = ProviderConfig;

std::unique_ptr<IVisionAnalyzer> CreateVisionAnalyzer(
    const VisionAnalyzerConfig& config,
    Error* outError = nullptr);

std::vector<std::string> ListVisionAnalyzerProviders();

bool RegisterVisionAnalyzerProvider(
    const std::string& providerId,
    std::function<std::unique_ptr<IVisionAnalyzer>(const VisionAnalyzerConfig&, Error*)> factory);

} // namespace UltraAI
