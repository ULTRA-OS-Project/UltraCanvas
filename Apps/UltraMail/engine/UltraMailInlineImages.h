// Apps/UltraMail/engine/UltraMailInlineImages.h
// The images an HTML message shows, sorted by where they come from:
//   * embedded - a part of the message itself, referenced as cid:<Content-ID>
//     (multipart/related) or by its Content-Location, or a data: URI. Always
//     shown: nothing leaves the machine.
//   * remote   - an http(s) address. Loading one tells the sender that, when
//     and from where the message was read (a tracking pixel is exactly
//     that), so the reading pane asks first.
// Headless: parsing only; the reading pane does the fetching and the asking.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace UltraMail {

enum class ImageSource {
    Embedded,   // cid:, a Content-Location of a part, or data:
    Remote,     // http:// or https://
    Other       // file:, javascript:, relative paths, ... - never loaded
};

struct InlineImages {
    std::map<std::string, std::vector<uint8_t>> byContentId;        // without <>
    std::map<std::string, std::vector<uint8_t>> byContentLocation;
};

// Every part of `rawMessage` that carries a Content-ID or Content-Location.
InlineImages CollectInlineImages(const std::string& rawMessage);

// Where an <img src> points. A Content-Location of one of the message's own
// parts counts as embedded even when it looks like a URL.
ImageSource ClassifyImageSource(const std::string& src, const InlineImages& images);

// The bytes of an embedded image (cid: / Content-Location / data:), empty
// when the message has no such part.
std::vector<uint8_t> ResolveEmbeddedImage(const std::string& src, const InlineImages& images);

} // namespace UltraMail
