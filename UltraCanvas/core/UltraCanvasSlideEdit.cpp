// core/UltraCanvasSlideEdit.cpp
// The presentation editor element (include/UltraCanvasSlideEdit.h).
//
// Version: 1.2.0 - a double-click on a table asks for its cell (onEditTableCell)
// Version: 1.1.0 - SetEditingShape: the shape's text left out while a host edits it in place
// Version: 1.0.0
// Author: UltraCanvas Framework
#include "UltraCanvasSlideEdit.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasPptxSlideRender.h"
#include "UltraCanvasRenderContext.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace UltraCanvas {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kCanvasMargin = 32.0;       // pixels round the slide at 1:1 fit
constexpr int kStripNumberWidth = 30;        // the slide numbers' column
constexpr int kStripGap = 14;
constexpr int kStripTop = 12;
constexpr double kDragThreshold = 4.0;
constexpr double kHandleSize = 8.0;
constexpr int64_t kNudge = 91440;            // a tenth of an inch
constexpr int64_t kFineNudge = 12700;        // a point

const Color kWorkspace(232, 232, 235);
const Color kStripFill(244, 244, 246);
const Color kStripLine(214, 214, 220);
const Color kAccent(41, 98, 191);
const Color kGuide(220, 40, 120);
const Color kPlaceholderLine(150, 150, 150);
const Color kNumber(90, 90, 90);

Point2Dd Turn(const Point2Dd& p, const Point2Dd& centre, double degrees) {
    const double a = degrees * kPi / 180.0;
    const double dx = p.x - centre.x, dy = p.y - centre.y;
    return Point2Dd(centre.x + dx * std::cos(a) - dy * std::sin(a), centre.y + dx * std::sin(a) + dy * std::cos(a));
}

bool HasText(const PptxShape& shape) {
    for (const PptxParagraph& paragraph : shape.paragraphs)
        for (const PptxRun& run : paragraph.runs)
            if (!run.text.empty()) return true;
    return false;
}

bool TakesText(const PptxShape& shape) {
    return shape.kind == PptxShapeKind::Title || shape.kind == PptxShapeKind::Subtitle ||
           shape.kind == PptxShapeKind::Body || shape.kind == PptxShapeKind::Text;
}

// A placeholder: one that names its type, or a title, subtitle or content
// box (PowerPoint's <p:ph idx="1"/> names none: "obj" is the default).
bool IsPlaceholder(const PptxShape& shape) {
    return !shape.placeholder.empty() || shape.kind == PptxShapeKind::Title || shape.kind == PptxShapeKind::Subtitle ||
           shape.kind == PptxShapeKind::Body;
}

// What an empty placeholder says.
std::string PromptFor(const PptxShape& shape) {
    if (!IsPlaceholder(shape) || !TakesText(shape)) return std::string();
    if (shape.placeholder == "title" || shape.placeholder == "ctrTitle" || shape.kind == PptxShapeKind::Title)
        return Tr("Click to add title");
    if (shape.placeholder == "subTitle" || shape.kind == PptxShapeKind::Subtitle) return Tr("Click to add subtitle");
    if (shape.placeholder == "dt" || shape.placeholder == "ftr" || shape.placeholder == "sldNum") return std::string();
    return Tr("Click to add text");
}

UCMouseCursor CursorFor(SlideHandle handle) {
    switch (handle) {
        case SlideHandle::TopLeft: case SlideHandle::BottomRight: return UCMouseCursor::SizeNWSE;
        case SlideHandle::TopRight: case SlideHandle::BottomLeft: return UCMouseCursor::SizeNESW;
        case SlideHandle::Top: case SlideHandle::Bottom: return UCMouseCursor::SizeNS;
        case SlideHandle::Left: case SlideHandle::Right: return UCMouseCursor::SizeWE;
        case SlideHandle::Rotate: return UCMouseCursor::Hand;
        default: return UCMouseCursor::Arrow;
    }
}

} // namespace

UltraCanvasSlideEdit::UltraCanvasSlideEdit(const std::string& id, int x, int y, int w, int h)
    : UltraCanvasUIElement(id, static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)) {
    // The editor lives as long as the element: raw `this` in its callbacks.
    editor_.onChange = [this]() {
        RequestRedraw();
        if (onChange) onChange();
    };
    editor_.onSelectionChange = [this]() {
        RequestRedraw();
        if (onSelectionChange) onSelectionChange();
    };
}

UltraCanvasSlideEdit::~UltraCanvasSlideEdit() = default;

// ===== THE DECK AND THE VIEW =====

void UltraCanvasSlideEdit::SetDeck(std::shared_ptr<PptxPresentation> deck) {
    drag_ = Drag::NoDrag;
    preview_.reset();
    editor_.SetDeck(std::move(deck));
    zoom_ = 1.0;
    panX_ = panY_ = stripScroll_ = 0;
    RequestRedraw();
}

void UltraCanvasSlideEdit::SetShowSlideStrip(bool show) {
    showStrip_ = show;
    RequestRedraw();
}

void UltraCanvasSlideEdit::SetStripWidth(int width) {
    stripWidth_ = std::clamp(width, 100, 400);
    RequestRedraw();
}

void UltraCanvasSlideEdit::SetZoom(double zoom) {
    zoom_ = std::clamp(zoom, 0.25, 8.0);
    ClampPan();
    RequestRedraw();
}

void UltraCanvasSlideEdit::SetEditingShape(int shape) {
    if (editingShape_ == shape) return;
    editingShape_ = shape;
    RequestRedraw();
}

void UltraCanvasSlideEdit::SetEditingCell(int shape, int row, int col) {
    editingCellShape_ = shape;
    editingCellRow_ = row;
    editingCellCol_ = col;
    RequestRedraw();
}

