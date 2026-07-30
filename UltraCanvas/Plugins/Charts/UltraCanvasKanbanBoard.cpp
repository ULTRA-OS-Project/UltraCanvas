// Plugins/Charts/UltraCanvasKanbanBoard.cpp
// Kanban board element: workflow columns with WIP limits, swimlanes, cards
// with priorities/due dates/assignees/tags, drag & drop, a built-in card
// editor, move history for flow metrics, text-definition (Mermaid kanban)
// and JSON loading, and a preset-based design/palette system.
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasKanbanBoard.h"
#include "DataFormats/UltraCanvasJSON.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace UltraCanvas {

// =============================================================================
// LOCAL HELPERS
// =============================================================================

namespace {

    std::string ToUpper(std::string s) {
        for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return s;
    }

    std::string TrimText(const std::string& s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    // Draw a single line of text clipped to `rect`, horizontally aligned and
    // vertically centered. Uses the context's current font and text paint.
    void DrawCellText(IRenderContext* ctx, const std::string& text,
                      const Rect2Dd& rect, TextAlignment align,
                      float padding = 4.0f) {
        if (text.empty() || rect.width <= 2 * padding) return;
        Size2Di sz = ctx->GetTextLineDimensions(text);
        double x = rect.x + padding;
        if (align == TextAlignment::Center) {
            x = rect.x + (rect.width - sz.width) / 2.0;
            x = std::max(x, rect.x + padding);
        } else if (align == TextAlignment::Right) {
            x = rect.x + rect.width - padding - sz.width;
            x = std::max(x, rect.x + padding);
        }
        double y = rect.y + (rect.height - sz.height) / 2.0;
        ctx->PushState();
        ctx->ClipRect(rect);
        ctx->DrawText(text, Point2Dd(x, y));
        ctx->PopState();
    }

    // Word-wrap `text` to `maxWidth` using the context's current font.
    std::vector<std::string> WrapTextLines(IRenderContext* ctx,
                                           const std::string& text,
                                           double maxWidth) {
        std::vector<std::string> lines;
        std::stringstream paragraphs(text);
        std::string paragraph;
        while (std::getline(paragraphs, paragraph, '\n')) {
            std::stringstream words(paragraph);
            std::string word, current;
            while (words >> word) {
                std::string candidate = current.empty() ? word : current + " " + word;
                if (!current.empty() &&
                    ctx->GetTextLineDimensions(candidate).width > maxWidth) {
                    lines.push_back(current);
                    current = word;
                } else {
                    current = candidate;
                }
            }
            lines.push_back(current);
        }
        while (!lines.empty() && lines.back().empty()) lines.pop_back();
        return lines;
    }

    std::string FormatISODate(const GanttDate& d) {
        int y, m, day;
        d.ToYMD(y, m, day);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, day);
        return buf;
    }

    bool ParseISODate(const std::string& s, GanttDate& out) {
        int y = 0, m = 0, d = 0;
        if (std::sscanf(s.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return false;
        if (m < 1 || m > 12 || d < 1 || d > 31) return false;
        out = GanttDate(y, m, d);
        return true;
    }

    const char* PriorityName(KanbanPriority p) {
        switch (p) {
            case KanbanPriority::Low:    return "Low";
            case KanbanPriority::Normal: return "Normal";
            case KanbanPriority::High:   return "High";
            case KanbanPriority::Urgent: return "Urgent";
            default:                     return "None";
        }
    }

    KanbanPriority PriorityFromText(const std::string& raw) {
        std::string s = ToUpper(TrimText(raw));
        if (s == "VERY HIGH" || s == "URGENT" || s == "CRITICAL") return KanbanPriority::Urgent;
        if (s == "HIGH") return KanbanPriority::High;
        if (s == "LOW" || s == "VERY LOW") return KanbanPriority::Low;
        if (s == "NONE" || s == "NO" || s.empty()) return KanbanPriority::NoPriority;
        return KanbanPriority::Normal;
    }

    // "#rgb" / "#rrggbb" -> Color; returns Transparent on failure.
    Color ParseHexColor(const std::string& raw) {
        std::string s = TrimText(raw);
        if (s.empty() || s[0] != '#') return Colors::Transparent;
        s.erase(0, 1);
        auto hex = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
            return -1;
        };
        if (s.size() == 3) {
            int r = hex(s[0]), g = hex(s[1]), b = hex(s[2]);
            if (r < 0 || g < 0 || b < 0) return Colors::Transparent;
            return Color(r * 17, g * 17, b * 17);
        }
        if (s.size() == 6) {
            int v[6];
            for (int i = 0; i < 6; ++i) {
                v[i] = hex(s[i]);
                if (v[i] < 0) return Colors::Transparent;
            }
            return Color(v[0] * 16 + v[1], v[2] * 16 + v[3], v[4] * 16 + v[5]);
        }
        return Colors::Transparent;
    }

    // Up-to-two-letter initials for the assignee avatar.
    std::string Initials(const std::string& name) {
        std::string out;
        bool newWord = true;
        for (char c : name) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                newWord = true;
            } else {
                if (newWord && out.size() < 2) {
                    out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                }
                newWord = false;
            }
        }
        return out;
    }

    // Remove the last UTF-8 code point from `s`.
    void PopUtf8(std::string& s) {
        while (!s.empty()) {
            unsigned char c = static_cast<unsigned char>(s.back());
            s.pop_back();
            if ((c & 0xC0) != 0x80) break;   // Stopped at a leading byte
        }
    }

    bool IsLightColor(const Color& c) {
        return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b >= 140.0;
    }

} // namespace

// =============================================================================
// DATA SOURCE — COLUMNS
// =============================================================================

int KanbanDataSource::AddColumn(const std::string& title, int wipLimit,
                                const Color& color) {
    KanbanColumn col;
    col.id = nextColumnId++;
    col.title = title;
    col.wipLimit = std::max(0, wipLimit);
    col.customColor = color;
    columns.push_back(col);
    cardOrder[col.id] = {};
    Touch();
    return col.id;
}

bool KanbanDataSource::RemoveColumn(int columnId) {
    int index = ColumnIndexOf(columnId);
    if (index < 0) return false;
    std::vector<int> doomed = GetCardsInColumn(columnId);
    for (int cardId : doomed) {
        RecordMove(cardId, columnId, -1);
        cards.erase(std::remove_if(cards.begin(), cards.end(),
                                   [&](const KanbanCard& c) { return c.id == cardId; }),
                    cards.end());
    }
    cardOrder.erase(columnId);
    columns.erase(columns.begin() + index);
    Touch();
    return true;
}

bool KanbanDataSource::RenameColumn(int columnId, const std::string& title) {
    KanbanColumn* col = FindColumn(columnId);
    if (!col) return false;
    col->title = title;
    Touch();
    return true;
}

bool KanbanDataSource::SetColumnWipLimit(int columnId, int wipLimit) {
    KanbanColumn* col = FindColumn(columnId);
    if (!col) return false;
    col->wipLimit = std::max(0, wipLimit);
    Touch();
    return true;
}

bool KanbanDataSource::SetColumnColor(int columnId, const Color& color) {
    KanbanColumn* col = FindColumn(columnId);
    if (!col) return false;
    col->customColor = color;
    Touch();
    return true;
}

bool KanbanDataSource::SetColumnFooterCaption(int columnId,
                                              const std::string& caption) {
    KanbanColumn* col = FindColumn(columnId);
    if (!col) return false;
    col->footerCaption = caption;
    Touch();
    return true;
}

bool KanbanDataSource::MoveColumn(int columnId, int newIndex) {
    int index = ColumnIndexOf(columnId);
    if (index < 0) return false;
    newIndex = std::clamp(newIndex, 0, static_cast<int>(columns.size()) - 1);
    if (newIndex == index) return true;
    KanbanColumn col = columns[index];
    columns.erase(columns.begin() + index);
    columns.insert(columns.begin() + newIndex, col);
    Touch();
    return true;
}

KanbanColumn* KanbanDataSource::FindColumn(int columnId) {
    for (auto& c : columns) if (c.id == columnId) return &c;
    return nullptr;
}

const KanbanColumn* KanbanDataSource::FindColumn(int columnId) const {
    for (const auto& c : columns) if (c.id == columnId) return &c;
    return nullptr;
}

int KanbanDataSource::ColumnIndexOf(int columnId) const {
    for (size_t i = 0; i < columns.size(); ++i) {
        if (columns[i].id == columnId) return static_cast<int>(i);
    }
    return -1;
}

// =============================================================================
// DATA SOURCE — SWIMLANES
// =============================================================================

int KanbanDataSource::AddLane(const std::string& title) {
    KanbanLane lane;
    lane.id = nextLaneId++;
    lane.title = title;
    lanes.push_back(lane);
    Touch();
    return lane.id;
}

bool KanbanDataSource::RemoveLane(int laneId) {
    auto it = std::find_if(lanes.begin(), lanes.end(),
                           [&](const KanbanLane& l) { return l.id == laneId; });
    if (it == lanes.end()) return false;
    lanes.erase(it);
    for (auto& card : cards) {
        if (card.laneId == laneId) card.laneId = -1;
    }
    Touch();
    return true;
}

bool KanbanDataSource::RenameLane(int laneId, const std::string& title) {
    KanbanLane* lane = FindLane(laneId);
    if (!lane) return false;
    lane->title = title;
    Touch();
    return true;
}

KanbanLane* KanbanDataSource::FindLane(int laneId) {
    for (auto& l : lanes) if (l.id == laneId) return &l;
    return nullptr;
}

const KanbanLane* KanbanDataSource::FindLane(int laneId) const {
    for (const auto& l : lanes) if (l.id == laneId) return &l;
    return nullptr;
}

// =============================================================================
// DATA SOURCE — CARDS
// =============================================================================

int KanbanDataSource::AddCard(int columnId, const std::string& title, int laneId) {
    KanbanCard card;
    card.columnId = columnId;
    card.laneId = laneId;
    card.title = title;
    return AddCard(card);
}

int KanbanDataSource::AddCard(const KanbanCard& card) {
    if (!FindColumn(card.columnId)) return -1;
    if (card.laneId >= 0 && !FindLane(card.laneId)) return -1;
    KanbanCard copy = card;
    copy.id = nextCardId++;
    copy.enteredColumn = currentDate;
    cards.push_back(copy);
    OrderOf(copy.columnId).push_back(copy.id);
    RecordMove(copy.id, -1, copy.columnId);
    Touch();
    return copy.id;
}

bool KanbanDataSource::RemoveCard(int cardId) {
    const KanbanCard* card = FindCard(cardId);
    if (!card) return false;
    RecordMove(cardId, card->columnId, -1);
    auto& order = OrderOf(card->columnId);
    order.erase(std::remove(order.begin(), order.end(), cardId), order.end());
    cards.erase(std::remove_if(cards.begin(), cards.end(),
                               [&](const KanbanCard& c) { return c.id == cardId; }),
                cards.end());
    Touch();
    return true;
}

void KanbanDataSource::Clear() {
    columns.clear();
    lanes.clear();
    cards.clear();
    cardOrder.clear();
    history.clear();
    commitmentColumnId = -1;
    deliveryColumnId = -1;
    Touch();
}

KanbanCard* KanbanDataSource::FindCard(int cardId) {
    for (auto& c : cards) if (c.id == cardId) return &c;
    return nullptr;
}

const KanbanCard* KanbanDataSource::FindCard(int cardId) const {
    for (const auto& c : cards) if (c.id == cardId) return &c;
    return nullptr;
}

#define UC_KANBAN_CARD_SETTER(body)                    \
    KanbanCard* card = FindCard(cardId);               \
    if (!card) return false;                           \
    body;                                              \
    Touch();                                           \
    return true;

bool KanbanDataSource::SetCardTitle(int cardId, const std::string& title) {
    UC_KANBAN_CARD_SETTER(card->title = title)
}
bool KanbanDataSource::SetCardDescription(int cardId, const std::string& text) {
    UC_KANBAN_CARD_SETTER(card->description = text)
}
bool KanbanDataSource::SetCardAssignee(int cardId, const std::string& assignee) {
    UC_KANBAN_CARD_SETTER(card->assignee = assignee)
}
bool KanbanDataSource::SetCardPriority(int cardId, KanbanPriority priority) {
    UC_KANBAN_CARD_SETTER(card->priority = priority)
}
bool KanbanDataSource::SetCardDueDate(int cardId, const GanttDate& date) {
    UC_KANBAN_CARD_SETTER(card->dueDate = date; card->hasDueDate = true)
}
bool KanbanDataSource::ClearCardDueDate(int cardId) {
    UC_KANBAN_CARD_SETTER(card->dueDate = GanttDate(); card->hasDueDate = false)
}
bool KanbanDataSource::SetCardColor(int cardId, const Color& color) {
    UC_KANBAN_CARD_SETTER(card->customColor = color)
}
bool KanbanDataSource::SetCardBlocked(int cardId, bool blocked,
                                      const std::string& reason) {
    UC_KANBAN_CARD_SETTER(card->blocked = blocked;
                          card->blockedReason = blocked ? reason : "")
}
bool KanbanDataSource::SetCardTags(int cardId, const std::vector<std::string>& tags) {
    UC_KANBAN_CARD_SETTER(card->tags = tags)
}
bool KanbanDataSource::AddCardTag(int cardId, const std::string& tag) {
    UC_KANBAN_CARD_SETTER(card->tags.push_back(tag))
}

#undef UC_KANBAN_CARD_SETTER

bool KanbanDataSource::SetCardLane(int cardId, int laneId) {
    KanbanCard* card = FindCard(cardId);
    if (!card) return false;
    if (laneId >= 0 && !FindLane(laneId)) return false;
    card->laneId = laneId;
    Touch();
    return true;
}

bool KanbanDataSource::MoveCard(int cardId, int toColumnId, int index) {
    KanbanCard* card = FindCard(cardId);
    if (!card || !FindColumn(toColumnId)) return false;

    int fromColumnId = card->columnId;
    auto& fromOrder = OrderOf(fromColumnId);
    fromOrder.erase(std::remove(fromOrder.begin(), fromOrder.end(), cardId),
                    fromOrder.end());

    auto& toOrder = OrderOf(toColumnId);
    if (index < 0 || index > static_cast<int>(toOrder.size())) {
        toOrder.push_back(cardId);
    } else {
        toOrder.insert(toOrder.begin() + index, cardId);
    }

    if (fromColumnId != toColumnId) {
        card->columnId = toColumnId;
        card->enteredColumn = currentDate;
        RecordMove(cardId, fromColumnId, toColumnId);
    }
    Touch();
    return true;
}

