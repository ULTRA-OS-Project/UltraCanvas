// Apps/UltraSnip/ui/UltraSnipTypes.h
// What a snip is: the shape (a rectangle drawn on the screen, a window, the
// whole screen), the kind (a picture, a video) and the delay before it. A
// request travels from the command line, from UltraDesktop's button and
// from a second start of the program (UltraCanvasSingleInstance) as JSON.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DataFormats/UltraCanvasJSON.h"

#include <string>

namespace UltraSnip {

enum class SnipMode { Rectangle, Window, FullScreen };
enum class SnipKind { Picture, Video };

struct SnipRequest {
    SnipMode mode = SnipMode::Rectangle;
    SnipKind kind = SnipKind::Picture;
    int delaySeconds = 0;           // 0, 3, 5 or 10
    bool startAtOnce = false;       // a capture begins without a click on New
};

inline const char* ModeKey(SnipMode mode) {
    switch (mode) {
        case SnipMode::Window:     return "window";
        case SnipMode::FullScreen: return "screen";
        default:                   return "rectangle";
    }
}

inline SnipMode ModeFromKey(const std::string& key, SnipMode fallback = SnipMode::Rectangle) {
    if (key == "window") return SnipMode::Window;
    if (key == "screen" || key == "fullscreen") return SnipMode::FullScreen;
    if (key == "rectangle") return SnipMode::Rectangle;
    return fallback;
}

inline const char* KindKey(SnipKind kind) { return kind == SnipKind::Video ? "video" : "picture"; }

inline SnipKind KindFromKey(const std::string& key, SnipKind fallback = SnipKind::Picture) {
    if (key == "video") return SnipKind::Video;
    if (key == "picture" || key == "image") return SnipKind::Picture;
    return fallback;
}

inline UltraCanvas::JSONValue RequestToJson(const SnipRequest& request) {
    UltraCanvas::JSONValue json = UltraCanvas::JSONValue::MakeObject();
    json.Set("mode", ModeKey(request.mode));
    json.Set("kind", KindKey(request.kind));
    json.Set("delay", request.delaySeconds);
    json.Set("start", request.startAtOnce);
    return json;
}

inline SnipRequest RequestFromJson(const UltraCanvas::JSONValue& json) {
    SnipRequest request;
    request.mode = ModeFromKey(json.Get("mode").GetString());
    request.kind = KindFromKey(json.Get("kind").GetString());
    request.delaySeconds = static_cast<int>(json.Get("delay").GetInteger());
    request.startAtOnce = json.Get("start").GetBoolean();
    return request;
}

} // namespace UltraSnip
