// core/UltraCanvasSlideDeckEditor.cpp
// The editing core of a presentation (include/UltraCanvasSlideDeckEditor.h).
//
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
    auto near = [&](double hx, double hy) { return std::fabs(lx - hx) <= t && std::fabs(ly - hy) <= t; };
    const double l = static_cast<double>(box.x), r = static_cast<double>(box.Right());
    const double tp = static_cast<double>(box.y), b = static_cast<double>(box.Bottom());
    const double cx = (l + r) / 2.0, cy = (tp + b) / 2.0;
    if (one && near(cx, tp - kRotateHandleOffset * emuPerPixel)) return SlideHandle::Rotate;
    if (near(l, tp)) return SlideHandle::TopLeft;
    if (near(r, tp)) return SlideHandle::TopRight;
    if (near(r, b)) return SlideHandle::BottomRight;
    if (near(l, b)) return SlideHandle::BottomLeft;
    if (near(cx, tp)) return SlideHandle::Top;
    if (near(r, cy)) return SlideHandle::Right;
    if (near(cx, b)) return SlideHandle::Bottom;
    if (near(l, cy)) return SlideHandle::Left;
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
