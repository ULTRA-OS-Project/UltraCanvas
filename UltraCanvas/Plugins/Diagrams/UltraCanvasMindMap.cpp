// Plugins/Diagrams/UltraCanvasMindMap.cpp
// Interactive mind map element
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasMindMap.h"
#include "DataFormats/UltraCanvasJSON.h"
#include "Plugins/Diagrams/UltraCanvasMindMapIO.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace UltraCanvas {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDragThreshold = 4.0;   // Pixels before a press becomes a drag

Color Lighten(const Color& color, double amount) {
    amount = std::clamp(amount, 0.0, 1.0);
    auto mix = [&](int channel) {
        return static_cast<int>(std::round(channel + (255 - channel) * amount));
    };
    return Color(mix(color.r), mix(color.g), mix(color.b), color.a);
}

Color WithAlpha(const Color& color, int alpha) {
    return Color(color.r, color.g, color.b, alpha);
}

// Perceived brightness, used to pick a readable text colour over a fill.
double Luminance(const Color& color) {
    return (0.299 * color.r + 0.587 * color.g + 0.114 * color.b) / 255.0;
}

std::string TrimRight(const std::string& text) {
    size_t end = text.find_last_not_of(" \t\r\n");
    return end == std::string::npos ? std::string() : text.substr(0, end + 1);
}

} // namespace

// =============================================================================
// CONSTRUCTION
// =============================================================================

UltraCanvasMindMap::UltraCanvasMindMap(const std::string& id,
                                       int x, int y, int width, int height)
    : UltraCanvasUIElement(id, x, y, width, height) {
    viewport.SetViewportSize(width, height);
    viewport.SetZoomRange(0.15, 4.0);
    ApplyTheme(MindMapTheme::Default);
}

UltraCanvasMindMap::~UltraCanvasMindMap() = default;

// =============================================================================
// MODEL
// =============================================================================

std::string UltraCanvasMindMap::SetCentralTopic(const std::string& text) {
    PushUndo("Set central topic");
    std::string id = model.SetCentralTopic(text);
    MarkLayoutDirty();
    RequestRedraw();
    return id;
}

std::string UltraCanvasMindMap::AddTopic(const std::string& parentId, const std::string& text) {
    PushUndo("Add topic");
    std::string id = model.AddTopic(parentId, text);
    if (!id.empty()) {
        MarkLayoutDirty();
        if (onTopicAdded) onTopicAdded(id);
        RequestRedraw();
    }
    return id;
}

bool UltraCanvasMindMap::AddTopic(const MindMapTopic& topic) {
    PushUndo("Add topic");
    if (!model.AddTopic(topic)) return false;
    MarkLayoutDirty();
    if (onTopicAdded) onTopicAdded(topic.id);
    RequestRedraw();
    return true;
}

std::string UltraCanvasMindMap::AddFloatingTopic(const std::string& text, double x, double y) {
    PushUndo("Add floating topic");
    std::string id = model.AddFloatingTopic(text, x, y);
    MarkLayoutDirty();
    if (onTopicAdded) onTopicAdded(id);
    RequestRedraw();
    return id;
}

void UltraCanvasMindMap::RemoveTopic(const std::string& id) {
    if (!model.GetTopic(id)) return;
    if (editingTopicId == id) CancelEdit();

    PushUndo("Remove topic");
    std::vector<std::string> removed = model.RemoveTopic(id);
    for (const auto& removedId : removed) {
        selectedTopics.erase(removedId);
        if (primarySelection == removedId) primarySelection.clear();
        if (onTopicRemoved) onTopicRemoved(removedId);
    }
    MarkLayoutDirty();
    NotifySelectionChanged();
    RequestRedraw();
}

bool UltraCanvasMindMap::MoveTopic(const std::string& id, const std::string& newParentId,
                                   int index) {
    PushUndo("Move topic");
    if (!model.MoveTopic(id, newParentId, index)) {
        if (!undoStack.empty()) undoStack.pop_back();   // Nothing changed
        return false;
    }
    MarkLayoutDirty();
    if (onTopicMoved) onTopicMoved(id, newParentId);
    RequestRedraw();
    return true;
}

void UltraCanvasMindMap::SetTopicText(const std::string& id, const std::string& text) {
    MindMapTopic* topic = model.GetTopic(id);
    if (!topic || topic->text == text) return;
    PushUndo("Edit text");
    std::string previous = topic->text;
    topic->text = text;
    MarkLayoutDirty();
    if (onTopicTextChanged) onTopicTextChanged(id, previous);
    RequestRedraw();
}

void UltraCanvasMindMap::BuildFromOutline(const std::vector<std::pair<int, std::string>>& outline) {
    PushUndo("Build from outline");
    model.BuildFromOutline(outline);
    MarkLayoutDirty();
    pendingAutoFit = true;
    RequestRedraw();
}

void UltraCanvasMindMap::Clear() {
    PushUndo("Clear");
    CancelEdit();
    model.Clear();
    selectedTopics.clear();
    primarySelection.clear();
    hoveredTopicId.clear();
    MarkLayoutDirty();
    RequestRedraw();
}

MindMapTopic* UltraCanvasMindMap::GetTopic(const std::string& id) { return model.GetTopic(id); }
const MindMapTopic* UltraCanvasMindMap::GetTopic(const std::string& id) const {
    return model.GetTopic(id);
}

bool UltraCanvasMindMap::AddRelationship(const std::string& id, const std::string& fromId,
                                         const std::string& toId, const std::string& label) {
    MindMapRelationship relationship(id, fromId, toId);
    relationship.label = label;
    return AddRelationship(relationship);
}

bool UltraCanvasMindMap::AddRelationship(const MindMapRelationship& relationship) {
    PushUndo("Add relationship");
    if (!model.AddRelationship(relationship)) {
        if (!undoStack.empty()) undoStack.pop_back();
        return false;
    }
    RequestRedraw();
    return true;
}

bool UltraCanvasMindMap::RemoveRelationship(const std::string& id) {
    PushUndo("Remove relationship");
    if (!model.RemoveRelationship(id)) {
        if (!undoStack.empty()) undoStack.pop_back();
        return false;
    }
    RequestRedraw();
    return true;
}

void UltraCanvasMindMap::SetTopicMarkers(const std::string& id,
                                         const std::vector<std::string>& markers) {
    if (MindMapTopic* topic = model.GetTopic(id)) {
        topic->markers = markers;
        RequestRedraw();
    }
}

void UltraCanvasMindMap::AddTopicMarker(const std::string& id, const std::string& marker) {
    if (MindMapTopic* topic = model.GetTopic(id)) {
        topic->markers.push_back(marker);
        RequestRedraw();
    }
}

void UltraCanvasMindMap::ClearTopicMarkers(const std::string& id) {
    if (MindMapTopic* topic = model.GetTopic(id)) {
        topic->markers.clear();
        RequestRedraw();
    }
}

void UltraCanvasMindMap::SetTopicLink(const std::string& id, const std::string& link) {
    if (MindMapTopic* topic = model.GetTopic(id)) {
        topic->link = link;
        RequestRedraw();
    }
}

void UltraCanvasMindMap::SetTopicIcon(const std::string& id, const std::string& iconPath) {
    if (MindMapTopic* topic = model.GetTopic(id)) {
        topic->iconPath = iconPath;
        MarkLayoutDirty();
        RequestRedraw();
    }
}

void UltraCanvasMindMap::SetTopicImage(const std::string& id, const std::string& imagePath) {
    if (MindMapTopic* topic = model.GetTopic(id)) {
        topic->imagePath = imagePath;
        MarkLayoutDirty();
        RequestRedraw();
    }
}

void UltraCanvasMindMap::SetTopicNote(const std::string& id, const std::string& note) {
    if (MindMapTopic* topic = model.GetTopic(id)) {
        topic->note = note;
        RequestRedraw();
    }
}

void UltraCanvasMindMap::SetTopicWeight(const std::string& id, double weight) {
    if (MindMapTopic* topic = model.GetTopic(id)) {
        topic->weight = weight;
        MarkLayoutDirty();
        RequestRedraw();
    }
}

// =============================================================================
// LAYOUT
// =============================================================================

void UltraCanvasMindMap::SetStructure(MindMapStructure structure) {
    layoutOptions.structure = structure;
    MarkLayoutDirty();
    pendingAutoFit = autoFitOnLayout;
    RequestRedraw();
}

void UltraCanvasMindMap::SetBalancePolicy(MindMapBalancePolicy policy) {
    layoutOptions.balancePolicy = policy;
    MarkLayoutDirty();
    RequestRedraw();
}

void UltraCanvasMindMap::SetSpacing(double siblingGap, double levelGap, double rootGap) {
    layoutOptions.siblingGap = siblingGap;
    layoutOptions.levelGap = levelGap;
    layoutOptions.rootGap = rootGap;
    MarkLayoutDirty();
    RequestRedraw();
}

void UltraCanvasMindMap::SetLayoutOptions(const MindMapLayoutOptions& options) {
    layoutOptions = options;
    MarkLayoutDirty();
    RequestRedraw();
}

void UltraCanvasMindMap::SetTopicSide(const std::string& id, MindMapTopicSide side) {
    if (MindMapTopic* topic = model.GetTopic(id)) {
        topic->side = side;
        MarkLayoutDirty();
        RequestRedraw();
    }
}

Size2Dd UltraCanvasMindMap::MeasureText(IRenderContext* ctx, const std::string& text,
                                        double fontSize, bool bold, double maxWidth) const {
    if (!ctx) {
        return UltraCanvasMindMapLayout::EstimateTextSize(text, fontSize, bold, maxWidth);
    }
    FontStyle font;
    font.fontFamily = style.fontFamily;
    font.fontSize = fontSize;
    font.fontWeight = bold ? FontWeight::Bold : FontWeight::Normal;
    ctx->SetFontStyle(font);

    if (maxWidth > 0.0) {
        Size2Di wrapped = ctx->GetTextDimensions(text, Size2Di(static_cast<int>(maxWidth), 0));
        if (wrapped.width > 0 && wrapped.height > 0) {
            return Size2Dd(wrapped.width, wrapped.height);
        }
    }
    Size2Di measured = ctx->GetTextLineDimensions(text);
    if (measured.width <= 0 && !text.empty()) {
        return UltraCanvasMindMapLayout::EstimateTextSize(text, fontSize, bold, maxWidth);
    }
    return Size2Dd(measured.width, measured.height > 0 ? measured.height : fontSize * 1.35);
}

void UltraCanvasMindMap::EnsureLayout() {
    if (!layoutDirty) return;

    if (style.numbering != MindMapNumbering::Off) {
        model.AssignOrdinals();
    }

    IRenderContext* ctx = measureContext;
    MindMapMeasureFn measure =
        [this, ctx](const std::string& text, double fontSize, bool bold, double maxWidth) {
            return MeasureText(ctx, text, fontSize, bold, maxWidth);
        };
    MindMapStyleFn styleOf = [this](const MindMapTopic& topic) {
        return ResolveTopicStyle(topic);
    };

    UltraCanvasMindMapLayout::ComputeLayout(model, layoutOptions, measure, styleOf, layoutResult);
    layoutDirty = false;

    if (pendingAutoFit) {
        pendingAutoFit = false;
        SyncViewportSize();
        if (viewport.FitView(layoutResult.contentBounds, 50.0)) {
            NotifyViewportChanged();
        }
    }
}

void UltraCanvasMindMap::RunLayout() {
    MarkLayoutDirty();
    if (autoFitOnLayout) pendingAutoFit = true;
    EnsureLayout();
    RequestRedraw();
}

void UltraCanvasMindMap::SyncViewportSize() {
    viewport.SetViewportSize(GetWidth(), GetHeight());
}

DiagramContentBounds UltraCanvasMindMap::ComputeContentBounds() const {
    return layoutResult.contentBounds;
}

// =============================================================================
// STYLE
// =============================================================================

void UltraCanvasMindMap::SetStyle(const MindMapStyle& newStyle) {
    style = newStyle;
    MarkLayoutDirty();
    RequestRedraw();
}

void UltraCanvasMindMap::SetLevelStyle(int level, const MindMapTopicStyle& topicStyle) {
    if (level < 0) return;
    if (static_cast<int>(levelStyles.size()) <= level) {
        levelStyles.resize(level + 1, levelStyles.empty() ? MindMapTopicStyle()
                                                          : levelStyles.back());
    }
    levelStyles[level] = topicStyle;
    MarkLayoutDirty();
    RequestRedraw();
}

MindMapTopicStyle UltraCanvasMindMap::GetLevelStyle(int level) const {
    if (levelStyles.empty()) return MindMapTopicStyle();
    if (level < 0) level = 0;
    // The deepest configured level applies to everything beyond it.
    if (level >= static_cast<int>(levelStyles.size())) return levelStyles.back();
    return levelStyles[level];
}

void UltraCanvasMindMap::SetTopicStyle(const std::string& id, const MindMapTopicStyle& topicStyle) {
    if (MindMapTopic* topic = model.GetTopic(id)) {
        topic->styleOverride = topicStyle;
        MarkLayoutDirty();
        RequestRedraw();
    }
}

void UltraCanvasMindMap::ClearTopicStyle(const std::string& id) {
    if (MindMapTopic* topic = model.GetTopic(id)) {
        topic->styleOverride.reset();
        MarkLayoutDirty();
        RequestRedraw();
    }
}

void UltraCanvasMindMap::SetBranchPalette(const std::vector<Color>& palette) {
    branchPalette = palette;
    RequestRedraw();
}

Color UltraCanvasMindMap::GetBranchColor(int branchIndex) const {
    if (branchPalette.empty()) return Color(70, 110, 180, 255);
    if (branchIndex < 0) return branchPalette[0];
    return branchPalette[static_cast<size_t>(branchIndex) % branchPalette.size()];
}

void UltraCanvasMindMap::SetConnectorStyle(MindMapConnectorStyle connectorStyle) {
    style.connectorStyle = connectorStyle;
    RequestRedraw();
}

