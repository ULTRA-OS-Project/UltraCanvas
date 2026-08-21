// UltraAI/adapters/_shared/include/UltraAICassette.h
// On-disk cassette format for adapter tests (Docs/UltraNetIntegration.md
// §11): a JSON file holding a sequence of scripted transport exchanges —
// responses, transport errors, and SSE streams — that LoadCassette replays
// through a ScriptedTransport, so CI runs cloud-adapter tests against
// recorded traffic instead of a live provider. RecordingTransport captures
// such a file from real traffic: it delegates to an inner transport,
// remembers every completed exchange, and Save() writes the cassette.
// Requests are deliberately NOT stored (they carry Authorization headers);
// a cassette contains only what the server sent back.
//
// Only compiled when the vendored nlohmann/json is present
// (ULTRAAI_HAS_CASSETTE=1, defined by the _shared CMakeLists).
// Version: 0.1.0
// Last Modified: 2026-08-21
// Author: UltraAI Module
#pragma once

#include "UltraAITransport.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace UltraAI {

// Read `path` and script its exchanges, in file order, onto `transport`.
// Returns false with *outErrorMessage set when the file is missing,
// unparsable, or not a cassette.
bool LoadCassette(const std::string& path, ScriptedTransport& transport,
                  std::string* outErrorMessage = nullptr);

// ITransport wrapper that records the traffic it relays. Point an adapter
// at RecordingTransport(UltraNetTransport) once, run the scenario against
// the live provider, then Save() the cassette for offline replay.
class RecordingTransport : public ITransport {
public:
    explicit RecordingTransport(std::shared_ptr<ITransport> inner);

    TransportResponse Request(const TransportRequest& request,
                              Error* outError) override;
    CancelFn SseStream(const TransportRequest& request,
                       SseEventCallback onEvent,
                       SseCompleteCallback onComplete) override;

    // Write everything recorded so far as a cassette file. SSE exchanges
    // commit when their terminal callback fires; a stream still in flight
    // is not written.
    bool Save(const std::string& path,
              std::string* outErrorMessage = nullptr) const;

private:
    struct Recorded {
        bool isError = false;
        bool isSse   = false;
        TransportResponse response;
        Error error;
        std::vector<TransportSseEvent> events;
        int sseStatusCode = 200;
    };

    std::shared_ptr<ITransport> inner_;
    mutable std::mutex mu_;
    std::vector<Recorded> exchanges_;
};

} // namespace UltraAI
