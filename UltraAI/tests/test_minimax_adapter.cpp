// UltraAI/tests/test_minimax_adapter.cpp
// Exercises the MiniMax video + image adapters offline through
// ScriptedTransport: the submit/poll/retrieve video sequence and its
// request serialization, the base_resp envelope MiniMax returns inside
// HTTP 200 responses, mode rejection, the url-only escape hatch, image
// generation in both base64 and url form, and factory registration.
//
// Uses plain asserts so the test suite has no third-party dependency
// beyond the repo-vendored nlohmann/json (used to inspect recorded
// request bodies).

#include "UltraAIMiniMax.h"
#include "UltraAI.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
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

TransportResponse JsonResponse(int status, const json& body) {
    TransportResponse r;
    r.statusCode = status;
    r.headers    = {{"content-type", "application/json"}};
    r.body       = body.dump();
    return r;
}

json Ok() { return json{{"status_code", 0}, {"status_msg", "success"}}; }

std::string FindHeader(const TransportRequest& req, const std::string& name) {
    for (const auto& kv : req.headers) {
        if (kv.first == name) return kv.second;
    }
    return {};
}

VideoGenConfig VideoConfig() {
    VideoGenConfig cfg;
    cfg.providerId = "minimax";
    cfg.apiKey     = "mm-test-key";
    return cfg;
}

// Polling defaults are measured in seconds; tests must not wait for them.
void MakeFast(OptionsMap& options) {
    options["poll_interval_ms"] = int64_t(1);
    options["job_timeout_ms"]   = int64_t(5000);
}

void TestVideoSubmitPollRetrieve() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(200, json{{"task_id", "task-1"},
                                                     {"base_resp", Ok()}}));
    transport->ScriptResponse(JsonResponse(200, json{{"task_id", "task-1"},
                                                     {"status", "Processing"},
                                                     {"base_resp", Ok()}}));
    transport->ScriptResponse(JsonResponse(200, json{{"task_id", "task-1"},
                                                     {"status", "Success"},
                                                     {"file_id", "file-9"},
                                                     {"base_resp", Ok()}}));
    transport->ScriptResponse(JsonResponse(200, json{
        {"file", {{"file_id", "file-9"},
                  {"filename", "hailuo.mp4"},
                  {"download_url", "https://cdn.example/hailuo.mp4"}}},
        {"base_resp", Ok()}}));
    TransportResponse video;
    video.statusCode = 200;
    video.body       = "MP4BYTES";
    transport->ScriptResponse(video);

    auto gen = CreateMiniMaxVideoGen(VideoConfig(), nullptr, transport);
    EXPECT_TRUE(gen != nullptr);

    VideoGenRequest req;
    req.prompt      = "a kettle boiling";
    req.durationSec = 6.0;
    req.width       = 1920;
    req.height      = 1080;
    MakeFast(req.options);

    VideoGenResponse resp = gen->Generate(req);
    EXPECT_TRUE(resp.error.IsOk());
    EXPECT_EQ(resp.videos.size(), size_t(1));
    EXPECT_EQ(resp.videos[0].video.url,
              std::string("https://cdn.example/hailuo.mp4"));
    EXPECT_EQ(std::string(resp.videos[0].video.bytes.begin(),
                          resp.videos[0].video.bytes.end()),
              std::string("MP4BYTES"));
    EXPECT_EQ(resp.videos[0].durationSec, 6.0);
    EXPECT_EQ(resp.usage.units, 1);

    const auto& requests = transport->Requests();
    EXPECT_EQ(requests.size(), size_t(5));

    // Submit: model default, prompt, duration, mapped resolution, bearer.
    EXPECT_EQ(requests[0].url,
              std::string("https://api.minimax.io/v1/video_generation"));
    EXPECT_EQ(FindHeader(requests[0], "authorization"),
              std::string("Bearer mm-test-key"));
    const json body = json::parse(requests[0].body);
    EXPECT_EQ(body["model"], "MiniMax-Hailuo-02");
    EXPECT_EQ(body["prompt"], "a kettle boiling");
    EXPECT_EQ(body["duration"], 6);
    EXPECT_EQ(body["resolution"], "1080P");
    EXPECT_TRUE(!body.contains("first_frame_image"));
    // Control options stay out of the provider request.
    EXPECT_TRUE(!body.contains("poll_interval_ms"));
    EXPECT_TRUE(!body.contains("job_timeout_ms"));

    EXPECT_EQ(requests[1].url,
              std::string("https://api.minimax.io/v1/query/"
                          "video_generation?task_id=task-1"));
    EXPECT_EQ(requests[3].url,
              std::string("https://api.minimax.io/v1/files/"
                          "retrieve?file_id=file-9"));
    // The API key must not travel to the storage host.
    EXPECT_EQ(requests[4].url, std::string("https://cdn.example/hailuo.mp4"));
    EXPECT_EQ(FindHeader(requests[4], "authorization"), std::string());
}

