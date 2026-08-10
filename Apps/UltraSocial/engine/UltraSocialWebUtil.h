// Apps/UltraSocial/engine/UltraSocialWebUtil.h
// Shared HTTP plumbing for the connectors: JSON requests over UltraNet with
// the per-network error shapes surfaced, multipart/form-data body building
// for media uploads, and media-file loading with MIME-type guessing.
// Internal to the engine — connectors include it, apps don't.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DataFormats/UltraCanvasJSON.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetHttp.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace UltraSocial {

// One HTTP request/response with JSON payloads. `bearer` (if non-empty)
// becomes an `authorization: Bearer …` header. The response body is parsed
// into outDoc whenever it parses as JSON — on HTTP errors too, so callers
// can surface the server's error field. A body of JSONType::Null means
// "send no body".
UltraNetResult JsonRequest(UltraNetHttpMethod method,
                           const std::string& url,
                           const UltraCanvas::JSONValue& body,
                           const std::string& bearer,
                           UltraCanvas::JSONValue& outDoc,
                           int* outHttpStatus = nullptr,
                           const std::vector<std::pair<std::string, std::string>>&
                               extraHeaders = {},
                           UltraNetHttpHeaders* outResponseHeaders = nullptr);

// POST an application/x-www-form-urlencoded body. Auth: `bearer` when
// non-empty; else HTTP Basic when basicUser is non-empty (Reddit's token
// endpoint wants Basic with an empty password for installed apps).
UltraNetResult FormPost(const std::string& url,
                        const std::vector<std::pair<std::string, std::string>>&
                            fields,
                        const std::string& bearer,
                        const std::string& basicUser,
                        const std::string& basicPassword,
                        UltraCanvas::JSONValue& outDoc,
                        int* outHttpStatus = nullptr);

// POST with a raw body (uploads). Content type is required.
UltraNetResult RawPost(const std::string& url,
                       const std::vector<uint8_t>& body,
                       const std::string& contentType,
                       const std::string& bearer,
                       UltraCanvas::JSONValue& outDoc,
                       int* outHttpStatus = nullptr);

// multipart/form-data builder for media uploads.
struct MultipartField {
    std::string name;
    std::string value;
};
struct MultipartFile {
    std::string name;        // form field name, e.g. "file" / "photo"
    std::string fileName;    // basename presented to the server
    std::string contentType;
    std::vector<uint8_t> bytes;
};

// Builds the body and returns the matching `multipart/form-data; boundary=…`
// content-type string through outContentType.
std::vector<uint8_t> BuildMultipartBody(const std::vector<MultipartField>& fields,
                                        const std::vector<MultipartFile>& files,
                                        std::string& outContentType);

UltraNetResult MultipartPost(const std::string& url,
                             const std::vector<MultipartField>& fields,
                             const std::vector<MultipartFile>& files,
                             const std::string& bearer,
                             UltraCanvas::JSONValue& outDoc,
                             int* outHttpStatus = nullptr);

// Reads a media file into memory. False (with an error result) if missing.
UltraNetResult LoadFileBytes(const std::string& path,
                             std::vector<uint8_t>& outBytes);

// image/jpeg for .jpg/.jpeg, image/png for .png, … ;
// application/octet-stream when unknown.
std::string GuessMimeType(const std::string& path);

// Base URL + path join that tolerates a trailing slash on the base.
std::string JoinUrl(const std::string& base, const std::string& path);

} // namespace UltraSocial
