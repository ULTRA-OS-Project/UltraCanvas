// UltraAI/src/CapabilityFactories.cpp
// CreateXxx / ListXxxProviders / RegisterXxxProvider implementations for
// every capability except TextLLM (kept in TextLLMFactory.cpp for clarity).
// Each capability owns a small registry; mock adapters self-register when
// the corresponding ULTRAAI_HAS_*_ADAPTER macro is defined.
// Version: 0.1.0
// Last Modified: 2026-05-08
// Author: UltraAI Module

#include "UltraAIEmbeddings.h"
#include "UltraAISpeechToText.h"
#include "UltraAITextToSpeech.h"
#include "UltraAIImageGen.h"
#include "UltraAIVisionAnalyzer.h"
#include "UltraAITranslator.h"
#include "UltraAIVideoGen.h"
#include "UltraAIMusicGen.h"
#include "UltraAICodeAssist.h"

#ifdef ULTRAAI_HAS_MOCK_ADAPTER
#include "UltraAIMockEmbeddings.h"
#include "UltraAIMockSpeechToText.h"
#include "UltraAIMockTextToSpeech.h"
#include "UltraAIMockImageGen.h"
#include "UltraAIMockVisionAnalyzer.h"
#include "UltraAIMockTranslator.h"
#include "UltraAIMockVideoGen.h"
#include "UltraAIMockMusicGen.h"
#include "UltraAIMockCodeAssist.h"
#endif

#include <cmath>
#include <map>
#include <mutex>