void TestVideoImageToVideoAndUrlOnly() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(200, json{{"task_id", "t"},
                                                     {"base_resp", Ok()}}));
    transport->ScriptResponse(JsonResponse(200, json{{"status", "Success"},
                                                     {"file_id", "f"},
                                                     {"base_resp", Ok()}}));
    transport->ScriptResponse(JsonResponse(200, json{
        {"file", {{"download_url", "https://cdn.example/v.mp4"}}},
        {"base_resp", Ok()}}));

    auto gen = CreateMiniMaxVideoGen(VideoConfig(), nullptr, transport);

    VideoGenRequest req;
    req.mode   = VideoGenMode::ImageToVideo;
    req.prompt = "pan across the scene";
    MediaBlob source;
    source.bytes    = {0x89, 0x50, 0x4E, 0x47};
    source.mimeType = "image/png";
    req.sourceImage = source;
    MakeFast(req.options);
    req.options["return_url_only"] = true;

    VideoGenResponse resp = gen->Generate(req);
    EXPECT_TRUE(resp.error.IsOk());
    EXPECT_EQ(resp.videos.size(), size_t(1));
    // No download exchange was scripted, so url-only must have skipped it.
    EXPECT_EQ(transport->Requests().size(), size_t(3));
    EXPECT_TRUE(resp.videos[0].video.bytes.empty());
    EXPECT_EQ(resp.videos[0].video.url, std::string("https://cdn.example/v.mp4"));

    const json body = json::parse(transport->Requests()[0].body);
    EXPECT_TRUE(body["first_frame_image"].get<std::string>().rfind(
        "data:image/png;base64,", 0) == 0);
    EXPECT_TRUE(!body.contains("return_url_only"));
}

void TestVideoBaseRespErrorInsideHttp200() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(200, json{
        {"base_resp", {{"status_code", 1002},
                       {"status_msg", "rate limit reached"}}}}));

    auto gen = CreateMiniMaxVideoGen(VideoConfig(), nullptr, transport);
    VideoGenRequest req;
    req.prompt = "x";
    MakeFast(req.options);

    VideoGenResponse resp = gen->Generate(req);
    EXPECT_EQ(resp.error.code, ErrorCode::RateLimited);
    EXPECT_EQ(resp.error.providerCode, std::string("1002"));
    EXPECT_EQ(resp.error.message, std::string("rate limit reached"));
}

