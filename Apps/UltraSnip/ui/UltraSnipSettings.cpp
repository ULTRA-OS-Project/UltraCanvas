// Apps/UltraSnip/ui/UltraSnipSettings.cpp
// See the header.
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraSnipSettings.h"

#include "UltraSnipCapture.h"

#include "UltraCanvasAppSettings.h"
#include "UltraCanvasDesktopShell.h"

#include <algorithm>

using namespace UltraCanvas;

namespace UltraSnip {

namespace {
const char* kApp = "UltraSnip";

std::string LowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
} // namespace

const std::vector<std::string>& UltraSnipSettings::RecommendedPictureFormats() {
    static const std::vector<std::string> formats = {"png", "jxl", "avif", "heic", "webp", "jpg"};
    return formats;
}

const std::vector<std::string>& UltraSnipSettings::VideoFormats() {
    static const std::vector<std::string> formats = {"mp4", "mkv", "webm"};
    return formats;
}

void UltraSnipSettings::Load() {
    UltraCanvasAppSettings file(kApp);
    if (!file.Load()) return;
    mode = ModeFromKey(file.GetString("snip.mode", ModeKey(mode)), mode);
    kind = KindFromKey(file.GetString("snip.kind", KindKey(kind)), kind);
    delaySeconds = file.GetInt("snip.delay", delaySeconds);
    if (delaySeconds != 0 && delaySeconds != 3 && delaySeconds != 5 && delaySeconds != 10) delaySeconds = 0;
    pictureFormat = LowerAscii(file.GetString("picture.format", pictureFormat));
    if (pictureFormat.empty()) pictureFormat = "png";
    pictureFolder = file.GetString("picture.folder", pictureFolder);
    autoSave = file.GetBool("save.automatic", autoSave);
    autoCopy = file.GetBool("save.copy", autoCopy);
    videoFormat = LowerAscii(file.GetString("video.format", videoFormat));
    const auto& videos = VideoFormats();
    if (std::find(videos.begin(), videos.end(), videoFormat) == videos.end()) videoFormat = "mp4";
    videoFolder = file.GetString("video.folder", videoFolder);
    frameRate = std::clamp(file.GetInt("video.frameRate", frameRate), 5, 60);
    showWindowAfterSnip = file.GetBool("snip.showWindowAfter", showWindowAfterSnip);
    takePrintKey = file.GetBool("snip.printKey", takePrintKey);
    saveFolder = file.GetString("save.folder", saveFolder);
}

void UltraSnipSettings::Save() const {
    UltraCanvasAppSettings file(kApp);
    file.Load();                                  // keep what another version wrote
    file.Set("snip.mode", ModeKey(mode));
    file.Set("snip.kind", KindKey(kind));
    file.Set("snip.delay", delaySeconds);
    file.Set("picture.format", pictureFormat);
    file.Set("picture.folder", pictureFolder);
    file.Set("save.automatic", autoSave);
    file.Set("save.copy", autoCopy);
    file.Set("video.format", videoFormat);
    file.Set("video.folder", videoFolder);
    file.Set("video.frameRate", frameRate);
    file.Set("snip.showWindowAfter", showWindowAfterSnip);
    file.Set("snip.printKey", takePrintKey);
    file.Set("save.folder", saveFolder);
    file.Save();
}

void UltraSnipSettings::ResetPictures() {
    const UltraSnipSettings defaults;
    pictureFormat = defaults.pictureFormat;
    pictureFolder = defaults.pictureFolder;
    autoSave = defaults.autoSave;
    autoCopy = defaults.autoCopy;
}

void UltraSnipSettings::ResetVideos() {
    const UltraSnipSettings defaults;
    videoFormat = defaults.videoFormat;
    videoFolder = defaults.videoFolder;
    frameRate = defaults.frameRate;
}

void UltraSnipSettings::ResetBehaviour() {
    const UltraSnipSettings defaults;
    showWindowAfterSnip = defaults.showWindowAfterSnip;
    takePrintKey = defaults.takePrintKey;
}

std::string UltraSnipSettings::NewPicturePath() const {
    return SnipFilePath(UltraCanvasDesktopShell::DefaultScreenshotPath(), pictureFolder, pictureFormat);
}

std::string UltraSnipSettings::NewRecordingPath() const {
    return SnipFilePath(UltraCanvasDesktopShell::DefaultRecordingPath(), videoFolder, videoFormat);
}

} // namespace UltraSnip
