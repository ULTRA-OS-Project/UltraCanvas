// core/UltraCanvasProfilePicture.cpp
// The round profile picture, its editor row, the cropper and the crop
// dialog - see the header.
// Version: 1.1.0 - CropProfilePictureWithin: lower quality, then fewer pixels, until the file fits
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "UltraCanvasProfilePicture.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasElementPlugins.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasTheme.h"
#include "UltraCanvasWindow.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <system_error>

namespace UltraCanvas {

namespace fs = std::filesystem;

namespace {

constexpr double kPi = 3.14159265358979323846;
// The longest side a picture is read at for cropping: a 24-megapixel photo
// still makes a sharp 1024-pixel square, without holding all its pixels.
constexpr int kCropReadSide = 2048;

const std::vector<Color>& LightPalette() {
    static const std::vector<Color> palette = {
        Color(196, 79, 73, 255),  Color(172, 98, 33, 255),  Color(139, 113, 14, 255), Color(43, 132, 79, 255),
        Color(33, 127, 144, 255), Color(61, 113, 211, 255), Color(132, 94, 210, 255), Color(186, 76, 139, 255)};
    return palette;
}

const std::vector<Color>& DarkPalette() {
    static const std::vector<Color> palette = {
        Color(236, 112, 104, 255), Color(236, 148, 70, 255), Color(214, 180, 48, 255), Color(76, 184, 118, 255),
        Color(66, 176, 196, 255),  Color(96, 146, 236, 255), Color(156, 122, 228, 255), Color(220, 108, 172, 255)};
    return palette;
}

double Luminance(const Color& c) {
    auto channel = [](uint8_t v) {
        const double s = v / 255.0;
        return s <= 0.03928 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) + 0.0722 * channel(c.b);
}

double Contrast(const Color& a, const Color& b) {
    const double la = Luminance(a), lb = Luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// The length of the UTF-8 sequence that starts with `lead`.
size_t Utf8Length(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead >> 5) == 0x6) return 2;
    if ((lead >> 4) == 0xE) return 3;
    if ((lead >> 3) == 0x1E) return 4;
    return 1;
}

std::string FirstLetter(const std::string& word) {
    if (word.empty()) return std::string();
    const unsigned char lead = static_cast<unsigned char>(word[0]);
    if (lead < 0x80) {
        const char c = word[0];
        return std::string(1, (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c);
    }
    return word.substr(0, std::min(word.size(), Utf8Length(lead)));
}

// A letter or a digit starts a word worth an initial ("(Erin)" -> "E").
bool StartsWord(unsigned char c) {
    return c >= 0x80 || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

std::string LowerExtension(const std::string& path) {
    std::string ext = PathToUtf8(PathFromUtf8(path).extension());
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return ext;
}

// The picture decoded at no more than `maxSide` a side: its pixels
// (premultiplied ARGB32, rows of width * 4 bytes) and their size.
struct SourcePixels {
    UCPixmapPtr pixmap;
    int width = 0;
    int height = 0;
    const uint32_t* data = nullptr;
};

bool ReadSource(const std::string& path, int maxSide, SourcePixels& out, std::string* error) {
    UCImagePtr image = UCImage::Get(path);
    if (!image || image->GetWidth() <= 0 || image->GetHeight() <= 0) {
        if (error) *error = image && !image->errorMessage.empty() ? image->errorMessage : "The picture could not be read";
        return false;
    }
    const int w = image->GetWidth(), h = image->GetHeight();
    const double scale = std::min(1.0, static_cast<double>(maxSide) / std::max(w, h));
    const int rw = std::max(1, static_cast<int>(std::lround(w * scale)));
    const int rh = std::max(1, static_cast<int>(std::lround(h * scale)));
    out.pixmap = image->GetPixmap(rw, rh, ImageFitMode::Fill, 1.0f);
    if (!out.pixmap || !out.pixmap->IsValid()) {
        if (error) *error = "The picture could not be decoded";
        return false;
    }
    out.pixmap->Flush();
    out.width = out.pixmap->GetRawWidth();
    out.height = out.pixmap->GetRawHeight();
    out.data = out.pixmap->GetPixelData();
    return out.data != nullptr && out.width > 0 && out.height > 0;
}

// The camera on the badge of a picture there is, the "+" on that of none.
void PaintBadgeIcon(IRenderContext* ctx, const Point2Dd& c, double side, bool camera, const Color& ink) {
    ctx->SetStrokePaint(ink);
    ctx->SetStrokeWidth(std::max(1.2, side * 0.11));
    ctx->ClearPath();
    if (camera) {
        const double w = side * 0.84, h = side * 0.56;
        const double x = c.x - w / 2.0, y = c.y - h / 2.0 + side * 0.06;
        ctx->MoveTo(x, y);
        ctx->LineTo(x + w * 0.28, y);
        ctx->LineTo(x + w * 0.36, y - side * 0.14);
        ctx->LineTo(x + w * 0.64, y - side * 0.14);
        ctx->LineTo(x + w * 0.72, y);
        ctx->LineTo(x + w, y);
        ctx->LineTo(x + w, y + h);
        ctx->LineTo(x, y + h);
        ctx->ClosePath();
        ctx->StrokePathPreserve();
        ctx->ClearPath();
        ctx->Circle(c.x, y + h / 2.0, side * 0.16);
        ctx->StrokePathPreserve();
    } else {
        const double r = side * 0.36;
        ctx->MoveTo(c.x - r, c.y);
        ctx->LineTo(c.x + r, c.y);
        ctx->MoveTo(c.x, c.y - r);
        ctx->LineTo(c.x, c.y + r);
        ctx->StrokePathPreserve();
    }
    ctx->ClearPath();
}

} // namespace

// ===== THE FRAME =====
Rect2Dd ProfilePictureFrame::SquareIn(int width, int height) const {
    if (width <= 0 || height <= 0) return Rect2Dd(0, 0, 0, 0);
    const ProfilePictureFrame f = Clamped(width, height);
    const double side = std::min(width, height) / f.zoom;
    const double x = f.centerX * width - side / 2.0;
    const double y = f.centerY * height - side / 2.0;
    return Rect2Dd(std::clamp(x, 0.0, width - side), std::clamp(y, 0.0, height - side), side, side);
}

ProfilePictureFrame ProfilePictureFrame::Clamped(int width, int height) const {
    ProfilePictureFrame f = *this;
    if (!(f.zoom >= 1.0)) f.zoom = 1.0;   // NaN too
    if (width <= 0 || height <= 0) return f;
    const double side = std::min(width, height) / f.zoom;
    const double hx = side / 2.0 / width, hy = side / 2.0 / height;
    f.centerX = std::isfinite(f.centerX) ? std::clamp(f.centerX, hx, 1.0 - hx) : 0.5;
    f.centerY = std::isfinite(f.centerY) ? std::clamp(f.centerY, hy, 1.0 - hy) : 0.5;
    return f;
}

// ===== STYLE =====
ProfilePictureStyle ProfilePictureStyle::Light() {
    ProfilePictureStyle s;
    s.initialsPalette = LightPalette();
    return s;
}

ProfilePictureStyle ProfilePictureStyle::Dark() {
    ProfilePictureStyle s;
    s.initialsPalette = DarkPalette();
    s.badgeColor = Color(76, 187, 110, 255);
    s.badgeInk = Color(12, 28, 18, 255);
    s.badgeBorder = Color(32, 35, 42, 255);
    s.hoverShade = Color(0, 0, 0, 80);
    s.focusRing = Color(76, 187, 110, 255);
    return s;
}

ProfilePictureStyle ProfilePictureStyle::FromTheme(const UltraCanvasTheme& theme, const ProfilePictureStyle& base) {
    ProfilePictureStyle s = base;
    const ThemeRoles& r = theme.roles;
    const std::vector<Color>& fallback = theme.dark ? DarkPalette() : LightPalette();
    s.initialsPalette.clear();
    for (size_t i = 0; i < fallback.size(); ++i) {
        s.initialsPalette.push_back(theme.Role("avatar." + std::to_string(i), fallback[i]));
    }
    s.initialsInkDark = theme.dark ? r.ground : r.ink;
    s.badgeColor = r.accent;
    s.badgeInk = r.onAccent;
    s.badgeBorder = r.panel;
    s.focusRing = r.focus;
    s.hoverShade = theme.dark ? Color(0, 0, 0, 80) : Color(0, 0, 0, 60);
    return s;
}

ProfilePictureStyle ProfilePictureStyle::FromTheme(const UltraCanvasTheme& theme) {
    return FromTheme(theme, theme.dark ? Dark() : Light());
}

ProfilePictureStyle ProfilePictureStyle::FromTheme() {
    return FromTheme(UltraCanvasTheme::Current());
}

Color ProfilePictureStyle::ColorForKey(const std::string& key) const {
    const std::vector<Color>& palette = initialsPalette.empty() ? LightPalette() : initialsPalette;
    // FNV-1a: the same key, the same colour, on every platform and run.
    uint32_t h = 2166136261u;
    for (const char c : key) {
        h ^= static_cast<unsigned char>(c);
        h *= 16777619u;
    }
    return palette[h % palette.size()];
}

Color ProfilePictureStyle::InkOn(const Color& fill) const {
    return Contrast(initialsInkLight, fill) >= Contrast(initialsInkDark, fill) ? initialsInkLight : initialsInkDark;
}

// ===== WRITING THE PICTURE =====
std::string ProfilePictureInitials(const std::string& name) {
    std::vector<std::string> words;
    size_t i = 0;
    while (i < name.size()) {
        while (i < name.size() && !StartsWord(static_cast<unsigned char>(name[i]))) ++i;
        const size_t start = i;
        while (i < name.size() && name[i] != ' ' && name[i] != '\t' && name[i] != '-' && name[i] != '_') ++i;
        if (i > start) words.push_back(name.substr(start, i - start));
    }
    if (words.empty()) return std::string();
    std::string initials = FirstLetter(words.front());
    if (words.size() > 1) initials += FirstLetter(words.back());
    return initials;
}

bool ProfilePictureHasTransparency(const std::string& path) {
    UCImagePtr image = UCImage::Get(path);
    return image && image->HasTransparency();
}

namespace {
bool CropAt(const std::string& sourcePath, const ProfilePictureFrame& frame, int outputSize,
            const std::string& outputPath, int quality, std::string* error);
} // namespace

bool CropProfilePicture(const std::string& sourcePath, const ProfilePictureFrame& frame, int outputSize,
                        const std::string& outputPath, std::string* error) {
    return CropAt(sourcePath, frame, outputSize, outputPath, 88, error);
}

bool CropProfilePictureWithin(const std::string& sourcePath, const ProfilePictureFrame& frame, int outputSize,
                              const std::string& outputPath, size_t maxBytes, std::string* writtenPath,
                              std::string* error) {
    if (!CropAt(sourcePath, frame, outputSize, outputPath, 88, error)) return false;
    auto sizeOf = [](const std::string& path) {
        std::error_code ec;
        const auto n = fs::file_size(PathFromUtf8(path), ec);
        return ec ? static_cast<uintmax_t>(-1) : n;
    };
    if (maxBytes == 0 || sizeOf(outputPath) <= maxBytes) {
        if (writtenPath) *writtenPath = outputPath;
        return true;
    }
    // Smaller, as JPEG: lower quality first, then fewer pixels.
    fs::path jpeg = PathFromUtf8(outputPath);
    const std::string ext = LowerExtension(outputPath);
    if (ext != ".jpg" && ext != ".jpeg") jpeg.replace_extension(".jpg");
    const std::string target = PathToUtf8(jpeg);
    for (const int side : {outputSize, std::max(64, outputSize * 3 / 4), std::max(64, outputSize / 2)}) {
        for (const int quality : {88, 75, 60}) {
            if (!CropAt(sourcePath, frame, side, target, quality, error)) return false;
            if (sizeOf(target) <= maxBytes) {
                std::error_code ec;
                if (target != outputPath) fs::remove(PathFromUtf8(outputPath), ec);
                if (writtenPath) *writtenPath = target;
                return true;
            }
        }
    }
    std::error_code ec;
    fs::remove(PathFromUtf8(target), ec);
    if (target != outputPath) fs::remove(PathFromUtf8(outputPath), ec);
    if (error) *error = "The picture does not get small enough";
    return false;
}

namespace {
bool CropAt(const std::string& sourcePath, const ProfilePictureFrame& frame, int outputSize,
            const std::string& outputPath, int quality, std::string* error) {
    if (outputSize < 1 || outputPath.empty()) {
        if (error) *error = "No size or no file to write";
        return false;
    }
    SourcePixels src;
    if (!ReadSource(sourcePath, kCropReadSide, src, error)) return false;

    const Rect2Dd square = frame.SquareIn(src.width, src.height);
    const std::string ext = LowerExtension(outputPath);
    const bool opaque = ext == ".jpg" || ext == ".jpeg";

    // Each output pixel is the average of the source pixels it covers (a box
    // filter): sharp when shrinking, smooth when a small picture is enlarged.
    std::vector<uint8_t> out(static_cast<size_t>(outputSize) * outputSize * 4);
    const double step = square.width / outputSize;
    for (int oy = 0; oy < outputSize; ++oy) {
        const double sy0 = square.y + oy * step, sy1 = sy0 + step;
        for (int ox = 0; ox < outputSize; ++ox) {
            const double sx0 = square.x + ox * step, sx1 = sx0 + step;
            double acc[4] = {0, 0, 0, 0};
            double weight = 0;
            const int y0 = std::clamp(static_cast<int>(std::floor(sy0)), 0, src.height - 1);
            const int y1 = std::clamp(static_cast<int>(std::ceil(sy1)) - 1, y0, src.height - 1);
            const int x0 = std::clamp(static_cast<int>(std::floor(sx0)), 0, src.width - 1);
            const int x1 = std::clamp(static_cast<int>(std::ceil(sx1)) - 1, x0, src.width - 1);
            for (int y = y0; y <= y1; ++y) {
                const double wy = std::max(0.0, std::min<double>(y + 1, sy1) - std::max<double>(y, sy0));
                const double fy = wy > 0 ? wy : 1e-6;
                const uint32_t* row = src.data + static_cast<size_t>(y) * src.width;
                for (int x = x0; x <= x1; ++x) {
                    const double wx = std::max(0.0, std::min<double>(x + 1, sx1) - std::max<double>(x, sx0));
                    const double w = fy * (wx > 0 ? wx : 1e-6);
                    const uint32_t p = row[x];
                    acc[0] += w * (p & 0xFF);           // blue
                    acc[1] += w * ((p >> 8) & 0xFF);    // green
                    acc[2] += w * ((p >> 16) & 0xFF);   // red
                    acc[3] += w * ((p >> 24) & 0xFF);   // alpha
                    weight += w;
                }
            }
            uint8_t* o = &out[(static_cast<size_t>(oy) * outputSize + ox) * 4];
            const double a = weight > 0 ? acc[3] / weight : 0;
            for (int k = 0; k < 3; ++k) {
                const double premultiplied = weight > 0 ? acc[k] / weight : 0;
                double v;
                if (opaque) v = premultiplied + (255.0 - a);   // over white
                else v = a > 0 ? premultiplied * 255.0 / a : 0;   // straight alpha
                o[k] = static_cast<uint8_t>(std::clamp(std::lround(v), 0L, 255L));
            }
            o[3] = opaque ? 255 : static_cast<uint8_t>(std::clamp(std::lround(a), 0L, 255L));
        }
    }

    UCImagePtr result = UCImage::FromBgra(out.data(), outputSize, outputSize, outputSize * 4, opaque);
    if (!result) {
        if (error) *error = "The picture could not be made";
        return false;
    }
    std::error_code ec;
    const fs::path parent = PathFromUtf8(outputPath).parent_path();
    if (!parent.empty()) fs::create_directories(parent, ec);

    UCImageSave::ImageExportOptions options;
    options.preserveMetadata = false;
    options.preserveTransparency = !opaque;
    if (opaque) {
        options.format = UCImageSaveFormat::JPEG;
        options.jpeg.quality = quality;
    } else if (ext == ".webp") {
        options.format = UCImageSaveFormat::WEBP;
        options.webp.quality = quality;
    } else {
        options.format = UCImageSaveFormat::PNG;
    }
    const std::string failure = result->Save(outputPath, options);
    if (!failure.empty()) {
        if (error) *error = failure;
        return false;
    }
    return true;
}
} // namespace

// ===== THE PICTURE =====
UltraCanvasProfilePicture::UltraCanvasProfilePicture(const std::string& identifier, float x, float y, float w,
                                                     float h)
    : UltraCanvasUIElement(identifier, x, y, w, h), style_(ProfilePictureStyle::Light()) {
    outputDirectory_ = PathToUtf8(fs::temp_directory_path() / "ultracanvas-profile-pictures");
    ApplyThemeIfFollowing();
}

UltraCanvasProfilePicture::~UltraCanvasProfilePicture() = default;

void UltraCanvasProfilePicture::SetPicturePath(const std::string& path) {
    picturePath_ = path;
    frameSource_.clear();
    RequestRedraw();
}

void UltraCanvasProfilePicture::SetPictureFrame(const std::string& sourcePath, const ProfilePictureFrame& frame) {
    frameSource_ = sourcePath;
    frame_ = frame;
    RequestRedraw();
}

void UltraCanvasProfilePicture::SetName(const std::string& name) {
    name_ = name;
    SetAccessibleName(name.empty() ? std::string() : TrF("Picture of {0}", name));
    RequestRedraw();
}

void UltraCanvasProfilePicture::SetColorKey(const std::string& key) {
    colorKey_ = key;
    RequestRedraw();
}

void UltraCanvasProfilePicture::SetEditable(bool editable) {
    editable_ = editable;
    SetMouseCursor(editable ? UCMouseCursor::Hand : UCMouseCursor::Default);
    SetTooltip(editable ? Tr("Choose a picture") : std::string());
    RequestRedraw();
}

void UltraCanvasProfilePicture::SetOutputSize(int pixels) {
    outputSize_ = std::clamp(pixels, 64, 1024);
}

void UltraCanvasProfilePicture::SetStyle(const ProfilePictureStyle& style) {
    style_ = style;
    StyleSetByHand();
    RequestRedraw();
}

void UltraCanvasProfilePicture::ApplyTheme(const UltraCanvasTheme& theme) {
    SetStyle(ProfilePictureStyle::FromTheme(theme, style_));
}

Rect2Df UltraCanvasProfilePicture::PictureRect() const {
    const Rect2Df b = GetLocalBounds();
    const float side = std::min(b.width, b.height);
    return Rect2Df(b.x + (b.width - side) / 2.0f, b.y + (b.height - side) / 2.0f, side, side);
}

AccessibleRole UltraCanvasProfilePicture::GetAccessibleRole() const {
    return editable_ ? AccessibleRole::Button : AccessibleRole::Image;
}

void UltraCanvasProfilePicture::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
    if (!ctx) return;
    const Rect2Df pr = PictureRect();
    if (pr.width <= 0) return;
    const Rect2Dd r(pr.x, pr.y, pr.width, pr.height);
    const double radius = r.width / 2.0;
    const Point2Dd centre(r.x + radius, r.y + radius);

    bool drawn = false;
    ctx->PushState();
    ctx->ClipRoundedRectangle(r, radius, radius, radius, radius);
    if (!frameSource_.empty()) {
        UCImagePtr image = UCImage::Get(frameSource_);
        if (image && image->GetWidth() > 0 && image->GetHeight() > 0) {
            ctx->DrawPartOfImage(*image, frame_.SquareIn(image->GetWidth(), image->GetHeight()), r);
            drawn = true;
        }
    } else if (!picturePath_.empty()) {
        UCImagePtr image = UCImage::Get(picturePath_);
        if (image && image->GetWidth() > 0 && image->GetHeight() > 0) {
            ctx->DrawImage(*image, r, ImageFitMode::Cover);
            drawn = true;
        }
    }
    if (!drawn) {
        // The initials on the name's colour.
        const Color fill = style_.ColorForKey(colorKey_.empty() ? name_ : colorKey_);
        ctx->SetFillPaint(fill);
        ctx->FillRectangle(r);
        const std::string initials = ProfilePictureInitials(name_);
        if (!initials.empty()) {
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->SetFontSize(std::max(6.0, r.width * style_.initialsScale * 0.75));
            ctx->SetTextPaint(style_.InkOn(fill));
            ctx->DrawText(initials, ctx->CalculateCenteredTextPosition(initials, r));
        }
    }
    if (editable_ && hovered_ && !IsDisabled()) {
        ctx->SetFillPaint(style_.hoverShade);
        ctx->FillRectangle(r);
    }
    ctx->PopState();

    if (style_.ringColor.a > 0) {
        ctx->SetStrokePaint(style_.ringColor);
        ctx->SetStrokeWidth(style_.ringWidth);
        ctx->DrawCircle(centre, radius - style_.ringWidth / 2.0);
    }
    if (editable_ && IsFocused()) {
        ctx->SetStrokePaint(style_.focusRing);
        ctx->SetStrokeWidth(2.0);
        ctx->DrawCircle(centre, radius - 1.0);
    }
    if (editable_) {
        const double badge = std::max(14.0, r.width * style_.badgeScale);
        const Point2Dd at(r.x + r.width * 0.85, r.y + r.height * 0.85);
        ctx->DrawFilledCircle(at, static_cast<float>(badge / 2.0), style_.badgeBorder);
        ctx->DrawFilledCircle(at, static_cast<float>(badge / 2.0 - std::max(2.0, badge * 0.1)), style_.badgeColor);
        PaintBadgeIcon(ctx, at, badge * 0.5, drawn, style_.badgeInk);
    }
}

bool UltraCanvasProfilePicture::OnEvent(const UCEvent& event) {
    if (!IsVisible()) return false;
    if (UltraCanvasUIElement::OnEvent(event)) return true;
    if (!editable_ || IsDisabled()) return false;
    switch (event.type) {
        case UCEventType::MouseEnter:
        case UCEventType::MouseMove: {
            const Rect2Df pr = PictureRect();
            const double dx = event.pointer.x - (pr.x + pr.width / 2.0);
            const double dy = event.pointer.y - (pr.y + pr.height / 2.0);
            const bool over = dx * dx + dy * dy <= (pr.width / 2.0) * (pr.width / 2.0);
            if (over != hovered_) {
                hovered_ = over;
                RequestRedraw();
            }
            return event.type == UCEventType::MouseMove && over;
        }
        case UCEventType::MouseLeave:
            if (hovered_) {
                hovered_ = false;
                RequestRedraw();
            }
            return false;
        case UCEventType::MouseDown:
            if (event.button != UCMouseButton::Left || !hovered_) return false;
            SetFocus(true);
            return true;
        case UCEventType::MouseUp:
            if (event.button != UCMouseButton::Left || !hovered_) return false;
            ChoosePicture();
            return true;
        case UCEventType::KeyDown:
            if (!IsFocused()) return false;
            if (event.virtualKey == UCKeys::Return || event.virtualKey == UCKeys::Space) {
                ChoosePicture();
                return true;
            }
            if ((event.virtualKey == UCKeys::Delete || event.virtualKey == UCKeys::Backspace) && HasPicture()) {
                RemovePicture();
                return true;
            }
            return false;
        default:
            break;
    }
    return false;
}

void UltraCanvasProfilePicture::ChoosePicture() {
    FileDialogOptions options;
    options.SetTitle(Tr("Choose a picture"));
    UltraCanvasFileLoader::AddLoadTypes(options, Tr("Pictures"), {MediaFormatCategory::Bitmap});
    options.SetParentWindow(GetWindow());
    std::weak_ptr<UltraCanvasUIElement> alive = weak_from_this();
    UltraCanvasFileLoader::OpenFileDialog(options, [this, alive](DialogResult result, const std::string& path) {
        if (result != DialogResult::OK || path.empty() || alive.expired()) return;
        PlacePicture(path);
    });
}

void UltraCanvasProfilePicture::PlacePicture(const std::string& sourcePath) {
    std::weak_ptr<UltraCanvasUIElement> alive = weak_from_this();
    ShowProfilePictureCropDialog(GetWindow(), sourcePath, style_,
                                 [this, alive, sourcePath](bool placed, const ProfilePictureFrame& frame) {
        if (!placed || alive.expired()) return;
        Commit(sourcePath, frame);
    });
}

std::string UltraCanvasProfilePicture::NextOutputPath(bool transparent) const {
    static std::atomic<unsigned> counter{0};
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    const std::string name = "picture-" + std::to_string(ms) + "-" + std::to_string(counter++) +
                             (transparent ? ".png" : ".jpg");
    return PathToUtf8(PathFromUtf8(outputDirectory_) / PathFromUtf8(name));
}

void UltraCanvasProfilePicture::Commit(const std::string& sourcePath, const ProfilePictureFrame& frame) {
    const std::string wanted = NextOutputPath(ProfilePictureHasTransparency(sourcePath));
    std::string target, error;
    if (!CropProfilePictureWithin(sourcePath, frame, outputSize_, wanted, maxOutputBytes_, &target, &error)) {
        if (onPictureError) onPictureError(error);
        return;
    }
    SetPicturePath(target);
    if (onPictureChanged) onPictureChanged(target);
}

void UltraCanvasProfilePicture::RemovePicture() {
    if (picturePath_.empty() && frameSource_.empty()) return;
    SetPicturePath(std::string());
    if (onPictureChanged) onPictureChanged(std::string());
}

// ===== THE CROPPER =====
UltraCanvasProfilePictureCropper::UltraCanvasProfilePictureCropper(const std::string& identifier, float x, float y,
                                                                   float w, float h)
    : UltraCanvasUIElement(identifier, x, y, w, h), style_(ProfilePictureStyle::Light()) {
    SetMouseCursor(UCMouseCursor::Hand);
    SetAccessibleName(Tr("Place your picture"));
    ApplyThemeIfFollowing();
}

bool UltraCanvasProfilePictureCropper::SetSourcePath(const std::string& path) {
    UCImagePtr image = UCImage::Get(path);
    if (!image || image->GetWidth() <= 0 || image->GetHeight() <= 0) return false;
    sourcePath_ = path;
    sourceWidth_ = image->GetWidth();
    sourceHeight_ = image->GetHeight();
    frame_ = ProfilePictureFrame();
    RequestRedraw();
    return true;
}

void UltraCanvasProfilePictureCropper::SetFrame(const ProfilePictureFrame& frame, bool notify) {
    ProfilePictureFrame f = frame;
    f.zoom = std::min(f.zoom, maxZoom_);
    frame_ = f.Clamped(sourceWidth_, sourceHeight_);
    Changed(notify);
}

void UltraCanvasProfilePictureCropper::SetZoom(double zoom, bool notify) {
    ProfilePictureFrame f = frame_;
    f.zoom = std::clamp(zoom, 1.0, maxZoom_);
    SetFrame(f, notify);
}

void UltraCanvasProfilePictureCropper::Changed(bool notify) {
    RequestRedraw();
    if (notify && onFrameChanged) onFrameChanged(frame_);
}

void UltraCanvasProfilePictureCropper::MoveBy(double dx, double dy) {
    if (sourceWidth_ <= 0 || sourceHeight_ <= 0) return;
    const Rect2Dd square = frame_.SquareIn(sourceWidth_, sourceHeight_);
    const Rect2Df circle = CircleRect();
    if (square.width <= 0 || circle.width <= 0) return;
    // The picture follows the pointer: the frame goes the other way.
    const double k = circle.width / square.width;   // element pixels per picture pixel
    ProfilePictureFrame f = frame_;
    f.centerX -= dx / k / sourceWidth_;
    f.centerY -= dy / k / sourceHeight_;
    SetFrame(f, true);
}

Rect2Df UltraCanvasProfilePictureCropper::CircleRect() const {
    const Rect2Df b = GetLocalBounds();
    const float side = std::max(0.0f, std::min(b.width, b.height) - 2.0f * style_.cropInset);
    return Rect2Df(b.x + (b.width - side) / 2.0f, b.y + (b.height - side) / 2.0f, side, side);
}

void UltraCanvasProfilePictureCropper::ApplyTheme(const UltraCanvasTheme& theme) {
    style_ = ProfilePictureStyle::FromTheme(theme, style_);
    RequestRedraw();
}

AccessibleRole UltraCanvasProfilePictureCropper::GetAccessibleRole() const {
    return AccessibleRole::Image;
}

void UltraCanvasProfilePictureCropper::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
    if (!ctx) return;
    const Rect2Df b = GetLocalBounds();
    const Rect2Dd bounds(b.x, b.y, b.width, b.height);
    ctx->PushState();
    ctx->ClipRoundedRectangle(bounds, 10, 10, 10, 10);
    ctx->SetFillPaint(style_.cropBackground);
    ctx->FillRectangle(bounds);

