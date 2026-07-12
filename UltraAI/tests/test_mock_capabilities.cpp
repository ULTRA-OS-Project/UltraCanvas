// UltraAI/tests/test_mock_capabilities.cpp
// Exercises every non-TextLLM mock adapter through its public interface,
// including the registry-based factories and the RawProvider escape hatch.

#include "UltraAI.h"
#include "UltraAIMockEmbeddings.h"
#include "UltraAIMockSpeechToText.h"
#include "UltraAIMockTextToSpeech.h"
#include "UltraAIMockImageGen.h"
#include "UltraAIMockVisionAnalyzer.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

using namespace UltraAI;

namespace {

#define EXPECT_TRUE(cond) do { \
    if (!(cond)) { std::cerr << "FAIL: " #cond " at " << __FILE__ << ":" \
                  << __LINE__ << std::endl; std::abort(); } \
} while (0)

#define EXPECT_EQ(a, b) do { \
    if (!((a) == (b))) { std::cerr << "FAIL: " #a " == " #b " at " \
                  << __FILE__ << ":" << __LINE__ << std::endl; std::abort(); } \
} while (0)

// ============================================================
// Embeddings
// ============================================================

void TestEmbeddings() {
    EmbeddingsConfig cfg; cfg.providerId = "mock";
    auto emb = CreateEmbeddings(cfg);
    EXPECT_TRUE(emb != nullptr);

    EmbeddingRequest req;
    req.input = { "alpha", "alpha", "beta" };

    auto resp = emb->Embed(req);
    EXPECT_EQ(resp.error.code, ErrorCode::None);
    EXPECT_EQ(resp.embeddings.size(), size_t{3});
    EXPECT_TRUE(!resp.embeddings[0].values.empty());

    // Determinism: same input -> same vector.
    EXPECT_TRUE(resp.embeddings[0].values == resp.embeddings[1].values);
    // Different inputs -> different vectors.
    EXPECT_TRUE(!(resp.embeddings[0].values == resp.embeddings[2].values));

    // Cosine sim of identical vectors is 1.0
    double sim = IEmbeddings::CosineSimilarity(resp.embeddings[0],
                                               resp.embeddings[1]);
    EXPECT_TRUE(std::abs(sim - 1.0) < 1e-6);

    // Variable dimensions
    req.dimensions = 16;
    auto resp16 = emb->Embed(req);
    EXPECT_EQ(resp16.embeddings[0].values.size(), size_t{16});

    // Async path
    auto fut = emb->EmbedAsync(req);
    EXPECT_EQ(fut.get().embeddings.size(), size_t{3});

    // Provider listing includes mock
    bool found = false;
    for (const auto& p : ListEmbeddingsProviders()) if (p == "mock") found = true;
    EXPECT_TRUE(found);
}

// ============================================================
// Speech-to-Text
// ============================================================