std::vector<int> KanbanDataSource::GetCardsInColumn(int columnId, int laneId) const {
    std::vector<int> result;
    auto it = cardOrder.find(columnId);
    if (it == cardOrder.end()) return result;
    for (int cardId : it->second) {
        const KanbanCard* card = FindCard(cardId);
        if (!card) continue;
        if (laneId != -2 && card->laneId != laneId) continue;
        result.push_back(cardId);
    }
    return result;
}

int KanbanDataSource::CardCountInColumn(int columnId) const {
    auto it = cardOrder.find(columnId);
    return it == cardOrder.end() ? 0 : static_cast<int>(it->second.size());
}

bool KanbanDataSource::IsWipExceeded(int columnId) const {
    const KanbanColumn* col = FindColumn(columnId);
    if (!col || col->wipLimit <= 0) return false;
    return CardCountInColumn(columnId) > col->wipLimit;
}

std::vector<int>& KanbanDataSource::OrderOf(int columnId) {
    return cardOrder[columnId];
}

void KanbanDataSource::RecordMove(int cardId, int fromColumnId, int toColumnId) {
    KanbanMoveRecord rec;
    rec.cardId = cardId;
    rec.fromColumnId = fromColumnId;
    rec.toColumnId = toColumnId;
    rec.date = currentDate;
    history.push_back(rec);
}

// =============================================================================
// DATA SOURCE — FLOW METRICS
// =============================================================================

int KanbanDataSource::EffectiveCommitmentColumn() const {
    if (commitmentColumnId >= 0 && FindColumn(commitmentColumnId)) {
        return commitmentColumnId;
    }
    if (columns.empty()) return -1;
    return columns.size() > 1 ? columns[1].id : columns[0].id;
}

int KanbanDataSource::EffectiveDeliveryColumn() const {
    if (deliveryColumnId >= 0 && FindColumn(deliveryColumnId)) {
        return deliveryColumnId;
    }
    return columns.empty() ? -1 : columns.back().id;
}

std::map<int, int> KanbanDataSource::GetColumnCountsAt(const GanttDate& date) const {
    std::map<int, int> cardColumn;   // cardId -> columnId
    for (const auto& rec : history) {
        if (rec.date > date) break;  // History is chronological
        if (rec.toColumnId < 0) {
            cardColumn.erase(rec.cardId);
        } else {
            cardColumn[rec.cardId] = rec.toColumnId;
        }
    }
    std::map<int, int> counts;
    for (const auto& col : columns) counts[col.id] = 0;
    for (const auto& [cardId, columnId] : cardColumn) {
        auto it = counts.find(columnId);
        if (it != counts.end()) ++it->second;
    }
    return counts;
}

// First history date the card reached (or passed) `columnIndex`; -1 stays
// unset. `strictlyAfter` measures the first move beyond the boundary.
long KanbanDataSource::CardLeadTimeDays(int cardId) const {
    int commitId = EffectiveCommitmentColumn();
    int deliverId = EffectiveDeliveryColumn();
    int commitIndex = ColumnIndexOf(commitId);
    int deliverIndex = ColumnIndexOf(deliverId);
    if (commitIndex < 0 || deliverIndex < 0) return -1;

    bool haveStart = false, haveEnd = false;
    GanttDate start, end;
    for (const auto& rec : history) {
        if (rec.cardId != cardId || rec.toColumnId < 0) continue;
        int index = ColumnIndexOf(rec.toColumnId);
        if (index < 0) continue;
        if (!haveStart && index >= commitIndex) {
            start = rec.date;
            haveStart = true;
        }
        if (!haveEnd && index >= deliverIndex) {
            end = rec.date;
            haveEnd = true;
        }
    }
    if (!haveStart || !haveEnd) return -1;
    return std::max(0L, start.DaysUntil(end));
}

long KanbanDataSource::CardCycleTimeDays(int cardId) const {
    int commitId = EffectiveCommitmentColumn();
    int deliverId = EffectiveDeliveryColumn();
    int commitIndex = ColumnIndexOf(commitId);
    int deliverIndex = ColumnIndexOf(deliverId);
    if (commitIndex < 0 || deliverIndex < 0) return -1;

    bool haveStart = false, haveEnd = false;
    GanttDate start, end;
    for (const auto& rec : history) {
        if (rec.cardId != cardId || rec.toColumnId < 0) continue;
        int index = ColumnIndexOf(rec.toColumnId);
        if (index < 0) continue;
        if (!haveStart && index > commitIndex) {   // Active work started
            start = rec.date;
            haveStart = true;
        }
        if (!haveEnd && index >= deliverIndex) {
            end = rec.date;
            haveEnd = true;
        }
    }
    if (!haveStart || !haveEnd) return -1;
    return std::max(0L, start.DaysUntil(end));
}

ChartDataPoint KanbanDataSource::GetPoint(size_t index) {
    if (index >= cards.size()) return ChartDataPoint(0, 0);
    const KanbanCard& c = cards[index];
    return ChartDataPoint(static_cast<double>(index),
                          static_cast<double>(ColumnIndexOf(c.columnId)), 0.0,
                          c.title);
}

// =============================================================================
// DATA SOURCE — MERMAID TEXT DEFINITIONS
// =============================================================================

namespace {

    struct ParsedItem {
        std::string id;
        std::string label;
        std::vector<std::pair<std::string, std::string>> meta;
        bool metaError = false;
    };

    // Parses `id[Label]@{ key: 'value', ... }` where every part is optional
    // (bare text becomes the label).
    ParsedItem ParseKanbanItem(const std::string& line) {
        ParsedItem item;
        std::string s = TrimText(line);

        size_t open = s.find('[');
        size_t metaAt = std::string::npos;
        if (open != std::string::npos) {
            size_t close = s.find(']', open);
            if (close == std::string::npos) {
                item.metaError = true;   // Unclosed bracket
                item.label = TrimText(s.substr(open + 1));
                item.id = TrimText(s.substr(0, open));
                return item;
            }
            item.id = TrimText(s.substr(0, open));
            item.label = TrimText(s.substr(open + 1, close - open - 1));
            metaAt = s.find("@{", close);
        } else {
            size_t at = s.find("@{");
            item.label = TrimText(at == std::string::npos ? s : s.substr(0, at));
            item.id = item.label;
            metaAt = at;
        }
        if (item.id.empty()) item.id = item.label;

        if (metaAt != std::string::npos) {
            size_t metaClose = s.find('}', metaAt);
            if (metaClose == std::string::npos) {
                item.metaError = true;
                return item;
            }
            std::string body = s.substr(metaAt + 2, metaClose - metaAt - 2);
            // Split on commas that are not inside quotes.
            std::vector<std::string> parts;
            std::string current;
            char quote = 0;
            for (char c : body) {
                if (quote) {
                    if (c == quote) quote = 0;
                    current += c;
                } else if (c == '\'' || c == '"') {
                    quote = c;
                    current += c;
                } else if (c == ',') {
                    parts.push_back(current);
                    current.clear();
                } else {
                    current += c;
                }
            }
            parts.push_back(current);

            for (const auto& part : parts) {
                size_t colon = part.find(':');
                if (colon == std::string::npos) continue;
                std::string key = ToUpper(TrimText(part.substr(0, colon)));
                std::string value = TrimText(part.substr(colon + 1));
                if (value.size() >= 2 &&
                    (value.front() == '\'' || value.front() == '"') &&
                    value.back() == value.front()) {
                    value = value.substr(1, value.size() - 2);
                }
                item.meta.emplace_back(key, value);
            }
        }
        return item;
    }

    int LineIndent(const std::string& line) {
        int indent = 0;
        for (char c : line) {
            if (c == ' ') ++indent;
            else if (c == '\t') indent += 4;
            else break;
        }
        return indent;
    }

} // namespace

bool KanbanDataSource::LoadFromText(const std::string& text, std::string* error) {
    auto fail = [&](int lineNo, const std::string& message) {
        if (error) {
            *error = "line " + std::to_string(lineNo) + ": " + message;
        }
        return false;
    };

    std::vector<std::string> rawLines;
    {
        std::stringstream ss(text);
        std::string line;
        while (std::getline(ss, line)) rawLines.push_back(line);
    }

    // Locate the `kanban` header.
    size_t first = 0;
    bool headerFound = false;
    for (; first < rawLines.size(); ++first) {
        std::string t = TrimText(rawLines[first]);
        if (t.empty() || t.rfind("%%", 0) == 0) continue;
        if (t == "kanban" || t.rfind("kanban ", 0) == 0) {
            headerFound = true;
            ++first;
            break;
        }
        return fail(static_cast<int>(first + 1),
                    "expected the text to start with 'kanban'");
    }
    if (!headerFound) {
        if (error) *error = "empty definition: no 'kanban' header found";
        return false;
    }

    Clear();

    int columnIndent = -1;
    int currentColumnId = -1;
    for (size_t i = first; i < rawLines.size(); ++i) {
        const std::string& raw = rawLines[i];
        std::string trimmed = TrimText(raw);
        int lineNo = static_cast<int>(i + 1);
        if (trimmed.empty() || trimmed.rfind("%%", 0) == 0) continue;

        int indent = LineIndent(raw);
        ParsedItem item = ParseKanbanItem(trimmed);
        if (item.metaError) return fail(lineNo, "malformed item: '" + trimmed + "'");
        if (item.label.empty()) return fail(lineNo, "item has no text");

        if (columnIndent < 0) columnIndent = indent;

        if (indent <= columnIndent) {
            // Column line.
            int wip = 0;
            Color color = Colors::Transparent;
            std::string footer;
            for (const auto& [key, value] : item.meta) {
                if (key == "WIP") wip = std::atoi(value.c_str());
                else if (key == "COLOR") color = ParseHexColor(value);
                else if (key == "FOOTER" || key == "CAPTION") footer = value;
            }
            currentColumnId = AddColumn(item.label, wip, color);
            if (!footer.empty()) SetColumnFooterCaption(currentColumnId, footer);
        } else {
            // Card line.
            if (currentColumnId < 0) {
                return fail(lineNo, "card defined before any column");
            }
            int cardId = AddCard(currentColumnId, item.label);
            for (const auto& [key, value] : item.meta) {
                if (key == "ASSIGNED" || key == "ASSIGNEE") {
                    SetCardAssignee(cardId, value);
                } else if (key == "PRIORITY") {
                    SetCardPriority(cardId, PriorityFromText(value));
                } else if (key == "DUE") {
                    GanttDate d;
                    if (!ParseISODate(value, d)) {
                        return fail(lineNo, "invalid due date '" + value +
                                            "' (expected YYYY-MM-DD)");
                    }
                    SetCardDueDate(cardId, d);
                } else if (key == "TICKET" || key == "LABEL" || key == "TAG") {
                    AddCardTag(cardId, value);
                } else if (key == "DESCRIPTION" || key == "DESC") {
                    SetCardDescription(cardId, value);
                } else if (key == "COLOR") {
                    SetCardColor(cardId, ParseHexColor(value));
                } else if (key == "BLOCKED") {
                    SetCardBlocked(cardId, ToUpper(value) != "FALSE", "");
                }
            }
        }
    }

    if (columns.empty()) {
        if (error) *error = "definition contains no columns";
        return false;
    }
    return true;
}

// =============================================================================
// DATA SOURCE — JSON PERSISTENCE
// =============================================================================

std::string KanbanDataSource::ToJSON(bool pretty) const {
    JSONValue doc = JSONValue::MakeObject();
    doc.Set("type", "kanban-board");
    doc.Set("version", static_cast<int64_t>(1));
    if (currentDate.serial != 0) doc.Set("currentDate", FormatISODate(currentDate));
    if (commitmentColumnId >= 0) {
        doc.Set("commitmentColumn", static_cast<int64_t>(commitmentColumnId));
    }
    if (deliveryColumnId >= 0) {
        doc.Set("deliveryColumn", static_cast<int64_t>(deliveryColumnId));
    }

    JSONValue jsonColumns = JSONValue::MakeArray();
    for (const auto& col : columns) {
        JSONValue jc = JSONValue::MakeObject();
        jc.Set("id", static_cast<int64_t>(col.id));
        jc.Set("title", col.title);
        if (col.wipLimit > 0) jc.Set("wipLimit", static_cast<int64_t>(col.wipLimit));
        if (col.customColor.a != 0) jc.Set("color", JSON::FromColor(col.customColor));
        if (!col.footerCaption.empty()) jc.Set("footerCaption", col.footerCaption);
        JSONValue order = JSONValue::MakeArray();
        auto it = cardOrder.find(col.id);
        if (it != cardOrder.end()) {
            for (int cardId : it->second) order.Append(static_cast<int64_t>(cardId));
        }
        jc.Set("cards", order);
        jsonColumns.Append(jc);
    }
    doc.Set("columns", jsonColumns);

    if (!lanes.empty()) {
        JSONValue jsonLanes = JSONValue::MakeArray();
        for (const auto& lane : lanes) {
            JSONValue jl = JSONValue::MakeObject();
            jl.Set("id", static_cast<int64_t>(lane.id));
            jl.Set("title", lane.title);
            jsonLanes.Append(jl);
        }
        doc.Set("lanes", jsonLanes);
    }

    JSONValue jsonCards = JSONValue::MakeArray();
    for (const auto& card : cards) {
        JSONValue jc = JSONValue::MakeObject();
        jc.Set("id", static_cast<int64_t>(card.id));
        jc.Set("columnId", static_cast<int64_t>(card.columnId));
        if (card.laneId >= 0) jc.Set("laneId", static_cast<int64_t>(card.laneId));
        jc.Set("title", card.title);
        if (!card.description.empty()) jc.Set("description", card.description);
        if (!card.assignee.empty()) jc.Set("assignee", card.assignee);
        if (!card.tags.empty()) {
            JSONValue tags = JSONValue::MakeArray();
            for (const auto& tag : card.tags) tags.Append(tag);
            jc.Set("tags", tags);
        }
        if (card.priority != KanbanPriority::NoPriority) {
            jc.Set("priority", PriorityName(card.priority));
        }
        if (card.hasDueDate) jc.Set("dueDate", FormatISODate(card.dueDate));
        if (card.customColor.a != 0) jc.Set("color", JSON::FromColor(card.customColor));
        if (card.blocked) {
            jc.Set("blocked", true);
            if (!card.blockedReason.empty()) jc.Set("blockedReason", card.blockedReason);
        }
        if (card.enteredColumn.serial != 0) {
            jc.Set("enteredColumn", FormatISODate(card.enteredColumn));
        }
        jsonCards.Append(jc);
    }
    doc.Set("cards", jsonCards);

    if (!history.empty()) {
        JSONValue jsonHistory = JSONValue::MakeArray();
        for (const auto& rec : history) {
            JSONValue jr = JSONValue::MakeObject();
            jr.Set("cardId", static_cast<int64_t>(rec.cardId));
            jr.Set("from", static_cast<int64_t>(rec.fromColumnId));
            jr.Set("to", static_cast<int64_t>(rec.toColumnId));
            if (rec.date.serial != 0) jr.Set("date", FormatISODate(rec.date));
            jsonHistory.Append(jr);
        }
        doc.Set("history", jsonHistory);
    }

    JSONSerializeOptions options;
    options.pretty = pretty;
    return JSON::Serialize(doc, options);
}