    const Rect2Df circle = CircleRect();
    UCImagePtr image = sourcePath_.empty() ? nullptr : UCImage::Get(sourcePath_);
    if (image && sourceWidth_ > 0 && circle.width > 0) {
        // The whole picture, scaled so the frame's square fills the circle.
        const Rect2Dd square = frame_.SquareIn(sourceWidth_, sourceHeight_);
        const double k = circle.width / square.width;
        const Rect2Dd dest(circle.x - square.x * k, circle.y - square.y * k, sourceWidth_ * k, sourceHeight_ * k);
        ctx->DrawPartOfImage(*image, Rect2Dd(0, 0, sourceWidth_, sourceHeight_), dest);
    }
    // Everything but the circle dimmed.
    const double r = circle.width / 2.0;
    ctx->ClearPath();
    ctx->SetFillRule(FillRule::EvenOdd);
    ctx->Rect(bounds.x, bounds.y, bounds.width, bounds.height);
    ctx->Circle(circle.x + r, circle.y + r, r);
    ctx->SetFillPaint(style_.cropShade);
    ctx->FillPathPreserve();
    ctx->ClearPath();
    ctx->SetFillRule(FillRule::NonZero);
    ctx->SetStrokePaint(style_.cropRing);
    ctx->SetStrokeWidth(2.0);
    ctx->DrawCircle(Point2Dd(circle.x + r, circle.y + r), r);
    if (IsFocused()) {
        ctx->SetStrokePaint(style_.focusRing);
        ctx->SetStrokeWidth(2.0);
        ctx->DrawRoundedRectangle(Rect2Dd(bounds.x + 1, bounds.y + 1, bounds.width - 2, bounds.height - 2), 9);
    }
    ctx->PopState();
}

