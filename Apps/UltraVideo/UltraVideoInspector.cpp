// Apps/UltraVideo/UltraVideoInspector.cpp
// The inspector's three tabs (UltraVideoInspector.h).
// Version: 0.4.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework
#include "UltraVideoInspector.h"
#include "UltraVideoMapping.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasVideoTimelineModel.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

using namespace UltraCanvas;
using namespace VideoFX;

namespace UltraVideo {

namespace {

constexpr int kPageWidth = 276;
constexpr int kCaptionWidth = 96;
constexpr int kFieldWidth = 160;

std::string Seconds(double s, int decimals = 1) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(decimals) << s << " s";
    return out.str();
}

std::string Times(double speed) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(2) << speed << "\xC3\x97";
    return out.str();
}

struct SizeChoice { const char* label; int width; int height; };
const SizeChoice kSizes[] = {
    {"From the first clip", 0, 0}, {"1920 \xC3\x97 1080 (Full HD)", 1920, 1080}, {"1280 \xC3\x97 720 (HD)", 1280, 720},
    {"3840 \xC3\x97 2160 (4K)", 3840, 2160}, {"1080 \xC3\x97 1920 (portrait)", 1080, 1920}, {"1080 \xC3\x97 1080 (square)", 1080, 1080},
};
const double kRates[] = {0.0, 24.0, 25.0, 29.97, 30.0, 50.0, 60.0};
const char* kRateLabels[] = {"From the first clip", "24", "25", "29.97", "30", "50", "60"};
const VideoFXMotionStyle kMotions[] = {VideoFXMotionStyle::Auto, VideoFXMotionStyle::Still, VideoFXMotionStyle::ZoomIn,
                                       VideoFXMotionStyle::ZoomOut, VideoFXMotionStyle::PanLeft, VideoFXMotionStyle::PanRight,
                                       VideoFXMotionStyle::PanUp, VideoFXMotionStyle::PanDown};
const char* kMotionLabels[] = {"Auto", "Still", "Zoom in", "Zoom out", "Pan left", "Pan right", "Pan up", "Pan down"};
const VideoFXImageFit kFits[] = {VideoFXImageFit::Auto, VideoFXImageFit::Cover, VideoFXImageFit::Contain, VideoFXImageFit::BlurredBackground};
const char* kFitLabels[] = {"Auto", "Fill the frame", "Whole picture", "Blurred background"};
const VideoFXDuckingPreset kDucking[] = {VideoFXDuckingPreset::Speech, VideoFXDuckingPreset::Outdoor, VideoFXDuckingPreset::LoudEvent};
const char* kDuckingLabels[] = {"Speech", "Outdoor", "Loud event"};

const VideoFXAnchor kAnchors[] = {VideoFXAnchor::TopLeft, VideoFXAnchor::Top, VideoFXAnchor::TopRight, VideoFXAnchor::Left,
                                  VideoFXAnchor::Center, VideoFXAnchor::Right, VideoFXAnchor::BottomLeft, VideoFXAnchor::Bottom,
                                  VideoFXAnchor::BottomRight};
const char* kAnchorLabels[] = {"Top left", "Top", "Top right", "Left", "Centre", "Right", "Bottom left", "Bottom", "Bottom right"};
struct NamedColor { const char* label; uint32_t rgb; };
const NamedColor kColors[] = {{"White", 0xFFFFFF}, {"Black", 0x000000}, {"Amber", 0xF2A53A}, {"Red", 0xD23420},
                              {"Green", 0x2F7A4D}, {"Blue", 0x0B6AA6}, {"Yellow", 0xFFE08A}};

template <typename T, size_t N>
int IndexIn(const T (&values)[N], T value) {
    for (size_t i = 0; i < N; ++i) if (values[i] == value) return static_cast<int>(i);
    return 0;
}

} // namespace

// ============================================================================
// building
// ============================================================================
std::shared_ptr<UltraCanvasContainer> Inspector::Page(const std::string& id) {
    auto page = std::make_shared<UltraCanvasContainer>(id, 0, 0, kPageWidth + 24, 600);
    page->layout.SetFlexColumn().SetFlexGap(static_cast<int>(Tokens::Space2)).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    page->SetPadding(static_cast<float>(Tokens::Space3));
    // A form taller than the pane scrolls, down only
    ContainerStyle scroll = page->GetContainerStyle();
    scroll.autoShowScrollbars = true;
    scroll.autoShowHorizontalScrollbar = false;
    page->SetContainerStyle(scroll);
    return page;
}

std::shared_ptr<UltraCanvasLabel> Inspector::Heading(const std::shared_ptr<Container>& page, const std::string& id,
                                                     const std::string& text) {
    auto label = CreateLabel(id, 0, 0, kPageWidth, 22, text);
    label->SetFont(Tokens::Title.family, Tokens::Title.size * 0.75f, FontWeight::Bold);
    label->layoutItem.SetFlexShrink(0);
    page->AddChild(label);
    headings.push_back(label);
    return label;
}

std::shared_ptr<UltraCanvasContainer> Inspector::Row(const std::shared_ptr<Container>& page, const std::string& id,
                                                     const std::string& caption,
                                                     std::shared_ptr<UltraCanvasUIElement> field) {
    auto row = std::make_shared<UltraCanvasContainer>(id, 0, 0, kPageWidth, static_cast<int>(Tokens::ControlH) + 4);
    row->layout.SetFlexRow().SetFlexGap(static_cast<int>(Tokens::Space2)).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->layoutItem.SetFlexShrink(0);
    auto label = CreateLabel(id + ".caption", 0, 0, kCaptionWidth, static_cast<int>(Tokens::ControlH), caption);
    label->SetFont(Tokens::Label.family, Tokens::Label.size * 0.75f);
    label->layoutItem.SetFlexShrink(0);
    captions.push_back(label);
    row->AddChild(label);
    field->layoutItem.SetFlexShrink(0);
    row->AddChild(field);
    page->AddChild(row);
    return row;
}

void Inspector::Edit(const std::string& label, std::function<void(Document&)> edit) {
    if (updating) return;
    if (onEdit) onEdit(label, std::move(edit));
}