void UltraCanvasSlideEdit::GoToSlide(int index) {
    editor_.SetCurrentSlide(index);
    EnsureThumbVisible(editor_.GetCurrentSlide());
    RequestRedraw();
}

Rect2Dd UltraCanvasSlideEdit::StripRect() const {
    const Rect2Df b = GetLocalBounds();
    return showStrip_ ? Rect2Dd(0, 0, std::min<double>(stripWidth_, b.width), b.height) : Rect2Dd(0, 0, 0, b.height);
}

Rect2Dd UltraCanvasSlideEdit::CanvasRect() const {
    const Rect2Df b = GetLocalBounds();
    const Rect2Dd strip = StripRect();
    return Rect2Dd(strip.width, 0, std::max(0.0, b.width - strip.width), b.height);
}

double UltraCanvasSlideEdit::FitScale() const {
    const Rect2Dd canvas = CanvasRect();
    const double w = std::max(1.0, canvas.width - 2 * kCanvasMargin);
    const double h = std::max(1.0, canvas.height - 2 * kCanvasMargin);
    return std::min(w / static_cast<double>(editor_.SlideWidth()), h / static_cast<double>(editor_.SlideHeight()));
}

Rect2Dd UltraCanvasSlideEdit::SlideRect() const {
    const Rect2Dd canvas = CanvasRect();
    const double scale = FitScale() * zoom_;
    const double w = static_cast<double>(editor_.SlideWidth()) * scale;
    const double h = static_cast<double>(editor_.SlideHeight()) * scale;
    // Centred while it fits; scrolled (panX_, panY_) when it is larger.
    const double x = w <= canvas.width - 2 * kCanvasMargin ? canvas.x + (canvas.width - w) / 2.0
                                                           : canvas.x + kCanvasMargin - panX_;
    const double y = h <= canvas.height - 2 * kCanvasMargin ? canvas.y + (canvas.height - h) / 2.0
                                                            : canvas.y + kCanvasMargin - panY_;
    return Rect2Dd(x, y, w, h);
}

double UltraCanvasSlideEdit::EmuPerPixel() const {
    const double scale = FitScale() * zoom_;
    return scale > 0 ? 1.0 / scale : 1.0;
}

void UltraCanvasSlideEdit::ClampPan() {
    const Rect2Dd canvas = CanvasRect();
    const double scale = FitScale() * zoom_;
    const double w = static_cast<double>(editor_.SlideWidth()) * scale;
    const double h = static_cast<double>(editor_.SlideHeight()) * scale;
    panX_ = std::clamp(panX_, 0.0, std::max(0.0, w - (canvas.width - 2 * kCanvasMargin)));
    panY_ = std::clamp(panY_, 0.0, std::max(0.0, h - (canvas.height - 2 * kCanvasMargin)));
}

Point2Dd UltraCanvasSlideEdit::ToSlide(const Point2Df& p) const {
    const Rect2Dd slide = SlideRect();
    const double emu = EmuPerPixel();
    return Point2Dd((p.x - slide.x) * emu, (p.y - slide.y) * emu);
}

Point2Dd UltraCanvasSlideEdit::ToView(double x, double y) const {
    const Rect2Dd slide = SlideRect();
    const double emu = EmuPerPixel();
    return Point2Dd(slide.x + x / emu, slide.y + y / emu);
}

int UltraCanvasSlideEdit::ThumbHeight() const {
    const int width = std::max(20, stripWidth_ - kStripNumberWidth - 12);
    return static_cast<int>(std::lround(width * static_cast<double>(editor_.SlideHeight()) /
                                        static_cast<double>(editor_.SlideWidth())));
}

Rect2Dd UltraCanvasSlideEdit::ThumbRect(int index) const {
    const int width = std::max(20, stripWidth_ - kStripNumberWidth - 12);
    return Rect2Dd(kStripNumberWidth, kStripTop + index * (ThumbHeight() + kStripGap) - stripScroll_, width, ThumbHeight());
}

int UltraCanvasSlideEdit::StripSlideAt(const Point2Df& p) const {
    if (!showStrip_ || p.x >= StripRect().width) return -1;
    for (int i = 0; i < editor_.GetSlideCount(); ++i) {
        const Rect2Dd r = ThumbRect(i);
        if (p.y >= r.y - kStripGap / 2.0 && p.y < r.y + r.height + kStripGap / 2.0) return i;
    }
    return -1;
}

int UltraCanvasSlideEdit::StripDropAt(const Point2Df& p) const {
    // The gap the pointer is nearest: before slide i (0 .. count).
    const int count = editor_.GetSlideCount();
    for (int i = 0; i < count; ++i) {
        const Rect2Dd r = ThumbRect(i);
        if (p.y < r.y + r.height / 2.0) return i;
    }
    return count;
}

void UltraCanvasSlideEdit::EnsureThumbVisible(int index) {
    if (index < 0 || !showStrip_) return;
    const Rect2Dd strip = StripRect();
    const Rect2Dd r = ThumbRect(index);
    if (r.y < kStripTop) stripScroll_ -= kStripTop - r.y;
    else if (r.y + r.height > strip.height - kStripTop) stripScroll_ += r.y + r.height - (strip.height - kStripTop);
    stripScroll_ = std::max(0.0, stripScroll_);
}

// ===== DRAWING =====

// ===== THE SHOW =====

void UltraCanvasSlideEdit::StartShow(int fromSlide) {
    showing_ = true;
    drag_ = Drag::NoDrag;
    preview_.reset();
    const auto& slides = editor_.GetDeck()->slides;
    showSlide_ = -1;
    for (int i = std::max(0, fromSlide); i < static_cast<int>(slides.size()); ++i)
        if (!slides[static_cast<size_t>(i)].hidden) { showSlide_ = i; break; }
    SetFocus(true);
    RequestRedraw();
}