bool UltraCanvasProfilePictureCropper::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;
    if (UltraCanvasUIElement::OnEvent(event)) return true;
    switch (event.type) {
        case UCEventType::MouseDown:
            if (event.button != UCMouseButton::Left) return false;
            SetFocus(true);
            dragging_ = true;
            dragFrom_ = event.pointer;
            if (auto* app = UltraCanvasApplication::GetInstance()) app->CaptureMouse(this);
            return true;
        case UCEventType::MouseMove:
            if (!dragging_) return false;
            MoveBy(event.pointer.x - dragFrom_.x, event.pointer.y - dragFrom_.y);
            dragFrom_ = event.pointer;
            return true;
        case UCEventType::MouseUp:
            if (!dragging_) return false;
            dragging_ = false;
            if (auto* app = UltraCanvasApplication::GetInstance()) app->ReleaseMouse();
            return true;
        case UCEventType::MouseWheel:
            if (event.wheelDelta == 0) return false;
            SetZoom(frame_.zoom * (event.wheelDelta > 0 ? 1.1 : 1.0 / 1.1), true);
            return true;
        case UCEventType::KeyDown: {
            if (!IsFocused()) return false;
            const double step = 4.0;
            switch (event.virtualKey) {
                // The picture moves the arrow's way, as a drag would move it.
                case UCKeys::Left:  MoveBy(-step, 0); return true;
                case UCKeys::Right: MoveBy(step, 0); return true;
                case UCKeys::Up:    MoveBy(0, -step); return true;
                case UCKeys::Down:  MoveBy(0, step); return true;
                default: break;
            }
            if (event.text == "+" || event.text == "=") {
                SetZoom(frame_.zoom * 1.1, true);
                return true;
            }
            if (event.text == "-") {
                SetZoom(frame_.zoom / 1.1, true);
                return true;
            }
            return false;
        }
        default:
            break;
    }
    return false;
}

