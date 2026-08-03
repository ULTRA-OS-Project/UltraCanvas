// Plugins/Diagrams/UltraCanvasGitGraphMermaid.cpp
// Mermaid `gitGraph` import/export.
//
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasGitGraphMermaid.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace UltraCanvas {

namespace {

std::string Trim(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string();
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string ToLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(::tolower(c)); });
    return text;
}

// Strips a surrounding pair of double or single quotes.
std::string Unquote(const std::string& text) {
    if (text.size() >= 2 &&
        ((text.front() == '"' && text.back() == '"') ||
         (text.front() == '\'' && text.back() == '\''))) {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

// Splits a statement into whitespace-separated tokens, keeping quoted runs
// together and gluing `key:` to the value that follows it.
std::vector<std::string> Tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::string current;
    bool inQuotes = false;
    char quoteChar = '"';

    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (inQuotes) {
            current.push_back(c);
            if (c == quoteChar) inQuotes = false;
            continue;
        }
        if (c == '"' || c == '\'') {
            inQuotes = true;
            quoteChar = c;
            current.push_back(c);
            continue;
        }
        if (c == ' ' || c == '\t') {
            // A trailing ':' means the value belongs to this key, so keep going.
            if (!current.empty() && current.back() == ':') continue;
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(c);
    }
    if (!current.empty()) tokens.push_back(current);
    return tokens;
}

struct Attributes {
    std::string id, tag, type, msg, parent;
    int  order = -1;
    bool hasOrder = false;
};

Attributes ParseAttributes(const std::vector<std::string>& tokens, size_t from) {
    Attributes attributes;
    for (size_t i = from; i < tokens.size(); ++i) {
        const std::string& token = tokens[i];
        const size_t colon = token.find(':');
        if (colon == std::string::npos) continue;

        const std::string key = ToLower(Trim(token.substr(0, colon)));
        const std::string value = Unquote(Trim(token.substr(colon + 1)));

        if (key == "id")          attributes.id = value;
        else if (key == "tag")    attributes.tag = value;
        else if (key == "type")   attributes.type = value;
        else if (key == "msg")    attributes.msg = value;
        else if (key == "parent") attributes.parent = value;
        else if (key == "order") {
            attributes.order = std::atoi(value.c_str());
            attributes.hasOrder = true;
        }
    }
    return attributes;
}

GitGraphCommitType ParseCommitType(const std::string& text) {
    const std::string upper = ToLower(text);
    if (upper == "reverse")   return GitGraphCommitType::Reverse;
    if (upper == "highlight") return GitGraphCommitType::Highlight;
    return GitGraphCommitType::Normal;
}

const char* CommitTypeName(GitGraphCommitType type) {
    switch (type) {
        case GitGraphCommitType::Reverse:   return "REVERSE";
        case GitGraphCommitType::Highlight: return "HIGHLIGHT";
        default:                            return "NORMAL";
    }
}

// Mermaid identifiers may contain characters that need quoting on the way out.
bool NeedsQuotes(const std::string& text) {
    if (text.empty()) return true;
    for (char c : text) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' ||
              c == '/' || c == '.')) {
            return true;
        }
    }
    return false;
}

std::string QuoteIfNeeded(const std::string& text) {
    if (!NeedsQuotes(text)) return text;
    std::string escaped;
    escaped.reserve(text.size() + 2);
    escaped.push_back('"');
    for (char c : text) {
        if (c == '"') escaped.push_back('\'');
        else escaped.push_back(c);
    }
    escaped.push_back('"');
    return escaped;
}

// Incremental DAG builder shared by the parser: mirrors the element's authoring
// API (commit -> parent is the current branch tip, merge -> two parents), but
// without any dependency on the widget.
class MermaidBuilder {
public:
    explicit MermaidBuilder(const std::string& mainBranch) : current(mainBranch) {
        branchTips[mainBranch] = std::string();
        declared.push_back(mainBranch);
    }

    bool HasBranch(const std::string& name) const {
        return branchTips.find(name) != branchTips.end();
    }

