// Apps/UltraVideo/UltraVideoViewer.cpp
// The viewer (UltraVideoViewer.h).
// Version: 0.2.3 - a percent zoom is of the project's pixels on the surface and in the player alike; the sliders follow the theme
// Version: 0.2.2 - the player draws the title-safe guide too
// Version: 0.2.1 - SetFit places the picture in the surface and the player alike
// Version: 0.2.0 - the transport bar and the fit dropdown follow the theme; DraftPlay reports a refusal
// Version: 0.1.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework
#include "UltraVideoViewer.h"

#include "UltraCanvasImage.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasVideoTimelineModel.h"

#include <algorithm>
#include <cmath>

using namespace UltraCanvas;
using namespace VideoFX;

namespace UltraVideo {

// ============================================================================
// FrameSurface
// ============================================================================
FrameSurface::FrameSurface(const std::string& id) : UltraCanvasUIElement(id, 0, 0, 640, 360) {}

void FrameSurface::SetFrame(const VideoFXFrame& frame) {
    if (!frame.IsValid()) {
        ClearFrame();
        return;
    }
    if (!pixmap || frameWidth != frame.width || frameHeight != frame.height) {
        pixmap = std::make_shared<UCPixmap>(frame.width, frame.height);
        frameWidth = frame.width;
        frameHeight = frame.height;
    }
    uint32_t* out = pixmap ? pixmap->GetPixelData() : nullptr;
    if (!out) {
        pixmap.reset();
        return;
    }
    const uint8_t* in = frame.pixels.data();
    const size_t count = static_cast<size_t>(frame.width) * frame.height;
    for (size_t i = 0; i < count; ++i, in += 4) {
        out[i] = 0xFF000000u | (static_cast<uint32_t>(in[0]) << 16) | (static_cast<uint32_t>(in[1]) << 8) | in[2];
    }
    pixmap->MarkDirty();
    RequestRedraw();
}

void FrameSurface::ClearFrame() {
    pixmap.reset();
    frameWidth = frameHeight = 0;
    RequestRedraw();
}

void FrameSurface::SetZoomPercent(double percent, bool fill) {
    zoomPercent = std::max(0.0, percent);
    fillSurface = fill;
    RequestRedraw();
}

void FrameSurface::SetColors(const Color& g, const Color& gd, const Color& fl) {
    ground = g;
    guide = gd;
    frameLine = fl;
    RequestRedraw();
}

void FrameSurface::Render(IRenderContext* ctx, const Rect2Df&) {
    const Rect2Df b = GetLocalBounds();
    ctx->PushState();
    ctx->ClipRect(Rect2Dd(b.x, b.y, b.width, b.height));
    ctx->SetFillPaint(ground);
    ctx->FillRectangle(Rect2Dd(b.x, b.y, b.width, b.height));

    // Where the picture goes: fitted, filled, or at a zoom of its own pixels
    const double shape = pixmap ? static_cast<double>(frameWidth) / std::max(1, frameHeight) : aspect;
    const double margin = kMargin;
    const double availW = std::max(1.0, b.width - 2 * margin);
    const double availH = std::max(1.0, b.height - 2 * margin);
    double w, h;
    if (pixmap && zoomPercent > 0.0) {
        // percent of the project's pixels, not of the preview rendered for
        // the surface (the draft player's zoom is read the same way)
        const double refW = projectWidth > 0 ? projectWidth : frameWidth;
        const double refH = projectHeight > 0 ? projectHeight : frameHeight;
        w = refW * zoomPercent / 100.0;
        h = refH * zoomPercent / 100.0;
    } else if (fillSurface) {
        if (availW / availH > shape) { w = availW; h = availW / shape; }
        else { h = availH; w = availH * shape; }
    } else {
        if (availW / availH > shape) { h = availH; w = availH * shape; }
        else { w = availW; h = availW / shape; }
    }
    const Rect2Dd dest(b.x + (b.width - w) / 2.0, b.y + (b.height - h) / 2.0, w, h);
    if (pixmap) {
        ctx->DrawPixmap(*pixmap, dest, ImageFitMode::Fill);
    } else {
        ctx->SetStrokePaint(frameLine);
        ctx->SetStrokeWidth(1.0);
        ctx->DrawRectangle(dest);
    }
    // The title-safe area: 5 % in from each edge, dashed
    ctx->SetStrokePaint(guide);
    ctx->SetStrokeWidth(1.0);
    ctx->SetLineDash(UCDashPattern({4.0, 4.0}));
    ctx->DrawRectangle(Rect2Dd(dest.x + dest.width * 0.05, dest.y + dest.height * 0.05, dest.width * 0.9, dest.height * 0.9));
    ctx->SetLineDash(UCDashPattern());
    ctx->PopState();
}

// ============================================================================
// Viewer
// ============================================================================
std::shared_ptr<UltraCanvasContainer> Viewer::Build(const Palette& p) {
    panel = std::make_shared<UltraCanvasContainer>("uv.viewer");
    panel->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    panel->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // The row over the picture: the Fit dropdown on the right
    top = std::make_shared<UltraCanvasContainer>("uv.viewer.top", 0, 0, 100, static_cast<int>(Tokens::ToolbarH));
    top->layout.SetFlexRow().SetFlexGap(static_cast<int>(Tokens::Space2)).SetFlexAlignItems(CSSLayout::AlignItems::Center)
        .SetFlexJustifyContent(CSSLayout::JustifyContent::FlexEnd);
    top->layoutItem.SetFlexShrink(0).SetFlexBasis(CSSLayout::Dimension::Px(Tokens::ToolbarH))
        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    top->SetPadding(0, static_cast<float>(Tokens::Space2));
    fit = CreateDropdown("uv.viewer.fit", 0, 0, 96, static_cast<int>(Tokens::ControlH));
    for (const char* choice : {"Fit", "Fill", "50 %", "100 %", "200 %"}) fit->AddItem(choice);
    fit->SetSelectedIndex(0, false);
    DropdownStyle fitStyle = fit->GetStyle();
    fitStyle.cornerRadius = Tokens::RadiusSm;
    fit->SetStyle(fitStyle);
    fit->SetFollowTheme(true);
    fit->onSelectionChanged = [this](int index, const DropdownItem&) { SetFit(index); };
    fit->layoutItem.SetFlexShrink(0);
    top->AddChild(fit);
    panel->AddChild(top);

    surface = std::make_shared<FrameSurface>("uv.viewer.surface");
    surface->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0))
        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    panel->AddChild(surface);

    // The draft's player, shown in the surface's place while it plays; the
    // transport below is the app's, so the element's own controls stay off
    player = std::make_shared<UltraCanvasVideoPlayerElement>("uv.viewer.player", 0, 0, 640, 360);
    player->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0))
        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    VideoPlayerStyle playerStyle = player->GetStyle();
    playerStyle.showControls = false;
    playerStyle.autoHideControls = false;
    playerStyle.videoInset = static_cast<int>(FrameSurface::kMargin);   // the picture where the surface has it
    playerStyle.showSafeArea = true;                                     // and the surface's title-safe guide
    player->SetStyle(playerStyle);
    player->SetVisible(false);
    panel->AddChild(player);

    // The transport
    transport = std::make_shared<UltraCanvasToolbar>("uv.transport", 0, 0, 100, static_cast<int>(Tokens::ToolbarH));
    transport->layoutItem.SetFlexShrink(0).SetFlexBasis(CSSLayout::Dimension::Px(Tokens::ToolbarH))
        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    ToolbarAppearance appearance;
    appearance.style = ToolbarStyle::Flat;
    appearance.showIconLabels = false;
    appearance.hasShadow = false;
    appearance.iconSize = ToolbarIconSize::Small;
    transport->SetAppearance(appearance);
    transport->SetFollowTheme(true);
    toStart = MakeIconButton("uv.tr.start", "toStart", "To start  Home", p);
    stepBack = MakeIconButton("uv.tr.back", "stepBack", "Back a frame  \xE2\x86\x90", p);
    play = MakeIconButton("uv.tr.play", "play", "Play  Space", p);
    stepForward = MakeIconButton("uv.tr.fwd", "stepFwd", "Forward a frame  \xE2\x86\x92", p);
    toEnd = MakeIconButton("uv.tr.end", "toEnd", "To end  End", p);
    toStart->SetOnClick([this]() { if (onToStart) onToStart(); });
    stepBack->SetOnClick([this]() { if (onStepBack) onStepBack(); });
    play->SetOnClick([this]() { if (onPlayPause) onPlayPause(); });
    stepForward->SetOnClick([this]() { if (onStepForward) onStepForward(); });
    toEnd->SetOnClick([this]() { if (onToEnd) onToEnd(); });
    transport->AddStretch();
    transport->AddChild(toStart);
    transport->AddChild(stepBack);
    transport->AddChild(play);
    transport->AddChild(stepForward);
    transport->AddChild(toEnd);
    transport->AddSpacer(static_cast<int>(Tokens::Space3));
    readout = CreateTimecodeField("uv.tr.readout", 0, 0, 150, static_cast<int>(Tokens::ControlH));
    readout->SetReadout(true);
    readout->SetFrameRate(frameRate);
    readout->layoutItem.SetFlexShrink(0);
    transport->AddChild(readout);
    total = CreateLabel("uv.tr.total", 0, 0, 110, static_cast<int>(Tokens::ControlH), "/ 00:00:00:00");
    total->SetFont(Tokens::Timecode.family, Tokens::Timecode.size * 0.75f);
    total->layoutItem.SetFlexShrink(0);
    transport->AddChild(total);
    transport->AddStretch();
    auto speaker = MakeIconButton("uv.tr.speaker", "speaker", "Volume", p);
    speaker->SetDisabled(true);
    transport->AddChild(speaker);
    volume = CreateSlider("uv.tr.volume", 0, 0, 90, static_cast<int>(Tokens::ControlH));
    volume->SetRange(0.0f, 1.0f);
    volume->SetValue(1.0f);
    volume->SetValueDisplay(SliderValueDisplay::NoDisplay);
    SliderVisualStyle volumeStyle = volume->GetStyle();
    volumeStyle.trackHeight = 4.0f;
    volumeStyle.handleSize = 12.0f;
    volume->SetStyle(volumeStyle);
    volume->SetFollowTheme(true);          // the colours, with the slider.handle roles
    volume->onValueChanged = [this](float v) {
        volumeLevel = v;
        if (player && player->GetPlayer()) player->GetPlayer()->SetVolume(v);
        if (onVolumeChanged) onVolumeChanged(v);
    };
    volume->layoutItem.SetFlexShrink(0);
    transport->AddChild(volume);
    transport->AddSpacer(static_cast<int>(Tokens::Space2));
    panel->AddChild(transport);

    ApplyPalette(p);
    return panel;
}

