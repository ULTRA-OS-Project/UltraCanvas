// OS/Linux/UltraCanvasLinuxDesktopShellAudio.cpp
// The loudspeaker's volume, for the X11 backend of UltraCanvasDesktopShell:
// the default sink of the sound server through libpulse, which PulseAudio
// and PipeWire (whose pipewire-pulse serves the same protocol) both answer.
//
// libpulse is loaded at run time (dlopen) rather than linked: the framework
// does not depend on a sound server - the audio engine plays through
// miniaudio, which does the same - and a machine without one still runs the
// desktop, whose speaker panel then says so. The handful of types the calls
// need are declared here in libpulse's own layout, so no header is needed to
// build either; they have not changed since PulseAudio 1.0 (2011) and are the
// library's public ABI.
//
// Each call runs its own mainloop to completion: connect, one or two
// requests, disconnect - a few milliseconds, with a deadline so a hung
// server never holds the desktop.
// Version: 1.0.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework

#include "UltraCanvasDesktopShellBackend.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

#include <dlfcn.h>

namespace UltraCanvas {
namespace DesktopShellBackend {

namespace {

    // ===== LIBPULSE'S TYPES, AS ITS HEADERS LAY THEM OUT =====
    struct pa_mainloop;
    struct pa_mainloop_api;
    struct pa_context;
    struct pa_operation;
    struct pa_proplist;
    struct pa_spawn_api;
    using pa_volume_t = uint32_t;
    using pa_usec_t = uint64_t;
    constexpr pa_volume_t kVolumeNorm = 0x10000U;   // PA_VOLUME_NORM: 100 %
    constexpr unsigned kChannelsMax = 32;           // PA_CHANNELS_MAX
    struct pa_sample_spec { int format; uint32_t rate; uint8_t channels; };
    struct pa_channel_map { uint8_t channels; int map[kChannelsMax]; };
    struct pa_cvolume { uint8_t channels; pa_volume_t values[kChannelsMax]; };
    struct pa_server_info {
        const char* user_name;
        const char* host_name;
        const char* server_version;
        const char* server_name;
        pa_sample_spec sample_spec;
        const char* default_sink_name;
        const char* default_source_name;
        uint32_t cookie;
        pa_channel_map channel_map;
    };
    struct pa_sink_info {
        const char* name;
        uint32_t index;
        const char* description;
        pa_sample_spec sample_spec;
        pa_channel_map channel_map;
        uint32_t owner_module;
        pa_cvolume volume;
        int mute;
        uint32_t monitor_source;
        const char* monitor_source_name;
        pa_usec_t latency;
        const char* driver;
        int flags;
        pa_proplist* proplist;
        pa_usec_t configured_latency;
        pa_volume_t base_volume;
        int state;
        uint32_t n_volume_steps;
        uint32_t card;
        uint32_t n_ports;
        void** ports;
        void* active_port;
        uint8_t n_formats;
        void** formats;
    };
    enum { kContextReady = 4, kContextFailed = 5, kContextTerminated = 6 };   // pa_context_state_t
    enum { kOperationRunning = 0 };                                           // pa_operation_state_t
    constexpr int kContextNoAutoSpawn = 0x0001;                               // PA_CONTEXT_NOAUTOSPAWN

    using ServerInfoCallback = void (*)(pa_context*, const pa_server_info*, void*);
    using SinkInfoCallback = void (*)(pa_context*, const pa_sink_info*, int eol, void*);
    using SuccessCallback = void (*)(pa_context*, int success, void*);

    struct Pulse {
        void* handle = nullptr;
        pa_mainloop* (*mainloop_new)() = nullptr;
        void (*mainloop_free)(pa_mainloop*) = nullptr;
        int (*mainloop_iterate)(pa_mainloop*, int block, int* retval) = nullptr;
        pa_mainloop_api* (*mainloop_get_api)(pa_mainloop*) = nullptr;
        pa_context* (*context_new)(pa_mainloop_api*, const char* name) = nullptr;
        int (*context_connect)(pa_context*, const char* server, int flags, const pa_spawn_api*) = nullptr;
        int (*context_get_state)(pa_context*) = nullptr;
        void (*context_disconnect)(pa_context*) = nullptr;
        void (*context_unref)(pa_context*) = nullptr;
        pa_operation* (*context_get_server_info)(pa_context*, ServerInfoCallback, void*) = nullptr;
        pa_operation* (*context_get_sink_info_by_name)(pa_context*, const char*, SinkInfoCallback, void*) = nullptr;
        pa_operation* (*context_set_sink_volume_by_name)(pa_context*, const char*, const pa_cvolume*,
                                                         SuccessCallback, void*) = nullptr;
        pa_operation* (*context_set_sink_mute_by_name)(pa_context*, const char*, int, SuccessCallback,
                                                       void*) = nullptr;
        int (*operation_get_state)(pa_operation*) = nullptr;
        void (*operation_unref)(pa_operation*) = nullptr;
        pa_volume_t (*cvolume_avg)(const pa_cvolume*) = nullptr;
        pa_cvolume* (*cvolume_set)(pa_cvolume*, unsigned channels, pa_volume_t) = nullptr;
    };