    std::string CreateCommit(const Attributes& attributes,
                             const std::string& cherryPickSource = std::string()) {
        GitGraphCommit commit;
        commit.sha = attributes.id.empty() ? MakeId() : attributes.id;
        if (used.count(commit.sha)) return std::string();       // Duplicate id
        used.insert(commit.sha);

        commit.subject = attributes.msg.empty() ? commit.sha : attributes.msg;
        commit.branch  = current;
        commit.type    = ParseCommitType(attributes.type);
        commit.commitDate = static_cast<int64_t>(commits.size());
        commit.authorDate = commit.commitDate;
        commit.cherryPickSource = cherryPickSource;

        const std::string& tip = branchTips[current];
        if (!tip.empty()) commit.parents.push_back(tip);

        commits.push_back(commit);
        branchTips[current] = commit.sha;
        if (!attributes.tag.empty()) tags.emplace_back(attributes.tag, commit.sha);
        return commit.sha;
    }

    std::string CreateMerge(const std::string& fromBranch, const Attributes& attributes) {
        auto source = branchTips.find(fromBranch);
        if (source == branchTips.end() || source->second.empty()) return std::string();

        GitGraphCommit commit;
        commit.sha = attributes.id.empty() ? MakeId() : attributes.id;
        if (used.count(commit.sha)) return std::string();
        used.insert(commit.sha);

        commit.subject = attributes.msg.empty() ? ("Merge branch '" + fromBranch + "'")
                                                : attributes.msg;
        commit.branch  = current;
        commit.type    = ParseCommitType(attributes.type);
        commit.commitDate = static_cast<int64_t>(commits.size());
        commit.authorDate = commit.commitDate;

        const std::string& tip = branchTips[current];
        if (!tip.empty()) commit.parents.push_back(tip);
        commit.parents.push_back(source->second);

        commits.push_back(commit);
        branchTips[current] = commit.sha;
        if (!attributes.tag.empty()) tags.emplace_back(attributes.tag, commit.sha);
        return commit.sha;
    }

    void Branch(const std::string& name) {
        branchTips[name] = branchTips[current];
        current = name;
        if (std::find(declared.begin(), declared.end(), name) == declared.end()) {
            declared.push_back(name);
        }
    }

    bool Checkout(const std::string& name) {
        if (!HasBranch(name)) return false;
        current = name;
        return true;
    }

    bool KnowsCommit(const std::string& sha) const { return used.count(sha) > 0; }

    void Finish(GitGraphMermaidDocument& document, const std::string& mainBranch) const {
        document.commits = commits;
        for (const std::string& name : declared) {
            auto tip = branchTips.find(name);
            if (tip == branchTips.end() || tip->second.empty()) continue;
            GitGraphRef ref;
            ref.name      = name;
            ref.sha       = tip->second;
            ref.type      = GitGraphRefType::LocalBranch;
            ref.isCurrent = (name == current);
            document.refs.push_back(ref);
        }
        for (const auto& tag : tags) {
            GitGraphRef ref;
            ref.name      = tag.first;
            ref.sha       = tag.second;
            ref.type      = GitGraphRefType::Tag;
            ref.annotated = true;
            document.refs.push_back(ref);
        }
        document.branchOrder = declared;
        document.mainBranchName = mainBranch;
    }

private:
    std::string MakeId() { return "c" + std::to_string(counter++); }

    std::vector<GitGraphCommit> commits;
    std::unordered_map<std::string, std::string> branchTips;
    std::unordered_set<std::string> used;
    std::vector<std::pair<std::string, std::string>> tags;
    std::vector<std::string> declared;
    std::string current;
    int counter = 0;
};

