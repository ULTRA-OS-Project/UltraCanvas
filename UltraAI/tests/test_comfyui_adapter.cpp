// UltraAI/tests/test_comfyui_adapter.cpp
// Exercises the ComfyUI adapter offline through ScriptedTransport: the
// built-in workflow templates and how request fields bind into them, the
// upload -> submit -> history -> view sequence, the WebSocket progress path
// with preview frames, node_errors mapping, caller-supplied workflows and
// node overrides, capability discovery from /object_info, and the modes the
// adapter refuses rather than mis-generating.
//
// Uses plain asserts so the test suite has no third-party dependency
// beyond the repo-vendored nlohmann/json (used to inspect recorded
// request bodies).

#include "UltraAIComfyUI.h"
#include "UltraAI.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace UltraAI;
using nlohmann::json;

namespace {

#define EXPECT_TRUE(cond) do { \
    if (!(cond)) { std::cerr << "FAIL: " #cond " at " << __FILE__ << ":" \
                  << __LINE__ << std::endl; std::abort(); } \
} while (0)

#define EXPECT_EQ(a, b) do { \
    if (!((a) == (b))) { std::cerr << "FAIL: " #a " == " #b " at " \
                  << __FILE__ << ":" << __LINE__ << std::endl; std::abort(); } \
} while (0)

// A transport-level failure, as ScriptedTransport::ScriptError takes it.
Error NetworkError(const std::string& message) {
    Error error;
    error.code    = ErrorCode::NetworkError;
    error.message = message;
    return error;
}

TransportResponse JsonResponse(int status, const json& body) {
    TransportResponse r;
    r.statusCode = status;
    r.headers    = {{"content-type", "application/json"}};
    r.body       = body.dump();
    return r;
}

TransportResponse ImageResponse(const std::string& bytes) {
    TransportResponse r;
    r.statusCode = 200;
    r.headers    = {{"content-type", "image/png"}};
    r.body       = bytes;
    return r;
}

json HistoryFinished(const std::string& promptId, const std::string& filename,
                     const std::string& subfolder = "") {
    return json{{promptId, {
        {"status", {{"completed", true}, {"status_str", "success"}}},
        {"outputs", {{"9", {{"images", json::array({json{
            {"filename", filename},
            {"subfolder", subfolder},
            {"type", "output"}}})}}}}}}}};
}

ImageGenConfig Config() {
    ImageGenConfig cfg;
    cfg.providerId   = "comfyui";
    cfg.defaultModel = "sd_xl_base_1.0.safetensors";
    return cfg;
}

// The poll fallback must not wait a second per iteration in tests.
void MakeFast(OptionsMap& options) {
    options["poll_interval_ms"] = int64_t(1);
    options["job_timeout_ms"]   = int64_t(5000);
}

void TestTextToImageThroughPolling() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(200, json{{"prompt_id", "p1"},
                                                     {"number", 1}}));
    transport->ScriptResponse(JsonResponse(200, json::object()));  // queued
    transport->ScriptResponse(JsonResponse(200,
        HistoryFinished("p1", "UltraAI_00001_.png", "batch 1")));
    transport->ScriptResponse(ImageResponse("PNGBYTES"));

    auto gen = CreateComfyUIImageGen(Config(), nullptr, transport);
    EXPECT_TRUE(gen != nullptr);

    ImageGenRequest req;
    req.prompt         = "a kettle on a stove";
    req.negativePrompt = "blurry";
    req.width          = 768;
    req.height         = 512;
    req.count          = 2;
    req.steps          = 30;
    req.guidanceScale  = 6.5;
    req.scheduler      = "karras";
    MakeFast(req.options);
    req.options["sampler_name"] = std::string("dpmpp_2m");

    ImageGenResponse resp = gen->Generate(req);
    EXPECT_TRUE(resp.error.IsOk());
    EXPECT_EQ(resp.images.size(), size_t(1));
    EXPECT_EQ(std::string(resp.images[0].image.bytes.begin(),
                          resp.images[0].image.bytes.end()),
              std::string("PNGBYTES"));
    EXPECT_EQ(resp.images[0].image.mimeType, std::string("image/png"));
    EXPECT_EQ(resp.usage.units, 1);

    const auto& requests = transport->Requests();
    EXPECT_EQ(requests.size(), size_t(4));
    EXPECT_EQ(requests[0].url, std::string("http://127.0.0.1:8188/prompt"));

    const json submitted = json::parse(requests[0].body);
    EXPECT_TRUE(submitted.contains("client_id"));
    const json& graph = submitted["prompt"];
    EXPECT_EQ(graph["6"]["inputs"]["text"], "a kettle on a stove");
    EXPECT_EQ(graph["7"]["inputs"]["text"], "blurry");
    EXPECT_EQ(graph["4"]["inputs"]["ckpt_name"], "sd_xl_base_1.0.safetensors");
    EXPECT_EQ(graph["5"]["inputs"]["width"], 768);
    EXPECT_EQ(graph["5"]["inputs"]["height"], 512);
    EXPECT_EQ(graph["5"]["inputs"]["batch_size"], 2);
    EXPECT_EQ(graph["3"]["inputs"]["steps"], 30);
    EXPECT_EQ(graph["3"]["inputs"]["cfg"], 6.5);
    EXPECT_EQ(graph["3"]["inputs"]["scheduler"], "karras");
    EXPECT_EQ(graph["3"]["inputs"]["sampler_name"], "dpmpp_2m");
    // An unset seed must not become a constant: ComfyUI caches per
    // (graph, seed) and would return the previous image.
    EXPECT_TRUE(graph["3"]["inputs"]["seed"].get<int64_t>() != 0);

    // The subfolder and filename come back from the server, so they are
    // percent-encoded rather than concatenated raw.
    EXPECT_EQ(requests[3].url,
              std::string("http://127.0.0.1:8188/view?"
                          "filename=UltraAI_00001_.png&subfolder=batch%201"
                          "&type=output"));
}

