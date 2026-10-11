// Apps/UltraVideo/UltraVideoDocument.cpp
// The document and the .ultravideo file (UltraVideoDocument.h).
// Version: 0.1.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework
#include "UltraVideoDocument.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;
using namespace UltraCanvas;
using namespace VideoFX;

namespace UltraVideo {

namespace {

constexpr int kFileVersion = 1;
constexpr double kDefaultPictureSeconds = 4.0;

std::string LowerExtension(const std::string& path) {
    std::string ext = PathToUtf8(PathFromUtf8(path).extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

std::string FileName(const std::string& path) {
    return PathToUtf8(PathFromUtf8(path).filename());
}

std::string Clock(double seconds) {
    const int total = static_cast<int>(std::lround(seconds));
    std::ostringstream out;
    out.imbue(std::locale::classic());
    if (total >= 3600) out << total / 3600 << ':' << std::setw(2) << std::setfill('0') << (total / 60) % 60 << ':';
    else out << total / 60 << ':';
    out << std::setw(2) << std::setfill('0') << total % 60;
    return out.str();
}

// Paths in the file are relative to its folder when they are inside it
std::string Relative(const std::string& path, const fs::path& base) {
    if (base.empty()) return path;
    std::error_code ec;
    const fs::path p = PathFromUtf8(path);
    const fs::path rel = fs::relative(p, base, ec);
    if (ec || rel.empty()) return path;
    const std::string text = PathToUtf8(rel);
    if (text.rfind("..", 0) == 0) return path;
    return text;
}

std::string Absolute(const std::string& path, const fs::path& base) {
    const fs::path p = PathFromUtf8(path);
    if (p.is_absolute() || base.empty()) return path;
    return PathToUtf8((base / p).lexically_normal());
}

JSONValue MarkerToJson(const TimelineMarker& m) {
    JSONValue o = JSONValue::MakeObject();
    o.Set("id", m.id);
    o.Set("seconds", m.seconds);
    o.Set("color", static_cast<int64_t>(m.color));
    o.Set("label", m.label);
    return o;
}

} // namespace

std::string OverlaySpan::Name() const {
    const std::string name = PathToUtf8(PathFromUtf8(path).filename());
    return name.empty() ? "Overlay" : name;
}

bool OverlaySpan::IsStill() const { return Document::KindOfPath(path) == MediaKind::Image; }

namespace {
// The first free place on a lane of spans at or after `at`: spans never
// overlap on a lane, so one asked for over another goes after it
template <class Span>
double FreePlace(const std::vector<Span>& spans, double at, double duration) {
    bool moved = true;
    while (moved) {
        moved = false;
        for (const Span& other : spans) {
            if (at < other.End() && other.start < at + duration) {
                at = other.End();
                moved = true;
            }
        }
    }
    return at;
}
}

std::string TitleSpan::Name() const {
    const std::string::size_type nl = look.text.find('\n');
    const std::string first = nl == std::string::npos ? look.text : look.text.substr(0, nl);
    return first.empty() ? "Title" : first;
}

const std::vector<TitleStyle>& TitleStyles() {
    static const std::vector<TitleStyle> styles = [] {
        std::vector<TitleStyle> v;
        TitleStyle opening{"opening", "Opening title", "Large, centred, fading in and out", VideoFXOverlay::Text("", VideoFXAnchor::Center, 0.11), 0.6, 0.6, 4.0};
        opening.look.shadow = true;
        v.push_back(opening);
        TitleStyle lower{"lower", "Lower third", "A name at the bottom left on a band", VideoFXOverlay::Text("", VideoFXAnchor::BottomLeft, 0.05), 0.3, 0.3, 5.0};
        lower.look.box = true;
        lower.look.boxColor = 0x000000;
        lower.look.boxOpacity = 0.55;
        lower.look.shadow = false;
        lower.look.margin = 0.07;
        v.push_back(lower);
        TitleStyle caption{"caption", "Caption", "Small, along the bottom, as a slideshow's captions", VideoFXOverlay::Text("", VideoFXAnchor::Bottom, 0.045), 0.25, 0.25, 4.0};
        caption.look.shadow = true;
        v.push_back(caption);
        TitleStyle card{"card", "Closing card", "Large on a dark band, for a last word", VideoFXOverlay::Text("", VideoFXAnchor::Center, 0.08), 0.8, 0.8, 4.0};
        card.look.box = true;
        card.look.boxColor = 0x000000;
        card.look.boxOpacity = 0.7;
        v.push_back(card);
        return v;
    }();
    return styles;
}

const TitleStyle* FindTitleStyle(const std::string& key) {
    for (const TitleStyle& t : TitleStyles()) if (key == t.key) return &t;
    return nullptr;
}

std::string MediaItem::Badge() const {
    if (!error.empty()) return "unreadable";
    if (kind == MediaKind::Image) return "photo";
    return Clock(duration);
}

MediaKind Document::KindOfPath(const std::string& path) {
    static const char* const images[] = {".jpg", ".jpeg", ".png", ".webp", ".heic", ".heif", ".tif", ".tiff", ".bmp",
                                         ".avif", ".jxl", ".gif"};
    static const char* const sounds[] = {".mp3", ".m4a", ".aac", ".wav", ".flac", ".ogg", ".opus", ".wma", ".aiff"};
    const std::string ext = LowerExtension(path);
    for (const char* e : images) if (ext == e) return MediaKind::Image;
    for (const char* e : sounds) if (ext == e) return MediaKind::Audio;
    return MediaKind::Unknown;
}

std::string Document::Name() const {
    if (!project.title.empty()) return project.title;
    if (!path.empty()) return PathToUtf8(PathFromUtf8(path).stem());
    return "Untitled";
}

// The output size: the settings', else the first video's on the timeline
// (a photo's own size says nothing about the video it goes into: pictures
// alone make a 1080p video, as the engine's slideshow does)
int Document::Width() const {
    if (project.settings.width > 0) return project.settings.width;
    for (const VideoFXSegment& s : project.segments)
        if (s.kind == VideoFXSourceKind::File) { const MediaItem* m = FindMedia(s.path); if (m && m->width > 0 && m->kind == MediaKind::Video) return m->width; }
    return 1920;
}

int Document::Height() const {
    if (project.settings.height > 0) return project.settings.height;
    for (const VideoFXSegment& s : project.segments)
        if (s.kind == VideoFXSourceKind::File) { const MediaItem* m = FindMedia(s.path); if (m && m->height > 0 && m->kind == MediaKind::Video) return m->height; }
    return 1080;
}

double Document::FrameRate() const {
    if (project.settings.frameRate > 0.0) return project.settings.frameRate;
    for (const VideoFXSegment& s : project.segments)
        if (s.kind == VideoFXSourceKind::File) { const MediaItem* m = FindMedia(s.path); if (m && m->frameRate > 0.0 && m->kind == MediaKind::Video) return m->frameRate; }
    return 25.0;
}

std::string Document::Describe() const {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << Width() << " \xC3\x97 " << Height() << " \xC2\xB7 ";
    const double fps = FrameRate();
    if (std::fabs(fps - std::round(fps)) < 0.01) out << static_cast<int>(std::lround(fps));
    else out << std::fixed << std::setprecision(2) << fps;
    out << " fps";
    return out.str();
}

double Document::SegmentSeconds(size_t index) const {
    if (index >= project.segments.size()) return 0.0;
    const VideoFXSegment& s = project.segments[index];
    if (s.kind != VideoFXSourceKind::File) return s.duration;
    double end = s.end;
    if (end <= 0.0) {
        const MediaItem* m = FindMedia(s.path);
        end = m ? m->duration : 0.0;
    }
    const double range = std::max(0.0, end - s.start);
    return s.speed > 0.0 ? range / s.speed : range;
}

double Document::Length() const {
    double at = 0.0;
    for (size_t i = 0; i < project.segments.size(); ++i) {
        const double seconds = SegmentSeconds(i);
        if (i > 0 && !project.segments[i].transitionIn.IsCut()) {
            at -= std::min(project.segments[i].transitionIn.duration, std::min(seconds, SegmentSeconds(i - 1)));
        }
        at = std::max(0.0, at) + seconds;
    }
    return at;
}

std::string Document::NewClipId() {
    return "c" + std::to_string(nextClipId++);
}

std::string Document::NewOverlayId() {
    return "o" + std::to_string(nextOverlayId++);
}

std::string Document::NewTitleId() {
    return "t" + std::to_string(nextTitleId++);
}

std::vector<double> Document::SegmentStarts() const {
    std::vector<double> starts;
    double at = 0.0;
    for (size_t i = 0; i < project.segments.size(); ++i) {
        const double seconds = SegmentSeconds(i);
        if (i > 0 && !project.segments[i].transitionIn.IsCut()) {
            at -= std::min(project.segments[i].transitionIn.duration, std::min(seconds, SegmentSeconds(i - 1)));
        }
        at = std::max(0.0, at);
        starts.push_back(at);
        at += seconds;
    }
    return starts;
}

std::vector<VideoFXSegment> Document::SegmentsForEngine() const {
    std::vector<VideoFXSegment> segments = project.segments;
    if (titles.empty() && overlays.empty()) return segments;
    const std::vector<double> starts = SegmentStarts();
    for (size_t i = 0; i < segments.size(); ++i) {
        const double s = starts[i];
        const double e = s + SegmentSeconds(i);
        // The overlay lane lies under the titles: its pieces go first
        for (const OverlaySpan& v : overlays) {
            if (v.path.empty()) continue;
            const double a = std::max(s, v.start);
            const double b = std::min(e, v.End());
            if (b - a < 0.04) continue;
            VideoFXOverlay o = v.look;
            if (v.IsStill()) {
                o.kind = VideoFXOverlayKind::Image;
                o.imagePath = v.path;
                o.videoPath.clear();
            } else {
                o.kind = VideoFXOverlayKind::Video;
                o.videoPath = v.path;
                o.videoStart = v.sourceIn + (a - v.start);
                o.imagePath.clear();
            }
            o.start = a - s;
            o.end = b - s;
            if (o.end >= SegmentSeconds(i) - 1e-6) o.end = 0.0;
            o.fadeIn = a <= v.start + 1e-6 ? std::min(v.fadeIn, b - a) : 0.0;
            o.fadeOut = b >= v.End() - 1e-6 ? std::min(v.fadeOut, b - a) : 0.0;
            segments[i].overlays.push_back(o);
        }
        for (const TitleSpan& t : titles) {
            if (t.look.text.empty()) continue;
            const double a = std::max(s, t.start);
            const double b = std::min(e, t.End());
            if (b - a < 0.04) continue;
            VideoFXOverlay o = t.look;
            o.kind = VideoFXOverlayKind::Text;
            o.start = a - s;
            o.end = b - s;
            if (o.end >= SegmentSeconds(i) - 1e-6) o.end = 0.0;   // to the segment's end
            o.fadeIn = a <= t.start + 1e-6 ? std::min(t.fadeIn, b - a) : 0.0;
            o.fadeOut = b >= t.End() - 1e-6 ? std::min(t.fadeOut, b - a) : 0.0;
            segments[i].overlays.push_back(o);
        }
    }
    return segments;
}

int Document::AddTitle(const std::string& styleKey, double at, const std::string& text) {
    const TitleStyle* style = FindTitleStyle(styleKey);
    if (!style) style = &TitleStyles().front();
    TitleSpan span;
    span.id = NewTitleId();
    span.style = style->key;
    span.look = style->look;
    span.look.text = text.empty() ? style->label : text;
    span.fadeIn = style->fadeIn;
    span.fadeOut = style->fadeOut;
    span.duration = style->seconds;
    // Over the picture, not after it: a title asked for at or past the end
    // sits on the last seconds
    const double length = Length();
    if (length > 0.0 && at + 0.04 >= length) at = length - span.duration;
    // Not over another title: the lane holds one span at a time
    span.start = FreePlace(titles, std::max(0.0, at), span.duration);
    titles.push_back(span);
    return static_cast<int>(titles.size()) - 1;
}

int Document::AddOverlay(const std::string& path, double at) {
    const MediaItem* m = FindMedia(path);
    if (!m) m = &Import(path);
    if (!m->error.empty() || m->kind == MediaKind::Audio || m->kind == MediaKind::Unknown) return -1;
    OverlaySpan span;
    span.id = NewOverlayId();
    span.path = m->path;
    span.duration = m->kind == MediaKind::Video && m->duration > 0.04 ? m->duration : 4.0;
    span.look = VideoFXOverlay::Video(m->path, VideoFXAnchor::BottomRight, 0.3);
    span.look.margin = 0.04;
    const double length = Length();
    if (length > 0.0 && at + 0.04 >= length) at = length - span.duration;
    span.start = FreePlace(overlays, std::max(0.0, at), span.duration);
    overlays.push_back(span);
    return static_cast<int>(overlays.size()) - 1;
}

int Document::OverlayIndex(const std::string& id) const {
    for (size_t i = 0; i < overlays.size(); ++i) if (overlays[i].id == id) return static_cast<int>(i);
    return -1;
}

int Document::TitleIndex(const std::string& id) const {
    for (size_t i = 0; i < titles.size(); ++i) if (titles[i].id == id) return static_cast<int>(i);
    return -1;
}

const MediaItem* Document::FindMedia(const std::string& p) const {
    for (const MediaItem& m : media) if (m.path == p) return &m;
    return nullptr;
}

MediaItem* Document::FindMedia(const std::string& p) {
    for (MediaItem& m : media) if (m.path == p) return &m;
    return nullptr;
}

MediaItem& Document::Import(const std::string& p) {
    std::error_code ec;
    const fs::path abs = fs::absolute(PathFromUtf8(p), ec);
    const std::string absolute = ec ? p : PathToUtf8(abs);
    MediaItem* item = FindMedia(absolute);
    if (!item) {
        media.push_back(MediaItem{});
        item = &media.back();
        item->path = absolute;
    }
    item->name = FileName(absolute);
    item->kind = KindOfPath(absolute);
    item->error.clear();
    VideoFXMediaInfo info;
    const VideoFXResult r = VideoFX_Probe(absolute, info);
    if (r != VideoFXResult::Ok) {
        item->error = VideoFX_GetLastError();
        if (item->error.empty()) item->error = VideoFX_ResultToString(r);
        return *item;
    }
    item->duration = info.duration;
    item->width = info.width;
    item->height = info.height;
    item->frameRate = info.frameRate;
    item->hasAudio = info.HasAudio();
    if (item->kind == MediaKind::Unknown) {
        if (info.HasVideo() && info.duration > 0.0) item->kind = MediaKind::Video;
        else if (info.HasVideo()) item->kind = MediaKind::Image;
        else if (info.HasAudio()) item->kind = MediaKind::Audio;
    }
    if (item->kind == MediaKind::Image) item->duration = 0.0;
    return *item;
}

bool Document::MediaInUse(const std::string& p) const {
    for (const VideoFXSegment& s : project.segments) if (s.path == p) return true;
    for (const OverlaySpan& v : overlays) if (v.path == p) return true;
    for (const std::string& song : project.settings.music.Songs()) if (song == p) return true;
    return false;
}

bool Document::RemoveMedia(const std::string& p) {
    if (MediaInUse(p)) return false;
    media.erase(std::remove_if(media.begin(), media.end(), [&](const MediaItem& m) { return m.path == p; }), media.end());
    return true;
}

int Document::AppendMedia(const MediaItem& item, double seconds) {
    return InsertMedia(project.segments.size(), item, seconds);
}

int Document::InsertMedia(size_t index, const MediaItem& item, double seconds) {
    if (!item.error.empty()) return -1;
    index = std::min(index, project.segments.size());
    VideoFXSegment segment;
    switch (item.kind) {
        case MediaKind::Video:
            segment = VideoFXSegment::FromFile(item.path);
            break;
        case MediaKind::Image:
            segment = VideoFXSegment::FromImage(item.path, seconds > 0.0 ? seconds : kDefaultPictureSeconds);
            break;
        case MediaKind::Audio: {
            VideoFXMusic& music = project.settings.music;
            if (!music.IsSet()) music = VideoFXMusic::FromFile(item.path);
            else music.playlist.push_back(item.path);
            return -1;
        }
        case MediaKind::Unknown:
            return -1;
    }
    project.segments.insert(project.segments.begin() + static_cast<std::ptrdiff_t>(index), segment);
    clipIds.insert(clipIds.begin() + static_cast<std::ptrdiff_t>(index), NewClipId());
    return static_cast<int>(index);
}

bool Document::Save(const std::string& toPath, std::string& error) {
    error.clear();
    if (clipIds.size() != project.segments.size()) {
        clipIds.resize(project.segments.size());
        for (std::string& id : clipIds) if (id.empty()) id = NewClipId();
    }
    const fs::path file = PathFromUtf8(toPath);
    const fs::path base = file.parent_path();
    // The engine's project carries the titles as overlays, so `videofx render`
    // works on it; the spans themselves follow in `titles`
    VideoFXProject rendered = project;
    rendered.segments = SegmentsForEngine();
    std::string projectText;
    const VideoFXResult r = VideoFX_ProjectToJson(rendered, projectText, PathToUtf8(base));
    if (r != VideoFXResult::Ok) {
        error = VideoFX_GetLastError();
        if (error.empty()) error = VideoFX_ResultToString(r);
        return false;
    }
    JSONParseResult parsed;
    JSONValue projectJson = JSON::Parse(projectText, &parsed);
    if (!parsed.success) {
        error = "The engine's project text is not JSON: " + parsed.errorMessage;
        return false;
    }

    JSONValue root = JSONValue::MakeObject();
    root.Set("format", "ultravideo");
    root.Set("version", kFileVersion);
    root.Set("project", projectJson);
    JSONValue ids = JSONValue::MakeArray();
    for (const std::string& id : clipIds) ids.Append(id);
    root.Set("clipIds", ids);
    root.Set("nextClipId", nextClipId);
    JSONValue spans = JSONValue::MakeArray();
    for (const TitleSpan& t : titles) {
        JSONValue o = JSONValue::MakeObject();
        o.Set("id", t.id);
        o.Set("start", t.start);
        o.Set("duration", t.duration);
        o.Set("fadeIn", t.fadeIn);
        o.Set("fadeOut", t.fadeOut);
        o.Set("style", t.style);
        o.Set("text", t.look.text);
        o.Set("anchor", static_cast<int>(t.look.anchor));
        o.Set("fontSize", t.look.fontSize);
        o.Set("color", static_cast<int64_t>(t.look.textColor));
        o.Set("shadow", t.look.shadow);
        o.Set("box", t.look.box);
        o.Set("boxColor", static_cast<int64_t>(t.look.boxColor));
        o.Set("boxOpacity", t.look.boxOpacity);
        o.Set("margin", t.look.margin);
        o.Set("opacity", t.look.opacity);
        o.Set("fontPath", Relative(t.look.fontPath, base));
        spans.Append(o);
    }
    root.Set("titles", spans);
    root.Set("nextTitleId", nextTitleId);
    JSONValue pips = JSONValue::MakeArray();
    for (const OverlaySpan& v : overlays) {
        JSONValue o = JSONValue::MakeObject();
        o.Set("id", v.id);
        o.Set("path", Relative(v.path, base));
        o.Set("start", v.start);
        o.Set("duration", v.duration);
        o.Set("sourceIn", v.sourceIn);
        o.Set("fadeIn", v.fadeIn);
        o.Set("fadeOut", v.fadeOut);
        o.Set("anchor", static_cast<int>(v.look.anchor));
        o.Set("margin", v.look.margin);
        o.Set("x", v.look.x);
        o.Set("y", v.look.y);
        o.Set("size", v.look.imageHeight);
        o.Set("opacity", v.look.opacity);
        pips.Append(o);
    }
    root.Set("overlays", pips);
    root.Set("nextOverlayId", nextOverlayId);
    JSONValue bin = JSONValue::MakeArray();
    for (const MediaItem& m : media) {
        JSONValue o = JSONValue::MakeObject();
        o.Set("path", Relative(m.path, base));
        o.Set("name", m.name);
        bin.Append(o);
    }
    root.Set("media", bin);
    JSONValue marks = JSONValue::MakeArray();
    for (const TimelineMarker& m : markers) marks.Append(MarkerToJson(m));
    root.Set("markers", marks);
    JSONValue v = JSONValue::MakeObject();
    v.Set("pixelsPerSecond", view.pixelsPerSecond);
    v.Set("scrollSeconds", view.scrollSeconds);
    v.Set("playhead", view.playhead);
    v.Set("rangeIn", view.rangeIn);
    v.Set("rangeOut", view.rangeOut);
    v.Set("dark", view.dark);
    v.Set("binWidth", view.binWidth);
    v.Set("inspectorWidth", view.inspectorWidth);
    v.Set("dockPercent", view.dockPercent);
    root.Set("view", v);

    JSONSerializeOptions options;
    options.pretty = true;
    options.indentWidth = 2;
    const std::string text = JSON::Serialize(root, options);
    if (text.empty()) {
        error = "The project could not be written as JSON";
        return false;
    }
    // Atomic: the whole file or nothing
    const fs::path temp = file.parent_path() / PathFromUtf8(PathToUtf8(file.filename()) + ".tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "Cannot write " + toPath;
            return false;
        }
        out << text << '\n';
        if (!out) {
            error = "Writing " + toPath + " failed";
            std::error_code ec;
            fs::remove(temp, ec);
            return false;
        }
    }
    std::error_code ec;
    fs::rename(temp, file, ec);
    if (ec) {
        error = "Cannot replace " + toPath + ": " + ec.message();
        fs::remove(temp, ec);
        return false;
    }
    path = toPath;
    return true;
}

bool Document::Load(const std::string& fromPath, Document& doc, std::string& error, std::vector<std::string>* missing) {
    error.clear();
    if (missing) missing->clear();
    JSONParseResult parsed;
    const JSONValue root = JSON::ParseFile(fromPath, &parsed);
    if (!parsed.success) {
        error = parsed.errorMessage.empty() ? "Cannot read " + fromPath : parsed.errorMessage;
        return false;
    }
    if (!root.IsObject() || root.Get("format").GetString() != "ultravideo") {
        error = fromPath + " is not an UltraVideo project";
        return false;
    }
    if (root.Get("version").GetInteger() > kFileVersion) {
        error = fromPath + " was written by a newer UltraVideo";
        return false;
    }
    const fs::path base = PathFromUtf8(fromPath).parent_path();
    Document loaded;
    const std::string projectText = JSON::Serialize(root.Get("project"));
    const VideoFXResult r = VideoFX_ProjectFromJson(projectText, loaded.project, PathToUtf8(base));
    if (r != VideoFXResult::Ok) {
        error = VideoFX_GetLastError();
        if (error.empty()) error = VideoFX_ResultToString(r);
        return false;
    }
    loaded.nextClipId = static_cast<int>(root.Get("nextClipId").GetInteger(1));
    const JSONValue& ids = root.Get("clipIds");
    for (size_t i = 0; i < ids.GetSize(); ++i) loaded.clipIds.push_back(ids.At(i).GetString());
    loaded.clipIds.resize(loaded.project.segments.size());
    for (std::string& id : loaded.clipIds) if (id.empty()) id = loaded.NewClipId();

    // The spans are the truth for titles: the overlays written for the
    // engine are derived from them and go
    if (root.Contains("titles")) {
        for (VideoFXSegment& seg : loaded.project.segments) {
            seg.overlays.erase(std::remove_if(seg.overlays.begin(), seg.overlays.end(),
                                              [](const VideoFXOverlay& o) { return o.kind == VideoFXOverlayKind::Text; }),
                               seg.overlays.end());
        }
        const JSONValue& spans = root.Get("titles");
        for (size_t i = 0; i < spans.GetSize(); ++i) {
            const JSONValue& o = spans.At(i);
            TitleSpan t;
            t.id = o.Get("id").GetString();
            t.start = o.Get("start").GetNumber();
            t.duration = std::max(0.04, o.Get("duration").GetNumber(4.0));
            t.fadeIn = o.Get("fadeIn").GetNumber(0.0);
            t.fadeOut = o.Get("fadeOut").GetNumber(0.0);
            t.style = o.Get("style").GetString();
            t.look = VideoFXOverlay::Text(o.Get("text").GetString(),
                                          static_cast<VideoFXAnchor>(std::clamp<int64_t>(o.Get("anchor").GetInteger(7), 0, 9)),
                                          o.Get("fontSize").GetNumber(0.06));
            t.look.textColor = static_cast<uint32_t>(o.Get("color").GetInteger(0xFFFFFF));
            t.look.shadow = o.Get("shadow").GetBoolean(true);
            t.look.box = o.Get("box").GetBoolean(false);
            t.look.boxColor = static_cast<uint32_t>(o.Get("boxColor").GetInteger(0));
            t.look.boxOpacity = o.Get("boxOpacity").GetNumber(0.5);
            t.look.margin = o.Get("margin").GetNumber(0.05);
            t.look.opacity = o.Get("opacity").GetNumber(1.0);
            t.look.fontPath = Absolute(o.Get("fontPath").GetString(), base);
            if (t.id.empty()) t.id = loaded.NewTitleId();
            loaded.titles.push_back(t);
        }
        loaded.nextTitleId = static_cast<int>(root.Get("nextTitleId").GetInteger(static_cast<int64_t>(loaded.titles.size()) + 1));
    }

    // The same for the overlay lane: the spans are written back as the
    // engine's picture overlays, which go again here
    if (root.Contains("overlays")) {
        for (VideoFXSegment& seg : loaded.project.segments) {
            seg.overlays.erase(std::remove_if(seg.overlays.begin(), seg.overlays.end(), [](const VideoFXOverlay& o) {
                                   return o.kind == VideoFXOverlayKind::Video || o.kind == VideoFXOverlayKind::Image; }),
                               seg.overlays.end());
        }
        const JSONValue& pips = root.Get("overlays");
        for (size_t i = 0; i < pips.GetSize(); ++i) {
            const JSONValue& o = pips.At(i);
            OverlaySpan v;
            v.id = o.Get("id").GetString();
            v.path = Absolute(o.Get("path").GetString(), base);
            v.start = o.Get("start").GetNumber();
            v.duration = std::max(0.04, o.Get("duration").GetNumber(4.0));
            v.sourceIn = std::max(0.0, o.Get("sourceIn").GetNumber(0.0));
            v.fadeIn = o.Get("fadeIn").GetNumber(0.0);
            v.fadeOut = o.Get("fadeOut").GetNumber(0.0);
            v.look = VideoFXOverlay::Video(v.path, static_cast<VideoFXAnchor>(std::clamp<int64_t>(o.Get("anchor").GetInteger(8), 0, 9)),
                                           o.Get("size").GetNumber(0.3));
            v.look.margin = o.Get("margin").GetNumber(0.04);
            v.look.x = o.Get("x").GetNumber(0.0);
            v.look.y = o.Get("y").GetNumber(0.0);
            v.look.opacity = o.Get("opacity").GetNumber(1.0);
            if (v.id.empty() || v.path.empty()) continue;
            loaded.overlays.push_back(v);
        }
        loaded.nextOverlayId = static_cast<int>(root.Get("nextOverlayId").GetInteger(static_cast<int64_t>(loaded.overlays.size()) + 1));
    }

    const JSONValue& bin = root.Get("media");
    for (size_t i = 0; i < bin.GetSize(); ++i) {
        const std::string p = Absolute(bin.At(i).Get("path").GetString(), base);
        if (p.empty()) continue;
        std::error_code ec;
        if (!fs::exists(PathFromUtf8(p), ec)) {
            MediaItem item;
            item.path = p;
            item.name = FileName(p);
            item.kind = KindOfPath(p);
            item.error = "The file is missing";
            loaded.media.push_back(item);
            if (missing) missing->push_back(p);
            continue;
        }
        loaded.Import(p);
    }
    // Media the project uses that the bin forgot
    auto known = [&](const std::string& p) {
        if (p.empty() || loaded.FindMedia(p)) return;
        std::error_code ec;
        if (fs::exists(PathFromUtf8(p), ec)) loaded.Import(p);
        else if (missing) missing->push_back(p);
    };
    for (const VideoFXSegment& s : loaded.project.segments)
        if (s.kind == VideoFXSourceKind::File || s.kind == VideoFXSourceKind::Image) known(s.path);
    for (const OverlaySpan& v : loaded.overlays) known(v.path);
    const JSONValue& marks = root.Get("markers");
    for (size_t i = 0; i < marks.GetSize(); ++i) {
        const JSONValue& m = marks.At(i);
        TimelineMarker marker;
        marker.id = m.Get("id").GetString();
        marker.seconds = m.Get("seconds").GetNumber();
        marker.color = static_cast<uint32_t>(m.Get("color").GetInteger(0xF2A53A));
        marker.label = m.Get("label").GetString();
        loaded.markers.push_back(marker);
    }
    const JSONValue& v = root.Get("view");
    if (v.IsObject()) {
        loaded.view.pixelsPerSecond = v.Get("pixelsPerSecond").GetNumber(80.0);
        loaded.view.scrollSeconds = v.Get("scrollSeconds").GetNumber(0.0);
        loaded.view.playhead = v.Get("playhead").GetNumber(0.0);
        loaded.view.rangeIn = v.Get("rangeIn").GetNumber(0.0);
        loaded.view.rangeOut = v.Get("rangeOut").GetNumber(0.0);
        loaded.view.dark = v.Get("dark").GetBoolean(true);
        loaded.view.binWidth = static_cast<int>(v.Get("binWidth").GetInteger(280));
        loaded.view.inspectorWidth = static_cast<int>(v.Get("inspectorWidth").GetInteger(300));
        loaded.view.dockPercent = static_cast<int>(v.Get("dockPercent").GetInteger(35));
    }
    loaded.path = fromPath;
    doc = std::move(loaded);
    return true;
}

} // namespace UltraVideo
