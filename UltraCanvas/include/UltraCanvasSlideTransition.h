// include/UltraCanvasSlideTransition.h
// How two pictures change places in a transition, frame by frame: for an
// effect and how far the transition is (0..1), the pictures to draw back to
// front - the one going off and the one coming on - each with its opacity,
// how far it is moved, how much it is scaled about its centre and where it
// may paint. The caller draws its own pictures with that (a photo through
// its fit mode, a slide from a cached pixmap); the geometry lives here once.
// The photo slideshow (UltraCanvasSlideshow) and the presentation player
// (UltraCanvasSlideShowPlayer) both draw with it.
//
// doc-check: example
//   const Rect2Dd area(0, 0, 1280, 720);
//   for (const SlideTransitionLayer& layer :
//        SlideTransitionFrame(SlideTransitionEffect::Cover, area, SlideTransitionEase(0.4))) {
//       // draw layer.incoming ? the new picture : the old one, clipped to
//       // layer.clip, at layer.alpha, moved by layer.offsetX / offsetY
//   }
//
// Version: 1.0.0 - the slideshow's transitions, out of UltraCanvasSlideshow, plus Wipe, Split, Cover and Zoom
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"

#include <algorithm>
#include <vector>

namespace UltraCanvas {

enum class SlideTransitionEffect {
    Cut,         // the new picture at once
    CrossFade,   // the old fades out as the new fades in
    FadeOutIn,   // the old fades out completely, then the new fades in
    PushLeft,    // the new comes in from the right and pushes the old out to the left
    PushUp,      // ... from below, pushing the old out at the top
    Wipe,        // the new is uncovered from left to right over the old
    Split,       // the new opens from a line down the centre outwards, left and right
    Cover,       // the new slides in from the right over the old, which stays
    Zoom,        // the new grows from the centre over the old
    ZoomFade     // the new fades in while shrinking from slightly enlarged
};

// One of the pictures of a frame.
struct SlideTransitionLayer {
    bool incoming = false;            // the picture coming on (else the one going off)
    double alpha = 1.0;               // 0..1
    double offsetX = 0, offsetY = 0;  // how far it is moved from its place
    double scale = 1.0;               // about its centre, after the move; 1: as it is
    Rect2Dd clip;                     // where it may paint (the area, or a part of it)
};

// Smoothstep: slow at both ends, as the transitions look best.
inline double SlideTransitionEase(double t) {
    t = std::max(0.0, std::min(1.0, t));
    return t * t * (3.0 - 2.0 * t);
}

// The pictures of the frame at `progress` (0: the old picture alone, 1: the
// new one alone - ease it first if wanted) of `effect` in `area`, back to
// front. A picture is left out where nothing of it shows.
// `opaque`: both pictures fill the area (slides, not letterboxed photos), so
// in a fade the old one stays at full strength under the new one fading in -
// the frame is then a true mix of the two (half and half half way) rather
// than both faded over what lies beneath.
inline std::vector<SlideTransitionLayer> SlideTransitionFrame(SlideTransitionEffect effect, const Rect2Dd& area,
                                                              double progress, bool opaque = false) {
    const double p = std::max(0.0, std::min(1.0, progress));
    SlideTransitionLayer outgoing, incoming;
    outgoing.clip = area;
    incoming.clip = area;
    incoming.incoming = true;
    std::vector<SlideTransitionLayer> layers;
    switch (effect) {
        case SlideTransitionEffect::Cut:
            layers.push_back(incoming);
            break;
        case SlideTransitionEffect::CrossFade:
            outgoing.alpha = opaque ? 1.0 : 1.0 - p;
            incoming.alpha = p;
            layers = {outgoing, incoming};
            break;
        case SlideTransitionEffect::FadeOutIn:
            if (p < 0.5) {
                outgoing.alpha = 1.0 - p * 2.0;
                layers.push_back(outgoing);
            } else {
                incoming.alpha = (p - 0.5) * 2.0;
                layers.push_back(incoming);
            }
            break;
        case SlideTransitionEffect::PushLeft:
            outgoing.offsetX = -p * area.width;
            incoming.offsetX = (1.0 - p) * area.width;
            layers = {outgoing, incoming};
            break;
        case SlideTransitionEffect::PushUp:
            outgoing.offsetY = -p * area.height;
            incoming.offsetY = (1.0 - p) * area.height;
            layers = {outgoing, incoming};
            break;
        case SlideTransitionEffect::Wipe:
            incoming.clip = Rect2Dd(area.x, area.y, area.width * p, area.height);
            layers = {outgoing, incoming};
            break;
        case SlideTransitionEffect::Split:
            incoming.clip = Rect2Dd(area.x + area.width * (1.0 - p) / 2.0, area.y, area.width * p, area.height);
            layers = {outgoing, incoming};
            break;
        case SlideTransitionEffect::Cover:
            incoming.offsetX = (1.0 - p) * area.width;
            layers = {outgoing, incoming};
            break;
        case SlideTransitionEffect::Zoom:
            incoming.scale = p;
            layers = {outgoing, incoming};
            break;
        case SlideTransitionEffect::ZoomFade:
            outgoing.alpha = opaque ? 1.0 : 1.0 - p;
            incoming.alpha = p;
            incoming.scale = 1.08 - 0.08 * p;
            layers = {outgoing, incoming};
            break;
    }
    // Nothing of a picture shows: leave it out.
    layers.erase(std::remove_if(layers.begin(), layers.end(),
                                [](const SlideTransitionLayer& layer) {
                                    return layer.alpha <= 0.0 || layer.scale <= 0.0 || layer.clip.width <= 0.0 ||
                                           layer.clip.height <= 0.0;
                                }),
                 layers.end());
    return layers;
}

} // namespace UltraCanvas