// Pulls the handful of keys we honour out of an `%%{init: {...}}%%` header
// without dragging in a JSON parser.
void ApplyInitDirective(const std::string& line, GitGraphMermaidDocument& document) {
    // Keys and values may be single-quoted, double-quoted or bare - mermaid's
    // own documentation uses all three.
    auto findValue = [&line](const std::string& key) -> std::string {
        size_t at = line.find(key);
        while (at != std::string::npos) {
            const bool boundaryBefore =
                (at == 0) || line[at - 1] == '"' || line[at - 1] == '\'' ||
                line[at - 1] == '{' || line[at - 1] == ',' || std::isspace(
                    static_cast<unsigned char>(line[at - 1]));
            const size_t after = at + key.size();
            const bool boundaryAfter =
                (after >= line.size()) || line[after] == '"' || line[after] == '\'' ||
                line[after] == ':' || std::isspace(static_cast<unsigned char>(line[after]));

            if (boundaryBefore && boundaryAfter) {
                const size_t colon = line.find(':', after);
                if (colon == std::string::npos) return std::string();

                const size_t start = line.find_first_not_of(" \t", colon + 1);
                if (start == std::string::npos) return std::string();

                if (line[start] == '"' || line[start] == '\'') {
                    const size_t end = line.find(line[start], start + 1);
                    if (end == std::string::npos) return std::string();
                    return line.substr(start + 1, end - start - 1);
                }
                const size_t end = line.find_first_of(",}\n", start);
                return Trim(line.substr(start,
                                        (end == std::string::npos ? line.size() : end) - start));
            }
            at = line.find(key, at + 1);
        }
        return std::string();
    };

    const std::string mainBranch = findValue("mainBranchName");
    if (!mainBranch.empty()) document.mainBranchName = mainBranch;

    const std::string showBranches = findValue("showBranches");
    if (!showBranches.empty()) document.showBranches = (ToLower(showBranches) != "false");

    const std::string showLabel = findValue("showCommitLabel");
    if (!showLabel.empty()) document.showCommitLabel = (ToLower(showLabel) != "false");

    const std::string rotate = findValue("rotateCommitLabel");
    if (!rotate.empty()) document.rotateCommitLabel = (ToLower(rotate) != "false");

    const std::string parallel = findValue("parallelCommits");
    if (!parallel.empty()) document.parallelCommits = (ToLower(parallel) != "false");
}

} // namespace

// =============================================================================
// PARSE
// =============================================================================