void UltraCanvasMindMap::SetNumbering(MindMapNumbering numbering) {
    style.numbering = numbering;
    MarkLayoutDirty();
    RequestRedraw();
}

void UltraCanvasMindMap::SetBackgroundColor(const Color& color) {
    style.backgroundColor = color;
    RequestRedraw();
}

void UltraCanvasMindMap::SetFontFamily(const std::string& family) {
    style.fontFamily = family;
    MarkLayoutDirty();
    RequestRedraw();
}

MindMapTopicStyle UltraCanvasMindMap::ResolveTopicStyle(const MindMapTopic& topic) const {
    // Per-topic override wins outright - no branch tinting on top of it.
    if (topic.styleOverride.has_value()) return topic.styleOverride.value();

    MindMapTopicStyle resolved = GetLevelStyle(topic.depth);

    // Branch hue cascade: everything under a main topic takes its colour.
    if (topic.depth >= 1 && topic.branchIndex >= 0 && !branchPalette.empty()) {
        Color hue = GetBranchColor(topic.branchIndex);
        if (style.depthLightenStep > 0.0 && topic.depth > 1) {
            hue = Lighten(hue, style.depthLightenStep * (topic.depth - 1));
        }
        resolved.borderColor = hue;
        if (resolved.outlineOnly) {
            resolved.fillColor = Color(255, 255, 255, 0);
            resolved.textColor = hue;
        } else {
            resolved.fillColor = hue;
            // Keep the label readable whichever way the hue went.
            resolved.textColor = (Luminance(hue) > 0.6) ? Color(35, 35, 45, 255)
                                                        : Color(255, 255, 255, 255);
        }
    }
    return resolved;
}

// =============================================================================
// THEMES
// =============================================================================

void UltraCanvasMindMap::SetTheme(MindMapTheme newTheme) {
    ApplyTheme(newTheme);
    MarkLayoutDirty();
    RequestRedraw();
}

void UltraCanvasMindMap::ApplyTheme(MindMapTheme newTheme) {
    theme = newTheme;
    style = MindMapStyle();
    levelStyles.clear();

    MindMapTopicStyle root, main, sub, leaf;

    // Shared skeleton: the centre is the largest and boldest, main topics sit
    // between, and leaves are small.
    root.shape = MindMapNodeShape::RoundedRect;
    root.fontSize = 17.0;
    root.bold = true;
    root.paddingX = 20.0;
    root.paddingY = 12.0;
    root.cornerRadius = 12.0;
    root.maxWidth = 240.0;

    main.shape = MindMapNodeShape::RoundedRect;
    main.fontSize = 13.0;
    main.bold = true;
    main.paddingX = 14.0;
    main.paddingY = 8.0;
    main.cornerRadius = 10.0;
    main.maxWidth = 200.0;

    sub = main;
    sub.fontSize = 12.0;
    sub.bold = false;
    sub.paddingX = 12.0;
    sub.paddingY = 6.0;
    sub.cornerRadius = 8.0;
    sub.borderWidth = 1.5;

    leaf = sub;
    leaf.fontSize = 11.5;
    leaf.borderWidth = 1.2;

    switch (newTheme) {
        case MindMapTheme::Professional:
            branchPalette = {
                Color( 38,  84, 124, 255), Color( 96, 125, 139, 255),
                Color( 84, 110,  92, 255), Color(124,  94,  66, 255),
                Color( 90,  78, 120, 255), Color( 60, 100, 110, 255),
            };
            style.backgroundColor = Color(250, 250, 251, 255);
            style.connectorStyle = MindMapConnectorStyle::RoundedElbow;
            root.fillColor = Color(38, 60, 84, 255);
            root.borderColor = Color(28, 45, 64, 255);
            root.textColor = Color(255, 255, 255, 255);
            break;

        case MindMapTheme::Colorful:
            branchPalette = {
                Color(233,  86,  86, 255), Color(247, 168,  40, 255),
                Color( 76, 175,  92, 255), Color( 45, 156, 219, 255),
                Color(155,  89, 182, 255), Color( 26, 188, 156, 255),
            };
            style.backgroundColor = Color(252, 252, 250, 255);
            style.connectorStyle = MindMapConnectorStyle::Curve;
            root.fillColor = Color(45, 52, 68, 255);
            root.borderColor = Color(30, 36, 50, 255);
            root.textColor = Color(255, 255, 255, 255);
            break;

        case MindMapTheme::Pastel:
            branchPalette = {
                Color(247, 183, 190, 255), Color(250, 218, 160, 255),
                Color(184, 224, 190, 255), Color(174, 208, 238, 255),
                Color(212, 190, 235, 255), Color(175, 226, 222, 255),
            };
            style.backgroundColor = Color(253, 252, 250, 255);
            style.connectorStyle = MindMapConnectorStyle::Curve;
            style.depthLightenStep = 0.18;
            root.fillColor = Color(255, 255, 255, 255);
            root.borderColor = Color(150, 150, 165, 255);
            root.textColor = Color(60, 60, 75, 255);
            break;

        case MindMapTheme::Dark:
            branchPalette = {
                Color( 88, 166, 255, 255), Color(255, 138,  96, 255),
                Color(126, 211, 133, 255), Color(214, 152, 255, 255),
                Color(255, 205,  92, 255), Color(100, 214, 208, 255),
            };
            style.backgroundColor = Color(28, 30, 36, 255);
            style.gridColor = Color(44, 46, 54, 255);
            style.connectorColor = Color(110, 114, 128, 255);
            style.collapseHandleFill = Color(48, 51, 60, 255);
            style.collapseHandleBorder = Color(100, 104, 118, 255);
            style.collapseHandleGlyph = Color(215, 218, 228, 255);
            root.fillColor = Color(52, 56, 68, 255);
            root.borderColor = Color(92, 98, 116, 255);
            root.textColor = Color(240, 242, 248, 255);
            main.textColor = Color(30, 32, 40, 255);
            sub.textColor = Color(30, 32, 40, 255);
            leaf.textColor = Color(30, 32, 40, 255);
            break;

        case MindMapTheme::HandDrawn:
            branchPalette = {
                Color(226, 116,  92, 255), Color(238, 180,  74, 255),
                Color(114, 172, 116, 255), Color( 84, 148, 200, 255),
                Color(160, 118, 190, 255), Color( 90, 186, 178, 255),
            };
            style.backgroundColor = Color(253, 251, 245, 255);
            style.connectorStyle = MindMapConnectorStyle::TaperedBranch;
            style.taperStartWidth = 7.0;
            style.taperEndWidth = 1.5;
            root.shape = MindMapNodeShape::Cloud;
            root.fillColor = Color(255, 255, 255, 255);
            root.borderColor = Color(70, 70, 80, 255);
            root.textColor = Color(50, 50, 60, 255);
            main.outlineOnly = true;
            sub.shape = MindMapNodeShape::Underline;
            leaf.shape = MindMapNodeShape::Underline;
            break;

        case MindMapTheme::Default:
        default:
            branchPalette = {
                Color( 70, 130, 200, 255), Color(226, 128,  76, 255),
                Color( 92, 172, 110, 255), Color(160, 110, 190, 255),
                Color(212, 168,  62, 255), Color( 78, 176, 172, 255),
            };
            style.backgroundColor = Color(252, 252, 253, 255);
            style.connectorStyle = MindMapConnectorStyle::Curve;
            root.fillColor = Color(255, 255, 255, 255);
            root.borderColor = Color(90, 96, 112, 255);
            root.textColor = Color(40, 42, 52, 255);
            break;
    }

    levelStyles = { root, main, sub, leaf };
}

// =============================================================================
// SELECTION
// =============================================================================

void UltraCanvasMindMap::SelectTopic(const std::string& id, bool addToSelection) {
    if (!model.GetTopic(id)) return;
    if (!addToSelection) selectedTopics.clear();
    selectedTopics.insert(id);
    primarySelection = id;
    NotifySelectionChanged();
    RequestRedraw();
}

void UltraCanvasMindMap::SelectSubtree(const std::string& id) {
    if (!model.GetTopic(id)) return;
    for (const auto& subtreeId : model.GetSubtreeIds(id)) {
        selectedTopics.insert(subtreeId);
    }
    primarySelection = id;
    NotifySelectionChanged();
    RequestRedraw();
}

void UltraCanvasMindMap::SelectAll() {
    selectedTopics.clear();
    for (const auto& [id, topic] : model.Topics()) {
        if (!model.IsHiddenByCollapse(id)) selectedTopics.insert(id);
    }
    NotifySelectionChanged();
    RequestRedraw();
}

void UltraCanvasMindMap::DeselectAll() {
    if (selectedTopics.empty() && primarySelection.empty()) return;
    selectedTopics.clear();
    primarySelection.clear();
    NotifySelectionChanged();
    RequestRedraw();
}

bool UltraCanvasMindMap::IsTopicSelected(const std::string& id) const {
    return selectedTopics.find(id) != selectedTopics.end();
}

std::vector<std::string> UltraCanvasMindMap::GetSelectedTopicIds() const {
    return std::vector<std::string>(selectedTopics.begin(), selectedTopics.end());
}

void UltraCanvasMindMap::NotifySelectionChanged() {
    if (onSelectionChanged) onSelectionChanged(GetSelectedTopicIds());
}

void UltraCanvasMindMap::NotifyViewportChanged() {
    if (onViewportChanged) {
        Point2Dd pan = viewport.GetPanOffset();
        onViewportChanged(viewport.GetZoomLevel(), pan.x, pan.y);
    }
}

// =============================================================================
// COLLAPSE & NAVIGATION
// =============================================================================

void UltraCanvasMindMap::SetCollapsed(const std::string& id, bool collapsed) {
    MindMapTopic* topic = model.GetTopic(id);
    if (!topic || topic->collapsed == collapsed) return;
    topic->collapsed = collapsed;
    MarkLayoutDirty();
    if (onCollapseChanged) onCollapseChanged(id, collapsed);
    RequestRedraw();
}

void UltraCanvasMindMap::ToggleCollapsed(const std::string& id) {
    if (const MindMapTopic* topic = model.GetTopic(id)) {
        SetCollapsed(id, !topic->collapsed);
    }
}

bool UltraCanvasMindMap::IsCollapsed(const std::string& id) const {
    const MindMapTopic* topic = model.GetTopic(id);
    return topic && topic->collapsed;
}

void UltraCanvasMindMap::ExpandAll() {
    for (auto& [id, topic] : model.Topics()) topic.collapsed = false;
    MarkLayoutDirty();
    RequestRedraw();
}

void UltraCanvasMindMap::CollapseAll() {
    // Collapse the main topics, not the centre - collapsing the root would
    // hide the whole map.
    const MindMapTopic* root = model.GetTopic(model.GetRootId());
    if (!root) return;
    for (const auto& branchId : root->childIds) {
        if (MindMapTopic* branch = model.GetTopic(branchId)) branch->collapsed = true;
    }
    MarkLayoutDirty();
    RequestRedraw();
}

void UltraCanvasMindMap::ExpandToLevel(int level) {
    model.ForEachTopic([&](MindMapTopic& topic) {
        int depth = model.GetDepth(topic.id);
        topic.collapsed = (depth >= level);
    });
    MarkLayoutDirty();
    RequestRedraw();
}

void UltraCanvasMindMap::CenterOnTopic(const std::string& id) {
    EnsureLayout();
    if (!layoutResult.Has(id)) return;
    Rect2Dd box = layoutResult.Get(id);
    SyncViewportSize();
    viewport.CenterOn(box.x + box.width * 0.5, box.y + box.height * 0.5);
    NotifyViewportChanged();
    RequestRedraw();
}

void UltraCanvasMindMap::FitView(double padding) {
    EnsureLayout();
    SyncViewportSize();
    if (viewport.FitView(layoutResult.contentBounds, padding)) {
        NotifyViewportChanged();
        RequestRedraw();
    }
}

void UltraCanvasMindMap::RevealTopic(const std::string& id) {
    if (!model.GetTopic(id)) return;
    // Expand every ancestor so the topic is actually laid out.
    for (const auto& ancestorId : model.GetPath(id)) {
        if (ancestorId == id) continue;
        if (MindMapTopic* ancestor = model.GetTopic(ancestorId)) {
            if (ancestor->collapsed) {
                ancestor->collapsed = false;
                MarkLayoutDirty();
            }
        }
    }
    EnsureLayout();
    CenterOnTopic(id);
}

