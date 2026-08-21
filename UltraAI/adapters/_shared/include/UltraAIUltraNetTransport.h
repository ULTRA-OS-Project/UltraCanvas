// UltraAI/adapters/_shared/include/UltraAIUltraNetTransport.h
// ITransport backed by UltraNet (UltraNet_HttpRequest /
// UltraNet_SseStreamAsync). Only available when the UltraAI module is
// built with ULTRAAI_USE_ULTRANET=ON and the UltraNet target is visible;
// the header guards itself so including it unconditionally is safe.
// Version: 0.1.0
// Last Modified: 2026-08-21
// Author: UltraAI Module
#pragma once

#ifdef ULTRAAI_HAS_ULTRANET

#include "UltraAITransport.h"

namespace UltraAI {

class UltraNetTransport : public ITransport {
public:
    TransportResponse Request(const TransportRequest& request,
                              Error* outError) override;

    // Callbacks fire on the UltraNet curl_multi worker thread — see
    // Docs/UltraNetIntegration.md §5 for the rules. When the server answers
    // an SSE request with an HTTP error status, no events fire and
    // onComplete receives MapHttpStatus(status).
    CancelFn SseStream(const TransportRequest& request,
                       SseEventCallback onEvent,
                       SseCompleteCallback onComplete) override;
};

} // namespace UltraAI

#endif // ULTRAAI_HAS_ULTRANET