void TestImageToImageUploadsSource() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(200, json{{"name", "src.png"},
                                                     {"subfolder", "clipspace"},
                                                     {"type", "input"}}));
    transport->ScriptResponse(JsonResponse(200, json{{"prompt_id", "p2"}}));
    transport->ScriptResponse(JsonResponse(200, HistoryFinished("p2", "out.png")));
    transport->ScriptResponse(ImageResponse("BYTES"));

    auto gen = CreateComfyUIImageGen(Config(), nullptr, transport);

    ImageGenRequest req;
    req.mode     = ImageGenMode::ImageToImage;
    req.prompt   = "make it winter";
    req.strength = 0.6;
    MediaBlob source;
    source.bytes    = {0x89, 0x50, 0x4E, 0x47};
    source.mimeType = "image/png";
    source.filename = "photo.png";
    req.sourceImage = source;
    MakeFast(req.options);

    ImageGenResponse resp = gen->Generate(req);
    EXPECT_TRUE(resp.error.IsOk());

    const auto& requests = transport->Requests();
    EXPECT_EQ(requests[0].url, std::string("http://127.0.0.1:8188/upload/image"));
    // Uploads land in ComfyUI's shared input directory, so the caller's own
    // name is prefixed rather than used as-is — "photo.png" must not be
    // able to replace a file the user put there.
    EXPECT_TRUE(requests[0].body.find("filename=\"photo.png\"") ==
                std::string::npos);
    EXPECT_TRUE(requests[0].body.find("filename=\"ultraai-") != std::string::npos);
    EXPECT_TRUE(requests[0].body.find("photo.png\"") != std::string::npos);
    EXPECT_TRUE(requests[0].body.find("name=\"overwrite\"") == std::string::npos);

    const json graph = json::parse(requests[1].body)["prompt"];
    // The upload's subfolder has to travel with the name or LoadImage will
    // not find the file.
    EXPECT_EQ(graph["10"]["inputs"]["image"], "clipspace/src.png");
    EXPECT_EQ(graph["3"]["inputs"]["denoise"], 0.6);
}