std::vector<std::string> UltraCanvasMindMap::FindTopics(const std::string& text) const {
    std::vector<std::string> matches;
    if (text.empty()) return matches;

    std::string needle = text;
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    model.ForEachTopic([&](const MindMapTopic& topic) {
        std::string haystack = topic.text;
        std::transform(haystack.begin(), haystack.end(), haystack.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (haystack.find(needle) != std::string::npos) matches.push_back(topic.id);
    });
    return matches;
}

void UltraCanvasMindMap::SetZoomLevel(double zoom) {
    viewport.SetZoomLevel(zoom);
    NotifyViewportChanged();
    RequestRedraw();
}

void UltraCanvasMindMap::ZoomIn(double factor) {
    viewport.ZoomIn(factor);
    NotifyViewportChanged();
    RequestRedraw();
}

void UltraCanvasMindMap::ZoomOut(double factor) {
    viewport.ZoomOut(factor);
    NotifyViewportChanged();
    RequestRedraw();
}

void UltraCanvasMindMap::SetMinimapVisible(bool visible) {
    viewport.SetMinimapVisible(visible);
    RequestRedraw();
}

void UltraCanvasMindMap::SetControlsVisible(bool visible) {
    viewport.SetControlsVisible(visible);
    RequestRedraw();
}

// =============================================================================
// GEOMETRY
// =============================================================================

Point2Dd UltraCanvasMindMap::TopicCenter(const MindMapTopic& topic) const {
    return Point2Dd(topic.bounds.x + topic.bounds.width * 0.5,
                    topic.bounds.y + topic.bounds.height * 0.5);
}

Point2Dd UltraCanvasMindMap::PerimeterPoint(const MindMapTopic& topic,
                                            const MindMapTopicStyle& topicStyle,
                                            const Point2Dd& towards) const {
    Point2Dd center = TopicCenter(topic);
    double dx = towards.x - center.x;
    double dy = towards.y - center.y;
    if (std::abs(dx) < 1e-9 && std::abs(dy) < 1e-9) return center;

    double halfW = topic.bounds.width * 0.5;
    double halfH = topic.bounds.height * 0.5;

    switch (topicStyle.shape) {
        case MindMapNodeShape::Circle:
        case MindMapNodeShape::Ellipse: {
            // Parametric intersection with the ellipse.
            double denom = std::sqrt((dx * dx) / (halfW * halfW) + (dy * dy) / (halfH * halfH));
            if (denom < 1e-9) return center;
            return Point2Dd(center.x + dx / denom, center.y + dy / denom);
        }
        case MindMapNodeShape::Diamond: {
            // |x|/halfW + |y|/halfH = 1
            double denom = std::abs(dx) / halfW + std::abs(dy) / halfH;
            if (denom < 1e-9) return center;
            return Point2Dd(center.x + dx / denom, center.y + dy / denom);
        }
        default: {
            // Rectangle: scale the direction until it hits an edge.
            double scaleX = (std::abs(dx) > 1e-9) ? halfW / std::abs(dx) : 1e18;
            double scaleY = (std::abs(dy) > 1e-9) ? halfH / std::abs(dy) : 1e18;
            double scale = std::min(scaleX, scaleY);
            return Point2Dd(center.x + dx * scale, center.y + dy * scale);
        }
    }
}

Rect2Dd UltraCanvasMindMap::CollapseHandleRect(const MindMapTopic& topic) const {
    double r = style.collapseHandleRadius;
    double cy = topic.bounds.y + topic.bounds.height * 0.5;
    double cx = (topic.resolvedSide == MindMapTopicSide::Left)
        ? topic.bounds.x - r - 2.0
        : topic.bounds.x + topic.bounds.width + r + 2.0;

    if (layoutOptions.structure == MindMapStructure::OrgChartDown) {
        cx = topic.bounds.x + topic.bounds.width * 0.5;
        cy = topic.bounds.y + topic.bounds.height + r + 2.0;
    } else if (layoutOptions.structure == MindMapStructure::OrgChartUp) {
        cx = topic.bounds.x + topic.bounds.width * 0.5;
        cy = topic.bounds.y - r - 2.0;
    }
    return Rect2Dd(cx - r, cy - r, r * 2.0, r * 2.0);
}

bool UltraCanvasMindMap::PointInTopic(const MindMapTopic& topic, const Point2Dd& worldPos) const {
    const Rect2Dd& box = topic.bounds;
    if (worldPos.x < box.x || worldPos.x > box.x + box.width ||
        worldPos.y < box.y || worldPos.y > box.y + box.height) {
        return false;
    }
    MindMapTopicStyle topicStyle = ResolveTopicStyle(topic);
    double halfW = box.width * 0.5;
    double halfH = box.height * 0.5;
    double dx = worldPos.x - (box.x + halfW);
    double dy = worldPos.y - (box.y + halfH);

    switch (topicStyle.shape) {
        case MindMapNodeShape::Circle:
        case MindMapNodeShape::Ellipse:
            return (dx * dx) / (halfW * halfW) + (dy * dy) / (halfH * halfH) <= 1.0;
        case MindMapNodeShape::Diamond:
            return std::abs(dx) / halfW + std::abs(dy) / halfH <= 1.0;
        default:
            return true;
    }
}

std::string UltraCanvasMindMap::FindTopicAt(const Point2Di& localPos) const {
    Point2Dd world = viewport.ScreenToWorld(localPos);
    // Deepest topics first so children win over their parent's box.
    std::string best;
    int bestDepth = -1;
    for (const auto& [id, box] : layoutResult.bounds) {
        const MindMapTopic* topic = model.GetTopic(id);
        if (!topic) continue;
        if (PointInTopic(*topic, world) && topic->depth > bestDepth) {
            best = id;
            bestDepth = topic->depth;
        }
    }
    return best;
}

std::string UltraCanvasMindMap::FindCollapseHandleAt(const Point2Di& localPos) const {
    if (!style.showCollapseHandles) return std::string();
    Point2Dd world = viewport.ScreenToWorld(localPos);
    for (const auto& [id, box] : layoutResult.bounds) {
        const MindMapTopic* topic = model.GetTopic(id);
        if (!topic || topic->childIds.empty()) continue;
        Rect2Dd handle = CollapseHandleRect(*topic);
        double cx = handle.x + handle.width * 0.5;
        double cy = handle.y + handle.height * 0.5;
        double dx = world.x - cx;
        double dy = world.y - cy;
        double r = style.collapseHandleRadius;
        if (dx * dx + dy * dy <= r * r) return id;
    }
    return std::string();
}


// =============================================================================
// RENDERING
// =============================================================================

void UltraCanvasMindMap::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    (void)dirtyRect;
    if (!ctx || !IsVisible()) return;

    // Real text metrics only exist during Render(), so the layout pass is run
    // from here with the live context rather than from the mutating setters.
    measureContext = ctx;
    SyncViewportSize();
    EnsureLayout();

    ctx->SetFillPaint(style.backgroundColor);
    ctx->FillRectangle(GetLocalBounds());

    ctx->PushState();
    Point2Dd pan = viewport.GetPanOffset();
    ctx->Translate(pan.x, pan.y);
    ctx->Scale(viewport.GetZoomLevel(), viewport.GetZoomLevel());

    // Background layer sits beneath everything, in world space (S9).
    if (style.backgroundRenderer) {
        style.backgroundRenderer(ctx, viewport.GetVisibleWorldRect());
    }
    if (style.showGrid) RenderGrid(ctx);

    RenderConnectors(ctx);
    RenderRelationships(ctx);
    RenderTopics(ctx);
    if (isDraggingTopic) RenderDropIndicator(ctx);

    ctx->PopState();

    // Overlays live in screen space, after the transform is popped.
    if (viewport.IsMinimapVisible()) {
        viewport.RenderMinimap(ctx, layoutResult.contentBounds,
            [&](const DiagramMinimapProjection& projection) {
                for (const auto& [id, box] : layoutResult.bounds) {
                    Point2Dd tl = projection.WorldToMinimap(box.x, box.y);
                    double w = std::max(1.0, box.width * projection.scale);
                    double h = std::max(1.0, box.height * projection.scale);
                    ctx->FillRectangle(Rect2Dd(tl.x, tl.y, w, h));
                }
            });
    }
    if (viewport.AreControlsVisible()) {
        viewport.RenderControls(ctx, hoveredControlButton, !editable);
    }

    // The in-place editor sits above everything, in screen space.
    if (editOverlay && editOverlay->IsVisible()) {
        PositionEditOverlay();
        editOverlay->Render(ctx, dirtyRect);
    }
}

void UltraCanvasMindMap::RenderGrid(IRenderContext* ctx) {
    Rect2Dd visible = viewport.GetVisibleWorldRect();
    double spacing = style.gridSpacing;
    if (spacing <= 0.0) return;

    ctx->SetStrokePaint(style.gridColor);
    ctx->SetStrokeWidth(1.0 / viewport.GetZoomLevel());

    double startX = std::floor(visible.x / spacing) * spacing;
    double startY = std::floor(visible.y / spacing) * spacing;
    for (double x = startX; x <= visible.x + visible.width; x += spacing) {
        ctx->DrawLine({x, visible.y}, {x, visible.y + visible.height});
    }
    for (double y = startY; y <= visible.y + visible.height; y += spacing) {
        ctx->DrawLine({visible.x, y}, {visible.x + visible.width, y});
    }
}

void UltraCanvasMindMap::RenderConnectors(IRenderContext* ctx) {
    for (const auto& [id, box] : layoutResult.bounds) {
        const MindMapTopic* topic = model.GetTopic(id);
        if (!topic || topic->parentId.empty()) continue;
        const MindMapTopic* parent = model.GetTopic(topic->parentId);
        if (!parent || !layoutResult.Has(parent->id)) continue;
        RenderConnector(ctx, *parent, *topic);
    }
}

void UltraCanvasMindMap::RenderConnector(IRenderContext* ctx, const MindMapTopic& parent,
                                         const MindMapTopic& child) {
    MindMapTopicStyle parentStyle = ResolveTopicStyle(parent);
    MindMapTopicStyle childStyle = ResolveTopicStyle(child);

    Point2Dd parentCenter = TopicCenter(parent);
    Point2Dd childCenter = TopicCenter(child);
    Point2Dd from = PerimeterPoint(parent, parentStyle, childCenter);
    Point2Dd to = PerimeterPoint(child, childStyle, parentCenter);

    Color color;
    switch (style.connectorColorMode) {
        case MindMapConnectorColorMode::InheritFromChild:
            color = childStyle.borderColor;
            break;
        case MindMapConnectorColorMode::Fixed:
            color = style.connectorColor;
            break;
        case MindMapConnectorColorMode::InheritFromBranch:
        default:
            color = (child.branchIndex >= 0 && !branchPalette.empty())
                ? GetBranchColor(child.branchIndex)
                : style.connectorColor;
            break;
    }

    ctx->SetStrokePaint(color);
    ctx->SetStrokeWidth(style.connectorWidth);

    const bool horizontal = (layoutOptions.structure != MindMapStructure::OrgChartDown &&
                             layoutOptions.structure != MindMapStructure::OrgChartUp);

    switch (style.connectorStyle) {
        case MindMapConnectorStyle::Straight:
            ctx->DrawLine(from, to);
            break;

        case MindMapConnectorStyle::Curve: {
            // Control points pushed along the growth axis give the classic
            // S-curve that leaves both nodes perpendicular to their edge.
            double dx = to.x - from.x;
            double dy = to.y - from.y;
            Point2Dd c1 = horizontal ? Point2Dd(from.x + dx * 0.5, from.y)
                                     : Point2Dd(from.x, from.y + dy * 0.5);
            Point2Dd c2 = horizontal ? Point2Dd(to.x - dx * 0.5, to.y)
                                     : Point2Dd(to.x, to.y - dy * 0.5);
            ctx->DrawBezierCurve(from, c1, c2, to);
            break;
        }

        case MindMapConnectorStyle::Elbow: {
            std::vector<Point2Dd> path;
            if (horizontal) {
                double midX = (from.x + to.x) * 0.5;
                path = {from, {midX, from.y}, {midX, to.y}, to};
            } else {
                double midY = (from.y + to.y) * 0.5;
                path = {from, {from.x, midY}, {to.x, midY}, to};
            }
            ctx->DrawLinePath(path, false);
            break;
        }

        case MindMapConnectorStyle::RoundedElbow: {
            // Straight runs joined by a short bezier at the corner.
            double radius = std::min(12.0, std::min(std::abs(to.x - from.x),
                                                    std::abs(to.y - from.y)) * 0.5);
            if (horizontal) {
                double midX = (from.x + to.x) * 0.5;
                double signY = (to.y >= from.y) ? 1.0 : -1.0;
                double signX = (to.x >= from.x) ? 1.0 : -1.0;
                if (radius < 1.0) {
                    ctx->DrawLine(from, to);
                    break;
                }
                ctx->DrawLine(from, {midX - radius * signX, from.y});
                ctx->DrawBezierCurve({midX - radius * signX, from.y}, {midX, from.y},
                                     {midX, from.y}, {midX, from.y + radius * signY});
                ctx->DrawLine({midX, from.y + radius * signY}, {midX, to.y - radius * signY});
                ctx->DrawBezierCurve({midX, to.y - radius * signY}, {midX, to.y},
                                     {midX, to.y}, {midX + radius * signX, to.y});
                ctx->DrawLine({midX + radius * signX, to.y}, to);
            } else {
                double midY = (from.y + to.y) * 0.5;
                ctx->DrawLinePath({from, {from.x, midY}, {to.x, midY}, to}, false);
            }
            break;
        }

        case MindMapConnectorStyle::TaperedBranch: {
            // Filled quad along the curve: wide at the parent, narrow at the
            // child. Sampled rather than stroked so the width can vary.
            double dx = to.x - from.x;
            double dy = to.y - from.y;
            Point2Dd c1 = horizontal ? Point2Dd(from.x + dx * 0.5, from.y)
                                     : Point2Dd(from.x, from.y + dy * 0.5);
            Point2Dd c2 = horizontal ? Point2Dd(to.x - dx * 0.5, to.y)
                                     : Point2Dd(to.x, to.y - dy * 0.5);

            const int samples = 18;
            std::vector<Point2Dd> upper, lower;
            upper.reserve(samples + 1);
            lower.reserve(samples + 1);
            for (int i = 0; i <= samples; ++i) {
                double t = static_cast<double>(i) / samples;
                double mt = 1.0 - t;
                double px = mt*mt*mt*from.x + 3*mt*mt*t*c1.x + 3*mt*t*t*c2.x + t*t*t*to.x;
                double py = mt*mt*mt*from.y + 3*mt*mt*t*c1.y + 3*mt*t*t*c2.y + t*t*t*to.y;

                // Tangent, for the perpendicular offset.
                double tx = 3*mt*mt*(c1.x-from.x) + 6*mt*t*(c2.x-c1.x) + 3*t*t*(to.x-c2.x);
                double ty = 3*mt*mt*(c1.y-from.y) + 6*mt*t*(c2.y-c1.y) + 3*t*t*(to.y-c2.y);
                double len = std::sqrt(tx*tx + ty*ty);
                if (len < 1e-9) { tx = 1.0; ty = 0.0; len = 1.0; }
                double nx = -ty / len;
                double ny = tx / len;

                double halfWidth = (style.taperStartWidth * (1.0 - t) +
                                    style.taperEndWidth * t) * 0.5;
                upper.push_back({px + nx * halfWidth, py + ny * halfWidth});
                lower.push_back({px - nx * halfWidth, py - ny * halfWidth});
            }
            std::vector<Point2Dd> outline = upper;
            outline.insert(outline.end(), lower.rbegin(), lower.rend());
            ctx->SetFillPaint(color);
            ctx->FillLinePath(outline);
            break;
        }
    }

    if (style.showConnectorBadges) {
        RenderConnectorBadge(ctx, child, from, to, color);
    }

    if (style.endDecoration != MindMapEndDecoration::NoDecoration) {
        double dirX = to.x - from.x;
        double dirY = to.y - from.y;
        double len = std::sqrt(dirX * dirX + dirY * dirY);
        if (len > 1e-9) {
            RenderEndDecoration(ctx, to, {dirX / len, dirY / len}, color);
        }
    }
}

