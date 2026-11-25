// include/UltraCanvasProfilePicture.h
// A person's profile picture - shown round, chosen, placed and removed.
//
//  - UltraCanvasProfilePicture: the round picture itself. A square picture
//    file is shown in a circle; without one, the initials of the name on a
//    colour picked from a key (the same key always gets the same colour), as
//    the messengers do. Editable, it wears a small badge - a camera with a
//    picture, a "+" without - and a click on it chooses a new picture.
//  - UltraCanvasProfilePictureEditor: the row of a profile page - that
//    picture, a caption, "Choose a picture" (or "Change picture" once there
//    is one), "Remove", and a line of hint under them.
//  - UltraCanvasProfilePictureCropper: the area the picture is placed in - the
//    whole picture under a round window, dragged to move it, the wheel (or
//    SetZoom) to zoom; what lies outside the circle is dimmed.
//  - ShowProfilePictureCropDialog: that cropper in a dialog, with a zoom
//    slider, the picture as it will look at two small sizes, Cancel and
//    "Use this picture".
//
// Choosing goes: the file dialog (UltraCanvasFileLoader's picture types),
// then the crop dialog, then CropProfilePicture writes the square that was
// framed - outputSize pixels a side, JPEG, or PNG when the picture has
// transparency - into the output folder, and onPictureChanged reports the
// new file. Removing reports "". The picture shown follows at once; the
// host keeps the file (or sends it on) and gives it back with
// SetPicturePath at the next start.
//
// The file written is the application's own (a cache, a profile folder),
// not a document of the user's, so it is written directly; the picture
// chosen is opened through the FileLoader's dialog.
//
// "profile-picture" and "profile-picture-editor" in the element registry
// (category Widget): compiled into the core by default, or the
// ultracanvas-widget-profile-picture runtime module when ULTRACANVAS_WIDGETS
// leaves ProfilePicture out.
//
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasCommonTypes.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

class UltraCanvasButton;
class UltraCanvasLabel;
class UltraCanvasWindowBase;
struct UltraCanvasTheme;

// ===== THE FRAME =====
// Which square of a picture is shown: its centre as a fraction of the
// picture's width and height (0.5, 0.5 is the middle), and the zoom - 1 is
// the largest square that fits the picture (its shorter side), 2 a square
// half as wide. Clamped so the square never leaves the picture.
struct ProfilePictureFrame {
    double centerX = 0.5;
    double centerY = 0.5;
    double zoom = 1.0;

    // The square in pixels of a `width` x `height` picture, as x, y, side.
    Rect2Dd SquareIn(int width, int height) const;
    // This frame moved and zoomed back inside a `width` x `height` picture.
    ProfilePictureFrame Clamped(int width, int height) const;
    bool operator==(const ProfilePictureFrame& o) const {
        return centerX == o.centerX && centerY == o.centerY && zoom == o.zoom;
    }
};

// ===== STYLE =====
struct ProfilePictureStyle {
    // The initials' colours: a name's key picks one (ColorForKey). Deep
    // enough for white initials on the light look, light under dark ink on
    // the dark one (initialsInk picks whichever reads).
    std::vector<Color> initialsPalette;
    Color initialsInkLight = Color(255, 255, 255, 255);
    Color initialsInkDark  = Color(24, 26, 31, 255);
    float initialsScale = 0.38f;          // the initials' size, of the diameter

    Color ringColor = Color(0, 0, 0, 0);  // a hairline round the picture (transparent: none)
    float ringWidth = 1.0f;

    // The badge of an editable picture, at the lower right.
    Color badgeColor  = Color(34, 139, 70, 255);
    Color badgeInk    = Color(255, 255, 255, 255);
    Color badgeBorder = Color(255, 255, 255, 255);   // the surface the picture sits on
    float badgeScale  = 0.29f;            // the badge's diameter, of the picture's

    Color hoverShade = Color(0, 0, 0, 60);    // over an editable picture under the pointer
    Color focusRing  = Color(34, 139, 70, 255);

