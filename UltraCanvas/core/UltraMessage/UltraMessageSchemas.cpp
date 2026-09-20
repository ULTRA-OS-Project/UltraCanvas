// UltraCanvas/core/UltraMessage/UltraMessageSchemas.cpp
// The topic schema registry (§5.3): the well-known topics every feed consumer
// is written against, vendor registrations, validation and the persistence
// decision the journal makes from it. Deliberately lightweight — field names,
// types and required-ness — rather than JSON Schema.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraMessageInternal.h"

#include <mutex>
#include <vector>

namespace {

using UltraCanvas::JSONValue;

struct Registry {
    std::mutex mutex;
    std::vector<UltraMsgTopicSchema> schemas;
};

UltraMsgFieldSpec Field(const char* name, const char* type, bool required = false) {
    UltraMsgFieldSpec f;
    f.name = name;
    f.type = type;
    f.required = required;
    return f;
}

UltraMsgTopicSchema Schema(const char* topic, bool persistent, const char* description,
                           std::vector<UltraMsgFieldSpec> fields) {
    UltraMsgTopicSchema s;
    s.topic = topic;
    s.version = 1;
    s.persistent = persistent;
    s.description = description;
    s.fields = std::move(fields);
    return s;
}

std::vector<UltraMsgTopicSchema> WellKnownSchemas() {
    return {
        Schema(UltraMsgTopics::MessagingMessage, true,
               "A chat message from any messenger service",
               {Field("service", "string", true), Field("account", "string"),
                Field("conversation", "object", true), Field("sender", "object", true),
                Field("text", "string"), Field("html", "string"),
                Field("direction", "string"), Field("read", "boolean"),
                Field("externalId", "string"), Field("replyTo", "string"),
                Field("attachments", "array")}),
        Schema(UltraMsgTopics::MailMessage, true,
               "An e-mail message header and snippet",
               {Field("account", "string", true), Field("folder", "string"),
                Field("from", "object", true), Field("to", "array"),
                Field("subject", "string", true), Field("snippet", "string"),
                Field("hasAttachments", "boolean"), Field("read", "boolean"),
                Field("flagged", "boolean"), Field("externalId", "string"),
                Field("threadId", "string")}),
        Schema(UltraMsgTopics::SystemNotification, true,
               "A desktop notification any application raised",
               {Field("appId", "string"), Field("appName", "string", true),
                Field("category", "string"), Field("summary", "string", true),
                Field("body", "string"), Field("icon", "string"),
                Field("urgency", "string"), Field("actions", "array"),
                Field("origin", "string")}),
        Schema(UltraMsgTopics::SystemNotificationAction, false,
               "The feed invoking a notification's button",
               {Field("notificationId", "string", true), Field("actionId", "string", true)}),
        Schema(UltraMsgTopics::SystemNotificationDismissed, false,
               "A notification was dismissed",
               {Field("notificationId", "string", true), Field("reason", "string")}),
        Schema(UltraMsgTopics::FeedRead, false, "The user read a journaled message",
               {Field("messageId", "string", true)}),
        Schema(UltraMsgTopics::FeedDismissed, false, "The user dismissed a journaled message",
               {Field("messageId", "string", true)}),
        Schema(UltraMsgTopics::AppLifecycleStarted, false, "An application connected",
               {Field("appId", "string", true), Field("instanceId", "string", true),
                Field("displayName", "string"), Field("commands", "boolean")}),
        Schema(UltraMsgTopics::AppLifecycleStopping, false, "An application is leaving",
               {Field("appId", "string", true), Field("instanceId", "string", true)}),
        Schema(UltraMsgTopics::AppOpenRequest, false,
               "Open these paths or URLs (single-instance hand-off)",
               {Field("paths", "array"), Field("urls", "array"), Field("activate", "boolean")}),
        Schema(UltraMsgTopics::AppCommandList, false, "List an application's commands", {}),
        Schema(UltraMsgTopics::AppCommandInvoke, false, "Invoke a command",
               {Field("verb", "string", true), Field("args", "object"), Field("dryRun", "boolean")}),
        Schema(UltraMsgTopics::AppCommandEcho, false,
               "The broker's echo of a routed invocation (recorder role)",
               {Field("caller", "string", true), Field("target", "string", true),
                Field("verb", "string", true), Field("args", "object"), Field("ok", "boolean")}),
        Schema(UltraMsgTopics::FileChanged, false, "A file changed on disk",
               {Field("path", "string", true), Field("change", "string", true),
                Field("by", "string")}),
        Schema(UltraMsgTopics::ClipboardChanged, false, "The clipboard changed",
               {Field("formats", "array")}),
    };
}

Registry& GetRegistry() {
    static Registry* registry = [] {
        auto* r = new Registry();
        r->schemas = WellKnownSchemas();
        return r;
    }();
    return *registry;
}

bool TypeMatches(const std::string& type, const JSONValue& value) {
    if (type == "string") return value.IsString();
    if (type == "integer") return value.IsInteger();
    if (type == "number") return value.IsNumber();
    if (type == "boolean") return value.IsBoolean();
    if (type == "object") return value.IsObject();
    if (type == "array") return value.IsArray();
    return true; // unknown type name: no constraint
}

// Exact topic first, then the most specific matching pattern.
const UltraMsgTopicSchema* FindLocked(Registry& registry, const std::string& topic) {
    const UltraMsgTopicSchema* best = nullptr;
    size_t bestLength = 0;
    for (const auto& schema : registry.schemas) {
        if (schema.topic == topic) return &schema;
        if (UltraMessage::Internal::TopicMatches(schema.topic, topic) &&
            schema.topic.size() >= bestLength) {
            best = &schema;
            bestLength = schema.topic.size();
        }
    }
    return best;
}

} // namespace