void UltraCanvasSlideEdit::StopShow() {
    if (!showing_) return;
    showing_ = false;
    RequestRedraw();
    if (onShowEnd) onShowEnd();
}

void UltraCanvasSlideEdit::ShowStep(int direction) {
    const auto& slides = editor_.GetDeck()->slides;
    const int count = static_cast<int>(slides.size());
    if (showSlide_ < 0) {   // the end screen: on ends the show, back goes to the last slide
        if (direction > 0) { StopShow(); return; }
        for (int i = count - 1; i >= 0; --i)
            if (!slides[static_cast<size_t>(i)].hidden) { showSlide_ = i; break; }
        RequestRedraw();
        return;
    }
    for (int i = showSlide_ + direction; i >= 0 && i < count; i += direction) {
        if (!slides[static_cast<size_t>(i)].hidden) { showSlide_ = i; RequestRedraw(); return; }
    }
    if (direction > 0) { showSlide_ = -1; RequestRedraw(); }
}

void UltraCanvasSlideEdit::DrawShow(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    ctx->SetFillPaint(Colors::Black);
    ctx->FillRectangle(Rect2Dd(0, 0, b.width, b.height));
    const auto& deck = editor_.GetDeck();
    if (showSlide_ >= 0 && showSlide_ < static_cast<int>(deck->slides.size())) {
        const double scale = std::min(b.width / static_cast<double>(editor_.SlideWidth()),
                                      b.height / static_cast<double>(editor_.SlideHeight()));
        const double w = static_cast<double>(editor_.SlideWidth()) * scale;
        const double h = static_cast<double>(editor_.SlideHeight()) * scale;
        DrawPptxSlide(ctx, *deck, deck->slides[static_cast<size_t>(showSlide_)],
                      Rect2Dd((b.width - w) / 2.0, (b.height - h) / 2.0, w, h));
        return;
    }
    FontStyle font;
    font.fontSize = 14;
    ctx->SetFontStyle(font);
    ctx->SetTextPaint(Color(200, 200, 200));
    ctx->DrawText(Tr("End of the slide show. Click to leave."), Point2Dd(24, 24));
}

bool UltraCanvasSlideEdit::OnShowEvent(const UCEvent& event) {
    switch (event.type) {
        case UCEventType::MouseDown:
            ShowStep(event.button == UCMouseButton::Right ? -1 : 1);
            return true;
        case UCEventType::MouseWheel:
            ShowStep(event.wheelDelta > 0 ? -1 : 1);
            return true;
        case UCEventType::KeyDown:
            switch (event.virtualKey) {
                case UCKeys::Escape: StopShow(); return true;
                case UCKeys::Right: case UCKeys::Down: case UCKeys::PageDown: case UCKeys::Space: case UCKeys::Return:
                case UCKeys::N:
                    ShowStep(1); return true;
                case UCKeys::Left: case UCKeys::Up: case UCKeys::PageUp: case UCKeys::Backspace: case UCKeys::P:
                    ShowStep(-1); return true;
                case UCKeys::Home: StartShow(0); return true;
                case UCKeys::End: showSlide_ = -1; ShowStep(-1); return true;
                default: return true;   // nothing else edits during the show
            }
        default:
            return false;
    }
}

void UltraCanvasSlideEdit::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    (void)dirtyRect;
    if (!IsVisible()) return;
    if (showing_) { DrawShow(ctx); return; }
    const Rect2Df b = GetLocalBounds();
    ctx->PushState();
    ctx->SetFillPaint(kWorkspace);
    ctx->FillRectangle(Rect2Dd(0, 0, b.width, b.height));
    DrawCanvas(ctx);
    if (showStrip_) DrawStrip(ctx);
    ctx->PopState();
}

void UltraCanvasSlideEdit::DrawStrip(IRenderContext* ctx) {
    const Rect2Dd strip = StripRect();
    ctx->PushState();
    ctx->ClipRect(strip);
    ctx->SetFillPaint(kStripFill);
    ctx->FillRectangle(strip);
    ctx->SetStrokePaint(kStripLine);
    ctx->SetStrokeWidth(1.0f);
    ctx->DrawLine(Point2Dd(strip.width - 0.5, 0), Point2Dd(strip.width - 0.5, strip.height));
    const auto& deck = editor_.GetDeck();
    FontStyle font;
    font.fontSize = 10;
    ctx->SetFontStyle(font);
    for (int i = 0; i < editor_.GetSlideCount(); ++i) {
        const Rect2Dd r = ThumbRect(i);
        if (r.y + r.height < 0 || r.y > strip.height) continue;
        const PptxSlide& slide = deck->slides[static_cast<size_t>(i)];
        DrawPptxSlide(ctx, *deck, slide, r);
        if (slide.hidden) {   // dimmed: left out of the show
            ctx->SetFillPaint(Color(255, 255, 255, 140));
            ctx->FillRectangle(r);
        }
        const bool current = i == editor_.GetCurrentSlide();
        ctx->SetStrokePaint(current ? kAccent : kStripLine);
        ctx->SetStrokeWidth(current ? 2.5f : 1.0f);
        ctx->DrawRectangle(Rect2Dd(r.x - 1, r.y - 1, r.width + 2, r.height + 2));
        ctx->SetTextPaint(current ? kAccent : kNumber);
        const std::string number = std::to_string(i + 1);
        ctx->DrawText(number, Point2Dd(kStripNumberWidth - 8 - ctx->GetTextLineWidth(number), r.y));
    }
    if (drag_ == Drag::StripSlide && dragStarted_ && stripDropIndex_ >= 0) {
        // Where the slide goes: a line in the gap.
        const double y = stripDropIndex_ < editor_.GetSlideCount() ? ThumbRect(stripDropIndex_).y - kStripGap / 2.0
                                                                   : ThumbRect(stripDropIndex_ - 1).y +
                                                                         ThumbHeight() + kStripGap / 2.0;
        ctx->SetStrokePaint(kAccent);
        ctx->SetStrokeWidth(3.0f);
        ctx->DrawLine(Point2Dd(kStripNumberWidth - 6, y), Point2Dd(strip.width - 8, y));
    }
    ctx->PopState();
}