void TestVideoJobFailureAndRejectedModes() {
    // A "Fail" status ends the poll loop instead of running to the deadline.
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(200, json{{"task_id", "t"},
                                                     {"base_resp", Ok()}}));
    transport->ScriptResponse(JsonResponse(200, json{{"status", "Fail"},
                                                     {"base_resp", Ok()}}));
    auto gen = CreateMiniMaxVideoGen(VideoConfig(), nullptr, transport);

    VideoGenRequest req;
    req.prompt = "x";
    MakeFast(req.options);
    VideoGenResponse resp = gen->Generate(req);
    EXPECT_EQ(resp.error.code, ErrorCode::ProviderError);

    // Modes MiniMax has no endpoint for are refused before any traffic.
    auto quiet = std::make_shared<ScriptedTransport>();
    auto gen2  = CreateMiniMaxVideoGen(VideoConfig(), nullptr, quiet);
    VideoGenRequest upscale;
    upscale.mode   = VideoGenMode::Upscale;
    upscale.prompt = "x";
    MakeFast(upscale.options);
    VideoGenResponse resp2 = gen2->Generate(upscale);
    EXPECT_EQ(resp2.error.code, ErrorCode::UnsupportedFormat);
    EXPECT_EQ(quiet->Requests().size(), size_t(0));

    // ImageToVideo without a source image is a request error, not a 400.
    VideoGenRequest noSource;
    noSource.mode   = VideoGenMode::ImageToVideo;
    noSource.prompt = "x";
    MakeFast(noSource.options);
    EXPECT_EQ(gen2->Generate(noSource).error.code, ErrorCode::InvalidRequest);
}

void TestVideoMissingCredentials() {
    auto transport = std::make_shared<ScriptedTransport>();
    VideoGenConfig cfg;
    cfg.providerId = "minimax";          // no apiKey, no vault reference
    auto gen = CreateMiniMaxVideoGen(cfg, nullptr, transport);

    VideoGenRequest req;
    req.prompt = "x";
    MakeFast(req.options);
    EXPECT_EQ(gen->Generate(req).error.code, ErrorCode::AuthenticationFailed);
    EXPECT_EQ(transport->Requests().size(), size_t(0));
}

void TestVideoJobEvents() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(200, json{{"task_id", "t"},
                                                     {"base_resp", Ok()}}));
    transport->ScriptResponse(JsonResponse(200, json{{"status", "Processing"},
                                                     {"base_resp", Ok()}}));
    transport->ScriptResponse(JsonResponse(200, json{{"status", "Success"},
                                                     {"file_id", "f"},
                                                     {"base_resp", Ok()}}));
    transport->ScriptResponse(JsonResponse(200, json{
        {"file", {{"download_url", "https://cdn.example/v.mp4"}}},
        {"base_resp", Ok()}}));

    auto gen = CreateMiniMaxVideoGen(VideoConfig(), nullptr, transport);
    VideoGenRequest req;
    req.prompt = "x";
    MakeFast(req.options);
    req.options["return_url_only"] = true;

    std::vector<VideoJobEventKind> kinds;
    std::mutex mu;
    StreamHandle handle = gen->GenerateJob(req, [&](const VideoJobEvent& event) {
        std::lock_guard<std::mutex> lock(mu);
        kinds.push_back(event.kind);
    });
    EXPECT_TRUE(handle != nullptr);
    while (!handle->IsDone()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    std::lock_guard<std::mutex> lock(mu);
    EXPECT_TRUE(!kinds.empty());
    EXPECT_EQ(kinds.front(), VideoJobEventKind::Queued);
    EXPECT_EQ(kinds.back(), VideoJobEventKind::Completed);
    EXPECT_TRUE(std::find(kinds.begin(), kinds.end(),
                          VideoJobEventKind::InProgress) != kinds.end());
}

