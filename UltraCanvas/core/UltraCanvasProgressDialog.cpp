// core/UltraCanvasProgressDialog.cpp
// Implementation of the modal progress window.
// See UltraCanvasProgressDialog.h for the contract.
// Version: 2.2.0 - time left from the whole run so far (EstimateSecondsLeft, CombinedProgress)
//                  rather than from the last three seconds' speed
// Version: 2.1.0 - Run in background (ProgressDialogOptions::onBackground)
// Version: 2.0.0 - the transfer layout: route chips, the strip of figures, the percentage alone in
//                  the ring, Pause / Resume; plain labels refreshed at most four times a second
// Version: 1.2.0 - the ring's colours are the theme's (accent, line, ink), again on a theme change
// Version: 1.1.0 - the Cancel button goes through Tr
// Version: 1.0.0
// Last Modified: 2026-11-20
// Author: UltraCanvas Framework

#include "UltraCanvasProgressDialog.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTheme.h"
#include "Plugins/Charts/UltraCanvasCircularProgressChart.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace UltraCanvas {

    namespace {
        constexpr int kRingSize = 116;          // px, square
        constexpr float kGap = 20.0f;           // between the ring and the texts
        constexpr int kPlainWidth = 460;        // the window without the strip
        constexpr int kTransferWidth = 580;     // ... with it: four cells of "12 947 / 100 001"
        constexpr float kStripHeight = 52.0f;
        constexpr double kRateWindowSeconds = 3.0;   // speed is averaged over this
        constexpr double kRateMinSeconds = 1.0;      // and shown once this much is in
        constexpr double kEstimateMinSeconds = 2.0;  // time left: shown after this much work
        constexpr double kEstimateMinProgress = 0.002;   // ... and this much progress

        // The centre reading. A negative fraction means the total is unknown,
        // so there is no honest percentage to show.
        std::string PercentText(double fraction) {
            if (fraction < 0.0) return "...";
            const int pct = static_cast<int>(std::lround(
                    std::clamp(fraction, 0.0, 1.0) * 100.0));
            return std::to_string(pct) + "%";
        }

        // Code points, not bytes: a name in Greek or Japanese is cut between
        // characters. The tail is kept by default - a file name is more
        // telling than the folder it is in.
        std::string Ellipsize(const std::string& text, size_t maxChars, bool keepTail = true) {
            std::vector<size_t> starts;   // byte offset of each code point
            for (size_t i = 0; i < text.size(); ++i)
                if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) starts.push_back(i);
            if (starts.size() <= maxChars) return text;
            const std::string dots = "\xE2\x80\xA6";   // …
            const size_t keep = maxChars > 1 ? maxChars - 1 : 0;
            if (keepTail) return dots + text.substr(starts[starts.size() - keep]);
            return text.substr(0, starts[keep]) + dots;
        }

        // Thousands set apart by a narrow no-break space: "26 731", read the
        // same way in every language, unlike a comma or a point.
        std::string Grouped(uint64_t n) {
            std::string digits = std::to_string(n);
            std::string out;
            const size_t len = digits.size();
            for (size_t i = 0; i < len; ++i) {
                if (i > 0 && (len - i) % 3 == 0) out += "\xE2\x80\xAF";
                out += digits[i];
            }
            return out;
        }

        // One line of text at a fixed height, as wide as its parent lets it be.
        std::shared_ptr<UltraCanvasLabel> MakeLine(const std::string& id, float height, float fontSize,
                                                   bool bold, bool muted) {
            auto label = std::make_shared<UltraCanvasLabel>(id, 0, 0, 0, height, "");
            label->SetFontSize(fontSize);
            if (bold) label->SetFontWeight(FontWeight::Bold);
            if (muted) label->SetThemeVariant(LabelVariant::Muted);
            label->size.height = CSSLayout::Dimension::Px(height);
            label->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                             .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
            return label;
        }

        // A chip's width from its text: the dialog is laid out before there is
        // anything to measure with. About 0.6 em a character, plus the padding.
        float ChipWidth(const std::string& text, float fontSize) {
            size_t chars = 0;
            for (unsigned char c : text) if ((c & 0xC0) != 0x80) ++chars;
            return std::min(180.0f, 0.68f * fontSize * static_cast<float>(chars) + 26.0f);
        }
    }

    std::shared_ptr<UltraCanvasProgressDialog> UltraCanvasProgressDialog::Show(
            UltraCanvasWindowBase* parent,
            const std::string& title,
            const std::string& caption,
            std::function<void()> onCancel,
            bool showIcon) {
        ProgressDialogOptions options;
        options.showIcon = showIcon;
        return Show(parent, title, caption, std::move(onCancel), options);
    }

    std::shared_ptr<UltraCanvasProgressDialog> UltraCanvasProgressDialog::Show(
            UltraCanvasWindowBase* parent,
            const std::string& title,
            const std::string& caption,
            std::function<void()> onCancel,
            const ProgressDialogOptions& options) {

        DialogConfig cfg;
        cfg.title = title;
        cfg.dialogType = DialogType::Information;
        // No severity badge unless the caller asks for one: the ring is this
        // dialog's graphic, and an icon column beside it would offset the
        // whole content column.
        cfg.showIcon = options.showIcon;
        cfg.message = caption;
        cfg.buttons = DialogButtons::NoButtons;   // Pause / Cancel are added below
        cfg.width = options.figures ? kTransferWidth : kPlainWidth;
        cfg.height = 260;
        cfg.closeOnEscape = true;

        auto modal = UltraCanvasDialogManager::CreateDialog(cfg);
        if (!modal) return nullptr;   // dialogs unavailable — caller runs bare
        // The texts are labels of their own, not the dialog's Markdown
        // message: a label takes a new file name without laying a document
        // out again, and keeps its place in the window.
        modal->SetMessageVisible(false);

        auto self = std::shared_ptr<UltraCanvasProgressDialog>(
                new UltraCanvasProgressDialog());
        self->dialog = modal;
        self->caption = caption;
        self->options = options;
        self->onCancel = std::move(onCancel);
        self->BuildContent(title, caption, options);

        std::weak_ptr<UltraCanvasProgressDialog> weak = self;
        if (options.onBackground) {
            const ModalDialogStyle& style = modal->GetStyle();
            self->backgroundButton = std::make_shared<UltraCanvasButton>(
                    "ProgressBackground", 0, 0, 170, static_cast<long>(style.buttonHeight));
            self->backgroundButton->SetText(Tr("Run in background"));
            if (style.buttonFontSize > 0.0f) self->backgroundButton->SetFontSize(style.buttonFontSize);
            self->backgroundButton->onClick = [weak]() {
                if (auto me = weak.lock()) me->SendToBackground();
            };
            modal->AddFooterElement(self->backgroundButton);
        }
        if (options.onPause) {
            const ModalDialogStyle& style = modal->GetStyle();
            self->pauseButton = std::make_shared<UltraCanvasButton>(
                    "ProgressPause", 0, 0, 100, static_cast<long>(style.buttonHeight));
            self->paused = options.startPaused;
            self->pauseButton->SetText(self->paused ? Tr("Resume") : Tr("Pause"));
            if (style.buttonFontSize > 0.0f) self->pauseButton->SetFontSize(style.buttonFontSize);
            self->pauseButton->onClick = [weak]() {
                if (auto me = weak.lock()) me->TogglePause();
            };
            modal->AddFooterElement(self->pauseButton);
        }
        modal->AddCustomButton(Tr("Cancel"), DialogResult::Cancel, nullptr);
        // Every way out of the window - the button, Escape, the title bar - is
        // a cancel, and each of them means the window is already going away.
        modal->onResult = [weak](DialogResult) {
            auto me = weak.lock();
            if (!me || me->cancelled) return;
            if (me->backgrounded) return;   // closed by Run in background: no cancel
            me->cancelled = true;
            me->closed = true;          // the dialog closes itself on a result
            // A paused worker must hear the cancel: it waits on the pause.
            if (me->paused && me->options.onPause) {
                me->paused = false;
                me->options.onPause(false);
            }
            if (me->onCancel) me->onCancel();
        };

        self->ApplyThemeColors();
        // Elements styled by hand do not follow the theme by themselves, so a
        // change while the window is up is taken here.
        self->themeHandler = UltraCanvasTheme::AddChangeHandler([weak](const UltraCanvasTheme&) {
            if (auto me = weak.lock()) me->ApplyThemeColors();
        });
        self->RefreshTexts(true);

        UltraCanvasDialogManager::ShowDialog(modal, nullptr, parent);
        return self;
    }

    void UltraCanvasProgressDialog::BuildContent(const std::string& title, const std::string& captionText,
                                                 const ProgressDialogOptions& opts) {
        const UltraCanvasTheme& theme = UltraCanvasTheme::Current();
        const float body = static_cast<float>(theme.FontSize(ThemeTextRole::Body));
        const float small = static_cast<float>(theme.FontSize(ThemeTextRole::Caption));
        routeShown = !opts.from.empty();

        // ----- the ring | headline, route, file -----
        auto top = std::make_shared<UltraCanvasContainer>("ProgressTop");
        top->layout.SetFlexRow().SetFlexGap(kGap).SetFlexAlignItems(CSSLayout::AlignItems::Center);
        top->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        top->size.height = CSSLayout::Dimension::Px(kRingSize);

        // The ring: one arc over a track, the percentage alone in the middle -
        // the time left is in the strip, not crowded into the ring. Explicit size - content measuring needs a
        // render context, which the dialog does not have while it is first
        // laid out.
        ring = CreateCircularProgressChart("ProgressRing", 0, 0, kRingSize, kRingSize);
        ring->SetSubStyle(CircularProgressStyle::SingleRing);
        ring->AddRing("", 0.0, theme.roles.accent);
        ring->SetInnerRadiusFraction(0.78f);
        ring->SetTrackMode(RingTrackMode::Explicit);
        ring->SetCapStyle(RingCapStyle::Round);
        ring->SetTipLabelContent(RingTipLabel::NoLabel);
        ring->SetRingNameLabels(RingNamePosition::Hidden);
        ring->SetLegendStyle(CircularLegendStyle::NoLegend);
        ring->SetShowCenterDisc(false);
        ring->SetCenterText(PercentText(0.0));
        ring->SetCenterFont("Arial", 24.0f, FontWeight::Bold);
        ring->SetTooltipsEnabled(false);
        ring->SetHoverHighlightEnabled(false);
        ring->size.width  = CSSLayout::Dimension::Px(kRingSize);
        ring->size.height = CSSLayout::Dimension::Px(kRingSize);
        ring->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        top->AddChild(ring);

        auto texts = std::make_shared<UltraCanvasContainer>("ProgressTexts");
        texts->layout.SetFlexColumn().SetFlexGap(8)
                     .SetFlexJustifyContent(CSSLayout::JustifyContent::Center);
        texts->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        // With a route the window's title says what is happening ("Copying")
        // and the chips where; without one the caption says both, as before.
        headlineLabel = MakeLine("ProgressHeadline", body * 1.6f, body * 1.15f, true, false);
        headlineLabel->SetText(Ellipsize(routeShown ? title : captionText, 40, false));
        texts->AddChild(headlineLabel);

        if (routeShown) {
            auto route = std::make_shared<UltraCanvasContainer>("ProgressRoute");
            route->layout.SetFlexRow().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Center);
            route->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
            route->size.height = CSSLayout::Dimension::Px(small * 2.0f);
            auto chip = [&](const std::string& id, const std::string& name,
                            std::shared_ptr<UltraCanvasLabel>& text) {
                const std::string shown = Ellipsize(name, 22, false);
                auto box = std::make_shared<UltraCanvasContainer>(id);
                box->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Center);
                box->layoutItem.SetFlexGrow(0).SetFlexShrink(1);
                box->size.width = CSSLayout::Dimension::Px(ChipWidth(shown, small));
                box->size.height = CSSLayout::Dimension::Px(small * 1.9f);
                box->SetPadding(0, 8, 0, 8);
                text = MakeLine(id + "Text", small * 1.6f, small, false, false);
                text->SetText(shown);
                box->AddChild(text);
                route->AddChild(box);
                return box;
            };
            fromChip = chip("ProgressFrom", opts.from, fromChipText);
            if (!opts.to.empty()) {
                auto arrow = MakeLine("ProgressArrow", small * 1.6f, small, false, true);
                arrow->SetText("\xE2\x86\x92");   // →
                arrow->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
                arrow->size.width = CSSLayout::Dimension::Px(small * 1.8f);
                route->AddChild(arrow);
                toChip = chip("ProgressTo", opts.to, toChipText);
            }
            texts->AddChild(route);
        }

        detailLabel = MakeLine("ProgressDetail", small * 1.6f, small, false, true);
        texts->AddChild(detailLabel);
        top->AddChild(texts);
        dialog->AddDialogElement(top);

        // ----- the strip of figures -----
        if (!opts.figures) return;
        strip = std::make_shared<UltraCanvasContainer>("ProgressStrip");
        strip->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        strip->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        strip->size.height = CSSLayout::Dimension::Px(kStripHeight);
        // The space above it as an element of its own: the dialog sizes its
        // window from its elements' heights, without their margins, and a
        // margin here cut the strip's foot off.
        auto gap = std::make_shared<UltraCanvasContainer>("ProgressStripGap");
        gap->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        gap->size.height = CSSLayout::Dimension::Px(12);
        dialog->AddDialogElement(gap);
        const std::string labels[4] = {Tr("Speed"), Tr("Time left"), Tr("Items"), Tr("Size")};
        const bool shown[4] = {true, true, opts.items, opts.bytes};
        for (int i = 0; i < 4; ++i) {
            if (!shown[i]) continue;
            auto cell = std::make_shared<UltraCanvasContainer>("ProgressCell" + std::to_string(i));
            cell->layout.SetFlexColumn().SetFlexGap(2)
                        .SetFlexJustifyContent(CSSLayout::JustifyContent::Center);
            // Items and Size hold "done / total" and get the larger share.
            cell->layoutItem.SetFlexGrow(i >= 2 ? 1.35f : 1.0f).SetFlexShrink(1)
                            .SetFlexBasis(CSSLayout::Dimension::Px(0))
                            .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
            cell->SetPadding(0, 10, 0, 10);
            auto name = MakeLine("ProgressCellName" + std::to_string(i), small * 1.4f, small, false, true);
            name->SetText(labels[i]);
            auto value = MakeLine("ProgressCellValue" + std::to_string(i), body * 1.4f, body, true, false);
            cell->AddChild(name);
            cell->AddChild(value);
            stripCells[i] = cell;
            stripValues[i] = value;
            strip->AddChild(cell);
        }
        dialog->AddDialogElement(strip);
    }

    UltraCanvasProgressDialog::~UltraCanvasProgressDialog() {
        Close();
    }

    void UltraCanvasProgressDialog::SetProgress(double value) {
        const double clamped = value < 0.0 ? -1.0 : std::clamp(value, 0.0, 1.0);
        const auto now = std::chrono::steady_clock::now();
        if (!paused && !runStarted &&
            (figures.doneItems > 0 || figures.doneBytes > 0 || clamped > 0.0)) {
            runStarted = true;
            runStart = now;
            pausedFor = {};
            runStartProgress = std::max(0.0, CombinedProgress(figures, clamped));
        }
        if (!paused) {
            samples.push_back({now, clamped, figures.doneBytes, figures.doneItems});
            while (samples.size() > 2 &&
                   std::chrono::duration<double>(now - samples.front().at).count() > kRateWindowSeconds)
                samples.pop_front();
        }
        if (std::abs(clamped - fraction) >= 0.0005 && ring) {   // a new rounded reading
            fraction = clamped;
            ring->SetRingValue(0, fraction < 0.0 ? 25.0 : fraction * 100.0);
            ring->RequestRedraw();
        }
        RefreshTexts(false);
    }

    void UltraCanvasProgressDialog::SetFigures(const ProgressFigures& value) {
        figures = value;
    }

    void UltraCanvasProgressDialog::SetCaption(const std::string& text) {
        if (caption == text) return;
        caption = text;
        if (dialog && !closed) dialog->SetMessage(caption);   // kept as the dialog's own text too
        if (headlineLabel && !routeShown) headlineLabel->SetText(Ellipsize(caption, 40, false));
    }

    void UltraCanvasProgressDialog::SetDetail(const std::string& text) {
        detail = text;
        RefreshTexts(false);
    }

    double UltraCanvasProgressDialog::RecentRate(int which) const {
        if (samples.size() < 2) return 0.0;
        const Sample& a = samples.front();
        const Sample& b = samples.back();
        const double seconds = std::chrono::duration<double>(b.at - a.at).count();
        if (seconds < kRateMinSeconds) return 0.0;
        double delta = 0.0;
        switch (which) {
            case 0:  delta = (a.fraction < 0.0 || b.fraction < 0.0) ? 0.0 : b.fraction - a.fraction; break;
            case 1:  delta = static_cast<double>(b.bytes) - static_cast<double>(a.bytes); break;
            default: delta = static_cast<double>(b.items) - static_cast<double>(a.items); break;
        }
        return delta > 0.0 ? delta / seconds : 0.0;
    }

    void UltraCanvasProgressDialog::RefreshTexts(bool force) {
        if (!dialog || closed) return;
        const auto now = std::chrono::steady_clock::now();
        if (!force && now - lastTextRefresh < std::chrono::milliseconds(kTextRefreshMs)) return;
        lastTextRefresh = now;

        const double byteRate = RecentRate(1);
        const double itemRate = RecentRate(2);

        // Time left: from everything done since the work began and the time
        // it took (pauses left out) - not from the last seconds' speed, which
        // over many small files swings with each file's size and made the
        // estimate jump between seconds and hours.
        double secondsLeft = -1.0;
        if (!paused && runStarted) {
            const double worked = std::chrono::duration<double>(now - runStart - pausedFor).count();
            secondsLeft = EstimateSecondsLeft(worked, runStartProgress,
                                              CombinedProgress(figures, fraction));
        }
        const std::string dash = "\xE2\x80\x94";   // —
        const std::string left = paused ? Tr("Paused")
                               : secondsLeft >= 0.0 ? FormatDuration(secondsLeft) : dash;

        if (ring) ring->SetCenterText(PercentText(fraction));
        if (detailLabel) detailLabel->SetText(Ellipsize(detail, 48));

        if (!strip) return;
        if (stripValues[0]) {
            std::string speed = dash;
            if (!paused) {
                if (options.bytes && byteRate > 0.0)
                    speed = FormatBytes(static_cast<uint64_t>(byteRate)) + "/s";
                else if (!options.bytes && itemRate > 0.0)
                    speed = TrF("{0} items/s", Grouped(static_cast<uint64_t>(std::lround(itemRate))));
            }
            stripValues[0]->SetText(speed);
        }
        if (stripValues[1]) stripValues[1]->SetText(left);
        if (stripValues[2]) {
            stripValues[2]->SetText(figures.totalItems > 0
                    ? Grouped(figures.doneItems) + " / " + Grouped(figures.totalItems)
                    : Grouped(figures.doneItems));
        }
        if (stripValues[3]) {
            stripValues[3]->SetText(figures.totalBytes > 0
                    ? FormatBytes(figures.doneBytes) + " / " + FormatBytes(figures.totalBytes)
                    : FormatBytes(figures.doneBytes));
        }
    }

    void UltraCanvasProgressDialog::SendToBackground() {
        if (closed || backgrounded || !options.onBackground) return;
        backgrounded = true;
        // Held: the callback may drop the caller's last reference to this
        // window while it is still closing.
        auto keep = shared_from_this();
        auto modal = dialog;
        closed = true;
        if (themeHandler != 0) {
            UltraCanvasTheme::RemoveChangeHandler(themeHandler);
            themeHandler = 0;
        }
        dialog.reset();
        if (modal) modal->CloseDialog(DialogResult::Close);
        options.onBackground();
    }

    void UltraCanvasProgressDialog::TogglePause() {
        if (closed || !options.onPause) return;
        paused = !paused;
        // A pause is no measurement of speed: start over when it ends; and
        // its time is no work, so the time left leaves it out.
        samples.clear();
        const auto now = std::chrono::steady_clock::now();
        if (paused) pausedAt = now;
        else if (runStarted) pausedFor += now - pausedAt;
        if (pauseButton) pauseButton->SetText(paused ? Tr("Resume") : Tr("Pause"));
        options.onPause(paused);
        RefreshTexts(true);
    }

    double UltraCanvasProgressDialog::CombinedProgress(const ProgressFigures& f, double ringFraction) {
        auto share = [](uint64_t done, uint64_t total) {
            return std::min(1.0, static_cast<double>(done) / static_cast<double>(total));
        };
        const bool items = f.totalItems > 0;
        const bool bytes = f.totalBytes > 0;
        if (items && bytes) return 0.5 * share(f.doneItems, f.totalItems) + 0.5 * share(f.doneBytes, f.totalBytes);
        if (items) return share(f.doneItems, f.totalItems);
        if (bytes) return share(f.doneBytes, f.totalBytes);
        return ringFraction >= 0.0 ? std::min(1.0, ringFraction) : -1.0;
    }

    double UltraCanvasProgressDialog::EstimateSecondsLeft(double seconds, double fromProgress,
                                                          double progress) {
        if (progress < 0.0 || seconds < kEstimateMinSeconds) return -1.0;
        if (progress >= 1.0) return 0.0;
        const double gained = progress - std::max(0.0, fromProgress);
        if (gained < kEstimateMinProgress) return -1.0;
        return seconds * (1.0 - progress) / gained;
    }

    std::string UltraCanvasProgressDialog::FormatDuration(double seconds) {
        const long long s = static_cast<long long>(std::ceil(std::max(0.0, seconds)));
        if (s < 60) return TrF("{0} s", s);
        if (s < 3600) return TrF("{0} min", (s + 30) / 60);
        const long long minutes = (s + 30) / 60;
        return TrF("{0} h {1} min", minutes / 60, minutes % 60);
    }

    std::string UltraCanvasProgressDialog::FormatBytes(uint64_t bytes) {
        static const char* const kUnits[] = {"B", "KB", "MB", "GB", "TB"};
        double value = static_cast<double>(bytes);
        int unit = 0;
        while (value >= 1024.0 && unit < 4) { value /= 1024.0; ++unit; }
        char buf[32];
        // One decimal below ten, none above: "1.2 GB", "742 MB".
        if (unit == 0 || value >= 10.0) std::snprintf(buf, sizeof buf, "%.0f %s", value, kUnits[unit]);
        else                            std::snprintf(buf, sizeof buf, "%.1f %s", value, kUnits[unit]);
        return buf;
    }

    void UltraCanvasProgressDialog::ApplyThemeColors() {
        const ThemeRoles& roles = UltraCanvasTheme::Current().roles;
        if (ring) {
            ring->SetRingColor(0, roles.accent);
            ring->SetTrackColor(roles.line);
            ring->SetCenterTextColor(roles.ink);
            ring->RequestRedraw();
        }
        // The source a plain chip, the destination the accent's.
        if (fromChip) {
            fromChip->SetBackgroundColor(roles.well);
            fromChip->SetBorders(1.0f, roles.line, 5.0f);
            fromChip->RequestRedraw();
        }
        if (toChip) {
            toChip->SetBackgroundColor(roles.accentSoft);
            toChip->SetBorders(1.0f, roles.accentSoft, 5.0f);
            if (toChipText) toChipText->SetTextColor(roles.accent);
            toChip->RequestRedraw();
        }
        if (strip) {
            strip->SetBackgroundColor(roles.well);
            strip->SetBorders(1.0f, roles.line, 8.0f);
            bool first = true;
            for (auto& cell : stripCells) {
                if (!cell) continue;
                if (!first) cell->SetBorderLeft(1.0f, roles.line);
                first = false;
            }
            strip->RequestRedraw();
        }
    }

    void UltraCanvasProgressDialog::Close() {
        // Before the early return: a window the user closed is closed
        // already, and its handler still has to go.
        if (themeHandler != 0) {
            UltraCanvasTheme::RemoveChangeHandler(themeHandler);
            themeHandler = 0;
        }
        if (closed) return;
        closed = true;
        auto modal = dialog;
        dialog.reset();
        ring.reset();
        if (!modal) return;
        // Closing from code is not the user cancelling: drop the handler first
        // so the caller is not told its own Close() was a cancel.
        modal->onResult = nullptr;
        modal->CloseDialog(DialogResult::OK);
    }

} // namespace UltraCanvas
