// UltraAI/adapters/_shared/include/UltraAIMultipart.h
// multipart/form-data body builder for adapters that upload files through
// the transport seam (ComfyUI's /upload/image and /upload/mask). The seam
// carries bodies as std::string, so the body is composed here rather than
// by each adapter.
// Version: 0.1.0
// Last Modified: 2026-08-24
// Author: UltraAI Module
#pragma once

#include <string>
#include <vector>

namespace UltraAI {

struct MultipartPart {
    std::string name;         // form field name (required)
    std::string value;        // raw content; may contain arbitrary bytes
    std::string filename;     // non-empty -> sent as a file part
    std::string contentType;  // empty -> omitted for fields,
                              // "application/octet-stream" for file parts
};

struct MultipartBody {
    std::string contentType;  // "multipart/form-data; boundary=..."
    std::string body;
};

// Compose the parts into one body. The boundary is chosen so it does not
// occur in any part's content or headers; pass a fixed `boundary` only in
// tests that assert on the exact bytes (it is used verbatim, so a caller-
// supplied boundary that collides with the content produces an invalid
// body).
MultipartBody BuildMultipartBody(const std::vector<MultipartPart>& parts,
                                 const std::string& boundary = "");

} // namespace UltraAI
