// Apps/DemoApp/UltraCanvasHistogramChartExamples.cpp
// Charts > Histogram Chart: a picture on the left and its histogram
// (UltraCanvasHistogramChart) on the right - Combined (red, green and blue overlaid) or one
// channel, linear or logarithmic, with each channel's statistics and the
// level under the pointer read out. Sample pictures ship in media/images;
// "Open a picture" loads any other through UltraCanvasFileLoader.
// Version: 1.1.0 - the picture (artist.jpg to start with) on the left, the histogram on the
//                  right, the options, statistics and read-out underneath
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasHistogramChart.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSwitch.h"
#include "UltraCanvasUtils.h"

#include <cstdio>
#include <memory>
#include <string>

namespace UltraCanvas {

namespace {

struct HistogramDemoState {
    std::shared_ptr<UltraCanvasHistogramChart> chart;
    std::shared_ptr<UltraCanvasImageElement> picture;
    std::shared_ptr<UltraCanvasLabel> statistics;
    std::shared_ptr<UltraCanvasLabel> readout;
};

std::string Fixed(double v, int decimals) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, v);   // locale-ok: display text
    return buffer;
}

void ShowStatistics(HistogramDemoState& state) {
    const ImageHistogram& h = state.chart->GetHistogram();
    if (h.Empty()) {
        state.statistics->SetText(Tr("No pixels to count."));
        return;
    }
    std::string text = TrN("{n} pixel", "{n} pixels", static_cast<long long>(h.pixelCount));
    const std::string names[] = {Tr("Red"), Tr("Green"), Tr("Blue"), Tr("Luminosity")};
    for (HistogramChannel c : state.chart->ShownChannels()) {
        const HistogramStatistics s = h.Statistics(c);
        text += "\n" + TrF("{0}: mean {1}, median {2}, std dev {3}\n    clipped: {4} % at 0, {5} % at 255",
                           names[static_cast<int>(c)], Fixed(s.mean, 1), s.median, Fixed(s.standardDeviation, 1),
                           Fixed(s.shadowsClipped * 100.0, 2), Fixed(s.highlightsClipped * 100.0, 2));
    }
    state.statistics->SetText(text);
}