void UltraCanvasSlideEdit::DrawCanvas(IRenderContext* ctx) {
    const PptxSlide* current = editor_.CurrentSlide();
    if (!current) return;
    const PptxSlide& slide = preview_ ? *preview_ : *current;
    const Rect2Dd canvas = CanvasRect();
    const Rect2Dd rect = SlideRect();
    ctx->PushState();
    ctx->ClipRect(canvas);
    // A soft shadow under the slide.
    ctx->SetFillPaint(Color(0, 0, 0, 30));
    ctx->FillRectangle(Rect2Dd(rect.x + 3, rect.y + 4, rect.width, rect.height));
    PptxSlideDrawOptions options;
    options.promptText = [this](const PptxShape& shape, size_t index) {
        return static_cast<int>(index) == editingShape_ ? std::string() : PromptFor(shape);
    };
    options.hideTextOfShape = editingShape_;
    options.hideTextOfCellShape = editingCellShape_;
    options.hideTextOfCellRow = editingCellRow_;
    options.hideTextOfCellColumn = editingCellCol_;
    DrawPptxSlide(ctx, *editor_.GetDeck(), slide, rect, options);

    // Empty placeholders show where they are.
    ctx->SetStrokePaint(kPlaceholderLine);
    ctx->SetStrokeWidth(1.0f);
    ctx->SetLineDash(UCDashPattern({4, 3}));
    for (size_t i = 0; i < slide.shapes.size(); ++i) {
        const PptxShape& shape = slide.shapes[i];
        if (!IsPlaceholder(shape) || HasText(shape) || !TakesText(shape) || editor_.IsSelected(static_cast<int>(i))) continue;
        DrawFrame(ctx, shape, false);
    }
    ctx->SetLineDash(UCDashPattern());
    DrawSelection(ctx, slide);

    // Snapping's guide lines, across the slide.
    ctx->SetStrokePaint(kGuide);
    ctx->SetStrokeWidth(1.0f);
    for (int64_t x : guides_.verticalGuides) {
        const Point2Dd top = ToView(static_cast<double>(x), 0);
        ctx->DrawLine(Point2Dd(top.x, rect.y), Point2Dd(top.x, rect.y + rect.height));
    }
    for (int64_t y : guides_.horizontalGuides) {
        const Point2Dd left = ToView(0, static_cast<double>(y));
        ctx->DrawLine(Point2Dd(rect.x, left.y), Point2Dd(rect.x + rect.width, left.y));
    }
    if (drag_ == Drag::Band && dragStarted_) {
        const Point2Dd a = ToView(static_cast<double>(bandBox_.x), static_cast<double>(bandBox_.y));
        const Point2Dd z = ToView(static_cast<double>(bandBox_.Right()), static_cast<double>(bandBox_.Bottom()));
        ctx->SetFillPaint(Color(kAccent.r, kAccent.g, kAccent.b, 30));
        ctx->FillRectangle(Rect2Dd(a.x, a.y, z.x - a.x, z.y - a.y));
        ctx->SetStrokePaint(kAccent);
        ctx->DrawRectangle(Rect2Dd(a.x, a.y, z.x - a.x, z.y - a.y));
    }
    ctx->PopState();
}

// The shape's frame, turned with it, in view pixels.
void UltraCanvasSlideEdit::DrawFrame(IRenderContext* ctx, const PptxShape& shape, bool handles) {
    const double x = static_cast<double>(shape.x), y = static_cast<double>(shape.y);
    const double w = static_cast<double>(shape.width), h = static_cast<double>(shape.height);
    const Point2Dd centre(x + w / 2, y + h / 2);
    std::vector<Point2Dd> corners;
    for (const Point2Dd& p : {Point2Dd(x, y), Point2Dd(x + w, y), Point2Dd(x + w, y + h), Point2Dd(x, y + h)}) {
        const Point2Dd turned = Turn(p, centre, shape.rotationDeg);
        corners.push_back(ToView(turned.x, turned.y));
    }
    ctx->MoveTo(corners[0].x, corners[0].y);
    for (size_t i = 1; i < 4; ++i) ctx->LineTo(corners[i].x, corners[i].y);
    ctx->ClosePath();
    ctx->Stroke();
    if (handles) DrawHandles(ctx, corners, true);
}

void UltraCanvasSlideEdit::DrawHandles(IRenderContext* ctx, const std::vector<Point2Dd>& corners, bool rotate) {
    // Corners, then the middles of the sides; the turning handle above the top's middle.
    std::vector<Point2Dd> points = corners;
    for (size_t i = 0; i < 4; ++i) {
        const Point2Dd& a = corners[i];
        const Point2Dd& b = corners[(i + 1) % 4];
        points.emplace_back((a.x + b.x) / 2, (a.y + b.y) / 2);
    }
    ctx->SetStrokeWidth(1.0f);
    for (const Point2Dd& p : points) {
        const Rect2Dd box(p.x - kHandleSize / 2, p.y - kHandleSize / 2, kHandleSize, kHandleSize);
        ctx->SetFillPaint(Colors::White);
        ctx->FillRectangle(box);
        ctx->SetStrokePaint(kAccent);
        ctx->DrawRectangle(box);
    }
    if (!rotate) return;
    const Point2Dd topMid((corners[0].x + corners[1].x) / 2, (corners[0].y + corners[1].y) / 2);
    const Point2Dd bottomMid((corners[2].x + corners[3].x) / 2, (corners[2].y + corners[3].y) / 2);
    const double len = std::hypot(topMid.x - bottomMid.x, topMid.y - bottomMid.y);
    const double ux = len > 0 ? (topMid.x - bottomMid.x) / len : 0, uy = len > 0 ? (topMid.y - bottomMid.y) / len : -1;
    const Point2Dd knob(topMid.x + ux * UCSlideDeckEditor::kRotateHandleOffset, topMid.y + uy * UCSlideDeckEditor::kRotateHandleOffset);
    ctx->SetStrokePaint(kAccent);
    ctx->DrawLine(topMid, knob);
    ctx->SetFillPaint(Colors::White);
    ctx->FillCircle(knob, kHandleSize / 2 + 1);
    ctx->DrawCircle(knob, kHandleSize / 2 + 1);
}

