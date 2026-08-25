// Apps/UltraCleaner/ui/UltraCleanerHomeView.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCleanerHomeView.h"

#include "UltraCleanerTypes.h"

#include "Plugins/Charts/UltraCanvasCircularProgressChart.h"

#include <algorithm>
#include <cstdio>
#include <string>

using namespace UltraCanvas;

namespace UltraCleaner {
namespace {

constexpr float kChartSize = 138.0f;
constexpr float kCardWidth = 178.0f;

// Green while there is room, amber when it is getting tight, red when the
// disk is nearly full. The colour is the whole point of the graphic: it
// answers "should I care?" before any number is read.
Color FillColourFor(double usedPercent) {
    if (usedPercent >= 90.0) return Color(198, 60, 60);
    if (usedPercent >= 75.0) return Color(214, 152, 46);
    return Color(70, 150, 96);
}

std::string VerdictFor(const std::vector<VolumeInfo>& volumes) {
    if (volumes.empty()) return "No drives could be read.";
    const VolumeInfo* worst = &volumes.front();
    for (const auto& volume : volumes) {
        if (volume.UsedPercent() > worst->UsedPercent()) worst = &volume;
    }
    if (worst->UsedPercent() >= 90.0) {
        return worst->name + " is nearly full — " +
               FormatByteSize(worst->freeBytes) + " left. Worth cleaning now.";
    }
    if (worst->UsedPercent() >= 75.0) {
        return worst->name + " is filling up — " +
               FormatByteSize(worst->freeBytes) + " left.";
    }
    return "There is plenty of room on every drive. Nothing here is urgent.";
}

} // namespace

std::shared_ptr<UltraCanvasContainer> HomeView::Build(float width, float height) {
    root_ = CreateContainer("ucHomeRoot", 0, 0, width, height);
    root_->layout.SetFlexColumn()
                 .SetFlexGap(10)
                 .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    root_->SetPadding(14);

    auto heading = CreateLabel("ucHomeHeading", 0, 0, 600, 28, "Your drives");
    heading->SetFontSize(16);
    root_->AddChild(heading);

    driveRow_ = CreateContainer("ucHomeDrives", 0, 0, width - 32, kChartSize + 96);
    driveRow_->layout.SetFlexRow()
                     .SetFlexGap(14)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Start);
    root_->AddChild(driveRow_);
    driveRow_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    driveRow_->layoutItem.SetFlexShrink(0);

    verdictLabel_ = CreateLabel("ucHomeVerdict", 0, 0, width - 40, 26, "");
    verdictLabel_->SetWrap(TextWrap::WrapWord);
    verdictLabel_->SetElementSize(CSSLayout::Dimension::Auto(),
                                  CSSLayout::Dimension::Auto());
    root_->AddChild(verdictLabel_);
    verdictLabel_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    verdictLabel_->layoutItem.SetFlexShrink(0);

    auto actionsHeading = CreateLabel("ucHomeActionsHeading", 0, 0, 600, 26,
                                      "What would you like to do?");
    actionsHeading->SetFontSize(14);
    root_->AddChild(actionsHeading);

