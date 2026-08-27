#pragma once

// UltraCanvasDebug.h
// The framework's diagnostic stream, `debugOutput`.
//
// Historically this compiled to `std::cerr` under ULTRACANVAS_DEBUG and to a
// do-nothing NullStream otherwise. That made a packaged build undiagnosable in
// exactly the situation where diagnostics matter most: the Windows targets are
// built with WIN32_EXECUTABLE (the GUI subsystem), so the process has no
// console and `std::cerr` goes nowhere — and a Release build did not even
// evaluate the message. An app that failed to start on a user's machine
// therefore exited silently, with no window, no console output and no log.
//
// The sink is now chosen at *runtime*, in every build configuration:
//
//   ULTRACANVAS_DEBUG_LOG unset   Compile-time default: enabled and writing to
//                                 stderr if ULTRACANVAS_DEBUG is defined
//                                 (Debug builds), otherwise off.
//   ULTRACANVAS_DEBUG_LOG=0       Off, whatever the build configuration.
//                                 (also: "off", "no", "none", "false")
//   ULTRACANVAS_DEBUG_LOG=1       On, writing to stderr.
//                                 (also: "on", "yes", "true", "stderr", "-")
//   ULTRACANVAS_DEBUG_LOG=<path>  On, appending to that file. The file is
//                                 flushed after every line, so it survives a
//                                 crash. If it cannot be opened, the sink
//                                 falls back to stderr.
//
// Enabling costs one predictable branch per `<<` when the sink is off, so
// leaving the call sites compiled in is cheap. Values that have no
// `operator<<(std::ostream&, T)` are silently dropped rather than failing to
// compile, which keeps call sites that only ever built against the old
// NullStream working.
//
// Threading: a mutex serialises each `<<`, so concurrent logging cannot corrupt
// the stream. Lines from different threads may still interleave; every line
// carries a timestamp, which is normally enough to untangle them.

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>

namespace UltraCanvas {

    namespace Detail {

        // True when `std::ostream << const T&` compiles. Used to drop values the
        // old NullStream accepted but a real stream would reject.
        template <typename T, typename = void>
        struct IsStreamable : std::false_type {};

        template <typename T>
        struct IsStreamable<T, std::void_t<decltype(std::declval<std::ostream&>()
                                                    << std::declval<const T&>())>>
            : std::true_type {};

        inline bool EqualsAnyOf(const std::string& value,
                                std::initializer_list<const char*> candidates) {
            for (const char* candidate : candidates) {
                if (value == candidate) return true;
            }
            return false;
        }

        inline std::string LowerCased(const char* value) {
            std::string out;
            if (!value) return out;
            for (const char* c = value; *c; ++c) {
                out.push_back(static_cast<char>(
                    (*c >= 'A' && *c <= 'Z') ? (*c - 'A' + 'a') : *c));
            }
            return out;
        }

        // Resolves ULTRACANVAS_DEBUG_LOG once, on first use, and owns the log
        // file when one was requested.
        class DebugSink {
        public:
            static DebugSink& Instance() {
                // Deliberately never destroyed. Code that logs from a static
                // destructor -- shutdown paths do -- would otherwise write
                // through a closed file stream during teardown, which the old
                // `std::cerr` sink could not suffer from. Every line is flushed
                // as it is written, so nothing is lost by not closing the file.
                static DebugSink* sink = new DebugSink();
                return *sink;
            }

            bool IsEnabled() const { return enabled_; }
            std::ostream& Stream() { return *stream_; }
            std::mutex& Mutex() { return mutex_; }

            // Set the sink from code, overriding whatever the environment said.
            // Passing an empty path turns file logging off again.
            void SetLogFile(const std::string& path) {
                std::lock_guard<std::mutex> lock(mutex_);
                file_.close();
                if (path.empty()) {
                    stream_ = &std::cerr;
                    return;
                }
                file_.open(path, std::ios::out | std::ios::app);
                stream_ = file_.is_open() ? static_cast<std::ostream*>(&file_)
                                          : static_cast<std::ostream*>(&std::cerr);
                enabled_ = true;
            }

