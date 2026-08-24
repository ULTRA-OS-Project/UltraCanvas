// UltraAI/adapters/comfyui/src/ComfyUIWorkflows.cpp
// The built-in templates and node-binding helpers.
// Version: 0.2.0
// Last Modified: 2026-08-24
// Author: UltraAI Module

#include "ComfyUIWorkflows.h"

#include <utility>

namespace UltraAI {
namespace comfyui_detail {

namespace {

// Text to image: checkpoint -> two CLIP encoders -> KSampler over an empty
// latent -> VAE decode -> SaveImage.
constexpr const char* kTextToImage = R"JSON({
  "3": {"class_type": "KSampler", "_meta": {"title": "UltraAI Sampler"},
        "inputs": {"seed": 0, "steps": 20, "cfg": 7.0,
                   "sampler_name": "euler", "scheduler": "normal",
                   "denoise": 1.0,
                   "model": ["4", 0], "positive": ["6", 0],
                   "negative": ["7", 0], "latent_image": ["5", 0]}},
  "4": {"class_type": "CheckpointLoaderSimple",
        "_meta": {"title": "UltraAI Checkpoint"},
        "inputs": {"ckpt_name": ""}},
  "5": {"class_type": "EmptyLatentImage", "_meta": {"title": "UltraAI Latent"},
        "inputs": {"width": 512, "height": 512, "batch_size": 1}},
  "6": {"class_type": "CLIPTextEncode", "_meta": {"title": "UltraAI Positive"},
        "inputs": {"text": "", "clip": ["4", 1]}},
  "7": {"class_type": "CLIPTextEncode", "_meta": {"title": "UltraAI Negative"},
        "inputs": {"text": "", "clip": ["4", 1]}},
  "8": {"class_type": "VAEDecode",
        "inputs": {"samples": ["3", 0], "vae": ["4", 2]}},
  "9": {"class_type": "SaveImage", "_meta": {"title": "UltraAI Output"},
        "inputs": {"filename_prefix": "UltraAI", "images": ["8", 0]}}
})JSON";

// Image to image: same graph with the empty latent replaced by a VAE
// encode of the uploaded source. KSampler.denoise carries `strength`.
constexpr const char* kImageToImage = R"JSON({
  "3": {"class_type": "KSampler", "_meta": {"title": "UltraAI Sampler"},
        "inputs": {"seed": 0, "steps": 20, "cfg": 7.0,
                   "sampler_name": "euler", "scheduler": "normal",
                   "denoise": 0.75,
                   "model": ["4", 0], "positive": ["6", 0],
                   "negative": ["7", 0], "latent_image": ["5", 0]}},
  "4": {"class_type": "CheckpointLoaderSimple",
        "_meta": {"title": "UltraAI Checkpoint"},
        "inputs": {"ckpt_name": ""}},
  "5": {"class_type": "VAEEncode",
        "inputs": {"pixels": ["10", 0], "vae": ["4", 2]}},
  "6": {"class_type": "CLIPTextEncode", "_meta": {"title": "UltraAI Positive"},
        "inputs": {"text": "", "clip": ["4", 1]}},
  "7": {"class_type": "CLIPTextEncode", "_meta": {"title": "UltraAI Negative"},
        "inputs": {"text": "", "clip": ["4", 1]}},
  "8": {"class_type": "VAEDecode",
        "inputs": {"samples": ["3", 0], "vae": ["4", 2]}},
  "9": {"class_type": "SaveImage", "_meta": {"title": "UltraAI Output"},
        "inputs": {"filename_prefix": "UltraAI", "images": ["8", 0]}},
  "10": {"class_type": "LoadImage", "_meta": {"title": "UltraAI Source"},
         "inputs": {"image": ""}}
})JSON";

// Inpaint: source plus mask through VAEEncodeForInpaint.
constexpr const char* kInpaint = R"JSON({
  "3": {"class_type": "KSampler", "_meta": {"title": "UltraAI Sampler"},
        "inputs": {"seed": 0, "steps": 20, "cfg": 7.0,
                   "sampler_name": "euler", "scheduler": "normal",
                   "denoise": 1.0,
                   "model": ["4", 0], "positive": ["6", 0],
                   "negative": ["7", 0], "latent_image": ["5", 0]}},
  "4": {"class_type": "CheckpointLoaderSimple",
        "_meta": {"title": "UltraAI Checkpoint"},
        "inputs": {"ckpt_name": ""}},
  "5": {"class_type": "VAEEncodeForInpaint",
        "inputs": {"pixels": ["10", 0], "vae": ["4", 2],
                   "mask": ["11", 0], "grow_mask_by": 6}},
  "6": {"class_type": "CLIPTextEncode", "_meta": {"title": "UltraAI Positive"},
        "inputs": {"text": "", "clip": ["4", 1]}},
  "7": {"class_type": "CLIPTextEncode", "_meta": {"title": "UltraAI Negative"},
        "inputs": {"text": "", "clip": ["4", 1]}},
  "8": {"class_type": "VAEDecode",
        "inputs": {"samples": ["3", 0], "vae": ["4", 2]}},
  "9": {"class_type": "SaveImage", "_meta": {"title": "UltraAI Output"},
        "inputs": {"filename_prefix": "UltraAI", "images": ["8", 0]}},
  "10": {"class_type": "LoadImage", "_meta": {"title": "UltraAI Source"},
         "inputs": {"image": ""}},
  "11": {"class_type": "LoadImageMask", "_meta": {"title": "UltraAI Mask"},
         "inputs": {"image": "", "channel": "red"}}
})JSON";

