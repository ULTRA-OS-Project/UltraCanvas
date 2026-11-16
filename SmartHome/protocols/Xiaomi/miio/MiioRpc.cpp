// MiioRpc.cpp
// The JSON-RPC carried in miIO packets, and its MIoT-spec form.
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "MiioRpc.h"

namespace UltraCanvas {
namespace SmartHome {
namespace Miio {

namespace {

bool ReadId(const JSONValue& v, uint32_t& out) {
    if (!v.IsInteger()) return false;
    const int64_t n = v.GetInteger();
    if (n < 0 || n > 0xFFFFFFFFLL) return false;
    out = static_cast<uint32_t>(n);
    return true;
}

} // namespace

std::string BuildRequest(uint32_t id, const std::string& method, const JSONValue& params) {
    JSONValue request = JSONValue::MakeObject();
    request.Set("id", static_cast<int64_t>(id));
    request.Set("method", method);
    request.Set("params", params.IsNull() ? JSONValue::MakeArray() : params);
    return JSON::Serialize(request);
}

bool ParseReply(const std::string& json, Reply& out, std::string& error) {
    JSONParseResult status;
    const JSONValue doc = JSON::Parse(json, &status);
    if (!status.success || !doc.IsObject()) {
        error = "reply is not a JSON object";
        return false;
    }
    Reply reply;
    if (!ReadId(doc.Get("id"), reply.id)) {
        error = "reply has no id";
        return false;
    }
    if (const JSONValue* result = doc.Find("result")) {
        reply.ok = true;
        reply.result = *result;
    } else if (const JSONValue* err = doc.Find("error")) {
        reply.ok = false;
        // Usually {"code":-5001,"message":"..."}; some firmware sends a bare string.
        reply.errorCode = err->Get("code").GetInteger(0);
        reply.errorMessage = err->IsString() ? err->GetString() : err->Get("message").GetString();
        if (reply.errorMessage.empty()) reply.errorMessage = "device error " + std::to_string(reply.errorCode);
    } else {
        error = "reply has neither result nor error";
        return false;
    }
    out = std::move(reply);
    return true;
}

JSONValue GetPropertiesParams(const std::string& did, const std::vector<PropertyRef>& properties) {
    JSONValue params = JSONValue::MakeArray();
    for (const PropertyRef& p : properties) {
        params.Append(JSONValue::MakeObject()
                          .Set("did", did)
                          .Set("siid", static_cast<int64_t>(p.siid))
                          .Set("piid", static_cast<int64_t>(p.piid)));
    }
    return params;
}

JSONValue SetPropertiesParams(const std::string& did,
                              const std::vector<std::pair<PropertyRef, JSONValue>>& values) {
    JSONValue params = JSONValue::MakeArray();
    for (const auto& [p, value] : values) {
        params.Append(JSONValue::MakeObject()
                          .Set("did", did)
                          .Set("siid", static_cast<int64_t>(p.siid))
                          .Set("piid", static_cast<int64_t>(p.piid))
                          .Set("value", value));
    }
    return params;
}

JSONValue ActionParams(const std::string& did, uint32_t siid, uint32_t aiid, const JSONValue& in) {
    return JSONValue::MakeObject()
        .Set("did", did)
        .Set("siid", static_cast<int64_t>(siid))
        .Set("aiid", static_cast<int64_t>(aiid))
        .Set("in", in.IsArray() ? in : JSONValue::MakeArray());
}

bool ParsePropertyResults(const JSONValue& result, std::vector<PropertyResult>& out, std::string& error) {
    if (!result.IsArray()) {
        error = "property results are not an array";
        return false;
    }
    std::vector<PropertyResult> parsed;
    for (const JSONValue& item : result.GetElements()) {
        if (!item.IsObject() || !item.Get("siid").IsInteger() || !item.Get("piid").IsInteger()) {
            error = "a property result without siid / piid";
            return false;
        }
        PropertyResult r;
        r.siid = static_cast<uint32_t>(item.Get("siid").GetInteger());
        r.piid = static_cast<uint32_t>(item.Get("piid").GetInteger());
        r.code = item.Get("code").GetInteger(0);
        r.value = item.Get("value");
        parsed.push_back(std::move(r));
    }
    out = std::move(parsed);
    return true;
}

bool ParseActionResult(const JSONValue& result, int64_t& code, JSONValue& out, std::string& error) {
    if (!result.IsObject()) {
        error = "action result is not an object";
        return false;
    }
    code = result.Get("code").GetInteger(0);
    out = result.Get("out");
    return true;
}

} // namespace Miio
} // namespace SmartHome
} // namespace UltraCanvas
