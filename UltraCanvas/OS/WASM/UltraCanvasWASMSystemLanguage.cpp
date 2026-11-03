// OS/WASM/UltraCanvasWASMSystemLanguage.cpp
// SystemLanguageTagsNative in a browser: navigator.languages, the person's
// order, else navigator.language. The POSIX variables do not exist here.
// Version: 1.0.0
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework
#include "UltraCanvasSystemLanguage.h"

#include <emscripten.h>

#include <cstdlib>

namespace UltraCanvas {

std::vector<std::string> SystemLanguageTagsNative() {
    std::vector<std::string> tags;
    // A comma-separated list in a buffer the C side frees.
    char* joined = static_cast<char*>(EM_ASM_PTR({
        var list = '';
        try {
            if (navigator.languages && navigator.languages.length) list = navigator.languages.join(',');
            else if (navigator.language) list = navigator.language;
        } catch (err) {}
        return stringToNewUTF8(list);
    }));
    if (!joined) return tags;
    std::string item;
    for (const char* p = joined; ; ++p) {
        if (*p == ',' || *p == '\0') {
            if (!item.empty()) tags.push_back(item);
            item.clear();
            if (*p == '\0') break;
        } else {
            item += *p;
        }
    }
    std::free(joined);
    return tags;
}

}  // namespace UltraCanvas