// ===== THE CROP DIALOG =====
void ShowProfilePictureCropDialog(UltraCanvasWindowBase* parent, const std::string& sourcePath,
                                  const ProfilePictureStyle& style,
                                  std::function<void(bool placed, const ProfilePictureFrame& frame)> done) {
    constexpr float kCropSide = 300.0f;
    auto cropper = std::make_shared<UltraCanvasProfilePictureCropper>("ProfilePictureCropper", kCropSide);
    cropper->SetStyle(style);
    if (!cropper->SetSourcePath(sourcePath)) {
        UltraCanvasDialogManager::ShowError(Tr("The picture could not be read."), Tr("Place your picture"));
        if (done) done(false, ProfilePictureFrame());
        return;
    }

    DialogConfig config;
    config.title = Tr("Place your picture");
    config.dialogType = DialogType::Information;
    config.buttons = DialogButtons::NoButtons;
    config.showIcon = false;
    config.width = 380;
    config.height = 560;
    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    if (!dialog) {
        if (done) done(false, ProfilePictureFrame());
        return;
    }
    if (auto message = dialog->FindChildById("MessageArea")) message->SetVisible(false);

    auto column = std::make_shared<UltraCanvasContainer>("ProfilePictureCropColumn");
    column->layout.SetFlexColumn().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    column->size.width = CSSLayout::Dimension::Px(kCropSide + 20);

    auto hint = std::make_shared<UltraCanvasLabel>("ProfilePictureCropHint",
                                                   Tr("Drag the picture to move it inside the circle; the slider zooms."));
    hint->SetThemeVariant(LabelVariant::Muted);
    hint->SetWrap(TextWrap::WrapWord);
    hint->size.width = CSSLayout::Dimension::Px(kCropSide);
    column->AddChild(hint);

    cropper->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    column->AddChild(cropper);

    auto zoom = std::make_shared<UltraCanvasSlider>("ProfilePictureZoom", 0, 0, kCropSide, 24);
    zoom->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    zoom->SetRange(1.0f, static_cast<float>(cropper->GetMaxZoom()));
    zoom->SetStep(0.01f);
    zoom->SetValue(1.0f);
    zoom->SetAccessibleName(Tr("Zoom"));
    column->AddChild(zoom);

    // The picture at the sizes a list and a message show it.
    auto previews = std::make_shared<UltraCanvasContainer>("ProfilePicturePreviews");
    previews->layout.SetFlexRow().SetFlexGap(12).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    previews->size.width = CSSLayout::Dimension::Px(kCropSide);
    auto previewText = std::make_shared<UltraCanvasLabel>("ProfilePicturePreviewText", Tr("How others see it"));
    previewText->SetThemeVariant(LabelVariant::Muted);
    previewText->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    previews->AddChild(previewText);
    auto large = std::make_shared<UltraCanvasProfilePicture>("ProfilePicturePreviewList", 40.0f);
    auto small = std::make_shared<UltraCanvasProfilePicture>("ProfilePicturePreviewMessage", 28.0f);
    for (auto& p : {large, small}) {
        p->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        p->SetStyle(style);
        p->SetPictureFrame(sourcePath, cropper->GetFrame());
        previews->AddChild(p);
    }
    column->AddChild(previews);
    dialog->AddDialogElement(column);

    // The slider and the cropper follow each other; the previews follow both.
    // Raw pointers: all of them live in the dialog that holds these callbacks.
    auto syncing = std::make_shared<bool>(false);
    UltraCanvasProfilePictureCropper* crop = cropper.get();
    UltraCanvasSlider* slider = zoom.get();
    UltraCanvasProfilePicture* largeRaw = large.get();
    UltraCanvasProfilePicture* smallRaw = small.get();
    auto frame = std::make_shared<ProfilePictureFrame>(cropper->GetFrame());
    cropper->onFrameChanged = [slider, largeRaw, smallRaw, sourcePath, frame, syncing](const ProfilePictureFrame& f) {
        *frame = f;
        largeRaw->SetPictureFrame(sourcePath, f);
        smallRaw->SetPictureFrame(sourcePath, f);
        if (*syncing) return;
        *syncing = true;
        slider->SetValue(static_cast<float>(f.zoom));
        *syncing = false;
    };
    auto fromSlider = [crop, syncing](float v) {
        if (*syncing) return;
        *syncing = true;
        crop->SetZoom(v, true);
        *syncing = false;
    };
    zoom->onValueChanging = fromSlider;
    zoom->onValueChanged = fromSlider;

    dialog->AddCustomButton(Tr("Cancel"), DialogResult::Cancel, DialogButtonRole::Cancel);
    dialog->AddCustomButton(Tr("Use this picture"), DialogResult::OK, DialogButtonRole::Default);

    UltraCanvasDialogManager::ShowDialog(dialog, [frame, done](DialogResult result) {
        if (done) done(result == DialogResult::OK, *frame);
    }, parent);
}

