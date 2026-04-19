#pragma once

#include <iostream>

#ifdef ULTRACANVAS_DEBUG
    #include <chrono>
    #include <ctime>
    #include <iomanip>

namespace UltraCanvas {

    class TimestampedStream {
        bool atLineStart_ = true;
        using OManip = std::ostream& (*)(std::ostream&);

        void writeTimestamp() {
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
            std::cerr << '['
                      << std::put_time(&tm, "%H:%M:%S")
                      << '.' << std::setfill('0') << std::setw(3) << ms
                      << "] ";
        }

        void ensurePrefix() {
            if (atLineStart_) {
                atLineStart_ = false;
                writeTimestamp();
            }
        }

    public:
        template <typename T>
        TimestampedStream& operator<<(const T& value) {
            ensurePrefix();
            std::cerr << value;
            return *this;
        }

        TimestampedStream& operator<<(OManip manip) {
            ensurePrefix();
            std::cerr << manip;
            if (manip == static_cast<OManip>(std::endl)) {
                atLineStart_ = true;
            }
            return *this;
        }

        TimestampedStream& operator<<(std::ios_base& (*manip)(std::ios_base&)) {
            std::cerr << manip;
            return *this;
        }
    };

    inline TimestampedStream debugStream;

} // namespace UltraCanvas

    #define debugOutput UltraCanvas::debugStream
#else
namespace UltraCanvas {
    class NullStream {
    public:
        template<typename T>
        NullStream& operator<<(const T&) { return *this; }
        NullStream& operator<<(std::ostream& (*)(std::ostream&)) { return *this; }
    };
    inline NullStream nullStream;
}
    #define debugOutput UltraCanvas::nullStream
#endif