void UltraCanvasSlideEdit::DrawSelection(IRenderContext* ctx, const PptxSlide& slide) {
    const auto& selection = editor_.GetSelection();
    if (selection.empty()) return;
    ctx->SetStrokePaint(kAccent);
    ctx->SetStrokeWidth(1.5f);
    if (selection.size() == 1) {
        const int i = selection.front();
        if (i < static_cast<int>(slide.shapes.size())) DrawFrame(ctx, slide.shapes[static_cast<size_t>(i)], true);
        return;
    }
    // Several: each one's frame, and the box round them all with its handles.
    int64_t left = INT64_MAX, top = INT64_MAX, right = INT64_MIN, bottom = INT64_MIN;
    for (int i : selection) {
        if (i >= static_cast<int>(slide.shapes.size())) continue;
        const PptxShape& shape = slide.shapes[static_cast<size_t>(i)];
        DrawFrame(ctx, shape, false);
        left = std::min(left, shape.x);
        top = std::min(top, shape.y);
        right = std::max(right, shape.x + shape.width);
        bottom = std::max(bottom, shape.y + shape.height);
    }
    const Point2Dd a = ToView(static_cast<double>(left), static_cast<double>(top));
    const Point2Dd z = ToView(static_cast<double>(right), static_cast<double>(bottom));
    ctx->SetLineDash(UCDashPattern({3, 3}));
    ctx->DrawRectangle(Rect2Dd(a.x, a.y, z.x - a.x, z.y - a.y));
    ctx->SetLineDash(UCDashPattern());
    DrawHandles(ctx, {a, Point2Dd(z.x, a.y), z, Point2Dd(a.x, z.y)}, false);
}

// ===== EVENTS =====

bool UltraCanvasSlideEdit::OnEvent(const UCEvent& event) {
    if (showing_) return OnShowEvent(event);
    switch (event.type) {
        case UCEventType::MouseDown: return OnPress(event);
        case UCEventType::MouseMove: return OnMove(event);
        case UCEventType::MouseUp: return OnRelease(event);
        case UCEventType::MouseDoubleClick: {
            const Point2Df p(static_cast<float>(event.pointer.x), static_cast<float>(event.pointer.y));
            if (!Contains(p) || p.x < StripRect().width) return false;
            const Point2Dd at = ToSlide(p);
            const int shape = editor_.ShapeAt(static_cast<int64_t>(at.x), static_cast<int64_t>(at.y));
            int row = -1, col = -1;
            if (shape >= 0 && editor_.TableCellAt(shape, static_cast<int64_t>(at.x), static_cast<int64_t>(at.y), row, col)) {
                editor_.Select({shape});
                if (onEditTableCell) onEditTableCell(shape, row, col);
            } else if (shape >= 0) {
                EditTextOf(shape);
            }
            return true;
        }
        case UCEventType::MouseWheel: {
            const Point2Df p(static_cast<float>(event.pointer.x), static_cast<float>(event.pointer.y));
            if (!Contains(p)) return false;
            if (p.x < StripRect().width) {
                const double total = editor_.GetSlideCount() * (ThumbHeight() + kStripGap) + 2 * kStripTop;
                stripScroll_ = std::clamp(stripScroll_ - event.wheelDelta * 40.0, 0.0,
                                          std::max(0.0, total - StripRect().height));
            } else if (event.ctrl) {
                SetZoom(zoom_ * (event.wheelDelta > 0 ? 1.15 : 1.0 / 1.15));
            } else if (zoom_ > 1.0) {
                if (event.shift) panX_ -= event.wheelDelta * 40.0;
                else panY_ -= event.wheelDelta * 40.0;
                ClampPan();
            } else {
                GoToSlide(editor_.GetCurrentSlide() + (event.wheelDelta > 0 ? -1 : 1));
            }
            RequestRedraw();
            return true;
        }
        case UCEventType::KeyDown: return OnKey(event);   // keys come only to the focused element
        default: return false;
    }
}

