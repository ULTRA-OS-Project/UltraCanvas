// core/ClipboardHistory/UltraCanvasClipboardHistoryView.cpp
// The list model and row painter for a clipboard history. See
// include/UltraCanvasClipboardHistoryView.h.
// Version: 1.0.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework

#include "UltraCanvasClipboardHistoryView.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasImage.h"

#include <algorithm>
#include <cctype>
#include <ctime>

namespace UltraCanvas {

namespace {

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
}

std::tm LocalTime(int64_t ms) {
    const std::time_t seconds = static_cast<std::time_t>(ms / 1000);
    std::tm local{};
    if (const std::tm* t = std::localtime(&seconds)) local = *t;   // the UI thread only
    return local;
}

std::string TwoDigits(int value) {
    return (value < 10 ? "0" : "") + std::to_string(value);
}

const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

size_t CountCodePoints(const std::string& text) {
    size_t count = 0;
    for (unsigned char c : text) count += (c & 0xC0) != 0x80;
    return count;
}

std::string WithThousands(uint64_t value) {
    std::string digits = std::to_string(value);
    for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3) digits.insert(static_cast<size_t>(i), ",");
    return digits;
}

std::string Host(const std::string& url) {
    const size_t scheme = url.find("://");
    if (scheme == std::string::npos) return "";
    const size_t start = scheme + 3;
    const size_t end = url.find_first_of("/?#", start);
    std::string host = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (const size_t at = host.rfind('@'); at != std::string::npos) host = host.substr(at + 1);
    return host;
}

int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

const std::string& IconsDir(const std::string& configured) {
    static const std::string defaultDir = GetResourcesDir() + "media/icons/clipboard/";
    return configured.empty() ? defaultDir : configured;
}

} // namespace

// ===== MODEL =====
void ClipboardHistoryListModel::SetEntries(std::vector<ClipboardHistoryEntry> entries, Sections sections) {
    rows.clear();
    entryCount = entries.size();
    if (sections == Sections::Flat) {
        for (auto& entry : entries) {
            ClipboardHistoryRow row;
            row.entry = std::move(entry);
            rows.push_back(std::move(row));
        }
        NotifyDataChanged();
        return;
    }

    // Day boundaries in local time.
    const int64_t now = NowMs();
    std::tm today = LocalTime(now);
    today.tm_hour = 0;
    today.tm_min = 0;
    today.tm_sec = 0;
    const int64_t startOfToday = static_cast<int64_t>(std::mktime(&today)) * 1000;
    const int64_t startOfYesterday = startOfToday - 86400000;

    auto sectionOf = [&](const ClipboardHistoryEntry& entry) -> std::string {
        if (entry.pinned) return "PINNED";
        if (sections == Sections::PinnedAndRecent) return "RECENT";
        if (entry.lastUsedAt >= startOfToday) return "TODAY";
        if (entry.lastUsedAt >= startOfYesterday) return "YESTERDAY";
        return "EARLIER";
    };
    std::string current;
    size_t headerIndex = 0;
    for (auto& entry : entries) {
        const std::string section = sectionOf(entry);
        if (section != current) {
            current = section;
            ClipboardHistoryRow header;
            header.header = true;
            header.headerText = section;
            headerIndex = rows.size();
            rows.push_back(std::move(header));
        }
        rows[headerIndex].headerCount++;
        ClipboardHistoryRow row;
        row.entry = std::move(entry);
        rows.push_back(std::move(row));
    }
    NotifyDataChanged();
}

ListDataValue ClipboardHistoryListModel::GetData(const ListIndex& index, ListDataRole role) const {
    const ClipboardHistoryRow* row = GetRow(index.row);
    if (!row) return std::monostate{};
    if (role == ListDataRole::DisplayRole) return row->header ? row->headerText : row->entry.title;
    if (role == ListDataRole::ToolTipRole && !row->header) {
        std::string text = row->entry.preview.empty() ? row->entry.title : row->entry.preview;
        if (text.size() > 600) {
            size_t n = 600;
            while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80) --n;
            text = text.substr(0, n) + "\xE2\x80\xA6";
        }
        return text;
    }
    return std::monostate{};
}