void Viewer::ApplyPalette(const Palette& p) {
    if (!panel) return;
    panel->SetBackgroundColor(ToColor(p.Bg000));
    top->SetBackgroundColor(ToColor(p.Bg100));
    surface->SetColors(ToColor(p.Bg000), ToColor(p.LineStrong), ToColor(p.Line));
    if (player) {
        VideoPlayerStyle playerStyle = player->GetStyle();
        playerStyle.backgroundColor = ToColor(p.Bg000);
        playerStyle.safeAreaColor = ToColor(p.LineStrong);   // the surface's guide colour
        player->SetStyle(playerStyle);
    }
    // the transport bar and the fit dropdown follow the theme (Build)
    for (auto& b : {toStart, stepBack, play, stepForward, toEnd}) b->SetStyle(GhostButtonStyle(p));
    readout->SetStyle(TimecodeStyleFor(p));
    total->SetTextColor(ToColor(p.InkMuted));
}

void Viewer::SetFit(int index) {
    // the surface and the player alike, so the draft takes the frame's place
    double zoom = 0.0;
    bool fill = false;
    switch (index) {
        case 1: fill = true; break;
        case 2: zoom = 50.0; break;
        case 3: zoom = 100.0; break;
        case 4: zoom = 200.0; break;
        default: break;
    }
    zoomPercent = zoom;
    if (surface) surface->SetZoomPercent(zoom, fill);
    if (player) {
        VideoPlayerStyle style = player->GetStyle();
        style.videoFit = zoom > 0.0 ? VideoPlayerFit::Pixels : fill ? VideoPlayerFit::Cover : VideoPlayerFit::Contain;
        style.zoomPercent = zoom > 0.0 ? static_cast<float>(zoom) : 100.0f;
        player->SetStyle(style);
    }
}