    // The cropper.
    Color cropBackground = Color(28, 30, 36, 255);
    Color cropShade      = Color(0, 0, 0, 128);   // over what is outside the circle
    Color cropRing       = Color(255, 255, 255, 255);
    float cropInset      = 20.0f;         // between the circle and the cropper's edge

    // The light look's colours: the ChatCube green, eight initials hues.
    static ProfilePictureStyle Light();
    // The dark one's.
    static ProfilePictureStyle Dark();
    // The theme's roles over a base's own sizes: the accent for the badge and
    // the focus, the surface under the picture for the badge's border, the
    // palette for the theme's darkness (or the theme's "avatar.0" ...
    // "avatar.7" roles where it names them)
    static ProfilePictureStyle FromTheme(const UltraCanvasTheme& theme, const ProfilePictureStyle& base);
    static ProfilePictureStyle FromTheme(const UltraCanvasTheme& theme);
    static ProfilePictureStyle FromTheme();

    // The palette colour for `key` (a stable id, else the name): the same key
    // always gets the same one.
    Color ColorForKey(const std::string& key) const;
    // White or the dark ink, whichever reads better on `fill`.
    Color InkOn(const Color& fill) const;
};

// ===== WRITING THE PICTURE =====
// The initials of a name: the first letters of its first and last words
// ("Erin Mayer" -> "EM", "erin" -> "E", "" -> ""), upper-cased where the
// letter has a capital; a letter outside ASCII is taken whole (UTF-8).
std::string ProfilePictureInitials(const std::string& name);

// Writes the square `frame` shows of the picture at `sourcePath` to
// `outputPath`, `outputSize` pixels a side - a small picture is scaled up,
// so every picture written has the same size. The format follows the
// extension of `outputPath` (".jpg", ".png", ".webp" ...). False, with
// `error`, when the source cannot be read or the file not written.
bool CropProfilePicture(const std::string& sourcePath, const ProfilePictureFrame& frame, int outputSize,
                        const std::string& outputPath, std::string* error = nullptr);
// Whether the picture at `path` lets what is behind it show through (a PNG
// logo), so it is written as PNG rather than JPEG.
bool ProfilePictureHasTransparency(const std::string& path);

// ===== THE PICTURE =====
class UltraCanvasProfilePicture : public UltraCanvasUIElement {
public:
    UltraCanvasProfilePicture(const std::string& identifier, float x, float y, float w, float h);
    UltraCanvasProfilePicture(const std::string& identifier, float diameter)
        : UltraCanvasProfilePicture(identifier, -1, -1, diameter, diameter) {}
    explicit UltraCanvasProfilePicture(const std::string& identifier)
        : UltraCanvasProfilePicture(identifier, -1, -1, 96, 96) {}
    ~UltraCanvasProfilePicture() override;

    // ===== WHAT IT SHOWS =====
    // A picture file (UTF-8), shown whole in the circle when it is square,
    // its middle square otherwise; "" for none. No callback.
    void SetPicturePath(const std::string& path);
    const std::string& GetPicturePath() const { return picturePath_; }
    bool HasPicture() const { return !picturePath_.empty(); }
    // A square of a picture, not yet written: what the crop dialog's
    // previews show. SetPicturePath clears it.
    void SetPictureFrame(const std::string& sourcePath, const ProfilePictureFrame& frame);
    // The name the initials come from and its accessible name.
    void SetName(const std::string& name);
    const std::string& GetName() const { return name_; }
    // What picks the initials' colour (a person's id); "" uses the name.
    void SetColorKey(const std::string& key);
    const std::string& GetColorKey() const { return colorKey_; }