bool UltraCanvasSlideEdit::OnPress(const UCEvent& event) {
    const Point2Df p(static_cast<float>(event.pointer.x), static_cast<float>(event.pointer.y));
    if (!Contains(p)) return false;
    SetFocus(true);
    pressAt_ = p;
    dragStarted_ = false;
    guides_ = SlideSnap{};

    if (p.x < StripRect().width) {   // the strip
        stripActive_ = true;
        const int slide = StripSlideAt(p);
        if (event.button == UCMouseButton::Right) {
            if (slide >= 0) GoToSlide(slide);
            if (onContextMenu) onContextMenu(true, slide, -1, event.pointerWindow.x, event.pointerWindow.y);
            return true;
        }
        if (slide >= 0) {
            GoToSlide(slide);
            pressedStripSlide_ = slide;
            drag_ = Drag::StripSlide;
            if (auto* app = UltraCanvasApplication::GetInstance()) app->CaptureMouse(this);
        }
        return true;
    }

    stripActive_ = false;
    if (!editor_.CurrentSlide()) return true;
    const Point2Dd at = ToSlide(p);
    const int64_t ax = static_cast<int64_t>(at.x), ay = static_cast<int64_t>(at.y);
    if (event.button == UCMouseButton::Right) {
        const int shape = editor_.ShapeAt(ax, ay);
        if (shape >= 0 && !editor_.IsSelected(shape)) editor_.Select({shape});
        if (shape < 0) editor_.ClearSelection();
        if (onContextMenu) onContextMenu(false, editor_.GetCurrentSlide(), shape, event.pointerWindow.x, event.pointerWindow.y);
        return true;
    }
    if (event.button != UCMouseButton::Left) return true;

    handle_ = editor_.HandleAt(ax, ay, EmuPerPixel());
    if (handle_ != SlideHandle::NoHandle) {
        drag_ = handle_ == SlideHandle::Rotate ? Drag::Rotate : Drag::Resize;
    } else {
        const int shape = editor_.ShapeAt(ax, ay);
        if (shape < 0) {
            if (!event.shift && !event.ctrl) editor_.ClearSelection();
            drag_ = Drag::Band;
            bandBox_ = SlideBox{ax, ay, 0, 0};
        } else {
            if (event.shift || event.ctrl) editor_.ToggleSelected(shape);
            else if (!editor_.IsSelected(shape)) editor_.Select({shape});
            drag_ = editor_.IsSelected(shape) ? Drag::Move : Drag::NoDrag;
        }
    }
    if (drag_ == Drag::Move || drag_ == Drag::Resize || drag_ == Drag::Rotate) {
        const auto& selection = editor_.GetSelection();
        if (selection.size() == 1) {
            const PptxShape& shape = editor_.CurrentSlide()->shapes[static_cast<size_t>(selection.front())];
            startBox_ = SlideBox{shape.x, shape.y, shape.width, shape.height};
            startRotation_ = shape.rotationDeg;
        } else {
            startBox_ = editor_.SelectionBox();
            startRotation_ = 0;
        }
    }
    if (drag_ != Drag::NoDrag)
        if (auto* app = UltraCanvasApplication::GetInstance()) app->CaptureMouse(this);
    RequestRedraw();
    return true;
}

bool UltraCanvasSlideEdit::OnMove(const UCEvent& event) {
    const Point2Df p(static_cast<float>(event.pointer.x), static_cast<float>(event.pointer.y));
    if (drag_ == Drag::NoDrag) {
        if (Contains(p)) UpdateCursor(p);
        return false;
    }
    if (!dragStarted_) {
        if (std::hypot(p.x - pressAt_.x, p.y - pressAt_.y) < kDragThreshold) return true;
        dragStarted_ = true;
    }
    if (drag_ == Drag::StripSlide) {
        stripDropIndex_ = StripDropAt(p);
        RequestRedraw();
        return true;
    }
    UpdatePreview(p, event.shift, event.alt);
    RequestRedraw();
    return true;
}

