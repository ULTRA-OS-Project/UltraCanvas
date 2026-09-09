// Apps/EmailCleaner/ui/EmailCleanerMapView.cpp
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "EmailCleanerMapView.h"

#include <string>
#include <utility>
#include <vector>

using namespace UltraCanvas;

namespace EmailCleaner {

namespace {

constexpr float kControlsH = 34.0f;
constexpr float kLegendH   = 30.0f;
constexpr float kSummaryH  = 22.0f;

Color ToCanvasColor(const MapColor& color) {
    return Color(color.r, color.g, color.b, 255);
}

// The block's caption. A domain shows its name; a sender shows its display
// name, falling back to the address.
std::string BlockLabel(const MapNode& node) {
    return node.label.empty() ? node.key : node.label;
}

} // namespace

std::shared_ptr<UltraCanvasContainer> MapView::Build(float x, float y,
                                                     float width, float height) {
    container_ = CreateContainer("ecMapView", x, y, width, height);

    // ---- Controls ----------------------------------------------------------
    container_->AddChild(CreateLabel("ecMapMetricLabel", 8, 8, 60, 20, "Size by"));

    metricPicker_ = CreateSegmentedControl("ecMapMetric", 72, 4, 420, kControlsH - 8);
    metricPicker_->AddSegment("Messages");
    metricPicker_->AddSegment("Total size");
    metricPicker_->AddSegment("Attachments");
    metricPicker_->AddSegment("Unwanted");
    metricPicker_->SetSelectedIndex(0);
    metricPicker_->onSegmentSelected = [this](int index) {
        switch (index) {
            case 1:  metric_ = SenderMetric::TotalBytes; break;
            case 2:  metric_ = SenderMetric::AttachmentBytes; break;
            case 3:  metric_ = SenderMetric::UnwantedCount; break;
            default: metric_ = SenderMetric::MessageCount; break;
        }
        RebuildTreeMap();
    };
    container_->AddChild(metricPicker_);

    container_->AddChild(CreateLabel("ecMapGroupLabel", 508, 8, 50, 20, "Group"));

    // Two segments of "By domain" width: 100px each clipped the caption onto a
    // second line, which reads as a broken control.
    groupingPicker_ = CreateSegmentedControl("ecMapGrouping", 562, 4, 240, kControlsH - 8);
    groupingPicker_->AddSegment("By domain");
    groupingPicker_->AddSegment("By sender");
    groupingPicker_->SetSelectedIndex(0);
    groupingPicker_->onSegmentSelected = [this](int index) {
        shape_.groupByDomain = (index == 0);
        // A flat map has room for many more blocks than a nested one.
        shape_.maxSendersPerDomain = shape_.groupByDomain ? 12 : 60;
        RebuildTreeMap();
    };
    container_->AddChild(groupingPicker_);

    // ---- The map itself ----------------------------------------------------
    const float mapY = kControlsH + 4;
    const float mapH = height - mapY - kLegendH - kSummaryH - 8;

    treeMap_ = CreateTreeMap("ecMapTreeMap", 8, static_cast<int>(mapY),
                             static_cast<int>(width - 16), static_cast<int>(mapH));
    treeMap_->SetLayoutAlgorithm(TreeMapAlgorithm::Squarified);
    treeMap_->SetVisualStyle(TreeMapStyle::Raised);
    treeMap_->SetDisplayOptions(/*labels=*/true, /*values=*/true,
                                /*percentages=*/false, /*icons=*/false);
    treeMap_->SetPadding(3.0);
    treeMap_->SetMinimumRectangleSize(14.0);
    treeMap_->SetBorderProperties(1.5, Color(255, 255, 255, 220));

    // A click reports the block's name; map it back to sender/domain.
    treeMap_->onNodeSelect = [this](const std::string& name, double) {
        auto it = blockKeys_.find(name);
        if (it == blockKeys_.end()) return;
        if (summary_) {
            summary_->SetText(it->second.first.empty()
                                  ? ("Domain " + it->second.second)
                                  : it->second.first);
        }
        if (onBlockSelected) onBlockSelected(it->second.first, it->second.second);
    };
    treeMap_->onNodeDrillDown = [this](const std::string& name) {
        auto it = blockKeys_.find(name);
        if (it != blockKeys_.end() && onBlockSelected)
            onBlockSelected(std::string(), it->second.second);
    };
    treeMap_->onNodeReset = [this]() {
        if (onBlockSelected) onBlockSelected(std::string(), std::string());
    };
    container_->AddChild(treeMap_);

    // ---- Legend and summary ------------------------------------------------
    legend_ = CreateContainer("ecMapLegend", 8, mapY + mapH + 4, width - 16, kLegendH);
    container_->AddChild(legend_);

    summary_ = CreateLabel("ecMapSummary", 8, mapY + mapH + kLegendH + 4,
                           width - 16, kSummaryH,
                           "Click a block to see who it is; double-click a domain to open it.");
    container_->AddChild(summary_);
    return container_;
}

void MapView::Refresh(const MessageFilter& filter) {
    filter_ = filter;
    RebuildTreeMap();
    RebuildLegend();
}

std::shared_ptr<TreeMapNode> MapView::ToTreeMapNode(const MapNode& node) {
    auto out = std::make_shared<TreeMapNode>(BlockLabel(node), node.value);
    out->backgroundColor = ToCanvasColor(node.color);
    out->description     = node.tooltip;
    // The element identifies nodes by name, so remember which data each
    // caption stands for. A duplicate caption (the same display name under two
    // domains) keeps the first mapping, which is the larger block.
    blockKeys_.emplace(out->name, std::make_pair(node.IsLeaf() ? node.key : std::string(),
                                                 node.IsLeaf() ? node.block.domain : node.key));
    for (const MapNode& child : node.children)
        out->AddChild(ToTreeMapNode(child));
    return out;
}

void MapView::RebuildTreeMap() {
    if (!analytics_ || !treeMap_) return;

    root_ = MapNode{};
    blockKeys_.clear();
    if (!analytics_->BuildSenderMap(filter_, metric_, shape_, root_)) {
        if (summary_) summary_->SetText("Could not read the analysis database.");
        return;
    }

    if (root_.children.empty()) {
        // An empty root still has to be handed over, or the element keeps
        // drawing the previous mailbox.
        treeMap_->SetRootNode(std::make_shared<TreeMapNode>("No senders yet", 0.0));
        if (summary_) {
            summary_->SetText("Nothing analysed yet — press \"Load mail\" to read the "
                              "messages UltraMail has cached.");
        }
        return;
    }

    auto rootNode = std::make_shared<TreeMapNode>("All senders", 0.0);
    for (const MapNode& child : root_.children)
        rootNode->AddChild(ToTreeMapNode(child));
    treeMap_->SetRootNode(rootNode);

    if (summary_) {
        summary_->SetText(std::to_string(root_.children.size()) +
                          (shape_.groupByDomain ? " domains" : " senders") +
                          " · sized by " + ToString(metric_) +
                          " · click a block for its detail, double-click to open it.");
    }
}

void MapView::RebuildLegend() {
    if (!analytics_ || !legend_) return;
    legend_->ClearChildren();

    std::vector<CategoryTotal> totals;
    if (!analytics_->BuildCategoryLegend(filter_, totals)) return;

    float x = 0.0f;
    int index = 0;
    for (const CategoryTotal& total : totals) {
        const std::string id = "ecLegend" + std::to_string(index++);
        // A colour chip: a label with the category's colour as its background
        // is the element the framework already has for a coloured swatch.
        auto chip = CreateLabel(id + "Chip", x, 6, 14, 14, "");
        chip->SetBackgroundColor(ToCanvasColor(Analytics::CategoryColor(total.category)));
        legend_->AddChild(chip);
        x += 18.0f;

        const std::string text = CategoryLabel(total.category) + " (" +
                                 std::to_string(total.messageCount) + ")";
        // Widths are estimated from the character count rather than measured:
        // the legend is built before layout, so no render context is available
        // to ask. Erring generous costs a little space; erring tight ellipsises
        // the category name, which is the one thing the legend is for.
        const float textWidth = 12.0f + 8.5f * static_cast<float>(text.size());
        legend_->AddChild(CreateLabel(id + "Text", x, 4, textWidth, 18, text));
        x += textWidth + 8.0f;
    }
}

} // namespace EmailCleaner