bool KanbanDataSource::LoadFromJSONText(const std::string& jsonText,
                                        std::string* error) {
    JSONParseResult result;
    JSONValue doc = JSON::Parse(jsonText, &result);
    if (!result.success) {
        if (error) *error = result.errorMessage;
        return false;
    }
    if (doc["type"].GetString("") != "kanban-board") {
        if (error) *error = "not a kanban-board document";
        return false;
    }

    Clear();

    GanttDate parsedDate;
    if (ParseISODate(doc["currentDate"].GetString(""), parsedDate)) {
        currentDate = parsedDate;
    }

    const JSONValue& jsonColumns = doc["columns"];
    for (size_t i = 0; i < jsonColumns.GetSize(); ++i) {
        const JSONValue& jc = jsonColumns[i];
        KanbanColumn col;
        col.id = static_cast<int>(jc["id"].GetInteger(-1));
        col.title = jc["title"].GetString("");
        col.wipLimit = static_cast<int>(jc["wipLimit"].GetInteger(0));
        if (jc.Contains("color")) JSON::ToColor(jc["color"], col.customColor);
        col.footerCaption = jc["footerCaption"].GetString("");
        if (col.id < 0) {
            if (error) *error = "column without id";
            Clear();
            return false;
        }
        columns.push_back(col);
        cardOrder[col.id] = {};
        nextColumnId = std::max(nextColumnId, col.id + 1);
    }
    if (columns.empty()) {
        if (error) *error = "document contains no columns";
        return false;
    }

    const JSONValue& jsonLanes = doc["lanes"];
    for (size_t i = 0; i < jsonLanes.GetSize(); ++i) {
        KanbanLane lane;
        lane.id = static_cast<int>(jsonLanes[i]["id"].GetInteger(-1));
        lane.title = jsonLanes[i]["title"].GetString("");
        if (lane.id < 0) continue;
        lanes.push_back(lane);
        nextLaneId = std::max(nextLaneId, lane.id + 1);
    }

    const JSONValue& jsonCards = doc["cards"];
    for (size_t i = 0; i < jsonCards.GetSize(); ++i) {
        const JSONValue& jc = jsonCards[i];
        KanbanCard card;
        card.id = static_cast<int>(jc["id"].GetInteger(-1));
        card.columnId = static_cast<int>(jc["columnId"].GetInteger(-1));
        card.laneId = static_cast<int>(jc["laneId"].GetInteger(-1));
        card.title = jc["title"].GetString("");
        card.description = jc["description"].GetString("");
        card.assignee = jc["assignee"].GetString("");
        const JSONValue& tags = jc["tags"];
        for (size_t t = 0; t < tags.GetSize(); ++t) {
            card.tags.push_back(tags[t].GetString(""));
        }
        card.priority = PriorityFromText(jc["priority"].GetString("None"));
        if (ParseISODate(jc["dueDate"].GetString(""), card.dueDate)) {
            card.hasDueDate = true;
        }
        if (jc.Contains("color")) JSON::ToColor(jc["color"], card.customColor);
        card.blocked = jc["blocked"].GetBoolean(false);
        card.blockedReason = jc["blockedReason"].GetString("");
        ParseISODate(jc["enteredColumn"].GetString(""), card.enteredColumn);
        if (card.id < 0 || !FindColumn(card.columnId)) continue;
        if (card.laneId >= 0 && !FindLane(card.laneId)) card.laneId = -1;
        cards.push_back(card);
        nextCardId = std::max(nextCardId, card.id + 1);
    }

    // Card order per column; cards missing from the order arrays are appended.
    for (size_t i = 0; i < jsonColumns.GetSize(); ++i) {
        const JSONValue& jc = jsonColumns[i];
        int columnId = static_cast<int>(jc["id"].GetInteger(-1));
        const JSONValue& order = jc["cards"];
        for (size_t k = 0; k < order.GetSize(); ++k) {
            int cardId = static_cast<int>(order[k].GetInteger(-1));
            const KanbanCard* card = FindCard(cardId);
            if (card && card->columnId == columnId) {
                cardOrder[columnId].push_back(cardId);
            }
        }
    }
    for (const auto& card : cards) {
        auto& order = cardOrder[card.columnId];
        if (std::find(order.begin(), order.end(), card.id) == order.end()) {
            order.push_back(card.id);
        }
    }

    const JSONValue& jsonHistory = doc["history"];
    for (size_t i = 0; i < jsonHistory.GetSize(); ++i) {
        const JSONValue& jr = jsonHistory[i];
        KanbanMoveRecord rec;
        rec.cardId = static_cast<int>(jr["cardId"].GetInteger(-1));
        rec.fromColumnId = static_cast<int>(jr["from"].GetInteger(-1));
        rec.toColumnId = static_cast<int>(jr["to"].GetInteger(-1));
        ParseISODate(jr["date"].GetString(""), rec.date);
        if (rec.cardId >= 0) history.push_back(rec);
    }

    commitmentColumnId = static_cast<int>(doc["commitmentColumn"].GetInteger(-1));
    deliveryColumnId = static_cast<int>(doc["deliveryColumn"].GetInteger(-1));

    Touch();
    return true;
}

bool KanbanDataSource::SaveToJSONFile(const std::string& filePath) const {
    JSONParseResult ignored;
    JSONValue doc = JSON::Parse(ToJSON(false), &ignored);
    JSONSerializeOptions pretty;
    pretty.pretty = true;
    return JSON::SerializeToFile(filePath, doc, pretty);
}

bool KanbanDataSource::LoadFromJSONFile(const std::string& filePath,
                                        std::string* error) {
    JSONParseResult result;
    JSONValue doc = JSON::ParseFile(filePath, &result);
    if (!result.success) {
        if (error) *error = result.errorMessage;
        return false;
    }
    JSONSerializeOptions compact;
    return LoadFromJSONText(JSON::Serialize(doc, compact), error);
}

// =============================================================================
// PALETTES
// =============================================================================

namespace KanbanPalettes {

    const std::vector<Color>& Get(KanbanPalette palette) {
        static const std::vector<Color> vibrant = {
                Color(66, 165, 245), Color(126, 87, 194), Color(38, 132, 150),
                Color(239, 83, 80), Color(38, 166, 91), Color(255, 167, 38),
                Color(0, 151, 167), Color(236, 64, 122)};
        static const std::vector<Color> pastel = {
                Color(129, 199, 245), Color(179, 157, 219), Color(244, 143, 177),
                Color(255, 213, 128), Color(165, 214, 167), Color(128, 203, 196)};
        static const std::vector<Color> corporate = {
                Color(41, 98, 155), Color(66, 133, 190), Color(100, 160, 210),
                Color(140, 185, 225), Color(30, 70, 120)};
        static const std::vector<Color> ocean = {
                Color(0, 121, 140), Color(0, 150, 167), Color(38, 166, 154),
                Color(77, 182, 172), Color(0, 96, 120)};
        static const std::vector<Color> sunset = {
                Color(239, 108, 0), Color(230, 74, 69), Color(216, 67, 121),
                Color(255, 143, 60), Color(156, 39, 96)};
        static const std::vector<Color> slate = {
                Color(84, 100, 122), Color(105, 122, 143), Color(69, 84, 104),
                Color(127, 143, 163), Color(55, 68, 86)};
        static const std::vector<Color> mono = {
                Color(70, 70, 74), Color(105, 105, 110), Color(140, 140, 146),
                Color(175, 175, 180), Color(50, 50, 54)};
        switch (palette) {
            case KanbanPalette::Pastel:        return pastel;
            case KanbanPalette::CorporateBlue: return corporate;
            case KanbanPalette::Ocean:         return ocean;
            case KanbanPalette::Sunset:        return sunset;
            case KanbanPalette::Slate:         return slate;
            case KanbanPalette::Mono:          return mono;
            case KanbanPalette::Vibrant:
            default:                           return vibrant;
        }
    }

    const std::vector<Color>& PastelCards() {
        static const std::vector<Color> colors = {
                Color(255, 224, 178), Color(200, 230, 201), Color(187, 222, 251),
                Color(248, 187, 208), Color(255, 245, 157), Color(209, 196, 233),
                Color(178, 235, 242)};
        return colors;
    }

    const std::vector<Color>& PriorityColors() {
        // NoPriority, Low, Normal, High, Urgent
        static const std::vector<Color> colors = {
                Color(158, 158, 158), Color(139, 195, 74), Color(66, 165, 245),
                Color(255, 152, 0), Color(229, 57, 53)};
        return colors;
    }

} // namespace KanbanPalettes

// =============================================================================
// DESIGN PRESETS
// =============================================================================

namespace KanbanBoardStyles {

    KanbanBoardStyle CreateProfessional() {
        KanbanBoardStyle s;
        s.headerShape = KanbanHeaderShape::Pill;
        s.columnBackground = KanbanColumnBackground::Track;
        s.columnTrackColor = Color(243, 244, 246);
        s.cardShape = KanbanCardShape::Rounded;
        s.cardColorMode = KanbanCardColorMode::Fixed;
        s.cardBackground = Colors::White;
        s.cardTitleColor = Color(35, 40, 48);
        s.cardBorderColor = Color(225, 227, 232);
        s.cardBorderWidth = 1.0f;
        s.cardShadow = true;
        s.cardShadowColor = Color(20, 25, 40, 22);
        s.cardShadowOffset = 2.0f;
        s.showCardCount = true;
        s.showWipBadge = true;
        s.priorityMarker = KanbanPriorityMarker::Chip;
        s.dueDateLabel = "";
        s.palette = KanbanPalettes::Get(KanbanPalette::Vibrant);
        return s;
    }

    KanbanBoardStyle CreateStickyNotes() {
        KanbanBoardStyle s;
        s.headerShape = KanbanHeaderShape::Bar;
        s.headerHeight = 22.0f;
        s.headerGap = 8.0f;
        s.uppercaseHeaders = true;
        s.headerFontSize = 10.5f;
        s.columnBackground = KanbanColumnBackground::Track;
        s.columnTrackColor = Color(250, 250, 251);
        s.columnCornerRadius = 4.0f;
        s.cardLayout = KanbanCardLayout::Stagger;
        s.staggerOffset = 22.0f;
        s.cardShape = KanbanCardShape::StickyNote;
        s.cardColorMode = KanbanCardColorMode::ByCard;
        s.cardShadow = true;
        s.cardShadowColor = Color(30, 30, 50, 26);
        s.cardShadowOffset = 2.0f;
        s.cardCornerRadius = 2.0f;
        s.showCardDescription = false;
        s.showDueDate = false;
        s.showAssignee = false;
        s.showTags = false;
        s.priorityMarker = KanbanPriorityMarker::Hidden;
        s.showWipBadge = false;
        s.palette = KanbanPalettes::Get(KanbanPalette::Pastel);
        s.cardPalette = KanbanPalettes::PastelCards();
        return s;
    }

    KanbanBoardStyle CreatePoster() {
        KanbanBoardStyle s;
        s.headerShape = KanbanHeaderShape::PillWithDot;
        s.headerHeight = 34.0f;
        s.headerGap = 16.0f;
        s.uppercaseHeaders = true;
        s.headerFontSize = 14.0f;
        s.columnBackground = KanbanColumnBackground::Panel;
        s.columnCornerRadius = 0.0f;
        s.cardShape = KanbanCardShape::DogEar;
        s.cardColorMode = KanbanCardColorMode::Fixed;
        s.cardBackground = Colors::White;
        s.cardLayout = KanbanCardLayout::Stagger;
        s.staggerOffset = 30.0f;
        s.cardShadow = true;
        s.cardCornerRadius = 8.0f;
        s.cardPadding = 12.0f;
        s.showTitle = true;
        s.titleHeight = 46.0f;
        s.titleFontSize = 30.0f;
        s.showPriorityLegend = true;
        s.legendHeight = 24.0f;
        s.showCardDescription = true;
        s.descriptionMaxLines = 3;
        s.showDueDate = true;
        s.dueDateLabel = "Due Date:";
        s.showAssignee = false;
        s.showTags = false;
        s.priorityMarker = KanbanPriorityMarker::Dot;
        s.priorityDotRadius = 7.0f;
        s.showWipBadge = false;
        s.palette = KanbanPalettes::Get(KanbanPalette::Vibrant);
        return s;
    }

    KanbanBoardStyle CreateSchematic() {
        KanbanBoardStyle s;
        s.headerShape = KanbanHeaderShape::TextRow;
        s.headerHeight = 40.0f;
        s.headerGap = 14.0f;
        s.headerTextColor = Color(40, 42, 48);
        s.headerFontSize = 15.0f;
        s.columnBackground = KanbanColumnBackground::Separators;
        s.separatorColor = Color(222, 222, 226);
        s.separatorDashed = true;
        s.boardPanelColor = Color(238, 238, 240);
        s.boardPanelRadius = 16.0f;
        s.cardLayout = KanbanCardLayout::Grid;
        s.cardShape = KanbanCardShape::Rounded;
        s.cardColorMode = KanbanCardColorMode::ByCard;
        s.cardMinHeight = 52.0f;
        s.cardCornerRadius = 6.0f;
        s.showCardDescription = false;
        s.showDueDate = false;
        s.showAssignee = false;
        s.showTags = false;
        s.priorityMarker = KanbanPriorityMarker::Hidden;
        s.showWipBadge = false;
        s.cardTitleColor = Colors::White;
        s.palette = KanbanPalettes::Get(KanbanPalette::Slate);
        s.cardPalette = {Color(231, 111, 138), Color(126, 100, 180),
                         Color(88, 178, 138), Color(240, 168, 48),
                         Color(74, 125, 190), Color(78, 182, 182)};
        return s;
    }

    KanbanBoardStyle CreateTimeline() {
        KanbanBoardStyle s;
        s.headerShape = KanbanHeaderShape::Pill;
        s.headerHeight = 26.0f;
        s.uppercaseHeaders = true;
        s.headerFontSize = 11.0f;
        s.columnBackground = KanbanColumnBackground::Plain;
        s.cardShape = KanbanCardShape::Pill;
        s.cardColorMode = KanbanCardColorMode::ByColumn;
        s.cardLayout = KanbanCardLayout::Stack;
        s.cardMinHeight = 24.0f;
        s.cardGap = 6.0f;
        s.cardTitleFontSize = 10.5f;
        s.showCardDescription = false;
        s.showDueDate = false;
        s.showAssignee = false;
        s.showTags = false;
        s.priorityMarker = KanbanPriorityMarker::Hidden;
        s.showWipBadge = false;
        s.laneHeaderWidth = 114.0f;
        s.laneMinHeight = 66.0f;
        s.laneFontSize = 11.5f;
        s.footerHeight = 22.0f;
        s.palette = {Color(124, 179, 66), Color(66, 133, 244),
                     Color(230, 124, 34), Color(244, 180, 0)};
        return s;
    }

