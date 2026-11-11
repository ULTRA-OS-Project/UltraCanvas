// Apps/UltraAIServerAdmin/cli/Commands.cpp
// The commands of `ultraai-serveradmin` (Commands.h).
// Version: 0.1.0
// Author: UltraAI Module

#include "Commands.h"

#include "ModelServerClient.h"

#include "UltraCanvasPathUtf8.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>

namespace UltraAIServerAdmin::Cli {

using UltraCanvas::JSON::Serialize;
using UltraCanvas::JSONSerializeOptions;

namespace {

struct Args {
    std::vector<std::string> positional;
    std::map<std::string, std::vector<std::string>> options;   // --name value (repeatable)
    std::vector<std::string> flags;                             // --tools

    std::string Option(const std::string& name, const std::string& fallback = {}) const {
        auto it = options.find(name);
        return it == options.end() || it->second.empty() ? fallback : it->second.back();
    }
    bool Flag(const std::string& name) const {
        return std::find(flags.begin(), flags.end(), name) != flags.end();
    }
};

// Options that take a value; anything else starting with "--" is a flag.
const std::vector<std::string> kValued = {"server", "token", "name", "capability", "engine", "license", "description",
                                          "memory", "gpu", "context", "dims", "tag", "arg", "sha256", "size"};

Args Parse(const std::vector<std::string>& in) {
    Args a;
    for (size_t i = 0; i < in.size(); ++i) {
        const std::string& s = in[i];
        if (s.rfind("--", 0) == 0 && s.size() > 2) {
            const std::string name = s.substr(2);
            if (std::find(kValued.begin(), kValued.end(), name) != kValued.end() && i + 1 < in.size()) {
                a.options[name].push_back(in[++i]);
            } else {
                a.flags.push_back(name);
            }
        } else {
            a.positional.push_back(s);
        }
    }
    return a;
}

int Print(const Reply& r) {
    if (!r.ok) {
        std::cerr << "error: " << r.message << (r.error.empty() ? "" : " (" + r.error + ")") << "\n";
        return 1;
    }
    JSONSerializeOptions pretty;
    pretty.pretty = true;
    pretty.indentWidth = 2;
    std::cout << Serialize(r.body, pretty) << "\n";
    return 0;
}

int Need(const Args& a, size_t count, const char* what) {
    if (a.positional.size() < count + 1) {
        std::cerr << "usage: ultraai-serveradmin " << what << "\n";
        return 2;
    }
    return 0;
}

uint64_t Number(const std::string& text) {
    if (text.empty()) return 0;
    char* end = nullptr;
    const unsigned long long v = std::strtoull(text.c_str(), &end, 10);
    return end && *end == '\0' ? static_cast<uint64_t>(v) : 0;
}

JSONValue Strings(const std::vector<std::string>& list) {
    JSONValue out = JSONValue::MakeArray();
    for (const auto& s : list) out.Append(JSONValue(s));
    return out;
}

} // namespace

int Usage(FILE* to) {
    std::fputs(
        "UltraAI ServerAdmin - manage an UltraAI model server\n"
        "\n"
        "  ultraai-serveradmin                       the window\n"
        "  ultraai-serveradmin [--server URL] [--token TOKEN] <command>\n"
        "\n"
        "The server and the token default to ULTRAAI_SERVER_URL and ULTRAAI_SERVER_TOKEN.\n"
        "\n"
        "Models:\n"
        "  models                                    every model, any state\n"
        "  model <id>                                one model with its files and topics\n"
        "  model-put <id> --capability C --engine E [--name N] [--license L] [--description D]\n"
        "            [--memory BYTES] [--gpu BYTES] [--context N] [--dims N] [--tools] [--vision]\n"
        "            [--tag T]... [--arg A]...       create a draft, or change one\n"
        "  upload <id> <file> [--name N]             upload a model file (hashed first)\n"
        "  add-url <id> <name> <url> --size N [--sha256 H]   a file hosted elsewhere\n"
        "  remove-file <id> <name>\n"
        "  check <id>                                what still stands in the way of publishing\n"
        "  publish <id> | withdraw <id> | draft <id> | delete <id>\n"
        "\n"
        "Topics:\n"
        "  topics\n"
        "  topic-put <id> --name N --capability C [--description D] <model>[*]...\n"
        "                                            the models offered, best first; * marks the default\n"
        "  topic-delete <id>\n"
        "\n"
        "Server:\n"
        "  whoami | engines | uploads | prune | audit [since]\n"
        "  admins | admin-put <name> <super|editor> | admin-delete <name>\n",
        to);
    return to == stdout ? 0 : 2;
}

int Run(const std::vector<std::string>& in) {
    const Args a = Parse(in);
    if (a.positional.empty() || a.Flag("help") || a.positional[0] == "help") return Usage(a.positional.empty() && !a.Flag("help") ? stderr : stdout);

    Connection connection;
    connection.baseUrl = a.Option("server", UltraCanvas::GetEnvUtf8("ULTRAAI_SERVER_URL"));
    connection.token   = a.Option("token", UltraCanvas::GetEnvUtf8("ULTRAAI_SERVER_TOKEN"));
    std::string why;
    if (ModelServerClient::NormaliseBaseUrl(connection.baseUrl, &why).empty()) {
        std::cerr << "error: " << why << " Give --server or set ULTRAAI_SERVER_URL.\n";
        return 2;
    }
    if (connection.token.empty()) {
        std::cerr << "error: no token. Give --token or set ULTRAAI_SERVER_TOKEN.\n";
        return 2;
    }
    const ModelServerClient client(connection);
    const std::string& cmd = a.positional[0];
    auto arg = [&](size_t i) { return a.positional[i]; };

    if (cmd == "whoami")  return Print(client.WhoAmI());
    if (cmd == "models")  return Print(client.ListModels());
    if (cmd == "topics")  return Print(client.ListTopics());
    if (cmd == "engines") return Print(client.ListEngines());
    if (cmd == "uploads") return Print(client.ListUploads());
    if (cmd == "prune")   return Print(client.PruneUploads());
    if (cmd == "admins")  return Print(client.ListAdmins());
    if (cmd == "audit")   return Print(client.Audit(a.positional.size() > 1 ? static_cast<int64_t>(Number(arg(1))) : 0));

    if (cmd == "model")    { if (int e = Need(a, 1, "model <id>")) return e; return Print(client.GetModel(arg(1))); }
    if (cmd == "check")    { if (int e = Need(a, 1, "check <id>")) return e; return Print(client.CheckModel(arg(1))); }
    if (cmd == "publish")  { if (int e = Need(a, 1, "publish <id>")) return e; return Print(client.Publish(arg(1))); }
    if (cmd == "withdraw") { if (int e = Need(a, 1, "withdraw <id>")) return e; return Print(client.Withdraw(arg(1))); }
    if (cmd == "draft")    { if (int e = Need(a, 1, "draft <id>")) return e; return Print(client.BackToDraft(arg(1))); }
    if (cmd == "delete")   { if (int e = Need(a, 1, "delete <id>")) return e; return Print(client.DeleteModel(arg(1))); }

    if (cmd == "model-put") {
        if (int e = Need(a, 1, "model-put <id> --capability C --engine E ...")) return e;
        JSONValue m = JSONValue::MakeObject();
        m.Set("displayName", a.Option("name", arg(1)));
        m.Set("capability", a.Option("capability"));
        m.Set("engine", a.Option("engine", "llama-server"));
        if (!a.Option("license").empty()) m.Set("license", a.Option("license"));
        if (!a.Option("description").empty()) m.Set("description", a.Option("description"));
        JSONValue req = JSONValue::MakeObject();
        if (const uint64_t v = Number(a.Option("memory"))) req.Set("memoryBytes", static_cast<int64_t>(v));
        if (const uint64_t v = Number(a.Option("gpu"))) req.Set("gpuMemoryBytes", static_cast<int64_t>(v));
        m.Set("requires", req);
        JSONValue feat = JSONValue::MakeObject();
        if (const uint64_t v = Number(a.Option("context"))) feat.Set("contextWindow", static_cast<int64_t>(v));
        if (const uint64_t v = Number(a.Option("dims"))) feat.Set("embeddingDimensions", static_cast<int64_t>(v));
        if (a.Flag("tools")) feat.Set("tools", true);
        if (a.Flag("vision")) feat.Set("vision", true);
        m.Set("features", feat);
        auto tags = a.options.find("tag");
        m.Set("tags", Strings(tags == a.options.end() ? std::vector<std::string>{} : tags->second));
        auto args = a.options.find("arg");
        m.Set("engineArgs", Strings(args == a.options.end() ? std::vector<std::string>{} : args->second));
        return Print(client.PutModel(arg(1), m));
    }
    if (cmd == "upload") {
        if (int e = Need(a, 2, "upload <id> <file> [--name N]")) return e;
        const std::string path = arg(2);
        const std::string name = a.Option("name", UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(path).filename()));
        std::cerr << "hashing and uploading " << path << " as " << name << "\n";
        int lastPercent = -1;
        const Reply r = client.UploadFile(arg(1), name, path, [&](int64_t done, int64_t total) {
            if (total <= 0) return;
            const int percent = static_cast<int>(done * 100 / total);
            if (percent != lastPercent && percent % 5 == 0) {
                lastPercent = percent;
                std::cerr << "\r" << percent << "%" << std::flush;
            }
        });
        std::cerr << "\n";
        return Print(r);
    }
    if (cmd == "add-url") {
        if (int e = Need(a, 3, "add-url <id> <name> <url> --size N [--sha256 H]")) return e;
        return Print(client.PutExternalFile(arg(1), arg(2), arg(3), a.Option("sha256"), Number(a.Option("size"))));
    }
    if (cmd == "remove-file") {
        if (int e = Need(a, 2, "remove-file <id> <name>")) return e;
        return Print(client.RemoveModelFile(arg(1), arg(2)));
    }
    if (cmd == "topic-put") {
        if (int e = Need(a, 1, "topic-put <id> --name N --capability C <model>[*]...")) return e;
        JSONValue t = JSONValue::MakeObject();
        t.Set("name", a.Option("name", arg(1)));
        t.Set("capability", a.Option("capability", "textllm"));
        if (!a.Option("description").empty()) t.Set("description", a.Option("description"));
        JSONValue models = JSONValue::MakeArray();
        for (size_t i = 2; i < a.positional.size(); ++i) {
            std::string id = arg(i);
            JSONValue entry = JSONValue::MakeObject();
            if (!id.empty() && id.back() == '*') {
                id.pop_back();
                entry.Set("default", true);
            }
            entry.Set("id", id);
            models.Append(entry);
        }
        t.Set("models", models);
        return Print(client.PutTopic(arg(1), t));
    }
    if (cmd == "topic-delete") { if (int e = Need(a, 1, "topic-delete <id>")) return e; return Print(client.DeleteTopic(arg(1))); }
    if (cmd == "admin-put") {
        if (int e = Need(a, 2, "admin-put <name> <super|editor>")) return e;
        return Print(client.PutAdmin(arg(1), arg(2)));
    }
    if (cmd == "admin-delete") { if (int e = Need(a, 1, "admin-delete <name>")) return e; return Print(client.DeleteAdmin(arg(1))); }

    std::cerr << "unknown command: " << cmd << "\n\n";
    return Usage(stderr);
}

} // namespace UltraAIServerAdmin::Cli
