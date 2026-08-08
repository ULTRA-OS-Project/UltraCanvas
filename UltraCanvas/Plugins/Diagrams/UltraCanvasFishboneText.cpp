// Plugins/Diagrams/UltraCanvasFishboneText.cpp
// Fishbone text interchange: indented outline and Mermaid `ishikawa-beta`
// Version: 1.0.0
// Last Modified: 2026-08-07
// Author: UltraCanvas Framework
//
// Both notations are the same shape - an effect line followed by an indented
// tree - so the Mermaid codec is a thin wrapper over the outline codec that
// adds the diagram keyword and tolerates front matter.

#include "Plugins/Diagrams/UltraCanvasFishboneModel.h"
#include <algorithm>
#include <sstream>

namespace UltraCanvas {

// =============================================================================
// LINE SCANNING
// =============================================================================

    namespace {

        struct OutlineLine {
            int         indentUnits = 0;  // Raw indent: spaces, or tab count * tabWidth
            std::string text;
            size_t      lineNumber = 0;
        };

        std::string TrimRight(const std::string& text) {
            size_t end = text.find_last_not_of(" \t\r\n");
            return (end == std::string::npos) ? std::string() : text.substr(0, end + 1);
        }

        std::string TrimBoth(const std::string& text) {
            size_t begin = text.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos) return std::string();
            size_t end = text.find_last_not_of(" \t\r\n");
            return text.substr(begin, end - begin + 1);
        }

        // Strips a leading list bullet ("- ", "* ", "+ ") if present.
        std::string StripBullet(const std::string& text) {
            if (text.size() >= 2 && (text[0] == '-' || text[0] == '*' || text[0] == '+') &&
                (text[1] == ' ' || text[1] == '\t')) {
                return TrimBoth(text.substr(2));
            }
            return text;
        }

        bool IsComment(const std::string& text) {
            return text.rfind("//", 0) == 0 || text.rfind("#", 0) == 0 ||
                   text.rfind("%%", 0) == 0;   // %% is Mermaid's comment marker
        }

        // A tab is worth this many indent units; any consistent value works
        // because indents are normalized to steps afterwards.
        constexpr int kTabWidth = 4;

        std::vector<OutlineLine> ScanLines(const std::string& text) {
            std::vector<OutlineLine> lines;
            std::istringstream stream(text);
            std::string raw;
            size_t number = 0;

            while (std::getline(stream, raw)) {
                ++number;
                raw = TrimRight(raw);

                int indent = 0;
                size_t i = 0;
                for (; i < raw.size(); ++i) {
                    if (raw[i] == ' ') indent += 1;
                    else if (raw[i] == '\t') indent += kTabWidth;
                    else break;
                }
                if (i >= raw.size()) continue;  // Blank line

                std::string body = StripBullet(TrimBoth(raw.substr(i)));
                if (body.empty() || IsComment(body)) continue;

                lines.push_back({indent, body, number});
            }
            return lines;
        }

        // Maps raw indents onto 0,1,2,... levels. The step is the smallest
        // positive indent difference seen, so 2-space and 4-space files both
        // work, as do files that mix tabs with spaces consistently.
        void AssignLevels(std::vector<OutlineLine>& lines, std::vector<int>& levels) {
            levels.assign(lines.size(), 0);
            if (lines.empty()) return;

            std::vector<int> distinct;
            for (const auto& line : lines) distinct.push_back(line.indentUnits);
            std::sort(distinct.begin(), distinct.end());
            distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());

            int step = 0;
            for (size_t i = 1; i < distinct.size(); ++i) {
                int delta = distinct[i] - distinct[i - 1];
                if (delta > 0 && (step == 0 || delta < step)) step = delta;
            }
            if (step <= 0) step = 1;

            const int base = distinct.front();
            for (size_t i = 0; i < lines.size(); ++i) {
                levels[i] = std::max(0, (lines[i].indentUnits - base) / step);
            }

            // An indent jump of more than one level is treated as a single step
            // down the tree - the same forgiving reading a Markdown outline gets.
            int previous = 0;
            for (size_t i = 0; i < levels.size(); ++i) {
                if (levels[i] > previous + 1) levels[i] = previous + 1;
                previous = levels[i];
            }
        }

        // Appends a cause at the given depth by walking down the last branch.
        // depth 1 = a direct cause of the category.
        void AppendCause(std::vector<FishboneCause>& causes, int depth,
                         const std::string& text) {
            if (depth <= 1 || causes.empty()) {
                causes.emplace_back(text);
                return;
            }
            AppendCause(causes.back().subCauses, depth - 1, text);
        }

    } // namespace