// Upscale: a plain resampling scale of the uploaded image. Model-based
// upscaling (UpscaleModelLoader + ImageUpscaleWithModel) needs a model file
// the adapter cannot assume is installed; supply a workflow for that.
constexpr const char* kUpscale = R"JSON({
  "9": {"class_type": "SaveImage", "_meta": {"title": "UltraAI Output"},
        "inputs": {"filename_prefix": "UltraAI", "images": ["12", 0]}},
  "10": {"class_type": "LoadImage", "_meta": {"title": "UltraAI Source"},
         "inputs": {"image": ""}},
  "12": {"class_type": "ImageScaleBy", "_meta": {"title": "UltraAI Upscale"},
         "inputs": {"upscale_method": "lanczos", "scale_by": 2.0,
                    "image": ["10", 0]}}
})JSON";

// Image to video with Stable Video Diffusion. SVD is image-conditioned and
// has no text encoder, so VideoGenRequest::prompt has nothing to bind to
// here — a prompt-driven video model needs its own workflow.
constexpr const char* kImageToVideo = R"JSON({
  "3": {"class_type": "KSampler", "_meta": {"title": "UltraAI Sampler"},
        "inputs": {"seed": 0, "steps": 20, "cfg": 2.5,
                   "sampler_name": "euler", "scheduler": "karras",
                   "denoise": 1.0,
                   "model": ["13", 0], "positive": ["12", 0],
                   "negative": ["12", 1], "latent_image": ["12", 2]}},
  "8": {"class_type": "VAEDecode",
        "inputs": {"samples": ["3", 0], "vae": ["15", 2]}},
  "9": {"class_type": "SaveAnimatedWEBP", "_meta": {"title": "UltraAI Output"},
        "inputs": {"filename_prefix": "UltraAI", "fps": 6, "lossless": false,
                   "quality": 85, "method": "default", "images": ["8", 0]}},
  "10": {"class_type": "LoadImage", "_meta": {"title": "UltraAI Source"},
         "inputs": {"image": ""}},
  "12": {"class_type": "SVD_img2vid_Conditioning",
         "_meta": {"title": "UltraAI Video"},
         "inputs": {"width": 1024, "height": 576, "video_frames": 14,
                    "motion_bucket_id": 127, "fps": 6,
                    "augmentation_level": 0.0,
                    "clip_vision": ["15", 1], "init_image": ["10", 0],
                    "vae": ["15", 2]}},
  "13": {"class_type": "VideoLinearCFGGuidance",
         "inputs": {"min_cfg": 1.0, "model": ["15", 0]}},
  "15": {"class_type": "ImageOnlyCheckpointLoader",
         "_meta": {"title": "UltraAI Checkpoint"},
         "inputs": {"ckpt_name": ""}}
})JSON";

json Parse(const char* text) {
    return json::parse(text, nullptr, /*allow_exceptions=*/false);
}

} // namespace

json BuiltInWorkflow(ImageGenMode mode) {
    switch (mode) {
        case ImageGenMode::TextToImage:  return Parse(kTextToImage);
        case ImageGenMode::ImageToImage: return Parse(kImageToImage);
        case ImageGenMode::Inpaint:      return Parse(kInpaint);
        case ImageGenMode::Upscale:      return Parse(kUpscale);
        // Outpaint needs a pad node plus a mask the caller rarely has;
        // BackgroundRemoval and Variation need custom nodes. All three are
        // reachable by supplying a workflow through the options map.
        case ImageGenMode::Outpaint:
        case ImageGenMode::BackgroundRemoval:
        case ImageGenMode::Variation:
        default:
            return json::object();
    }
}

json BuiltInVideoWorkflow(VideoGenMode mode) {
    switch (mode) {
        case VideoGenMode::ImageToVideo: return Parse(kImageToVideo);
        // Text-to-video, restyling, interpolation and upscaling each need
        // model-specific loaders (and, for most, nodes a stock install does
        // not have). They are reachable through the "workflow" option.
        case VideoGenMode::TextToVideo:
        case VideoGenMode::VideoToVideo:
        case VideoGenMode::FrameInterpolation:
        case VideoGenMode::Upscale:
        default:
            return json::object();
    }
}

std::string FindNodeId(const json& graph, const std::string& title,
                       const std::string& classType) {
    if (!graph.is_object()) return {};

    if (!title.empty()) {
        for (const auto& [id, node] : graph.items()) {
            if (!node.is_object() || !node.contains("_meta")) continue;
            const json& meta = node["_meta"];
            if (meta.is_object() && meta.value("title", "") == title) return id;
        }
    }
    if (!classType.empty()) {
        for (const auto& [id, node] : graph.items()) {
            if (!node.is_object()) continue;
            if (node.value("class_type", "") == classType) return id;
        }
    }
    return {};
}

bool SetNodeInput(json& graph, const std::string& nodeId,
                  const std::string& field, json value) {
    if (nodeId.empty() || !graph.is_object() || !graph.contains(nodeId)) {
        return false;
    }
    json& node = graph[nodeId];
    if (!node.is_object()) return false;
    if (!node.contains("inputs") || !node["inputs"].is_object()) {
        node["inputs"] = json::object();
    }
    node["inputs"][field] = std::move(value);
    return true;
}

void SetByTitle(json& graph, const std::string& title,
                const std::string& classType, const std::string& field,
                json value) {
    const std::string id = FindNodeId(graph, title, classType);
    if (id.empty()) return;
    SetNodeInput(graph, id, field, std::move(value));
}

} // namespace comfyui_detail
} // namespace UltraAI
