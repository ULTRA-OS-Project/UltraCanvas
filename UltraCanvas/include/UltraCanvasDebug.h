#pragma once

#include <iostream>

#ifdef ULTRACANVAS_DEBUG
    #define debugOutput std::cerr
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