GitGraphMermaidDocument ParseMermaidGitGraph(const std::string& text) {
    GitGraphMermaidDocument document;

    // A first pass picks up the init header and the gitGraph directive, because
    // mainBranchName has to be known before the builder starts.
    std::vector<std::string> lines;
    {
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line)) lines.push_back(line);
    }

    int directiveLine = -1;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string trimmed = Trim(lines[i]);
        if (trimmed.empty()) continue;

        if (trimmed.rfind("%%{", 0) == 0) {
            ApplyInitDirective(trimmed, document);
            continue;
        }
        if (trimmed.rfind("%%", 0) == 0) continue;

        const std::string lower = ToLower(trimmed);
        if (lower.rfind("gitgraph", 0) == 0) {
            directiveLine = static_cast<int>(i);
            // "gitGraph LR:" / "gitGraph TB:" / "gitGraph:" / "gitGraph"
            std::string rest = Trim(trimmed.substr(8));
            if (!rest.empty() && rest.front() == ':') rest = Trim(rest.substr(1));
            if (!rest.empty() && rest.back() == ':')  rest = Trim(rest.substr(0, rest.size() - 1));

            const std::string direction = ToLower(rest);
            if (direction == "tb" || direction == "td") {
                document.orientation = GitGraphOrientation::TopToBottom;
            } else if (direction == "bt") {
                document.orientation = GitGraphOrientation::BottomToTop;
            } else {
                document.orientation = GitGraphOrientation::LeftToRight;
            }
            break;
        }

        document.error = "expected a 'gitGraph' directive";
        document.errorLine = static_cast<int>(i) + 1;
        return document;
    }

    if (directiveLine < 0) {
        document.error = "no 'gitGraph' directive found";
        document.errorLine = 0;
        return document;
    }

    MermaidBuilder builder(document.mainBranchName);
    std::unordered_map<std::string, int> branchOrders;

    for (size_t i = static_cast<size_t>(directiveLine) + 1; i < lines.size(); ++i) {
        std::string statement = Trim(lines[i]);
        if (statement.empty()) continue;
        if (statement.rfind("%%", 0) == 0) continue;

        // Strip a trailing comment.
        const size_t comment = statement.find("%%");
        if (comment != std::string::npos) statement = Trim(statement.substr(0, comment));
        if (statement.empty()) continue;

        const std::vector<std::string> tokens = Tokenize(statement);
        if (tokens.empty()) continue;

        const std::string keyword = ToLower(tokens[0]);
        const int lineNumber = static_cast<int>(i) + 1;

        if (keyword == "commit") {
            const Attributes attributes = ParseAttributes(tokens, 1);
            if (builder.CreateCommit(attributes).empty()) {
                document.error = "duplicate commit id '" + attributes.id + "'";
                document.errorLine = lineNumber;
                return document;
            }
        } else if (keyword == "branch") {
            if (tokens.size() < 2) {
                document.error = "'branch' needs a name";
                document.errorLine = lineNumber;
                return document;
            }
            const std::string name = Unquote(tokens[1]);
            const Attributes attributes = ParseAttributes(tokens, 2);
            if (attributes.hasOrder) branchOrders[name] = attributes.order;
            builder.Branch(name);
        } else if (keyword == "checkout" || keyword == "switch") {
            if (tokens.size() < 2) {
                document.error = "'" + keyword + "' needs a branch name";
                document.errorLine = lineNumber;
                return document;
            }
            const std::string name = Unquote(tokens[1]);
            if (!builder.Checkout(name)) {
                document.error = "unknown branch '" + name + "'";
                document.errorLine = lineNumber;
                return document;
            }
        } else if (keyword == "merge") {
            if (tokens.size() < 2) {
                document.error = "'merge' needs a branch name";
                document.errorLine = lineNumber;
                return document;
            }
            const std::string name = Unquote(tokens[1]);
            if (!builder.HasBranch(name)) {
                document.error = "unknown branch '" + name + "'";
                document.errorLine = lineNumber;
                return document;
            }
            const Attributes attributes = ParseAttributes(tokens, 2);
            if (builder.CreateMerge(name, attributes).empty()) {
                document.error = "cannot merge branch '" + name + "' - it has no commits";
                document.errorLine = lineNumber;
                return document;
            }
        } else if (keyword == "cherry-pick" || keyword == "cherrypick") {
            const Attributes attributes = ParseAttributes(tokens, 1);
            if (attributes.id.empty()) {
                document.error = "'cherry-pick' requires an id attribute";
                document.errorLine = lineNumber;
                return document;
            }
            if (!builder.KnowsCommit(attributes.id)) {
                document.error = "cherry-pick source '" + attributes.id + "' does not exist";
                document.errorLine = lineNumber;
                return document;
            }
            Attributes picked = attributes;
            picked.id.clear();                       // The new commit gets its own id
            if (picked.msg.empty()) picked.msg = "cherry-pick " + attributes.id;
            builder.CreateCommit(picked, attributes.id);
        } else {
            document.error = "unrecognised statement '" + tokens[0] + "'";
            document.errorLine = lineNumber;
            return document;
        }
    }

    builder.Finish(document, document.mainBranchName);

    // Honour `order:` on branch declarations; unordered branches keep their
    // declaration position after the ordered ones.
    if (!branchOrders.empty()) {
        std::stable_sort(document.branchOrder.begin(), document.branchOrder.end(),
                         [&branchOrders](const std::string& a, const std::string& b) {
                             auto ia = branchOrders.find(a);
                             auto ib = branchOrders.find(b);
                             const int oa = (ia == branchOrders.end()) ? INT32_MAX : ia->second;
                             const int ob = (ib == branchOrders.end()) ? INT32_MAX : ib->second;
                             return oa < ob;
                         });
    }

    document.ok = true;
    return document;
}

// =============================================================================
// EXPORT
// =============================================================================