void TestWebSocketProgressAndPreviews() {
    auto transport = std::make_shared<ScriptedTransport>();

    std::vector<TransportWsMessage> frames;
    auto text = [&frames](const json& body) {
        TransportWsMessage message;
        message.text = body.dump();
        frames.push_back(std::move(message));
    };
    text(json{{"type", "status"}, {"data", {{"status", json::object()}}}});
    text(json{{"type", "progress"},
              {"data", {{"value", 5}, {"max", 20}, {"prompt_id", "p3"}}}});
    // A frame for somebody else's prompt must not be counted as ours.
    text(json{{"type", "progress"},
              {"data", {{"value", 19}, {"max", 20}, {"prompt_id", "other"}}}});

    TransportWsMessage preview;
    preview.binary = true;
    preview.bytes  = {0, 0, 0, 1, 0, 0, 0, 2, 'P', 'N', 'G'};
    frames.push_back(preview);

    text(json{{"type", "executed"},
              {"data", {{"prompt_id", "p3"},
                        {"output", {{"9", {{"images", json::array({json{
                            {"filename", "ws.png"},
                            {"subfolder", ""},
                            {"type", "output"}}})}}}}}}}});
    text(json{{"type", "executing"},
              {"data", {{"node", nullptr}, {"prompt_id", "p3"}}}});

    transport->ScriptWebSocket(frames);
    transport->ScriptResponse(JsonResponse(200, json{{"prompt_id", "p3"}}));
    transport->ScriptResponse(ImageResponse("WSBYTES"));

    auto gen = CreateComfyUIImageGen(Config(), nullptr, transport);

    ImageGenRequest req;
    req.prompt = "a kettle";
    MakeFast(req.options);

    std::mutex mu;
    std::vector<ImageJobEventKind> kinds;
    std::vector<double> progress;
    ImageGenResponse completed;
    StreamHandle handle = gen->GenerateJob(req, [&](const ImageJobEvent& event) {
        std::lock_guard<std::mutex> lock(mu);
        kinds.push_back(event.kind);
        if (event.kind == ImageJobEventKind::InProgress) {
            progress.push_back(event.progress);
        }
        if (event.kind == ImageJobEventKind::PreviewImage && event.preview) {
            EXPECT_EQ(event.preview->mimeType, std::string("image/png"));
            EXPECT_EQ(std::string(event.preview->bytes.begin(),
                                  event.preview->bytes.end()),
                      std::string("PNG"));
        }
        if (event.kind == ImageJobEventKind::Completed && event.result) {
            completed = *event.result;
        }
    });
    while (!handle->IsDone()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    std::lock_guard<std::mutex> lock(mu);
    EXPECT_EQ(kinds.front(), ImageJobEventKind::Queued);
    EXPECT_EQ(kinds.back(), ImageJobEventKind::Completed);
    EXPECT_TRUE(std::find(kinds.begin(), kinds.end(),
                          ImageJobEventKind::PreviewImage) != kinds.end());
    EXPECT_EQ(progress.size(), size_t(1));
    EXPECT_EQ(progress[0], 0.25);
    EXPECT_EQ(completed.images.size(), size_t(1));
    EXPECT_EQ(std::string(completed.images[0].image.bytes.begin(),
                          completed.images[0].image.bytes.end()),
              std::string("WSBYTES"));

    // The socket is opened before the prompt is submitted, so frames that
    // arrive immediately are not lost.
    const std::string wsUrl = transport->Requests()[0].url;
    const std::string wsPrefix = "http://127.0.0.1:8188/ws?clientId=";
    EXPECT_TRUE(wsUrl.rfind(wsPrefix, 0) == 0);
    EXPECT_TRUE(wsUrl.size() > wsPrefix.size());
    EXPECT_EQ(transport->Requests()[1].url,
              std::string("http://127.0.0.1:8188/prompt"));
    // The submission carries the same client id the socket registered.
    EXPECT_EQ(json::parse(transport->Requests()[1].body)["client_id"],
              wsUrl.substr(wsPrefix.size()));
}

void TestNodeErrorsMapToModelNotFound() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(400, json{
        {"error", {{"type", "prompt_outputs_failed_validation"},
                   {"message", "Prompt outputs failed validation"}}},
        {"node_errors", {{"4", {
            {"class_type", "CheckpointLoaderSimple"},
            {"errors", json::array({json{
                {"type", "value_not_in_list"},
                {"message", "Value not in list"},
                {"details", "ckpt_name: 'missing.safetensors' not in []"}}})}}}}}}));

    auto gen = CreateComfyUIImageGen(Config(), nullptr, transport);
    ImageGenRequest req;
    req.prompt = "a kettle";
    MakeFast(req.options);

    ImageGenResponse resp = gen->Generate(req);
    EXPECT_EQ(resp.error.code, ErrorCode::ModelNotFound);
    EXPECT_TRUE(resp.error.message.find("CheckpointLoaderSimple") !=
                std::string::npos);
    EXPECT_TRUE(resp.error.message.find("missing.safetensors") !=
                std::string::npos);
}

void TestCustomWorkflowAndOverrides() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(200, json{{"prompt_id", "p4"}}));
    transport->ScriptResponse(JsonResponse(200, HistoryFinished("p4", "c.png")));

    auto gen = CreateComfyUIImageGen(Config(), nullptr, transport);

    const json workflow = {
        {"1", {{"class_type", "CLIPTextEncode"},
               {"_meta", {{"title", "UltraAI Positive"}}},
               {"inputs", {{"text", ""}}}}},
        {"2", {{"class_type", "SomeCustomSampler"},
               {"_meta", {{"title", "UltraAI Sampler"}}},
               {"inputs", {{"steps", 1}}}}}};

    ImageGenRequest req;
    req.prompt      = "bound by title";
    req.steps       = 12;
    req.returnAsUrl = true;
    MakeFast(req.options);
    req.options["workflow"] = workflow.dump();
    req.options["node_overrides"] =
        json{{"2", {{"custom_knob", 3}}}}.dump();

    ImageGenResponse resp = gen->Generate(req);
    EXPECT_TRUE(resp.error.IsOk());

    const json graph = json::parse(transport->Requests()[0].body)["prompt"];
    // Binding is by title, so a workflow whose nodes are not the built-in
    // classes still receives the request's fields.
    EXPECT_EQ(graph["1"]["inputs"]["text"], "bound by title");
    EXPECT_EQ(graph["2"]["inputs"]["steps"], 12);
    EXPECT_EQ(graph["2"]["inputs"]["custom_knob"], 3);
    EXPECT_TRUE(!graph.contains("9"));   // the built-in template is not used

    // returnAsUrl skips the download entirely.
    EXPECT_EQ(transport->Requests().size(), size_t(2));
    EXPECT_TRUE(resp.images[0].image.bytes.empty());
    EXPECT_TRUE(resp.images[0].image.url.find("/view?filename=c.png") !=
                std::string::npos);
}