void UltraCanvasSlideEdit::UpdatePreview(const Point2Df& p, bool shift, bool alt) {
    const PptxSlide* current = editor_.CurrentSlide();
    if (!current) return;
    const Point2Dd from = ToSlide(pressAt_), to = ToSlide(p);
    const double emu = EmuPerPixel();
    const int64_t tolerance = static_cast<int64_t>(6 * emu);
    const auto& selection = editor_.GetSelection();
    guides_ = SlideSnap{};

    if (drag_ == Drag::Band) {
        bandBox_.x = static_cast<int64_t>(std::min(from.x, to.x));
        bandBox_.y = static_cast<int64_t>(std::min(from.y, to.y));
        bandBox_.width = static_cast<int64_t>(std::fabs(to.x - from.x));
        bandBox_.height = static_cast<int64_t>(std::fabs(to.y - from.y));
        return;
    }
    preview_ = std::make_unique<PptxSlide>(*current);

    if (drag_ == Drag::Move) {
        int64_t dx = static_cast<int64_t>(to.x - from.x), dy = static_cast<int64_t>(to.y - from.y);
        if (shift) (std::llabs(dx) > std::llabs(dy) ? dy : dx) = 0;
        if (snapping_ && !alt) {
            SlideBox moved = startBox_;
            if (selection.size() > 1) moved = editor_.SelectionBox();
            moved.x += dx;
            moved.y += dy;
            guides_ = editor_.Snap(moved, tolerance, selection, true);
            dx += guides_.box.x - moved.x;
            dy += guides_.box.y - moved.y;
        }
        for (int i : selection) {
            preview_->shapes[static_cast<size_t>(i)].x += dx;
            preview_->shapes[static_cast<size_t>(i)].y += dy;
        }
        return;
    }

    if (drag_ == Drag::Rotate && selection.size() == 1) {
        const double cx = static_cast<double>(startBox_.x) + static_cast<double>(startBox_.width) / 2;
        const double cy = static_cast<double>(startBox_.y) + static_cast<double>(startBox_.height) / 2;
        double angle = std::atan2(to.y - cy, to.x - cx) * 180.0 / kPi + 90.0;
        if (shift) angle = std::round(angle / 15.0) * 15.0;
        else
            for (double right : {0.0, 90.0, 180.0, 270.0, 360.0, -90.0})
                if (std::fabs(angle - right) < 3.0) angle = right;
        angle = std::fmod(angle + 360.0, 360.0);
        preview_->shapes[static_cast<size_t>(selection.front())].rotationDeg = angle;
        return;
    }

    if (drag_ != Drag::Resize) return;
    const bool left = handle_ == SlideHandle::Left || handle_ == SlideHandle::TopLeft || handle_ == SlideHandle::BottomLeft;
    const bool right = handle_ == SlideHandle::Right || handle_ == SlideHandle::TopRight || handle_ == SlideHandle::BottomRight;
    const bool top = handle_ == SlideHandle::Top || handle_ == SlideHandle::TopLeft || handle_ == SlideHandle::TopRight;
    const bool bottom = handle_ == SlideHandle::Bottom || handle_ == SlideHandle::BottomLeft || handle_ == SlideHandle::BottomRight;
    const double rotation = selection.size() == 1 ? startRotation_ : 0.0;
    const Point2Dd centre(static_cast<double>(startBox_.x) + static_cast<double>(startBox_.width) / 2,
                          static_cast<double>(startBox_.y) + static_cast<double>(startBox_.height) / 2);
    // The pointer in the shape's own, unturned frame.
    const Point2Dd local = Turn(to, centre, -rotation);
    double l = static_cast<double>(startBox_.x), t = static_cast<double>(startBox_.y);
    double r = l + static_cast<double>(startBox_.width), b = t + static_cast<double>(startBox_.height);
    const double minSize = 4 * emu;
    if (alt) {   // from the centre: both sides move
        if (left || right) { const double half = std::max(minSize / 2, std::fabs(local.x - centre.x)); l = centre.x - half; r = centre.x + half; }
        if (top || bottom) { const double half = std::max(minSize / 2, std::fabs(local.y - centre.y)); t = centre.y - half; b = centre.y + half; }
    } else {
        if (left) l = std::min(local.x, r - minSize);
        if (right) r = std::max(local.x, l + minSize);
        if (top) t = std::min(local.y, b - minSize);
        if (bottom) b = std::max(local.y, t + minSize);
    }
    const bool corner = (left || right) && (top || bottom);
    if (shift && corner && startBox_.width > 0 && startBox_.height > 0) {
        // The proportions kept: the side that moved further leads.
        const double aspect = static_cast<double>(startBox_.width) / static_cast<double>(startBox_.height);
        const double w = r - l, h = b - t;
        if (w / aspect > h) { const double nh = w / aspect; if (top && !alt) t = b - nh; else if (alt) { t = centre.y - nh / 2; b = centre.y + nh / 2; } else b = t + nh; }
        else { const double nw = h * aspect; if (left && !alt) l = r - nw; else if (alt) { l = centre.x - nw / 2; r = centre.x + nw / 2; } else r = l + nw; }
    }
    SlideBox box{static_cast<int64_t>(l), static_cast<int64_t>(t), static_cast<int64_t>(r - l), static_cast<int64_t>(b - t)};
    if (snapping_ && !alt && !shift && std::fabs(rotation) < 0.01) {
        guides_ = editor_.Snap(box, tolerance, selection, false, handle_);
        box = guides_.box;
    }
    if (std::fabs(rotation) > 0.01 && !alt) {
        // The side or corner opposite the handle stays where it is on the
        // slide: the box turns about its new centre, so it is moved back.
        const Point2Dd anchorLocal(left ? r : (right ? l : (l + r) / 2), top ? b : (bottom ? t : (t + b) / 2));
        const Point2Dd before = Turn(anchorLocal, centre, rotation);
        const Point2Dd newCentre(static_cast<double>(box.x) + static_cast<double>(box.width) / 2,
                                 static_cast<double>(box.y) + static_cast<double>(box.height) / 2);
        const Point2Dd after = Turn(anchorLocal, newCentre, rotation);
        box.x += static_cast<int64_t>(before.x - after.x);
        box.y += static_cast<int64_t>(before.y - after.y);
    }
    if (selection.size() == 1) {
        PptxShape& shape = preview_->shapes[static_cast<size_t>(selection.front())];
        shape.x = box.x; shape.y = box.y; shape.width = box.width; shape.height = box.height;
        return;
    }
    const SlideBox from0 = startBox_;
    const double sx = from0.width > 0 ? static_cast<double>(box.width) / static_cast<double>(from0.width) : 1.0;
    const double sy = from0.height > 0 ? static_cast<double>(box.height) / static_cast<double>(from0.height) : 1.0;
    for (int i : selection) {
        PptxShape& shape = preview_->shapes[static_cast<size_t>(i)];
        const PptxShape& was = current->shapes[static_cast<size_t>(i)];
        shape.x = box.x + static_cast<int64_t>(static_cast<double>(was.x - from0.x) * sx);
        shape.y = box.y + static_cast<int64_t>(static_cast<double>(was.y - from0.y) * sy);
        shape.width = static_cast<int64_t>(static_cast<double>(was.width) * sx);
        shape.height = static_cast<int64_t>(static_cast<double>(was.height) * sy);
    }
}