// ===== THE EDITOR ROW =====
UltraCanvasProfilePictureEditor::UltraCanvasProfilePictureEditor(const std::string& identifier, float x, float y,
                                                                 float w, float h)
    : UltraCanvasContainer(identifier, x, y, w, h) {
    layout.SetFlexRow().SetFlexGap(18).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    pictureSize_ = h > 0 ? h : 96.0f;
    // As tall as the caption, the buttons and a wrapped hint need, and never
    // shorter than the picture.
    size.height = CSSLayout::Dimension::Auto();
    SetMinHeight(pictureSize_);

    picture_ = std::make_shared<UltraCanvasProfilePicture>(identifier + "-picture", pictureSize_);
    picture_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    picture_->SetEditable(true);
    AddChild(picture_);

    column_ = std::make_shared<UltraCanvasContainer>(identifier + "-actions");
    column_->layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Start);
    column_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    AddChild(column_);

    caption_ = std::make_shared<UltraCanvasLabel>(identifier + "-caption", Tr("Profile picture"));
    caption_->SetThemeVariant(LabelVariant::Muted);
    column_->AddChild(caption_);

    auto buttons = std::make_shared<UltraCanvasContainer>(identifier + "-buttons");
    buttons->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    column_->AddChild(buttons);
    choose_ = std::make_shared<UltraCanvasButton>(identifier + "-choose", 0, 0, 150, 32, Tr("Choose a picture"));
    choose_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    buttons->AddChild(choose_);
    remove_ = std::make_shared<UltraCanvasButton>(identifier + "-remove", 0, 0, 90, 32, Tr("Remove"));
    remove_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    remove_->SetThemeVariant(ButtonVariant::Ghost);
    buttons->AddChild(remove_);

    hint_ = std::make_shared<UltraCanvasLabel>(identifier + "-hint", std::string());
    hint_->SetThemeVariant(LabelVariant::Muted);
    hint_->SetWrap(TextWrap::WrapWord);
    hint_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    hint_->SetVisible(false);
    column_->AddChild(hint_);

    // The buttons and the picture are this row's children: reached raw.
    UltraCanvasProfilePicture* picture = picture_.get();
    choose_->SetOnClick([picture]() { picture->ChoosePicture(); });
    remove_->SetOnClick([picture]() { picture->RemovePicture(); });
    picture_->onPictureChanged = [this](const std::string& path) {
        UpdateButtons();
        if (onPictureChanged) onPictureChanged(path);
    };
    picture_->onPictureError = [this](const std::string& error) {
        if (onPictureError) onPictureError(error);
    };
    UpdateButtons();
    ApplyThemeIfFollowing();
}