void UltraCanvasMindMap::RenderEndDecoration(IRenderContext* ctx, const Point2Dd& tip,
                                             const Point2Dd& direction, const Color& color) {
    double size = style.endDecorationSize;
    ctx->SetFillPaint(color);
    ctx->SetStrokePaint(color);

    switch (style.endDecoration) {
        case MindMapEndDecoration::Dot:
            ctx->FillCircle(tip, size * 0.6);
            break;
        case MindMapEndDecoration::Square:
            ctx->FillRectangle(Rect2Dd(tip.x - size * 0.5, tip.y - size * 0.5, size, size));
            break;
        case MindMapEndDecoration::Arrow: {
            double angle = std::atan2(direction.y, direction.x);
            double spread = 0.45;
            Point2Dd left(tip.x - std::cos(angle - spread) * size * 2.0,
                          tip.y - std::sin(angle - spread) * size * 2.0);
            Point2Dd right(tip.x - std::cos(angle + spread) * size * 2.0,
                           tip.y - std::sin(angle + spread) * size * 2.0);
            ctx->FillLinePath({tip, left, right});
            break;
        }
        case MindMapEndDecoration::NoDecoration:
        default:
            break;
    }
}

void UltraCanvasMindMap::RenderRelationships(IRenderContext* ctx) {
    for (const auto& relationship : model.Relationships()) {
        const MindMapTopic* from = model.GetTopic(relationship.fromTopicId);
        const MindMapTopic* to = model.GetTopic(relationship.toTopicId);
        if (!from || !to) continue;
        if (!layoutResult.Has(from->id) || !layoutResult.Has(to->id)) continue;

        MindMapTopicStyle fromStyle = ResolveTopicStyle(*from);
        MindMapTopicStyle toStyle = ResolveTopicStyle(*to);
        Point2Dd start = PerimeterPoint(*from, fromStyle, TopicCenter(*to));
        Point2Dd end = PerimeterPoint(*to, toStyle, TopicCenter(*from));

        ctx->SetStrokePaint(relationship.color);
        ctx->SetStrokeWidth(relationship.lineWidth);
        if (relationship.dashed) {
            ctx->SetLineDash(UCDashPattern({6.0, 4.0}));
        }

        // Bow the arc away from the straight line so it reads as a separate
        // connection rather than a branch.
        double dx = end.x - start.x;
        double dy = end.y - start.y;
        double len = std::sqrt(dx * dx + dy * dy);
        if (len > 1e-9) {
            double bow = std::min(80.0, len * 0.28);
            double nx = -dy / len * bow;
            double ny = dx / len * bow;
            Point2Dd c1(start.x + dx * 0.25 + nx, start.y + dy * 0.25 + ny);
            Point2Dd c2(start.x + dx * 0.75 + nx, start.y + dy * 0.75 + ny);
            ctx->DrawBezierCurve(start, c1, c2, end);

            if (relationship.arrowAtEnd) {
                double tipDirX = end.x - c2.x;
                double tipDirY = end.y - c2.y;
                double tipLen = std::sqrt(tipDirX * tipDirX + tipDirY * tipDirY);
                if (tipLen > 1e-9) {
                    ctx->SetLineDash(UCDashPattern());
                    double angle = std::atan2(tipDirY / tipLen, tipDirX / tipLen);
                    double size = 5.0;
                    Point2Dd left(end.x - std::cos(angle - 0.45) * size * 2.0,
                                  end.y - std::sin(angle - 0.45) * size * 2.0);
                    Point2Dd right(end.x - std::cos(angle + 0.45) * size * 2.0,
                                   end.y - std::sin(angle + 0.45) * size * 2.0);
                    ctx->SetFillPaint(relationship.color);
                    ctx->FillLinePath({end, left, right});
                }
            }

            if (!relationship.label.empty()) {
                ctx->SetLineDash(UCDashPattern());
                FontStyle font;
                font.fontFamily = style.fontFamily;
                font.fontSize = 10.0;
                ctx->SetFontStyle(font);
                Size2Di textSize = ctx->GetTextLineDimensions(relationship.label);
                double labelX = start.x + dx * 0.5 + nx * 0.75;
                double labelY = start.y + dy * 0.5 + ny * 0.75;
                Rect2Dd plate(labelX - textSize.width * 0.5 - 4.0,
                              labelY - textSize.height * 0.5 - 2.0,
                              textSize.width + 8.0, textSize.height + 4.0);
                ctx->SetFillPaint(WithAlpha(style.backgroundColor, 235));
                ctx->FillRoundedRectangle(plate, 3.0);
                ctx->SetTextPaint(relationship.color);
                ctx->DrawText(relationship.label,
                              {plate.x + 4.0, plate.y + 2.0});
            }
        }
        ctx->SetLineDash(UCDashPattern());
    }
}

void UltraCanvasMindMap::RenderTopics(IRenderContext* ctx) {
    // Shallow first so deeper topics paint over their parents' edges.
    std::vector<const MindMapTopic*> ordered;
    ordered.reserve(layoutResult.bounds.size());
    for (const auto& [id, box] : layoutResult.bounds) {
        if (const MindMapTopic* topic = model.GetTopic(id)) ordered.push_back(topic);
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const MindMapTopic* a, const MindMapTopic* b) {
                  if (a->depth != b->depth) return a->depth < b->depth;
                  return a->id < b->id;   // Stable, so rendering is deterministic
              });

    for (const MindMapTopic* topic : ordered) {
        if (editingTopicId == topic->id) continue;   // The editor covers it
        MindMapTopicStyle topicStyle = ResolveTopicStyle(*topic);
        RenderTopicShape(ctx, *topic, topicStyle);
        RenderTopicContent(ctx, *topic, topicStyle);
        RenderTopicIndicators(ctx, *topic, topicStyle);
        if (style.showCollapseHandles && !topic->childIds.empty()) {
            RenderCollapseHandle(ctx, *topic);
        }
    }
}

double UltraCanvasMindMap::TopicOpacity(const MindMapTopic& topic) const {
    if (!style.dimUnrelatedOnHover || hoveredTopicId.empty()) return 1.0;
    const MindMapTopic* hovered = model.GetTopic(hoveredTopicId);
    if (!hovered) return 1.0;
    // The hovered topic's branch stays lit; so does the centre, which every
    // branch hangs off.
    if (topic.id == model.GetRootId()) return 1.0;
    if (hovered->branchIndex < 0) return 1.0;   // Hovering the centre lights all
    return (topic.branchIndex == hovered->branchIndex) ? 1.0 : style.dimOpacity;
}

void UltraCanvasMindMap::RenderTopicShape(IRenderContext* ctx, const MindMapTopic& topic,
                                          const MindMapTopicStyle& topicStyle) {
    const Rect2Dd& box = topic.bounds;
    const bool selected = IsTopicSelected(topic.id);
    const bool hovered = (hoveredTopicId == topic.id);

    Color fill = topicStyle.outlineOnly ? Color(0, 0, 0, 0) : topicStyle.fillColor;
    Color border = topicStyle.borderColor;
    double borderWidth = topicStyle.borderWidth;

    double opacity = TopicOpacity(topic);
    if (opacity < 1.0) {
        fill = WithAlpha(fill, static_cast<int>(fill.a * opacity));
        border = WithAlpha(border, static_cast<int>(border.a * opacity));
    }

    // Drop shadow: the same silhouette offset behind the node (S8).
    if (style.showShadows && opacity >= 1.0 &&
        topicStyle.shape != MindMapNodeShape::Underline &&
        topicStyle.shape != MindMapNodeShape::NoShape) {
        Rect2Dd shadowBox(box.x + style.shadowOffsetX, box.y + style.shadowOffsetY,
                          box.width, box.height);
        ctx->SetFillPaint(style.shadowColor);
        switch (topicStyle.shape) {
            case MindMapNodeShape::Circle:
            case MindMapNodeShape::Ellipse:
                ctx->FillEllipse(shadowBox);
                break;
            case MindMapNodeShape::Pill:
                ctx->FillRoundedRectangle(shadowBox, shadowBox.height * 0.5);
                break;
            case MindMapNodeShape::Rect:
                ctx->FillRectangle(shadowBox);
                break;
            default:
                ctx->FillRoundedRectangle(shadowBox, topicStyle.cornerRadius);
                break;
        }
    }

    if (selected) {
        border = style.selectionColor;
        borderWidth = std::max(borderWidth, style.selectionWidth);
    } else if (hovered) {
        border = style.hoverColor;
    }

    ctx->SetFillPaint(fill);
    ctx->SetStrokePaint(border);
    ctx->SetStrokeWidth(borderWidth);

    switch (topicStyle.shape) {
        case MindMapNodeShape::Rect:
            if (fill.a > 0) ctx->FillRectangle(box);
            ctx->DrawRectangle(box);
            break;

        case MindMapNodeShape::Pill: {
            double radius = box.height * 0.5;
            if (fill.a > 0) ctx->FillRoundedRectangle(box, radius);
            ctx->DrawRoundedRectangle(box, radius);
            break;
        }

        case MindMapNodeShape::Circle:
        case MindMapNodeShape::Ellipse:
            if (fill.a > 0) ctx->FillEllipse(box);
            ctx->DrawEllipse(box);
            break;

        case MindMapNodeShape::Diamond: {
            std::vector<Point2Dd> points = {
                {box.x + box.width * 0.5, box.y},
                {box.x + box.width, box.y + box.height * 0.5},
                {box.x + box.width * 0.5, box.y + box.height},
                {box.x, box.y + box.height * 0.5},
            };
            if (fill.a > 0) ctx->FillLinePath(points);
            ctx->DrawLinePath(points, true);
            break;
        }

        case MindMapNodeShape::Hexagon: {
            double inset = std::min(box.width * 0.22, box.height * 0.5);
            std::vector<Point2Dd> points = {
                {box.x + inset, box.y},
                {box.x + box.width - inset, box.y},
                {box.x + box.width, box.y + box.height * 0.5},
                {box.x + box.width - inset, box.y + box.height},
                {box.x + inset, box.y + box.height},
                {box.x, box.y + box.height * 0.5},
            };
            if (fill.a > 0) ctx->FillLinePath(points);
            ctx->DrawLinePath(points, true);
            break;
        }

        case MindMapNodeShape::Cloud: {
            // Overlapping circles along the perimeter give the bumpy outline.
            const int bumps = 9;
            double rx = box.width * 0.5;
            double ry = box.height * 0.5;
            double cx = box.x + rx;
            double cy = box.y + ry;
            double bumpRadius = std::min(rx, ry) * 0.42;
            if (fill.a > 0) {
                ctx->FillEllipse(Rect2Dd(box.x + bumpRadius * 0.5, box.y + bumpRadius * 0.5,
                                         box.width - bumpRadius, box.height - bumpRadius));
            }
            for (int i = 0; i < bumps; ++i) {
                double angle = (2.0 * kPi * i) / bumps;
                double px = cx + std::cos(angle) * (rx - bumpRadius * 0.8);
                double py = cy + std::sin(angle) * (ry - bumpRadius * 0.8);
                if (fill.a > 0) {
                    ctx->SetFillPaint(fill);
                    ctx->FillCircle({px, py}, bumpRadius);
                }
            }
            ctx->SetStrokePaint(border);
            for (int i = 0; i < bumps; ++i) {
                double angle = (2.0 * kPi * i) / bumps;
                double px = cx + std::cos(angle) * (rx - bumpRadius * 0.8);
                double py = cy + std::sin(angle) * (ry - bumpRadius * 0.8);
                ctx->DrawArc(px, py, bumpRadius, angle - 1.35, angle + 1.35);
            }
            break;
        }

        case MindMapNodeShape::Bang: {
            // Starburst: alternating outer and inner radii.
            const int spikes = 11;
            double rx = box.width * 0.5;
            double ry = box.height * 0.5;
            double cx = box.x + rx;
            double cy = box.y + ry;
            std::vector<Point2Dd> points;
            for (int i = 0; i < spikes * 2; ++i) {
                double angle = (kPi * i) / spikes;
                double scale = (i % 2 == 0) ? 1.0 : 0.82;
                points.push_back({cx + std::cos(angle) * rx * scale,
                                  cy + std::sin(angle) * ry * scale});
            }
            if (fill.a > 0) ctx->FillLinePath(points);
            ctx->DrawLinePath(points, true);
            break;
        }

        case MindMapNodeShape::Underline:
            ctx->SetStrokePaint(border);
            ctx->SetStrokeWidth(std::max(1.5, borderWidth));
            ctx->DrawLine({box.x, box.y + box.height},
                          {box.x + box.width, box.y + box.height});
            if (selected) {
                ctx->SetStrokePaint(style.selectionColor);
                ctx->SetStrokeWidth(1.0);
                ctx->DrawRectangle(box);
            }
            break;

        case MindMapNodeShape::NoShape:
            if (selected) {
                ctx->SetStrokePaint(style.selectionColor);
                ctx->SetStrokeWidth(1.0);
                ctx->DrawRectangle(box);
            }
            break;

        case MindMapNodeShape::RoundedRect:
        default:
            if (fill.a > 0) ctx->FillRoundedRectangle(box, topicStyle.cornerRadius);
            ctx->DrawRoundedRectangle(box, topicStyle.cornerRadius);
            break;
    }
}

