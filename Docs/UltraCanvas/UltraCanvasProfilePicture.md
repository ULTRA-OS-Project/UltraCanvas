# UltraCanvas Profile Picture Documentation

<!-- doc-check: std::shared_ptr<UltraCanvasContainer> card; std::shared_ptr<UltraCanvasContainer> row; UltraCanvasWindowBase* window; std::string savedPicture; std::string dataFolder; void SavePicturePath(const std::string& path); void ShowStatus(const std::string& text); -->

## Contents

- [Overview](#overview)
- [Quick Start](#quick-start)
- [The picture](#the-picture)
- [Choosing and placing a picture](#choosing-and-placing-a-picture)
- [The cropper on its own](#the-cropper-on-its-own)
- [Writing the square yourself](#writing-the-square-yourself)
- [Style and the theme](#style-and-the-theme)
- [The element registry](#the-element-registry)

## Overview

A person's profile picture, the way the messengers show one: a square picture in a circle, or, without one, the initials of the name on a colour picked from a key (the same key always gets the same colour). Four parts, in `UltraCanvasProfilePicture.h`:

| Part | What it is |
|---|---|
| `UltraCanvasProfilePicture` | The round picture. Editable, it wears a badge - a camera with a picture, a **+** without - and a click (or Return / Space with the focus) chooses a new picture; Delete removes it. |
| `UltraCanvasProfilePictureEditor` | The row of a profile page: the picture, a caption, **Choose a picture** (**Change picture** once there is one), **Remove**, and a line of hint. Built from `UltraCanvasButton` and `UltraCanvasLabel`. |
| `UltraCanvasProfilePictureCropper` | The area a picture is placed in: the whole picture under a round window, dragged to move it, the wheel (or `SetZoom`) to zoom, the arrow keys and **+** / **-** with the focus; what lies outside the circle is dimmed. |
| `ShowProfilePictureCropDialog` | That cropper in a dialog, with a zoom slider, the picture as it will look at 40 and 28 pixels, **Cancel** and **Use this picture**. |

Choosing goes through three steps: the FileLoader's Open dialog (every picture type this build reads), the crop dialog, then `CropProfilePicture`, which writes the square that was framed - 256 pixels a side by default, JPEG, or PNG when the picture has transparency - into the output folder. `onPictureChanged` reports the new file; removing reports `""`. The picture shown follows at once. The file is the application's own (a cache or profile folder of its own, `SetOutputDirectory`), so the application keeps the path and hands it back with `SetPicturePath` at the next start.

## Quick Start

```cpp
#include "UltraCanvasProfilePicture.h"

auto editor = CreateProfilePictureEditor("ProfilePicture", 440);
editor->SetName("Erin Mayer");                           // the initials and the accessible name
editor->SetHint("A photo of you, or anything square. Your contacts see it beside your name.");
editor->GetPicture()->SetOutputDirectory(dataFolder);    // where chosen pictures are written
editor->SetPicturePath(savedPicture);                    // "" shows the initials
editor->onPictureChanged = [](const std::string& path) {
    SavePicturePath(path);                               // "" when the picture was removed
};
editor->onPictureError = [](const std::string& error) {
    ShowStatus("The picture could not be written: " + error);
};
card->AddChild(editor);
```

An application that styles its own buttons reaches the row's two with `GetChooseButton()` and `GetRemoveButton()`. The row is at least as tall as its picture (96 pixels; `SetPictureSize`), taller when the caption, the buttons and a wrapped hint need it. `SetCaption("")` hides the caption, `SetHint("")` the hint.

## The picture

```cpp
// In a list: 40 pixels, a person's id picks the colour of their initials.
auto avatar = CreateProfilePicture("Avatar", 40);
avatar->SetName("Mika Korhonen");
avatar->SetColorKey("user-7c1f");
avatar->SetPicturePath(savedPicture);      // shown in the circle; a picture that is not square shows its middle square
row->AddChild(avatar);

// Editable on its own, without the row's buttons.
auto mine = CreateProfilePicture("MyPicture", 72);
mine->SetName("Erin Mayer");
mine->SetEditable(true);
mine->onPictureChanged = [](const std::string& path) { SavePicturePath(path); };
```

- `ProfilePictureInitials(name)` gives the initials it draws: the first letters of the first and last word, upper-cased (`"Erin Mayer"` → `EM`, `"erin"` → `E`); a letter outside ASCII is taken whole.
- `PictureRect()` is the circle: the element's largest centred square.
- `SetPictureFrame(source, frame)` shows a square of a picture without writing a file - the crop dialog's previews use it; `SetPicturePath` clears it.
- `SetOutputSize(pixels)` (256 by default, 64 to 1024) and `SetMaxOutputBytes(bytes)` (0, the default, for any size) say what a chosen picture is written as: a service that takes pictures up to a size - ChatCube Direct's 32 KiB in one sealed message - gets one that fits (see `CropProfilePictureWithin` below).
- `ChoosePicture()` opens the chooser from code (the editor row's button does); `PlacePicture(path)` starts at the crop dialog with a picture the application already has (a file dropped on it, a photo taken); `RemovePicture()` goes back to the initials.

## Choosing and placing a picture

The crop dialog can be used without the picture element:

```cpp
ShowProfilePictureCropDialog(window, "/home/erin/Pictures/beach.jpg", ProfilePictureStyle::FromTheme(),
    [](bool placed, const ProfilePictureFrame& frame) {
        if (!placed) return;                             // Cancel, or the file could not be read
        std::string error;
        if (!CropProfilePicture("/home/erin/Pictures/beach.jpg", frame, 256, "/tmp/me.jpg", &error))
            ShowStatus(error);
    });
```

A `ProfilePictureFrame` says which square of the picture is shown: `centerX` and `centerY` are its centre as fractions of the picture's width and height (0.5, 0.5 is the middle), and `zoom` 1 is the largest square that fits (the picture's shorter side), 2 a square half as wide. `SquareIn(width, height)` gives that square in pixels and `Clamped(width, height)` brings a frame back inside the picture.

## The cropper on its own

```cpp
auto cropper = std::make_shared<UltraCanvasProfilePictureCropper>("Cropper", 260.0f);
cropper->SetSourcePath("/home/erin/Pictures/beach.jpg");   // false when it is no picture
cropper->SetMaxZoom(4.0);                                  // the default
auto preview = CreateProfilePicture("Preview", 64);
cropper->onFrameChanged = [preview = preview.get(), cropper = cropper.get()](const ProfilePictureFrame& f) {
    preview->SetPictureFrame(cropper->GetSourcePath(), f);
};
cropper->SetZoom(1.5, true);                               // true: report it as a drag would
card->AddChild(cropper);
card->AddChild(preview);
```

The circle is the cropper's largest centred square less `ProfilePictureStyle::cropInset` on each side (`CircleRect()`). A drag moves the picture with the pointer, and stops at the picture's edge.

## Writing the square yourself

```cpp
std::string error;
const bool written = CropProfilePicture("/home/erin/Pictures/beach.jpg",
                                        ProfilePictureFrame{0.4, 0.5, 1.2},
                                        512, "/tmp/me.png", &error);
const bool keepAsPng = ProfilePictureHasTransparency("/home/erin/Pictures/logo.png");
```

The format follows the extension of the file written: `.jpg` / `.jpeg` (quality 88, transparency put over white), `.webp`, otherwise PNG with its transparency. Every output pixel averages the source pixels it covers, so a large photo shrinks sharp and a small picture is enlarged smoothly to the size asked for. The source is read at no more than 2048 pixels on its long side.

To keep the file under a size, `CropProfilePictureWithin` writes it as `CropProfilePicture` does and, when it comes out larger, again as JPEG at quality 75 and 60, then at three quarters and half the size (never under 64 pixels), until it fits. The path written comes back - the one asked for, or the same with `.jpg` when a PNG had to become a JPEG; nothing is left behind when not even the smallest fits.

```cpp
std::string written, why;
if (CropProfilePictureWithin("/home/erin/Pictures/beach.png", ProfilePictureFrame{}, 256, "/tmp/me.png",
                             32 * 1024, &written, &why))
    SavePicturePath(written);   // "/tmp/me.jpg" when the PNG was too large
```

## Style and the theme

`ProfilePictureStyle` holds the eight initials colours (`initialsPalette`), the initials' two inks (`InkOn` picks whichever reads on the colour), the badge's colours and size, the hover shade, the focus ring, and the cropper's background, shade, ring and inset. `ProfilePictureStyle::Light()` and `Dark()` are the two looks; `FromTheme(theme)` takes the accent for the badge and the focus, the panel for the badge's border and the palette for the theme's darkness - or the theme's own `avatar.0` … `avatar.7` roles where it names them. A picture that follows the theme restyles with it; `SetStyle` by hand stops the following.

```cpp
ProfilePictureStyle style = ProfilePictureStyle::Dark();
style.ringColor = Color(255, 255, 255, 60);              // a hairline round the circle
const Color mine = style.ColorForKey("user-7c1f");       // the colour that key's initials get
auto picture = CreateProfilePicture("Picture", 48);
picture->SetStyle(style);
```

## The element registry

Two type names, category Widget: `profile-picture` and `profile-picture-editor`. Both come from the selectable widget `ProfilePicture`: compiled into the core by default, or, when `ULTRACANVAS_WIDGETS` leaves it out, the runtime module `ultracanvas-widget-profile-picture` in `<build>/Plugins/Elements` (see [UltraCanvasElementPlugins.md](UltraCanvasElementPlugins.md)).

```cpp
UltraCanvas_RegisterSelectedWidgets();
auto byName = UltraCanvasElementRegistry::Create("profile-picture", "Avatar", 0, 0, 64, 64);
```

DemoApp shows it under **Widgets > Profile Picture**; `Tests/ProfilePictureTest.cpp` covers the initials, the colours, the frame, the writing, the cropper's input and the editor row.