// =============================================================================
// OUTLINE
// =============================================================================

    FishboneDocument ParseFishboneOutline(const std::string& text, std::string* error) {
        FishboneDocument document;
        if (error) error->clear();

        std::vector<OutlineLine> lines = ScanLines(text);
        if (lines.empty()) {
            if (error) *error = "No content: the outline has no non-blank lines";
            return document;
        }

        std::vector<int> levels;
        AssignLevels(lines, levels);

        // The first line is the effect whatever its indent. The line after it
        // fixes the category level, which is what makes both shapes in the wild
        // parse: Mermaid's own example puts the effect and the categories at
        // the same indent, while a hand-written outline indents categories
        // under the effect. Everything deeper than the category level is a
        // cause, nested by how much deeper it is.
        document.effect = lines[0].text;
        if (lines.size() < 2) {
            if (error) *error = "No categories: only the effect line was found";
            return document;
        }

        const int categoryLevel = levels[1];

        for (size_t i = 1; i < lines.size(); ++i) {
            const int level = levels[i];
            if (level <= categoryLevel) {
                document.AddCategory(lines[i].text);
                continue;
            }
            if (document.categories.empty()) {
                // A cause before any category: invent one so nothing is lost.
                document.AddCategory("Causes");
            }
            AppendCause(document.categories.back().causes, level - categoryLevel,
                        lines[i].text);
        }

        if (document.categories.empty() && error) {
            *error = "No categories: only the effect line was found";
        }
        return document;
    }

    static void WriteCauses(std::ostringstream& out, const std::vector<FishboneCause>& causes,
                            int depth, const std::string& unit) {
        for (const auto& cause : causes) {
            for (int i = 0; i < depth; ++i) out << unit;
            out << cause.text << "\n";
            WriteCauses(out, cause.subCauses, depth + 1, unit);
        }
    }

    std::string ExportFishboneOutline(const FishboneDocument& document, int indentWidth) {
        const std::string unit(static_cast<size_t>(std::max(1, indentWidth)), ' ');
        std::ostringstream out;

        out << (document.effect.empty() ? "Effect" : document.effect) << "\n";
        for (const auto& category : document.categories) {
            out << unit << category.title << "\n";
            WriteCauses(out, category.causes, 2, unit);
        }
        return out.str();
    }

// =============================================================================
// MERMAID `ishikawa-beta`
// =============================================================================

    // Strips the diagram keyword and any `---` front-matter block, then hands
    // the remainder to the outline parser.
    FishboneDocument ParseMermaidIshikawa(const std::string& text, std::string* error) {
        std::istringstream stream(text);
        std::ostringstream body;
        std::string raw;

        bool inFrontMatter = false;
        bool sawKeyword = false;

        while (std::getline(stream, raw)) {
            const std::string trimmed = TrimBoth(raw);

            if (!sawKeyword) {
                if (trimmed.empty()) continue;
                if (trimmed == "---") { inFrontMatter = !inFrontMatter; continue; }
                if (inFrontMatter) continue;
                if (trimmed.rfind("ishikawa-beta", 0) == 0 || trimmed.rfind("ishikawa", 0) == 0 ||
                    trimmed.rfind("fishbone", 0) == 0) {
                    sawKeyword = true;
                    continue;
                }
                // No keyword at all - treat the whole text as a plain outline.
                break;
            }
            body << TrimRight(raw) << "\n";
        }

        if (!sawKeyword) {
            return ParseFishboneOutline(text, error);
        }

        std::string parseError;
        FishboneDocument document = ParseFishboneOutline(body.str(), &parseError);
        if (error) {
            *error = parseError.empty() ? std::string() : "ishikawa-beta: " + parseError;
        }
        return document;
    }

    std::string ExportMermaidIshikawa(const FishboneDocument& document, int indentWidth) {
        const int width = std::max(1, indentWidth);
        const std::string unit(static_cast<size_t>(width), ' ');
        std::ostringstream out;

        out << "ishikawa-beta\n";
        // Mermaid's documented shape: the effect and the categories share one
        // indent step under the keyword, and causes go deeper from there.
        out << unit << (document.effect.empty() ? "Effect" : document.effect) << "\n";
        for (const auto& category : document.categories) {
            out << unit << category.title << "\n";
            WriteCauses(out, category.causes, 2, unit);
        }
        return out.str();
    }

} // namespace UltraCanvas