std::shared_ptr<UltraCanvasTabbedContainer> Inspector::Build(const Palette& p) {
    tabs = std::make_shared<UltraCanvasTabbedContainer>("uv.inspector", 0, 0, kPageWidth + 24, 600);
    tabs->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    tabs->SetCloseMode(TabCloseMode::NoClose);
    tabs->SetTabMinWidth(60);
    tabs->SetTabStyle(TabStyle::Flat);
    const int ctl = static_cast<int>(Tokens::ControlH);

    // ---- Clip ----
    clipPage = Page("uv.insp.clip");
    clipEmpty = std::make_shared<UltraCanvasContainer>("uv.insp.clip.empty", 0, 0, kPageWidth, 60);
    clipEmpty->layout.SetFlexColumn();
    auto emptyText = CreateLabel("uv.insp.clip.emptyText", 0, 0, kPageWidth, 40, "Select a clip on the timeline to edit it.");
    emptyText->SetFont(Tokens::Body.family, Tokens::Body.size * 0.75f);
    captions.push_back(emptyText);
    clipEmpty->AddChild(emptyText);
    clipPage->AddChild(clipEmpty);

    // Tall enough for the longest form (a title's); the page scrolls when
    // the pane is shorter. Sections keep their size rather than shrinking,
    // so the page sees the overflow and shows its scrollbar.
    clipFields = std::make_shared<UltraCanvasContainer>("uv.insp.clip.fields", 0, 0, kPageWidth, 540);
    clipFields->layout.SetFlexColumn().SetFlexGap(static_cast<int>(Tokens::Space2)).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    clipFields->layoutItem.SetFlexShrink(0);
    clipName = Heading(clipFields, "uv.insp.clip.name", "");
    clipKind = CreateLabel("uv.insp.clip.kind", 0, 0, kPageWidth, 18, "");
    clipKind->SetFont(Tokens::Caption.family, Tokens::Caption.size * 0.75f);
    captions.push_back(clipKind);
    clipFields->AddChild(clipKind);

    fileRows = std::make_shared<UltraCanvasContainer>("uv.insp.clip.file", 0, 0, kPageWidth, 140);
    fileRows->layout.SetFlexColumn().SetFlexGap(static_cast<int>(Tokens::Space2)).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    fileRows->layoutItem.SetFlexShrink(0);
    inField = CreateTimecodeField("uv.insp.in", 0, 0, 120, ctl);
    inField->onCommit = [this](long long frames) {
        const double seconds = frames / documentFrameRate;
        const int index = segment;
        Edit("Set in point", [index, seconds](Document& d) {
            if (index < 0 || static_cast<size_t>(index) >= d.project.segments.size()) return;
            VideoFXSegment& s = d.project.segments[static_cast<size_t>(index)];
            double end = s.end;
            if (end <= 0.0) { const MediaItem* m = d.FindMedia(s.path); end = m ? m->duration : 0.0; }
            s.start = std::max(0.0, std::min(seconds, end > 0.0 ? end - 0.04 : seconds));
        });
    };
    timecodes.push_back(inField);
    Row(fileRows, "uv.insp.row.in", "In", inField);
    outField = CreateTimecodeField("uv.insp.out", 0, 0, 120, ctl);
    outField->onCommit = [this](long long frames) {
        const double seconds = frames / documentFrameRate;
        const int index = segment;
        Edit("Set out point", [index, seconds](Document& d) {
            if (index < 0 || static_cast<size_t>(index) >= d.project.segments.size()) return;
            VideoFXSegment& s = d.project.segments[static_cast<size_t>(index)];
            const MediaItem* m = d.FindMedia(s.path);
            double out = std::max(s.start + 0.04, seconds);
            if (m && m->duration > 0.0) out = std::min(out, m->duration);
            s.end = out;
        });
    };
    timecodes.push_back(outField);
    Row(fileRows, "uv.insp.row.out", "Out", outField);
    speed = CreateSlider("uv.insp.speed", 0, 0, 110, ctl);
    speed->SetRange(0.25f, 4.0f);
    speed->SetStep(0.05f);
    speed->SetValueDisplay(SliderValueDisplay::NoDisplay);
    speed->onValueChanged = [this](float v) {
        if (speedValue) speedValue->SetText(Times(v));
        const int index = segment;
        const double value = std::clamp(static_cast<double>(v), 0.25, 4.0);
        Edit("Set speed", [index, value](Document& d) {
            if (index >= 0 && static_cast<size_t>(index) < d.project.segments.size())
                d.project.segments[static_cast<size_t>(index)].speed = value;
        });
    };
    sliders.push_back(speed);
    auto speedRow = Row(fileRows, "uv.insp.row.speed", "Speed", speed);
    speedValue = CreateLabel("uv.insp.speedValue", 0, 0, 44, ctl, "1.00\xC3\x97");
    speedValue->SetFont(Tokens::Timecode.family, Tokens::Timecode.size * 0.75f);
    captions.push_back(speedValue);
    speedRow->AddChild(speedValue);
    clipFields->AddChild(fileRows);

    stillLength = CreateTimecodeField("uv.insp.still", 0, 0, 120, ctl);
    stillLength->onCommit = [this](long long frames) {
        const double seconds = std::max(0.04, frames / documentFrameRate);
        const int index = segment;
        Edit("Set length", [index, seconds](Document& d) {
            if (index >= 0 && static_cast<size_t>(index) < d.project.segments.size())
                d.project.segments[static_cast<size_t>(index)].duration = seconds;
        });
    };
    timecodes.push_back(stillLength);
    stillRow = Row(clipFields, "uv.insp.row.still", "Length", stillLength);

    clipDuration = CreateLabel("uv.insp.clip.duration", 0, 0, kPageWidth, 18, "");
    clipDuration->SetFont(Tokens::Caption.family, Tokens::Caption.size * 0.75f);
    captions.push_back(clipDuration);
    clipFields->AddChild(clipDuration);

    mute = UltraCanvasSwitch::Create("uv.insp.mute", 0, 0, "Mute", false);
    mute->onStateChanged = [this](CheckedState, CheckedState now) {
        const int index = segment;
        const bool on = now == CheckedState::Checked;
        Edit(on ? "Mute clip" : "Unmute clip", [index, on](Document& d) {
            if (index >= 0 && static_cast<size_t>(index) < d.project.segments.size())
                d.project.segments[static_cast<size_t>(index)].mute = on;
        });
    };
    mute->layoutItem.SetFlexShrink(0);
    switches.push_back(mute);
    clipFields->AddChild(mute);

    imageRows = std::make_shared<UltraCanvasContainer>("uv.insp.clip.image", 0, 0, kPageWidth, 110);
    imageRows->layout.SetFlexColumn().SetFlexGap(static_cast<int>(Tokens::Space2)).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    imageRows->layoutItem.SetFlexShrink(0);
    motion = CreateDropdown("uv.insp.motion", 0, 0, kFieldWidth, ctl);
    for (const char* l : kMotionLabels) motion->AddItem(l);
    motion->onSelectionChanged = [this](int i, const DropdownItem&) {
        const int index = segment;
        const VideoFXMotionStyle style = kMotions[std::clamp(i, 0, 7)];
        Edit("Set motion", [index, style](Document& d) {
            if (index >= 0 && static_cast<size_t>(index) < d.project.segments.size())
                d.project.segments[static_cast<size_t>(index)].motion = VideoFXImageMotion::Make(style);
        });
    };
    dropdowns.push_back(motion);
    Row(imageRows, "uv.insp.row.motion", "Motion", motion);
    imageFit = CreateDropdown("uv.insp.fit", 0, 0, kFieldWidth, ctl);
    for (const char* l : kFitLabels) imageFit->AddItem(l);
    imageFit->onSelectionChanged = [this](int i, const DropdownItem&) {
        const int index = segment;
        const VideoFXImageFit f = kFits[std::clamp(i, 0, 3)];
        Edit("Set framing", [index, f](Document& d) {
            if (index >= 0 && static_cast<size_t>(index) < d.project.segments.size())
                d.project.segments[static_cast<size_t>(index)].imageFit = f;
        });
    };
    dropdowns.push_back(imageFit);
    Row(imageRows, "uv.insp.row.fit", "Framing", imageFit);
    keepFaces = UltraCanvasSwitch::Create("uv.insp.faces", 0, 0, "Keep faces in shot", true);
    keepFaces->onStateChanged = [this](CheckedState, CheckedState now) {
        const int index = segment;
        const bool on = now == CheckedState::Checked;
        Edit("Keep faces in shot", [index, on](Document& d) {
            if (index >= 0 && static_cast<size_t>(index) < d.project.segments.size())
                d.project.segments[static_cast<size_t>(index)].keepFacesInView = on;
        });
    };
    keepFaces->layoutItem.SetFlexShrink(0);
    switches.push_back(keepFaces);
    imageRows->AddChild(keepFaces);
    clipFields->AddChild(imageRows);

    // ---- a title span ----
    titleRows = std::make_shared<UltraCanvasContainer>("uv.insp.clip.title", 0, 0, kPageWidth, 320);
    titleRows->layout.SetFlexColumn().SetFlexGap(static_cast<int>(Tokens::Space2)).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    titleRows->layoutItem.SetFlexShrink(0);
    titleText = CreateTextInput("uv.insp.titleText", 0, 0, kPageWidth, ctl);
    titleText->SetPlaceholder("The title's text");
    titleText->onEnterPressed = [this](const std::string& text) {
        const int index = title;
        Edit("Change title text", [index, text](Document& d) {
            if (index >= 0 && static_cast<size_t>(index) < d.titles.size()) d.titles[static_cast<size_t>(index)].look.text = text;
        });
        return true;
    };
    titleText->layoutItem.SetFlexShrink(0);
    titleRows->AddChild(titleText);
    titleLength = CreateTimecodeField("uv.insp.titleLength", 0, 0, 120, ctl);
    titleLength->onCommit = [this](long long frames) {
        const int index = title;
        const double seconds = std::max(0.04, frames / documentFrameRate);
        Edit("Set title length", [index, seconds](Document& d) {
            if (index >= 0 && static_cast<size_t>(index) < d.titles.size()) d.titles[static_cast<size_t>(index)].duration = seconds;
        });
    };
    timecodes.push_back(titleLength);
    Row(titleRows, "uv.insp.row.titleLength", "Length", titleLength);
    titleStyle = CreateDropdown("uv.insp.titleStyle", 0, 0, kFieldWidth, ctl);
    for (const TitleStyle& st : TitleStyles()) titleStyle->AddItem(st.label);
    titleStyle->onSelectionChanged = [this](int i, const DropdownItem&) {
        const int index = title;
        const std::string key = TitleStyles()[static_cast<size_t>(std::clamp<int>(i, 0, static_cast<int>(TitleStyles().size()) - 1))].key;
        Edit("Set title style", [index, key](Document& d) {
            if (index < 0 || static_cast<size_t>(index) >= d.titles.size()) return;
            const TitleStyle* st = FindTitleStyle(key);
            if (!st) return;
            TitleSpan& t = d.titles[static_cast<size_t>(index)];
            const std::string text = t.look.text;
            t.look = st->look;
            t.look.text = text;
            t.fadeIn = st->fadeIn;
            t.fadeOut = st->fadeOut;
            t.style = st->key;
        });
    };
    dropdowns.push_back(titleStyle);
    Row(titleRows, "uv.insp.row.titleStyle", "Style", titleStyle);
    titleAnchor = CreateDropdown("uv.insp.titleAnchor", 0, 0, kFieldWidth, ctl);
    for (const char* l : kAnchorLabels) titleAnchor->AddItem(l);
    titleAnchor->onSelectionChanged = [this](int i, const DropdownItem&) {
        const int index = title;
        const VideoFXAnchor anchor = kAnchors[std::clamp(i, 0, 8)];
        Edit("Place title", [index, anchor](Document& d) {
            if (index >= 0 && static_cast<size_t>(index) < d.titles.size()) d.titles[static_cast<size_t>(index)].look.anchor = anchor;
        });
    };
    dropdowns.push_back(titleAnchor);
    Row(titleRows, "uv.insp.row.titleAnchor", "Place", titleAnchor);
    titleSize = CreateSlider("uv.insp.titleSize", 0, 0, 110, ctl);
    titleSize->SetRange(0.02f, 0.3f);
    titleSize->SetStep(0.005f);
    titleSize->SetValueDisplay(SliderValueDisplay::NoDisplay);
    titleSize->onValueChanged = [this](float v) {
        if (titleSizeValue) titleSizeValue->SetText(std::to_string(static_cast<int>(std::lround(v * 100))) + " %");
        const int index = title;
        const double size = std::clamp(static_cast<double>(v), 0.02, 0.3);
        Edit("Set title size", [index, size](Document& d) {
            if (index >= 0 && static_cast<size_t>(index) < d.titles.size()) d.titles[static_cast<size_t>(index)].look.fontSize = size;
        });
    };
    sliders.push_back(titleSize);
    auto sizeRow = Row(titleRows, "uv.insp.row.titleSize", "Size", titleSize);
    titleSizeValue = CreateLabel("uv.insp.titleSizeValue", 0, 0, 44, ctl, "");
    titleSizeValue->SetFont(Tokens::Timecode.family, Tokens::Timecode.size * 0.75f);
    captions.push_back(titleSizeValue);
    sizeRow->AddChild(titleSizeValue);
    titleColor = CreateDropdown("uv.insp.titleColor", 0, 0, kFieldWidth, ctl);
    for (const NamedColor& c : kColors) titleColor->AddItem(c.label);
    titleColor->onSelectionChanged = [this](int i, const DropdownItem&) {
        const int index = title;
        const uint32_t rgb = kColors[std::clamp(i, 0, 6)].rgb;
        Edit("Colour title", [index, rgb](Document& d) {
            if (index >= 0 && static_cast<size_t>(index) < d.titles.size()) d.titles[static_cast<size_t>(index)].look.textColor = rgb;
        });
    };
    dropdowns.push_back(titleColor);
    Row(titleRows, "uv.insp.row.titleColor", "Colour", titleColor);
    titleShadow = UltraCanvasSwitch::Create("uv.insp.titleShadow", 0, 0, "Shadow", true);
    titleShadow->onStateChanged = [this](CheckedState, CheckedState now) {
        const int index = title;
        const bool on = now == CheckedState::Checked;
        Edit("Title shadow", [index, on](Document& d) {
            if (index >= 0 && static_cast<size_t>(index) < d.titles.size()) d.titles[static_cast<size_t>(index)].look.shadow = on;
        });
    };
    titleShadow->layoutItem.SetFlexShrink(0);
    switches.push_back(titleShadow);
    auto switchRow = std::make_shared<UltraCanvasContainer>("uv.insp.titleSwitches", 0, 0, kPageWidth, ctl + 4);
    switchRow->layout.SetFlexRow().SetFlexGap(static_cast<int>(Tokens::Space3)).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    switchRow->layoutItem.SetFlexShrink(0);
    switchRow->AddChild(titleShadow);
    titleBox = UltraCanvasSwitch::Create("uv.insp.titleBox", 0, 0, "Band", false);
    titleBox->onStateChanged = [this](CheckedState, CheckedState now) {
        const int index = title;
        const bool on = now == CheckedState::Checked;
        Edit("Title band", [index, on](Document& d) {
            if (index >= 0 && static_cast<size_t>(index) < d.titles.size()) d.titles[static_cast<size_t>(index)].look.box = on;
        });
    };
    titleBox->SetTooltip("A dark band behind the text, as a lower third has");
    titleBox->layoutItem.SetFlexShrink(0);
    switches.push_back(titleBox);
    switchRow->AddChild(titleBox);
    titleRows->AddChild(switchRow);
    auto titleSlider = [&](const std::string& id, float lo, float hi, float step, std::shared_ptr<Slider>& out,
                           std::shared_ptr<Label>& value, const std::string& caption, const std::string& label,
                           std::function<void(TitleSpan&, double)> apply, std::function<std::string(double)> format) {
        out = CreateSlider(id, 0, 0, 110, ctl);
        out->SetRange(lo, hi);
        out->SetStep(step);
        out->SetValueDisplay(SliderValueDisplay::NoDisplay);
        auto row = Row(titleRows, id + ".row", caption, out);
        value = CreateLabel(id + ".value", 0, 0, 50, ctl, "");
        value->SetFont(Tokens::Timecode.family, Tokens::Timecode.size * 0.75f);
        captions.push_back(value);
        row->AddChild(value);
        sliders.push_back(out);
        Label* valuePtr = value.get();
        out->onValueChanged = [this, valuePtr, label, apply, format](float v) {
            valuePtr->SetText(format(v));
            const int index = title;
            const double d = v;
            Edit(label, [index, apply, d](Document& doc) {
                if (index >= 0 && static_cast<size_t>(index) < doc.titles.size()) apply(doc.titles[static_cast<size_t>(index)], d);
            });
        };
    };
    titleSlider("uv.insp.titleFadeIn", 0.0f, 5.0f, 0.1f, titleFadeIn, titleFadeInValue, "Fade in", "Set title fade in",
                [](TitleSpan& t, double v) { t.fadeIn = v; }, [](double v) { return Seconds(v); });
    titleSlider("uv.insp.titleFadeOut", 0.0f, 5.0f, 0.1f, titleFadeOut, titleFadeOutValue, "Fade out", "Set title fade out",
                [](TitleSpan& t, double v) { t.fadeOut = v; }, [](double v) { return Seconds(v); });
    clipFields->AddChild(titleRows);

    // ---- an overlay clip: where and how big over the picture ----
    overlayRows = std::make_shared<UltraCanvasContainer>("uv.insp.clip.overlay", 0, 0, kPageWidth, 220);
    overlayRows->layout.SetFlexColumn().SetFlexGap(static_cast<int>(Tokens::Space2)).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    overlayRows->layoutItem.SetFlexShrink(0);
    overlayLength = CreateTimecodeField("uv.insp.overlayLength", 0, 0, 120, ctl);
    overlayLength->onCommit = [this](long long frames) {
        const int index = overlay;
        const double seconds = std::max(0.04, frames / documentFrameRate);
        Edit("Set overlay length", [index, seconds](Document& d) {
            if (index >= 0 && static_cast<size_t>(index) < d.overlays.size()) d.overlays[static_cast<size_t>(index)].duration = seconds;
        });
    };
    timecodes.push_back(overlayLength);
    Row(overlayRows, "uv.insp.row.overlayLength", "Length", overlayLength);
    overlayAnchor = CreateDropdown("uv.insp.overlayAnchor", 0, 0, kFieldWidth, ctl);
    for (const char* l : kAnchorLabels) overlayAnchor->AddItem(l);
    overlayAnchor->onSelectionChanged = [this](int i, const DropdownItem&) {
        const int index = overlay;
        const VideoFXAnchor anchor = kAnchors[std::clamp(i, 0, 8)];
        Edit("Place overlay", [index, anchor](Document& d) {
            if (index >= 0 && static_cast<size_t>(index) < d.overlays.size()) d.overlays[static_cast<size_t>(index)].look.anchor = anchor;
        });
    };
    dropdowns.push_back(overlayAnchor);
    Row(overlayRows, "uv.insp.row.overlayAnchor", "Place", overlayAnchor);
    auto overlaySlider = [&](const std::string& id, float lo, float hi, float step, std::shared_ptr<Slider>& out,
                             std::shared_ptr<Label>& value, const std::string& caption, const std::string& label,
                             std::function<void(OverlaySpan&, double)> apply, std::function<std::string(double)> format) {
        out = CreateSlider(id, 0, 0, 110, ctl);
        out->SetRange(lo, hi);
        out->SetStep(step);
        out->SetValueDisplay(SliderValueDisplay::NoDisplay);
        auto row = Row(overlayRows, id + ".row", caption, out);
        value = CreateLabel(id + ".value", 0, 0, 50, ctl, "");
        value->SetFont(Tokens::Timecode.family, Tokens::Timecode.size * 0.75f);
        captions.push_back(value);
        row->AddChild(value);
        sliders.push_back(out);
        Label* valuePtr = value.get();
        out->onValueChanged = [this, valuePtr, label, apply, format](float v) {
            valuePtr->SetText(format(v));
            const int index = overlay;
            const double d = v;
            Edit(label, [index, apply, d](Document& doc) {
                if (index >= 0 && static_cast<size_t>(index) < doc.overlays.size()) apply(doc.overlays[static_cast<size_t>(index)], d);
            });
        };
    };
    auto percent = [](double v) { return std::to_string(static_cast<int>(std::lround(v * 100))) + " %"; };
    overlaySlider("uv.insp.overlaySize", 0.1f, 1.0f, 0.05f, overlaySize, overlaySizeValue, "Size", "Size overlay",
                  [](OverlaySpan& v, double s) { v.look.imageHeight = s; }, percent);
    overlaySlider("uv.insp.overlayOpacity", 0.1f, 1.0f, 0.05f, overlayOpacity, overlayOpacityValue, "Opacity", "Set overlay opacity",
                  [](OverlaySpan& v, double o) { v.look.opacity = o; }, percent);
    overlaySlider("uv.insp.overlayFadeIn", 0.0f, 5.0f, 0.1f, overlayFadeIn, overlayFadeInValue, "Fade in", "Set overlay fade in",
                  [](OverlaySpan& v, double s) { v.fadeIn = s; }, [](double v) { return Seconds(v); });
    overlaySlider("uv.insp.overlayFadeOut", 0.0f, 5.0f, 0.1f, overlayFadeOut, overlayFadeOutValue, "Fade out", "Set overlay fade out",
                  [](OverlaySpan& v, double s) { v.fadeOut = s; }, [](double v) { return Seconds(v); });
    clipFields->AddChild(overlayRows);

    // ---- a voice-over or sound effect (A2): how loud, its fades, the music under it ----
    soundRows = std::make_shared<UltraCanvasContainer>("uv.insp.clip.sound", 0, 0, kPageWidth, 160);
    soundRows->layout.SetFlexColumn().SetFlexGap(static_cast<int>(Tokens::Space2)).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    soundRows->layoutItem.SetFlexShrink(0);
    auto soundSlider = [&](const std::string& id, float lo, float hi, float step, std::shared_ptr<Slider>& out,
                           std::shared_ptr<Label>& value, const std::string& caption, const std::string& label,
                           std::function<void(SoundSpan&, double)> apply, std::function<std::string(double)> format) {
        out = CreateSlider(id, 0, 0, 110, ctl);
        out->SetRange(lo, hi);
        out->SetStep(step);
        out->SetValueDisplay(SliderValueDisplay::NoDisplay);
        auto row = Row(soundRows, id + ".row", caption, out);
        value = CreateLabel(id + ".value", 0, 0, 50, ctl, "");
        value->SetFont(Tokens::Timecode.family, Tokens::Timecode.size * 0.75f);
        captions.push_back(value);
        row->AddChild(value);
        sliders.push_back(out);
        Label* valuePtr = value.get();
        out->onValueChanged = [this, valuePtr, label, apply, format](float v) {
            valuePtr->SetText(format(v));
            const int index = sound;
            const double d = v;
            Edit(label, [index, apply, d](Document& doc) {
                if (index >= 0 && static_cast<size_t>(index) < doc.sounds.size()) apply(doc.sounds[static_cast<size_t>(index)], d);
            });
        };
    };
    soundSlider("uv.insp.soundVolume", 0.0f, 2.0f, 0.05f, soundVolume, soundVolumeValue, "Volume", "Set sound volume",
                [](SoundSpan& a, double v) { a.volume = v; }, percent);
    soundSlider("uv.insp.soundFadeIn", 0.0f, 5.0f, 0.1f, soundFadeIn, soundFadeInValue, "Fade in", "Set sound fade in",
                [](SoundSpan& a, double s) { a.fadeIn = s; }, [](double v) { return Seconds(v); });
    soundSlider("uv.insp.soundFadeOut", 0.0f, 5.0f, 0.1f, soundFadeOut, soundFadeOutValue, "Fade out", "Set sound fade out",
                [](SoundSpan& a, double s) { a.fadeOut = s; }, [](double v) { return Seconds(v); });
    soundDucks = UltraCanvasSwitch::Create("uv.insp.soundDucks", 0, 0, "The music dips under it", true);
    soundDucks->onStateChanged = [this](CheckedState, CheckedState now) {
        const int index = sound;
        const bool on = now == CheckedState::Checked;
        Edit(on ? "Make voice-over" : "Make sound effect", [index, on](Document& d) {
            if (index >= 0 && static_cast<size_t>(index) < d.sounds.size()) d.sounds[static_cast<size_t>(index)].ducksMusic = on;
        });
    };
    soundDucks->layoutItem.SetFlexShrink(0);
    switches.push_back(soundDucks);
    soundRows->AddChild(soundDucks);
    clipFields->AddChild(soundRows);

    transitionRows = std::make_shared<UltraCanvasContainer>("uv.insp.clip.transition", 0, 0, kPageWidth, 100);
    transitionRows->layout.SetFlexColumn().SetFlexGap(static_cast<int>(Tokens::Space2)).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    transitionRows->layoutItem.SetFlexShrink(0);
    Heading(transitionRows, "uv.insp.transition.heading", "Transition in");
    transitionType = CreateDropdown("uv.insp.transitionType", 0, 0, kFieldWidth, ctl);
    for (const TransitionChoice& c : TransitionChoices()) transitionType->AddItem(c.label);
    transitionType->onSelectionChanged = [this](int i, const DropdownItem&) {
        const int index = segment;
        const VideoFXTransitionType type = TransitionChoices()[static_cast<size_t>(std::clamp<int>(i, 0, static_cast<int>(TransitionChoices().size()) - 1))].type;
        Edit("Set transition", [index, type](Document& d) {
            if (index <= 0 || static_cast<size_t>(index) >= d.project.segments.size()) return;
            VideoFXSegment& s = d.project.segments[static_cast<size_t>(index)];
            if (type == VideoFXTransitionType::Cut) s.transitionIn = VideoFXTransition{};
            else s.transitionIn = VideoFXTransition::Make(type, s.transitionIn.IsCut() ? 1.0 : s.transitionIn.duration);
        });
    };
    dropdowns.push_back(transitionType);
    Row(transitionRows, "uv.insp.row.transitionType", "Type", transitionType);
    transitionLength = CreateTimecodeField("uv.insp.transitionLength", 0, 0, 120, ctl);
    transitionLength->onCommit = [this](long long frames) {
        const int index = segment;
        const double seconds = std::clamp(frames / documentFrameRate, 0.04, 10.0);
        Edit("Set transition length", [index, seconds](Document& d) {
            if (index <= 0 || static_cast<size_t>(index) >= d.project.segments.size()) return;
            VideoFXSegment& s = d.project.segments[static_cast<size_t>(index)];
            if (!s.transitionIn.IsCut()) s.transitionIn.duration = seconds;
        });
    };
    timecodes.push_back(transitionLength);
    auto lengthRow = Row(transitionRows, "uv.insp.row.transitionLength", "Length", transitionLength);
    transitionMax = CreateLabel("uv.insp.transitionMax", 0, 0, 60, ctl, "");
    transitionMax->SetFont(Tokens::Caption.family, Tokens::Caption.size * 0.75f);
    captions.push_back(transitionMax);
    lengthRow->AddChild(transitionMax);
    clipFields->AddChild(transitionRows);

    // ---- the effects on a main-track clip, from the bin's Effects tab ----
    effectRows = std::make_shared<UltraCanvasContainer>("uv.insp.clip.effects", 0, 0, kPageWidth, 120);
    effectRows->layout.SetFlexColumn().SetFlexGap(static_cast<int>(Tokens::Space1)).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    effectRows->layoutItem.SetFlexShrink(0);
    Heading(effectRows, "uv.insp.effects.heading", "Effects");
    effectsNone = CreateLabel("uv.insp.effects.none", 0, 0, kPageWidth, 32, "None. The bin's Effects tab puts a look or a blur on this clip.");
    effectsNone->SetFont(Tokens::Caption.family, Tokens::Caption.size * 0.75f);
    effectsNone->layoutItem.SetFlexShrink(0);
    captions.push_back(effectsNone);
    effectRows->AddChild(effectsNone);
    for (int i = 0; i < kEffectRows; ++i) {
        const std::string id = "uv.insp.effect." + std::to_string(i);
        auto row = std::make_shared<UltraCanvasContainer>(id, 0, 0, kPageWidth, ctl);
        row->layout.SetFlexRow().SetFlexGap(static_cast<int>(Tokens::Space2)).SetFlexAlignItems(CSSLayout::AlignItems::Center);
        row->layoutItem.SetFlexShrink(0);
        auto name = CreateLabel(id + ".name", 0, 0, kPageWidth - kEffectSliderWidth - ctl - 16, ctl, "");
        name->SetFont(Tokens::Label.family, Tokens::Label.size * 0.75f);
        name->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        captions.push_back(name);
        row->AddChild(name);
        // How strong: the name follows the slider, the edit goes through undo
        auto amount = CreateSlider(id + ".amount", 0, 0, kEffectSliderWidth, ctl);
        amount->SetValueDisplay(SliderValueDisplay::NoDisplay);
        amount->layoutItem.SetFlexShrink(0);
        Label* namePtr = name.get();
        amount->onValueChanged = [this, i, namePtr](float v) {
            if (static_cast<size_t>(i) >= shownEffects.size()) return;
            VideoFXEffect shown = shownEffects[static_cast<size_t>(i)];
            shown.amount = v;
            namePtr->SetText(DescribeEffect(shown));
            const EffectTarget target = Target();
            const double value = v;
            Edit("Set effect strength", [target, i, value](Document& d) {
                d.SetEffectAmount(target, static_cast<size_t>(i), value);
            });
        };
        sliders.push_back(amount);
        row->AddChild(amount);
        auto remove = MakeIconButton(id + ".remove", "trash", "Remove this effect", p);
        remove->SetOnClick([this, i]() {
            const EffectTarget target = Target();
            Edit("Remove effect", [target, i](Document& d) {
                std::vector<VideoFXEffect>* effects = d.EffectsOf(target);
                if (effects && static_cast<size_t>(i) < effects->size()) effects->erase(effects->begin() + i);
            });
        });
        row->AddChild(remove);
        effectRow.push_back(row);
        effectName.push_back(name);
        effectAmount.push_back(amount);
        effectRemove.push_back(remove);
        effectRows->AddChild(row);
    }
    effectsMore = CreateLabel("uv.insp.effects.more", 0, 0, kPageWidth, 18, "");
    effectsMore->SetFont(Tokens::Caption.family, Tokens::Caption.size * 0.75f);
    effectsMore->layoutItem.SetFlexShrink(0);
    captions.push_back(effectsMore);
    effectRows->AddChild(effectsMore);
    removeEffects = MakeTextButton("uv.insp.removeEffects", "Remove all effects", p, 150);
    removeEffects->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Start);
    removeEffects->SetOnClick([this]() {
        const EffectTarget target = Target();
        Edit("Remove effects", [target](Document& d) {
            if (std::vector<VideoFXEffect>* effects = d.EffectsOf(target)) effects->clear();
        });
    });
    effectRows->AddChild(removeEffects);
    clipFields->AddChild(effectRows);
    clipPage->AddChild(clipFields);
    tabs->AddTab("Clip", clipPage);

    // ---- Audio ----
    audioPage = Page("uv.insp.audio");
    Heading(audioPage, "uv.insp.audio.heading", "Music");
    songs = CreateLabel("uv.insp.songs", 0, 0, kPageWidth, 40, "No music. Add a song, or drop one on the bin.");
    songs->SetFont(Tokens::Body.family, Tokens::Body.size * 0.75f);
    captions.push_back(songs);
    audioPage->AddChild(songs);
    auto songButtons = std::make_shared<UltraCanvasContainer>("uv.insp.songButtons", 0, 0, kPageWidth, ctl + 4);
    songButtons->layout.SetFlexRow().SetFlexGap(static_cast<int>(Tokens::Space2));
    songButtons->layoutItem.SetFlexShrink(0);
    addSong = MakeTextButton("uv.insp.addSong", "Add song\xE2\x80\xA6", p, 110);
    addSong->SetOnClick([this]() { if (onAddMusic) onAddMusic(); });
    songButtons->AddChild(addSong);
    removeSongs = MakeTextButton("uv.insp.removeSongs", "Remove music", p, 120);
    removeSongs->SetOnClick([this]() {
        Edit("Remove music", [](Document& d) { d.project.settings.music = VideoFXMusic{}; });
    });
    songButtons->AddChild(removeSongs);
    audioPage->AddChild(songButtons);

    auto makeSlider = [&](const std::string& id, float lo, float hi, float step, std::shared_ptr<Slider>& out,
                          std::shared_ptr<Label>& value, const std::string& caption, const std::string& label,
                          std::function<void(Document&, double)> apply, std::function<std::string(double)> format) {
        out = CreateSlider(id, 0, 0, 110, ctl);
        out->SetRange(lo, hi);
        out->SetStep(step);
        out->SetValueDisplay(SliderValueDisplay::NoDisplay);
        auto row = Row(audioPage, id + ".row", caption, out);
        value = CreateLabel(id + ".value", 0, 0, 50, ctl, "");
        value->SetFont(Tokens::Timecode.family, Tokens::Timecode.size * 0.75f);
        captions.push_back(value);
        row->AddChild(value);
        sliders.push_back(out);
        Label* valuePtr = value.get();
        out->onValueChanged = [this, valuePtr, label, apply, format](float v) {
            valuePtr->SetText(format(v));
            const double d = v;
            Edit(label, [apply, d](Document& doc) { apply(doc, d); });
        };
    };
    makeSlider("uv.insp.musicVolume", 0.0f, 1.5f, 0.05f, musicVolume, musicVolumeValue, "Volume", "Set music volume",
               [](Document& d, double v) { d.project.settings.music.volume = v; },
               [](double v) { return std::to_string(static_cast<int>(std::lround(v * 100))) + " %"; });
    makeSlider("uv.insp.fadeIn", 0.0f, 10.0f, 0.5f, fadeIn, fadeInValue, "Fade in", "Set fade in",
               [](Document& d, double v) { d.project.settings.music.fadeIn = v; }, [](double v) { return Seconds(v); });
    makeSlider("uv.insp.fadeOut", 0.0f, 10.0f, 0.5f, fadeOut, fadeOutValue, "Fade out", "Set fade out",
               [](Document& d, double v) { d.project.settings.music.fadeOut = v; }, [](double v) { return Seconds(v); });
    makeSlider("uv.insp.crossfade", 0.0f, 10.0f, 0.5f, crossfade, crossfadeValue, "Crossfade", "Set crossfade",
               [](Document& d, double v) { d.project.settings.music.crossfade = v; }, [](double v) { return Seconds(v); });
    loop = UltraCanvasSwitch::Create("uv.insp.loop", 0, 0, "Repeat when the video is longer", true);
    loop->onStateChanged = [this](CheckedState, CheckedState now) {
        const bool on = now == CheckedState::Checked;
        Edit("Loop music", [on](Document& d) { d.project.settings.music.loop = on; });
    };
    loop->layoutItem.SetFlexShrink(0);
    switches.push_back(loop);
    audioPage->AddChild(loop);
    Heading(audioPage, "uv.insp.ducking.heading", "Under speech");
    ducking = CreateDropdown("uv.insp.ducking", 0, 0, kFieldWidth, ctl);
    for (const char* l : kDuckingLabels) ducking->AddItem(l);
    ducking->onSelectionChanged = [this](int i, const DropdownItem&) {
        const VideoFXDuckingPreset preset = kDucking[std::clamp(i, 0, 2)];
        Edit("Set ducking", [preset](Document& d) { d.project.settings.music.SetDuckingPreset(preset); });
    };
    dropdowns.push_back(ducking);
    Row(audioPage, "uv.insp.row.ducking", "Preset", ducking);
    makeSlider("uv.insp.duckLevel", 0.0f, 1.0f, 0.05f, duckLevel, duckValue, "Dip to", "Set ducking level",
               [](Document& d, double v) { d.project.settings.music.duckingLevel = v; },
               [](double v) { return std::to_string(static_cast<int>(std::lround(v * 100))) + " %"; });
    tabs->AddTab("Audio", audioPage);

    // ---- Project ----
    projectPage = Page("uv.insp.project");
    Heading(projectPage, "uv.insp.project.heading", "Project");
    projectName = CreateTextInput("uv.insp.projectName", 0, 0, kFieldWidth, ctl);
    projectName->SetPlaceholder("Untitled");
    projectName->onEnterPressed = [this](const std::string& text) {
        Edit("Rename project", [text](Document& d) { d.project.title = text; });
        return true;
    };
    Row(projectPage, "uv.insp.row.name", "Name", projectName);
    size = CreateDropdown("uv.insp.size", 0, 0, kFieldWidth, ctl);
    for (const SizeChoice& c : kSizes) size->AddItem(c.label);
    size->onSelectionChanged = [this](int i, const DropdownItem&) {
        const SizeChoice c = kSizes[std::clamp<int>(i, 0, 5)];
        Edit("Set frame size", [c](Document& d) { d.project.settings.width = c.width; d.project.settings.height = c.height; });
    };
    dropdowns.push_back(size);
    Row(projectPage, "uv.insp.row.size", "Size", size);
    frameRate = CreateDropdown("uv.insp.frameRate", 0, 0, kFieldWidth, ctl);
    for (const char* l : kRateLabels) frameRate->AddItem(l);
    frameRate->onSelectionChanged = [this](int i, const DropdownItem&) {
        const double rate = kRates[std::clamp<int>(i, 0, 6)];
        Edit("Set frame rate", [rate](Document& d) { d.project.settings.frameRate = rate; });
    };
    dropdowns.push_back(frameRate);
    Row(projectPage, "uv.insp.row.frameRate", "Frame rate", frameRate);
    fitMode = CreateDropdown("uv.insp.fitMode", 0, 0, kFieldWidth, ctl);
    for (const char* l : {"Letterbox", "Fill the frame", "Stretch"}) fitMode->AddItem(l);
    fitMode->onSelectionChanged = [this](int i, const DropdownItem&) {
        const VideoFXFitMode mode = i == 1 ? VideoFXFitMode::Fill : i == 2 ? VideoFXFitMode::Stretch : VideoFXFitMode::Letterbox;
        Edit("Set frame fit", [mode](Document& d) { d.project.settings.fitMode = mode; });
    };
    dropdowns.push_back(fitMode);
    Row(projectPage, "uv.insp.row.fitMode", "Clips that differ", fitMode);
    // Proxies: small stand-ins the viewer plays while large clips are edited
    useProxies = UltraCanvasSwitch::Create("uv.insp.proxies", 0, 0, "Preview from proxies", true);
    useProxies->onStateChanged = [this](CheckedState, CheckedState now) {
        if (updating || !onUseProxies) return;
        onUseProxies(now == CheckedState::Checked);
    };
    useProxies->layoutItem.SetFlexShrink(0);
    switches.push_back(useProxies);
    projectPage->AddChild(useProxies);
    tabs->AddTab("Project", projectPage);

    ApplyPalette(p);
    return tabs;
}