    std::mutex g_pulseMutex;
    Pulse g_pulse;
    bool g_pulseTried = false;

    template <typename F>
    bool Load(void* handle, const char* name, F& into) {
        into = reinterpret_cast<F>(dlsym(handle, name));
        return into != nullptr;
    }

    // Loaded once; a machine without the library answers "no" from then on.
    const Pulse* LoadPulse(std::string& error) {
        std::lock_guard<std::mutex> lock(g_pulseMutex);
        if (!g_pulseTried) {
            g_pulseTried = true;
            void* handle = dlopen("libpulse.so.0", RTLD_NOW | RTLD_LOCAL);
            if (handle) {
                Pulse p;
                p.handle = handle;
                const bool ok =
                    Load(handle, "pa_mainloop_new", p.mainloop_new) &&
                    Load(handle, "pa_mainloop_free", p.mainloop_free) &&
                    Load(handle, "pa_mainloop_iterate", p.mainloop_iterate) &&
                    Load(handle, "pa_mainloop_get_api", p.mainloop_get_api) &&
                    Load(handle, "pa_context_new", p.context_new) &&
                    Load(handle, "pa_context_connect", p.context_connect) &&
                    Load(handle, "pa_context_get_state", p.context_get_state) &&
                    Load(handle, "pa_context_disconnect", p.context_disconnect) &&
                    Load(handle, "pa_context_unref", p.context_unref) &&
                    Load(handle, "pa_context_get_server_info", p.context_get_server_info) &&
                    Load(handle, "pa_context_get_sink_info_by_name", p.context_get_sink_info_by_name) &&
                    Load(handle, "pa_context_set_sink_volume_by_name", p.context_set_sink_volume_by_name) &&
                    Load(handle, "pa_context_set_sink_mute_by_name", p.context_set_sink_mute_by_name) &&
                    Load(handle, "pa_operation_get_state", p.operation_get_state) &&
                    Load(handle, "pa_operation_unref", p.operation_unref) &&
                    Load(handle, "pa_cvolume_avg", p.cvolume_avg) &&
                    Load(handle, "pa_cvolume_set", p.cvolume_set);
                if (ok) g_pulse = p;
                else dlclose(handle);
            }
        }
        if (!g_pulse.handle) {
            error = "No sound server library (libpulse.so.0): neither PulseAudio nor PipeWire is installed.";
            return nullptr;
        }
        return &g_pulse;
    }

    // ===== ONE CONVERSATION WITH THE SERVER =====
    // A mainloop, a context connected to the default server, and the
    // requests run to completion one after the other.
    class Session {
    public:
        explicit Session(const Pulse& pulse) : pa(pulse) {}
        ~Session() {
            if (context) {
                pa.context_disconnect(context);
                pa.context_unref(context);
            }
            if (loop) pa.mainloop_free(loop);
        }

        bool Connect(std::string& error) {
            loop = pa.mainloop_new();
            context = loop ? pa.context_new(pa.mainloop_get_api(loop), "UltraCanvas desktop") : nullptr;
            if (!context) {
                error = "The sound server library could not be set up.";
                return false;
            }
            // No autospawn: a machine without a running server has no
            // loudspeaker to show, and starting one is not the desktop's job.
            if (pa.context_connect(context, nullptr, kContextNoAutoSpawn, nullptr) < 0) {
                error = "No sound server is running.";
                return false;
            }
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            for (;;) {
                const int state = pa.context_get_state(context);
                if (state == kContextReady) return true;
                if (state == kContextFailed || state == kContextTerminated) {
                    error = "No sound server is running.";
                    return false;
                }
                if (std::chrono::steady_clock::now() > deadline) {
                    error = "The sound server did not answer in time.";
                    return false;
                }
                if (pa.mainloop_iterate(loop, 1, nullptr) < 0) {
                    error = "The sound server's connection broke.";
                    return false;
                }
            }
        }