const ClipboardHistoryRow* ClipboardHistoryListModel::GetRow(int row) const {
    if (row < 0 || row >= static_cast<int>(rows.size())) return nullptr;
    return &rows[static_cast<size_t>(row)];
}

const ClipboardHistoryEntry* ClipboardHistoryListModel::GetEntry(int row) const {
    const ClipboardHistoryRow* r = GetRow(row);
    return r && !r->header ? &r->entry : nullptr;
}

int ClipboardHistoryListModel::FindRow(int64_t entryId) const {
    for (size_t i = 0; i < rows.size(); ++i) {
        if (!rows[i].header && rows[i].entry.id == entryId) return static_cast<int>(i);
    }
    return -1;
}

int ClipboardHistoryListModel::EntryRowFrom(int row, int step) const {
    if (step == 0) step = 1;
    for (int r = row; r >= 0 && r < static_cast<int>(rows.size()); r += step) {
        if (!rows[static_cast<size_t>(r)].header) return r;
    }
    return -1;
}

// ===== STYLES =====
ClipboardRowStyle ClipboardRowStyle::Light() {
    return ClipboardRowStyle{};
}

ClipboardRowStyle ClipboardRowStyle::Dark() {
    ClipboardRowStyle s;
    s.compact = true;
    s.rowHeight = 48;
    s.headerHeight = 24;
    s.thumbnailSize = 32;
    s.padding = 10;
    s.actionSize = 28;
    s.actionGlyph = 16;
    s.titleSize = 10.0f;
    s.metaSize = 8.5f;
    s.titleColor = Color(242, 242, 242);
    s.metaColor = Color(140, 140, 140);
    s.selectedTitleColor = Color(255, 255, 255);
    s.selectedMetaColor = Color(170, 170, 170);
    s.headerColor = Color(140, 140, 140);
    s.headerBackground = Color(31, 31, 31);
    s.divider = Color(31, 31, 31);
    s.actionColor = Color(229, 229, 229);
    s.actionActiveColor = Color(255, 255, 255);
    s.actionHoverBackground = Color(74, 74, 74);
    s.deleteHoverColor = Color(248, 113, 113);
    s.deleteHoverBackground = Color(90, 40, 40);
    s.pinColor = Color(96, 165, 250);
    s.copiedColor = Color(74, 222, 128);
    s.copiedBackground = Color(30, 70, 45);
    s.tileBackground = Color(55, 55, 55);
    s.tileBorder = Color(70, 70, 70);
    s.selectionBackground = Color(56, 56, 56);
    s.hoverBackground = Color(44, 44, 44);
    s.listBackground = Color(31, 31, 31);
    return s;
}

// ===== DESCRIPTIONS =====
std::string FormatClipboardSize(uint64_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " bytes";
    if (bytes < 1024 * 1024) return std::to_string((bytes + 512) / 1024) + " KB";
    const uint64_t tenths = (bytes * 10 + 524288) / 1048576;
    if (tenths >= 100) return std::to_string(tenths / 10) + " MB";
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " MB";
}

std::string FormatClipboardAge(int64_t whenMs, int64_t nowMs) {
    const int64_t seconds = std::max<int64_t>(0, (nowMs - whenMs) / 1000);
    if (seconds < 60) return "just now";
    if (seconds < 3600) return std::to_string(seconds / 60) + " min ago";
    const std::tm when = LocalTime(whenMs);
    const std::tm now = LocalTime(nowMs);
    const std::string clock = TwoDigits(when.tm_hour) + ":" + TwoDigits(when.tm_min);
    if (seconds < 6 * 3600) return std::to_string(seconds / 3600) + " h ago";
    if (when.tm_year == now.tm_year && when.tm_yday == now.tm_yday) return "today " + clock;
    std::tm yesterday = now;
    yesterday.tm_mday -= 1;
    std::mktime(&yesterday);
    if (when.tm_year == yesterday.tm_year && when.tm_yday == yesterday.tm_yday) return "yesterday " + clock;
    std::string date = std::to_string(when.tm_mday) + " " + kMonths[std::clamp(when.tm_mon, 0, 11)];
    if (when.tm_year != now.tm_year) date += " " + std::to_string(when.tm_year + 1900);
    return date;
}