void Inspector::StyleControls(const Palette& p) {
    DropdownStyle dropdown;
    dropdown.normalColor = ToColor(p.Bg300);
    dropdown.hoverColor = ToColor(p.Bg400);
    dropdown.pressedColor = ToColor(p.Bg500);
    dropdown.borderColor = ToColor(p.LineStrong);
    dropdown.focusBorderColor = ToColor(p.Focus);
    dropdown.normalTextColor = ToColor(p.Ink);
    dropdown.listBackgroundColor = ToColor(p.Bg300);
    dropdown.listBorderColor = ToColor(p.LineStrong);
    dropdown.itemHoverColor = ToColor(p.Bg400);
    dropdown.itemSelectedColor = ToColor(p.AccentSoft);
    dropdown.cornerRadius = Tokens::RadiusSm;
    for (auto& d : dropdowns) d->SetStyle(dropdown);
    SliderVisualStyle slider;
    slider.trackColor = ToColor(p.Bg500);
    slider.activeTrackColor = ToColor(p.Accent);
    slider.handleColor = ToColor(p.Ink);
    slider.handleBorderColor = ToColor(p.Bg100);
    slider.trackHeight = 4.0f;
    slider.handleSize = 12.0f;
    for (auto& s : sliders) s->SetStyle(slider);
    SwitchVisualStyle sw;
    sw.base.textColor = ToColor(p.Ink);
    sw.trackOffColor = ToColor(p.Bg500);
    sw.trackOnColor = ToColor(p.Accent);
    sw.trackBorderColor = ToColor(p.LineStrong);
    sw.thumbColor = ToColor(p.OnAccent);
    sw.thumbBorderColor = ToColor(p.LineStrong);
    for (auto& s : switches) s->SetVisualStyle(sw);
    for (auto& t : timecodes) t->SetStyle(TimecodeStyleFor(p));
    for (auto& c : captions) c->SetTextColor(ToColor(p.InkMuted));
    for (auto& h : headings) h->SetTextColor(ToColor(p.Ink));
    if (clipName) clipName->SetTextColor(ToColor(p.Ink));
    TextInputStyle input = projectName->GetStyle();
    input.backgroundColor = ToColor(p.Bg200);
    input.borderColor = ToColor(p.LineStrong);
    input.focusBorderColor = ToColor(p.Focus);
    input.textColor = ToColor(p.Ink);
    input.placeholderColor = ToColor(p.InkFaint);
    input.caretColor = ToColor(p.Ink);
    input.borderRadius = static_cast<int>(Tokens::RadiusSm);
    projectName->SetStyle(input);
    titleText->SetStyle(input);
    addSong->SetStyle(PlainButtonStyle(p));
    removeSongs->SetStyle(PlainButtonStyle(p));
    removeEffects->SetStyle(PlainButtonStyle(p));
    for (auto& b : effectRemove) b->SetStyle(GhostButtonStyle(p));
}

