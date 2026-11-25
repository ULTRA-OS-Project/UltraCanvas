// core/UltraCanvasSlideDeckEditor.cpp
// The editing core of a presentation (include/UltraCanvasSlideDeckEditor.h).
//
// Version: 1.3.0 - InsertTable with the cells' text, InsertFormattedTextBox, InsertChart
// Version: 1.2.0 - tables: InsertTable, TableCellBox, TableCellAt, rows and columns inserted and deleted
// Version: 1.1.0 - FormatSelectedText, AllSelectedText
// Version: 1.0.0
// Author: UltraCanvas Framework
#include "UltraCanvasSlideDeckEditor.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

namespace UltraCanvas {

namespace {

constexpr int64_t kDefaultSlideWidth = 12192000;
constexpr int64_t kDefaultSlideHeight = 6858000;
constexpr int64_t kPasteOffset = 152400;   // a sixth of an inch, as PowerPoint offsets a duplicate
constexpr double kPi = 3.14159265358979323846;

SlideBox BoxOf(const PptxShape& shape) { return SlideBox{shape.x, shape.y, shape.width, shape.height}; }

void PlaceShape(PptxShape& shape, const SlideBox& box) {
    shape.x = box.x;
    shape.y = box.y;
    shape.width = std::max<int64_t>(box.width, shape.kind == PptxShapeKind::Connector ? 0 : 1);
    shape.height = std::max<int64_t>(box.height, shape.kind == PptxShapeKind::Connector ? 0 : 1);
    shape.placeInherited = false;
}

// `x`, `y` turned by -`degrees` about the box's centre: the point in the
// shape's own, unturned frame.
void ToShapeFrame(const PptxShape& shape, int64_t x, int64_t y, double& outX, double& outY) {
    const double cx = static_cast<double>(shape.x) + static_cast<double>(shape.width) / 2.0;
    const double cy = static_cast<double>(shape.y) + static_cast<double>(shape.height) / 2.0;
    const double a = -shape.rotationDeg * kPi / 180.0;
    const double dx = static_cast<double>(x) - cx, dy = static_cast<double>(y) - cy;
    outX = cx + dx * std::cos(a) - dy * std::sin(a);
    outY = cy + dx * std::sin(a) + dy * std::cos(a);
}

// The title placeholder's text, which the slide keeps as its title.
void RefreshTitle(PptxSlide& slide) {
    for (const PptxShape& shape : slide.shapes) {
        if (shape.kind == PptxShapeKind::Title || shape.placeholder == "title" || shape.placeholder == "ctrTitle") {
            slide.title = UCSlideDeckEditor::ShapeText(shape);
            for (char& c : slide.title) if (c == '\n') c = ' ';
            return;
        }
    }
}

std::vector<PptxParagraph> ParagraphsOf(const std::string& text, const std::vector<PptxParagraph>& old) {
    PptxParagraph model = old.empty() ? PptxParagraph() : old.front();
    PptxRun look = model.runs.empty() ? PptxRun() : model.runs.front();
    look.hyperlink.clear();
    std::vector<PptxParagraph> paragraphs;
    size_t start = 0;
    while (true) {
        const size_t end = text.find('\n', start);
        PptxParagraph paragraph = model;
        PptxRun run = look;
        run.text = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        paragraph.runs = {run};
        paragraphs.push_back(paragraph);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return paragraphs;
}

} // namespace

// ===== COMMANDS =====

struct UCSlideDeckEditor::Command {
    std::string description;
    virtual ~Command() = default;
    virtual void Apply(UCSlideDeckEditor& editor) = 0;
    virtual void Revert(UCSlideDeckEditor& editor) = 0;
};

namespace {

// One slide before and after (and the selection on it).
struct ReplaceSlide : UCSlideDeckEditor::Command {
    int index = 0;
    PptxSlide before, after;
    std::vector<int> selectionBefore, selectionAfter;
    bool nudge = false;
    void Apply(UCSlideDeckEditor& editor) override {
        editor.GetDeck()->slides[static_cast<size_t>(index)] = after;
        editor.SetCurrentSlide(index);
        editor.Select(selectionAfter);
    }
    void Revert(UCSlideDeckEditor& editor) override {
        editor.GetDeck()->slides[static_cast<size_t>(index)] = before;
        editor.SetCurrentSlide(index);
        editor.Select(selectionBefore);
    }
};

struct InsertSlideCommand : UCSlideDeckEditor::Command {
    int index = 0;
    PptxSlide slide;
    int currentBefore = -1;
    bool remove = false;   // the same command the other way round: a deleted slide
    void Insert(UCSlideDeckEditor& editor) {
        auto& slides = editor.GetDeck()->slides;
        slides.insert(slides.begin() + index, slide);
        editor.SetCurrentSlide(index);
    }
    void Erase(UCSlideDeckEditor& editor) {
        auto& slides = editor.GetDeck()->slides;
        slides.erase(slides.begin() + index);
        const int count = static_cast<int>(slides.size());
        editor.SetCurrentSlide(count == 0 ? -1 : std::min(remove ? index : currentBefore, count - 1));
    }
    void Apply(UCSlideDeckEditor& editor) override { if (remove) Erase(editor); else Insert(editor); }
    void Revert(UCSlideDeckEditor& editor) override { if (remove) Insert(editor); else Erase(editor); }
};

struct MoveSlideCommand : UCSlideDeckEditor::Command {
    int from = 0, to = 0;
    static void Move(UCSlideDeckEditor& editor, int a, int b) {
        auto& slides = editor.GetDeck()->slides;
        PptxSlide moved = std::move(slides[static_cast<size_t>(a)]);
        slides.erase(slides.begin() + a);
        slides.insert(slides.begin() + b, std::move(moved));
        editor.SetCurrentSlide(b);
    }
    void Apply(UCSlideDeckEditor& editor) override { Move(editor, from, to); }
    void Revert(UCSlideDeckEditor& editor) override { Move(editor, to, from); }
};

} // namespace

// ===== THE DECK =====

UCSlideDeckEditor::UCSlideDeckEditor(std::shared_ptr<PptxPresentation> deck) { SetDeck(std::move(deck)); }

UCSlideDeckEditor::~UCSlideDeckEditor() = default;

void UCSlideDeckEditor::SetDeck(std::shared_ptr<PptxPresentation> deck) {
    deck_ = deck ? std::move(deck) : std::make_shared<PptxPresentation>();
    undo_.clear();
    redo_.clear();
    selection_.clear();
    modified_ = false;
    nudging_ = false;
    current_ = deck_->slides.empty() ? -1 : 0;
    SelectionChanged();
}

int64_t UCSlideDeckEditor::SlideWidth() const { return deck_->slideWidth > 0 ? deck_->slideWidth : kDefaultSlideWidth; }
int64_t UCSlideDeckEditor::SlideHeight() const { return deck_->slideHeight > 0 ? deck_->slideHeight : kDefaultSlideHeight; }
int UCSlideDeckEditor::GetSlideCount() const { return static_cast<int>(deck_->slides.size()); }

void UCSlideDeckEditor::SetCurrentSlide(int index) {
    const int count = GetSlideCount();
    const int next = count == 0 ? -1 : std::clamp(index, 0, count - 1);
    if (next == current_) return;
    current_ = next;
    selection_.clear();
    SelectionChanged();
}

const PptxSlide* UCSlideDeckEditor::CurrentSlide() const {
    return current_ >= 0 && current_ < GetSlideCount() ? &deck_->slides[static_cast<size_t>(current_)] : nullptr;
}

// ===== SELECTION =====

void UCSlideDeckEditor::Select(std::vector<int> shapes) {
    const PptxSlide* slide = CurrentSlide();
    const int count = slide ? static_cast<int>(slide->shapes.size()) : 0;
    shapes.erase(std::remove_if(shapes.begin(), shapes.end(), [count](int i) { return i < 0 || i >= count; }), shapes.end());
    std::sort(shapes.begin(), shapes.end());
    shapes.erase(std::unique(shapes.begin(), shapes.end()), shapes.end());
    if (shapes == selection_) return;
    selection_ = std::move(shapes);
    SelectionChanged();
}

void UCSlideDeckEditor::SelectAll() {
    std::vector<int> all;
    if (const PptxSlide* slide = CurrentSlide())
        for (size_t i = 0; i < slide->shapes.size(); ++i) all.push_back(static_cast<int>(i));
    Select(all);
}

void UCSlideDeckEditor::ClearSelection() { Select({}); }

void UCSlideDeckEditor::ToggleSelected(int shape) {
    std::vector<int> next = selection_;
    const auto it = std::find(next.begin(), next.end(), shape);
    if (it == next.end()) next.push_back(shape);
    else next.erase(it);
    Select(next);
}

bool UCSlideDeckEditor::IsSelected(int shape) const {
    return std::find(selection_.begin(), selection_.end(), shape) != selection_.end();
}

SlideBox UCSlideDeckEditor::SelectionBox() const {
    const PptxSlide* slide = CurrentSlide();
    if (!slide || selection_.empty()) return SlideBox{};
    int64_t left = INT64_MAX, top = INT64_MAX, right = INT64_MIN, bottom = INT64_MIN;
    for (int i : selection_) {
        const PptxShape& shape = slide->shapes[static_cast<size_t>(i)];
        left = std::min(left, shape.x);
        top = std::min(top, shape.y);
        right = std::max(right, shape.x + shape.width);
        bottom = std::max(bottom, shape.y + shape.height);
    }
    return SlideBox{left, top, right - left, bottom - top};
}

// ===== FINDING =====

int UCSlideDeckEditor::ShapeAt(int64_t x, int64_t y) const {
    const PptxSlide* slide = CurrentSlide();
    if (!slide) return -1;
    for (int i = static_cast<int>(slide->shapes.size()) - 1; i >= 0; --i) {
        const PptxShape& shape = slide->shapes[static_cast<size_t>(i)];
        double lx = 0, ly = 0;
        ToShapeFrame(shape, x, y, lx, ly);
        // A line is found within a little of its box, which may be flat.
        const double slack = shape.kind == PptxShapeKind::Connector ? 45720.0 : 0.0;
        if (lx >= shape.x - slack && lx <= shape.x + shape.width + slack && ly >= shape.y - slack &&
            ly <= shape.y + shape.height + slack)
            return i;
    }
    return -1;
}

std::vector<int> UCSlideDeckEditor::ShapesInside(const SlideBox& area) const {
    std::vector<int> inside;
    const PptxSlide* slide = CurrentSlide();
    if (!slide) return inside;
    for (size_t i = 0; i < slide->shapes.size(); ++i) {
        const PptxShape& shape = slide->shapes[i];
        if (shape.x >= area.x && shape.y >= area.y && shape.x + shape.width <= area.Right() &&
            shape.y + shape.height <= area.Bottom())
            inside.push_back(static_cast<int>(i));
    }
    return inside;
}

SlideHandle UCSlideDeckEditor::HandleAt(int64_t x, int64_t y, double emuPerPixel) const {
    const PptxSlide* slide = CurrentSlide();
    if (!slide || selection_.empty()) return SlideHandle::NoHandle;
    double lx = static_cast<double>(x), ly = static_cast<double>(y);
    SlideBox box = SelectionBox();
    const bool one = selection_.size() == 1;
    if (one) {
        const PptxShape& shape = slide->shapes[static_cast<size_t>(selection_.front())];
        ToShapeFrame(shape, x, y, lx, ly);
        box = BoxOf(shape);
    }
    const double t = kHandleReach * emuPerPixel;
    auto closeTo = [&](double hx, double hy) { return std::fabs(lx - hx) <= t && std::fabs(ly - hy) <= t; };
    const double l = static_cast<double>(box.x), r = static_cast<double>(box.Right());
    const double tp = static_cast<double>(box.y), b = static_cast<double>(box.Bottom());
    const double cx = (l + r) / 2.0, cy = (tp + b) / 2.0;
    if (one && closeTo(cx, tp - kRotateHandleOffset * emuPerPixel)) return SlideHandle::Rotate;
    if (closeTo(l, tp)) return SlideHandle::TopLeft;
    if (closeTo(r, tp)) return SlideHandle::TopRight;
    if (closeTo(r, b)) return SlideHandle::BottomRight;
    if (closeTo(l, b)) return SlideHandle::BottomLeft;
    if (closeTo(cx, tp)) return SlideHandle::Top;
    if (closeTo(r, cy)) return SlideHandle::Right;
    if (closeTo(cx, b)) return SlideHandle::Bottom;
    if (closeTo(l, cy)) return SlideHandle::Left;
    return SlideHandle::NoHandle;
}

SlideSnap UCSlideDeckEditor::Snap(const SlideBox& box, int64_t tolerance, const std::vector<int>& ignore, bool moving,
                                  SlideHandle handle) const {
    SlideSnap result;
    result.box = box;
    std::vector<int64_t> xs = {0, SlideWidth() / 2, SlideWidth()};
    std::vector<int64_t> ys = {0, SlideHeight() / 2, SlideHeight()};
    if (const PptxSlide* slide = CurrentSlide()) {
        for (size_t i = 0; i < slide->shapes.size(); ++i) {
            if (std::find(ignore.begin(), ignore.end(), static_cast<int>(i)) != ignore.end()) continue;
            const PptxShape& shape = slide->shapes[i];
            if (std::fabs(shape.rotationDeg) > 0.01) continue;   // a turned shape's box is not its edges
            xs.insert(xs.end(), {shape.x, shape.x + shape.width / 2, shape.x + shape.width});
            ys.insert(ys.end(), {shape.y, shape.y + shape.height / 2, shape.y + shape.height});
        }
    }
    // The closest candidate to any of `edges`, within the tolerance.
    auto best = [tolerance](const std::vector<int64_t>& edges, const std::vector<int64_t>& candidates,
                            int64_t& delta, int64_t& line) {
        bool found = false;
        for (int64_t edge : edges) {
            for (int64_t c : candidates) {
                const int64_t d = c - edge;
                if (std::llabs(d) <= tolerance && (!found || std::llabs(d) < std::llabs(delta))) {
                    delta = d;
                    line = c;
                    found = true;
                }
            }
        }
        return found;
    };
    int64_t dx = 0, dy = 0, lineX = 0, lineY = 0;
    if (moving) {
        if (best({box.x, box.CentreX(), box.Right()}, xs, dx, lineX)) {
            result.box.x += dx;
            result.verticalGuides.push_back(lineX);
        }
        if (best({box.y, box.CentreY(), box.Bottom()}, ys, dy, lineY)) {
            result.box.y += dy;
            result.horizontalGuides.push_back(lineY);
        }
        return result;
    }
    const bool left = handle == SlideHandle::Left || handle == SlideHandle::TopLeft || handle == SlideHandle::BottomLeft;
    const bool right = handle == SlideHandle::Right || handle == SlideHandle::TopRight || handle == SlideHandle::BottomRight;
    const bool top = handle == SlideHandle::Top || handle == SlideHandle::TopLeft || handle == SlideHandle::TopRight;
    const bool bottom = handle == SlideHandle::Bottom || handle == SlideHandle::BottomLeft || handle == SlideHandle::BottomRight;
    if (left && best({box.x}, xs, dx, lineX)) {
        result.box.x += dx;
        result.box.width -= dx;
        result.verticalGuides.push_back(lineX);
    } else if (right && best({box.Right()}, xs, dx, lineX)) {
        result.box.width += dx;
        result.verticalGuides.push_back(lineX);
    }
    if (top && best({box.y}, ys, dy, lineY)) {
        result.box.y += dy;
        result.box.height -= dy;
        result.horizontalGuides.push_back(lineY);
    } else if (bottom && best({box.Bottom()}, ys, dy, lineY)) {
        result.box.height += dy;
        result.horizontalGuides.push_back(lineY);
    }
    return result;
}

// ===== CHANGES TO SHAPES =====

void UCSlideDeckEditor::ChangeSlide(const std::string& description, const std::function<void(PptxSlide&)>& change,
                                    const std::vector<int>* selectAfter) {
    if (!CurrentSlide()) return;
    auto command = std::make_unique<ReplaceSlide>();
    command->description = description;
    command->index = current_;
    command->before = *CurrentSlide();
    command->after = command->before;
    command->selectionBefore = selection_;
    change(command->after);
    RefreshTitle(command->after);
    command->selectionAfter = selectAfter ? *selectAfter : selection_;
    Run(std::move(command));
}

void UCSlideDeckEditor::MoveSelection(int64_t dx, int64_t dy) {
    if (selection_.empty() || (dx == 0 && dy == 0)) return;
    const std::vector<int> moved = selection_;
    ChangeSlide("Move", [&](PptxSlide& slide) {
        for (int i : moved) {
            slide.shapes[static_cast<size_t>(i)].x += dx;
            slide.shapes[static_cast<size_t>(i)].y += dy;
            slide.shapes[static_cast<size_t>(i)].placeInherited = false;
        }
    });
}

void UCSlideDeckEditor::NudgeSelection(int64_t dx, int64_t dy) {
    if (selection_.empty()) return;
    auto* last = undo_.empty() ? nullptr : dynamic_cast<ReplaceSlide*>(undo_.back().get());
    if (nudging_ && last && last->nudge && last->index == current_ && last->selectionAfter == selection_) {
        // This nudge joins the last: one undo step for a run of them.
        PptxSlide& slide = deck_->slides[static_cast<size_t>(current_)];
        for (int i : selection_) {
            slide.shapes[static_cast<size_t>(i)].x += dx;
            slide.shapes[static_cast<size_t>(i)].y += dy;
        }
        last->after = slide;
        modified_ = true;
        if (onChange) onChange();
        return;
    }
    MoveSelection(dx, dy);
    if (auto* step = undo_.empty() ? nullptr : dynamic_cast<ReplaceSlide*>(undo_.back().get())) {
        step->nudge = true;
        step->description = "Nudge";
        nudging_ = true;
    }
}

void UCSlideDeckEditor::SetShapeBox(int shape, const SlideBox& box) {
    const PptxSlide* slide = CurrentSlide();
    if (!slide || shape < 0 || shape >= static_cast<int>(slide->shapes.size())) return;
    ChangeSlide("Resize", [&](PptxSlide& s) { PlaceShape(s.shapes[static_cast<size_t>(shape)], box); });
}

void UCSlideDeckEditor::SetSelectionBox(const SlideBox& from, const SlideBox& to) {
    if (selection_.empty() || from.width <= 0 || from.height <= 0) return;
    const std::vector<int> shapes = selection_;
    ChangeSlide("Resize", [&](PptxSlide& slide) {
        const double sx = static_cast<double>(to.width) / static_cast<double>(from.width);
        const double sy = static_cast<double>(to.height) / static_cast<double>(from.height);
        for (int i : shapes) {
            PptxShape& shape = slide.shapes[static_cast<size_t>(i)];
            SlideBox box;
            box.x = to.x + static_cast<int64_t>(std::llround(static_cast<double>(shape.x - from.x) * sx));
            box.y = to.y + static_cast<int64_t>(std::llround(static_cast<double>(shape.y - from.y) * sy));
            box.width = static_cast<int64_t>(std::llround(static_cast<double>(shape.width) * sx));
            box.height = static_cast<int64_t>(std::llround(static_cast<double>(shape.height) * sy));
            PlaceShape(shape, box);
        }
    });
}

void UCSlideDeckEditor::SetShapeRotation(int shape, double degrees) {
    const PptxSlide* slide = CurrentSlide();
    if (!slide || shape < 0 || shape >= static_cast<int>(slide->shapes.size())) return;
    double normal = std::fmod(degrees, 360.0);
    if (normal < 0) normal += 360.0;
    ChangeSlide("Rotate", [&](PptxSlide& s) { s.shapes[static_cast<size_t>(shape)].rotationDeg = normal; });
}

void UCSlideDeckEditor::DeleteSelection() {
    if (selection_.empty()) return;
    const std::vector<int> gone = selection_;
    const std::vector<int> none;
    ChangeSlide("Delete", [&](PptxSlide& slide) {
        for (auto it = gone.rbegin(); it != gone.rend(); ++it) slide.shapes.erase(slide.shapes.begin() + *it);
    }, &none);
}

void UCSlideDeckEditor::DuplicateSelection() {
    if (selection_.empty()) return;
    const std::vector<int> originals = selection_;
    std::vector<int> copies;
    const size_t count = CurrentSlide()->shapes.size();
    for (size_t k = 0; k < originals.size(); ++k) copies.push_back(static_cast<int>(count + k));
    ChangeSlide("Duplicate", [&](PptxSlide& slide) {
        for (int i : originals) {
            PptxShape copy = slide.shapes[static_cast<size_t>(i)];
            copy.x += kPasteOffset;
            copy.y += kPasteOffset;
            copy.placeholder.clear();   // a copy of the title is a text box, not a second title
            if (copy.kind == PptxShapeKind::Title || copy.kind == PptxShapeKind::Subtitle || copy.kind == PptxShapeKind::Body)
                copy.kind = PptxShapeKind::Text;
            slide.shapes.push_back(copy);
        }
    }, &copies);
}

void UCSlideDeckEditor::Reorder(int how) {
    if (selection_.empty()) return;
    const PptxSlide* slide = CurrentSlide();
    const int count = static_cast<int>(slide->shapes.size());
    // The new order as old indices.
    std::vector<int> order(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) order[static_cast<size_t>(i)] = i;
    auto selected = [this](int i) { return IsSelected(i); };
    if (how == 0 || how == 1) {   // front / back
        std::stable_partition(order.begin(), order.end(), [&](int i) { return how == 0 ? !selected(i) : selected(i); });
    } else if (how == 2) {        // forward: each selected one past the next unselected one above it
        for (int k = count - 2; k >= 0; --k)
            if (selected(order[static_cast<size_t>(k)]) && !selected(order[static_cast<size_t>(k + 1)]))
                std::swap(order[static_cast<size_t>(k)], order[static_cast<size_t>(k + 1)]);
    } else {                      // backward
        for (int k = 1; k < count; ++k)
            if (selected(order[static_cast<size_t>(k)]) && !selected(order[static_cast<size_t>(k - 1)]))
                std::swap(order[static_cast<size_t>(k)], order[static_cast<size_t>(k - 1)]);
    }
    std::vector<int> nextSelection;
    for (int k = 0; k < count; ++k)
        if (selected(order[static_cast<size_t>(k)])) nextSelection.push_back(k);
    const char* names[] = {"Bring to front", "Send to back", "Bring forward", "Send backward"};
    ChangeSlide(names[how], [&](PptxSlide& s) {
        std::vector<PptxShape> shapes;
        for (int i : order) shapes.push_back(s.shapes[static_cast<size_t>(i)]);
        s.shapes = std::move(shapes);
    }, &nextSelection);
}

void UCSlideDeckEditor::BringToFront() { Reorder(0); }
void UCSlideDeckEditor::SendToBack() { Reorder(1); }
void UCSlideDeckEditor::BringForward() { Reorder(2); }
void UCSlideDeckEditor::SendBackward() { Reorder(3); }

int UCSlideDeckEditor::InsertShape(const PptxShape& shape) {
    if (!CurrentSlide()) return -1;
    const int index = static_cast<int>(CurrentSlide()->shapes.size());
    const std::vector<int> inserted = {index};
    ChangeSlide("Insert", [&](PptxSlide& slide) { slide.shapes.push_back(shape); }, &inserted);
    return index;
}

int UCSlideDeckEditor::InsertTextBox(const SlideBox& box, const std::string& text) {
    PptxShape shape;
    shape.kind = PptxShapeKind::Text;
    shape.name = "TextBox";
    PlaceShape(shape, box);
    PptxRun run;
    run.sizePt = 18;
    run.font = deck_->minorFont;
    PptxParagraph paragraph;
    paragraph.runs.push_back(run);
    shape.paragraphs = ParagraphsOf(text, {paragraph});
    return InsertShape(shape);
}

int UCSlideDeckEditor::InsertFormattedTextBox(const SlideBox& box, const std::vector<PptxParagraph>& paragraphs) {
    PptxShape shape;
    shape.kind = PptxShapeKind::Text;
    shape.name = "TextBox";
    PlaceShape(shape, box);
    shape.paragraphs = paragraphs;
    for (PptxParagraph& paragraph : shape.paragraphs) {
        if (paragraph.runs.empty()) paragraph.runs.emplace_back();
        for (PptxRun& run : paragraph.runs) {
            if (run.sizePt <= 0) run.sizePt = 18;
            if (run.font.empty()) run.font = deck_->minorFont;
        }
    }
    if (shape.paragraphs.empty()) {
        PptxRun run;
        run.sizePt = 18;
        run.font = deck_->minorFont;
        shape.paragraphs.emplace_back();
        shape.paragraphs.back().runs.push_back(run);
    }
    return InsertShape(shape);
}

int UCSlideDeckEditor::InsertChart(const SlideBox& box, const RichChart& chart) {
    if (!chart.IsChart() || !CurrentSlide()) return -1;
    PptxShape shape;
    shape.kind = PptxShapeKind::Chart;
    shape.name = chart.title.empty() ? "Chart" : chart.title;
    PlaceShape(shape, box);
    shape.chart = chart;
    return InsertShape(shape);
}

int UCSlideDeckEditor::InsertGeometry(const SlideBox& box, const std::string& geometry, const std::string& fill) {
    PptxShape shape;
    shape.kind = PptxShapeKind::Text;
    shape.name = geometry;
    shape.geometry = geometry;
    shape.fill = fill;
    shape.line = "2F528F";
    shape.lineWidthPt = 1;
    shape.anchor = PptxAnchor::Middle;
    PlaceShape(shape, box);
    PptxRun run;
    run.sizePt = 18;
    run.color = "FFFFFF";
    run.font = deck_->minorFont;
    PptxParagraph paragraph;
    paragraph.align = PptxAlign::Center;
    paragraph.runs.push_back(run);
    shape.paragraphs = {paragraph};
    return InsertShape(shape);
}

int UCSlideDeckEditor::InsertPicture(const std::vector<uint8_t>& bytes, const std::string& extension, int widthPx,
                                     int heightPx, const std::string& altText) {
    if (bytes.empty() || !CurrentSlide()) return -1;
    std::string ext = extension;
    if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string target;
    for (int n = static_cast<int>(deck_->pictures.size()) + 1;; ++n) {
        target = "ppt/media/inserted" + std::to_string(n) + "." + (ext.empty() ? "png" : ext);
        if (!deck_->pictures.count(target)) break;
    }
    deck_->pictures[target] = bytes;
    PptxShape shape;
    shape.kind = PptxShapeKind::Picture;
    shape.name = "Picture";
    shape.pictureTarget = target;
    shape.altText = altText;
    // At most half the slide either way, the picture's proportions kept, centred.
    const double aspect = widthPx > 0 && heightPx > 0 ? static_cast<double>(widthPx) / heightPx : 4.0 / 3.0;
    double width = static_cast<double>(SlideWidth()) / 2.0;
    double height = width / aspect;
    if (height > static_cast<double>(SlideHeight()) / 2.0) {
        height = static_cast<double>(SlideHeight()) / 2.0;
        width = height * aspect;
    }
    SlideBox box;
    box.width = static_cast<int64_t>(width);
    box.height = static_cast<int64_t>(height);
    box.x = (SlideWidth() - box.width) / 2;
    box.y = (SlideHeight() - box.height) / 2;
    PlaceShape(shape, box);
    return InsertShape(shape);
}

std::string UCSlideDeckEditor::ShapeText(const PptxShape& shape) {
    std::string text;
    for (size_t i = 0; i < shape.paragraphs.size(); ++i) {
        if (i) text += '\n';
        text += shape.paragraphs[i].Text();
    }
    return text;
}

void UCSlideDeckEditor::SetShapeText(int shape, const std::string& text) {
    const PptxSlide* slide = CurrentSlide();
    if (!slide || shape < 0 || shape >= static_cast<int>(slide->shapes.size())) return;
    if (ShapeText(slide->shapes[static_cast<size_t>(shape)]) == text) return;
    ChangeSlide("Typing", [&](PptxSlide& s) {
        PptxShape& target = s.shapes[static_cast<size_t>(shape)];
        target.paragraphs = ParagraphsOf(text, target.paragraphs);
    });
}

void UCSlideDeckEditor::FormatSelectedText(const std::string& description, const std::function<void(PptxRun&)>& run,
                                           const std::function<void(PptxParagraph&)>& paragraph) {
    if (selection_.empty()) return;
    const std::vector<int> shapes = selection_;
    ChangeSlide(description, [&](PptxSlide& slide) {
        auto apply = [&](std::vector<PptxParagraph>& paragraphs) {
            for (PptxParagraph& p : paragraphs) {
                if (paragraph) paragraph(p);
                if (run) for (PptxRun& r : p.runs) run(r);
            }
        };
        for (int i : shapes) {
            PptxShape& shape = slide.shapes[static_cast<size_t>(i)];
            apply(shape.paragraphs);
            for (auto& row : shape.rows)
                for (auto& cell : row) apply(cell);
        }
    });
}

bool UCSlideDeckEditor::AllSelectedText(const std::function<bool(const PptxRun&)>& test) const {
    const PptxSlide* slide = CurrentSlide();
    if (!slide || selection_.empty()) return false;
    bool any = false;
    for (int i : selection_) {
        const PptxShape& shape = slide->shapes[static_cast<size_t>(i)];
        for (const PptxParagraph& p : shape.paragraphs)
            for (const PptxRun& r : p.runs) {
                if (r.text.empty() || r.text == "\n") continue;
                if (!test(r)) return false;
                any = true;
            }
    }
    return any;
}

// ===== TABLES =====

namespace {

std::vector<PptxParagraph> CellText(float sizePt, const std::string& color, bool bold) {
    PptxRun run;
    run.sizePt = sizePt;
    run.color = color;
    run.bold = bold;
    PptxParagraph paragraph;
    paragraph.runs.push_back(run);
    return {paragraph};
}

// The table's look for row `row`: PowerPoint's Medium Style 2, accent 1.
PptxCellStyle BandStyle(int row) {
    PptxCellStyle style;
    style.fill = row == 0 ? "4472C4" : (row % 2 ? "CFD5EA" : "E9EBF5");
    style.textColor = row == 0 ? "FFFFFF" : "";
    style.bold = row == 0;
    PptxCellStyle::Border white{"FFFFFF", 1.0};
    style.left = style.right = style.top = style.bottom = white;
    return style;
}

// Sizes scaled so they add up to `total`.
std::vector<double> Scaled(const std::vector<int64_t>& sizes, int64_t total, size_t count) {
    std::vector<double> out(count, count ? static_cast<double>(total) / static_cast<double>(count) : 0.0);
    double sum = 0;
    for (size_t i = 0; i < count && i < sizes.size(); ++i) sum += static_cast<double>(sizes[i]);
    if (sum <= 0 || sizes.size() < count) return out;
    for (size_t i = 0; i < count; ++i) out[i] = static_cast<double>(sizes[i]) * static_cast<double>(total) / sum;
    return out;
}

} // namespace

static PptxShape BandedTable(const SlideBox& box, int rows, int columns, float sizePt);

int UCSlideDeckEditor::InsertTable(const SlideBox& box, int rows, int columns) {
    return InsertShape(BandedTable(box, rows, columns, 18));
}

int UCSlideDeckEditor::InsertTable(const SlideBox& box, const std::vector<std::vector<std::vector<PptxParagraph>>>& cells) {
    size_t columns = 0;
    for (const auto& row : cells) columns = std::max(columns, row.size());
    if (cells.empty() || columns == 0) return -1;
    // A wide table in smaller type, as it has to fit the slide.
    const float sizePt = columns <= 4 ? 18.0f : columns <= 8 ? 14.0f : 11.0f;
    PptxShape table = BandedTable(box, static_cast<int>(cells.size()), static_cast<int>(columns), sizePt);
    for (size_t r = 0; r < table.rows.size(); ++r) {
        for (size_t c = 0; c < table.rows[r].size(); ++c) {
            if (c >= cells[r].size() || cells[r][c].empty()) continue;
            const PptxCellStyle& style = table.cellStyles[r][c];
            std::vector<PptxParagraph> text = cells[r][c];
            for (PptxParagraph& paragraph : text) {
                if (paragraph.runs.empty()) paragraph.runs.emplace_back();
                for (PptxRun& run : paragraph.runs) {
                    if (run.sizePt <= 0) run.sizePt = sizePt;
                    if (run.color.empty()) run.color = style.textColor;
                    run.bold = run.bold || style.bold;
                }
            }
            table.rows[r][c] = std::move(text);
        }
    }
    return InsertShape(table);
}

// A rows x columns table at `box`, in the banded look, its cells empty in
// `sizePt` type.
static PptxShape BandedTable(const SlideBox& box, int rows, int columns, float sizePt) {
    rows = std::clamp(rows, 1, 75);
    columns = std::clamp(columns, 1, 75);
    PptxShape table;
    table.kind = PptxShapeKind::Table;
    table.name = "Table";
    PlaceShape(table, box);
    for (int r = 0; r < rows; ++r) {
        std::vector<std::vector<PptxParagraph>> row;
        std::vector<PptxCellStyle> styles;
        for (int c = 0; c < columns; ++c) {
            const PptxCellStyle style = BandStyle(r);
            row.push_back(CellText(sizePt, style.textColor, style.bold));
            styles.push_back(style);
        }
        table.rows.push_back(row);
        table.cellStyles.push_back(styles);
        table.rowHeights.push_back(box.height / rows);
    }
    for (int c = 0; c < columns; ++c) table.columnWidths.push_back(box.width / columns);
    return table;
}

SlideBox UCSlideDeckEditor::TableCellBox(const PptxShape& table, int row, int col) {
    const size_t rows = table.rows.size();
    size_t columns = 0;
    for (const auto& r : table.rows) columns = std::max(columns, r.size());
    if (rows == 0 || columns == 0) return SlideBox{table.x, table.y, table.width, table.height};
    const std::vector<double> widths = Scaled(table.columnWidths, table.width, columns);
    const std::vector<double> heights = Scaled(table.rowHeights, table.height, rows);
    int spanCols = 1, spanRows = 1;
    if (row >= 0 && row < static_cast<int>(table.cellStyles.size()) &&
        col >= 0 && col < static_cast<int>(table.cellStyles[static_cast<size_t>(row)].size())) {
        const PptxCellStyle& style = table.cellStyles[static_cast<size_t>(row)][static_cast<size_t>(col)];
        spanCols = std::max(1, style.gridSpan);
        spanRows = std::max(1, style.rowSpan);
    }
    double x = static_cast<double>(table.x), y = static_cast<double>(table.y), w = 0, h = 0;
    for (int c = 0; c < col && c < static_cast<int>(columns); ++c) x += widths[static_cast<size_t>(c)];
    for (int r = 0; r < row && r < static_cast<int>(rows); ++r) y += heights[static_cast<size_t>(r)];
    for (int c = col; c < col + spanCols && c < static_cast<int>(columns); ++c) w += widths[static_cast<size_t>(c)];
    for (int r = row; r < row + spanRows && r < static_cast<int>(rows); ++r) h += heights[static_cast<size_t>(r)];
    return SlideBox{static_cast<int64_t>(x), static_cast<int64_t>(y), static_cast<int64_t>(w), static_cast<int64_t>(h)};
}

bool UCSlideDeckEditor::TableCellAt(int shape, int64_t x, int64_t y, int& row, int& col) const {
    const PptxSlide* slide = CurrentSlide();
    if (!slide || shape < 0 || shape >= static_cast<int>(slide->shapes.size())) return false;
    const PptxShape& table = slide->shapes[static_cast<size_t>(shape)];
    if (table.kind != PptxShapeKind::Table || table.rows.empty()) return false;
    for (int r = 0; r < static_cast<int>(table.rows.size()); ++r) {
        for (int c = 0; c < static_cast<int>(table.rows[static_cast<size_t>(r)].size()); ++c) {
            const bool away = r < static_cast<int>(table.cellStyles.size()) &&
                              c < static_cast<int>(table.cellStyles[static_cast<size_t>(r)].size()) &&
                              table.cellStyles[static_cast<size_t>(r)][static_cast<size_t>(c)].mergedAway;
            if (away) continue;
            const SlideBox box = TableCellBox(table, r, c);
            if (x >= box.x && x < box.Right() && y >= box.y && y < box.Bottom()) {
                row = r;
                col = c;
                return true;
            }
        }
    }
    return false;
}

void UCSlideDeckEditor::InsertTableRow(int shape, int row, bool below) {
    const PptxSlide* slide = CurrentSlide();
    if (!slide || shape < 0 || shape >= static_cast<int>(slide->shapes.size())) return;
    if (slide->shapes[static_cast<size_t>(shape)].kind != PptxShapeKind::Table) return;
    ChangeSlide("Insert row", [&](PptxSlide& s) {
        PptxShape& table = s.shapes[static_cast<size_t>(shape)];
        const int count = static_cast<int>(table.rows.size());
        const int at = std::clamp(below ? row + 1 : row, 0, count);
        // The look of the row it is next to - under the header row, the row
        // under it, so a new body row is not a second header.
        int from = std::clamp(row, 0, std::max(0, count - 1));
        if (below && from == 0 && count > 1) from = 1;
        // The new row copies the look of the one it is next to, emptied.
        std::vector<std::vector<PptxParagraph>> cells;
        std::vector<PptxCellStyle> styles;
        if (count > 0) {
            for (const auto& cell : table.rows[static_cast<size_t>(from)]) {
                PptxRun look = !cell.empty() && !cell.front().runs.empty() ? cell.front().runs.front() : PptxRun();
                look.text.clear();
                PptxParagraph paragraph = cell.empty() ? PptxParagraph() : cell.front();
                paragraph.runs = {look};
                cells.push_back({paragraph});
            }
            if (from < static_cast<int>(table.cellStyles.size())) {
                styles = table.cellStyles[static_cast<size_t>(from)];
                for (PptxCellStyle& style : styles) { style.rowSpan = 1; style.mergedAway = false; style.gridSpan = 1; }
            }
        }
        table.rows.insert(table.rows.begin() + at, cells);
        if (!table.cellStyles.empty()) table.cellStyles.insert(table.cellStyles.begin() + std::min<int>(at, static_cast<int>(table.cellStyles.size())), styles);
        const int64_t height = from < static_cast<int>(table.rowHeights.size()) ? table.rowHeights[static_cast<size_t>(from)]
                                                                               : table.height / std::max(1, count);
        if (!table.rowHeights.empty()) table.rowHeights.insert(table.rowHeights.begin() + std::min<int>(at, static_cast<int>(table.rowHeights.size())), height);
        table.height += height;
    });
}

void UCSlideDeckEditor::DeleteTableRow(int shape, int row) {
    const PptxSlide* slide = CurrentSlide();
    if (!slide || shape < 0 || shape >= static_cast<int>(slide->shapes.size())) return;
    const PptxShape& current = slide->shapes[static_cast<size_t>(shape)];
    if (current.kind != PptxShapeKind::Table || current.rows.size() <= 1 || row < 0 || row >= static_cast<int>(current.rows.size())) return;
    ChangeSlide("Delete row", [&](PptxSlide& s) {
        PptxShape& table = s.shapes[static_cast<size_t>(shape)];
        int64_t height = table.height / static_cast<int64_t>(table.rows.size());
        if (row < static_cast<int>(table.rowHeights.size())) {
            height = table.rowHeights[static_cast<size_t>(row)];
            table.rowHeights.erase(table.rowHeights.begin() + row);
        }
        table.rows.erase(table.rows.begin() + row);
        if (row < static_cast<int>(table.cellStyles.size())) table.cellStyles.erase(table.cellStyles.begin() + row);
        table.height = std::max<int64_t>(1, table.height - height);
    });
}

void UCSlideDeckEditor::InsertTableColumn(int shape, int col, bool right) {
    const PptxSlide* slide = CurrentSlide();
    if (!slide || shape < 0 || shape >= static_cast<int>(slide->shapes.size())) return;
    if (slide->shapes[static_cast<size_t>(shape)].kind != PptxShapeKind::Table) return;
    ChangeSlide("Insert column", [&](PptxSlide& s) {
        PptxShape& table = s.shapes[static_cast<size_t>(shape)];
        size_t columns = 0;
        for (const auto& r : table.rows) columns = std::max(columns, r.size());
        const int from = std::clamp(col, 0, std::max(0, static_cast<int>(columns) - 1));
        const int at = std::clamp(right ? from + 1 : from, 0, static_cast<int>(columns));
        for (size_t r = 0; r < table.rows.size(); ++r) {
            auto& row = table.rows[r];
            PptxParagraph paragraph;
            if (from < static_cast<int>(row.size()) && !row[static_cast<size_t>(from)].empty()) {
                paragraph = row[static_cast<size_t>(from)].front();
                PptxRun look = paragraph.runs.empty() ? PptxRun() : paragraph.runs.front();
                look.text.clear();
                paragraph.runs = {look};
            }
            row.insert(row.begin() + std::min<int>(at, static_cast<int>(row.size())), std::vector<PptxParagraph>{paragraph});
            if (r < table.cellStyles.size()) {
                auto& styles = table.cellStyles[r];
                PptxCellStyle style = from < static_cast<int>(styles.size()) ? styles[static_cast<size_t>(from)] : PptxCellStyle();
                style.gridSpan = 1;
                style.mergedAway = false;
                styles.insert(styles.begin() + std::min<int>(at, static_cast<int>(styles.size())), style);
            }
        }
        const int64_t width = from < static_cast<int>(table.columnWidths.size()) ? table.columnWidths[static_cast<size_t>(from)]
                                                                                : table.width / static_cast<int64_t>(std::max<size_t>(1, columns));
        if (!table.columnWidths.empty())
            table.columnWidths.insert(table.columnWidths.begin() + std::min<int>(at, static_cast<int>(table.columnWidths.size())), width);
        table.width += width;
    });
}

void UCSlideDeckEditor::DeleteTableColumn(int shape, int col) {
    const PptxSlide* slide = CurrentSlide();
    if (!slide || shape < 0 || shape >= static_cast<int>(slide->shapes.size())) return;
    const PptxShape& current = slide->shapes[static_cast<size_t>(shape)];
    size_t columns = 0;
    for (const auto& r : current.rows) columns = std::max(columns, r.size());
    if (current.kind != PptxShapeKind::Table || columns <= 1 || col < 0 || col >= static_cast<int>(columns)) return;
    ChangeSlide("Delete column", [&](PptxSlide& s) {
        PptxShape& table = s.shapes[static_cast<size_t>(shape)];
        int64_t width = table.width / static_cast<int64_t>(columns);
        if (col < static_cast<int>(table.columnWidths.size())) {
            width = table.columnWidths[static_cast<size_t>(col)];
            table.columnWidths.erase(table.columnWidths.begin() + col);
        }
        for (auto& row : table.rows)
            if (col < static_cast<int>(row.size())) row.erase(row.begin() + col);
        for (auto& styles : table.cellStyles)
            if (col < static_cast<int>(styles.size())) styles.erase(styles.begin() + col);
        table.width = std::max<int64_t>(1, table.width - width);
    });
}

void UCSlideDeckEditor::CopySelection() {
    const PptxSlide* slide = CurrentSlide();
    if (!slide || selection_.empty()) return;
    clipboard_.clear();
    for (int i : selection_) clipboard_.push_back(slide->shapes[static_cast<size_t>(i)]);
}

void UCSlideDeckEditor::CutSelection() {
    CopySelection();
    DeleteSelection();
}

void UCSlideDeckEditor::Paste() {
    const PptxSlide* slide = CurrentSlide();
    if (!slide || clipboard_.empty()) return;
    // In place, unless a shape already stands there (a paste on the slide it
    // came from): then a little down and to the right.
    bool occupied = false;
    for (const PptxShape& copy : clipboard_)
        for (const PptxShape& shape : slide->shapes)
            occupied = occupied || (shape.x == copy.x && shape.y == copy.y && shape.width == copy.width);
    const size_t count = slide->shapes.size();
    std::vector<int> pasted;
    for (size_t k = 0; k < clipboard_.size(); ++k) pasted.push_back(static_cast<int>(count + k));
    ChangeSlide("Paste", [&](PptxSlide& s) {
        for (PptxShape copy : clipboard_) {
            if (occupied) { copy.x += kPasteOffset; copy.y += kPasteOffset; }
            s.shapes.push_back(copy);
        }
    }, &pasted);
    if (occupied)
        for (PptxShape& copy : clipboard_) { copy.x += kPasteOffset; copy.y += kPasteOffset; }
}

// ===== CHANGES TO THE SLIDE LIST =====

void UCSlideDeckEditor::InsertSlide(int index, PptxSlide slide) {
    const int count = GetSlideCount();
    auto command = std::make_unique<InsertSlideCommand>();
    command->description = "New slide";
    command->index = index < 0 || index > count ? count : index;
    RefreshTitle(slide);
    command->slide = std::move(slide);
    command->currentBefore = current_;
    Run(std::move(command));
}

void UCSlideDeckEditor::NewSlide(int layoutIndex) {
    PptxSlide slide = PptxSlideFromLayout(*deck_, layoutIndex);
    // Without a layout, the next slide takes the current one's (PowerPoint's
    // way), a title slide's next being Title and Content.
    if (layoutIndex < 0 && CurrentSlide() && CurrentSlide()->layoutIndex >= 0) {
        const PptxSlide* current = CurrentSlide();
        int next = current->layoutIndex;
        const auto& layouts = deck_->layouts;
        if (next < static_cast<int>(layouts.size()) && layouts[static_cast<size_t>(next)].type == "title") {
            for (size_t i = 0; i < layouts.size(); ++i)
                if (layouts[i].type == "obj") { next = static_cast<int>(i); break; }
        }
        slide = PptxSlideFromLayout(*deck_, next);
    }
    InsertSlide(current_ + 1, std::move(slide));
}

void UCSlideDeckEditor::DuplicateSlide(int index) {
    if (index < 0 || index >= GetSlideCount()) return;
    PptxSlide copy = deck_->slides[static_cast<size_t>(index)];
    InsertSlide(index + 1, std::move(copy));
    if (!undo_.empty()) undo_.back()->description = "Duplicate slide";
}

void UCSlideDeckEditor::DeleteSlide(int index) {
    if (index < 0 || index >= GetSlideCount()) return;
    auto command = std::make_unique<InsertSlideCommand>();
    command->description = "Delete slide";
    command->remove = true;
    command->index = index;
    command->slide = deck_->slides[static_cast<size_t>(index)];
    command->currentBefore = current_;
    Run(std::move(command));
}

void UCSlideDeckEditor::MoveSlide(int from, int to) {
    const int count = GetSlideCount();
    if (from < 0 || from >= count) return;
    to = std::clamp(to, 0, count - 1);
    if (from == to) return;
    auto command = std::make_unique<MoveSlideCommand>();
    command->description = "Move slide";
    command->from = from;
    command->to = to;
    Run(std::move(command));
}

void UCSlideDeckEditor::SetSlideHidden(int index, bool hidden) {
    if (index < 0 || index >= GetSlideCount() || deck_->slides[static_cast<size_t>(index)].hidden == hidden) return;
    SetCurrentSlide(index);
    ChangeSlide(hidden ? "Hide slide" : "Show slide", [hidden](PptxSlide& slide) { slide.hidden = hidden; });
}

std::string UCSlideDeckEditor::SlideNotesText(const PptxSlide& slide) {
    std::string text;
    for (size_t i = 0; i < slide.notes.size(); ++i) {
        if (i) text += '\n';
        text += slide.notes[i].Text();
    }
    return text;
}

void UCSlideDeckEditor::SetSlideNotes(int index, const std::string& text) {
    if (index < 0 || index >= GetSlideCount() || SlideNotesText(deck_->slides[static_cast<size_t>(index)]) == text) return;
    SetCurrentSlide(index);
    const std::vector<int> keep = selection_;
    ChangeSlide("Notes", [&](PptxSlide& slide) {
        slide.notes = text.empty() ? std::vector<PptxParagraph>() : ParagraphsOf(text, slide.notes);
    });
    Select(keep);
}

// ===== UNDO =====

void UCSlideDeckEditor::Run(std::unique_ptr<Command> command) {
    command->Apply(*this);
    undo_.push_back(std::move(command));
    redo_.clear();
    nudging_ = false;
    RenumberSlides();
    Changed();
}

void UCSlideDeckEditor::Undo() {
    if (undo_.empty()) return;
    std::unique_ptr<Command> command = std::move(undo_.back());
    undo_.pop_back();
    command->Revert(*this);
    redo_.push_back(std::move(command));
    nudging_ = false;
    RenumberSlides();
    Changed();
}

void UCSlideDeckEditor::Redo() {
    if (redo_.empty()) return;
    std::unique_ptr<Command> command = std::move(redo_.back());
    redo_.pop_back();
    command->Apply(*this);
    undo_.push_back(std::move(command));
    nudging_ = false;
    RenumberSlides();
    Changed();
}

std::string UCSlideDeckEditor::UndoDescription() const { return undo_.empty() ? std::string() : undo_.back()->description; }
std::string UCSlideDeckEditor::RedoDescription() const { return redo_.empty() ? std::string() : redo_.back()->description; }

void UCSlideDeckEditor::RenumberSlides() {
    for (size_t i = 0; i < deck_->slides.size(); ++i) deck_->slides[i].number = static_cast<int>(i) + 1;
}

void UCSlideDeckEditor::Changed() {
    modified_ = true;
    if (onChange) onChange();
}

void UCSlideDeckEditor::SelectionChanged() {
    if (onSelectionChange) onSelectionChange();
}

} // namespace UltraCanvas
