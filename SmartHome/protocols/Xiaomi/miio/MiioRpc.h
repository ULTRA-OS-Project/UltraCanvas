// MiioRpc.h
// The JSON-RPC carried in miIO packets, and its MIoT-spec form.
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework
//
// A request is {"id":<n>,"method":<name>,"params":<array or object>}; the
// device answers with the same id and either "result" or
// "error":{"code":<n>,"message":<text>}. The id is how a reply is matched to
// its request - datagrams can be repeated, late or lost.
//
// Two families of methods ride on it:
//
//   * per-model methods, e.g. Roborock vacuums: "get_status", "app_start",
//     "app_pause", "app_charge", "find_me", "set_custom_mode" [fan level];
//   * MIoT spec (newer devices of every brand): properties and actions named
//     by service / property / action ids that miot-spec.org publishes per
//     model -
//       get_properties [{"did","siid","piid"}, ...]
//         -> [{"did","siid","piid","code":0,"value":...}, ...]
//       set_properties [{"did","siid","piid","value"}, ...]
//         -> [{"did","siid","piid","code":0}, ...]
//       action {"did","siid","aiid","in":[...]}
//         -> {"code":0,"out":[...]}
//     A "code" other than 0 is that one property's or action's error.
//
// Text in, text out; tests/MiioRpcTest.cpp.
#pragma once

#include "DataFormats/UltraCanvasJSON.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace UltraCanvas {
namespace SmartHome {
namespace Miio {

std::string BuildRequest(uint32_t id, const std::string& method, const JSONValue& params);

struct Reply {
    uint32_t id = 0;
    bool ok = false;               // "result" was present
    JSONValue result;              // when ok
    int64_t errorCode = 0;         // when !ok
    std::string errorMessage;      // when !ok
};

// False for text that is not a reply (no id, or neither result nor error).
bool ParseReply(const std::string& json, Reply& out, std::string& error);

// ----- MIoT spec -----

struct PropertyRef {
    uint32_t siid = 0;
    uint32_t piid = 0;
};

struct PropertyResult {
    uint32_t siid = 0;
    uint32_t piid = 0;
    int64_t code = 0;              // 0: read or written; otherwise the device's error code
    JSONValue value;               // get_properties only
};

// "did" is echoed back in each result; any text, conventionally the device id.
JSONValue GetPropertiesParams(const std::string& did, const std::vector<PropertyRef>& properties);
JSONValue SetPropertiesParams(const std::string& did,
                              const std::vector<std::pair<PropertyRef, JSONValue>>& values);
JSONValue ActionParams(const std::string& did, uint32_t siid, uint32_t aiid,
                       const JSONValue& in = JSONValue::MakeArray());

// The result array of get_properties / set_properties.
bool ParsePropertyResults(const JSONValue& result, std::vector<PropertyResult>& out, std::string& error);

// The result object of action: its code, and "out" when present.
bool ParseActionResult(const JSONValue& result, int64_t& code, JSONValue& out, std::string& error);

} // namespace Miio
} // namespace SmartHome
} // namespace UltraCanvas