void Inspector::ApplyPalette(const Palette& p) {
    if (!tabs) return;
    tabs->SetBackgroundColor(ToColor(p.Bg100));
    tabs->SetTabBarColor(ToColor(p.Bg100));
    tabs->SetInactiveTabBackgroundColor(ToColor(p.Bg100));
    tabs->SetActiveTabBackgroundColor(ToColor(p.Bg100));
    tabs->SetHoveredTabBackgroundColor(ToColor(p.Bg400));
    tabs->SetInactiveTabTextColor(ToColor(p.InkMuted));
    tabs->SetActiveTabTextColor(ToColor(p.Ink));
    tabs->SetActiveTabIndicatorColor(ToColor(p.Accent));
    tabs->SetActiveTabBorderColor(ToColor(p.Line));
    tabs->SetInactiveTabBorderColor(ToColor(p.Line));
    tabs->SetHoveredTabBorderColor(ToColor(p.Line));
    tabs->contentAreaColor = ToColor(p.Bg100);
    tabs->tabContentBorderColor = ToColor(p.Line);
    tabs->navButtonBackgroundColor = ToColor(p.Bg300);
    tabs->navButtonIconColor = ToColor(p.Ink);
    for (auto& page : {clipPage, audioPage, projectPage}) page->SetBackgroundColor(ToColor(p.Bg100));
    StyleControls(p);
}

