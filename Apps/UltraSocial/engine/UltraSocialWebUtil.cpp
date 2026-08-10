// Apps/UltraSocial/engine/UltraSocialWebUtil.cpp
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSocialWebUtil.h"

#include <UltraNet/UltraNetOAuth2.h>   // UltraNet_OAuth2GenerateState (boundary)
#include <UltraNet/UltraNetUrl.h>      // UltraNet_UrlEncode (form bodies)

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

using UltraCanvas::JSONValue;

namespace UltraSocial {

namespace {

// Pulls the server-reported error out of a parsed error body. The
// networks disagree on the shape:
//   Mastodon   {"error": "..."}                       (error_description too)
//   Bluesky    {"error": "Name", "message": "..."}
//   Telegram   {"ok": false, "description": "..."}
//   X          {"title": "Unauthorized", "detail": "...", "status": 401}
std::string ServerErrorMessage(const JSONValue& doc) {
    std::string message;
    if (doc.Contains("error")) {
        message = doc["error"].GetString("");
        if (doc.Contains("message")) {
            message += ": " + doc["message"].GetString("");
        } else if (doc.Contains("error_description")) {
            message += ": " + doc["error_description"].GetString("");
        }
    } else if (doc.Contains("description")) {
        message = doc["description"].GetString("");
    } else if (doc.Contains("title")) {
        message = doc["title"].GetString("");
        if (doc.Contains("detail")) {
            message += ": " + doc["detail"].GetString("");
        }
    }
    return message;
}

UltraNetResult FinishJsonExchange(UltraNetResult transport,
                                  const UltraNetResponse& response,
                                  JSONValue& outDoc,
                                  int* outHttpStatus) {
    if (outHttpStatus) *outHttpStatus = response.statusCode;

    UltraCanvas::JSONParseResult parsed;
    JSONValue doc = UltraCanvas::JSON::Parse(response.GetBodyAsString(), &parsed);
    if (parsed.success) outDoc = doc;

    if (!transport) {
        // Prefer the server's own error text over a bare "HTTP 422".
        if (parsed.success) {
            std::string serverError = ServerErrorMessage(doc);
            if (!serverError.empty()) {
                auto r = UltraNetResult::Error(transport.code, serverError);
                r.httpStatus = response.statusCode;
                return r;
            }
        }
        return transport;
    }
    return transport;
}

} // namespace

UltraNetResult JsonRequest(UltraNetHttpMethod method,
                           const std::string& url,
                           const JSONValue& body,
                           const std::string& bearer,
                           JSONValue& outDoc,
                           int* outHttpStatus,
                           const std::vector<std::pair<std::string, std::string>>&
                               extraHeaders) {
    outDoc = JSONValue();

    UltraNetHttpRequest request;
    request.url    = url;
    request.method = method;
    request.headers.Set("accept", "application/json");
    if (!bearer.empty()) {
        request.headers.Set("authorization", "Bearer " + bearer);
    }
    for (const auto& [name, value] : extraHeaders) {
        request.headers.Set(name, value);
    }
    if (body.GetType() != UltraCanvas::JSONType::Null) {
        std::string serialized = UltraCanvas::JSON::Serialize(body);
        request.headers.Set("content-type", "application/json");
        request.body.assign(serialized.begin(), serialized.end());
    }

    UltraNetResponse response;
    auto transport = UltraNet_HttpRequest(request, response);
    return FinishJsonExchange(transport, response, outDoc, outHttpStatus);
}

UltraNetResult FormPost(const std::string& url,
                        const std::vector<std::pair<std::string, std::string>>&
                            fields,
                        const std::string& bearer,
                        const std::string& basicUser,
                        const std::string& basicPassword,
                        JSONValue& outDoc,
                        int* outHttpStatus) {
    outDoc = JSONValue();

    std::string body;
    for (const auto& [key, value] : fields) {
        if (!body.empty()) body += '&';
        body += UltraNet_UrlEncode(key);
        body += '=';
        body += UltraNet_UrlEncode(value);
    }

    UltraNetHttpRequest request;
    request.url    = url;
    request.method = UltraNetHttpMethod::Post;
    request.body.assign(body.begin(), body.end());
    request.headers.Set("accept", "application/json");
    request.headers.Set("content-type", "application/x-www-form-urlencoded");
    if (!bearer.empty()) {
        request.headers.Set("authorization", "Bearer " + bearer);
    } else if (!basicUser.empty()) {
        request.options.authType = UltraNetAuthType::Basic;
        request.options.credentials.type     = UltraNetAuthType::Basic;
        request.options.credentials.username = basicUser;
        request.options.credentials.password = basicPassword;
    }

    UltraNetResponse response;
    auto transport = UltraNet_HttpRequest(request, response);
    return FinishJsonExchange(transport, response, outDoc, outHttpStatus);
}

UltraNetResult RawPost(const std::string& url,
                       const std::vector<uint8_t>& body,
                       const std::string& contentType,
                       const std::string& bearer,
                       JSONValue& outDoc,
                       int* outHttpStatus) {
    outDoc = JSONValue();

    UltraNetHttpRequest request;
    request.url    = url;
    request.method = UltraNetHttpMethod::Post;
    request.body   = body;
    request.headers.Set("accept", "application/json");
    request.headers.Set("content-type", contentType);
    if (!bearer.empty()) {
        request.headers.Set("authorization", "Bearer " + bearer);
    }

    UltraNetResponse response;
    auto transport = UltraNet_HttpRequest(request, response);
    return FinishJsonExchange(transport, response, outDoc, outHttpStatus);
}

std::vector<uint8_t> BuildMultipartBody(const std::vector<MultipartField>& fields,
                                        const std::vector<MultipartFile>& files,
                                        std::string& outContentType) {
    // Random boundary; the generator's base64url alphabet is header-safe.
    std::string boundary =
        "----UltraSocial" + UltraNet_OAuth2GenerateState(18);
    outContentType = "multipart/form-data; boundary=" + boundary;

    std::string head;
    std::vector<uint8_t> body;
    auto append = [&body](const std::string& s) {
        body.insert(body.end(), s.begin(), s.end());
    };

    for (const auto& field : fields) {
        append("--" + boundary + "\r\n"
               "Content-Disposition: form-data; name=\"" + field.name + "\"\r\n"
               "\r\n" + field.value + "\r\n");
    }
    for (const auto& file : files) {
        append("--" + boundary + "\r\n"
               "Content-Disposition: form-data; name=\"" + file.name +
               "\"; filename=\"" + file.fileName + "\"\r\n"
               "Content-Type: " + file.contentType + "\r\n"
               "\r\n");
        body.insert(body.end(), file.bytes.begin(), file.bytes.end());
        append("\r\n");
    }
    append("--" + boundary + "--\r\n");
    return body;
}

UltraNetResult MultipartPost(const std::string& url,
                             const std::vector<MultipartField>& fields,
                             const std::vector<MultipartFile>& files,
                             const std::string& bearer,
                             JSONValue& outDoc,
                             int* outHttpStatus) {
    std::string contentType;
    auto body = BuildMultipartBody(fields, files, contentType);
    return RawPost(url, body, contentType, bearer, outDoc, outHttpStatus);
}

UltraNetResult LoadFileBytes(const std::string& path,
                             std::vector<uint8_t>& outBytes) {
    outBytes.clear();
    std::ifstream is(path, std::ios::binary);
    if (!is) {
        return UltraNetResult::Error(UltraNetResultCode::NotFound,
                                     "cannot read media file: " + path);
    }
    outBytes.assign(std::istreambuf_iterator<char>(is),
                    std::istreambuf_iterator<char>());
    return UltraNetResult::Ok();
}

std::string GuessMimeType(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".png")  return "image/png";
    if (ext == ".gif")  return "image/gif";
    if (ext == ".webp") return "image/webp";
    if (ext == ".mp4")  return "video/mp4";
    if (ext == ".webm") return "video/webm";
    return "application/octet-stream";
}

std::string JoinUrl(const std::string& base, const std::string& path) {
    if (base.empty()) return path;
    if (!base.empty() && base.back() == '/') {
        return base.substr(0, base.size() - 1) + path;
    }
    return base + path;
}

} // namespace UltraSocial
