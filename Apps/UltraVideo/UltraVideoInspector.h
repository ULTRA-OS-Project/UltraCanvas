// Apps/UltraVideo/UltraVideoInspector.h
// The inspector (Docs/Research/UltraVideoDesignProposal.md §6.4): three tabs
// of catalogue elements. Clip - the selected clip's name, in and out, speed,
// mute, the transition into it, a still's motion and framing. Audio - the
// music: the songs, volume, fades, crossfade, loop, ducking. Project - name,
// size, frame rate, fit. Every field reports an edit as a closure over the
// document; the window applies it through the undo history and shows the
// document again, so the inspector never writes the document itself.
// Version: 0.1.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework
#pragma once

#include "UltraVideoDocument.h"
#include "UltraVideoTheme.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasSwitch.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTimecodeField.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraVideo {

class Inspector {
public:
    std::shared_ptr<UltraCanvas::UltraCanvasTabbedContainer> Build(const Palette& palette);
    void ApplyPalette(const Palette& palette);

    // Show the document: `segment` is the selected clip's segment, `title`
    // the selected title span and `overlay` the selected overlay clip, -1 for none
    void Show(const Document& document, int segment, int title = -1, int overlay = -1);
    void ShowClipTab();
    void ShowAudioTab();

    // An edit of the document, named for the history
    std::function<void(const std::string& label, std::function<void(Document&)> edit)> onEdit;
    std::function<void()> onAddMusic;      // the Audio tab's Add song: a file dialog, the window's

private:
    using Label = UltraCanvas::UltraCanvasLabel;
    using Dropdown = UltraCanvas::UltraCanvasDropdown;
    using Slider = UltraCanvas::UltraCanvasSlider;
    using Switch = UltraCanvas::UltraCanvasSwitch;
    using TimecodeField = UltraCanvas::UltraCanvasTimecodeField;
    using Container = UltraCanvas::UltraCanvasContainer;

    std::shared_ptr<Container> Page(const std::string& id);
    std::shared_ptr<Container> Row(const std::shared_ptr<Container>& page, const std::string& id, const std::string& caption,
                                   std::shared_ptr<UltraCanvas::UltraCanvasUIElement> field);
    std::shared_ptr<Label> Heading(const std::shared_ptr<Container>& page, const std::string& id, const std::string& text);
    void Edit(const std::string& label, std::function<void(Document&)> edit);
    void StyleControls(const Palette& palette);

    std::shared_ptr<UltraCanvas::UltraCanvasTabbedContainer> tabs;
    std::shared_ptr<Container> clipPage, audioPage, projectPage;
    std::vector<std::shared_ptr<Label>> captions, headings;
    std::vector<std::shared_ptr<Dropdown>> dropdowns;
    std::vector<std::shared_ptr<Slider>> sliders;
    std::vector<std::shared_ptr<Switch>> switches;
    std::vector<std::shared_ptr<TimecodeField>> timecodes;

    // Clip
    std::shared_ptr<Label> clipName, clipKind, clipDuration, speedValue, transitionMax;
    std::shared_ptr<Container> clipFields, clipEmpty, fileRows, imageRows, transitionRows;
    std::shared_ptr<TimecodeField> inField, outField, transitionLength;
    std::shared_ptr<Slider> speed;
    std::shared_ptr<Switch> mute;
    std::shared_ptr<Dropdown> transitionType, motion, imageFit;
    std::shared_ptr<Switch> keepFaces;
    std::shared_ptr<TimecodeField> stillLength;
    std::shared_ptr<Container> stillRow;
    // the clip's effects, one row each, rebuilt on every Show
    std::shared_ptr<Container> effectRows, effectList;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> clearEffects;
    std::shared_ptr<Label> effectsEmpty;
    std::vector<std::shared_ptr<Label>> effectLabels;
    std::vector<std::shared_ptr<UltraCanvas::UltraCanvasButton>> effectButtons;
    const Palette* palette = nullptr;
    // a title span
    std::shared_ptr<Container> titleRows;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> titleText;
    std::shared_ptr<Dropdown> titleStyle, titleAnchor, titleColor;
    std::shared_ptr<Slider> titleSize, titleFadeIn, titleFadeOut;
    std::shared_ptr<Label> titleSizeValue, titleFadeInValue, titleFadeOutValue;
    std::shared_ptr<Switch> titleShadow, titleBox;
    std::shared_ptr<TimecodeField> titleLength;
    // an overlay clip (V2)
    std::shared_ptr<Container> overlayRows;
    std::shared_ptr<TimecodeField> overlayLength;
    std::shared_ptr<Dropdown> overlayAnchor;
    std::shared_ptr<Slider> overlaySize, overlayOpacity, overlayFadeIn, overlayFadeOut;
    std::shared_ptr<Label> overlaySizeValue, overlayOpacityValue, overlayFadeInValue, overlayFadeOutValue;

    // Audio
    std::shared_ptr<Label> songs;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> addSong, removeSongs;
    std::shared_ptr<Slider> musicVolume, fadeIn, fadeOut, crossfade, duckLevel;
    std::shared_ptr<Label> musicVolumeValue, fadeInValue, fadeOutValue, crossfadeValue, duckValue;
    std::shared_ptr<Switch> loop;
    std::shared_ptr<Dropdown> ducking;

    // Project
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> projectName;
    std::shared_ptr<Dropdown> size, frameRate, fitMode;

    int segment = -1;
    int title = -1;
    int overlay = -1;
    double documentFrameRate = 25.0;
    bool updating = false;
};

} // namespace UltraVideo