void ShowImage(HistogramDemoState& state, const std::shared_ptr<UCImage>& image) {
    state.picture->LoadFromImage(image);
    state.chart->SetImage(image);
    ShowStatistics(state);
}

} // namespace

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateHistogramChartExamples() {
    auto page = DemoKit::CreatePage("histogramMainContainer", Tr("Histogram Chart"),
                                    Tr("How many pixels of a picture have each of the 256 levels: red, green and "
                                    "blue together, or one channel at a time. Hover the chart for a level's "
                                    "counts."));
    auto state = std::make_shared<HistogramDemoState>();

    // ----- the picture on the left, its histogram on the right -----
    auto row = DemoKit::AddRow(page, "histogramContent");
    auto pictureCard = DemoKit::AddCard(row, "histogramPictureCard", Tr("Picture"), "", 370);
    state->picture = CreateImageElement("histogramPicture", 340, 400);
    state->picture->SetFitMode(ImageFitMode::Contain);
    DemoKit::AddFixed(pictureCard, state->picture);

    auto chartCard = DemoKit::AddCard(row, "histogramChartCard", Tr("Histogram"), "", 590);
    state->chart = CreateHistogramChart("histogramChart", 0, 0, 560, 400);
    DemoKit::AddFixed(chartCard, state->chart);

    // ----- the controls and the read-outs underneath -----
    auto controlsRow = DemoKit::AddRow(page, "histogramControlsRow");
    auto controls = DemoKit::AddCard(controlsRow, "histogramControls", Tr("Options"), "", 260);
    auto addCaption = [](const std::shared_ptr<UltraCanvasContainer>& card, const std::string& id,
                         const std::string& text) {
        DemoKit::AddCaption(card, id, text)->SetFontWeight(FontWeight::Bold);
    };

    addCaption(controls, "histogramPictureCaption", Tr("Picture"));
    auto pictures = std::make_shared<UltraCanvasDropdown>("histogramPictureChoice", 0, 0, 220, 28);
    const std::string mediaRoot = NormalizePath(GetResourcesDir() + "media/images/");
    pictures->AddItem(Tr("Artist"), mediaRoot + "artist.jpg");
    pictures->AddItem(Tr("Landscape"), mediaRoot + "landscape.jpg");
    pictures->AddItem(Tr("Portrait"), mediaRoot + "portrait.jpg");
    pictures->AddItem(Tr("Photo"), mediaRoot + "sample_photo.jpg");
    pictures->AddItem(Tr("Dice"), mediaRoot + "dice.jpg");
    pictures->SetSelectedIndex(0);
    DemoKit::AddStretched(controls, pictures);
    auto open = std::make_shared<UltraCanvasButton>("histogramOpen", 0, 0, 220, 28, Tr("Open a picture"));
    DemoKit::AddStretched(controls, open);

    addCaption(controls, "histogramDisplayCaption", Tr("Channels"));
    auto channels = std::make_shared<UltraCanvasDropdown>("histogramDisplay", 0, 0, 220, 28);
    channels->AddItem(Tr("Combined (red, green, blue)"), "combined");
    channels->AddItem(Tr("Red"), "red");
    channels->AddItem(Tr("Green"), "green");
    channels->AddItem(Tr("Blue"), "blue");
    channels->AddItem(Tr("Luminosity"), "luminosity");
    channels->SetSelectedIndex(0);
    DemoKit::AddStretched(controls, channels);
    auto logarithmic = UltraCanvasSwitch::Create("histogramLog", 0, 0, Tr("Logarithmic scale"), false);
    DemoKit::Add(controls, logarithmic);

    auto statisticsCard = DemoKit::AddCard(controlsRow, "histogramStatisticsCard", Tr("Statistics"), "", 360);
    state->statistics = DemoKit::AddCaption(statisticsCard, "histogramStatistics", "");
    state->statistics->SetTextColor(DemoKit::BodyText());

    auto readoutCard = DemoKit::AddCard(controlsRow, "histogramReadoutCard", Tr("Under the pointer"), "", 220);
    state->readout = DemoKit::AddCaption(readoutCard, "histogramReadout", Tr("Hover the chart."));
    state->readout->SetTextColor(DemoKit::BodyText());

    pictures->onSelectionChanged = [state](int, const DropdownItem& item) {
        ShowImage(*state, UCImage::Get(item.value));
    };
    open->onClick = [state]() {
        FileDialogOptions options;
        options.title = Tr("Open a picture");
        UltraCanvasFileLoader::OpenImage(options, [state](const FileLoadResult& result, std::shared_ptr<UCImage> image) {
            if (!result.IsSuccess() || !image) return;
            ShowImage(*state, image);
        });
    };
    channels->onSelectionChanged = [state](int, const DropdownItem& item) {
        state->chart->SetDisplay(item.value == "red"          ? HistogramDisplay::Red
                                 : item.value == "green"      ? HistogramDisplay::Green
                                 : item.value == "blue"       ? HistogramDisplay::Blue
                                 : item.value == "luminosity" ? HistogramDisplay::Luminosity
                                                              : HistogramDisplay::Combined);
        ShowStatistics(*state);
    };
    logarithmic->onStateChanged = [state](CheckedState, CheckedState now) {
        state->chart->SetScale(now == CheckedState::Checked ? HistogramScale::Logarithmic : HistogramScale::Linear);
    };
    std::weak_ptr<HistogramDemoState> weak = state;
    state->chart->onLevelHover = [weak](int level) {
        auto s = weak.lock();
        if (!s) return;
        s->readout->SetText(level < 0 ? Tr("Hover the chart.") : s->chart->DescribeLevel(level));
    };

    ShowImage(*state, UCImage::Get(mediaRoot + "artist.jpg"));
    return page;
}

} // namespace UltraCanvas