    KanbanBoardStyle CreateDark() {
        KanbanBoardStyle s = CreateProfessional();
        s.background = Color(30, 32, 38);
        s.columnTrackColor = Color(42, 45, 53);
        s.cardBackground = Color(55, 59, 69);
        s.cardTitleColor = Color(235, 237, 240);
        s.cardTextColor = Color(170, 175, 184);
        s.cardBorderColor = Color(70, 74, 84);
        s.cardShadowColor = Color(0, 0, 0, 60);
        s.headerTextColor = Colors::White;
        s.legendTextColor = Color(200, 203, 210);
        s.titleColor = Color(230, 232, 236);
        s.separatorColor = Color(60, 64, 74);
        s.addButtonColor = Color(150, 155, 165);
        s.editorPanelColor = Color(44, 47, 55);
        s.editorFieldColor = Color(58, 62, 72);
        s.editorTextColor = Color(225, 227, 232);
        s.laneSeparatorColor = Color(58, 62, 72);
        return s;
    }

    KanbanBoardStyle Create(KanbanDesign design) {
        switch (design) {
            case KanbanDesign::StickyNotes: return CreateStickyNotes();
            case KanbanDesign::Poster:      return CreatePoster();
            case KanbanDesign::Schematic:   return CreateSchematic();
            case KanbanDesign::Timeline:    return CreateTimeline();
            case KanbanDesign::Dark:        return CreateDark();
            case KanbanDesign::Professional:
            default:                        return CreateProfessional();
        }
    }

} // namespace KanbanBoardStyles

// =============================================================================
// ELEMENT — LIFECYCLE, STYLE, DATA
// =============================================================================

UltraCanvasKanbanBoardElement::UltraCanvasKanbanBoardElement(
        const std::string& id, int x, int y, int width, int height)
        : UltraCanvasChartElementBase(id, x, y, width, height) {
    style = KanbanBoardStyles::CreateProfessional();
    showBackground = false;
    showGrid = false;
    showAxes = false;
}

void UltraCanvasKanbanBoardElement::SetKanbanDataSource(
        std::shared_ptr<KanbanDataSource> ds) {
    dataSource = ds;
    columnScroll.clear();
    scrollX = 0;
    selectedCardId = -1;
    hoveredCardId = -1;
    RequestRedraw();
}

void UltraCanvasKanbanBoardElement::SetStyle(const KanbanBoardStyle& newStyle) {
    style = newStyle;
    RequestRedraw();
}

void UltraCanvasKanbanBoardElement::StyleChanged() {
    RequestRedraw();
}

void UltraCanvasKanbanBoardElement::ApplyDesign(KanbanDesign design) {
    style = KanbanBoardStyles::Create(design);
    RequestRedraw();
}

void UltraCanvasKanbanBoardElement::SetPalette(KanbanPalette palette) {
    style.palette = KanbanPalettes::Get(palette);
    RequestRedraw();
}

void UltraCanvasKanbanBoardElement::SetCustomPalette(const std::vector<Color>& colors) {
    if (!colors.empty()) style.palette = colors;
    RequestRedraw();
}

void UltraCanvasKanbanBoardElement::SetToday(const GanttDate& date) {
    today = date;
    todaySet = true;
    auto ds = GetKanbanDataSource();
    if (ds && ds->GetCurrentDate().serial == 0) ds->SetCurrentDate(date);
    RequestRedraw();
}

void UltraCanvasKanbanBoardElement::SetEditable(bool value) {
    editable = value;
    if (!editable) {
        CloseCardEditor(false);
        CommitRename(false);
        draggingCard = false;
        pressedCardId = -1;
    }
    RequestRedraw();
}

void UltraCanvasKanbanBoardElement::SelectCard(int cardId) {
    auto ds = GetKanbanDataSource();
    if (cardId >= 0 && (!ds || !ds->FindCard(cardId))) cardId = -1;
    if (cardId == selectedCardId) return;
    selectedCardId = cardId;
    if (onSelectionChanged) onSelectionChanged(selectedCardId);
    RequestRedraw();
}

void UltraCanvasKanbanBoardElement::ScrollToCard(int cardId) {
    auto ds = GetKanbanDataSource();
    if (!ds) return;
    const KanbanCard* card = ds->FindCard(cardId);
    if (!card) return;
    // Vertical position: sum heights is layout-dependent; rely on the cached
    // layout (valid after the first render).
    for (const auto& cl : layout.cards) {
        if (cl.cardId != cardId) continue;
        if (cl.columnIndex >= 0 &&
            cl.columnIndex < static_cast<int>(layout.columns.size())) {
            const ColumnLayout& col = layout.columns[cl.columnIndex];
            float& scroll = columnScroll[col.columnId];
            double relative = cl.rect.y + scroll - col.bodyRect.y;
            scroll = static_cast<float>(std::max(0.0, relative - 10.0));
            ClampScroll();
        }
        break;
    }
    RequestRedraw();
}

// =============================================================================
// ELEMENT — COLOR / FORMAT HELPERS
// =============================================================================

Color UltraCanvasKanbanBoardElement::ColumnColor(size_t columnIndex,
                                                 const KanbanColumn& col) const {
    if (col.customColor.a != 0) return col.customColor;
    const std::vector<Color>& palette =
            style.palette.empty() ? KanbanPalettes::Get(KanbanPalette::Vibrant)
                                  : style.palette;
    return palette[columnIndex % palette.size()];
}

Color UltraCanvasKanbanBoardElement::CardFaceColor(const KanbanCard& card,
                                                   size_t cardOrdinal,
                                                   size_t columnIndex) const {
    auto ds = GetKanbanDataSource();
    switch (style.cardColorMode) {
        case KanbanCardColorMode::Fixed:
            return style.cardBackground;
        case KanbanCardColorMode::ByColumn: {
            if (!ds || columnIndex >= ds->GetColumns().size()) break;
            return ColumnColor(columnIndex, ds->GetColumns()[columnIndex]);
        }
        case KanbanCardColorMode::ByPriority: {
            const auto& colors = KanbanPalettes::PriorityColors();
            return colors[static_cast<size_t>(card.priority) % colors.size()];
        }
        case KanbanCardColorMode::ByLane: {
            if (!ds) break;
            const std::vector<Color>& palette =
                    style.palette.empty()
                            ? KanbanPalettes::Get(KanbanPalette::Vibrant)
                            : style.palette;
            const auto& lanes = ds->GetLanes();
            for (size_t i = 0; i < lanes.size(); ++i) {
                if (lanes[i].id == card.laneId) return palette[i % palette.size()];
            }
            break;
        }
        case KanbanCardColorMode::ByCard:
        default: {
            if (card.customColor.a != 0) return card.customColor;
            const std::vector<Color>& cardPalette =
                    style.cardPalette.empty() ? KanbanPalettes::PastelCards()
                                              : style.cardPalette;
            return cardPalette[cardOrdinal % cardPalette.size()];
        }
    }
    return style.cardBackground;
}

Color UltraCanvasKanbanBoardElement::CardTitleColor(const KanbanCard& card,
                                                    size_t columnIndex) const {
    if (style.cardTitleColor.a != 0) return style.cardTitleColor;
    Color face = CardFaceColor(card, 0, columnIndex);
    if (!IsLightColor(face)) return Colors::White;
    // Light card faces: white cards take the column accent, tinted cards
    // take a dark neutral.
    if (style.cardColorMode == KanbanCardColorMode::Fixed) {
        auto ds = GetKanbanDataSource();
        if (ds && columnIndex < ds->GetColumns().size()) {
            return ColumnColor(columnIndex, ds->GetColumns()[columnIndex]);
        }
    }
    return Color(45, 48, 56);
}

std::string UltraCanvasKanbanBoardElement::FormatDate(const GanttDate& date) const {
    int y, m, d;
    date.ToYMD(y, m, d);
    char buf[24];
    switch (style.dateFormat) {
        case GanttDateFormat::DayMonthYearSlash:
            std::snprintf(buf, sizeof(buf), "%02d/%02d/%02d", d, m, y % 100);
            break;
        case GanttDateFormat::MonthDayYearSlash:
            std::snprintf(buf, sizeof(buf), "%02d/%02d/%02d", m, d, y % 100);
            break;
        case GanttDateFormat::DayMonthShort: {
            static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May",
                                           "Jun", "Jul", "Aug", "Sep", "Oct",
                                           "Nov", "Dec"};
            std::snprintf(buf, sizeof(buf), "%d %s", d, months[(m - 1) % 12]);
            break;
        }
        case GanttDateFormat::ISO:
        default:
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);
            break;
    }
    return buf;
}

// =============================================================================
// ELEMENT — LAYOUT
// =============================================================================

double UltraCanvasKanbanBoardElement::CardHeight(IRenderContext* ctx,
                                                 const KanbanCard& card,
                                                 double cardWidth) const {
    if (style.cardShape == KanbanCardShape::Pill ||
        style.cardLayout == KanbanCardLayout::Grid) {
        return std::max(20.0f, style.cardMinHeight > 0 ? style.cardMinHeight
                                                       : 44.0f);
    }

    double innerWidth = cardWidth - 2 * style.cardPadding;
    double h = style.cardPadding * 2;

    // Mirror RenderCard's reserved corner space for the priority marker.
    double titleReserve = 0;
    if (card.priority != KanbanPriority::NoPriority) {
        if (style.priorityMarker == KanbanPriorityMarker::Dot) {
            titleReserve = style.priorityDotRadius * 2 + 6;
        } else if (style.priorityMarker == KanbanPriorityMarker::Chip) {
            ctx->SetFontSize(style.metaFontSize * 0.95f);
            titleReserve = ctx->GetTextLineDimensions(
                                   PriorityName(card.priority)).width + 16;
        }
    }

    ctx->SetFontSize(style.cardTitleFontSize);
    ctx->SetFontWeight(FontWeight::Bold);
    double titleLineH = style.cardTitleFontSize * 1.35;
    size_t titleLines = 1;
    if (!card.title.empty()) {
        titleLines = std::max<size_t>(1, WrapTextLines(ctx, card.title,
                                                       innerWidth - titleReserve).size());
        titleLines = std::min<size_t>(titleLines, 3);
    }
    h += titleLines * titleLineH;
    ctx->SetFontWeight(FontWeight::Normal);

    if (style.showCardDescription && !card.description.empty()) {
        ctx->SetFontSize(style.cardTextFontSize);
        size_t lines = WrapTextLines(ctx, card.description, innerWidth).size();
        lines = std::min<size_t>(lines,
                                 static_cast<size_t>(std::max(1, style.descriptionMaxLines)));
        h += 3 + lines * style.cardTextFontSize * 1.35;
    }
    if (style.showDueDate && card.hasDueDate) {
        double metaLineH = style.metaFontSize * 1.4;
        h += 5 + metaLineH;                            // Value line
        if (!style.dueDateLabel.empty()) h += metaLineH; // Label line
    }
    bool avatarRow = style.showAssignee && !card.assignee.empty();
    bool tagRow = style.showTags && !card.tags.empty();
    if (avatarRow || tagRow) {
        h += 6 + std::max(18.0, style.metaFontSize * 1.5);
    }
    return std::max(h, 30.0);
}