        // Runs `operation` to its end; false when the server went away or
        // took too long. Takes the operation's reference either way.
        bool Finish(pa_operation* operation, std::string& error) {
            if (!operation) {
                error = "The sound server refused the request.";
                return false;
            }
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            bool ok = true;
            while (pa.operation_get_state(operation) == kOperationRunning) {
                if (std::chrono::steady_clock::now() > deadline) {
                    error = "The sound server did not answer in time.";
                    ok = false;
                    break;
                }
                if (pa.mainloop_iterate(loop, 1, nullptr) < 0) {
                    error = "The sound server's connection broke.";
                    ok = false;
                    break;
                }
            }
            pa.operation_unref(operation);
            return ok;
        }

        // The default sink's name, and what the server knows about it.
        struct Sink {
            bool found = false;
            std::string name;
            std::string description;
            pa_cvolume volume{};
            bool muted = false;
        };

        bool DefaultSink(Sink& sink, std::string& error) {
            std::string defaultName;
            auto onServer = [](pa_context*, const pa_server_info* info, void* into) {
                if (info && info->default_sink_name) *static_cast<std::string*>(into) = info->default_sink_name;
            };
            if (!Finish(pa.context_get_server_info(context, onServer, &defaultName), error)) return false;
            if (defaultName.empty()) {
                error = "The sound server has no default output.";
                return false;
            }
            auto onSink = [](pa_context*, const pa_sink_info* info, int eol, void* into) {
                if (eol > 0 || !info) return;
                auto* s = static_cast<Sink*>(into);
                s->found = true;
                s->name = info->name ? info->name : "";
                s->description = info->description ? info->description : "";
                s->volume = info->volume;
                s->muted = info->mute != 0;
            };
            if (!Finish(pa.context_get_sink_info_by_name(context, defaultName.c_str(), onSink, &sink), error))
                return false;
            if (!sink.found) {
                error = "The sound server's default output \"" + defaultName + "\" is gone.";
                return false;
            }
            return true;
        }

        bool Succeeded(pa_operation* operation, std::string& error, int& success) {
            // The callback's `success` lands in the int the request was given.
            if (!Finish(operation, error)) return false;
            if (success == 0) {
                error = "The sound server refused the change.";
                return false;
            }
            return true;
        }

        static void OnSuccess(pa_context*, int success, void* into) { *static_cast<int*>(into) = success; }

        const Pulse& pa;
        pa_mainloop* loop = nullptr;
        pa_context* context = nullptr;
    };

} // namespace

// ===== THE LOUDSPEAKER =====

DesktopAudioOutput ReadAudioOutput() {
    DesktopAudioOutput out;
    const Pulse* pulse = LoadPulse(out.error);
    if (!pulse) return out;
    Session session(*pulse);
    Session::Sink sink;
    if (!session.Connect(out.error) || !session.DefaultSink(sink, out.error)) return out;
    out.available = true;
    out.device = sink.description.empty() ? sink.name : sink.description;
    out.muted = sink.muted;
    const pa_volume_t average = pulse->cvolume_avg(&sink.volume);
    out.volumePercent = static_cast<int>((static_cast<uint64_t>(average) * 100 + kVolumeNorm / 2) / kVolumeNorm);
    return out;
}

bool SetAudioOutputVolume(int percent, std::string& error) {
    const Pulse* pulse = LoadPulse(error);
    if (!pulse) return false;
    Session session(*pulse);
    Session::Sink sink;
    if (!session.Connect(error) || !session.DefaultSink(sink, error)) return false;
    // Every channel to the same level, as the desktop's one slider means;
    // above 100 % only where the server allows it, so a caller cannot
    // overdrive by a typo (PulseAudio's own limit is 153 %).
    const pa_volume_t level = static_cast<pa_volume_t>(
        (static_cast<uint64_t>(std::clamp(percent, 0, 150)) * kVolumeNorm) / 100);
    pa_cvolume volume = sink.volume;
    pulse->cvolume_set(&volume, volume.channels == 0 ? 2 : volume.channels, level);
    int success = 0;
    return session.Succeeded(pulse->context_set_sink_volume_by_name(session.context, sink.name.c_str(), &volume,
                                                                    &Session::OnSuccess, &success),
                             error, success);
}

bool SetAudioOutputMuted(bool muted, std::string& error) {
    const Pulse* pulse = LoadPulse(error);
    if (!pulse) return false;
    Session session(*pulse);
    Session::Sink sink;
    if (!session.Connect(error) || !session.DefaultSink(sink, error)) return false;
    int success = 0;
    return session.Succeeded(pulse->context_set_sink_mute_by_name(session.context, sink.name.c_str(), muted ? 1 : 0,
                                                                  &Session::OnSuccess, &success),
                             error, success);
}

} // namespace DesktopShellBackend
} // namespace UltraCanvas