    // ===== CHOOSING =====
    // Editable: the badge, the hand pointer, and a click (or Return / Space
    // with the focus) opens ChoosePicture. Off by default.
    void SetEditable(bool editable);
    bool IsEditable() const { return editable_; }
    // Where chosen pictures are written: a folder of the application's own.
    // Default: "ultracanvas-profile-pictures" in the temporary folder.
    void SetOutputDirectory(const std::string& directory) { outputDirectory_ = directory; }
    const std::string& GetOutputDirectory() const { return outputDirectory_; }
    // The side of a picture written, in pixels (64 to 1024; 256 by default).
    void SetOutputSize(int pixels);
    int GetOutputSize() const { return outputSize_; }
    // The file dialog, then the crop dialog; the picture chosen replaces
    // this one and onPictureChanged reports it. Cancel at either step
    // changes nothing.
    void ChoosePicture();
    // The crop dialog for a picture already chosen (a file dropped, a
    // photo taken), then as ChoosePicture.
    void PlacePicture(const std::string& sourcePath);
    // Back to the initials; onPictureChanged("").
    void RemovePicture();

    // A new picture written (its path), or "" when it was removed.
    std::function<void(const std::string& path)> onPictureChanged;
    // Writing the chosen picture failed (why).
    std::function<void(const std::string& error)> onPictureError;

    // ===== APPEARANCE =====
    void SetStyle(const ProfilePictureStyle& style);
    const ProfilePictureStyle& GetStyle() const { return style_; }
    void ApplyTheme(const UltraCanvasTheme& theme) override;
    // The circle: the element's largest centred square.
    Rect2Df PictureRect() const;

    // ===== OVERRIDES =====
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;
    bool AcceptsFocus() const override { return editable_ && IsVisible() && !IsDisabled(); }
    AccessibleRole GetAccessibleRole() const override;

private:
    // The written picture's file name: a new one each time, so a cached
    // copy of the last one is never shown in its place.
    std::string NextOutputPath(bool transparent) const;
    void Commit(const std::string& sourcePath, const ProfilePictureFrame& frame);

    std::string picturePath_;
    std::string frameSource_;          // SetPictureFrame's picture ("" when none)
    ProfilePictureFrame frame_;
    std::string name_;
    std::string colorKey_;
    bool editable_ = false;
    bool hovered_ = false;
    bool styleByHand_ = false;
    std::string outputDirectory_;
    int outputSize_ = 256;
    ProfilePictureStyle style_;
};

// ===== THE CROPPER =====
class UltraCanvasProfilePictureCropper : public UltraCanvasUIElement {
public:
    UltraCanvasProfilePictureCropper(const std::string& identifier, float x, float y, float w, float h);
    UltraCanvasProfilePictureCropper(const std::string& identifier, float side)
        : UltraCanvasProfilePictureCropper(identifier, -1, -1, side, side) {}

    // The picture to place; the frame starts in its middle, zoom 1. False
    // when it cannot be read.
    bool SetSourcePath(const std::string& path);
    const std::string& GetSourcePath() const { return sourcePath_; }
    int GetSourceWidth() const { return sourceWidth_; }
    int GetSourceHeight() const { return sourceHeight_; }

    void SetFrame(const ProfilePictureFrame& frame, bool notify = false);
    const ProfilePictureFrame& GetFrame() const { return frame_; }
    // 1 (the whole shorter side) to GetMaxZoom(), about the frame's centre.
    void SetZoom(double zoom, bool notify = false);
    double GetZoom() const { return frame_.zoom; }
    void SetMaxZoom(double zoom) { maxZoom_ = zoom < 1.0 ? 1.0 : zoom; }
    double GetMaxZoom() const { return maxZoom_; }

    // The circle, in the element's own coordinates.
    Rect2Df CircleRect() const;

    // The frame moved or zoomed by the user (or by a notifying SetFrame / SetZoom).
    std::function<void(const ProfilePictureFrame&)> onFrameChanged;

    void SetStyle(const ProfilePictureStyle& style) { style_ = style; RequestRedraw(); }
    const ProfilePictureStyle& GetStyle() const { return style_; }
    void ApplyTheme(const UltraCanvasTheme& theme) override;

    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;
    bool AcceptsFocus() const override { return IsVisible() && !IsDisabled(); }
    AccessibleRole GetAccessibleRole() const override;

private:
    void Changed(bool notify);
    // Moves the frame by a pointer's step in the element's pixels.
    void MoveBy(double dx, double dy);

