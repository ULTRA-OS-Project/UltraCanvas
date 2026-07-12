// UltraAI/tests/test_mock_more_capabilities.cpp
// Exercises the four additional capabilities: Translator, VideoGen,
// MusicGen, CodeAssist — through their public interfaces, factories,
// and RawProvider escape hatches.

#include "UltraAI.h"
#include "UltraAIMockTranslator.h"
#include "UltraAIMockVideoGen.h"
#include "UltraAIMockMusicGen.h"
#include "UltraAIMockCodeAssist.h"

#include <atomic>
#include <cassert>
#include <chrono>
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
// Translator
// ============================================================

void TestTranslator() {
    TranslatorConfig cfg; cfg.providerId = "mock";
    auto tr = CreateTranslator(cfg);
    EXPECT_TRUE(tr != nullptr);

    TranslateRequest req;
    req.texts = { "hello world", "ich bin der schnelle fuchs und das ist gut" };
    req.targetLanguage = "fr";

    auto resp = tr->Translate(req);
    EXPECT_EQ(resp.error.code, ErrorCode::None);
    EXPECT_EQ(resp.results.size(), size_t{2});
    EXPECT_TRUE(resp.results[0].text.find("[fr]") == 0);
    EXPECT_EQ(resp.results[0].detectedSourceLanguage, std::string("en"));
    EXPECT_EQ(resp.results[1].detectedSourceLanguage, std::string("de"));

    // Async
    auto fut = tr->TranslateAsync(req);
    EXPECT_EQ(fut.get().results.size(), size_t{2});

    // Language detection
    DetectLanguageRequest dreq;
    dreq.texts = { "il est ici", "el coche es rojo" };
    dreq.topN = 2;
    auto dresp = tr->DetectLanguage(dreq);
    EXPECT_EQ(dresp.guesses.size(), size_t{2});
    EXPECT_EQ(dresp.guesses[0][0].language, std::string("fr"));
    EXPECT_EQ(dresp.guesses[1][0].language, std::string("es"));
    EXPECT_EQ(dresp.guesses[0].size(), size_t{2});  // topN=2

    // Scripted response via RawProvider
    auto* mock = static_cast<MockTranslator*>(tr->RawProvider());
    EXPECT_TRUE(mock != nullptr);
    TranslateResponse scripted;
    TranslateResult sr;
    sr.text = "Bonjour"; sr.detectedSourceLanguage = "en"; sr.confidence = 1.0;
    scripted.results.push_back(sr);
    mock->EnqueueResponse(std::move(scripted));
    auto resp2 = tr->Translate(req);
    EXPECT_EQ(resp2.results[0].text, std::string("Bonjour"));
}

// ============================================================
// Video generation
// ============================================================

