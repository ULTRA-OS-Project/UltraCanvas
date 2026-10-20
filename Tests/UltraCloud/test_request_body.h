// Tests/UltraCloud/test_request_body.h
// What a request would send, for a fake transport to look at: the bytes it
// carries, or - when it names a file (UltraNetHttpRequest::bodyFilePath, an
// upload streamed from disk) - the range of that file, read the way UltraNet
// reads it.
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework
#pragma once

#include <UltraNet/UltraNetHttp.h>

#include <fstream>
#include <iterator>
#include <string>
#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"

inline std::string RequestBody(const UltraNetHttpRequest& req) {
    if (req.bodyFilePath.empty()) return std::string(req.body.begin(), req.body.end());
    std::ifstream in(UltraCanvas::PathFromUtf8(req.bodyFilePath), std::ios::binary);
    std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::size_t start = static_cast<std::size_t>(req.bodyFileOffset);
    if (start > all.size()) return {};
    return req.bodyFileLength < 0
            ? all.substr(start)
            : all.substr(start, static_cast<std::size_t>(req.bodyFileLength));
}