    std::string sourcePath_;
    int sourceWidth_ = 0;
    int sourceHeight_ = 0;
    ProfilePictureFrame frame_;
    double maxZoom_ = 4.0;
    bool dragging_ = false;
    Point2Di dragFrom_;
    ProfilePictureStyle style_;
};

// ===== THE CROP DIALOG =====
// The cropper (300 x 300) with a zoom slider and the picture at the sizes a
// list and a message show it, over Cancel and "Use this picture". `done`
// gets true and the frame chosen, or false when it was cancelled (or the
// picture could not be read: `done` is then called at once).
void ShowProfilePictureCropDialog(UltraCanvasWindowBase* parent, const std::string& sourcePath,
                                  const ProfilePictureStyle& style,
                                  std::function<void(bool placed, const ProfilePictureFrame& frame)> done);

// ===== THE EDITOR ROW =====
// [picture]  Profile picture
//            [Choose a picture] [Remove]
//            hint
class UltraCanvasProfilePictureEditor : public UltraCanvasContainer {
public:
    UltraCanvasProfilePictureEditor(const std::string& identifier, float x, float y, float w, float h);
    UltraCanvasProfilePictureEditor(const std::string& identifier, float w)
        : UltraCanvasProfilePictureEditor(identifier, -1, -1, w, 96) {}
    ~UltraCanvasProfilePictureEditor() override;

    // The picture in the row: name, colour key, output folder and size are
    // set on it.
    UltraCanvasProfilePicture* GetPicture() const { return picture_.get(); }
    void SetPicturePath(const std::string& path);
    const std::string& GetPicturePath() const;
    void SetName(const std::string& name);

    // The diameter of the picture (96 by default); the row is at least as tall,
    // taller when its caption, buttons and hint need it.
    void SetPictureSize(float diameter);
    float GetPictureSize() const { return pictureSize_; }
    // The caption over the buttons ("Profile picture"), and the line under them
    // (none by default; the host says who sees the picture).
    void SetCaption(const std::string& caption);
    void SetHint(const std::string& hint);

    // As on the picture: a new picture's path, "" when removed.
    std::function<void(const std::string& path)> onPictureChanged;
    std::function<void(const std::string& error)> onPictureError;

    void ApplyTheme(const UltraCanvasTheme& theme) override;

private:
    void UpdateButtons();
    void SetMinHeight(float height);

    float pictureSize_ = 96.0f;
    std::shared_ptr<UltraCanvasProfilePicture> picture_;
    std::shared_ptr<UltraCanvasContainer> column_;
    std::shared_ptr<UltraCanvasLabel> caption_;
    std::shared_ptr<UltraCanvasButton> choose_;
    std::shared_ptr<UltraCanvasButton> remove_;
    std::shared_ptr<UltraCanvasLabel> hint_;
};

// ===== FACTORIES =====
inline std::shared_ptr<UltraCanvasProfilePicture> CreateProfilePicture(const std::string& identifier,
                                                                       float diameter = 96) {
    return std::make_shared<UltraCanvasProfilePicture>(identifier, diameter);
}
inline std::shared_ptr<UltraCanvasProfilePictureEditor> CreateProfilePictureEditor(const std::string& identifier,
                                                                                   float width = 440) {
    return std::make_shared<UltraCanvasProfilePictureEditor>(identifier, width);
}

// The two elements in the element registry (UltraCanvasElementPlugins.h) as
// "profile-picture" and "profile-picture-editor" (category Widget):
// compiled into the core by default, or the ultracanvas-widget-profile-picture
// runtime module when ULTRACANVAS_WIDGETS leaves ProfilePicture out.
struct UCElementDescriptor;
UCElementDescriptor MakeProfilePictureDescriptor();
UCElementDescriptor MakeProfilePictureEditorDescriptor();
void RegisterProfilePictureElement();

} // namespace UltraCanvas
