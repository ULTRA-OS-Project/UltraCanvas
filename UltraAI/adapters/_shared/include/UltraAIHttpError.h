// UltraAI/adapters/_shared/include/UltraAIHttpError.h
// Provider-neutral HTTP status → UltraAI::Error mapping, plus Retry-After
// parsing. Adapters call MapHttpStatus for any completed exchange with
// statusCode >= 400 and may refine the result with provider-specific error
// bodies afterwards.
// Version: 0.1.0
// Last Modified: 2026-08-21
// Author: UltraAI Module
#pragma once

#include "UltraAICommon.h"

#include <string>

namespace UltraAI {

// Map an HTTP status code to an Error. `detail` (typically a snippet of the
// response body) is appended to the message; Error::providerCode carries the
// numeric status as text. Statuses < 400 return ErrorCode::None.
Error MapHttpStatus(int statusCode, const std::string& detail = "");

// Parse a Retry-After header value into milliseconds. Only the
// delta-seconds form ("120") is supported; the HTTP-date form and anything
// unparsable return -1, meaning "use your backoff policy".
int ParseRetryAfterMs(const std::string& headerValue);

} // namespace UltraAI