bool ParseClipboardColour(const std::string& text, Color& colour) {
    std::string t;
    for (char c : text) {
        if (!std::isspace(static_cast<unsigned char>(c))) t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (!t.empty() && t[0] == '#') {
        std::vector<int> v;
        for (size_t i = 1; i < t.size(); ++i) {
            const int h = HexValue(t[i]);
            if (h < 0) return false;
            v.push_back(h);
        }
        if (v.size() == 3 || v.size() == 4) {
            colour = Color(static_cast<uint8_t>(v[0] * 17), static_cast<uint8_t>(v[1] * 17),
                           static_cast<uint8_t>(v[2] * 17), static_cast<uint8_t>(v.size() == 4 ? v[3] * 17 : 255));
            return true;
        }
        if (v.size() == 6 || v.size() == 8) {
            colour = Color(static_cast<uint8_t>(v[0] * 16 + v[1]), static_cast<uint8_t>(v[2] * 16 + v[3]),
                           static_cast<uint8_t>(v[4] * 16 + v[5]),
                           static_cast<uint8_t>(v.size() == 8 ? v[6] * 16 + v[7] : 255));
            return true;
        }
        return false;
    }
    // rgb(59, 130, 246), rgba(59 130 246 / 50%), rgb(100%, 50%, 0%): the first
    // three numbers, a percentage taken of 255. Close is plenty for a swatch.
    const bool rgba = t.rfind("rgba(", 0) == 0;
    if ((rgba || t.rfind("rgb(", 0) == 0) && t.back() == ')') {
        const std::string inner = t.substr(rgba ? 5 : 4, t.size() - (rgba ? 6 : 5));
        int values[3] = {0, 0, 0};
        int count = 0;
        size_t i = 0;
        while (i < inner.size() && count < 3) {
            if (!std::isdigit(static_cast<unsigned char>(inner[i]))) {
                if (inner[i] != ',' && inner[i] != ' ' && inner[i] != '/' && inner[i] != '.') return false;
                ++i;
                continue;
            }
            int value = 0;
            while (i < inner.size() && std::isdigit(static_cast<unsigned char>(inner[i]))) {
                value = std::min(value * 10 + (inner[i] - '0'), 100000);
                ++i;
            }
            while (i < inner.size() && (std::isdigit(static_cast<unsigned char>(inner[i])) || inner[i] == '.')) ++i;
            if (i < inner.size() && inner[i] == '%') {
                value = value * 255 / 100;
                ++i;
            }
            values[count++] = std::clamp(value, 0, 255);
        }
        if (count < 3) return false;
        colour = Color(static_cast<uint8_t>(values[0]), static_cast<uint8_t>(values[1]),
                       static_cast<uint8_t>(values[2]), 255);
        return true;
    }
    return false;
}

std::string DescribeClipboardEntry(const ClipboardHistoryEntry& entry, int64_t nowMs, bool compact) {
    const char* dot = " \xC2\xB7 ";   // ·
    std::string meta;
    auto add = [&meta, dot](const std::string& part) {
        if (part.empty()) return;
        if (!meta.empty()) meta += dot;
        meta += part;
    };
    switch (entry.kind) {
        case ClipboardEntryKind::Image:
            add("Image");
            if (entry.width > 0) add(std::to_string(entry.width) + " \xC3\x97 " + std::to_string(entry.height));
            if (!compact) add(FormatClipboardSize(entry.sizeBytes));
            break;
        case ClipboardEntryKind::Files:
            add(std::to_string(entry.fileCount) + (entry.fileCount == 1 ? " file" : " files"));
            if (entry.cut) add("cut");
            break;
        case ClipboardEntryKind::Link:
            add("Link");
            add(Host(entry.title));
            break;
        case ClipboardEntryKind::Colour: {
            add("Colour");
            Color c;
            if (!compact && !entry.title.empty() && entry.title[0] == '#' && ParseClipboardColour(entry.title, c)) {
                add("rgb(" + std::to_string(c.r) + ", " + std::to_string(c.g) + ", " + std::to_string(c.b) + ")");
            }
            break;
        }
        default: {
            add(ClipboardEntryKindName(entry.kind));
            if (!compact) {
                add(std::to_string(entry.lineCount) + (entry.lineCount == 1 ? " line" : " lines"));
                if (entry.sizeBytes <= entry.preview.size()) {
                    const size_t characters = CountCodePoints(entry.preview);
                    add(WithThousands(characters) + (characters == 1 ? " character" : " characters"));
                } else {
                    add(FormatClipboardSize(entry.sizeBytes));
                }
            }
            break;
        }
    }
    add(entry.sourceApplication);
    add(FormatClipboardAge(entry.lastUsedAt, nowMs));
    return meta;
}

// ===== THE ROW PAINTER =====
ClipboardHistoryRowDelegate::ClipboardHistoryRowDelegate(ClipboardRowStyle rowStyle) : style(rowStyle) {}

int ClipboardHistoryRowDelegate::GetRowHeight(const IListModel* model, int row) const {
    const auto* history = dynamic_cast<const ClipboardHistoryListModel*>(model);
    const ClipboardHistoryRow* r = history ? history->GetRow(row) : nullptr;
    return r && r->header ? style.headerHeight : style.rowHeight;
}

double ClipboardHistoryRowDelegate::ActionsLeft(int width) const {
    return width - (style.compact ? 8 : 12) - 3.0 * style.actionSize;
}

bool ClipboardHistoryRowDelegate::ActionsVisible(const ListItemStyleOption& option) const {
    return !style.compact || option.isHovered || option.isSelected;
}

ClipboardRowAction ClipboardHistoryRowDelegate::ActionAt(int row, const Point2Di& posInCell) const {
    (void)row;
    if (rowWidth <= 0) return ClipboardRowAction::NoAction;
    const double left = ActionsLeft(rowWidth);
    if (posInCell.x < left || posInCell.x >= left + 3.0 * style.actionSize) return ClipboardRowAction::NoAction;
    const int slot = static_cast<int>((posInCell.x - left) / style.actionSize);
    return slot == 0 ? ClipboardRowAction::Copy : slot == 1 ? ClipboardRowAction::Edit : ClipboardRowAction::Delete;
}

bool ClipboardHistoryRowDelegate::SetHover(int row, const Point2Di& posInCell) {
    const ClipboardRowAction action = row >= 0 ? ActionAt(row, posInCell) : ClipboardRowAction::NoAction;
    const bool changed = row != hoverRow || action != hoverAction;
    hoverRow = row;
    hoverAction = action;
    return changed;
}

void ClipboardHistoryRowDelegate::ShowCopied(int64_t entryId) {
    copiedId = entryId;
    copiedAt = std::chrono::steady_clock::now();
}

bool ClipboardHistoryRowDelegate::IsShowingCopied() const {
    return copiedId != 0 && std::chrono::steady_clock::now() - copiedAt < std::chrono::milliseconds(1500);
}

std::string ClipboardHistoryRowDelegate::ActionTooltip(ClipboardRowAction action) {
    switch (action) {
        case ClipboardRowAction::Copy: return "Copy  (Enter)";
        case ClipboardRowAction::Edit: return "Edit  (F2)";
        case ClipboardRowAction::Delete: return "Delete  (Del)";
        default: return "";
    }
}

void ClipboardHistoryRowDelegate::PaintText(IRenderContext* ctx, const std::string& text, const Rect2Dd& rect,
                                            float size, FontWeight weight, const Color& color, bool monospace) const {
    if (text.empty() || rect.width <= 4) return;
    auto layout = ctx->CreateTextLayout(text, false);
    if (!layout) return;
    FontStyle font;
    font.fontSize = size;
    font.fontWeight = weight;
    if (monospace) font.fontFamily = "monospace";
    layout->SetFontStyle(font);
    layout->SetWrap(TextWrap::WrapNone);
    layout->SetEllipsize(EllipsizeMode::EllipsizeEnd);
    layout->SetExplicitWidth(rect.width);
    ctx->SetTextPaint(color);
    const Size2Dd measured = layout->GetLayoutSize();
    ctx->DrawTextLayout(*layout, Point2Dd(rect.x, rect.y + (rect.height - measured.height) / 2.0));
}

void ClipboardHistoryRowDelegate::PaintThumbnail(IRenderContext* ctx, const ClipboardHistoryEntry& entry,
                                                 const Rect2Dd& tile) const {
    const double s = tile.width / 48.0;   // drawn on a 48-unit grid, like the mock-up
    const double radius = 7 * s;
    auto bar = [&](double x, double y, double w, const Color& colour) {
        ctx->DrawFilledRectangle(Rect2Dd(tile.x + x * s, tile.y + y * s, w * s, 3 * s), colour, 0.0f,
                                 Colors::Transparent, static_cast<float>(1.5 * s));
    };
    auto tileFill = [&](const Color& fill, const Color& border) {
        ctx->DrawFilledRectangle(tile, fill, border.a ? 1.0f : 0.0f, border, static_cast<float>(radius));
    };
    switch (entry.kind) {
        case ClipboardEntryKind::Image: {
            tileFill(style.tileBackground, style.tileBorder);
            if (!entry.thumbnailPath.empty()) {
                ctx->PushState();
                ctx->ClipRect(tile);
                ctx->DrawImage(entry.thumbnailPath, tile, ImageFitMode::Cover);
                ctx->PopState();
            }
            break;
        }
        case ClipboardEntryKind::Code:
            tileFill(Color(30, 30, 46), Colors::Transparent);
            bar(8, 12, 20, Color(137, 180, 250));
            bar(14, 20, 24, Color(249, 226, 175));
            bar(14, 28, 18, Color(166, 227, 161));
            bar(8, 36, 10, Color(137, 180, 250));
            break;
        case ClipboardEntryKind::RichText:
            tileFill(Color(255, 247, 237), Color(254, 215, 170));
            ctx->DrawFilledRectangle(Rect2Dd(tile.x + 8 * s, tile.y + 10 * s, 26 * s, 5 * s), Color(234, 88, 12),
                                     0.0f, Colors::Transparent, static_cast<float>(2 * s));
            bar(8, 21, 32, Color(168, 162, 158));
            bar(8, 28, 29, Color(168, 162, 158));
            bar(8, 35, 20, Color(168, 162, 158));
            bar(30, 35, 10, Color(37, 99, 235));
            break;
        case ClipboardEntryKind::Link: {
            tileFill(style.compact ? Color(30, 58, 95) : Color(232, 241, 251),
                     style.compact ? Colors::Transparent : Color(207, 227, 247));
            if (auto icon = UCImage::Get(IconsDir(iconsDir) + "link.svg")) {
                ctx->DrawMask(style.compact ? Color(147, 197, 253) : Colors::Selection, *icon,
                              Rect2Dd(tile.x + 12 * s, tile.y + 12 * s, 24 * s, 24 * s), ImageFitMode::Contain);
            }
            break;
        }
        case ClipboardEntryKind::Colour: {
            Color swatch(156, 163, 175);
            ParseClipboardColour(entry.title, swatch);
            ctx->DrawFilledRectangle(tile, swatch, 1.0f, Color(0, 0, 0, 38), static_cast<float>(radius));
            break;
        }
        case ClipboardEntryKind::Files: {
            tileFill(style.tileBackground, style.tileBorder);
            const Color paper = style.compact ? Color(229, 229, 229) : Colors::White;
            const Color edge(156, 163, 175);
            ctx->DrawFilledRectangle(Rect2Dd(tile.x + 18 * s, tile.y + 6 * s, 20 * s, 28 * s), paper, 1.0f, edge,
                                     static_cast<float>(2 * s));
            ctx->DrawFilledRectangle(Rect2Dd(tile.x + 18 * s, tile.y + 25 * s, 20 * s, 6 * s), Color(37, 99, 235),
                                     0.0f, Colors::Transparent, 0.0f);
            ctx->DrawFilledRectangle(Rect2Dd(tile.x + 10 * s, tile.y + 12 * s, 20 * s, 30 * s), paper, 1.0f, edge,
                                     static_cast<float>(2 * s));
            ctx->DrawFilledRectangle(Rect2Dd(tile.x + 10 * s, tile.y + 30 * s, 20 * s, 7 * s), Color(220, 38, 38),
                                     0.0f, Colors::Transparent, 0.0f);
            if (entry.fileCount > 1) {
                const Point2Dd centre(tile.x + 39 * s, tile.y + 39 * s);
                ctx->SetFillPaint(Color(55, 65, 81));
                ctx->FillCircle(centre, 8 * s);
                PaintText(ctx, "+" + std::to_string(std::min(entry.fileCount - 1, 99)),
                          Rect2Dd(centre.x - 7 * s, centre.y - 7 * s, 14 * s, 14 * s),
                          static_cast<float>(6.0 * s), FontWeight::Bold, Colors::White, false);
            }
            break;
        }
        default:   // Text
            tileFill(style.tileBackground, style.tileBorder);
            bar(8, 12, 32, Color(156, 163, 175));
            bar(8, 20, 26, Color(156, 163, 175));
            bar(8, 28, 30, Color(156, 163, 175));
            bar(8, 36, 17, Color(156, 163, 175));
            break;
    }
}

void ClipboardHistoryRowDelegate::PaintActions(IRenderContext* ctx, const ClipboardHistoryEntry& entry,
                                               const ListItemStyleOption& option, int row) const {
    const Rect2Di& r = option.rect;
    const double left = r.x + ActionsLeft(r.width);
    const double glyph = style.actionGlyph;
    const bool selectedLight = option.isSelected && !style.compact;
    const bool copied = entry.id == copiedId && IsShowingCopied();
    const char* names[3] = {"copy.svg", "edit.svg", "delete.svg"};
    const ClipboardRowAction actions[3] = {ClipboardRowAction::Copy, ClipboardRowAction::Edit,
                                           ClipboardRowAction::Delete};
    for (int i = 0; i < 3; ++i) {
        const double x = left + i * style.actionSize;
        const Point2Dd centre(x + style.actionSize / 2.0, r.y + r.height / 2.0);
        const bool hovered = row == hoverRow && actions[i] == hoverAction;
        Color colour = selectedLight ? Colors::White
                     : (option.isHovered || option.isSelected) ? style.actionActiveColor : style.actionColor;
        std::string icon = names[i];
        if (i == 0 && copied) {
            ctx->SetFillPaint(style.copiedBackground);
            ctx->FillCircle(centre, style.actionSize / 2.0);
            colour = style.copiedColor;
            icon = "check.svg";
        } else if (hovered) {
            const bool destructive = actions[i] == ClipboardRowAction::Delete;
            ctx->SetFillPaint(destructive ? style.deleteHoverBackground
                              : selectedLight ? Color(255, 255, 255, 60) : style.actionHoverBackground);
            ctx->FillCircle(centre, style.actionSize / 2.0);
            if (destructive) colour = style.deleteHoverColor;
        }
        if (auto image = UCImage::Get(IconsDir(iconsDir) + icon)) {
            ctx->DrawMask(colour, *image, Rect2Dd(centre.x - glyph / 2, centre.y - glyph / 2, glyph, glyph),
                          ImageFitMode::Contain);
        }
    }
    if (copied && !style.compact) {
        const double w = 62, h = 22;
        const Rect2Dd pill(left - w - 10, r.y + (r.height - h) / 2.0, w, h);
        ctx->DrawFilledRectangle(pill, style.copiedBackground, 0.0f, Colors::Transparent, static_cast<float>(h / 2));
        auto layout = ctx->CreateTextLayout("Copied", false);
        if (layout) {
            FontStyle font;
            font.fontSize = 8.5f;
            font.fontWeight = FontWeight::Bold;
            layout->SetFontStyle(font);
            const Size2Dd size = layout->GetLayoutSize();
            ctx->SetTextPaint(style.copiedColor);
            ctx->DrawTextLayout(*layout, Point2Dd(pill.x + (w - size.width) / 2, pill.y + (h - size.height) / 2));
        }
    }
}

void ClipboardHistoryRowDelegate::RenderItem(IRenderContext* ctx, const IListModel* model, int row, int,
                                             const ListItemStyleOption& option) {
    const auto* history = dynamic_cast<const ClipboardHistoryListModel*>(model);
    const ClipboardHistoryRow* r = history ? history->GetRow(row) : nullptr;
    if (!ctx || !r) return;
    const Rect2Di& rect = option.rect;
    rowWidth = rect.width;

    if (r->header) {
        ctx->DrawFilledRectangle(Rect2Dd(rect.x, rect.y, rect.width, rect.height), style.headerBackground, 0.0f,
                                 Colors::Transparent, 0.0f);
        PaintText(ctx, r->headerText + "   " + std::to_string(r->headerCount),
                  Rect2Dd(rect.x + style.padding + (style.compact ? 0 : 4), rect.y, rect.width / 2.0, rect.height),
                  style.headerSize, FontWeight::Bold, style.headerColor, false);
        if (!style.compact) {
            ctx->DrawFilledRectangle(Rect2Dd(rect.x, rect.y + rect.height - 1, rect.width, 1), style.divider, 0.0f,
                                     Colors::Transparent, 0.0f);
        }
        return;
    }

    const ClipboardHistoryEntry& entry = r->entry;
    const bool selectedLight = option.isSelected && !style.compact;
    if (style.compact && option.isSelected) {
        // The keyboard's row: an accent bar on its left edge.
        ctx->DrawFilledRectangle(Rect2Dd(rect.x + 2, rect.y + 8, 3, rect.height - 16), Colors::Selection, 0.0f,
                                 Colors::Transparent, 1.5f);
    }
    const double t = style.thumbnailSize;
    PaintThumbnail(ctx, entry, Rect2Dd(rect.x + style.padding, rect.y + (rect.height - t) / 2.0, t, t));

    const double textX = rect.x + style.padding + t + (style.compact ? 10 : 16);
    const bool actions = ActionsVisible(option);
    const double textRight = actions ? rect.x + ActionsLeft(rect.width) - 8 : rect.x + rect.width - style.padding;
    double titleX = textX;
    if (entry.pinned) {
        if (auto pin = UCImage::Get(IconsDir(iconsDir) + "pin.svg")) {
            const double side = style.compact ? 13 : 15;
            ctx->DrawMask(selectedLight ? Colors::White : style.pinColor, *pin,
                          Rect2Dd(textX, rect.y + rect.height * 0.32 - side / 2, side, side), ImageFitMode::Contain);
            titleX += side + 5;
        }
    }
    const double half = rect.height / 2.0;
    std::string title = entry.title;
    std::replace(title.begin(), title.end(), '\n', ' ');
    PaintText(ctx, title.empty() ? ClipboardEntryKindName(entry.kind) : title,
              Rect2Dd(titleX, rect.y + half - (style.compact ? 18 : 22), textRight - titleX, style.compact ? 18 : 22),
              style.titleSize, FontWeight::Normal, selectedLight ? style.selectedTitleColor : style.titleColor,
              entry.kind == ClipboardEntryKind::Code);
    PaintText(ctx, DescribeClipboardEntry(entry, NowMs(), style.compact),
              Rect2Dd(textX, rect.y + half + 1, textRight - textX, style.compact ? 16 : 20), style.metaSize,
              FontWeight::Normal, selectedLight ? style.selectedMetaColor : style.metaColor, false);

    if (actions) PaintActions(ctx, entry, option, row);
    if (!style.compact && !option.isSelected && !option.isHovered) {
        ctx->DrawFilledRectangle(Rect2Dd(textX, rect.y + rect.height - 1, rect.x + rect.width - textX, 1),
                                 style.divider, 0.0f, Colors::Transparent, 0.0f);
    }
}

} // namespace UltraCanvas