void UltraCanvasMindMap::RenderTopicContent(IRenderContext* ctx, const MindMapTopic& topic,
                                            const MindMapTopicStyle& topicStyle) {
    const Rect2Dd& box = topic.bounds;

    // Image node: the picture is the body (C1).
    if (!topic.imagePath.empty()) {
        Rect2Dd inner(box.x + topicStyle.borderWidth, box.y + topicStyle.borderWidth,
                      box.width - topicStyle.borderWidth * 2.0,
                      box.height - topicStyle.borderWidth * 2.0);
        ctx->DrawImage(topic.imagePath, inner, ImageFitMode::Contain);
        if (topic.text.empty()) return;
    }

    double textLeft = box.x + topicStyle.paddingX;
    double availableWidth = box.width - topicStyle.paddingX * 2.0;

    // Inline icon before the label (C2).
    if (!topic.iconPath.empty()) {
        double iconSize = topicStyle.fontSize * 1.15;
        Rect2Dd iconRect(textLeft, box.y + (box.height - iconSize) * 0.5, iconSize, iconSize);
        ctx->DrawImage(topic.iconPath, iconRect, ImageFitMode::Contain);
        double advance = iconSize + topicStyle.fontSize * 0.45;
        textLeft += advance;
        availableWidth -= advance;
    }

    // Ordinal badge before the label (C8).
    std::string label = topic.text;
    if (style.numbering == MindMapNumbering::Prefix && !topic.ordinal.empty()) {
        label = topic.ordinal + "  " + label;
    } else if (style.numbering == MindMapNumbering::Badge && !topic.ordinal.empty()) {
        double radius = topicStyle.fontSize * 0.72;
        Point2Dd center(box.x - radius - 3.0, box.y + box.height * 0.5);
        if (topic.resolvedSide == MindMapTopicSide::Left) {
            center.x = box.x + box.width + radius + 3.0;
        }
        Color badgeColor = (topic.branchIndex >= 0) ? GetBranchColor(topic.branchIndex)
                                                    : topicStyle.borderColor;
        ctx->SetFillPaint(badgeColor);
        ctx->FillCircle(center, radius);

        FontStyle badgeFont;
        badgeFont.fontFamily = style.fontFamily;
        badgeFont.fontSize = topicStyle.fontSize * 0.8;
        badgeFont.fontWeight = FontWeight::Bold;
        ctx->SetFontStyle(badgeFont);
        Size2Di ordinalSize = ctx->GetTextLineDimensions(topic.ordinal);
        ctx->SetTextPaint(Luminance(badgeColor) > 0.6 ? Color(30, 30, 40, 255)
                                                      : Color(255, 255, 255, 255));
        ctx->DrawText(topic.ordinal,
                      {center.x - ordinalSize.width * 0.5,
                       center.y - ordinalSize.height * 0.5});
    }

    if (label.empty()) return;

    FontStyle font;
    font.fontFamily = style.fontFamily;
    font.fontSize = topicStyle.fontSize;
    font.fontWeight = topicStyle.bold ? FontWeight::Bold : FontWeight::Normal;
    ctx->SetFontStyle(font);
    ctx->SetTextPaint(topicStyle.textColor);

    // DrawTextInRect handles wrapping; the box was sized to fit during layout.
    Rect2Dd textRect(textLeft, box.y + topicStyle.paddingY,
                     std::max(1.0, availableWidth),
                     box.height - topicStyle.paddingY * 2.0);
    ctx->DrawTextInRect(label, textRect);
}

void UltraCanvasMindMap::RenderCollapseHandle(IRenderContext* ctx, const MindMapTopic& topic) {
    Rect2Dd handle = CollapseHandleRect(topic);
    Point2Dd center(handle.x + handle.width * 0.5, handle.y + handle.height * 0.5);
    double radius = style.collapseHandleRadius;

    ctx->SetFillPaint(style.collapseHandleFill);
    ctx->FillCircle(center, radius);
    ctx->SetStrokePaint(hoveredHandleTopicId == topic.id ? style.selectionColor
                                                         : style.collapseHandleBorder);
    ctx->SetStrokeWidth(1.2);
    ctx->DrawCircle(center, radius);

    // Minus when expanded, plus when collapsed.
    ctx->SetStrokePaint(style.collapseHandleGlyph);
    ctx->SetStrokeWidth(1.6);
    double arm = radius * 0.5;
    ctx->DrawLine({center.x - arm, center.y}, {center.x + arm, center.y});
    if (topic.collapsed) {
        ctx->DrawLine({center.x, center.y - arm}, {center.x, center.y + arm});
    }

    if (topic.collapsed && style.showChildCountWhenCollapsed) {
        int count = model.CountDescendants(topic.id);
        if (count > 0) {
            FontStyle font;
            font.fontFamily = style.fontFamily;
            font.fontSize = 9.5;
            ctx->SetFontStyle(font);
            std::string text = std::to_string(count);
            Size2Di textSize = ctx->GetTextLineDimensions(text);
            double badgeX = (topic.resolvedSide == MindMapTopicSide::Left)
                ? center.x - radius - 6.0 - textSize.width
                : center.x + radius + 6.0;
            ctx->SetTextPaint(style.collapseHandleGlyph);
            ctx->DrawText(text, {badgeX, center.y - textSize.height * 0.5});
        }
    }
}

void UltraCanvasMindMap::RenderDropIndicator(IRenderContext* ctx) {
    if (dropTargetId.empty()) return;
    const MindMapTopic* target = model.GetTopic(dropTargetId);
    if (!target || !layoutResult.Has(target->id)) return;

    Rect2Dd box = target->bounds;
    box.x -= 3.0;
    box.y -= 3.0;
    box.width += 6.0;
    box.height += 6.0;

    ctx->SetStrokePaint(style.dropIndicatorColor);
    ctx->SetStrokeWidth(2.5);
    ctx->SetLineDash(UCDashPattern({5.0, 3.0}));
    ctx->DrawRoundedRectangle(box, 8.0);
    ctx->SetLineDash(UCDashPattern());
}


// =============================================================================
// EVENTS
// =============================================================================

bool UltraCanvasMindMap::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;

    isMultiSelectKeyHeld = event.shift;

    // While editing, the overlay gets first refusal so Tab/Enter/Delete reach
    // the editor instead of the map's authoring shortcuts.
    if (editOverlay && editOverlay->IsVisible() && IsEditing()) {
        switch (event.type) {
            case UCEventType::KeyDown:
            case UCEventType::TextInput:
            case UCEventType::KeyUp:
                if (editOverlay->OnEvent(event)) {
                    RequestRedraw();
                    return true;
                }
                break;
            case UCEventType::MouseDown: {
                // A click inside the editor edits; a click outside commits.
                Rect2Df editorBounds = editOverlay->GetBounds();
                Point2Di pos(event.pointer.x, event.pointer.y);
                if (pos.x >= editorBounds.x && pos.x <= editorBounds.x + editorBounds.width &&
                    pos.y >= editorBounds.y && pos.y <= editorBounds.y + editorBounds.height) {
                    bool handled = editOverlay->OnEvent(event);
                    RequestRedraw();
                    if (handled) return true;
                } else {
                    CommitEdit();
                }
                break;
            }
            default:
                break;
        }
    }

    switch (event.type) {
        case UCEventType::MouseDown:        return HandleMouseDown(event);
        case UCEventType::MouseUp:          return HandleMouseUp(event);
        case UCEventType::MouseMove:        return HandleMouseMove(event);
        case UCEventType::MouseWheel:       return HandleMouseWheel(event);
        case UCEventType::MouseDoubleClick: return HandleDoubleClick(event);
        case UCEventType::KeyDown:          return HandleKeyDown(event);
        default:                            return false;
    }
}

bool UltraCanvasMindMap::HandleMouseDown(const UCEvent& event) {
    Point2Di mousePos(event.pointer.x, event.pointer.y);
    if (!Contains(mousePos)) return false;

    SetFocus(true);
    lastMousePos = mousePos;
    dragStartPos = mousePos;
    dragExceededThreshold = false;

    // 1. Controls overlay - reachable even when editing is locked, so the lock
    //    button can always be released.
    if (viewport.AreControlsVisible() && event.button == UCMouseButton::Left) {
        int buttonIndex = viewport.FindControlButtonAt(mousePos);
        if (buttonIndex >= 0) {
            SyncViewportSize();
            DiagramControlButton role =
                viewport.ApplyControlButton(buttonIndex, layoutResult.contentBounds);
            if (role == DiagramControlButton::ToggleLock) {
                editable = !editable;
            } else if (role != DiagramControlButton::NoAction) {
                NotifyViewportChanged();
            }
            RequestRedraw();
            return true;
        }
    }

    // 2. Minimap
    if (viewport.IsMinimapVisible() && viewport.MinimapConfig().pannable &&
        event.button == UCMouseButton::Left && viewport.PointInMinimap(mousePos)) {
        isDraggingMinimap = true;
        viewport.HandleMinimapDrag(mousePos, layoutResult.contentBounds);
        NotifyViewportChanged();
        RequestRedraw();
        return true;
    }

    // 3. Right click -> context callbacks, no state change.
    if (event.button == UCMouseButton::Right) {
        std::string topicId = FindTopicAt(mousePos);
        Point2Dd world = viewport.ScreenToWorld(mousePos);
        if (!topicId.empty()) {
            if (onTopicRightClick) onTopicRightClick(topicId, world.x, world.y);
        } else if (onCanvasRightClick) {
            onCanvasRightClick(world.x, world.y);
        }
        return true;
    }

    // 4. Collapse handle
    std::string handleTopic = FindCollapseHandleAt(mousePos);
    if (!handleTopic.empty()) {
        ToggleCollapsed(handleTopic);
        return true;
    }

    // 5. Link indicator on a topic (C6)
    std::string linkTopic = FindLinkIndicatorAt(mousePos);
    if (!linkTopic.empty()) {
        const MindMapTopic* topic = model.GetTopic(linkTopic);
        if (topic && onTopicLinkActivated) onTopicLinkActivated(linkTopic, topic->link);
        SelectTopic(linkTopic, isMultiSelectKeyHeld);
        return true;
    }

    // 6. Topic
    std::string topicId = FindTopicAt(mousePos);
    if (!topicId.empty()) {
        if (!IsTopicSelected(topicId) || !isMultiSelectKeyHeld) {
            SelectTopic(topicId, isMultiSelectKeyHeld);
        }
        if (editable && topicId != model.GetRootId()) {
            isDraggingTopic = true;
            dragTopicId = topicId;
        }
        if (onTopicClick) onTopicClick(topicId);
        return true;
    }

    // 7. Empty canvas -> deselect and start panning.
    DeselectAll();
    isDraggingViewport = true;
    SetMouseCursor(UCMouseCursor::SizeAll);
    return true;
}

bool UltraCanvasMindMap::HandleMouseMove(const UCEvent& event) {
    Point2Di mousePos(event.pointer.x, event.pointer.y);
    Point2Di delta(mousePos.x - lastMousePos.x, mousePos.y - lastMousePos.y);

    if (isDraggingMinimap) {
        viewport.HandleMinimapDrag(mousePos, layoutResult.contentBounds);
        NotifyViewportChanged();
        RequestRedraw();
        lastMousePos = mousePos;
        return true;
    }

    if (isDraggingViewport) {
        viewport.PanBy(delta.x, delta.y);
        NotifyViewportChanged();
        RequestRedraw();
        lastMousePos = mousePos;
        return true;
    }

    if (isDraggingTopic) {
        double moved = std::hypot(static_cast<double>(mousePos.x - dragStartPos.x),
                                  static_cast<double>(mousePos.y - dragStartPos.y));
        if (moved > kDragThreshold) dragExceededThreshold = true;

        if (dragExceededThreshold) {
            // The drop target is whatever topic is under the cursor, as long as
            // it isn't the dragged topic or one of its own descendants.
            std::string candidate = FindTopicAt(mousePos);
            if (candidate == dragTopicId ||
                (!candidate.empty() && model.IsDescendantOf(candidate, dragTopicId))) {
                candidate.clear();
            }
            if (candidate != dropTargetId) {
                dropTargetId = candidate;
                RequestRedraw();
            }
            SetMouseCursor(dropTargetId.empty() ? UCMouseCursor::Default : UCMouseCursor::Hand);
        }
        lastMousePos = mousePos;
        return true;
    }

    // Hover feedback.
    std::string previousTopic = hoveredTopicId;
    std::string previousHandle = hoveredHandleTopicId;
    int previousControl = hoveredControlButton;

    hoveredControlButton = viewport.AreControlsVisible()
        ? viewport.FindControlButtonAt(mousePos) : -1;
    hoveredHandleTopicId = FindCollapseHandleAt(mousePos);
    hoveredTopicId = hoveredHandleTopicId.empty() ? FindTopicAt(mousePos) : std::string();

    if (!hoveredHandleTopicId.empty() || hoveredControlButton >= 0) {
        SetMouseCursor(UCMouseCursor::Hand);
    } else if (!hoveredTopicId.empty()) {
        SetMouseCursor(editable ? UCMouseCursor::SizeAll : UCMouseCursor::Default);
    } else {
        SetMouseCursor(UCMouseCursor::Default);
    }

    if (previousTopic != hoveredTopicId || previousHandle != hoveredHandleTopicId ||
        previousControl != hoveredControlButton) {
        RequestRedraw();   // Also repaints the dimming when it is enabled (I11)
    }
    lastMousePos = mousePos;
    return false;
}

bool UltraCanvasMindMap::HandleMouseUp(const UCEvent& event) {
    Point2Di mousePos(event.pointer.x, event.pointer.y);

    if (isDraggingMinimap) {
        isDraggingMinimap = false;
        return true;
    }

    if (isDraggingViewport) {
        isDraggingViewport = false;
        SetMouseCursor(UCMouseCursor::Default);
        return true;
    }

    if (isDraggingTopic) {
        bool reparented = false;
        if (dragExceededThreshold && !dropTargetId.empty() && dropTargetId != dragTopicId) {
            reparented = MoveTopic(dragTopicId, dropTargetId);
        }
        isDraggingTopic = false;
        dragTopicId.clear();
        dropTargetId.clear();
        dragExceededThreshold = false;
        SetMouseCursor(UCMouseCursor::Default);
        if (reparented) RequestRedraw();
        return true;
    }

    (void)mousePos;
    return false;
}