void TestImageGeneration() {
    auto transport = std::make_shared<ScriptedTransport>();
    // "Zm9v" is "foo".
    transport->ScriptResponse(JsonResponse(200, json{
        {"id", "img-1"},
        {"data", {{"image_base64", json::array({"Zm9v"})}}},
        {"base_resp", Ok()}}));

    ImageGenConfig cfg;
    cfg.providerId = "minimax";
    cfg.apiKey     = "mm-test-key";
    auto gen = CreateMiniMaxImageGen(cfg, nullptr, transport);
    EXPECT_TRUE(gen != nullptr);

    ImageGenRequest req;
    req.prompt = "a kettle";
    req.count  = 2;
    req.width  = 1024;
    req.height = 1024;

    ImageGenResponse resp = gen->Generate(req);
    EXPECT_TRUE(resp.error.IsOk());
    EXPECT_EQ(resp.images.size(), size_t(1));
    EXPECT_EQ(std::string(resp.images[0].image.bytes.begin(),
                          resp.images[0].image.bytes.end()),
              std::string("foo"));
    EXPECT_EQ(resp.usage.units, 1);

    const json body = json::parse(transport->Requests()[0].body);
    EXPECT_EQ(body["model"], "image-01");
    EXPECT_EQ(body["n"], 2);
    EXPECT_EQ(body["width"], 1024);
    EXPECT_EQ(body["response_format"], "base64");
}

void TestImageUrlFormatAndRejections() {
    auto transport = std::make_shared<ScriptedTransport>();
    transport->ScriptResponse(JsonResponse(200, json{
        {"data", {{"image_urls", json::array({"https://cdn.example/a.png"})}}},
        {"base_resp", Ok()}}));

    ImageGenConfig cfg;
    cfg.apiKey = "mm-test-key";
    auto gen = CreateMiniMaxImageGen(cfg, nullptr, transport);

    ImageGenRequest req;
    req.prompt      = "a kettle";
    req.returnAsUrl = true;
    ImageGenResponse resp = gen->Generate(req);
    EXPECT_TRUE(resp.error.IsOk());
    EXPECT_EQ(resp.images[0].image.url, std::string("https://cdn.example/a.png"));
    EXPECT_EQ(json::parse(transport->Requests()[0].body)["response_format"],
              "url");

    // Modes and sizes the API cannot serve fail before any traffic.
    auto quiet = std::make_shared<ScriptedTransport>();
    auto gen2  = CreateMiniMaxImageGen(cfg, nullptr, quiet);
    ImageGenRequest inpaint;
    inpaint.prompt = "x";
    inpaint.mode   = ImageGenMode::Inpaint;
    EXPECT_EQ(gen2->Generate(inpaint).error.code, ErrorCode::UnsupportedFormat);

    ImageGenRequest tiny;
    tiny.prompt = "x";
    tiny.width  = 64;
    tiny.height = 64;
    EXPECT_EQ(gen2->Generate(tiny).error.code, ErrorCode::InvalidRequest);
    EXPECT_EQ(quiet->Requests().size(), size_t(0));
}

void TestFactoryRegistration() {
    const std::vector<std::string> videoProviders = ListVideoGenProviders();
    EXPECT_TRUE(std::find(videoProviders.begin(), videoProviders.end(),
                          "minimax") != videoProviders.end());

    const std::vector<std::string> imageProviders = ListImageGenProviders();
    EXPECT_TRUE(std::find(imageProviders.begin(), imageProviders.end(),
                          "minimax") != imageProviders.end());

    // Capability reporting does not need a server.
    auto transport = std::make_shared<ScriptedTransport>();
    auto gen = CreateMiniMaxVideoGen(VideoConfig(), nullptr, transport);
    VideoGenProviderCapabilities caps = gen->GetCapabilities();
    EXPECT_EQ(caps.providerId, std::string("minimax"));
    EXPECT_TRUE(!caps.models.empty());
    EXPECT_TRUE(!caps.models[0].runsLocally);
}

} // namespace

int main() {
    TestVideoSubmitPollRetrieve();
    TestVideoImageToVideoAndUrlOnly();
    TestVideoBaseRespErrorInsideHttp200();
    TestVideoJobFailureAndRejectedModes();
    TestVideoMissingCredentials();
    TestVideoJobEvents();
    TestImageGeneration();
    TestImageUrlFormatAndRejections();
    TestFactoryRegistration();
    std::cout << "test_minimax_adapter: all checks passed" << std::endl;
    return 0;
}
