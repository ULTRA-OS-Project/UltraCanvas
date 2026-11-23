// Apps/ChatCube/ui/ChatCubeFeatures.cpp
// See the header.
// Version: 0.1.0
// Last Modified: 2026-11-23
// Author: UltraCanvas Framework / ULTRA OS

#include "ChatCubeFeatures.h"

#include "UltraLocalisation/UltraLocalisation.h"

using UltraCanvas::Tr;

namespace ChatCube {

const char* FeatureKey(Feature feature) {
    switch (feature) {
        case Feature::Attach: return "attach";
        case Feature::Emoji: return "emoji";
        case Feature::Screenshot: return "screenshot";
        case Feature::Location: return "location";
        case Feature::VoiceMessage: return "voice";
        case Feature::VideoMessage: return "video";
    }
    return "attach";
}

const char* FeatureIcon(Feature feature) {
    switch (feature) {
        case Feature::Attach: return "attach";
        case Feature::Emoji: return "smile";
        case Feature::Screenshot: return "screenshot";
        case Feature::Location: return "location";
        case Feature::VoiceMessage: return "mic";
        case Feature::VideoMessage: return "video-message";
    }
    return "attach";
}

std::string FeatureName(Feature feature) {
    switch (feature) {
        case Feature::Attach: return Tr("Picture, video or file");
        case Feature::Emoji: return Tr("Smileys & stickers");
        case Feature::Screenshot: return Tr("Screenshot");
        case Feature::Location: return Tr("Location");
        case Feature::VoiceMessage: return Tr("Voice message");
        case Feature::VideoMessage: return Tr("Video message");
    }
    return {};
}

std::string FeatureDescription(Feature feature) {
    switch (feature) {
        case Feature::Attach: return Tr("The paperclip: a picture, a video or any file from this computer");
        case Feature::Emoji: return Tr("A smiley into the text, or a sticker as a picture; the arrow beside it chooses the set");
        case Feature::Screenshot: return Tr("A window or an area of the screen, as a picture");
        case Feature::Location: return Tr("A place found on OpenStreetMap, as a link and a map picture");
        case Feature::VoiceMessage: return Tr("The microphone: a voice message");
        case Feature::VideoMessage: return Tr("The camera: a short video message");
    }
    return {};
}

FeaturePlace DefaultPlace(Feature feature) {
    switch (feature) {
        case Feature::Attach:
        case Feature::Emoji:
        case Feature::VoiceMessage:
            return FeaturePlace::Field;
        case Feature::Screenshot:
        case Feature::Location:
        case Feature::VideoMessage:
            break;
    }
    return FeaturePlace::Menu;
}

const char* PlaceKey(FeaturePlace place) {
    switch (place) {
        case FeaturePlace::Field: return "field";
        case FeaturePlace::Menu: return "menu";
        case FeaturePlace::Off: return "off";
    }
    return "field";
}

FeaturePlace PlaceFromKey(const std::string& key, FeaturePlace fallback) {
    if (key == "field") return FeaturePlace::Field;
    if (key == "menu") return FeaturePlace::Menu;
    if (key == "off") return FeaturePlace::Off;
    return fallback;
}

std::string PlaceName(FeaturePlace place) {
    switch (place) {
        case FeaturePlace::Field: return Tr("In the message field");
        case FeaturePlace::Menu: return Tr("Behind the + button");
        case FeaturePlace::Off: return Tr("Not shown");
    }
    return {};
}

FeaturePlaces::FeaturePlaces() {
    for (Feature f : kAllFeatures) Set(f, DefaultPlace(f));
}

bool FeaturePlaces::AnyInMenu() const {
    for (FeaturePlace p : place) {
        if (p == FeaturePlace::Menu) return true;
    }
    return false;
}

} // namespace ChatCube