bool UltraCanvasMindMap::HandleMouseWheel(const UCEvent& event) {
    Point2Di mousePos(event.pointer.x, event.pointer.y);
    if (!Contains(mousePos)) return false;

    SyncViewportSize();
    double factor = (event.wheelDelta > 0) ? 1.1 : (1.0 / 1.1);
    if (!viewport.ZoomAtPoint(mousePos, factor)) {
        return true;   // At the zoom limit; the event is still consumed
    }
    NotifyViewportChanged();
    RequestRedraw();
    return true;
}

bool UltraCanvasMindMap::HandleDoubleClick(const UCEvent& event) {
    Point2Di mousePos(event.pointer.x, event.pointer.y);
    if (!Contains(mousePos)) return false;

    std::string topicId = FindTopicAt(mousePos);
    if (topicId.empty()) {
        FitView();
        return true;
    }
    if (onTopicDoubleClick) onTopicDoubleClick(topicId);
    if (editable) {
        BeginEditTopic(topicId);
    } else {
        ToggleCollapsed(topicId);
    }
    return true;
}

bool UltraCanvasMindMap::HandleKeyDown(const UCEvent& event) {
    // Navigation and view shortcuts work even when editing is locked.
    if ((event.ctrl || event.meta) && event.virtualKey == UCKeys::A) {
        SelectAll();
        return true;
    }
    if (event.virtualKey == UCKeys::Escape) {
        DeselectAll();
        return true;
    }

    if (!editable) return false;

    switch (event.virtualKey) {
        case UCKeys::Return:
            if (!primarySelection.empty()) {
                if (primarySelection == model.GetRootId()) {
                    // The centre has no siblings; Enter adds a main topic.
                    CreateChild(primarySelection);
                } else {
                    CreateSibling(primarySelection);
                }
                return true;
            }
            break;

        case UCKeys::Tab:
            if (!primarySelection.empty()) {
                CreateChild(primarySelection);
                return true;
            }
            break;

        case UCKeys::Delete:
        case UCKeys::Backspace:
            if (!selectedTopics.empty()) {
                DeleteSelected();
                return true;
            }
            break;

        case UCKeys::F2:
            if (!primarySelection.empty()) {
                BeginEditTopic(primarySelection);
                return true;
            }
            break;

        case UCKeys::Space:
            if (!primarySelection.empty()) {
                ToggleCollapsed(primarySelection);
                return true;
            }
            break;

        case UCKeys::C:
            if (event.ctrl || event.meta) {
                CopySelection();
                return true;
            }
            break;

        case UCKeys::X:
            if (event.ctrl || event.meta) {
                CutSelection();
                return true;
            }
            break;

        case UCKeys::V:
            if ((event.ctrl || event.meta) && !primarySelection.empty()) {
                PasteInto(primarySelection);
                return true;
            }
            break;

        case UCKeys::Z:
            if (event.ctrl || event.meta) {
                if (event.shift) Redo(); else Undo();
                return true;
            }
            break;

        case UCKeys::Y:
            if (event.ctrl || event.meta) {
                Redo();
                return true;
            }
            break;

        default:
            break;
    }

    // Arrow navigation: geometric, so it behaves sensibly in every structure.
    bool isArrow = (event.virtualKey == UCKeys::Left || event.virtualKey == UCKeys::Right ||
                    event.virtualKey == UCKeys::Up || event.virtualKey == UCKeys::Down);
    if (isArrow && !primarySelection.empty() && layoutResult.Has(primarySelection)) {
        Rect2Dd current = layoutResult.Get(primarySelection);
        Point2Dd from(current.x + current.width * 0.5, current.y + current.height * 0.5);

        std::string best;
        double bestScore = 1e18;
        for (const auto& [id, box] : layoutResult.bounds) {
            if (id == primarySelection) continue;
            Point2Dd to(box.x + box.width * 0.5, box.y + box.height * 0.5);
            double dx = to.x - from.x;
            double dy = to.y - from.y;

            bool inDirection =
                (event.virtualKey == UCKeys::Left  && dx < -1.0 && std::abs(dy) < std::abs(dx)) ||
                (event.virtualKey == UCKeys::Right && dx >  1.0 && std::abs(dy) < std::abs(dx)) ||
                (event.virtualKey == UCKeys::Up    && dy < -1.0 && std::abs(dx) < std::abs(dy)) ||
                (event.virtualKey == UCKeys::Down  && dy >  1.0 && std::abs(dx) < std::abs(dy));
            if (!inDirection) continue;

            double score = std::hypot(dx, dy);
            if (score < bestScore) {
                bestScore = score;
                best = id;
            }
        }
        if (!best.empty()) {
            SelectTopic(best);
            return true;
        }
    }

    return false;
}

// =============================================================================
// EDITING
// =============================================================================

void UltraCanvasMindMap::SetEditable(bool value) {
    editable = value;
    if (!editable) CancelEdit();
    RequestRedraw();
}

void UltraCanvasMindMap::EnsureEditOverlay() {
    if (editOverlay) return;

    editOverlay = std::make_shared<UltraCanvasTextInput>(
        GetIdentifier() + "_editor", 0.0f, 0.0f, 120.0f, 24.0f);

    editOverlay->onEnterPressed = [this](const std::string&) -> bool {
        CommitEdit();
        return true;
    };
    editOverlay->onEscapePressed = [this]() -> bool {
        CancelEdit();
        return true;
    };
    editOverlay->onFocusLost = [this]() {
        // Commit on blur, guarded so CommitEdit's own Hide() can't re-enter.
        if (!editCommitting && IsEditing()) CommitEdit();
    };
    editOverlay->onTextChanged = [this](const std::string&) {
        RequestRedraw();
    };
}

void UltraCanvasMindMap::BeginEditTopic(const std::string& id) {
    if (!editable) return;
    MindMapTopic* topic = model.GetTopic(id);
    if (!topic) return;

    if (IsEditing() && editingTopicId != id) CommitEdit();

    EnsureLayout();
    EnsureEditOverlay();

    editingTopicId = id;
    editOverlay->SetText(topic->text);
    editOverlay->SetVisible(true);
    PositionEditOverlay();
    editOverlay->SetFocus(true);
    editOverlay->SetSelection(0, topic->text.size());
    RequestRedraw();
}

void UltraCanvasMindMap::PositionEditOverlay() {
    if (!editOverlay || editingTopicId.empty()) return;
    const MindMapTopic* topic = model.GetTopic(editingTopicId);
    if (!topic) return;

    // Screen-space placement over the topic's box, with the font scaled by the
    // zoom so the editor visually matches the text it replaces.
    double zoom = viewport.GetZoomLevel();
    Point2Dd topLeft = viewport.WorldToScreenD({topic->bounds.x, topic->bounds.y});
    double width = std::max(60.0, topic->bounds.width * zoom);
    double height = std::max(20.0, topic->bounds.height * zoom);

    editOverlay->SetBounds(static_cast<float>(topLeft.x), static_cast<float>(topLeft.y),
                           static_cast<float>(width), static_cast<float>(height));

    MindMapTopicStyle topicStyle = ResolveTopicStyle(*topic);
    editOverlay->SetFontSize(static_cast<float>(std::max(8.0, topicStyle.fontSize * zoom)));
}

void UltraCanvasMindMap::CommitEdit() {
    if (!IsEditing() || !editOverlay) return;

    editCommitting = true;
    std::string id = editingTopicId;
    std::string text = editOverlay->GetText();

    editingTopicId.clear();
    editOverlay->SetVisible(false);
    editOverlay->SetFocus(false);
    editCommitting = false;

    SetTopicText(id, text);
    SetFocus(true);
    RequestRedraw();
}

void UltraCanvasMindMap::CancelEdit() {
    if (!IsEditing() || !editOverlay) return;
    editCommitting = true;
    editingTopicId.clear();
    editOverlay->SetVisible(false);
    editOverlay->SetFocus(false);
    editCommitting = false;
    SetFocus(true);
    RequestRedraw();
}

std::string UltraCanvasMindMap::CreateSibling(const std::string& id, const std::string& text) {
    const MindMapTopic* topic = model.GetTopic(id);
    if (!topic || topic->parentId.empty()) return std::string();

    const MindMapTopic* parent = model.GetTopic(topic->parentId);
    if (!parent) return std::string();

    int index = -1;
    for (size_t i = 0; i < parent->childIds.size(); ++i) {
        if (parent->childIds[i] == id) {
            index = static_cast<int>(i) + 1;
            break;
        }
    }

    PushUndo("Add sibling");
    suppressUndo = true;
    std::string newId = model.AddTopic(topic->parentId, text);
    suppressUndo = false;
    if (newId.empty()) return newId;

    if (index >= 0) model.MoveTopicToIndex(newId, index);
    MarkLayoutDirty();
    if (onTopicAdded) onTopicAdded(newId);
    SelectTopic(newId);
    BeginEditTopic(newId);
    return newId;
}

std::string UltraCanvasMindMap::CreateChild(const std::string& id, const std::string& text) {
    if (!model.GetTopic(id)) return std::string();

    PushUndo("Add child");
    suppressUndo = true;
    std::string newId = model.AddTopic(id, text);
    suppressUndo = false;
    if (newId.empty()) return newId;

    // Adding to a collapsed parent must reveal it, or the new topic is invisible.
    if (MindMapTopic* parent = model.GetTopic(id)) parent->collapsed = false;

    MarkLayoutDirty();
    if (onTopicAdded) onTopicAdded(newId);
    SelectTopic(newId);
    BeginEditTopic(newId);
    return newId;
}

void UltraCanvasMindMap::DeleteSelected() {
    if (selectedTopics.empty()) return;

    PushUndo("Delete topics");
    suppressUndo = true;
    // Snapshot first: removing a subtree invalidates ids still in the set.
    std::vector<std::string> targets(selectedTopics.begin(), selectedTopics.end());
    for (const auto& id : targets) {
        if (id == model.GetRootId()) continue;   // Never delete the centre
        if (!model.GetTopic(id)) continue;       // Already gone with an ancestor
        for (const auto& removedId : model.RemoveTopic(id)) {
            if (onTopicRemoved) onTopicRemoved(removedId);
        }
    }
    suppressUndo = false;

    selectedTopics.clear();
    primarySelection.clear();
    MarkLayoutDirty();
    NotifySelectionChanged();
    RequestRedraw();
}

// =============================================================================
// UNDO / REDO
// =============================================================================

void UltraCanvasMindMap::PushUndo(const std::string& label) {
    if (suppressUndo) return;
    undoStack.push_back({ToJson(), label});
    while (undoStack.size() > undoLimit) undoStack.pop_front();
    redoStack.clear();
}

void UltraCanvasMindMap::ApplySnapshot(const std::string& json) {
    suppressUndo = true;
    CancelEdit();
    FromJson(json);
    suppressUndo = false;

    // Ids may be gone after a restore.
    for (auto it = selectedTopics.begin(); it != selectedTopics.end();) {
        it = model.GetTopic(*it) ? std::next(it) : selectedTopics.erase(it);
    }
    if (!primarySelection.empty() && !model.GetTopic(primarySelection)) {
        primarySelection.clear();
    }
    MarkLayoutDirty();
    NotifySelectionChanged();
    RequestRedraw();
}

void UltraCanvasMindMap::Undo() {
    if (undoStack.empty()) return;
    UndoEntry entry = undoStack.back();
    undoStack.pop_back();
    redoStack.push_back({ToJson(), entry.label});
    ApplySnapshot(entry.json);
}

void UltraCanvasMindMap::Redo() {
    if (redoStack.empty()) return;
    UndoEntry entry = redoStack.back();
    redoStack.pop_back();
    undoStack.push_back({ToJson(), entry.label});
    ApplySnapshot(entry.json);
}


// =============================================================================
// SERIALIZATION
// =============================================================================

namespace {

JSONValue ColorToJson(const Color& color) {
    JSONValue value = JSONValue::MakeArray();
    value.Append(JSONValue(static_cast<int>(color.r)));
    value.Append(JSONValue(static_cast<int>(color.g)));
    value.Append(JSONValue(static_cast<int>(color.b)));
    value.Append(JSONValue(static_cast<int>(color.a)));
    return value;
}

Color ColorFromJson(const JSONValue& value, const Color& fallback) {
    if (!value.IsArray() || value.GetSize() < 3) return fallback;
    int alpha = (value.GetSize() >= 4) ? static_cast<int>(value.At(3).GetInteger(255)) : 255;
    return Color(static_cast<int>(value.At(0).GetInteger(0)),
                 static_cast<int>(value.At(1).GetInteger(0)),
                 static_cast<int>(value.At(2).GetInteger(0)),
                 alpha);
}

JSONValue TopicStyleToJson(const MindMapTopicStyle& s) {
    JSONValue value = JSONValue::MakeObject();
    value.Set("shape", JSONValue(static_cast<int>(s.shape)));
    value.Set("fillColor", ColorToJson(s.fillColor));
    value.Set("borderColor", ColorToJson(s.borderColor));
    value.Set("textColor", ColorToJson(s.textColor));
    value.Set("borderWidth", JSONValue(s.borderWidth));
    value.Set("cornerRadius", JSONValue(s.cornerRadius));
    value.Set("fontSize", JSONValue(s.fontSize));
    value.Set("bold", JSONValue(s.bold));
    value.Set("paddingX", JSONValue(s.paddingX));
    value.Set("paddingY", JSONValue(s.paddingY));
    value.Set("minWidth", JSONValue(s.minWidth));
    value.Set("minHeight", JSONValue(s.minHeight));
    value.Set("maxWidth", JSONValue(s.maxWidth));
    value.Set("outlineOnly", JSONValue(s.outlineOnly));
    return value;
}

MindMapTopicStyle TopicStyleFromJson(const JSONValue& value) {
    MindMapTopicStyle s;
    s.shape = static_cast<MindMapNodeShape>(value.Get("shape").GetInteger(
        static_cast<int64_t>(s.shape)));
    s.fillColor = ColorFromJson(value.Get("fillColor"), s.fillColor);
    s.borderColor = ColorFromJson(value.Get("borderColor"), s.borderColor);
    s.textColor = ColorFromJson(value.Get("textColor"), s.textColor);
    s.borderWidth = value.Get("borderWidth").GetNumber(s.borderWidth);
    s.cornerRadius = value.Get("cornerRadius").GetNumber(s.cornerRadius);
    s.fontSize = value.Get("fontSize").GetNumber(s.fontSize);
    s.bold = value.Get("bold").GetBoolean(s.bold);
    s.paddingX = value.Get("paddingX").GetNumber(s.paddingX);
    s.paddingY = value.Get("paddingY").GetNumber(s.paddingY);
    s.minWidth = value.Get("minWidth").GetNumber(s.minWidth);
    s.minHeight = value.Get("minHeight").GetNumber(s.minHeight);
    s.maxWidth = value.Get("maxWidth").GetNumber(s.maxWidth);
    s.outlineOnly = value.Get("outlineOnly").GetBoolean(s.outlineOnly);
    return s;
}

} // namespace