void UltraCanvasKanbanBoardElement::ComputeLayout(IRenderContext* ctx) {
    layout = Layout();
    auto ds = GetKanbanDataSource();
    Rect2Df local = GetLocalBounds();

    double y = local.y + style.boardPadding;
    double x = local.x + style.boardPadding;
    double width = local.width - 2 * style.boardPadding;

    if (style.showTitle && style.titleHeight > 0) {
        layout.titleRect = Rect2Dd(x, y, width, style.titleHeight);
        y += style.titleHeight;
    }
    if (style.showPriorityLegend && style.legendHeight > 0) {
        layout.legendRect = Rect2Dd(x, y, width, style.legendHeight);
        y += style.legendHeight;
    }

    layout.boardRect = Rect2Dd(x, y, width,
                               local.y + local.height - style.boardPadding - y);
    if (!ds || ds->GetColumns().empty() || layout.boardRect.height <= 0) return;

    const auto& columns = ds->GetColumns();
    const auto& lanes = ds->GetLanes();
    bool hasLanes = !lanes.empty();
    double laneHeaderW = hasLanes && style.laneHeaderWidth > 0
                                 ? style.laneHeaderWidth : 0.0;
    double addColumnW = editable ? 30.0 : 0.0;

    size_t n = columns.size();
    double columnsX = layout.boardRect.x + laneHeaderW;
    double columnsWidth = layout.boardRect.width - laneHeaderW -
                          (addColumnW > 0 ? addColumnW + style.columnGap : 0.0);
    double colW = style.columnWidth > 0
                          ? style.columnWidth
                          : std::max(style.minColumnWidth,
                                     static_cast<float>((columnsWidth -
                                                         (n - 1) * style.columnGap) / n));

    double contentW = n * colW + (n - 1) * style.columnGap;
    float maxScrollX = static_cast<float>(std::max(0.0, contentW - columnsWidth));
    scrollX = std::clamp(scrollX, 0.0f, maxScrollX);

    double footerH = style.footerHeight;
    double bodyTop = layout.boardRect.y + style.headerHeight + style.headerGap;
    double bodyBottom = layout.boardRect.y + layout.boardRect.height - footerH;
    double addButtonH = editable ? 26.0 : 0.0;

    if (editable) {
        layout.addColumnRect = Rect2Dd(
                layout.boardRect.x + layout.boardRect.width - addColumnW,
                layout.boardRect.y, addColumnW, style.headerHeight);
    }

    // Column frames.
    for (size_t i = 0; i < n; ++i) {
        ColumnLayout cl;
        cl.columnId = columns[i].id;
        double cx = columnsX + i * (colW + style.columnGap) - scrollX;
        cl.headerRect = Rect2Dd(cx, layout.boardRect.y, colW, style.headerHeight);
        cl.bodyRect = Rect2Dd(cx, bodyTop, colW,
                              bodyBottom - bodyTop - addButtonH);
        if (editable) {
            cl.addButtonRect = Rect2Dd(cx, bodyBottom - addButtonH + 2, colW,
                                       addButtonH - 4);
        }
        if (footerH > 0) {
            cl.footerRect = Rect2Dd(cx, bodyBottom, colW, footerH);
        }
        layout.columns.push_back(cl);
    }

    // Card geometry. Card x/width per layout mode.
    auto cardFrame = [&](const ColumnLayout& cl, size_t ordinal,
                         double& outX, double& outW) {
        outX = cl.bodyRect.x + style.cardSidePadding;
        outW = cl.bodyRect.width - 2 * style.cardSidePadding;
        if (style.cardLayout == KanbanCardLayout::Stagger) {
            outW -= style.staggerOffset;
            if (ordinal % 2 == 1) outX += style.staggerOffset;
        }
    };

    if (!hasLanes) {
        for (size_t i = 0; i < n; ++i) {
            ColumnLayout& cl = layout.columns[i];
            std::vector<int> ids = ds->GetCardsInColumn(cl.columnId);
            double cursor = cl.bodyRect.y + style.cardGap -
                            columnScroll[cl.columnId];
            double contentH = style.cardGap;
            for (size_t k = 0; k < ids.size(); ++k) {
                const KanbanCard* card = ds->FindCard(ids[k]);
                if (!card) continue;
                double cx, cw;
                cardFrame(cl, k, cx, cw);
                double ch = CardHeight(ctx, *card, cw);
                CardLayout cardLayout;
                cardLayout.cardId = card->id;
                cardLayout.columnIndex = static_cast<int>(i);
                cardLayout.rect = Rect2Dd(cx, cursor, cw, ch);
                layout.cards.push_back(cardLayout);
                cursor += ch + style.cardGap;
                contentH += ch + style.cardGap;
            }
            cl.contentHeight = static_cast<float>(contentH);
        }
    } else {
        // Swimlane partition: lane height fits the tallest column stack.
        double laneTop = bodyTop;
        for (const auto& lane : lanes) {
            double required = style.laneMinHeight;
            for (size_t i = 0; i < n; ++i) {
                std::vector<int> ids =
                        ds->GetCardsInColumn(columns[i].id, lane.id);
                double h = style.cardGap;
                for (size_t k = 0; k < ids.size(); ++k) {
                    const KanbanCard* card = ds->FindCard(ids[k]);
                    if (!card) continue;
                    double cx, cw;
                    cardFrame(layout.columns[i], k, cx, cw);
                    h += CardHeight(ctx, *card, cw) + style.cardGap;
                }
                required = std::max(required, h);
            }

            LaneLayout ll;
            ll.laneId = lane.id;
            ll.top = laneTop - columnScroll[-1];
            ll.height = required;
            if (laneHeaderW > 0) {
                ll.headerRect = Rect2Dd(layout.boardRect.x, ll.top,
                                        laneHeaderW - 6, required);
            }
            layout.lanes.push_back(ll);

            for (size_t i = 0; i < n; ++i) {
                std::vector<int> ids =
                        ds->GetCardsInColumn(columns[i].id, lane.id);
                double cursor = ll.top + style.cardGap;
                for (size_t k = 0; k < ids.size(); ++k) {
                    const KanbanCard* card = ds->FindCard(ids[k]);
                    if (!card) continue;
                    double cx, cw;
                    cardFrame(layout.columns[i], k, cx, cw);
                    double ch = CardHeight(ctx, *card, cw);
                    CardLayout cardLayout;
                    cardLayout.cardId = card->id;
                    cardLayout.columnIndex = static_cast<int>(i);
                    cardLayout.rect = Rect2Dd(cx, cursor, cw, ch);
                    layout.cards.push_back(cardLayout);
                    cursor += ch + style.cardGap;
                }
            }
            laneTop += required;
        }
        float contentH = static_cast<float>(laneTop - bodyTop);
        for (auto& cl : layout.columns) cl.contentHeight = contentH;
    }
}

void UltraCanvasKanbanBoardElement::ClampScroll() {
    for (auto& cl : layout.columns) {
        float maxScroll = std::max(0.0f, cl.contentHeight -
                                         static_cast<float>(cl.bodyRect.height));
        float& scroll = columnScroll[cl.columnId];
        scroll = std::clamp(scroll, 0.0f, maxScroll);
    }
    if (!layout.lanes.empty() && !layout.columns.empty()) {
        float maxScroll = std::max(0.0f, layout.columns[0].contentHeight -
                                         static_cast<float>(layout.columns[0].bodyRect.height));
        float& scroll = columnScroll[-1];
        scroll = std::clamp(scroll, 0.0f, maxScroll);
    }
}

const UltraCanvasKanbanBoardElement::CardLayout*
UltraCanvasKanbanBoardElement::CardAt(const Point2Di& pos) const {
    // Later cards draw on top; search back-to-front.
    for (auto it = layout.cards.rbegin(); it != layout.cards.rend(); ++it) {
        if (it->columnIndex >= 0 &&
            it->columnIndex < static_cast<int>(layout.columns.size())) {
            const Rect2Dd& body = layout.columns[it->columnIndex].bodyRect;
            if (pos.y < body.y || pos.y > body.y + body.height) continue;
        }
        if (pos.x >= it->rect.x && pos.x <= it->rect.x + it->rect.width &&
            pos.y >= it->rect.y && pos.y <= it->rect.y + it->rect.height) {
            return &*it;
        }
    }
    return nullptr;
}

int UltraCanvasKanbanBoardElement::ColumnIndexAt(const Point2Di& pos) const {
    for (size_t i = 0; i < layout.columns.size(); ++i) {
        const ColumnLayout& cl = layout.columns[i];
        double left = cl.headerRect.x - style.columnGap / 2.0;
        double right = cl.headerRect.x + cl.headerRect.width +
                       style.columnGap / 2.0;
        if (pos.x >= left && pos.x <= right) return static_cast<int>(i);
    }
    return -1;
}

int UltraCanvasKanbanBoardElement::LaneIdAt(const Point2Di& pos) const {
    for (const auto& lane : layout.lanes) {
        if (pos.y >= lane.top && pos.y <= lane.top + lane.height) {
            return lane.laneId;
        }
    }
    return -1;
}

void UltraCanvasKanbanBoardElement::UpdateDropTarget(const Point2Di& pos) {
    dropColumnIndex = ColumnIndexAt(pos);
    dropLaneId = layout.lanes.empty() ? -1 : LaneIdAt(pos);
    dropInsertIndex = -1;
    if (dropColumnIndex < 0) return;

    auto ds = GetKanbanDataSource();
    if (!ds) return;
    int columnId = layout.columns[dropColumnIndex].columnId;
    std::vector<int> ids = ds->GetCardsInColumn(
            columnId, layout.lanes.empty() ? -2 : dropLaneId);

    int index = 0;
    for (int cardId : ids) {
        if (cardId == pressedCardId) continue;
        for (const auto& cl : layout.cards) {
            if (cl.cardId != cardId) continue;
            if (pos.y > cl.rect.y + cl.rect.height / 2.0) ++index;
            break;
        }
    }
    dropInsertIndex = index;
}

// =============================================================================
// ELEMENT — RENDERING
// =============================================================================

void UltraCanvasKanbanBoardElement::Render(IRenderContext* ctx,
                                           const Rect2Df& dirtyRect) {
    if (!ctx) return;
    ctx->PushState();
    if (!style.fontFamily.empty()) ctx->SetFontFamily(style.fontFamily);

    Rect2Df local = GetLocalBounds();
    ctx->DrawFilledRectangle(Rect2Dd(local.x, local.y, local.width, local.height),
                             style.background);

    ComputeLayout(ctx);
    ClampScroll();

    auto ds = GetKanbanDataSource();
    if (!ds || ds->GetColumns().empty()) {
        ctx->SetTextPaint(Color(120, 124, 132));
        ctx->SetFontSize(13.0f);
        DrawCellText(ctx, "No columns — load a board or enable editing",
                     Rect2Dd(local.x, local.y, local.width, local.height),
                     TextAlignment::Center);
        ctx->PopState();
        return;
    }

    RenderChart(ctx);
    ctx->PopState();
}

void UltraCanvasKanbanBoardElement::RenderChart(IRenderContext* ctx) {
    if (style.boardPanelColor.a != 0) {
        ctx->SetFillPaint(style.boardPanelColor);
        ctx->FillRoundedRectangle(layout.boardRect, style.boardPanelRadius);
    }

    RenderTitleAndLegend(ctx);

    ctx->PushState();
    ctx->ClipRect(layout.boardRect);
    RenderLanes(ctx);
    RenderColumns(ctx);
    RenderCards(ctx);
    RenderDropIndicator(ctx);
    RenderEditingChrome(ctx);
    ctx->PopState();

    if (editor.open) RenderEditorOverlay(ctx);
}

void UltraCanvasKanbanBoardElement::RenderTitleAndLegend(IRenderContext* ctx) {
    if (style.showTitle && layout.titleRect.height > 0) {
        ctx->SetTextPaint(style.titleColor);
        ctx->SetFontSize(style.titleFontSize);
        ctx->SetFontWeight(FontWeight::Bold);
        DrawCellText(ctx, chartTitle.empty() ? "Kanban Board" : chartTitle,
                     layout.titleRect, TextAlignment::Center);
        ctx->SetFontWeight(FontWeight::Normal);
    }

    if (!style.showPriorityLegend) return;
    Rect2Dd band = layout.legendRect.height > 0 ? layout.legendRect
                                                : layout.titleRect;
    if (band.height <= 0) return;

    auto ds = GetKanbanDataSource();
    std::vector<std::pair<std::string, Color>> entries;
    if (todaySet) entries.emplace_back("Overdue", style.overdueColor);
    if (ds) {
        bool used[5] = {false, false, false, false, false};
        for (const auto& col : ds->GetColumns()) {
            for (int cardId : ds->GetCardsInColumn(col.id)) {
                const KanbanCard* card = ds->FindCard(cardId);
                if (card) used[static_cast<int>(card->priority)] = true;
            }
        }
        static const KanbanPriority order[] = {KanbanPriority::Urgent,
                                               KanbanPriority::High,
                                               KanbanPriority::Normal,
                                               KanbanPriority::Low};
        for (KanbanPriority p : order) {
            if (used[static_cast<int>(p)]) {
                entries.emplace_back(
                        PriorityName(p),
                        KanbanPalettes::PriorityColors()[static_cast<int>(p)]);
            }
        }
    }
    if (entries.empty()) return;

    ctx->SetFontSize(11.5f);
    double x = band.x + 4;
    double cy = band.y + band.height / 2.0;
    for (const auto& [name, color] : entries) {
        ctx->DrawFilledCircle(Point2Dd(x + 7, cy), 7.0f, color);
        ctx->SetTextPaint(style.legendTextColor);
        Size2Di sz = ctx->GetTextLineDimensions(name);
        ctx->DrawText(name, Point2Dd(x + 18, cy - sz.height / 2.0));
        x += 18 + sz.width + 16;
    }
}