void TestRejectedRequests() {
    auto transport = std::make_shared<ScriptedTransport>();
    auto gen = CreateComfyUIImageGen(Config(), nullptr, transport);

    // No built-in template, and no workflow supplied.
    ImageGenRequest removal;
    removal.prompt = "x";
    removal.mode   = ImageGenMode::BackgroundRemoval;
    MakeFast(removal.options);
    EXPECT_EQ(gen->Generate(removal).error.code, ErrorCode::UnsupportedFormat);

    // ControlNet guidance needs a workflow with ControlNet nodes.
    ImageGenRequest control;
    control.prompt = "x";
    MakeFast(control.options);
    ControlImage guide;
    guide.image.bytes = {1, 2, 3};
    control.controlImages.push_back(guide);
    EXPECT_EQ(gen->Generate(control).error.code, ErrorCode::UnsupportedFormat);

    // Inpaint without a mask.
    ImageGenRequest inpaint;
    inpaint.prompt = "x";
    inpaint.mode   = ImageGenMode::Inpaint;
    MediaBlob source;
    source.bytes = {1};
    inpaint.sourceImage = source;
    MakeFast(inpaint.options);
    EXPECT_EQ(gen->Generate(inpaint).error.code, ErrorCode::InvalidRequest);

    // A URL-only source cannot be uploaded; say so instead of failing later
    // inside ComfyUI's validation.
    ImageGenRequest remote;
    remote.prompt = "x";
    remote.mode   = ImageGenMode::ImageToImage;
    MediaBlob remoteBlob;
    remoteBlob.url = "https://example.com/a.png";
    remote.sourceImage = remoteBlob;
    MakeFast(remote.options);
    EXPECT_EQ(gen->Generate(remote).error.code, ErrorCode::InvalidRequest);

    EXPECT_EQ(transport->Requests().size(), size_t(0));
}

void TestCapabilitiesFromObjectInfo() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(200, json{
        {"CheckpointLoaderSimple", {
            {"input", {{"required", {{"ckpt_name", json::array({
                json::array({"sd_xl_base_1.0.safetensors", "flux1-dev.safetensors"}),
                json::object()})}}}}}}}}));

    auto gen = CreateComfyUIImageGen(Config(), nullptr, transport);
    ImageGenProviderCapabilities caps = gen->GetCapabilities();

    EXPECT_EQ(caps.providerId, std::string("comfyui"));
    EXPECT_EQ(caps.models.size(), size_t(2));
    EXPECT_EQ(caps.models[0].id, std::string("sd_xl_base_1.0.safetensors"));
    EXPECT_TRUE(caps.models[0].runsLocally);
    EXPECT_EQ(transport->Requests()[0].url,
              std::string("http://127.0.0.1:8188/object_info/"
                          "CheckpointLoaderSimple"));

    // Cached: a second call does not re-probe.
    gen->GetCapabilities();
    EXPECT_EQ(transport->Requests().size(), size_t(1));

    // With no server the model list is empty rather than an error.
    auto offline = std::make_shared<ScriptedTransport>();
    offline->ScriptError(NetworkError("refused"));
    auto gen2 = CreateComfyUIImageGen(Config(), nullptr, offline);
    EXPECT_TRUE(gen2->GetCapabilities().models.empty());
}

void TestFactoryRegistration() {
    const std::vector<std::string> providers = ListImageGenProviders();
    EXPECT_TRUE(std::find(providers.begin(), providers.end(), "comfyui") !=
                providers.end());
}

} // namespace

int main() {
    TestTextToImageThroughPolling();
    TestImageToImageUploadsSource();
    TestWebSocketProgressAndPreviews();
    TestNodeErrorsMapToModelNotFound();
    TestCustomWorkflowAndOverrides();
    TestRejectedRequests();
    TestCapabilitiesFromObjectInfo();
    TestFactoryRegistration();
    std::cout << "test_comfyui_adapter: all checks passed" << std::endl;
    return 0;
}