namespace UltraAI {

namespace {

template<typename Interface, typename Config>
struct Registry {
    using FactoryFn =
        std::function<std::unique_ptr<Interface>(const Config&, Error*)>;
    std::mutex mu;
    std::map<std::string, FactoryFn> providers;
    std::function<void()> ensureBuiltins;
};

template<typename Interface, typename Config>
std::unique_ptr<Interface> Create(Registry<Interface, Config>& r,
                                  const Config& cfg,
                                  Error* outError) {
    std::lock_guard<std::mutex> lock(r.mu);
    if (r.ensureBuiltins) r.ensureBuiltins();

    std::string id = cfg.providerId;
    if (id.empty()) {
        if (r.providers.count("mock")) id = "mock";
        else if (!r.providers.empty()) id = r.providers.begin()->first;
    }
    auto it = r.providers.find(id);
    if (it == r.providers.end()) {
        if (outError) {
            outError->code    = ErrorCode::ModelNotFound;
            outError->message = "No provider registered for id '" + id + "'";
        }
        return nullptr;
    }
    Error tmp;
    Error* err = outError ? outError : &tmp;
    auto inst = it->second(cfg, err);
    if (!inst && err->code == ErrorCode::None) {
        err->code    = ErrorCode::ProviderError;
        err->message = "Provider '" + id + "' factory returned null";
    }
    return inst;
}

template<typename Interface, typename Config>
std::vector<std::string> List(Registry<Interface, Config>& r) {
    std::lock_guard<std::mutex> lock(r.mu);
    if (r.ensureBuiltins) r.ensureBuiltins();
    std::vector<std::string> ids;
    ids.reserve(r.providers.size());
    for (const auto& kv : r.providers) ids.push_back(kv.first);
    return ids;
}

template<typename Interface, typename Config>
bool Register(Registry<Interface, Config>& r,
              const std::string& providerId,
              std::function<std::unique_ptr<Interface>(const Config&, Error*)> fn) {
    std::lock_guard<std::mutex> lock(r.mu);
    r.providers[providerId] = std::move(fn);
    return true;
}

// ===== Per-capability singletons =====

Registry<IEmbeddings, EmbeddingsConfig>& EmbedsReg() {
    static Registry<IEmbeddings, EmbeddingsConfig> r;
    static std::once_flag init;
    std::call_once(init, [&]() {
        r.ensureBuiltins = [&]() {
#ifdef ULTRAAI_HAS_MOCK_ADAPTER
            if (!r.providers.count("mock")) {
                r.providers["mock"] = [](const EmbeddingsConfig&, Error*) {
                    return CreateMockEmbeddings();
                };
            }
#endif
        };
    });
    return r;
}

Registry<ISpeechToText, SpeechToTextConfig>& SttReg() {
    static Registry<ISpeechToText, SpeechToTextConfig> r;
    static std::once_flag init;
    std::call_once(init, [&]() {
        r.ensureBuiltins = [&]() {
#ifdef ULTRAAI_HAS_MOCK_ADAPTER
            if (!r.providers.count("mock")) {
                r.providers["mock"] = [](const SpeechToTextConfig&, Error*) {
                    return CreateMockSpeechToText();
                };
            }
#endif
        };
    });
    return r;
}

Registry<ITextToSpeech, TextToSpeechConfig>& TtsReg() {
    static Registry<ITextToSpeech, TextToSpeechConfig> r;
    static std::once_flag init;
    std::call_once(init, [&]() {
        r.ensureBuiltins = [&]() {
#ifdef ULTRAAI_HAS_MOCK_ADAPTER
            if (!r.providers.count("mock")) {
                r.providers["mock"] = [](const TextToSpeechConfig&, Error*) {
                    return CreateMockTextToSpeech();
                };
            }
#endif
        };
    });
    return r;
}

Registry<IImageGen, ImageGenConfig>& ImgReg() {
    static Registry<IImageGen, ImageGenConfig> r;
    static std::once_flag init;
    std::call_once(init, [&]() {
        r.ensureBuiltins = [&]() {
#ifdef ULTRAAI_HAS_MOCK_ADAPTER
            if (!r.providers.count("mock")) {
                r.providers["mock"] = [](const ImageGenConfig&, Error*) {
                    return CreateMockImageGen();
                };
            }
#endif
        };
    });
    return r;
}

Registry<IVisionAnalyzer, VisionAnalyzerConfig>& VisReg() {
    static Registry<IVisionAnalyzer, VisionAnalyzerConfig> r;
    static std::once_flag init;
    std::call_once(init, [&]() {
        r.ensureBuiltins = [&]() {
#ifdef ULTRAAI_HAS_MOCK_ADAPTER
            if (!r.providers.count("mock")) {
                r.providers["mock"] = [](const VisionAnalyzerConfig&, Error*) {
                    return CreateMockVisionAnalyzer();
                };
            }
#endif
        };
    });
    return r;
}

Registry<ITranslator, TranslatorConfig>& TrReg() {
    static Registry<ITranslator, TranslatorConfig> r;
    static std::once_flag init;
    std::call_once(init, [&]() {
        r.ensureBuiltins = [&]() {
#ifdef ULTRAAI_HAS_MOCK_ADAPTER
            if (!r.providers.count("mock")) {
                r.providers["mock"] = [](const TranslatorConfig&, Error*) {
                    return CreateMockTranslator();
                };
            }
#endif
        };
    });
    return r;
}

Registry<IVideoGen, VideoGenConfig>& VidReg() {
    static Registry<IVideoGen, VideoGenConfig> r;
    static std::once_flag init;
    std::call_once(init, [&]() {
        r.ensureBuiltins = [&]() {
#ifdef ULTRAAI_HAS_MOCK_ADAPTER
            if (!r.providers.count("mock")) {
                r.providers["mock"] = [](const VideoGenConfig&, Error*) {
                    return CreateMockVideoGen();
                };
            }
#endif
        };
    });
    return r;
}

Registry<IMusicGen, MusicGenConfig>& MusReg() {
    static Registry<IMusicGen, MusicGenConfig> r;
    static std::once_flag init;
    std::call_once(init, [&]() {
        r.ensureBuiltins = [&]() {
#ifdef ULTRAAI_HAS_MOCK_ADAPTER
            if (!r.providers.count("mock")) {
                r.providers["mock"] = [](const MusicGenConfig&, Error*) {
                    return CreateMockMusicGen();
                };
            }
#endif
        };
    });
    return r;
}

Registry<ICodeAssist, CodeAssistConfig>& CodeReg() {
    static Registry<ICodeAssist, CodeAssistConfig> r;
    static std::once_flag init;
    std::call_once(init, [&]() {
        r.ensureBuiltins = [&]() {
#ifdef ULTRAAI_HAS_MOCK_ADAPTER
            if (!r.providers.count("mock")) {
                r.providers["mock"] = [](const CodeAssistConfig&, Error*) {
                    return CreateMockCodeAssist();
                };
            }
#endif
        };
    });
    return r;
}

} // namespace

// =====================================================================
// Public factory functions
// =====================================================================

std::unique_ptr<IEmbeddings> CreateEmbeddings(const EmbeddingsConfig& cfg, Error* err) {
    return Create(EmbedsReg(), cfg, err);
}
std::vector<std::string> ListEmbeddingsProviders() { return List(EmbedsReg()); }
bool RegisterEmbeddingsProvider(
    const std::string& id,
    std::function<std::unique_ptr<IEmbeddings>(const EmbeddingsConfig&, Error*)> fn) {
    return Register(EmbedsReg(), id, std::move(fn));
}

std::unique_ptr<ISpeechToText> CreateSpeechToText(const SpeechToTextConfig& cfg, Error* err) {
    return Create(SttReg(), cfg, err);
}
std::vector<std::string> ListSpeechToTextProviders() { return List(SttReg()); }
bool RegisterSpeechToTextProvider(
    const std::string& id,
    std::function<std::unique_ptr<ISpeechToText>(const SpeechToTextConfig&, Error*)> fn) {
    return Register(SttReg(), id, std::move(fn));
}

std::unique_ptr<ITextToSpeech> CreateTextToSpeech(const TextToSpeechConfig& cfg, Error* err) {
    return Create(TtsReg(), cfg, err);
}
std::vector<std::string> ListTextToSpeechProviders() { return List(TtsReg()); }
bool RegisterTextToSpeechProvider(
    const std::string& id,
    std::function<std::unique_ptr<ITextToSpeech>(const TextToSpeechConfig&, Error*)> fn) {
    return Register(TtsReg(), id, std::move(fn));
}

std::unique_ptr<IImageGen> CreateImageGen(const ImageGenConfig& cfg, Error* err) {
    return Create(ImgReg(), cfg, err);
}
std::vector<std::string> ListImageGenProviders() { return List(ImgReg()); }
bool RegisterImageGenProvider(
    const std::string& id,
    std::function<std::unique_ptr<IImageGen>(const ImageGenConfig&, Error*)> fn) {
    return Register(ImgReg(), id, std::move(fn));
}

std::unique_ptr<IVisionAnalyzer> CreateVisionAnalyzer(
    const VisionAnalyzerConfig& cfg, Error* err) {
    return Create(VisReg(), cfg, err);
}
std::vector<std::string> ListVisionAnalyzerProviders() { return List(VisReg()); }
bool RegisterVisionAnalyzerProvider(
    const std::string& id,
    std::function<std::unique_ptr<IVisionAnalyzer>(const VisionAnalyzerConfig&, Error*)> fn) {
    return Register(VisReg(), id, std::move(fn));
}

std::unique_ptr<ITranslator> CreateTranslator(const TranslatorConfig& cfg, Error* err) {
    return Create(TrReg(), cfg, err);
}
std::vector<std::string> ListTranslatorProviders() { return List(TrReg()); }
bool RegisterTranslatorProvider(
    const std::string& id,
    std::function<std::unique_ptr<ITranslator>(const TranslatorConfig&, Error*)> fn) {
    return Register(TrReg(), id, std::move(fn));
}

std::unique_ptr<IVideoGen> CreateVideoGen(const VideoGenConfig& cfg, Error* err) {
    return Create(VidReg(), cfg, err);
}
std::vector<std::string> ListVideoGenProviders() { return List(VidReg()); }
bool RegisterVideoGenProvider(
    const std::string& id,
    std::function<std::unique_ptr<IVideoGen>(const VideoGenConfig&, Error*)> fn) {
    return Register(VidReg(), id, std::move(fn));
}

std::unique_ptr<IMusicGen> CreateMusicGen(const MusicGenConfig& cfg, Error* err) {
    return Create(MusReg(), cfg, err);
}
std::vector<std::string> ListMusicGenProviders() { return List(MusReg()); }
bool RegisterMusicGenProvider(
    const std::string& id,
    std::function<std::unique_ptr<IMusicGen>(const MusicGenConfig&, Error*)> fn) {
    return Register(MusReg(), id, std::move(fn));
}

std::unique_ptr<ICodeAssist> CreateCodeAssist(const CodeAssistConfig& cfg, Error* err) {
    return Create(CodeReg(), cfg, err);
}
std::vector<std::string> ListCodeAssistProviders() { return List(CodeReg()); }
bool RegisterCodeAssistProvider(
    const std::string& id,
    std::function<std::unique_ptr<ICodeAssist>(const CodeAssistConfig&, Error*)> fn) {
    return Register(CodeReg(), id, std::move(fn));
}

// =====================================================================
// IEmbeddings::CosineSimilarity (declared in UltraAIEmbeddings.h)
// =====================================================================

double IEmbeddings::CosineSimilarity(const EmbeddingVector& a,
                                     const EmbeddingVector& b) {
    if (a.values.size() != b.values.size() || a.values.empty()) return 0.0;
    double dot = 0.0, na = 0.0, nb = 0.0;
    for (size_t i = 0; i < a.values.size(); ++i) {
        dot += static_cast<double>(a.values[i]) * b.values[i];
        na  += static_cast<double>(a.values[i]) * a.values[i];
        nb  += static_cast<double>(b.values[i]) * b.values[i];
    }
    if (na == 0.0 || nb == 0.0) return 0.0;
    return dot / (std::sqrt(na) * std::sqrt(nb));
}

} // namespace UltraAI