void UltraCanvasKanbanBoardElement::RenderLanes(IRenderContext* ctx) {
    if (layout.lanes.empty()) return;
    for (size_t i = 0; i < layout.lanes.size(); ++i) {
        const LaneLayout& lane = layout.lanes[i];

        if (i > 0 && style.laneSeparatorColor.a != 0) {
            ctx->SetStrokePaint(style.laneSeparatorColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawLine(Point2Dd(layout.boardRect.x, lane.top),
                          Point2Dd(layout.boardRect.x + layout.boardRect.width,
                                   lane.top));
        }
        if (lane.headerRect.width > 0) {
            auto ds = GetKanbanDataSource();
            const KanbanLane* data = ds ? ds->FindLane(lane.laneId) : nullptr;
            Rect2Dd tab(lane.headerRect.x,
                        lane.headerRect.y + lane.headerRect.height / 2.0 - 16,
                        lane.headerRect.width, 32);
            ctx->SetFillPaint(style.laneHeaderColor);
            ctx->FillRoundedRectangle(tab, 4.0);
            ctx->SetTextPaint(style.laneHeaderTextColor);
            ctx->SetFontSize(style.laneFontSize);
            DrawCellText(ctx, data ? data->title : "", tab, TextAlignment::Center);
        }
    }
}

void UltraCanvasKanbanBoardElement::RenderColumns(IRenderContext* ctx) {
    auto ds = GetKanbanDataSource();
    const auto& columns = ds->GetColumns();

    for (size_t i = 0; i < layout.columns.size(); ++i) {
        const ColumnLayout& cl = layout.columns[i];
        Color accent = ColumnColor(i, columns[i]);

        // Body background.
        Rect2Dd body(cl.bodyRect.x, cl.bodyRect.y, cl.bodyRect.width,
                     cl.bodyRect.height +
                             (cl.addButtonRect.height > 0
                                      ? cl.addButtonRect.height + 4 : 0));
        switch (style.columnBackground) {
            case KanbanColumnBackground::Panel: {
                Color fill = accent;
                fill.a = static_cast<uint8_t>(255 * style.columnPanelAlpha);
                ctx->SetFillPaint(fill);
                ctx->FillRoundedRectangle(body, style.columnCornerRadius);
                break;
            }
            case KanbanColumnBackground::Track:
                ctx->SetFillPaint(style.columnTrackColor);
                ctx->FillRoundedRectangle(body, style.columnCornerRadius);
                break;
            case KanbanColumnBackground::Separators:
                if (i > 0) {
                    ctx->SetStrokePaint(style.separatorColor);
                    ctx->SetStrokeWidth(2.0f);
                    if (style.separatorDashed) {
                        ctx->SetLineDash(UCDashPattern({2.0, 7.0}));
                    }
                    double sx = cl.bodyRect.x - style.columnGap / 2.0;
                    ctx->DrawLine(Point2Dd(sx, layout.boardRect.y + 8),
                                  Point2Dd(sx, layout.boardRect.y +
                                               layout.boardRect.height - 8));
                    ctx->SetLineDash(UCDashPattern::EMPTY);
                }
                break;
            case KanbanColumnBackground::Plain:
            default:
                break;
        }

        RenderColumnHeader(ctx, i, cl);

        // Footer caption strip.
        if (cl.footerRect.height > 0) {
            ctx->SetFillPaint(style.footerColor);
            ctx->DrawFilledRectangle(cl.footerRect, style.footerColor);
            if (!columns[i].footerCaption.empty()) {
                ctx->SetTextPaint(style.footerTextColor);
                ctx->SetFontSize(10.5f);
                DrawCellText(ctx, columns[i].footerCaption, cl.footerRect,
                             TextAlignment::Left, 8.0f);
            }
        }
    }
}

void UltraCanvasKanbanBoardElement::RenderColumnHeader(IRenderContext* ctx,
                                                       size_t columnIndex,
                                                       const ColumnLayout& cl) {
    auto ds = GetKanbanDataSource();
    const KanbanColumn& col = ds->GetColumns()[columnIndex];
    Color accent = ColumnColor(columnIndex, col);
    bool renaming = renamingColumnId == col.id;

    std::string caption = renaming ? renameBuffer + "|" : col.title;
    if (style.uppercaseHeaders) caption = ToUpper(caption);
    if (!renaming && style.showCardCount) {
        caption += " (" + std::to_string(ds->CardCountInColumn(col.id)) + ")";
    }

    Rect2Dd headerRect = cl.headerRect;
    switch (style.headerShape) {
        case KanbanHeaderShape::Bar:
            ctx->SetFillPaint(accent);
            ctx->FillRoundedRectangle(headerRect, 3.0);
            break;
        case KanbanHeaderShape::Pill:
            ctx->SetFillPaint(accent);
            ctx->FillRoundedRectangle(headerRect, headerRect.height / 2.0);
            break;
        case KanbanHeaderShape::PillWithDot: {
            double dotR = headerRect.height * 0.32;
            Rect2Dd pill(headerRect.x, headerRect.y,
                         headerRect.width - dotR * 2 - 12, headerRect.height);
            ctx->SetFillPaint(accent);
            ctx->FillRoundedRectangle(pill, pill.height / 2.0);
            double cy = headerRect.y + headerRect.height / 2.0;
            ctx->SetStrokePaint(accent);
            ctx->SetStrokeWidth(2.5f);
            ctx->DrawLine(Point2Dd(pill.x + pill.width, cy),
                          Point2Dd(headerRect.x + headerRect.width - dotR * 2, cy));
            ctx->DrawFilledCircle(
                    Point2Dd(headerRect.x + headerRect.width - dotR, cy),
                    static_cast<float>(dotR), accent);
            headerRect = pill;
            break;
        }
        case KanbanHeaderShape::TextRow:
            ctx->SetStrokePaint(style.separatorColor.WithAlpha(255));
            ctx->SetStrokeWidth(1.5f);
            ctx->DrawLine(Point2Dd(headerRect.x - style.columnGap / 2.0,
                                   headerRect.y + headerRect.height),
                          Point2Dd(headerRect.x + headerRect.width +
                                   style.columnGap / 2.0,
                                   headerRect.y + headerRect.height));
            break;
    }

    Color textColor = style.headerShape == KanbanHeaderShape::TextRow
                              ? style.headerTextColor
                              : (IsLightColor(accent) ? Color(40, 42, 48)
                                                      : style.headerTextColor);
    ctx->SetTextPaint(textColor);
    ctx->SetFontSize(style.headerFontSize);
    ctx->SetFontWeight(FontWeight::Bold);

    // WIP badge on the right edge of the header.
    double reserved = 0;
    if (style.showWipBadge && col.wipLimit > 0) {
        int count = ds->CardCountInColumn(col.id);
        std::string badge = std::to_string(count) + "/" +
                            std::to_string(col.wipLimit);
        ctx->SetFontSize(style.headerFontSize * 0.85f);
        Size2Di sz = ctx->GetTextLineDimensions(badge);
        double bw = sz.width + 12;
        double bh = headerRect.height * 0.62;
        Rect2Dd badgeRect(headerRect.x + headerRect.width - bw - 5,
                          headerRect.y + (headerRect.height - bh) / 2.0, bw, bh);
        bool exceeded = count > col.wipLimit;
        ctx->SetFillPaint(exceeded ? style.wipExceededColor : style.wipBadgeColor);
        ctx->FillRoundedRectangle(badgeRect, bh / 2.0);
        ctx->SetTextPaint(exceeded ? Colors::White : textColor);
        DrawCellText(ctx, badge, badgeRect, TextAlignment::Center, 2.0f);
        ctx->SetTextPaint(textColor);
        ctx->SetFontSize(style.headerFontSize);
        reserved = bw + 8;
    }

    Rect2Dd captionRect(headerRect.x, headerRect.y,
                        headerRect.width - reserved, headerRect.height);
    DrawCellText(ctx, caption, captionRect,
                 style.headerShape == KanbanHeaderShape::TextRow
                         ? TextAlignment::Center
                         : TextAlignment::Center, 8.0f);
    ctx->SetFontWeight(FontWeight::Normal);
}

void UltraCanvasKanbanBoardElement::RenderCardShape(IRenderContext* ctx,
                                                    const Rect2Dd& rect,
                                                    const Color& face) {
    if (style.cardShape == KanbanCardShape::DogEar) {
        double fold = std::min(16.0, rect.height * 0.28);
        double r = style.cardCornerRadius;
        ctx->SetFillPaint(face);
        ctx->ClearPath();
        ctx->MoveTo(rect.x + r, rect.y);
        ctx->LineTo(rect.x + rect.width - r, rect.y);
        ctx->QuadraticCurveTo(rect.x + rect.width, rect.y,
                              rect.x + rect.width, rect.y + r);
        ctx->LineTo(rect.x + rect.width, rect.y + rect.height - r);
        ctx->QuadraticCurveTo(rect.x + rect.width, rect.y + rect.height,
                              rect.x + rect.width - r, rect.y + rect.height);
        ctx->LineTo(rect.x + fold, rect.y + rect.height);
        ctx->LineTo(rect.x, rect.y + rect.height - fold);
        ctx->LineTo(rect.x, rect.y + r);
        ctx->QuadraticCurveTo(rect.x, rect.y, rect.x + r, rect.y);
        ctx->ClosePath();
        ctx->Fill();
        // Folded flap.
        ctx->SetFillPaint(Color(150, 152, 158));
        ctx->ClearPath();
        ctx->MoveTo(rect.x, rect.y + rect.height - fold);
        ctx->LineTo(rect.x + fold, rect.y + rect.height);
        ctx->LineTo(rect.x + fold, rect.y + rect.height - fold);
        ctx->ClosePath();
        ctx->Fill();
        return;
    }

    double radius = style.cardCornerRadius;
    if (style.cardShape == KanbanCardShape::Pill) radius = rect.height / 2.0;
    else if (style.cardShape == KanbanCardShape::StickyNote) radius = 2.0;

    ctx->SetFillPaint(face);
    ctx->FillRoundedRectangle(rect, radius);
    if (style.cardBorderWidth > 0 && style.cardBorderColor.a != 0) {
        ctx->SetStrokePaint(style.cardBorderColor);
        ctx->SetStrokeWidth(style.cardBorderWidth);
        ctx->DrawRoundedRectangle(rect, radius);
    }
}

void UltraCanvasKanbanBoardElement::RenderCard(IRenderContext* ctx,
                                               const KanbanCard& card,
                                               const Rect2Dd& rect,
                                               size_t cardOrdinal,
                                               size_t columnIndex, bool ghost) {
    if (ghost) ctx->SetAlpha(style.dragGhostAlpha);

    if (style.cardShadow) {
        Rect2Dd shadow(rect.x + style.cardShadowOffset,
                       rect.y + style.cardShadowOffset, rect.width, rect.height);
        double radius = style.cardShape == KanbanCardShape::Pill
                                ? rect.height / 2.0 : style.cardCornerRadius;
        ctx->SetFillPaint(style.cardShadowColor);
        ctx->FillRoundedRectangle(shadow, radius);
    }

    Color face = CardFaceColor(card, cardOrdinal, columnIndex);
    RenderCardShape(ctx, rect, face);

    if (card.blocked) {
        ctx->SetFillPaint(style.overdueColor);
        ctx->DrawFilledRectangle(Rect2Dd(rect.x, rect.y + 3, 3.5,
                                         rect.height - 6),
                                 style.overdueColor);
    }
    if (card.id == hoveredCardId && !ghost && style.hoverTint.a != 0) {
        ctx->SetFillPaint(style.hoverTint);
        ctx->FillRoundedRectangle(rect, style.cardCornerRadius);
    }
    if (card.id == selectedCardId && !ghost) {
        ctx->SetStrokePaint(style.selectionColor);
        ctx->SetStrokeWidth(2.0f);
        double radius = style.cardShape == KanbanCardShape::Pill
                                ? rect.height / 2.0 : style.cardCornerRadius;
        ctx->DrawRoundedRectangle(Rect2Dd(rect.x - 1.5, rect.y - 1.5,
                                          rect.width + 3, rect.height + 3),
                                  radius);
    }

    double pad = style.cardPadding;
    double innerWidth = rect.width - 2 * pad;
    bool showDot = style.priorityMarker == KanbanPriorityMarker::Dot &&
                   card.priority != KanbanPriority::NoPriority;
    bool showChip = style.priorityMarker == KanbanPriorityMarker::Chip &&
                    card.priority != KanbanPriority::NoPriority;
    double titleReserve = 0;
    if (showDot) {
        titleReserve = style.priorityDotRadius * 2 + 6;
    } else if (showChip) {
        ctx->SetFontSize(style.metaFontSize * 0.95f);
        titleReserve = ctx->GetTextLineDimensions(
                               PriorityName(card.priority)).width + 16;
    }

    // Pill cards: a single centered-left line and nothing else.
    if (style.cardShape == KanbanCardShape::Pill) {
        ctx->SetTextPaint(CardTitleColor(card, columnIndex));
        ctx->SetFontSize(style.cardTitleFontSize);
        DrawCellText(ctx, card.title, Rect2Dd(rect.x, rect.y, rect.width,
                                              rect.height),
                     TextAlignment::Center, 6.0f);
        if (ghost) ctx->SetAlpha(1.0);
        return;
    }

    ctx->PushState();
    ctx->ClipRect(rect);

    double cursor = rect.y + pad;

    // Title.
    ctx->SetTextPaint(CardTitleColor(card, columnIndex));
    ctx->SetFontSize(style.cardTitleFontSize);
    ctx->SetFontWeight(FontWeight::Bold);
    double titleLineH = style.cardTitleFontSize * 1.35;
    if (style.cardLayout == KanbanCardLayout::Grid && card.title.empty()) {
        // Color-block card without caption (Schematic reference image).
    } else {
        std::vector<std::string> titleLines =
                WrapTextLines(ctx, card.title, innerWidth - titleReserve);
        size_t maxTitle = std::min<size_t>(titleLines.size(), 3);
        if (style.cardLayout == KanbanCardLayout::Grid) {
            // Vertically center a single line in the fixed-height card.
            Rect2Dd box(rect.x + pad, rect.y, innerWidth - titleReserve,
                        rect.height);
            DrawCellText(ctx, titleLines.empty() ? "" : titleLines[0], box,
                         TextAlignment::Left, 0.0f);
            cursor = rect.y + rect.height;
        } else {
            for (size_t i = 0; i < maxTitle; ++i) {
                ctx->DrawText(titleLines[i], Point2Dd(rect.x + pad, cursor));
                cursor += titleLineH;
            }
        }
    }
    ctx->SetFontWeight(FontWeight::Normal);

    // Description.
    if (style.showCardDescription && !card.description.empty()) {
        cursor += 3;
        Color descColor = IsLightColor(CardFaceColor(card, cardOrdinal, columnIndex))
                                  ? style.cardTextColor
                                  : Colors::White.WithAlpha(215);
        ctx->SetTextPaint(descColor);
        ctx->SetFontSize(style.cardTextFontSize);
        std::vector<std::string> lines =
                WrapTextLines(ctx, card.description, innerWidth);
        size_t maxLines = static_cast<size_t>(std::max(1, style.descriptionMaxLines));
        double lineH = style.cardTextFontSize * 1.35;
        for (size_t i = 0; i < lines.size() && i < maxLines; ++i) {
            std::string text = lines[i];
            if (i + 1 == maxLines && lines.size() > maxLines) text += "…";
            ctx->DrawText(text, Point2Dd(rect.x + pad, cursor));
            cursor += lineH;
        }
    }

    // Due date.
    if (style.showDueDate && card.hasDueDate) {
        cursor += 5;
        double metaLineH = style.metaFontSize * 1.4;
        bool overdue = todaySet && card.dueDate < today;
        Color labelColor = style.dueLabelColor.a != 0
                                   ? style.dueLabelColor
                                   : CardTitleColor(card, columnIndex);
        ctx->SetFontSize(style.metaFontSize);
        if (!style.dueDateLabel.empty()) {
            ctx->SetTextPaint(overdue ? style.overdueColor : labelColor);
            ctx->DrawText(style.dueDateLabel, Point2Dd(rect.x + pad, cursor));
            cursor += metaLineH;
            ctx->SetTextPaint(overdue ? style.overdueColor : style.cardTextColor);
            ctx->DrawText(FormatDate(card.dueDate), Point2Dd(rect.x + pad, cursor));
        } else {
            ctx->SetTextPaint(overdue ? style.overdueColor : style.cardTextColor);
            ctx->DrawText("Due " + FormatDate(card.dueDate),
                          Point2Dd(rect.x + pad, cursor));
        }
        cursor += metaLineH;
    }

    // Bottom row: tags on the left, assignee on the right.
    bool avatarRow = style.showAssignee && !card.assignee.empty();
    bool tagRow = style.showTags && !card.tags.empty();
    if (avatarRow || tagRow) {
        double rowH = std::max(18.0, style.metaFontSize * 1.5);
        double rowY = rect.y + rect.height - pad - rowH + 2;
        if (tagRow) {
            Color accent = CardTitleColor(card, columnIndex);
            ctx->SetFontSize(style.metaFontSize);
            double tx = rect.x + pad;
            for (const auto& tag : card.tags) {
                Size2Di sz = ctx->GetTextLineDimensions(tag);
                double tw = sz.width + 12;
                if (tx + tw > rect.x + rect.width - pad -
                              (avatarRow ? 24.0 : 0.0)) break;
                Rect2Dd chip(tx, rowY, tw, rowH - 2);
                ctx->SetFillPaint(accent.WithAlpha(30));
                ctx->FillRoundedRectangle(chip, (rowH - 2) / 2.0);
                ctx->SetTextPaint(accent);
                DrawCellText(ctx, tag, chip, TextAlignment::Center, 2.0f);
                tx += tw + 5;
            }
        }
        if (avatarRow) {
            double r = rowH / 2.0 - 1;
            Point2Dd center(rect.x + rect.width - pad - r,
                            rowY + rowH / 2.0 - 1);
            if (style.assigneeAsAvatar) {
                Color accent = CardTitleColor(card, columnIndex);
                ctx->DrawFilledCircle(center, static_cast<float>(r),
                                      IsLightColor(accent) ? accent.Darken(0.25f)
                                                           : accent);
                ctx->SetTextPaint(Colors::White);
                ctx->SetFontSize(style.metaFontSize * 0.9f);
                std::string ini = Initials(card.assignee);
                Size2Di sz = ctx->GetTextLineDimensions(ini);
                ctx->DrawText(ini, Point2Dd(center.x - sz.width / 2.0,
                                            center.y - sz.height / 2.0));
            } else {
                ctx->SetTextPaint(style.cardTextColor);
                ctx->SetFontSize(style.metaFontSize);
                Size2Di sz = ctx->GetTextLineDimensions(card.assignee);
                ctx->DrawText(card.assignee,
                              Point2Dd(rect.x + rect.width - pad - sz.width,
                                       rowY + (rowH - sz.height) / 2.0));
            }
        }
    }

    // Priority marker.
    if (showDot) {
        Color dotColor = KanbanPalettes::PriorityColors()
                [static_cast<int>(card.priority)];
        ctx->DrawFilledCircle(
                Point2Dd(rect.x + rect.width - pad - style.priorityDotRadius,
                         rect.y + pad + style.priorityDotRadius),
                style.priorityDotRadius, dotColor);
    } else if (style.priorityMarker == KanbanPriorityMarker::Chip &&
               card.priority != KanbanPriority::NoPriority) {
        Color chipColor = KanbanPalettes::PriorityColors()
                [static_cast<int>(card.priority)];
        std::string name = PriorityName(card.priority);
        ctx->SetFontSize(style.metaFontSize * 0.95f);
        Size2Di sz = ctx->GetTextLineDimensions(name);
        Rect2Dd chip(rect.x + rect.width - pad - sz.width - 10,
                     rect.y + pad - 2, sz.width + 10, 15);
        ctx->SetFillPaint(chipColor.WithAlpha(36));
        ctx->FillRoundedRectangle(chip, 7.5);
        ctx->SetTextPaint(chipColor.Darken(0.15f));
        DrawCellText(ctx, name, chip, TextAlignment::Center, 2.0f);
    }

    ctx->PopState();
    if (ghost) ctx->SetAlpha(1.0);
}

void UltraCanvasKanbanBoardElement::RenderCards(IRenderContext* ctx) {
    auto ds = GetKanbanDataSource();

    for (const auto& cl : layout.cards) {
        if (draggingCard && cl.cardId == pressedCardId) continue;
        const KanbanCard* card = ds->FindCard(cl.cardId);
        if (!card) continue;
        if (cl.columnIndex < 0 ||
            cl.columnIndex >= static_cast<int>(layout.columns.size())) continue;
        const ColumnLayout& col = layout.columns[cl.columnIndex];
        Rect2Dd body = col.bodyRect;
        if (cl.rect.y + cl.rect.height < body.y - 40 ||
            cl.rect.y > body.y + body.height + 40) continue;

        // Ordinal within its column for the ByCard palette cycle, offset per
        // column so neighbouring columns don't repeat the same color pattern.
        size_t ordinal = static_cast<size_t>(cl.columnIndex);
        for (const auto& other : layout.cards) {
            if (other.columnIndex != cl.columnIndex) continue;
            if (other.cardId == cl.cardId) break;
            ++ordinal;
        }

        ctx->PushState();
        ctx->ClipRect(Rect2Dd(body.x - style.staggerOffset - 4, body.y,
                              body.width + 2 * style.staggerOffset + 8,
                              body.height));
        RenderCard(ctx, *card, cl.rect, ordinal,
                   static_cast<size_t>(cl.columnIndex), false);
        ctx->PopState();
    }

    // Drag ghost on top, unclipped.
    if (draggingCard) {
        const KanbanCard* card = ds->FindCard(pressedCardId);
        if (card) {
            for (const auto& cl : layout.cards) {
                if (cl.cardId != pressedCardId) continue;
                Rect2Dd ghost(dragPos.x - pressOffset.x, dragPos.y - pressOffset.y,
                              cl.rect.width, cl.rect.height);
                RenderCard(ctx, *card, ghost, 0,
                           static_cast<size_t>(std::max(0, cl.columnIndex)), true);
                break;
            }
        }
    }
}

void UltraCanvasKanbanBoardElement::RenderDropIndicator(IRenderContext* ctx) {
    if (!draggingCard || dropColumnIndex < 0 ||
        dropColumnIndex >= static_cast<int>(layout.columns.size())) return;

    auto ds = GetKanbanDataSource();
    const ColumnLayout& cl = layout.columns[dropColumnIndex];
    int columnId = cl.columnId;

    // Highlight the target column.
    ctx->SetStrokePaint(style.dropIndicatorColor.WithAlpha(120));
    ctx->SetStrokeWidth(1.5f);
    ctx->DrawRoundedRectangle(cl.bodyRect, style.columnCornerRadius);

    // Insertion line.
    std::vector<int> ids = ds->GetCardsInColumn(
            columnId, layout.lanes.empty() ? -2 : dropLaneId);
    double y = cl.bodyRect.y + style.cardGap / 2.0;
    if (!layout.lanes.empty()) {
        for (const auto& lane : layout.lanes) {
            if (lane.laneId == dropLaneId) {
                y = lane.top + style.cardGap / 2.0;
                break;
            }
        }
    }
    int seen = 0;
    for (int cardId : ids) {
        if (cardId == pressedCardId) continue;
        if (seen >= dropInsertIndex) break;
        for (const auto& c : layout.cards) {
            if (c.cardId == cardId) {
                y = c.rect.y + c.rect.height + style.cardGap / 2.0;
                break;
            }
        }
        ++seen;
    }
    ctx->SetStrokePaint(style.dropIndicatorColor);
    ctx->SetStrokeWidth(2.5f);
    ctx->DrawLine(Point2Dd(cl.bodyRect.x + 6, y),
                  Point2Dd(cl.bodyRect.x + cl.bodyRect.width - 6, y));
}

void UltraCanvasKanbanBoardElement::RenderEditingChrome(IRenderContext* ctx) {
    if (!editable) return;

    ctx->SetFontSize(11.5f);
    for (const auto& cl : layout.columns) {
        if (cl.addButtonRect.height <= 0) continue;
        ctx->SetStrokePaint(style.addButtonColor.WithAlpha(140));
        ctx->SetStrokeWidth(1.0f);
        ctx->SetLineDash(UCDashPattern({3.0, 3.0}));
        ctx->DrawRoundedRectangle(cl.addButtonRect, 5.0);
        ctx->SetLineDash(UCDashPattern::EMPTY);
        ctx->SetTextPaint(style.addButtonColor);
        DrawCellText(ctx, "+ Add card", cl.addButtonRect, TextAlignment::Center);
    }
    if (layout.addColumnRect.width > 0) {
        ctx->SetFillPaint(style.addButtonColor.WithAlpha(30));
        ctx->FillRoundedRectangle(layout.addColumnRect, 6.0);
        ctx->SetTextPaint(style.addButtonColor);
        ctx->SetFontSize(16.0f);
        DrawCellText(ctx, "+", layout.addColumnRect, TextAlignment::Center);
    }
}

// =============================================================================
// ELEMENT — EDITOR OVERLAY
// =============================================================================

void UltraCanvasKanbanBoardElement::OpenCardEditor(int cardId) {
    auto ds = GetKanbanDataSource();
    const KanbanCard* card = ds ? ds->FindCard(cardId) : nullptr;
    if (!card) return;

    editor = EditorState();
    editor.open = true;
    editor.cardId = cardId;
    editor.priority = card->priority;
    auto addField = [&](const std::string& label, const std::string& text,
                        bool multiline) {
        EditorField f;
        f.label = label;
        f.text = text;
        f.multiline = multiline;
        editor.fields.push_back(f);
    };
    addField("Title", card->title, false);
    addField("Description", card->description, true);
    addField("Assignee", card->assignee, false);
    addField("Due date (YYYY-MM-DD)",
             card->hasDueDate ? FormatISODate(card->dueDate) : "", false);
    std::string tags;
    for (size_t i = 0; i < card->tags.size(); ++i) {
        if (i) tags += ", ";
        tags += card->tags[i];
    }
    addField("Tags (comma separated)", tags, false);

    SetFocus(true);
    RequestRedraw();
}

void UltraCanvasKanbanBoardElement::CloseCardEditor(bool save) {
    if (!editor.open) return;
    auto ds = GetKanbanDataSource();
    if (ds && ds->FindCard(editor.cardId)) {
        if (save) {
            std::string title = TrimText(editor.fields[0].text);
            ds->SetCardTitle(editor.cardId,
                             title.empty() ? "Untitled" : title);
            ds->SetCardDescription(editor.cardId, TrimText(editor.fields[1].text));
            ds->SetCardAssignee(editor.cardId, TrimText(editor.fields[2].text));
            GanttDate due;
            if (ParseISODate(TrimText(editor.fields[3].text), due)) {
                ds->SetCardDueDate(editor.cardId, due);
            } else {
                ds->ClearCardDueDate(editor.cardId);
            }
            std::vector<std::string> tags;
            std::stringstream ss(editor.fields[4].text);
            std::string tag;
            while (std::getline(ss, tag, ',')) {
                tag = TrimText(tag);
                if (!tag.empty()) tags.push_back(tag);
            }
            ds->SetCardTags(editor.cardId, tags);
            ds->SetCardPriority(editor.cardId, editor.priority);
            if (editor.newCard && onCardAdded) onCardAdded(editor.cardId);
            if (onBoardChanged) onBoardChanged();
        } else if (editor.newCard) {
            ds->RemoveCard(editor.cardId);
        }
    }
    editor = EditorState();
    RequestRedraw();
}

void UltraCanvasKanbanBoardElement::RenderEditorOverlay(IRenderContext* ctx) {
    Rect2Df local = GetLocalBounds();
    ctx->SetFillPaint(Color(15, 17, 22, 110));
    ctx->DrawFilledRectangle(Rect2Dd(local.x, local.y, local.width, local.height),
                             Color(15, 17, 22, 110));

    double panelW = std::min(440.0, local.width - 30.0);
    double labelH = 16.0, fieldH = 30.0, multiH = 76.0, gap = 8.0;
    double panelH = 16.0;
    for (const auto& f : editor.fields) {
        panelH += labelH + (f.multiline ? multiH : fieldH) + gap;
    }
    panelH += labelH + 26.0 + gap;   // Priority row
    panelH += 40.0 + 12.0;           // Buttons
    panelH = std::min(panelH, static_cast<double>(local.height) - 20.0);

    double px = local.x + (local.width - panelW) / 2.0;
    double py = local.y + (local.height - panelH) / 2.0;
    editor.panelRect = Rect2Dd(px, py, panelW, panelH);

    ctx->SetFillPaint(Color(0, 0, 0, 60));
    ctx->FillRoundedRectangle(Rect2Dd(px + 3, py + 4, panelW, panelH), 10.0);
    ctx->SetFillPaint(style.editorPanelColor);
    ctx->FillRoundedRectangle(editor.panelRect, 10.0);

    double cx = px + 16, cw = panelW - 32;
    double cy = py + 12;

    for (size_t i = 0; i < editor.fields.size(); ++i) {
        EditorField& f = editor.fields[i];
        ctx->SetTextPaint(style.editorTextColor.WithAlpha(170));
        ctx->SetFontSize(10.5f);
        ctx->DrawText(f.label, Point2Dd(cx, cy));
        cy += labelH;

        double fh = f.multiline ? multiH : fieldH;
        f.rect = Rect2Dd(cx, cy, cw, fh);
        ctx->SetFillPaint(style.editorFieldColor);
        ctx->FillRoundedRectangle(f.rect, 5.0);
        bool focused = static_cast<int>(i) == editor.focusedField;
        if (focused) {
            ctx->SetStrokePaint(style.editorAccentColor);
            ctx->SetStrokeWidth(1.5f);
            ctx->DrawRoundedRectangle(f.rect, 5.0);
        }

        ctx->SetTextPaint(style.editorTextColor);
        ctx->SetFontSize(12.0f);
        std::string shown = f.text + (focused ? "|" : "");
        if (f.multiline) {
            ctx->PushState();
            ctx->ClipRect(f.rect);
            std::vector<std::string> lines = WrapTextLines(ctx, shown, cw - 16);
            if (lines.empty()) lines.push_back(focused ? "|" : "");
            double ty = cy + 7;
            size_t firstLine = lines.size() > 4 ? lines.size() - 4 : 0;
            for (size_t li = firstLine; li < lines.size(); ++li) {
                ctx->DrawText(lines[li], Point2Dd(cx + 8, ty));
                ty += 15.5;
            }
            ctx->PopState();
        } else {
            // Keep the caret visible for long values.
            std::string visible = shown;
            while (visible.size() > 1 &&
                   ctx->GetTextLineDimensions(visible).width > cw - 16) {
                visible.erase(0, 1);
            }
            DrawCellText(ctx, visible, f.rect, TextAlignment::Left, 8.0f);
        }
        cy += fh + gap;
    }

    // Priority chips.
    ctx->SetTextPaint(style.editorTextColor.WithAlpha(170));
    ctx->SetFontSize(10.5f);
    ctx->DrawText("Priority", Point2Dd(cx, cy));
    cy += labelH;
    editor.priorityRect = Rect2Dd(cx, cy, cw, 24);
    double chipX = cx;
    ctx->SetFontSize(11.0f);
    for (int p = 0; p <= static_cast<int>(KanbanPriority::Urgent); ++p) {
        std::string name = PriorityName(static_cast<KanbanPriority>(p));
        Size2Di sz = ctx->GetTextLineDimensions(name);
        double chipW = sz.width + 16;
        Rect2Dd chip(chipX, cy, chipW, 22);
        Color chipColor = KanbanPalettes::PriorityColors()[p];
        bool active = editor.priority == static_cast<KanbanPriority>(p);
        ctx->SetFillPaint(active ? chipColor : chipColor.WithAlpha(30));
        ctx->FillRoundedRectangle(chip, 11.0);
        ctx->SetTextPaint(active ? Colors::White : style.editorTextColor);
        DrawCellText(ctx, name, chip, TextAlignment::Center, 2.0f);
        chipX += chipW + 6;
    }
    cy += 26 + gap;

    // Buttons.
    double buttonH = 30;
    editor.deleteRect = Rect2Dd(cx, cy, 70, buttonH);
    editor.saveRect = Rect2Dd(cx + cw - 74, cy, 74, buttonH);
    editor.cancelRect = Rect2Dd(cx + cw - 74 - 78, cy, 72, buttonH);

    if (!editor.newCard) {
        ctx->SetFillPaint(style.overdueColor.WithAlpha(26));
        ctx->FillRoundedRectangle(editor.deleteRect, 6.0);
        ctx->SetTextPaint(style.overdueColor);
        ctx->SetFontSize(12.0f);
        DrawCellText(ctx, "Delete", editor.deleteRect, TextAlignment::Center);
    }
    ctx->SetFillPaint(style.editorFieldColor);
    ctx->FillRoundedRectangle(editor.cancelRect, 6.0);
    ctx->SetTextPaint(style.editorTextColor);
    ctx->SetFontSize(12.0f);
    DrawCellText(ctx, "Cancel", editor.cancelRect, TextAlignment::Center);

    ctx->SetFillPaint(style.editorAccentColor);
    ctx->FillRoundedRectangle(editor.saveRect, 6.0);
    ctx->SetTextPaint(Colors::White);
    DrawCellText(ctx, "Save", editor.saveRect, TextAlignment::Center);
}

// =============================================================================
// ELEMENT — INTERACTION
// =============================================================================

static bool RectContains(const Rect2Dd& r, const Point2Di& p) {
    return p.x >= r.x && p.x <= r.x + r.width &&
           p.y >= r.y && p.y <= r.y + r.height;
}

std::string UltraCanvasKanbanBoardElement::BuildCardTooltip(
        const KanbanCard& card) const {
    auto ds = GetKanbanDataSource();
    std::ostringstream out;
    out << (card.title.empty() ? "(untitled)" : card.title);
    if (ds) {
        const KanbanColumn* col = ds->FindColumn(card.columnId);
        if (col) out << "\nColumn: " << col->title;
        const KanbanLane* lane = ds->FindLane(card.laneId);
        if (lane) out << "\nLane: " << lane->title;
    }
    if (!card.description.empty()) out << "\n" << card.description;
    if (card.priority != KanbanPriority::NoPriority) {
        out << "\nPriority: " << PriorityName(card.priority);
    }
    if (card.hasDueDate) {
        out << "\nDue: " << FormatDate(card.dueDate);
        if (todaySet && card.dueDate < today) out << " (overdue)";
    }
    if (!card.assignee.empty()) out << "\nAssignee: " << card.assignee;
    if (!card.tags.empty()) {
        out << "\nTags: ";
        for (size_t i = 0; i < card.tags.size(); ++i) {
            if (i) out << ", ";
            out << card.tags[i];
        }
    }
    if (card.blocked) {
        out << "\nBLOCKED";
        if (!card.blockedReason.empty()) out << ": " << card.blockedReason;
    }
    return out.str();
}

bool UltraCanvasKanbanBoardElement::HandleChartMouseMove(const Point2Di& mousePos) {
    if (editor.open) return true;

    const CardLayout* over = CardAt(mousePos);
    int overId = over ? over->cardId : -1;
    if (overId != hoveredCardId) {
        hoveredCardId = overId;
        RequestRedraw();
    }

    if (over && enableTooltips && !draggingCard) {
        auto ds = GetKanbanDataSource();
        const KanbanCard* card = ds ? ds->FindCard(over->cardId) : nullptr;
        if (card) {
            std::string content = BuildCardTooltip(*card);
            auto windowMousePos = MapFromLocal(mousePos, nullptr);
            UltraCanvasTooltipManager::UpdateAndShowTooltip(this->window, content,
                                                            windowMousePos);
            isTooltipActive = true;
            return true;
        }
    }
    if (isTooltipActive) HideTooltip();
    return over != nullptr;
}

bool UltraCanvasKanbanBoardElement::HandleWheel(const UCEvent& event) {
    if (editor.open) return true;

    float step = 60.0f;
    if (event.shift) {
        scrollX += (event.wheelDelta > 0 ? -step : step);
        RequestRedraw();
        return true;
    }
    if (!layout.lanes.empty()) {
        columnScroll[-1] += (event.wheelDelta > 0 ? -step : step);
        ClampScroll();
        RequestRedraw();
        return true;
    }
    int columnIndex = ColumnIndexAt(event.pointer);
    if (columnIndex < 0) {
        scrollX += (event.wheelDelta > 0 ? -step : step);
        RequestRedraw();
        return true;
    }
    int columnId = layout.columns[columnIndex].columnId;
    columnScroll[columnId] += (event.wheelDelta > 0 ? -step : step);
    ClampScroll();
    RequestRedraw();
    return true;
}

bool UltraCanvasKanbanBoardElement::EditorMouseDown(const UCEvent& event) {
    if (!RectContains(editor.panelRect, event.pointer)) {
        CloseCardEditor(false);
        return true;
    }
    for (size_t i = 0; i < editor.fields.size(); ++i) {
        if (RectContains(editor.fields[i].rect, event.pointer)) {
            editor.focusedField = static_cast<int>(i);
            RequestRedraw();
            return true;
        }
    }
    if (RectContains(editor.priorityRect, event.pointer)) {
        // Chip hit-test mirrors the render loop's widths; approximate by
        // cycling when the exact chip is ambiguous.
        editor.priority = static_cast<KanbanPriority>(
                (static_cast<int>(editor.priority) + 1) %
                (static_cast<int>(KanbanPriority::Urgent) + 1));
        RequestRedraw();
        return true;
    }
    if (!editor.newCard && RectContains(editor.deleteRect, event.pointer)) {
        int cardId = editor.cardId;
        auto ds = GetKanbanDataSource();
        editor.open = false;
        if (ds && ds->RemoveCard(cardId)) {
            if (selectedCardId == cardId) SelectCard(-1);
            if (onCardRemoved) onCardRemoved(cardId);
            if (onBoardChanged) onBoardChanged();
        }
        editor = EditorState();
        RequestRedraw();
        return true;
    }
    if (RectContains(editor.saveRect, event.pointer)) {
        CloseCardEditor(true);
        return true;
    }
    if (RectContains(editor.cancelRect, event.pointer)) {
        CloseCardEditor(false);
        return true;
    }
    return true;
}

bool UltraCanvasKanbanBoardElement::HandleMouseDown(const UCEvent& event) {
    SetFocus(true);
    if (editor.open) return EditorMouseDown(event);
    if (renamingColumnId >= 0) {
        CommitRename(true);
        return true;
    }

    if (editable) {
        for (const auto& cl : layout.columns) {
            if (cl.addButtonRect.height > 0 &&
                RectContains(cl.addButtonRect, event.pointer)) {
                StartAddCard(cl.columnId);
                return true;
            }
        }
        if (layout.addColumnRect.width > 0 &&
            RectContains(layout.addColumnRect, event.pointer)) {
            StartAddColumn();
            return true;
        }
    }

    const CardLayout* over = CardAt(event.pointer);
    if (over) {
        pressedCardId = over->cardId;
        pressAnchor = event.pointer;
        pressOffset = Point2Dd(event.pointer.x - over->rect.x,
                               event.pointer.y - over->rect.y);
        SelectCard(over->cardId);
        if (onCardClick) onCardClick(over->cardId);
        return true;
    }
    SelectCard(-1);
    return false;
}

bool UltraCanvasKanbanBoardElement::HandleMouseUp(const UCEvent& event) {
    if (editor.open) return true;

    if (draggingCard) {
        auto ds = GetKanbanDataSource();
        if (ds && dropColumnIndex >= 0 &&
            dropColumnIndex < static_cast<int>(layout.columns.size())) {
            int toColumnId = layout.columns[dropColumnIndex].columnId;
            const KanbanCard* card = ds->FindCard(pressedCardId);
            int fromColumnId = card ? card->columnId : -1;
            bool allowed = !canDropCard || canDropCard(pressedCardId, toColumnId);
            if (card && allowed) {
                if (!layout.lanes.empty() && dropLaneId >= 0 &&
                    card->laneId != dropLaneId) {
                    ds->SetCardLane(pressedCardId, dropLaneId);
                }
                // The insertion index counted only same-lane cards; map it to
                // the full column order.
                int insertAt = dropInsertIndex;
                if (!layout.lanes.empty()) {
                    std::vector<int> laneIds =
                            ds->GetCardsInColumn(toColumnId, dropLaneId);
                    std::vector<int> allIds = ds->GetCardsInColumn(toColumnId);
                    int seen = 0;
                    insertAt = static_cast<int>(allIds.size());
                    for (size_t i = 0; i < allIds.size(); ++i) {
                        const KanbanCard* c = ds->FindCard(allIds[i]);
                        if (!c || allIds[i] == pressedCardId) continue;
                        if (c->laneId == dropLaneId) {
                            if (seen == dropInsertIndex) {
                                insertAt = static_cast<int>(i);
                                break;
                            }
                            ++seen;
                        }
                    }
                }
                if (ds->MoveCard(pressedCardId, toColumnId, insertAt)) {
                    if (fromColumnId != toColumnId) {
                        if (onCardMoved) {
                            onCardMoved(pressedCardId, fromColumnId, toColumnId);
                        }
                        NotifyWipIfExceeded(toColumnId);
                    }
                    if (onBoardChanged) onBoardChanged();
                }
            }
        }
        draggingCard = false;
        pressedCardId = -1;
        dropColumnIndex = -1;
        RequestRedraw();
        return true;
    }

    pressedCardId = -1;
    return true;
}

bool UltraCanvasKanbanBoardElement::HandleDoubleClick(const UCEvent& event) {
    if (editor.open) return true;

    // Column header: inline rename (editable boards).
    if (editable) {
        auto ds = GetKanbanDataSource();
        for (const auto& cl : layout.columns) {
            if (RectContains(cl.headerRect, event.pointer)) {
                const KanbanColumn* col = ds ? ds->FindColumn(cl.columnId) : nullptr;
                if (col) {
                    CommitRename(true);
                    renamingColumnId = col->id;
                    renameBuffer = col->title;
                    SetFocus(true);
                    RequestRedraw();
                }
                return true;
            }
        }
    }

    const CardLayout* over = CardAt(event.pointer);
    if (over) {
        SelectCard(over->cardId);
        if (editable) {
            OpenCardEditor(over->cardId);
        }
        if (onCardDoubleClick) onCardDoubleClick(over->cardId);
        return true;
    }
    return false;
}

bool UltraCanvasKanbanBoardElement::HandleKeyDown(const UCEvent& event) {
    // Inline column rename.
    if (renamingColumnId >= 0) {
        if (event.virtualKey == UCKeys::Return) CommitRename(true);
        else if (event.virtualKey == UCKeys::Escape) CommitRename(false);
        else if (event.virtualKey == UCKeys::Backspace) {
            PopUtf8(renameBuffer);
            RequestRedraw();
        }
        return true;
    }

    if (editor.open) {
        EditorField& field = editor.fields[editor.focusedField];
        switch (event.virtualKey) {
            case UCKeys::Escape:
                CloseCardEditor(false);
                return true;
            case UCKeys::Return:
                if (field.multiline) {
                    field.text += "\n";
                    RequestRedraw();
                } else {
                    CloseCardEditor(true);
                }
                return true;
            case UCKeys::Tab:
                editor.focusedField =
                        (editor.focusedField +
                         (event.shift ? static_cast<int>(editor.fields.size()) - 1
                                      : 1)) %
                        static_cast<int>(editor.fields.size());
                RequestRedraw();
                return true;
            case UCKeys::Backspace:
                PopUtf8(field.text);
                RequestRedraw();
                return true;
            default:
                return true;
        }
    }

    if (event.virtualKey == UCKeys::Escape) {
        if (draggingCard) {
            draggingCard = false;
            pressedCardId = -1;
            RequestRedraw();
            return true;
        }
        SelectCard(-1);
        return true;
    }
    if (editable && event.virtualKey == UCKeys::Delete &&
        selectedCardId >= 0) {
        auto ds = GetKanbanDataSource();
        int cardId = selectedCardId;
        if (ds && ds->RemoveCard(cardId)) {
            SelectCard(-1);
            if (onCardRemoved) onCardRemoved(cardId);
            if (onBoardChanged) onBoardChanged();
            RequestRedraw();
        }
        return true;
    }
    return false;
}

bool UltraCanvasKanbanBoardElement::HandleTextInput(const UCEvent& event) {
    const std::string& incoming =
            !event.text.empty()
                    ? event.text
                    : (event.character >= 32 ? std::string(1, event.character)
                                             : std::string());
    if (incoming.empty()) return false;

    if (renamingColumnId >= 0) {
        renameBuffer += incoming;
        RequestRedraw();
        return true;
    }
    if (editor.open) {
        editor.fields[editor.focusedField].text += incoming;
        RequestRedraw();
        return true;
    }
    return false;
}

void UltraCanvasKanbanBoardElement::CommitRename(bool save) {
    if (renamingColumnId < 0) return;
    auto ds = GetKanbanDataSource();
    if (save && ds) {
        std::string title = TrimText(renameBuffer);
        if (!title.empty()) {
            ds->RenameColumn(renamingColumnId, title);
            if (onBoardChanged) onBoardChanged();
        }
    }
    renamingColumnId = -1;
    renameBuffer.clear();
    RequestRedraw();
}

void UltraCanvasKanbanBoardElement::StartAddCard(int columnId) {
    auto ds = GetKanbanDataSource();
    if (!ds) return;
    int laneId = -1;
    if (!ds->GetLanes().empty()) laneId = ds->GetLanes().front().id;
    int cardId = ds->AddCard(columnId, "", laneId);
    if (cardId < 0) return;
    SelectCard(cardId);
    OpenCardEditor(cardId);
    editor.newCard = true;
    NotifyWipIfExceeded(columnId);
}

void UltraCanvasKanbanBoardElement::StartAddColumn() {
    auto ds = GetKanbanDataSource();
    if (!ds) return;
    int columnId = ds->AddColumn("New Column");
    renamingColumnId = columnId;
    renameBuffer = "New Column";
    SetFocus(true);
    if (onBoardChanged) onBoardChanged();
    RequestRedraw();
}

void UltraCanvasKanbanBoardElement::NotifyWipIfExceeded(int columnId) {
    auto ds = GetKanbanDataSource();
    if (ds && onWipExceeded && ds->IsWipExceeded(columnId)) {
        onWipExceeded(columnId);
    }
}

bool UltraCanvasKanbanBoardElement::OnEvent(const UCEvent& event) {
    if (IsDisabled() || !IsVisible()) return false;

    switch (event.type) {
        case UCEventType::MouseDown:
            if (event.button == UCMouseButton::Left) {
                return HandleMouseDown(event);
            }
            return false;

        case UCEventType::MouseDoubleClick:
            return HandleDoubleClick(event);

        case UCEventType::MouseUp:
            return HandleMouseUp(event);

        case UCEventType::MouseMove:
            if (pressedCardId >= 0 && !draggingCard && editable &&
                !editor.open) {
                int dx = event.pointer.x - pressAnchor.x;
                int dy = event.pointer.y - pressAnchor.y;
                if (dx * dx + dy * dy > 16) {
                    draggingCard = true;
                    if (isTooltipActive) HideTooltip();
                }
            }
            if (draggingCard) {
                dragPos = event.pointer;
                UpdateDropTarget(event.pointer);
                RequestRedraw();
                return true;
            }
            return HandleChartMouseMove(event.pointer);

        case UCEventType::MouseWheel:
            return HandleWheel(event);

        case UCEventType::MouseLeave:
            if (draggingCard) {
                draggingCard = false;
                dropColumnIndex = -1;
            }
            pressedCardId = -1;
            if (hoveredCardId != -1) {
                hoveredCardId = -1;
                RequestRedraw();
            }
            if (isTooltipActive) HideTooltip();
            return true;

        case UCEventType::KeyDown:
            return HandleKeyDown(event);

        case UCEventType::TextInput:
            return HandleTextInput(event);

        default:
            return false;
    }
}

} // namespace UltraCanvas