std::string UltraCanvasMindMap::ToJson() const {
    JSONValue root = JSONValue::MakeObject();
    root.Set("version", JSONValue(1));
    root.Set("rootId", JSONValue(model.GetRootId()));

    JSONValue topics = JSONValue::MakeArray();
    for (const auto& [id, topic] : model.Topics()) {
        JSONValue value = JSONValue::MakeObject();
        value.Set("id", JSONValue(topic.id));
        value.Set("parentId", JSONValue(topic.parentId));
        value.Set("text", JSONValue(topic.text));
        value.Set("side", JSONValue(static_cast<int>(topic.side)));
        value.Set("collapsed", JSONValue(topic.collapsed));
        value.Set("weight", JSONValue(topic.weight));
        if (!topic.iconPath.empty())  value.Set("iconPath", JSONValue(topic.iconPath));
        if (!topic.imagePath.empty()) value.Set("imagePath", JSONValue(topic.imagePath));
        if (!topic.note.empty())      value.Set("note", JSONValue(topic.note));
        if (!topic.link.empty())      value.Set("link", JSONValue(topic.link));
        if (topic.floating) {
            value.Set("floating", JSONValue(true));
            value.Set("floatX", JSONValue(topic.floatX));
            value.Set("floatY", JSONValue(topic.floatY));
        }
        if (topic.styleOverride.has_value()) {
            value.Set("style", TopicStyleToJson(topic.styleOverride.value()));
        }
        // Child order matters and an unordered map does not preserve it.
        JSONValue children = JSONValue::MakeArray();
        for (const auto& childId : topic.childIds) children.Append(JSONValue(childId));
        value.Set("children", children);
        topics.Append(value);
    }
    root.Set("topics", topics);

    JSONValue relationships = JSONValue::MakeArray();
    for (const auto& relationship : model.Relationships()) {
        JSONValue value = JSONValue::MakeObject();
        value.Set("id", JSONValue(relationship.id));
        value.Set("from", JSONValue(relationship.fromTopicId));
        value.Set("to", JSONValue(relationship.toTopicId));
        value.Set("label", JSONValue(relationship.label));
        value.Set("color", ColorToJson(relationship.color));
        value.Set("lineWidth", JSONValue(relationship.lineWidth));
        value.Set("dashed", JSONValue(relationship.dashed));
        value.Set("arrowAtEnd", JSONValue(relationship.arrowAtEnd));
        relationships.Append(value);
    }
    root.Set("relationships", relationships);

    JSONValue layout = JSONValue::MakeObject();
    layout.Set("structure", JSONValue(static_cast<int>(layoutOptions.structure)));
    layout.Set("balancePolicy", JSONValue(static_cast<int>(layoutOptions.balancePolicy)));
    layout.Set("siblingGap", JSONValue(layoutOptions.siblingGap));
    layout.Set("levelGap", JSONValue(layoutOptions.levelGap));
    layout.Set("rootGap", JSONValue(layoutOptions.rootGap));
    layout.Set("radialRingGap", JSONValue(layoutOptions.radialRingGap));
    layout.Set("sizeFromWeight", JSONValue(layoutOptions.sizeFromWeight));
    root.Set("layout", layout);

    JSONValue view = JSONValue::MakeObject();
    view.Set("theme", JSONValue(static_cast<int>(theme)));
    view.Set("connectorStyle", JSONValue(static_cast<int>(style.connectorStyle)));
    view.Set("numbering", JSONValue(static_cast<int>(style.numbering)));
    view.Set("zoom", JSONValue(viewport.GetZoomLevel()));
    view.Set("panX", JSONValue(viewport.GetPanOffset().x));
    view.Set("panY", JSONValue(viewport.GetPanOffset().y));
    root.Set("view", view);

    JSONSerializeOptions options;
    options.pretty = true;
    return JSON::Serialize(root, options);
}

bool UltraCanvasMindMap::FromJson(const std::string& json) {
    JSONParseResult parseResult;
    JSONValue root = JSON::Parse(json, &parseResult);
    if (!parseResult.success || !root.IsObject()) return false;

    model.Clear();
    selectedTopics.clear();
    primarySelection.clear();

    // Two passes: create every topic first, then wire the child order, so
    // forward references in the file are fine.
    const JSONValue& topics = root.Get("topics");
    std::vector<std::pair<std::string, std::vector<std::string>>> childOrder;

    for (size_t i = 0; i < topics.GetSize(); ++i) {
        const JSONValue& value = topics.At(i);
        MindMapTopic topic;
        topic.id = value.Get("id").GetString();
        if (topic.id.empty()) continue;
        topic.parentId = value.Get("parentId").GetString();
        topic.text = value.Get("text").GetString();
        topic.side = static_cast<MindMapTopicSide>(value.Get("side").GetInteger(0));
        topic.collapsed = value.Get("collapsed").GetBoolean(false);
        topic.weight = value.Get("weight").GetNumber(1.0);
        topic.iconPath = value.Get("iconPath").GetString();
        topic.imagePath = value.Get("imagePath").GetString();
        topic.note = value.Get("note").GetString();
        topic.link = value.Get("link").GetString();
        topic.floating = value.Get("floating").GetBoolean(false);
        topic.floatX = value.Get("floatX").GetNumber(0.0);
        topic.floatY = value.Get("floatY").GetNumber(0.0);
        if (value.Contains("style")) {
            topic.styleOverride = TopicStyleFromJson(value.Get("style"));
        }

        std::vector<std::string> children;
        const JSONValue& childArray = value.Get("children");
        for (size_t c = 0; c < childArray.GetSize(); ++c) {
            children.push_back(childArray.At(c).GetString());
        }
        childOrder.emplace_back(topic.id, children);

        // Insert directly: AddTopic would reject a forward parent reference.
        topic.childIds.clear();
        model.Topics()[topic.id] = topic;
    }

    for (const auto& [id, children] : childOrder) {
        auto it = model.Topics().find(id);
        if (it == model.Topics().end()) continue;
        for (const auto& childId : children) {
            if (model.Topics().count(childId)) it->second.childIds.push_back(childId);
        }
    }

    // Topics() was populated directly, so rebuild the root, the floating list
    // and the id counter from it.
    model.RestoreStructure(root.Get("rootId").GetString());

    const JSONValue& relationships = root.Get("relationships");
    for (size_t i = 0; i < relationships.GetSize(); ++i) {
        const JSONValue& value = relationships.At(i);
        MindMapRelationship relationship(value.Get("id").GetString(),
                                         value.Get("from").GetString(),
                                         value.Get("to").GetString());
        relationship.label = value.Get("label").GetString();
        relationship.color = ColorFromJson(value.Get("color"), relationship.color);
        relationship.lineWidth = value.Get("lineWidth").GetNumber(relationship.lineWidth);
        relationship.dashed = value.Get("dashed").GetBoolean(relationship.dashed);
        relationship.arrowAtEnd = value.Get("arrowAtEnd").GetBoolean(relationship.arrowAtEnd);
        model.AddRelationship(relationship);
    }

    const JSONValue& layout = root.Get("layout");
    if (layout.IsObject()) {
        layoutOptions.structure = static_cast<MindMapStructure>(
            layout.Get("structure").GetInteger(static_cast<int64_t>(layoutOptions.structure)));
        layoutOptions.balancePolicy = static_cast<MindMapBalancePolicy>(
            layout.Get("balancePolicy").GetInteger(
                static_cast<int64_t>(layoutOptions.balancePolicy)));
        layoutOptions.siblingGap = layout.Get("siblingGap").GetNumber(layoutOptions.siblingGap);
        layoutOptions.levelGap = layout.Get("levelGap").GetNumber(layoutOptions.levelGap);
        layoutOptions.rootGap = layout.Get("rootGap").GetNumber(layoutOptions.rootGap);
        layoutOptions.radialRingGap =
            layout.Get("radialRingGap").GetNumber(layoutOptions.radialRingGap);
        layoutOptions.sizeFromWeight =
            layout.Get("sizeFromWeight").GetBoolean(layoutOptions.sizeFromWeight);
    }

    const JSONValue& view = root.Get("view");
    if (view.IsObject()) {
        if (view.Contains("theme")) {
            ApplyTheme(static_cast<MindMapTheme>(view.Get("theme").GetInteger(0)));
        }
        style.connectorStyle = static_cast<MindMapConnectorStyle>(
            view.Get("connectorStyle").GetInteger(static_cast<int64_t>(style.connectorStyle)));
        style.numbering = static_cast<MindMapNumbering>(
            view.Get("numbering").GetInteger(static_cast<int64_t>(style.numbering)));
        viewport.SetZoomLevel(view.Get("zoom").GetNumber(1.0));
        viewport.SetPanOffset(view.Get("panX").GetNumber(0.0),
                              view.Get("panY").GetNumber(0.0));
    }

    MarkLayoutDirty();
    RequestRedraw();
    return true;
}

// =============================================================================
// MARKDOWN
// =============================================================================

std::string UltraCanvasMindMap::ToMarkdown() const {
    std::ostringstream out;
    const std::string& rootId = model.GetRootId();
    if (rootId.empty()) return std::string();

    std::function<void(const std::string&, int)> walk =
        [&](const std::string& id, int depth) {
            const MindMapTopic* topic = model.GetTopic(id);
            if (!topic) return;
            if (depth == 0) {
                out << "# " << topic->text << "\n\n";
            } else {
                out << std::string((depth - 1) * 2, ' ') << "- " << topic->text << "\n";
            }
            for (const auto& childId : topic->childIds) walk(childId, depth + 1);
        };
    walk(rootId, 0);

    for (const auto& id : model.FloatingTopicIds()) {
        const MindMapTopic* topic = model.GetTopic(id);
        if (topic) out << "\n- " << topic->text << "\n";
    }
    return out.str();
}

bool UltraCanvasMindMap::FromMarkdown(const std::string& markdown) {
    std::vector<std::pair<int, std::string>> outline;
    std::istringstream in(markdown);
    std::string line;

    // Headings set the level directly; list items nest by indentation under the
    // deepest heading seen so far.
    int headingBase = 0;
    while (std::getline(in, line)) {
        std::string trimmed = TrimRight(line);
        if (trimmed.empty()) continue;

        size_t firstNonSpace = trimmed.find_first_not_of(" \t");
        if (firstNonSpace == std::string::npos) continue;
        size_t indent = firstNonSpace;
        std::string content = trimmed.substr(firstNonSpace);

        if (content[0] == '#') {
            size_t hashes = content.find_first_not_of('#');
            if (hashes == std::string::npos) continue;
            int level = static_cast<int>(hashes) - 1;
            std::string text = content.substr(hashes);
            size_t textStart = text.find_first_not_of(" \t");
            if (textStart == std::string::npos) continue;
            outline.emplace_back(level, text.substr(textStart));
            headingBase = level + 1;
            continue;
        }

        if (content[0] == '-' || content[0] == '*' || content[0] == '+') {
            std::string text = content.substr(1);
            size_t textStart = text.find_first_not_of(" \t");
            if (textStart == std::string::npos) continue;
            // Two spaces of indent per nesting level, the markmap convention.
            int level = headingBase + static_cast<int>(indent / 2);
            outline.emplace_back(level, text.substr(textStart));
        }
    }

    if (outline.empty()) return false;

    PushUndo("Import Markdown");
    suppressUndo = true;
    model.Clear();
    selectedTopics.clear();
    primarySelection.clear();
    model.BuildFromOutline(outline);
    suppressUndo = false;

    MarkLayoutDirty();
    pendingAutoFit = true;
    RequestRedraw();
    return true;
}

std::string UltraCanvasMindMap::ToOutlineText() const {
    std::ostringstream out;
    model.ForEachTopic([&](const MindMapTopic& topic) {
        int depth = model.GetDepth(topic.id);
        out << std::string(std::max(0, depth) * 2, ' ') << topic.text << "\n";
    });
    return out.str();
}


// =============================================================================
// INTERCHANGE FORMATS (delegated to UltraCanvasMindMapIO)
// =============================================================================

// Every importer follows the same shape: snapshot for undo, import into a
// scratch model so a failure cannot damage the live map, then adopt it.
bool UltraCanvasMindMap::FromMermaid(const std::string& source) {
    MindMapModel scratch;
    if (!UltraCanvasMindMapIO::FromMermaid(scratch, source)) return false;
    PushUndo("Import Mermaid");
    model = std::move(scratch);
    selectedTopics.clear();
    primarySelection.clear();
    MarkLayoutDirty();
    pendingAutoFit = true;
    RequestRedraw();
    return true;
}

std::string UltraCanvasMindMap::ToMermaid() const {
    return UltraCanvasMindMapIO::ToMermaid(model);
}