std::string ExportMermaidGitGraph(const std::vector<GitGraphCommit>& commits,
                                  const std::vector<GitGraphRef>& refs,
                                  const std::string& mainBranchName) {
    std::ostringstream out;
    out << "gitGraph\n";
    if (commits.empty()) return out.str();

    std::unordered_map<std::string, size_t> bySha;
    for (size_t i = 0; i < commits.size(); ++i) bySha.emplace(commits[i].sha, i);

    // Tags to emit alongside their commit.
    std::unordered_map<std::string, std::string> tagOfSha;
    for (const GitGraphRef& ref : refs) {
        if (ref.type == GitGraphRefType::Tag) tagOfSha[ref.sha] = ref.name;
    }

    // Branch membership: the explicit field when present, otherwise a
    // first-parent walk down from each branch ref (trunk first).
    std::vector<std::string> branchOf(commits.size());
    for (size_t i = 0; i < commits.size(); ++i) branchOf[i] = commits[i].branch;

    std::vector<const GitGraphRef*> walkOrder;
    for (const GitGraphRef& ref : refs) {
        if (ref.type == GitGraphRefType::LocalBranch && ref.name == mainBranchName) {
            walkOrder.push_back(&ref);
        }
    }
    for (const GitGraphRef& ref : refs) {
        if (ref.type != GitGraphRefType::LocalBranch) continue;
        if (ref.name == mainBranchName) continue;
        walkOrder.push_back(&ref);
    }
    for (const GitGraphRef* ref : walkOrder) {
        std::string cursor = ref->sha;
        while (!cursor.empty()) {
            auto it = bySha.find(cursor);
            if (it == bySha.end()) break;
            if (!branchOf[it->second].empty()) break;
            branchOf[it->second] = ref->name;
            cursor = commits[it->second].parents.empty() ? std::string()
                                                         : commits[it->second].parents.front();
        }
    }
    for (size_t i = 0; i < commits.size(); ++i) {
        if (branchOf[i].empty()) branchOf[i] = mainBranchName;
    }

    // Walk oldest-first. The stored order may be newest-first (git log order),
    // so sort by the number of ancestors rather than trusting the input.
    std::vector<size_t> order(commits.size());
    for (size_t i = 0; i < commits.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (commits[a].commitDate != commits[b].commitDate) {
            return commits[a].commitDate < commits[b].commitDate;
        }
        return a > b;                       // Newest-first input reverses cleanly
    });

    std::unordered_set<std::string> knownBranches;
    knownBranches.insert(mainBranchName);
    std::string currentBranch = mainBranchName;

    for (size_t index : order) {
        const GitGraphCommit& commit = commits[index];
        const std::string& branch = branchOf[index];

        if (knownBranches.find(branch) == knownBranches.end()) {
            out << "    branch " << QuoteIfNeeded(branch) << "\n";
            knownBranches.insert(branch);
            currentBranch = branch;
        } else if (branch != currentBranch) {
            out << "    checkout " << QuoteIfNeeded(branch) << "\n";
            currentBranch = branch;
        }

        std::string suffix;
        auto tag = tagOfSha.find(commit.sha);
        if (tag != tagOfSha.end()) suffix += " tag: " + QuoteIfNeeded(tag->second);
        if (commit.type != GitGraphCommitType::Normal) {
            suffix += std::string(" type: ") + CommitTypeName(commit.type);
        }

        if (commit.parents.size() > 1) {
            // Emit the merge against the branch the second parent belongs to.
            std::string sourceBranch;
            auto parent = bySha.find(commit.parents[1]);
            if (parent != bySha.end()) sourceBranch = branchOf[parent->second];
            if (sourceBranch.empty() || sourceBranch == branch) {
                out << "    commit id: " << QuoteIfNeeded(commit.sha) << suffix << "\n";
            } else {
                out << "    merge " << QuoteIfNeeded(sourceBranch)
                    << " id: " << QuoteIfNeeded(commit.sha) << suffix << "\n";
            }
        } else if (!commit.cherryPickSource.empty()) {
            out << "    cherry-pick id: " << QuoteIfNeeded(commit.cherryPickSource) << "\n";
        } else {
            out << "    commit id: " << QuoteIfNeeded(commit.sha) << suffix << "\n";
        }
    }

    return out.str();
}

} // namespace UltraCanvas