            void SetEnabled(bool enabled) { enabled_ = enabled; }

        private:
            DebugSink() {
                const char* raw = std::getenv("ULTRACANVAS_DEBUG_LOG");
                if (!raw || !*raw) {
#ifdef ULTRACANVAS_DEBUG
                    enabled_ = true;
#endif
                    return;
                }

                const std::string setting = LowerCased(raw);
                if (EqualsAnyOf(setting, {"0", "off", "no", "none", "false"})) {
                    return;
                }
                enabled_ = true;
                if (EqualsAnyOf(setting, {"1", "on", "yes", "true", "stderr", "-"})) {
                    return;
                }

                // Anything else is a path. Keep the original spelling: the
                // lower-cased copy is only for keyword matching.
                file_.open(raw, std::ios::out | std::ios::app);
                if (file_.is_open()) {
                    stream_ = &file_;
                }
            }

            bool           enabled_ = false;
            std::ofstream  file_;
            std::ostream*  stream_ = &std::cerr;
            std::mutex     mutex_;
        };

    } // namespace Detail

    // Streams to whatever DebugSink resolved to, prefixing every line with a
    // wall-clock timestamp.
    class TimestampedStream {
        bool atLineStart_ = true;
        using OManip = std::ostream& (*)(std::ostream&);

        static void WriteTimestamp(std::ostream& out) {
            using namespace std::chrono;
            const auto now  = system_clock::now();
            const auto secs = time_point_cast<seconds>(now);
            const auto ms   = duration_cast<milliseconds>(now - secs).count();
            const std::time_t t = system_clock::to_time_t(now);
            std::tm tm{};
#if defined(_WIN32)
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            out << '['
                << std::put_time(&tm, "%H:%M:%S")
                << '.' << std::setfill('0') << std::setw(3) << ms
                << "] ";
        }

        void EnsurePrefix(std::ostream& out) {
            if (atLineStart_) {
                atLineStart_ = false;
                WriteTimestamp(out);
            }
        }

    public:
        template <typename T>
        TimestampedStream& operator<<(const T& value) {
            if constexpr (Detail::IsStreamable<T>::value) {
                auto& sink = Detail::DebugSink::Instance();
                if (sink.IsEnabled()) {
                    std::lock_guard<std::mutex> lock(sink.Mutex());
                    EnsurePrefix(sink.Stream());
                    sink.Stream() << value;
                }
            }
            return *this;
        }

        TimestampedStream& operator<<(OManip manip) {
            auto& sink = Detail::DebugSink::Instance();
            if (sink.IsEnabled()) {
                std::lock_guard<std::mutex> lock(sink.Mutex());
                EnsurePrefix(sink.Stream());
                sink.Stream() << manip;
                if (manip == static_cast<OManip>(std::endl)) {
                    atLineStart_ = true;
                    // endl already flushed; a file sink additionally has to
                    // survive an abrupt exit, which the flush above covers.
                }
            }
            return *this;
        }

        TimestampedStream& operator<<(std::ios_base& (*manip)(std::ios_base&)) {
            auto& sink = Detail::DebugSink::Instance();
            if (sink.IsEnabled()) {
                std::lock_guard<std::mutex> lock(sink.Mutex());
                sink.Stream() << manip;
            }
            return *this;
        }
    };

    inline TimestampedStream debugStream;

    // True when anything written to debugOutput actually reaches a sink. Use it
    // to skip building an expensive diagnostic string.
    inline bool IsDebugOutputEnabled() {
        return Detail::DebugSink::Instance().IsEnabled();
    }

    // Redirects debugOutput to `path` (appending), overriding
    // ULTRACANVAS_DEBUG_LOG. An empty path returns the sink to stderr.
    inline void SetDebugOutputFile(const std::string& path) {
        Detail::DebugSink::Instance().SetLogFile(path);
    }

    // Turns debugOutput on or off at runtime.
    inline void SetDebugOutputEnabled(bool enabled) {
        Detail::DebugSink::Instance().SetEnabled(enabled);
    }

} // namespace UltraCanvas

#define debugOutput UltraCanvas::debugStream