UltraCanvasProfilePictureEditor::~UltraCanvasProfilePictureEditor() = default;

void UltraCanvasProfilePictureEditor::SetPicturePath(const std::string& path) {
    picture_->SetPicturePath(path);
    UpdateButtons();
}

const std::string& UltraCanvasProfilePictureEditor::GetPicturePath() const {
    return picture_->GetPicturePath();
}

void UltraCanvasProfilePictureEditor::SetName(const std::string& name) {
    picture_->SetName(name);
}

void UltraCanvasProfilePictureEditor::SetPictureSize(float diameter) {
    pictureSize_ = std::max(24.0f, diameter);
    picture_->size.width = CSSLayout::Dimension::Px(pictureSize_);
    picture_->size.height = CSSLayout::Dimension::Px(pictureSize_);
    SetMinHeight(pictureSize_);
    InvalidateLayout();
}

void UltraCanvasProfilePictureEditor::SetCaption(const std::string& caption) {
    caption_->SetText(caption);
    caption_->SetVisible(!caption.empty());
}

void UltraCanvasProfilePictureEditor::SetHint(const std::string& hint) {
    hint_->SetText(hint);
    hint_->SetVisible(!hint.empty());
}

void UltraCanvasProfilePictureEditor::SetMinHeight(float height) {
    CSSLayout::BoxConstraints limits = boxConstraints.value_or(CSSLayout::BoxConstraints{});
    limits.minHeight = CSSLayout::Dimension::Px(height);
    boxConstraints = limits;
}