void TestVideoGen() {
    VideoGenConfig cfg; cfg.providerId = "mock";
    auto vid = CreateVideoGen(cfg);
    EXPECT_TRUE(vid != nullptr);

    VideoGenRequest req;
    req.prompt = "ocean waves at sunset";
    req.width = 1024; req.height = 576;
    req.durationSec = 5.0;
    req.fps = 30;
    req.count = 2;

    auto resp = vid->Generate(req);
    EXPECT_EQ(resp.error.code, ErrorCode::None);
    EXPECT_EQ(resp.videos.size(), size_t{2});
    EXPECT_EQ(resp.videos[0].width, 1024);
    EXPECT_EQ(resp.videos[0].height, 576);
    EXPECT_EQ(resp.videos[0].fps, 30);
    EXPECT_TRUE(resp.videos[0].video.bytes.size() > 0);
    EXPECT_TRUE(resp.videos[0].thumbnail.bytes.size() > 0);
    EXPECT_EQ(resp.videos[0].video.mimeType, std::string("video/mp4"));

    // Async
    auto fut = vid->GenerateAsync(req);
    EXPECT_EQ(fut.get().videos.size(), size_t{2});

    // Job-style progress
    auto* mock = static_cast<MockVideoGen*>(vid->RawProvider());
    mock->SetJobSteps(3);
    mock->SetJobStepInterval(std::chrono::milliseconds(0));

    std::atomic<int> queued{0}, progress{0};
    std::atomic<bool> completed{false};
    auto handle = vid->GenerateJob(req, [&](const VideoJobEvent& ev) {
        switch (ev.kind) {
            case VideoJobEventKind::Queued:     ++queued;    break;
            case VideoJobEventKind::InProgress: ++progress;  break;
            case VideoJobEventKind::Completed:  completed = true; break;
            case VideoJobEventKind::Error:      std::abort();
            default: break;
        }
    });
    while (!handle->IsDone()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_EQ(queued.load(), 1);
    EXPECT_EQ(progress.load(), 3);
    EXPECT_TRUE(completed.load());
}

// ============================================================
// Music generation
// ============================================================

void TestMusicGen() {
    MusicGenConfig cfg; cfg.providerId = "mock";
    auto mus = CreateMusicGen(cfg);
    EXPECT_TRUE(mus != nullptr);

    MusicGenRequest req;
    req.mode = MusicGenMode::Song;
    req.prompt = "uplifting jazz piano with vocals";
    req.genres = { "jazz" };
    req.instruments = { "piano", "double bass", "drums" };
    req.bpm = 120;
    req.durationSec = 8.0;
    req.lyrics = "la la la";
    req.returnStems = true;

    auto resp = mus->Generate(req);
    EXPECT_EQ(resp.error.code, ErrorCode::None);
    EXPECT_EQ(resp.tracks.size(), size_t{1});
    const auto& t = resp.tracks[0];
    EXPECT_EQ(t.bpm, 120);
    EXPECT_EQ(t.lyrics, std::string("la la la"));
    EXPECT_EQ(t.stems.size(), size_t{4});
    EXPECT_TRUE(t.audio.bytes.size() > 0);

    // Auto-generate lyrics path
    MusicGenRequest auto_req = req;
    auto_req.lyrics.clear();
    auto_req.autoGenerateLyrics = true;
    auto auto_resp = mus->Generate(auto_req);
    EXPECT_TRUE(!auto_resp.tracks[0].lyrics.empty());

    // Instrumental defaults
    MusicGenRequest inst;
    inst.mode = MusicGenMode::Instrumental;
    inst.prompt = "ambient";
    auto inst_resp = mus->Generate(inst);
    EXPECT_EQ(inst_resp.tracks.size(), size_t{1});
    EXPECT_TRUE(inst_resp.tracks[0].lyrics.empty());

    // Job-style progress
    auto* mock = static_cast<MockMusicGen*>(mus->RawProvider());
    mock->SetJobSteps(2);
    mock->SetJobStepInterval(std::chrono::milliseconds(0));

    std::atomic<bool> completed{false};
    auto handle = mus->GenerateJob(req, [&](const MusicJobEvent& ev) {
        if (ev.kind == MusicJobEventKind::Completed) completed = true;
        if (ev.kind == MusicJobEventKind::Error) std::abort();
    });
    while (!handle->IsDone()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_TRUE(completed.load());
}

// ============================================================
// Code assist
// ============================================================

void TestCodeAssistTasks() {
    CodeAssistConfig cfg; cfg.providerId = "mock";
    auto code = CreateCodeAssist(cfg);
    EXPECT_TRUE(code != nullptr);

    // Generate
    {
        CodeAssistRequest r;
        r.task = CodeTask::Generate;
        r.language = "python";
        r.instruction = "fizzbuzz";
        auto resp = code->Run(r);
        EXPECT_EQ(resp.error.code, ErrorCode::None);
        EXPECT_TRUE(resp.code.find("fizzbuzz") != std::string::npos);
    }
    // Explain
    {
        CodeAssistRequest r;
        r.task = CodeTask::Explain;
        r.codeSnippet = "int x = 1;";
        auto resp = code->Run(r);
        EXPECT_TRUE(!resp.explanation.empty());
    }
    // FIM
    {
        CodeAssistRequest r;
        r.task = CodeTask::Complete;
        r.prefix = "int sum(int a, int b) { return ";
        r.suffix = "; }";
        auto resp = code->Run(r);
        EXPECT_TRUE(resp.code.find("/*FIM*/") != std::string::npos);
    }
    // Bug detection
    {
        CodeAssistRequest r;
        r.task = CodeTask::DetectBugs;
        r.codeSnippet = "if (x = 1) {}";
        auto resp = code->Run(r);
        EXPECT_EQ(resp.diagnostics.size(), size_t{1});
        EXPECT_EQ(resp.diagnostics[0].severity, CodeDiagnostic::Severity::Warning);
    }
    // Translate language
    {
        CodeAssistRequest r;
        r.task = CodeTask::TranslateLanguage;
        r.language = "python";
        r.targetLanguage = "cpp";
        r.codeSnippet = "print('hi')";
        auto resp = code->Run(r);
        EXPECT_TRUE(resp.code.find("ported from python to cpp") != std::string::npos);
    }
    // Multiple suggestions
    {
        CodeAssistRequest r;
        r.task = CodeTask::Generate;
        r.instruction = "say hello";
        r.numSuggestions = 3;
        auto resp = code->Run(r);
        EXPECT_EQ(resp.alternativeSuggestions.size(), size_t{2});
    }
}

void TestCodeAssistStreaming() {
    auto code = CreateMockCodeAssist();
    auto* mock = static_cast<MockCodeAssist*>(code->RawProvider());
    mock->SetStreamDelay(std::chrono::milliseconds(0));

    CodeAssistRequest r;
    r.task = CodeTask::Generate;
    r.language = "python";
    r.instruction = "fizzbuzz fizz buzz";
    r.requestExplanation = true;

    std::atomic<int> codeDeltas{0}, explDeltas{0};
    std::atomic<bool> done{false};
    auto handle = code->RunStream(r, [&](const CodeStreamEvent& ev) {
        switch (ev.kind) {
            case CodeStreamEventKind::CodeDelta:        ++codeDeltas; break;
            case CodeStreamEventKind::ExplanationDelta: ++explDeltas; break;
            case CodeStreamEventKind::Done:             done = true;  break;
            case CodeStreamEventKind::Error:            std::abort();
            default: break;
        }
    });
    while (!handle->IsDone()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_TRUE(done.load());
    EXPECT_TRUE(codeDeltas.load() > 0);
    EXPECT_TRUE(explDeltas.load() > 0);
}

void TestCodeAssistDiagnosticsStream() {
    auto code = CreateMockCodeAssist();
    auto* mock = static_cast<MockCodeAssist*>(code->RawProvider());
    mock->SetStreamDelay(std::chrono::milliseconds(0));

    CodeAssistRequest r;
    r.task = CodeTask::DetectBugs;
    r.codeSnippet = "x = 1;";

    std::atomic<int> diagnostics{0};
    std::atomic<bool> done{false};
    auto handle = code->RunStream(r, [&](const CodeStreamEvent& ev) {
        if (ev.kind == CodeStreamEventKind::Diagnostic) ++diagnostics;
        if (ev.kind == CodeStreamEventKind::Done) done = true;
    });
    while (!handle->IsDone()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_TRUE(done.load());
    EXPECT_EQ(diagnostics.load(), 1);
}

// ============================================================
// Provider listing
// ============================================================

void TestProviderListings() {
    auto contains = [](const std::vector<std::string>& v, const std::string& s) {
        for (const auto& x : v) if (x == s) return true;
        return false;
    };
    EXPECT_TRUE(contains(ListTranslatorProviders(),  "mock"));
    EXPECT_TRUE(contains(ListVideoGenProviders(),    "mock"));
    EXPECT_TRUE(contains(ListMusicGenProviders(),    "mock"));
    EXPECT_TRUE(contains(ListCodeAssistProviders(),  "mock"));
}

} // namespace

int main() {
    TestTranslator();
    TestVideoGen();
    TestMusicGen();
    TestCodeAssistTasks();
    TestCodeAssistStreaming();
    TestCodeAssistDiagnosticsStream();
    TestProviderListings();
    std::cout << "OK: ultraai_test_mock_more_capabilities" << std::endl;
    return 0;
}