void TestSpeechToText() {
    SpeechToTextConfig cfg; cfg.providerId = "mock";
    auto stt = CreateSpeechToText(cfg);
    EXPECT_TRUE(stt != nullptr);

    TranscribeRequest req;
    req.audio.bytes = std::vector<uint8_t>(1000, 0x42);
    req.audio.mimeType = "audio/wav";
    req.language = "en";

    auto resp = stt->Transcribe(req);
    EXPECT_EQ(resp.error.code, ErrorCode::None);
    EXPECT_TRUE(!resp.text.empty());
    EXPECT_TRUE(!resp.segments.empty());
    EXPECT_EQ(resp.detectedLanguage, std::string("en"));

    // Scripted response via RawProvider
    auto* mock = static_cast<MockSpeechToText*>(stt->RawProvider());
    EXPECT_TRUE(mock != nullptr);
    mock->EnqueueTranscript("scripted speech");
    EXPECT_EQ(stt->Transcribe(req).text, std::string("scripted speech"));

    // Live transcription
    mock->SetLiveChunkInterval(std::chrono::milliseconds(0));
    mock->EnqueueTranscript("hello live world");

    std::atomic<int> partials{0}, finals{0};
    std::atomic<bool> done{false};
    auto live = stt->StartLiveTranscribe(req,
        [&](const StreamingTranscriptEvent& ev) {
            if (ev.kind == StreamingTranscriptKind::PartialText) ++partials;
            else if (ev.kind == StreamingTranscriptKind::FinalText) ++finals;
            else if (ev.kind == StreamingTranscriptKind::Done) done = true;
        });
    uint8_t buf[4] = { 1, 2, 3, 4 };
    live->PushAudio(buf, sizeof(buf));
    live->Finish();
    while (!live->IsDone()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_TRUE(partials.load() >= 1);
    EXPECT_TRUE(finals.load()   >= 1);
    EXPECT_TRUE(done.load());
}

// ============================================================
// Text-to-Speech
// ============================================================

void TestTextToSpeech() {
    TextToSpeechConfig cfg; cfg.providerId = "mock";
    auto tts = CreateTextToSpeech(cfg);
    EXPECT_TRUE(tts != nullptr);

    auto voices = tts->ListVoices();
    EXPECT_TRUE(!voices.empty());

    auto en = tts->ListVoices("en");
    for (const auto& v : en) EXPECT_EQ(v.language, std::string("en"));

    SpeakRequest req;
    req.text = "Hello world";
    req.voiceId = "mock-aria";
    req.format = TtsAudioFormat::Wav;

    auto resp = tts->Speak(req);
    EXPECT_EQ(resp.error.code, ErrorCode::None);
    EXPECT_EQ(resp.audio.bytes.size(), req.text.size());
    EXPECT_EQ(resp.audio.mimeType, std::string("audio/wav"));

    // Streaming
    auto* mock = static_cast<MockTextToSpeech*>(tts->RawProvider());
    mock->SetStreamChunkSize(4);
    mock->SetStreamChunkInterval(std::chrono::milliseconds(0));

    std::atomic<int> chunks{0};
    std::atomic<bool> sdone{false};
    size_t totalBytes = 0;
    auto handle = tts->SpeakStream(req,
        [&](const TtsStreamEvent& ev) {
            if (ev.kind == TtsStreamEventKind::AudioChunk) {
                ++chunks;
                totalBytes += ev.audioChunk.size();
            } else if (ev.kind == TtsStreamEventKind::Done) {
                sdone = true;
            }
        });
    while (!handle->IsDone()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_TRUE(sdone.load());
    EXPECT_TRUE(chunks.load() >= 3);              // 11 bytes / 4 = 3 chunks
    EXPECT_EQ(totalBytes, req.text.size());

    // Voice cloning
    CloneVoiceRequest cv;
    cv.displayName = "MyVoice";
    auto cloned = tts->CloneVoice(cv);
    EXPECT_EQ(cloned.voice.displayName, std::string("MyVoice"));
    EXPECT_TRUE(cloned.voice.isCloned);
}

// ============================================================
// Image generation
// ============================================================

void TestImageGen() {
    ImageGenConfig cfg; cfg.providerId = "mock";
    auto img = CreateImageGen(cfg);
    EXPECT_TRUE(img != nullptr);

    ImageGenRequest req;
    req.prompt = "a cat in a hat";
    req.width = 256;
    req.height = 256;
    req.count = 3;

    auto resp = img->Generate(req);
    EXPECT_EQ(resp.error.code, ErrorCode::None);
    EXPECT_EQ(resp.images.size(), size_t{3});
    for (const auto& g : resp.images) {
        EXPECT_TRUE(g.image.bytes.size() > 8);                  // PNG header + meta
        EXPECT_EQ(g.image.bytes[0], uint8_t{0x89});
        EXPECT_EQ(g.image.mimeType, std::string("image/png"));
    }

    // Async
    auto fut = img->GenerateAsync(req);
    EXPECT_EQ(fut.get().images.size(), size_t{3});

    // Job-style progress
    auto* mock = static_cast<MockImageGen*>(img->RawProvider());
    mock->SetJobSteps(3);
    mock->SetJobStepInterval(std::chrono::milliseconds(0));

    std::atomic<int> queued{0}, progress{0};
    std::atomic<bool> completed{false};
    auto handle = img->GenerateJob(req, [&](const ImageJobEvent& ev) {
        switch (ev.kind) {
            case ImageJobEventKind::Queued:     ++queued;    break;
            case ImageJobEventKind::InProgress: ++progress;  break;
            case ImageJobEventKind::Completed:  completed = true; break;
            case ImageJobEventKind::Error:      std::abort();
            default: break;
        }
    });
    while (!handle->IsDone()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_EQ(queued.load(), 1);
    EXPECT_EQ(progress.load(), 3);
    EXPECT_TRUE(completed.load());
}

// ============================================================
// Vision analysis
// ============================================================

void TestVisionAnalyzer() {
    VisionAnalyzerConfig cfg; cfg.providerId = "mock";
    auto vis = CreateVisionAnalyzer(cfg);
    EXPECT_TRUE(vis != nullptr);

    VisionAnalyzeRequest req;
    req.image.bytes = { 0x89, 0x50, 0x4E, 0x47 };
    req.image.mimeType = "image/png";
    req.tasks = {
        VisionTask::Caption, VisionTask::Tags, VisionTask::ObjectDetection,
        VisionTask::Ocr, VisionTask::DocumentLayout, VisionTask::FaceDetection,
        VisionTask::ContentSafety, VisionTask::VisualQuestion
    };
    req.prompt = "What is in the image?";

    auto resp = vis->Analyze(req);
    EXPECT_EQ(resp.error.code, ErrorCode::None);
    EXPECT_TRUE(!resp.caption.empty());
    EXPECT_TRUE(!resp.tags.empty());
    EXPECT_TRUE(!resp.objects.empty());
    EXPECT_TRUE(!resp.ocrText.empty());
    EXPECT_TRUE(!resp.ocrSpans.empty());
    EXPECT_TRUE(!resp.documentRegions.empty());
    EXPECT_TRUE(!resp.faces.empty());
    EXPECT_TRUE(!resp.safety.empty());
    EXPECT_TRUE(!resp.vqaAnswer.empty());

    // Subset of tasks -> empty fields elsewhere
    VisionAnalyzeRequest captionOnly;
    captionOnly.image = req.image;
    captionOnly.tasks = { VisionTask::Caption };
    auto resp2 = vis->Analyze(captionOnly);
    EXPECT_TRUE(!resp2.caption.empty());
    EXPECT_TRUE(resp2.tags.empty());
    EXPECT_TRUE(resp2.objects.empty());

    // Async
    auto fut = vis->AnalyzeAsync(req);
    EXPECT_TRUE(!fut.get().caption.empty());
}

// ============================================================
// Error path applied to one capability for sanity
// ============================================================

void TestErrorPath() {
    auto img = CreateMockImageGen();
    auto* mock = static_cast<MockImageGen*>(img->RawProvider());
    Error e; e.code = ErrorCode::QuotaExceeded; e.message = "stop";
    mock->EnqueueError(e);

    ImageGenRequest req;
    req.prompt = "anything";
    auto resp = img->Generate(req);
    EXPECT_EQ(resp.error.code, ErrorCode::QuotaExceeded);
    EXPECT_TRUE(resp.images.empty());
}

void TestUnknownProviderRoutes() {
    Error err;

    EmbeddingsConfig ec; ec.providerId = "nope";
    EXPECT_TRUE(CreateEmbeddings(ec, &err) == nullptr);

    SpeechToTextConfig sc; sc.providerId = "nope";
    EXPECT_TRUE(CreateSpeechToText(sc, &err) == nullptr);

    TextToSpeechConfig tc; tc.providerId = "nope";
    EXPECT_TRUE(CreateTextToSpeech(tc, &err) == nullptr);

    ImageGenConfig ic; ic.providerId = "nope";
    EXPECT_TRUE(CreateImageGen(ic, &err) == nullptr);

    VisionAnalyzerConfig vc; vc.providerId = "nope";
    EXPECT_TRUE(CreateVisionAnalyzer(vc, &err) == nullptr);
}

} // namespace

int main() {
    TestEmbeddings();
    TestSpeechToText();
    TestTextToSpeech();
    TestImageGen();
    TestVisionAnalyzer();
    TestErrorPath();
    TestUnknownProviderRoutes();
    std::cout << "OK: ultraai_test_mock_capabilities" << std::endl;
    return 0;
}