void UltraCanvasProfilePictureEditor::UpdateButtons() {
    const bool has = picture_->HasPicture();
    choose_->SetText(has ? Tr("Change picture") : Tr("Choose a picture"));
    remove_->SetVisible(has);
}

void UltraCanvasProfilePictureEditor::ApplyTheme(const UltraCanvasTheme& theme) {
    UltraCanvasContainer::ApplyTheme(theme);
    // "Remove" in the theme's danger colour.
    remove_->SetTextColors(theme.roles.danger);
}

// ===== THE ELEMENT REGISTRY =====
UCElementDescriptor MakeProfilePictureDescriptor() {
    UCElementDescriptor descriptor;
    descriptor.typeName = "profile-picture";
    descriptor.category = UCPluginCategory::Widget;
    descriptor.displayName = "Profile Picture";
    descriptor.description = "A person's picture in a circle, or their initials on a colour; editable, it chooses "
                             "and crops a new one";
    descriptor.docPath = "Docs/UltraCanvas/UltraCanvasProfilePicture.md";
    descriptor.create = [](const std::string& id, int x, int y, int w, int h) {
        return std::static_pointer_cast<UltraCanvasUIElement>(
            std::make_shared<UltraCanvasProfilePicture>(id, x, y, w, h));
    };
    return descriptor;
}

UCElementDescriptor MakeProfilePictureEditorDescriptor() {
    UCElementDescriptor descriptor;
    descriptor.typeName = "profile-picture-editor";
    descriptor.category = UCPluginCategory::Widget;
    descriptor.displayName = "Profile Picture Editor";
    descriptor.description = "A profile page's picture row: the picture, Choose / Change and Remove, and a hint";
    descriptor.docPath = "Docs/UltraCanvas/UltraCanvasProfilePicture.md";
    descriptor.create = [](const std::string& id, int x, int y, int w, int h) {
        return std::static_pointer_cast<UltraCanvasUIElement>(
            std::make_shared<UltraCanvasProfilePictureEditor>(id, x, y, w, h));
    };
    return descriptor;
}

void RegisterProfilePictureElement() {
    UltraCanvasElementRegistry::Register(MakeProfilePictureDescriptor());
    UltraCanvasElementRegistry::Register(MakeProfilePictureEditorDescriptor());
}

} // namespace UltraCanvas
