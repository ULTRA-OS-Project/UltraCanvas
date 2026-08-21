// UltraAI/adapters/_shared/src/UltraNetTransport.cpp
// Implementation of UltraNetTransport. Compiled only when the module is
// built with ULTRAAI_USE_ULTRANET=ON and the UltraNet target is visible.
// Version: 0.1.0
// Last Modified: 2026-08-21
// Author: UltraAI Module

#ifdef ULTRAAI_HAS_ULTRANET

#include "UltraAIUltraNetTransport.h"
#include "UltraAIHttpError.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetHttp.h>
#include <UltraNet/UltraNetSse.h>

#include <algorithm>
#include <cctype>

namespace UltraAI {

namespace {

std::string UpperCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return s;
}

UltraNetHttpRequest BuildNetRequest(const TransportRequest& request) {
    UltraNetHttpRequest net;
    net.url = request.url;

    const std::string method = UpperCopy(request.method);
    if      (method == "GET")     net.method = UltraNetHttpMethod::Get;
    else if (method == "POST")    net.method = UltraNetHttpMethod::Post;
    else if (method == "PUT")     net.method = UltraNetHttpMethod::Put;
    else if (method == "DELETE")  net.method = UltraNetHttpMethod::Delete;
    else if (method == "HEAD")    net.method = UltraNetHttpMethod::Head;
    else if (method == "PATCH")   net.method = UltraNetHttpMethod::Patch;
    else if (method == "OPTIONS") net.method = UltraNetHttpMethod::Options;
    else {
        net.method       = UltraNetHttpMethod::Custom;
        net.customMethod = method;
    }

    for (const auto& kv : request.headers) {
        net.headers.Add(kv.first, kv.second);
    }
    net.body.assign(request.body.begin(), request.body.end());
    net.options.timeoutMs = request.timeoutMs;
    return net;
}

TransportResponse ConvertResponse(const UltraNetResponse& netResp) {
    TransportResponse out;
    out.statusCode = netResp.statusCode;
    out.headers    = netResp.headers.Entries();
    out.body       = netResp.GetBodyAsString();
    return out;
}

Error MapNetError(const UltraNetResult& result) {
    Error e;
    switch (result.code) {
        case UltraNetResultCode::Success:
            return e;
        case UltraNetResultCode::Cancelled:
            e.code = ErrorCode::Cancelled; break;
        case UltraNetResultCode::Timeout:
        case UltraNetResultCode::ConnectionTimeout:
            e.code = ErrorCode::Timeout; break;
        case UltraNetResultCode::AuthenticationRequired:
        case UltraNetResultCode::AuthenticationFailed:
        case UltraNetResultCode::AccessDenied:
            e.code = ErrorCode::AuthenticationFailed; break;
        case UltraNetResultCode::InvalidUrl:
        case UltraNetResultCode::UnsupportedScheme:
            e.code = ErrorCode::InvalidRequest; break;
        case UltraNetResultCode::HostNotFound:
        case UltraNetResultCode::ConnectionRefused:
        case UltraNetResultCode::ConnectionReset:
        case UltraNetResultCode::SendFailed:
        case UltraNetResultCode::ReceiveFailed:
        case UltraNetResultCode::TlsHandshakeFailed:
        case UltraNetResultCode::TlsCertificateInvalid:
        case UltraNetResultCode::TlsCertificateExpired:
        case UltraNetResultCode::NotFound:
            e.code = ErrorCode::NetworkError; break;
        case UltraNetResultCode::HttpError:
            // Callers handle completed HTTP-error exchanges before calling
            // MapNetError; reaching here means an unexpected path.
            e.code = ErrorCode::ProviderError; break;
        default:
            e.code = ErrorCode::ProviderError; break;
    }
    e.message = result.message;
    return e;
}

} // namespace

TransportResponse UltraNetTransport::Request(const TransportRequest& request,
                                             Error* outError) {
    UltraNetHttpRequest net = BuildNetRequest(request);

    UltraNetResponse netResp;
    UltraNetResult result = UltraNet_HttpRequest(net, netResp);

    // HTTP >= 400 is a completed exchange: return the response for the
    // adapter to map (MapHttpStatus + provider error body).
    if (result || result.code == UltraNetResultCode::HttpError) {
        if (outError) *outError = {};
        return ConvertResponse(netResp);
    }

    if (outError) *outError = MapNetError(result);
    return {};
}

CancelFn UltraNetTransport::SseStream(const TransportRequest& request,
                                      SseEventCallback onEvent,
                                      SseCompleteCallback onComplete) {
    UltraNetHttpRequest net = BuildNetRequest(request);

    UltraNetHandle handle = UltraNet_SseStreamAsync(
        net,
        [onEvent](const UltraNetSseEvent& ev) {
            if (!onEvent) return;
            TransportSseEvent out;
            out.event   = ev.event;
            out.data    = ev.data;
            out.id      = ev.id;
            out.retryMs = ev.retry;
            onEvent(out);
        },
        [onComplete](const UltraNetResult& final) {
            if (!onComplete) return;
            if (final) {
                onComplete(Error{}, final.httpStatus);
            } else if (final.code == UltraNetResultCode::HttpError) {
                onComplete(MapHttpStatus(final.httpStatus, final.message),
                           final.httpStatus);
            } else {
                onComplete(MapNetError(final), final.httpStatus);
            }
        });

    if (handle == UltraNetInvalidHandle) {
        // Enqueue failed synchronously; surface it through onComplete so
        // the caller has exactly one completion path.
        if (onComplete) {
            Error e;
            e.code    = ErrorCode::NetworkError;
            e.message = "UltraNet_SseStreamAsync refused the request";
            onComplete(e, 0);
        }
        return [] {};
    }
    return [handle] { UltraNet_CancelRequest(handle); };
}

} // namespace UltraAI

#endif // ULTRAAI_HAS_ULTRANET