UltraMsgResult UltraMsg_RegisterSchema(const UltraMsgTopicSchema& schema) {
    if (!UltraMessage::Internal::IsValidPattern(schema.topic))
        return UltraMsgResult::Error(UltraMsgResultCode::InvalidArgument,
                                     "invalid topic or pattern: " + schema.topic);
    for (const auto& field : schema.fields) {
        if (field.name.empty())
            return UltraMsgResult::Error(UltraMsgResultCode::InvalidArgument,
                                         "schema field without a name");
    }
    Registry& registry = GetRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    for (auto& existing : registry.schemas) {
        if (existing.topic == schema.topic) {
            existing = schema;
            return UltraMsgResult::Ok();
        }
    }
    registry.schemas.push_back(schema);
    return UltraMsgResult::Ok();
}

bool UltraMsg_GetSchema(const std::string& topic, UltraMsgTopicSchema& out) {
    Registry& registry = GetRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    if (const UltraMsgTopicSchema* found = FindLocked(registry, topic)) {
        out = *found;
        return true;
    }
    return false;
}

std::vector<UltraMsgTopicSchema> UltraMsg_ListSchemas() {
    Registry& registry = GetRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    return registry.schemas;
}

UltraMsgResult UltraMsg_Validate(const std::string& topic, const JSONValue& body) {
    UltraMsgTopicSchema schema;
    if (!UltraMsg_GetSchema(topic, schema)) return UltraMsgResult::Ok();
    if (!body.IsObject())
        return UltraMsgResult::Error(UltraMsgResultCode::SchemaViolation,
                                     topic + ": body must be an object");
    for (const auto& field : schema.fields) {
        const JSONValue* value = body.Find(field.name);
        if (!value || value->IsNull()) {
            if (field.required)
                return UltraMsgResult::Error(UltraMsgResultCode::SchemaViolation,
                                             topic + ": missing required field '" + field.name + "'");
            continue;
        }
        if (!TypeMatches(field.type, *value))
            return UltraMsgResult::Error(UltraMsgResultCode::SchemaViolation,
                                         topic + ": field '" + field.name + "' must be " + field.type);
    }
    return UltraMsgResult::Ok();
}

bool UltraMsg_IsPersistentTopic(const std::string& topic) {
    UltraMsgTopicSchema schema;
    return UltraMsg_GetSchema(topic, schema) && schema.persistent;
}
