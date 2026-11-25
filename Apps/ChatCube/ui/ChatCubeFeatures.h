// Apps/ChatCube/ui/ChatCubeFeatures.h
// The feature icons around the message field - the paperclip, smileys and
// stickers, a screenshot, a location, a voice and a video message - and
// where each one is: in the field's row, behind the + button (its menu), or
// not shown at all. Settings > Display > Feature icons chooses; the choice
// is kept in settings.json under "features.<key>".
//
// Version: 0.1.0
// Last Modified: 2026-11-23
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <array>
#include <string>

namespace ChatCube {

enum class Feature { Attach, Emoji, Screenshot, Location, VoiceMessage, VideoMessage };

// Where a feature's icon is
enum class FeaturePlace { Field, Menu, Off };

inline constexpr std::array<Feature, 6> kAllFeatures = {
    Feature::Attach, Feature::Emoji, Feature::Screenshot,
    Feature::Location, Feature::VoiceMessage, Feature::VideoMessage};

// "attach", "emoji", "screenshot", "location", "voice", "video": the key in
// the settings file
const char* FeatureKey(Feature feature);
// The icon's name under media/icons/chatcube
const char* FeatureIcon(Feature feature);
// Its name as the Settings page and the + menu show it (translated)
std::string FeatureName(Feature feature);
// What it does, a line under the name on the Settings page (translated)
std::string FeatureDescription(Feature feature);
// Where it is when nothing was chosen: the paperclip, the smileys and the
// voice message in the field, the rest behind the +
FeaturePlace DefaultPlace(Feature feature);

// "field", "menu", "off" and back; an unknown key is the default
const char* PlaceKey(FeaturePlace place);
FeaturePlace PlaceFromKey(const std::string& key, FeaturePlace fallback);
// "In the message field", "Behind the + button", "Not shown" (translated)
std::string PlaceName(FeaturePlace place);

// Where each feature is, by kAllFeatures' order
struct FeaturePlaces {
    std::array<FeaturePlace, kAllFeatures.size()> place{};

    FeaturePlaces();     // every feature at its default
    FeaturePlace Get(Feature feature) const { return place[static_cast<size_t>(feature)]; }
    void Set(Feature feature, FeaturePlace p) { place[static_cast<size_t>(feature)] = p; }
    // Whether anything is behind the +, so it is shown at all
    bool AnyInMenu() const;
    bool operator==(const FeaturePlaces&) const = default;
};

} // namespace ChatCube
