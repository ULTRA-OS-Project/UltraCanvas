// UltraAI/runtime/src/LocalRuntimeTopics.cpp
// Topics: the server's topics.json in, the user's topic preferences in and
// out, and the one decision of which model a topic uses on this computer.
// Formats: Docs/Modules/UltraAI/LocalRuntime.md ("Topics").
// Version: 0.1.0
// Last Modified: 2026-11-06
// Author: UltraAI Module

#include "LocalRuntimeInternal.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace UltraAI {

std::string ModelTopic::ServerDefault() const {
    for (const auto& m : models) {
        if (m.isDefault) return m.modelId;
    }
    return models.empty() ? std::string() : models.front().modelId;
}

namespace local_detail {

using nlohmann::json;

namespace {

std::string Str(const json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

const LocalModelInfo* FindIn(const std::vector<LocalModelInfo>& models, const std::string& id) {
    for (const auto& m : models) {
        if (m.id == id) return &m;
    }
    return nullptr;
}

bool Usable(const LocalModelInfo* m) {
    return m && m->fit != ModelFit::TooLarge;
}

} // namespace

const ModelTopic* Topics::Find(const std::string& topicId) const {
    for (const auto& t : topics) {
        if (t.id == topicId) return &t;
    }
    return nullptr;
}

bool ParseTopics(const std::string& text, Topics& out, std::string& outError) {
    out = Topics{};
    json doc = json::parse(text, nullptr, false);
    if (!doc.is_object()) { outError = "the topics document is not a JSON object"; return false; }
    auto version = doc.find("formatVersion");
    out.formatVersion = version != doc.end() && version->is_number_integer() ? version->get<int>() : 0;
    if (out.formatVersion != 1) {
        outError = "the topics document has formatVersion " + std::to_string(out.formatVersion) +
                   "; this runtime reads 1";
        return false;
    }
    out.published = Str(doc, "published");
    auto topics = doc.find("topics");
    if (topics == doc.end() || !topics->is_array()) {
        outError = "the topics document has no \"topics\" list";
        return false;
    }
    for (const auto& t : *topics) {
        ModelTopic topic;
        topic.id = Str(t, "id");
        topic.capability = Str(t, "capability");
        if (topic.id.empty() || topic.capability.empty()) {
            outError = "a topic has no \"id\" or no \"capability\"";
            return false;
        }
        topic.name = Str(t, "name");
        if (topic.name.empty()) topic.name = topic.id;
        topic.description = Str(t, "description");
        auto models = t.find("models");
        if (models != t.end() && models->is_array()) {
            for (const auto& m : *models) {
                TopicModelRef ref;
                if (m.is_string()) {
                    ref.modelId = m.get<std::string>();
                } else if (m.is_object()) {
                    ref.modelId = Str(m, "id");
                    auto d = m.find("default");
                    ref.isDefault = d != m.end() && d->is_boolean() && d->get<bool>();
                }
                if (!ref.modelId.empty()) topic.models.push_back(std::move(ref));
            }
        }
        out.topics.push_back(std::move(topic));
    }
    return true;
}

std::string SerializeTopicPreferences(const TopicPreferences& p) {
    json j;
    j["newModels"] = ToString(p.newModels);
    json choices = json::object();
    for (const auto& [topicId, choice] : p.choices) {
        json c;
        c["model"] = choice.modelId;
        c["autoInstall"] = choice.autoInstall;
        choices[topicId] = std::move(c);
    }
    j["topics"] = std::move(choices);
    j["seenModels"] = p.seenModels;
    return j.dump(2);
}

bool ParseTopicPreferences(const std::string& text, TopicPreferences& out, bool& outHadSeenList) {
    outHadSeenList = false;
    json j = json::parse(text, nullptr, false);
    if (!j.is_object()) return false;
    TopicPreferences p;
    NewModelPolicy policy;
    if (ParseNewModelPolicy(Str(j, "newModels"), policy)) p.newModels = policy;
    auto topics = j.find("topics");
    if (topics != j.end() && topics->is_object()) {
        for (auto it = topics->begin(); it != topics->end(); ++it) {
            if (!it.value().is_object()) continue;
            TopicChoice c;
            c.modelId = Str(it.value(), "model");
            auto a = it.value().find("autoInstall");
            if (a != it.value().end() && a->is_boolean()) c.autoInstall = a->get<bool>();
            p.choices[it.key()] = std::move(c);
        }
    }
    auto seen = j.find("seenModels");
    if (seen != j.end() && seen->is_array()) {
        outHadSeenList = true;
        for (const auto& s : *seen) {
            if (s.is_string()) p.seenModels.push_back(s.get<std::string>());
        }
    }
    out = std::move(p);
    return true;
}

std::string ResolveTopicModel(const ModelTopic& topic, const TopicChoice* choice,
                              const std::vector<LocalModelInfo>& models,
                              const std::string& fallback) {
    // 1. What the user picked, while it exists and fits.
    if (choice && !choice->modelId.empty()) {
        const LocalModelInfo* m = FindIn(models, choice->modelId);
        if (Usable(m) && m->capability == topic.capability) return m->id;
    }
    // 2. The server's default.
    const std::string serverDefault = topic.ServerDefault();
    if (Usable(FindIn(models, serverDefault))) {
        const LocalModelInfo* m = FindIn(models, serverDefault);
        // Installed, or installable: when the user follows the server and
        // lets it download, the default is what gets installed.
        const bool follows = !choice || choice->modelId.empty();
        const bool mayInstall = follows && (!choice || choice->autoInstall);
        if (m->installed || mayInstall) return m->id;
    }
    // 3. A model the topic offers that is installed.
    for (const auto& ref : topic.models) {
        const LocalModelInfo* m = FindIn(models, ref.modelId);
        if (Usable(m) && m->installed) return m->id;
    }
    // 4. The server's default even though it is not installed, then the
    //    first offered model that fits.
    if (Usable(FindIn(models, serverDefault))) return serverDefault;
    for (const auto& ref : topic.models) {
        if (Usable(FindIn(models, ref.modelId))) return ref.modelId;
    }
    return fallback;
}

} // namespace local_detail

const char* ToString(NewModelPolicy policy) {
    switch (policy) {
        case NewModelPolicy::Ignore:    return "ignore";
        case NewModelPolicy::Notify:    return "notify";
        case NewModelPolicy::Automatic: return "automatic";
    }
    return "notify";
}

bool ParseNewModelPolicy(const std::string& text, NewModelPolicy& out) {
    if (text == "ignore")    { out = NewModelPolicy::Ignore;    return true; }
    if (text == "notify")    { out = NewModelPolicy::Notify;    return true; }
    if (text == "automatic") { out = NewModelPolicy::Automatic; return true; }
    return false;
}

} // namespace UltraAI
