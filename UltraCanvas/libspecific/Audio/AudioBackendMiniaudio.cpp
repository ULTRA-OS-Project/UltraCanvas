// libspecific/Audio/AudioBackendMiniaudio.cpp
// IAudioBackend implementation backed by miniaudio (single-header, MIT).
// Provides device enumeration, playback and capture streams, plus WAV
// encode and WAV/MP3/FLAC/Vorbis decode (miniaudio's built-in support).
// Only compiled when ULTRACANVAS_ENABLE_AUDIO is ON.
// Version: 0.1.0
// Last Modified: 2026-06-12
// Author: UltraCanvas Framework

#ifdef ULTRACANVAS_ENABLE_AUDIO

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "IAudioBackend.h"
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace UltraCanvas {

namespace {

// ===== FORMAT MAPPING =====
ma_format ToMiniaudioFormat(AudioSampleType t) {
    switch (t) {
        case AudioSampleType::PCM_S16: return ma_format_s16;
        case AudioSampleType::PCM_S24: return ma_format_s24;
        case AudioSampleType::PCM_S32: return ma_format_s32;
        case AudioSampleType::PCM_F32: return ma_format_f32;
    }
    return ma_format_s16;
}

AudioSampleType FromMiniaudioFormat(ma_format f) {
    switch (f) {
        case ma_format_s16: return AudioSampleType::PCM_S16;
        case ma_format_s24: return AudioSampleType::PCM_S24;
        case ma_format_s32: return AudioSampleType::PCM_S32;
        case ma_format_f32: return AudioSampleType::PCM_F32;
        default: return AudioSampleType::PCM_F32;
    }
}

// ===== STREAM =====
class MiniaudioStream : public IAudioStream {
public:
    MiniaudioStream(AudioStreamConfig cfg, AudioStreamDirection dir)
        : config(std::move(cfg)), direction(dir) {}

    ~MiniaudioStream() override {
        if (initialized) {
            ma_device_uninit(&device);
            initialized = false;
        }
    }

    bool Init(ma_context* ctx) {
        ma_device_config dc = ma_device_config_init(
            direction == AudioStreamDirection::Output ?
                ma_device_type_playback : ma_device_type_capture);

        if (direction == AudioStreamDirection::Output) {
            dc.playback.format = ToMiniaudioFormat(config.sampleType);
            dc.playback.channels = config.channels;
        } else {
            dc.capture.format = ToMiniaudioFormat(config.sampleType);
            dc.capture.channels = config.channels;
        }
        dc.sampleRate = config.sampleRate;
        dc.dataCallback = &MiniaudioStream::OnData;
        dc.pUserData = this;
        if (config.bufferFrames > 0) {
            dc.periodSizeInFrames = static_cast<ma_uint32>(config.bufferFrames);
        }

        if (ma_device_init(ctx, &dc, &device) != MA_SUCCESS) {
            if (errorCallback) errorCallback("ma_device_init failed");
            return false;
        }
        initialized = true;
        return true;
    }

    bool Start() override {
        if (!initialized) return false;
        if (ma_device_start(&device) != MA_SUCCESS) {
            if (errorCallback) errorCallback("ma_device_start failed");
            return false;
        }
        running = true;
        return true;
    }

    bool Stop() override {
        if (!initialized) return true;
        ma_device_stop(&device);
        running = false;
        return true;
    }

    bool IsRunning() const override { return running; }

    void SetVolume(float v) override {
        volume = v;
        if (initialized) ma_device_set_master_volume(&device, v);
    }
    float GetVolume() const override { return volume; }

    const AudioStreamConfig& GetConfig() const override { return config; }
    AudioStreamDirection GetDirection() const override { return direction; }

private:
    static void OnData(ma_device* pDevice, void* pOutput, const void* pInput,
                       ma_uint32 frameCount) {
        auto* self = static_cast<MiniaudioStream*>(pDevice->pUserData);
        if (!self) return;
        if (self->direction == AudioStreamDirection::Output) {
            if (self->fillCallback) {
                self->fillCallback(pOutput, frameCount);
            } else {
                ma_uint32 bpf = ma_get_bytes_per_frame(pDevice->playback.format,
                                                       pDevice->playback.channels);
                std::memset(pOutput, 0, bpf * frameCount);
            }
        } else {
            if (self->dataCallback) {
                self->dataCallback(pInput, frameCount);
            }
        }
    }

    AudioStreamConfig config;
    AudioStreamDirection direction;
    ma_device device{};
    bool initialized = false;
    bool running = false;
    float volume = 1.0f;
};

// ===== BACKEND =====
class MiniaudioBackend : public IAudioBackend {
public:
    bool Initialize() override {
        std::lock_guard<std::mutex> lock(mutex);
        if (initialized) return true;
        if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
            return false;
        }
        initialized = true;
        return true;
    }

    void Shutdown() override {
        std::lock_guard<std::mutex> lock(mutex);
        if (!initialized) return;
        ma_context_uninit(&context);
        initialized = false;
    }

    std::string GetName() const override { return "miniaudio"; }

    std::vector<AudioDeviceInfo> ListInputDevices() override {
        return ListDevices(/*input*/ true);
    }
    std::vector<AudioDeviceInfo> ListOutputDevices() override {
        return ListDevices(/*input*/ false);
    }

    AudioDeviceInfo GetDefaultInputDevice() override {
        for (auto& d : ListInputDevices()) if (d.isDefault) return d;
        return {};
    }
    AudioDeviceInfo GetDefaultOutputDevice() override {
        for (auto& d : ListOutputDevices()) if (d.isDefault) return d;
        return {};
    }

    MicrophonePermission GetMicrophonePermission() override {
        // miniaudio doesn't expose OS permission state. On Linux this is always
        // implicit; on macOS/Windows the OS prompts on first capture init.
        return MicrophonePermission::Granted;
    }

    void RequestMicrophonePermission(std::function<void(bool)> cb) override {
        // Trigger by opening + closing a transient capture device. If init
        // fails because the OS denied access, propagate false.
        AudioStreamConfig cfg;
        cfg.sampleRate = 44100;
        cfg.channels = 1;
        cfg.sampleType = AudioSampleType::PCM_S16;
        auto stream = OpenInputStream(cfg);
        bool granted = stream != nullptr;
        if (cb) cb(granted);
    }

    std::unique_ptr<IAudioStream> OpenOutputStream(const AudioStreamConfig& cfg) override {
        if (!Initialize()) return nullptr;
        auto s = std::make_unique<MiniaudioStream>(cfg, AudioStreamDirection::Output);
        if (!s->Init(&context)) return nullptr;
        return s;
    }

    std::unique_ptr<IAudioStream> OpenInputStream(const AudioStreamConfig& cfg) override {
        if (!Initialize()) return nullptr;
        auto s = std::make_unique<MiniaudioStream>(cfg, AudioStreamDirection::Input);
        if (!s->Init(&context)) return nullptr;
        return s;
    }

    std::shared_ptr<UCAudio> DecodeFile(const std::string& path) override {
        if (!Initialize()) return nullptr;
        ma_decoder decoder;
        ma_decoder_config dcfg = ma_decoder_config_init(ma_format_f32, 0, 0);
        if (ma_decoder_init_file(path.c_str(), &dcfg, &decoder) != MA_SUCCESS) {
            return nullptr;
        }
        return DecodeViaDecoder(decoder);
    }

    std::shared_ptr<UCAudio> DecodeMemory(const uint8_t* data, size_t size) override {
        if (!Initialize() || !data || !size) return nullptr;
        ma_decoder decoder;
        ma_decoder_config dcfg = ma_decoder_config_init(ma_format_f32, 0, 0);
        if (ma_decoder_init_memory(data, size, &dcfg, &decoder) != MA_SUCCESS) {
            return nullptr;
        }
        return DecodeViaDecoder(decoder);
    }

    bool EncodeFile(const std::string& path, const UCAudio& audio,
                    AudioFormat format) override {
        if (!Initialize() || !audio.IsValid()) return false;
        ma_encoding_format enc;
        switch (format) {
            case AudioFormat::WAV: enc = ma_encoding_format_wav; break;
            default: return false;  // Only WAV is built into miniaudio
        }
        const auto& info = audio.GetInfo();
        ma_encoder_config ec = ma_encoder_config_init(
            enc, ToMiniaudioFormat(info.sampleType),
            info.channels, info.sampleRate);
        ma_encoder encoder;
        if (ma_encoder_init_file(path.c_str(), &ec, &encoder) != MA_SUCCESS) {
            return false;
        }
        ma_uint64 written = 0;
        ma_encoder_write_pcm_frames(&encoder, audio.GetData(),
                                    info.frameCount, &written);
        ma_encoder_uninit(&encoder);
        return written == info.frameCount;
    }

private:
    std::vector<AudioDeviceInfo> ListDevices(bool input) {
        std::vector<AudioDeviceInfo> result;
        if (!Initialize()) return result;
        ma_device_info* playback = nullptr;
        ma_uint32 playbackCount = 0;
        ma_device_info* capture = nullptr;
        ma_uint32 captureCount = 0;
        if (ma_context_get_devices(&context,
                                   &playback, &playbackCount,
                                   &capture, &captureCount) != MA_SUCCESS) {
            return result;
        }
        ma_device_info* list = input ? capture : playback;
        ma_uint32 count = input ? captureCount : playbackCount;
        for (ma_uint32 i = 0; i < count; ++i) {
            AudioDeviceInfo info;
            info.name = list[i].name;
            info.isDefault = list[i].isDefault != 0;
            info.isInput = input;
            info.maxChannels = 2;  // miniaudio doesn't surface this here cheaply
            // Encode the ma_device_id pointer index as the opaque ID.
            info.id = (input ? "in:" : "out:") + std::to_string(i);
            result.push_back(std::move(info));
        }
        return result;
    }

    std::shared_ptr<UCAudio> DecodeViaDecoder(ma_decoder& decoder) {
        ma_uint64 totalFrames = 0;
        ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames);

        AudioBufferInfo info;
        info.sampleRate = decoder.outputSampleRate;
        info.channels = decoder.outputChannels;
        info.sampleType = FromMiniaudioFormat(decoder.outputFormat);
        info.frameCount = static_cast<size_t>(totalFrames);
        info.durationSeconds = info.sampleRate
            ? static_cast<double>(info.frameCount) / info.sampleRate : 0.0;

        size_t totalBytes = info.TotalBytes();
        auto audio = std::make_shared<UCAudio>();
        audio->SetInfo(info);
        audio->MutableData().resize(totalBytes);

        if (totalBytes > 0) {
            ma_uint64 framesRead = 0;
            ma_decoder_read_pcm_frames(&decoder,
                                       audio->MutableData().data(),
                                       totalFrames, &framesRead);
            if (framesRead < totalFrames) {
                // Best-effort: shrink to what we actually decoded
                info.frameCount = static_cast<size_t>(framesRead);
                info.durationSeconds = info.sampleRate
                    ? static_cast<double>(framesRead) / info.sampleRate : 0.0;
                audio->MutableData().resize(info.TotalBytes());
                audio->SetInfo(info);
            }
        }
        ma_decoder_uninit(&decoder);
        return audio;
    }

    ma_context context{};
    bool initialized = false;
    std::mutex mutex;
};

} // namespace

// Singleton accessor — replaces the null backend when this TU is compiled in.
IAudioBackend* GetAudioBackend() {
    static MiniaudioBackend backend;
    static bool inited = backend.Initialize();
    (void)inited;
    return &backend;
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_ENABLE_AUDIO