void Viewer::ShowFrame(const VideoFXFrame& frame) {
    if (surface) surface->SetFrame(frame);
}

void Viewer::ClearFrame() {
    if (surface) surface->ClearFrame();
}

void Viewer::SetTime(double seconds, double totalSeconds) {
    if (readout) readout->SetSeconds(std::max(0.0, seconds));
    if (total) total->SetText("/ " + TimelineTimecode(std::max(0.0, totalSeconds), frameRate));
}

void Viewer::SetFrameRate(double fps) {
    frameRate = fps > 0.0 ? fps : 25.0;
    if (readout) readout->SetFrameRate(frameRate);
}

void Viewer::SetProjectSize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    projectWidth = width;
    projectHeight = height;
    if (surface) surface->SetProjectSize(width, height);
    if (player) {
        // a percent zoom means the project's pixels on both sides, whatever
        // size the draft was rendered at
        VideoPlayerStyle style = player->GetStyle();
        style.zoomReferenceHeight = height;
        player->SetStyle(style);
    }
}

void Viewer::SetPlaying(bool on) {
    playing = on;
    if (!play) return;
    play->SetIcon(IconPath(on ? "pause" : "play"));
    play->SetTooltip(on ? "Pause  Space" : "Play  Space");
}

bool Viewer::ShowDraft(const std::string& path) {
    if (!player || !surface) return false;
    if (!player->LoadFromFile(path)) {
        player->Unload();
        return false;
    }
    if (player->GetPlayer()) player->GetPlayer()->SetVolume(volumeLevel);
    surface->SetVisible(false);
    player->SetVisible(true);
    draftShown = true;
    return true;
}

