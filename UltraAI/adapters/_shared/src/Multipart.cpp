// UltraAI/adapters/_shared/src/Multipart.cpp
// Implementation of BuildMultipartBody (RFC 7578).
// Version: 0.1.0
// Last Modified: 2026-08-24
// Author: UltraAI Module

#include "UltraAIMultipart.h"

#include <atomic>
#include <string>

namespace UltraAI {

namespace {

// A quoted-string parameter cannot carry a raw '"', CR or LF; strip them
// rather than emit a header a server would reject or mis-parse.
std::string SanitizeParam(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        if (c != '"' && c != '\r' && c != '\n') out += c;
    }
    return out;
}

bool OccursInParts(const std::vector<MultipartPart>& parts,
                   const std::string& needle) {
    for (const auto& part : parts) {
        if (part.value.find(needle) != std::string::npos) return true;
        if (part.name.find(needle) != std::string::npos) return true;
        if (part.filename.find(needle) != std::string::npos) return true;
        if (part.contentType.find(needle) != std::string::npos) return true;
    }
    return false;
}

// Boundaries only have to be unique within one body, so a process-wide
// counter is enough — and unlike a random string it keeps recorded
// cassettes reproducible for a given call sequence.
std::string NextBoundary(const std::vector<MultipartPart>& parts) {
    static std::atomic<unsigned long long> counter{0};
    for (int attempt = 0; attempt < 64; ++attempt) {
        std::string candidate =
            "----UltraAIFormBoundary" +
            std::to_string(counter.fetch_add(1) + 1) + "x" +
            std::to_string(attempt);
        if (!OccursInParts(parts, candidate)) return candidate;
    }
    // 64 collisions means the content is adversarial; the caller gets a
    // boundary that is still syntactically valid.
    return "----UltraAIFormBoundaryFallback";
}

} // namespace

MultipartBody BuildMultipartBody(const std::vector<MultipartPart>& parts,
                                 const std::string& boundary) {
    MultipartBody out;
    const std::string mark =
        boundary.empty() ? NextBoundary(parts) : boundary;

    out.contentType = "multipart/form-data; boundary=" + mark;

    std::string body;
    for (const auto& part : parts) {
        body += "--" + mark + "\r\n";
        body += "Content-Disposition: form-data; name=\"" +
                SanitizeParam(part.name) + "\"";
        if (!part.filename.empty()) {
            body += "; filename=\"" + SanitizeParam(part.filename) + "\"";
        }
        body += "\r\n";

        std::string contentType = part.contentType;
        if (contentType.empty() && !part.filename.empty()) {
            contentType = "application/octet-stream";
        }
        if (!contentType.empty()) {
            body += "Content-Type: " + SanitizeParam(contentType) + "\r\n";
        }
        body += "\r\n";
        body += part.value;
        body += "\r\n";
    }
    body += "--" + mark + "--\r\n";

    out.body = std::move(body);
    return out;
}

} // namespace UltraAI
