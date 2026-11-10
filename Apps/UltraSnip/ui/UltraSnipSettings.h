// Apps/UltraSnip/ui/UltraSnipSettings.h
// What UltraSnip remembers between starts, in the framework's per-application
// settings file (UltraCanvasAppSettings: <settings folder>/UltraSnip/
// settings.json): the last shape, kind and delay; for pictures the file
// type they are written as, where, and whether they are saved and copied by
// themselves; for videos the file type, the folder and the frame rate; what
// happens after a snip; and the folder Save as used last. The settings
// window (UltraSnipSettingsDialog) edits them, the main window reads them.
// Version: 0.3.0 - takePrintKey: the Print Screen key starts a snip
// Version: 0.2.0 - the picture and video file types and folders, the window
//                  after a snip; the paths of a new snip; the defaults per page
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraSnipTypes.h"

#include <string>
#include <vector>

namespace UltraSnip {

struct UltraSnipSettings {
    // ---- the snip ----
    SnipMode mode = SnipMode::Rectangle;
    SnipKind kind = SnipKind::Picture;
    int delaySeconds = 0;

    // ---- pictures ----
    std::string pictureFormat = "png";  // the extension the automatic save and Edit in UltraPaint write
    std::string pictureFolder;          // "" = <Pictures>/Screenshots (DefaultScreenshotPath's folder)
    bool autoSave = true;               // every picture is written as it is taken
    bool autoCopy = true;               // every picture goes to the clipboard (always as PNG)

    // ---- videos ----
    std::string videoFormat = "mp4";    // mp4, mkv or webm: the container, and with it the codec
    std::string videoFolder;            // "" = <Videos>/Screen Recordings
    int frameRate = 20;                 // 5..60

    // ---- after a snip ----
    bool showWindowAfterSnip = true;    // false: a snip started from the desktop ends without the window
    // The Print Screen key starts a snip while UltraSnip runs - taken from
    // the system, so it is not Windows' Snipping Tool that opens
    bool takePrintKey = true;

    std::string saveFolder;             // where Save as starts; "" = the default folder

    void Load();
    void Save() const;

    // Back to the defaults of one settings page
    void ResetPictures();
    void ResetVideos();
    void ResetBehaviour();

    // The file a new picture / recording is written to: the default name
    // (Screenshot <date> <time>, Screen recording <date> <time>) in the
    // chosen folder with the chosen type
    std::string NewPicturePath() const;
    std::string NewRecordingPath() const;

    // The picture types offered first, most useful first: PNG, JPEG XL,
    // AVIF, HEIC, WebP, JPEG - whether or not this build writes them (the
    // settings page says which it cannot)
    static const std::vector<std::string>& RecommendedPictureFormats();
    // The video containers a recording can be written as
    static const std::vector<std::string>& VideoFormats();
};

} // namespace UltraSnip