    // Two ways in, described by what they do rather than by what they are.
    auto actions = CreateContainer("ucHomeActions", 0, 0, width - 32, 96);
    actions->layout.SetFlexRow()
                   .SetFlexGap(14)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Start);

    auto makeAction = [&](const std::string& id, const std::string& title,
                          const std::string& explanation,
                          std::function<void()>* slot) {
        auto card = CreateContainer(id, 0, 0, 300, 92);
        // Block, for the same reason the category list is: the explanation
        // under the button wraps, and only the block path measures a wrapped
        // label against a definite width. Under flex it reported one line and
        // the rest was clipped.
        card->layout.SetDisplay(CSSLayout::DisplayType::Block);
        card->SetElementSize(CSSLayout::Dimension::Px(300),
                             CSSLayout::Dimension::Auto());
        ContainerStyle plainCard;
        plainCard.autoShowScrollbars = false;
        card->SetContainerStyle(plainCard);
        auto button = CreateButton(id + "Btn", 0, 0, 290, 34, title);
        button->onClick = [slot]() { if (*slot) (*slot)(); };
        card->AddChild(button);
        auto text = CreateLabel(id + "Text", 0, 0, 290, 46, explanation);
        text->SetWrap(TextWrap::WrapWord);
        text->SetFontSize(11);
        // Height from the wrapped text, not from a guess: a taller font or a
        // narrower card turns two lines into three, and a fixed box clips the
        // rest.
        text->SetElementSize(CSSLayout::Dimension::Auto(),
                             CSSLayout::Dimension::Auto());
        card->AddChild(text);
        text->layoutItem.SetFlexShrink(0);
        actions->AddChild(card);
        card->layoutItem.SetFlexShrink(0);
    };

    makeAction("ucHomeJunk", "Clean system junk",
               "Temporary files, caches, logs, crash reports, package "
               "downloads and the trash. Nothing is removed until you say so.",
               &onCleanSystemJunk);
    makeAction("ucHomePhotos", "Find duplicate photos",
               "Groups pictures that are the same, or shots of the same "
               "moment, and keeps one of each.",
               &onCleanPhotos);

    root_->AddChild(actions);
    actions->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    actions->layoutItem.SetFlexShrink(0);

    Refresh();
    return root_;
}

void HomeView::Refresh() {
    volumes_ = ListVolumes();
    RebuildDrives();
    if (verdictLabel_) verdictLabel_->SetText(VerdictFor(volumes_));
}

void HomeView::RebuildDrives() {
    if (!driveRow_) return;
    driveRow_->ClearChildren();

    if (volumes_.empty()) {
        driveRow_->AddChild(CreateLabel("ucHomeNoDrives", 0, 0, 400, 24,
                                        "No drives could be read."));
        return;
    }

    int index = 0;
    for (const auto& volume : volumes_) {
        const std::string id = "ucHomeDrive" + std::to_string(index++);

        auto card = CreateContainer(id, 0, 0, kCardWidth, kChartSize + 92);
        card->layout.SetFlexColumn()
                    .SetFlexGap(2)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Center);

        auto pie = CreateProgressPieChart(
            id + "Pie", 0, 0, static_cast<int>(kChartSize),
            static_cast<int>(kChartSize), volume.name, volume.UsedPercent(),
            FillColourFor(volume.UsedPercent()));
        card->AddChild(pie);
        pie->layoutItem.SetFlexShrink(0);

        // The chart draws its own caption inside the circle, where a chart
        // this small has no room for it, so the drive is named underneath
        // instead — and every line is kept short enough to survive the card
        // width, because a truncated "93% u…" defeats showing a number.
        auto name = CreateLabel(id + "Name", 0, 0, kCardWidth, 22, volume.name);
        name->SetFontSize(13);
        name->SetAlignment(TextAlignment::Center);
        card->AddChild(name);

        char percent[16];
        std::snprintf(percent, sizeof percent, "%.0f%% used",
                      volume.UsedPercent());
        auto usage = CreateLabel(id + "Usage", 0, 0, kCardWidth, 20, percent);
        usage->SetFontSize(12);
        usage->SetAlignment(TextAlignment::Center);
        card->AddChild(usage);

        auto free = CreateLabel(id + "Free", 0, 0, kCardWidth, 18,
                                FormatByteSize(volume.freeBytes) + " free of " +
                                    FormatByteSize(volume.totalBytes));
        free->SetFontSize(10);
        free->SetAlignment(TextAlignment::Center);
        free->SetTextColor(Color(120, 120, 120));
        card->AddChild(free);

        auto mount = CreateLabel(id + "Mount", 0, 0, kCardWidth, 18,
                                 volume.mountPoint + "  ·  " + volume.filesystem);
        mount->SetFontSize(10);
        mount->SetAlignment(TextAlignment::Center);
        mount->SetTextColor(Color(150, 150, 150));
        card->AddChild(mount);

        driveRow_->AddChild(card);
        card->layoutItem.SetFlexShrink(0);
    }
}

} // namespace UltraCleaner