bool UltraCanvasMindMap::FromFreeMind(const std::string& xml) {
    MindMapModel scratch;
    if (!UltraCanvasMindMapIO::FromFreeMind(scratch, xml)) return false;
    PushUndo("Import FreeMind");
    model = std::move(scratch);
    selectedTopics.clear();
    primarySelection.clear();
    MarkLayoutDirty();
    pendingAutoFit = true;
    RequestRedraw();
    return true;
}

std::string UltraCanvasMindMap::ToFreeMind() const {
    return UltraCanvasMindMapIO::ToFreeMind(model);
}

bool UltraCanvasMindMap::FromOpml(const std::string& xml) {
    MindMapModel scratch;
    if (!UltraCanvasMindMapIO::FromOpml(scratch, xml)) return false;
    PushUndo("Import OPML");
    model = std::move(scratch);
    selectedTopics.clear();
    primarySelection.clear();
    MarkLayoutDirty();
    pendingAutoFit = true;
    RequestRedraw();
    return true;
}

std::string UltraCanvasMindMap::ToOpml(const std::string& title) const {
    return UltraCanvasMindMapIO::ToOpml(model, title);
}

bool UltraCanvasMindMap::FromIndentedText(const std::string& text, int spacesPerLevel) {
    MindMapModel scratch;
    if (!UltraCanvasMindMapIO::FromIndentedText(scratch, text, spacesPerLevel)) return false;
    PushUndo("Import text outline");
    model = std::move(scratch);
    selectedTopics.clear();
    primarySelection.clear();
    MarkLayoutDirty();
    pendingAutoFit = true;
    RequestRedraw();
    return true;
}

bool UltraCanvasMindMap::FromCsv(const std::string& csv) {
    MindMapModel scratch;
    if (!UltraCanvasMindMapIO::FromCsv(scratch, csv)) return false;
    PushUndo("Import CSV");
    model = std::move(scratch);
    selectedTopics.clear();
    primarySelection.clear();
    MarkLayoutDirty();
    pendingAutoFit = true;
    RequestRedraw();
    return true;
}

std::string UltraCanvasMindMap::ToCsv() const {
    return UltraCanvasMindMapIO::ToCsv(model);
}

bool UltraCanvasMindMap::FromXMindFile(const std::string& filePath) {
    MindMapModel scratch;
    if (!UltraCanvasMindMapIO::FromXMindFile(scratch, filePath)) return false;
    PushUndo("Import XMind");
    model = std::move(scratch);
    selectedTopics.clear();
    primarySelection.clear();
    MarkLayoutDirty();
    pendingAutoFit = true;
    RequestRedraw();
    return true;
}

bool UltraCanvasMindMap::ImportAuto(const std::string& text) {
    switch (UltraCanvasMindMapIO::DetectFormat(text)) {
        case UltraCanvasMindMapIO::Format::Mermaid:      return FromMermaid(text);
        case UltraCanvasMindMapIO::Format::FreeMind:     return FromFreeMind(text);
        case UltraCanvasMindMapIO::Format::Opml:         return FromOpml(text);
        case UltraCanvasMindMapIO::Format::Markdown:     return FromMarkdown(text);
        case UltraCanvasMindMapIO::Format::Csv:          return FromCsv(text);
        case UltraCanvasMindMapIO::Format::Json:         return FromJson(text);
        case UltraCanvasMindMapIO::Format::IndentedText: return FromIndentedText(text);
        case UltraCanvasMindMapIO::Format::Unknown:
        default:                                         return false;
    }
}

MindMapSvgOptions UltraCanvasMindMap::MakeSvgOptions(double scale) const {
    MindMapSvgOptions options;
    options.scale = scale;
    options.fontFamily = style.fontFamily;
    options.backgroundColor = style.backgroundColor;
    options.connectorColor = style.connectorColor;
    options.connectorWidth = style.connectorWidth;
    options.styleOf = [this](const MindMapTopic& topic) { return ResolveTopicStyle(topic); };
    return options;
}

std::string UltraCanvasMindMap::ToSvg(double scale) const {
    return UltraCanvasMindMapIO::ToSvg(model, layoutResult, MakeSvgOptions(scale));
}

bool UltraCanvasMindMap::ExportSvg(const std::string& filePath, double scale) const {
    return UltraCanvasMindMapIO::WriteSvgFile(filePath, model, layoutResult,
                                              MakeSvgOptions(scale));
}


// =============================================================================
// CONTENT INDICATORS (C3, C4, C6) AND CONNECTOR BADGES (R7)
// =============================================================================

Rect2Dd UltraCanvasMindMap::LinkIndicatorRect(const MindMapTopic& topic) const {
    double size = style.markerSize;
    // Bottom-right corner, just inside the node.
    return Rect2Dd(topic.bounds.x + topic.bounds.width - size - 2.0,
                   topic.bounds.y + topic.bounds.height - size - 2.0,
                   size, size);
}

void UltraCanvasMindMap::RenderTopicIndicators(IRenderContext* ctx, const MindMapTopic& topic,
                                               const MindMapTopicStyle& topicStyle) {
    double opacity = TopicOpacity(topic);
    double size = style.markerSize;

    // Markers sit in a row above the node's top-right corner (C3).
    if (style.showMarkers && !topic.markers.empty()) {
        double x = topic.bounds.x + topic.bounds.width - size;
        double y = topic.bounds.y - size - 2.0;
        for (auto it = topic.markers.rbegin(); it != topic.markers.rend(); ++it) {
            ctx->DrawImage(*it, Rect2Dd(x, y, size, size), ImageFitMode::Contain);
            x -= size + 2.0;
        }
    }

    // Note indicator: a small filled corner triangle (C4).
    if (style.showNoteIndicator && !topic.note.empty()) {
        double tip = size * 0.7;
        double right = topic.bounds.x + topic.bounds.width;
        double top = topic.bounds.y;
        Color noteColor = WithAlpha(topicStyle.borderColor,
                                    static_cast<int>(topicStyle.borderColor.a * opacity));
        ctx->SetFillPaint(noteColor);
        ctx->FillLinePath({{right - tip, top}, {right, top}, {right, top + tip}});
    }

    // Link indicator: a small ring with a bar, clickable (C6).
    if (style.showLinkIndicator && !topic.link.empty()) {
        Rect2Dd rect = LinkIndicatorRect(topic);
        Point2Dd centre(rect.x + rect.width * 0.5, rect.y + rect.height * 0.5);
        double radius = rect.width * 0.42;
        Color linkColor = WithAlpha(topicStyle.borderColor,
                                    static_cast<int>(topicStyle.borderColor.a * opacity));
        ctx->SetStrokePaint(linkColor);
        ctx->SetStrokeWidth(1.4);
        ctx->DrawCircle(centre, radius);
        ctx->DrawLine({centre.x - radius * 0.5, centre.y + radius * 0.5},
                      {centre.x + radius * 0.5, centre.y - radius * 0.5});
    }
}

std::string UltraCanvasMindMap::FindLinkIndicatorAt(const Point2Di& localPos) const {
    if (!style.showLinkIndicator) return std::string();
    Point2Dd world = viewport.ScreenToWorld(localPos);
    for (const auto& [id, box] : layoutResult.bounds) {
        const MindMapTopic* topic = model.GetTopic(id);
        if (!topic || topic->link.empty()) continue;
        Rect2Dd rect = LinkIndicatorRect(*topic);
        if (world.x >= rect.x && world.x <= rect.x + rect.width &&
            world.y >= rect.y && world.y <= rect.y + rect.height) {
            return id;
        }
    }
    return std::string();
}

void UltraCanvasMindMap::RenderConnectorBadge(IRenderContext* ctx, const MindMapTopic& child,
                                              const Point2Dd& from, const Point2Dd& to,
                                              const Color& color) {
    double t = std::clamp(style.connectorBadgeT, 0.0, 1.0);
    Point2Dd centre(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t);
    double radius = style.connectorBadgeRadius;

    ctx->SetFillPaint(color);
    ctx->FillCircle(centre, radius);

    // The badge carries the icon when there is one, otherwise the ordinal.
    if (!child.iconPath.empty()) {
        double inner = radius * 1.3;
        ctx->DrawImage(child.iconPath,
                       Rect2Dd(centre.x - inner * 0.5, centre.y - inner * 0.5, inner, inner),
                       ImageFitMode::Contain);
        return;
    }
    if (child.ordinal.empty()) return;

    FontStyle font;
    font.fontFamily = style.fontFamily;
    font.fontSize = radius * 1.1;
    font.fontWeight = FontWeight::Bold;
    ctx->SetFontStyle(font);
    Size2Di textSize = ctx->GetTextLineDimensions(child.ordinal);
    ctx->SetTextPaint(style.connectorBadgeTextColor);
    ctx->DrawText(child.ordinal,
                  {centre.x - textSize.width * 0.5, centre.y - textSize.height * 0.5});
}

// =============================================================================
// CLIPBOARD (I7)
// =============================================================================

namespace {

// Serializes a subtree to JSON, ids and all. Paste re-generates the ids, so
// they only need to be internally consistent.
JSONValue SubtreeToJson(const MindMapModel& model, const std::string& id) {
    const MindMapTopic* topic = model.GetTopic(id);
    JSONValue value = JSONValue::MakeObject();
    if (!topic) return value;

    value.Set("text", JSONValue(topic->text));
    value.Set("collapsed", JSONValue(topic->collapsed));
    value.Set("weight", JSONValue(topic->weight));
    if (!topic->iconPath.empty())  value.Set("iconPath", JSONValue(topic->iconPath));
    if (!topic->imagePath.empty()) value.Set("imagePath", JSONValue(topic->imagePath));
    if (!topic->note.empty())      value.Set("note", JSONValue(topic->note));
    if (!topic->link.empty())      value.Set("link", JSONValue(topic->link));

    JSONValue children = JSONValue::MakeArray();
    for (const auto& childId : topic->childIds) {
        children.Append(SubtreeToJson(model, childId));
    }
    value.Set("children", children);
    return value;
}

// Grafts a serialized subtree under `parentId`, generating fresh ids.
std::string SubtreeFromJson(MindMapModel& model, const JSONValue& value,
                            const std::string& parentId) {
    std::string id = model.AddTopic(parentId, value.Get("text").GetString());
    if (id.empty()) return id;

    if (MindMapTopic* topic = model.GetTopic(id)) {
        topic->collapsed = value.Get("collapsed").GetBoolean(false);
        topic->weight = value.Get("weight").GetNumber(1.0);
        topic->iconPath = value.Get("iconPath").GetString();
        topic->imagePath = value.Get("imagePath").GetString();
        topic->note = value.Get("note").GetString();
        topic->link = value.Get("link").GetString();
    }

    const JSONValue& children = value.Get("children");
    for (size_t i = 0; i < children.GetSize(); ++i) {
        SubtreeFromJson(model, children.At(i), id);
    }
    return id;
}

} // namespace

bool UltraCanvasMindMap::CopySelection() {
    if (selectedTopics.empty()) return false;

    // Only copy the topmost selected topics: a descendant already travels with
    // its ancestor, and copying both would duplicate it on paste.
    std::vector<std::string> roots;
    for (const auto& id : selectedTopics) {
        bool coveredByAncestor = false;
        for (const auto& other : selectedTopics) {
            if (other != id && model.IsDescendantOf(id, other)) {
                coveredByAncestor = true;
                break;
            }
        }
        if (!coveredByAncestor) roots.push_back(id);
    }
    if (roots.empty()) return false;
    std::sort(roots.begin(), roots.end());

    JSONValue clip = JSONValue::MakeObject();
    JSONValue items = JSONValue::MakeArray();
    for (const auto& id : roots) items.Append(SubtreeToJson(model, id));
    clip.Set("subtrees", items);

    clipboardJson = JSON::Serialize(clip);
    return true;
}

bool UltraCanvasMindMap::CutSelection() {
    if (!CopySelection()) return false;
    DeleteSelected();
    return true;
}

bool UltraCanvasMindMap::PasteInto(const std::string& targetParentId) {
    if (clipboardJson.empty()) return false;
    if (!model.GetTopic(targetParentId)) return false;

    JSONParseResult parseResult;
    JSONValue clip = JSON::Parse(clipboardJson, &parseResult);
    if (!parseResult.success) return false;

    const JSONValue& items = clip.Get("subtrees");
    if (items.GetSize() == 0) return false;

    PushUndo("Paste");
    suppressUndo = true;
    std::vector<std::string> pastedIds;
    for (size_t i = 0; i < items.GetSize(); ++i) {
        std::string id = SubtreeFromJson(model, items.At(i), targetParentId);
        if (!id.empty()) pastedIds.push_back(id);
    }
    // Pasting into a collapsed parent would hide the result.
    if (MindMapTopic* parent = model.GetTopic(targetParentId)) parent->collapsed = false;
    suppressUndo = false;

    if (pastedIds.empty()) return false;

    selectedTopics.clear();
    for (const auto& id : pastedIds) selectedTopics.insert(id);
    primarySelection = pastedIds.front();

    MarkLayoutDirty();
    NotifySelectionChanged();
    for (const auto& id : pastedIds) {
        if (onTopicAdded) onTopicAdded(id);
    }
    RequestRedraw();
    return true;
}

bool UltraCanvasMindMap::PasteOutlineInto(const std::string& targetParentId,
                                          const std::string& outlineText) {
    if (!model.GetTopic(targetParentId)) return false;

    // Reuse the text importer's rules by building a scratch map, then graft it.
    MindMapModel scratch;
    if (!UltraCanvasMindMapIO::FromIndentedText(scratch, outlineText)) return false;
    if (scratch.GetRootId().empty()) return false;

    JSONValue subtree = SubtreeToJson(scratch, scratch.GetRootId());

    PushUndo("Paste outline");
    suppressUndo = true;
    std::string id = SubtreeFromJson(model, subtree, targetParentId);
    if (MindMapTopic* parent = model.GetTopic(targetParentId)) parent->collapsed = false;
    suppressUndo = false;

    if (id.empty()) return false;
    SelectTopic(id);
    MarkLayoutDirty();
    if (onTopicAdded) onTopicAdded(id);
    RequestRedraw();
    return true;
}

} // namespace UltraCanvas