bool UltraCanvasSlideEdit::OnRelease(const UCEvent& event) {
    if (drag_ == Drag::NoDrag) return false;
    if (auto* app = UltraCanvasApplication::GetInstance()) app->ReleaseMouse();
    const Drag drag = drag_;
    drag_ = Drag::NoDrag;
    const bool moved = dragStarted_;
    dragStarted_ = false;
    guides_ = SlideSnap{};
    std::unique_ptr<PptxSlide> preview = std::move(preview_);

    if (drag == Drag::StripSlide) {
        if (moved && stripDropIndex_ >= 0 && pressedStripSlide_ >= 0) {
            int to = stripDropIndex_;
            if (to > pressedStripSlide_) --to;   // the gap counted with the slide still in place
            editor_.MoveSlide(pressedStripSlide_, to);
            EnsureThumbVisible(editor_.GetCurrentSlide());
        }
        stripDropIndex_ = -1;
        pressedStripSlide_ = -1;
        RequestRedraw();
        return true;
    }
    if (drag == Drag::Band) {
        if (moved) {
            std::vector<int> picked = editor_.ShapesInside(bandBox_);
            if (event.shift || event.ctrl) {
                picked.insert(picked.end(), editor_.GetSelection().begin(), editor_.GetSelection().end());
            }
            editor_.Select(picked);
        }
        RequestRedraw();
        return true;
    }
    const PptxSlide* current = editor_.CurrentSlide();
    if (!moved || !preview || !current) { RequestRedraw(); return true; }
    const auto selection = editor_.GetSelection();
    // The drag becomes one command, from what the preview shows.
    if (drag == Drag::Move && !selection.empty()) {
        const PptxShape& a = current->shapes[static_cast<size_t>(selection.front())];
        const PptxShape& b = preview->shapes[static_cast<size_t>(selection.front())];
        editor_.MoveSelection(b.x - a.x, b.y - a.y);
    } else if (drag == Drag::Rotate && selection.size() == 1) {
        editor_.SetShapeRotation(selection.front(), preview->shapes[static_cast<size_t>(selection.front())].rotationDeg);
    } else if (drag == Drag::Resize) {
        if (selection.size() == 1) {
            const PptxShape& b = preview->shapes[static_cast<size_t>(selection.front())];
            editor_.SetShapeBox(selection.front(), SlideBox{b.x, b.y, b.width, b.height});
        } else {
            int64_t left = INT64_MAX, top = INT64_MAX, right = INT64_MIN, bottom = INT64_MIN;
            for (int i : selection) {
                const PptxShape& s = preview->shapes[static_cast<size_t>(i)];
                left = std::min(left, s.x); top = std::min(top, s.y);
                right = std::max(right, s.x + s.width); bottom = std::max(bottom, s.y + s.height);
            }
            editor_.SetSelectionBox(startBox_, SlideBox{left, top, right - left, bottom - top});
        }
    }
    RequestRedraw();
    return true;
}

void UltraCanvasSlideEdit::UpdateCursor(const Point2Df& p) {
    if (p.x < StripRect().width || !editor_.CurrentSlide()) { SetMouseCursor(UCMouseCursor::Arrow); return; }
    const Point2Dd at = ToSlide(p);
    const SlideHandle handle = editor_.HandleAt(static_cast<int64_t>(at.x), static_cast<int64_t>(at.y), EmuPerPixel());
    if (handle != SlideHandle::NoHandle) { SetMouseCursor(CursorFor(handle)); return; }
    SetMouseCursor(editor_.ShapeAt(static_cast<int64_t>(at.x), static_cast<int64_t>(at.y)) >= 0 ? UCMouseCursor::SizeAll
                                                                                                 : UCMouseCursor::Arrow);
}

void UltraCanvasSlideEdit::EditTextOf(int shape) {
    const PptxSlide* slide = editor_.CurrentSlide();
    if (!slide || shape < 0 || shape >= static_cast<int>(slide->shapes.size())) return;
    if (!TakesText(slide->shapes[static_cast<size_t>(shape)])) return;
    editor_.Select({shape});
    if (onEditText) onEditText(shape);
}

bool UltraCanvasSlideEdit::OnKey(const UCEvent& event) {
    const bool ctrl = event.ctrl && !event.alt;
    const int slide = editor_.GetCurrentSlide();
    // Undo and redo, the slide before and after, a new slide: anywhere.
    if (ctrl && event.virtualKey == UCKeys::Z) { if (event.shift) editor_.Redo(); else editor_.Undo(); return true; }
    if (ctrl && event.virtualKey == UCKeys::Y) { editor_.Redo(); return true; }
    if (ctrl && event.virtualKey == UCKeys::M) { editor_.NewSlide(-1); EnsureThumbVisible(editor_.GetCurrentSlide()); return true; }
    if (event.virtualKey == UCKeys::PageUp) { GoToSlide(slide - 1); return true; }
    if (event.virtualKey == UCKeys::PageDown) { GoToSlide(slide + 1); return true; }

    if (stripActive_) {
        switch (event.virtualKey) {
            case UCKeys::Up: GoToSlide(slide - 1); return true;
            case UCKeys::Down: GoToSlide(slide + 1); return true;
            case UCKeys::Delete: case UCKeys::Backspace: editor_.DeleteSlide(slide); return true;
            case UCKeys::D: if (ctrl) { editor_.DuplicateSlide(slide); return true; } break;
            default: break;
        }
        return false;
    }

    const int64_t step = ctrl ? kFineNudge : kNudge;
    switch (event.virtualKey) {
        case UCKeys::Left: editor_.NudgeSelection(-step, 0); return true;
        case UCKeys::Right: editor_.NudgeSelection(step, 0); return true;
        case UCKeys::Up: editor_.NudgeSelection(0, -step); return true;
        case UCKeys::Down: editor_.NudgeSelection(0, step); return true;
        case UCKeys::Delete: case UCKeys::Backspace: editor_.DeleteSelection(); return true;
        case UCKeys::Escape: editor_.ClearSelection(); return true;
        case UCKeys::Tab: {
            const PptxSlide* current = editor_.CurrentSlide();
            if (!current || current->shapes.empty()) return true;
            const int count = static_cast<int>(current->shapes.size());
            const int at = editor_.GetSelection().empty() ? (event.shift ? count : -1) : editor_.GetSelection().front();
            editor_.Select({(at + (event.shift ? -1 : 1) + count) % count});
            return true;
        }
        case UCKeys::Return: case UCKeys::F2:
            if (editor_.GetSelection().size() == 1) EditTextOf(editor_.GetSelection().front());
            return true;
        case UCKeys::A: if (ctrl) { editor_.SelectAll(); return true; } break;
        case UCKeys::D: if (ctrl) { editor_.DuplicateSelection(); return true; } break;
        case UCKeys::C: if (ctrl) { editor_.CopySelection(); return true; } break;
        case UCKeys::X: if (ctrl) { editor_.CutSelection(); return true; } break;
        case UCKeys::V: if (ctrl) { editor_.Paste(); return true; } break;
        default: break;
    }
    return false;
}

} // namespace UltraCanvas