void Viewer::HideDraft() {
    if (!draftShown) return;
    draftShown = false;
    if (player) {
        player->Unload();
        player->SetVisible(false);
    }
    if (surface) surface->SetVisible(true);
}

VideoPlaybackState Viewer::DraftState() const {
    return player && player->GetPlayer() ? player->GetPlayer()->GetState() : VideoPlaybackState::Idle;
}

bool Viewer::DraftPlay() {
    if (!player || !player->GetPlayer()) return false;
    player->Play();
    return player->GetPlayer()->GetState() == VideoPlaybackState::Playing;
}
void Viewer::DraftPause() { if (player) player->Pause(); }
void Viewer::DraftSeek(double seconds) { if (player) player->Seek(std::max(0.0, seconds)); }
double Viewer::DraftPosition() const {
    return player && player->GetPlayer() ? player->GetPlayer()->GetPosition() : 0.0;
}

// The size to ask the engine for: the surface, or at a percent zoom the
// project at that zoom (capped), so the pixels shown are real ones
int Viewer::SurfaceWidth() const {
    if (zoomPercent > 0.0 && projectWidth > 0) return std::clamp(static_cast<int>(std::lround(projectWidth * zoomPercent / 100.0)), 16, 4096);
    return surface ? std::max(16, static_cast<int>(std::lround(surface->GetBounds().width))) : 640;
}

int Viewer::SurfaceHeight() const {
    if (zoomPercent > 0.0 && projectHeight > 0) return std::clamp(static_cast<int>(std::lround(projectHeight * zoomPercent / 100.0)), 9, 4096);
    return surface ? std::max(9, static_cast<int>(std::lround(surface->GetBounds().height))) : 360;
}

} // namespace UltraVideo