// ============================================================================
// showing the document
// ============================================================================
EffectTarget Inspector::Target() const {
    return segment >= 0 ? EffectTarget{segment, -1} : EffectTarget{-1, overlay};
}

void Inspector::ShowClipTab() {
    if (tabs) tabs->SetActiveTab(0);
}

void Inspector::ShowAudioTab() {
    if (tabs) tabs->SetActiveTab(1);
}

void Inspector::Show(const Document& doc, int index, int titleIndex, int overlayIndex, int soundIndex) {
    updating = true;
    segment = index;
    title = titleIndex;
    overlay = overlayIndex;
    sound = soundIndex;
    documentFrameRate = doc.FrameRate();
    for (auto& t : timecodes) t->SetFrameRate(documentFrameRate);

    // ---- Clip ----
    const bool hasClip = index >= 0 && static_cast<size_t>(index) < doc.project.segments.size();
    const bool hasTitle = !hasClip && titleIndex >= 0 && static_cast<size_t>(titleIndex) < doc.titles.size();
    const bool hasOverlay = !hasClip && !hasTitle && overlayIndex >= 0 && static_cast<size_t>(overlayIndex) < doc.overlays.size();
    const bool hasSound = !hasClip && !hasTitle && !hasOverlay && soundIndex >= 0 && static_cast<size_t>(soundIndex) < doc.sounds.size();
    clipEmpty->SetVisible(!hasClip && !hasTitle && !hasOverlay && !hasSound);
    clipFields->SetVisible(hasClip || hasTitle || hasOverlay || hasSound);
    titleRows->SetVisible(hasTitle);
    overlayRows->SetVisible(hasOverlay);
    soundRows->SetVisible(hasSound);
    // a main-track clip's effects, or an overlay clip's own picture's
    effectRows->SetVisible(hasClip || hasOverlay);
    if (hasClip || hasOverlay) {
        const std::vector<VideoFXEffect>& effects = *doc.EffectsOf(Target());
        shownEffects = effects;
        effectsNone->SetVisible(effects.empty());
        for (int i = 0; i < kEffectRows; ++i) {
            const size_t at = static_cast<size_t>(i);
            const bool shown = at < effects.size();
            effectRow[at]->SetVisible(shown);
            if (!shown) continue;
            const EffectRange range = RangeOfEffect(effects[at].type);
            effectAmount[at]->SetVisible(range.adjustable);
            if (range.adjustable) {
                effectAmount[at]->SetRange(static_cast<float>(range.low), static_cast<float>(range.high));
                effectAmount[at]->SetStep(static_cast<float>(range.step));
                effectAmount[at]->SetValue(static_cast<float>(std::clamp(effects[at].amount, range.low, range.high)));
            }
            effectName[at]->SetText(DescribeEffect(effects[at]));   // after the slider, which may report as it is set
        }
        const size_t more = effects.size() > static_cast<size_t>(kEffectRows) ? effects.size() - kEffectRows : 0;
        effectsMore->SetVisible(more > 0);
        if (more > 0) effectsMore->SetText("and " + std::to_string(more) + " more");
        removeEffects->SetVisible(!effects.empty());
    }
    if (hasSound) {
        const SoundSpan& a = doc.sounds[static_cast<size_t>(soundIndex)];
        clipName->SetText(a.Name());
        clipKind->SetText(a.ducksMusic ? "Voice-over on Voice & sound" : "Sound effect on Voice & sound");
        fileRows->SetVisible(false);
        stillRow->SetVisible(false);
        imageRows->SetVisible(false);
        transitionRows->SetVisible(false);
        mute->SetVisible(false);
        clipDuration->SetText(Seconds(a.duration) + " on the timeline, from " + TimelineTimecode(a.start, documentFrameRate) +
                              (a.sourceIn <= 0.0 ? "" : ", the file from " + Seconds(a.sourceIn)));
        soundVolume->SetValue(static_cast<float>(a.volume));
        soundVolumeValue->SetText(std::to_string(static_cast<int>(std::lround(a.volume * 100))) + " %");
        soundFadeIn->SetValue(static_cast<float>(a.fadeIn));
        soundFadeInValue->SetText(Seconds(a.fadeIn));
        soundFadeOut->SetValue(static_cast<float>(a.fadeOut));
        soundFadeOutValue->SetText(Seconds(a.fadeOut));
        soundDucks->SetChecked(a.ducksMusic);
    }
    if (hasOverlay) {
        const OverlaySpan& v = doc.overlays[static_cast<size_t>(overlayIndex)];
        clipName->SetText(v.Name());
        const MediaItem* media = doc.FindMedia(v.path);
        std::string kind = v.IsStill() ? "Picture over the picture" : "Video over the picture, silent";
        if (media && media->width > 0) {
            std::ostringstream o;
            o.imbue(std::locale::classic());
            o << kind << " \xC2\xB7 " << media->width << " \xC3\x97 " << media->height;
            kind = o.str();
        }
        clipKind->SetText(kind);
        fileRows->SetVisible(false);
        stillRow->SetVisible(false);
        imageRows->SetVisible(false);
        transitionRows->SetVisible(false);
        mute->SetVisible(false);
        clipDuration->SetText(Seconds(v.duration) + " on the timeline, from " + TimelineTimecode(v.start, documentFrameRate) +
                              (v.IsStill() || v.sourceIn <= 0.0 ? "" : ", the file from " + Seconds(v.sourceIn)));
        overlayLength->SetMaximumFrames(0);
        overlayLength->SetSeconds(v.duration);
        overlayAnchor->SetSelectedIndex(IndexIn(kAnchors, v.look.anchor), false);
        const double size = v.look.imageHeight > 0.0 ? v.look.imageHeight : 1.0;
        overlaySize->SetValue(static_cast<float>(size));
        overlaySizeValue->SetText(std::to_string(static_cast<int>(std::lround(size * 100))) + " %");
        overlayOpacity->SetValue(static_cast<float>(v.look.opacity));
        overlayOpacityValue->SetText(std::to_string(static_cast<int>(std::lround(v.look.opacity * 100))) + " %");
        overlayFadeIn->SetValue(static_cast<float>(v.fadeIn));
        overlayFadeInValue->SetText(Seconds(v.fadeIn));
        overlayFadeOut->SetValue(static_cast<float>(v.fadeOut));
        overlayFadeOutValue->SetText(Seconds(v.fadeOut));
    }
    if (hasTitle) {
        const TitleSpan& t = doc.titles[static_cast<size_t>(titleIndex)];
        clipName->SetText(t.Name());
        clipKind->SetText("Title on the title lane");
        fileRows->SetVisible(false);
        stillRow->SetVisible(false);
        imageRows->SetVisible(false);
        transitionRows->SetVisible(false);
        mute->SetVisible(false);
        clipDuration->SetText(Seconds(t.duration) + " on the timeline, from " + TimelineTimecode(t.start, documentFrameRate));
        titleText->SetText(t.look.text);
        titleLength->SetMaximumFrames(0);
        titleLength->SetSeconds(t.duration);
        int styleIndex = 0;
        for (size_t i = 0; i < TitleStyles().size(); ++i) if (t.style == TitleStyles()[i].key) styleIndex = static_cast<int>(i);
        titleStyle->SetSelectedIndex(styleIndex, false);
        titleAnchor->SetSelectedIndex(IndexIn(kAnchors, t.look.anchor), false);
        titleSize->SetValue(static_cast<float>(t.look.fontSize));
        titleSizeValue->SetText(std::to_string(static_cast<int>(std::lround(t.look.fontSize * 100))) + " %");
        int colorIndex = 0;
        for (int i = 0; i < 7; ++i) if (kColors[i].rgb == t.look.textColor) colorIndex = i;
        titleColor->SetSelectedIndex(colorIndex, false);
        titleShadow->SetChecked(t.look.shadow);
        titleBox->SetChecked(t.look.box);
        titleFadeIn->SetValue(static_cast<float>(t.fadeIn));
        titleFadeInValue->SetText(Seconds(t.fadeIn));
        titleFadeOut->SetValue(static_cast<float>(t.fadeOut));
        titleFadeOutValue->SetText(Seconds(t.fadeOut));
    }
    if (hasClip) {
        mute->SetVisible(true);
        const VideoFXSegment& s = doc.project.segments[static_cast<size_t>(index)];
        clipName->SetText(ClipName(doc, static_cast<size_t>(index)));
        const bool isFile = s.kind == VideoFXSourceKind::File;
        const bool isImage = s.kind == VideoFXSourceKind::Image;
        const MediaItem* media = doc.FindMedia(s.path);
        std::string kind = isFile ? "Video clip" : isImage ? "Picture" : s.kind == VideoFXSourceKind::Color ? "Colour card" : "Test pattern";
        if (media && media->width > 0) {
            std::ostringstream o;
            o.imbue(std::locale::classic());
            o << kind << " \xC2\xB7 " << media->width << " \xC3\x97 " << media->height;
            if (media->frameRate > 0.0 && isFile) o << " \xC2\xB7 " << std::fixed << std::setprecision(media->frameRate == std::floor(media->frameRate) ? 0 : 2) << media->frameRate << " fps";
            kind = o.str();
        }
        clipKind->SetText(kind);
        fileRows->SetVisible(isFile);
        stillRow->SetVisible(!isFile);
        imageRows->SetVisible(isImage);
        if (isFile) {
            double end = s.end;
            if (end <= 0.0) end = media ? media->duration : 0.0;
            inField->SetMaximumFrames(media && media->duration > 0.0 ? static_cast<long long>(media->duration * documentFrameRate) : 0);
            outField->SetMaximumFrames(inField->GetMaximumFrames());
            inField->SetSeconds(s.start);
            outField->SetSeconds(end);
            speed->SetValue(static_cast<float>(s.speed));
            speedValue->SetText(Times(s.speed));
        } else {
            stillLength->SetMaximumFrames(0);
            stillLength->SetSeconds(s.duration);
        }
        clipDuration->SetText(Seconds(doc.SegmentSeconds(static_cast<size_t>(index))) + " on the timeline");
        mute->SetChecked(s.mute);
        if (isImage) {
            motion->SetSelectedIndex(IndexIn(kMotions, s.motion.style), false);
            imageFit->SetSelectedIndex(IndexIn(kFits, s.imageFit), false);
            keepFaces->SetChecked(s.keepFacesInView || !s.keepInView.empty());
        }
        transitionRows->SetVisible(index > 0);
        if (index > 0) {
            int which = 0;
            const auto& choices = TransitionChoices();
            for (size_t i = 0; i < choices.size(); ++i) if (choices[i].type == s.transitionIn.type) which = static_cast<int>(i);
            transitionType->SetSelectedIndex(which, false);
            transitionLength->SetVisible(!s.transitionIn.IsCut());
            transitionMax->SetVisible(!s.transitionIn.IsCut());
            const double longest = std::min({5.0, doc.SegmentSeconds(static_cast<size_t>(index)), doc.SegmentSeconds(static_cast<size_t>(index) - 1)});
            transitionLength->SetMaximumFrames(static_cast<long long>(longest * documentFrameRate));
            transitionLength->SetSeconds(s.transitionIn.duration);
            transitionMax->SetText("max " + Seconds(longest));
        }
    }

    // ---- Audio ----
    const VideoFXMusic& m = doc.project.settings.music;
    if (m.IsSet()) {
        std::string list;
        for (const std::string& song : m.Songs()) {
            if (!list.empty()) list += "\n";
            const MediaItem* item = doc.FindMedia(song);
            list += item ? item->name : PathToUtf8(PathFromUtf8(song).filename());
            if (item && item->duration > 0.0) list += "  " + item->Badge();
        }
        songs->SetText(list);
    } else {
        songs->SetText("No music. Add a song, or drop one on the bin.");
    }
    removeSongs->SetVisible(m.IsSet());
    musicVolume->SetValue(static_cast<float>(m.volume));
    musicVolumeValue->SetText(std::to_string(static_cast<int>(std::lround(m.volume * 100))) + " %");
    fadeIn->SetValue(static_cast<float>(m.fadeIn));
    fadeInValue->SetText(Seconds(m.fadeIn));
    fadeOut->SetValue(static_cast<float>(m.fadeOut));
    fadeOutValue->SetText(Seconds(m.fadeOut));
    crossfade->SetValue(static_cast<float>(m.crossfade));
    crossfadeValue->SetText(Seconds(m.crossfade));
    loop->SetChecked(m.loop);
    int preset = 0;
    if (m.duckingThresholdDb > -20.0) preset = 2;
    else if (m.duckingThresholdDb > -32.0) preset = 1;
    ducking->SetSelectedIndex(preset, false);
    duckLevel->SetValue(static_cast<float>(m.duckingLevel));
    duckValue->SetText(std::to_string(static_cast<int>(std::lround(m.duckingLevel * 100))) + " %");

    // ---- Project ----
    projectName->SetText(doc.project.title);
    int sizeIndex = 0;
    for (int i = 1; i < 6; ++i)
        if (kSizes[i].width == doc.project.settings.width && kSizes[i].height == doc.project.settings.height) sizeIndex = i;
    size->SetSelectedIndex(sizeIndex, false);
    int rateIndex = 0;
    for (int i = 1; i < 7; ++i) if (std::fabs(kRates[i] - doc.project.settings.frameRate) < 0.01) rateIndex = i;
    frameRate->SetSelectedIndex(rateIndex, false);
    fitMode->SetSelectedIndex(doc.project.settings.fitMode == VideoFXFitMode::Fill ? 1
                              : doc.project.settings.fitMode == VideoFXFitMode::Stretch ? 2 : 0, false);
    useProxies->SetChecked(doc.view.useProxies);
    updating = false;
}

} // namespace UltraVideo
