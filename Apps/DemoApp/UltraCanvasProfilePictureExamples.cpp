// Apps/DemoApp/UltraCanvasProfilePictureExamples.cpp
// Demonstration of the profile picture: the row of a profile page (choose,
// crop, change, remove), pictures and initials at the sizes a messenger
// uses, and the cropper on its own with its zoom slider.
// Version: 1.0.1 - no identifier named small (a <windows.h> macro: char, from the RPC headers)
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasProfilePicture.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSlider.h"

#include <cmath>
#include <memory>
#include <string>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateProfilePictureExamples() {
        auto page = DemoKit::CreatePage("ProfilePictureExamples", Tr("Profile Picture"),
            Tr("A person's picture in a circle, or their initials on a colour that their id picks. "
            "Editable, a click on it - or Choose a picture - opens the file dialog, then a crop "
            "dialog to place the picture in the circle; the square framed is written at 256 pixels "
            "and reported to the application. Remove goes back to the initials."));

        const std::string mediaRoot = NormalizePath(GetResourcesDir() + "media/images/");
        auto row = DemoKit::AddRow(page, "ProfilePictureRow");

        // ===== Column 1: the row of a profile page =====
        auto editorCard = DemoKit::AddCard(row, "ProfilePictureEditorCard", Tr("On a profile page"));
        auto editor = CreateProfilePictureEditor("ProfilePictureEditor", 400);
        editor->SetName("Erin Mayer");
        editor->SetHint(Tr("A photo of you, or anything square. Your contacts see it beside your name."));
        DemoKit::AddFixed(editorCard, editor);
        auto status = DemoKit::AddText(editorCard, "ProfilePictureEditorStatus",
                                       Tr("No picture yet: the initials. Click the circle or Choose a picture."));
        // Raw pointers: the callbacks live on the editor, in the same card.
        editor->onPictureChanged = [status = status.get()](const std::string& path) {
            status->SetText(path.empty() ? Tr("Removed: the initials again.")
                                         : TrF("Written: {0}", path));
        };
        editor->onPictureError = [status = status.get()](const std::string& error) {
            status->SetText(TrF("The picture could not be written: {0}", error));
        };

        // ===== Column 2: sizes and initials =====
        auto sizesCard = DemoKit::AddCard(row, "ProfilePictureSizesCard", Tr("Sizes, pictures and initials"));
        auto sizes = DemoKit::AddRow(sizesCard, "ProfilePictureSizes");
        for (float diameter : {64.0f, 48.0f, 40.0f, 28.0f}) {
            auto p = CreateProfilePicture("ProfilePictureSize" + std::to_string(static_cast<int>(diameter)), diameter);
            p->SetName("Mika Korhonen");
            p->SetPicturePath(mediaRoot + "portrait.jpg");
            DemoKit::AddFixed(sizes, p);
        }
        DemoKit::AddCaption(sizesCard, "ProfilePictureSizesNote",
            Tr("A picture that is not square shows its middle square."));
        auto names = DemoKit::AddRow(sizesCard, "ProfilePictureInitials");
        int n = 0;
        for (const char* name : {"Erin Mayer", "Bob", "Ana Lucía Pérez", "Zoë", "李 小龙", "ULTRA OS Team"}) {
            auto p = CreateProfilePicture("ProfilePictureInitials" + std::to_string(n++), 44.0f);
            p->SetName(name);
            DemoKit::AddFixed(names, p);
        }
        DemoKit::AddCaption(sizesCard, "ProfilePictureInitialsNote",
            Tr("No picture: the first letters of the first and last name, on one of eight colours the "
            "name (or SetColorKey, a person's id) picks - the same one every time."));
        auto editable = CreateProfilePicture("ProfilePictureEditable", 72.0f);
        editable->SetName("Bob");
        editable->SetEditable(true);
        DemoKit::AddFixed(sizesCard, editable);
        DemoKit::AddCaption(sizesCard, "ProfilePictureEditableNote",
            Tr("Editable on its own: the badge, the hand pointer; a click, Return or Space chooses a "
            "picture and Delete removes it."));

        // ===== Column 3: the cropper =====
        auto cropCard = DemoKit::AddCard(row, "ProfilePictureCropCard", Tr("The cropper"));
        auto cropper = std::make_shared<UltraCanvasProfilePictureCropper>("ProfilePictureCropper", 260.0f);
        cropper->SetSourcePath(mediaRoot + "landscape.jpg");
        DemoKit::AddFixed(cropCard, cropper);
        auto zoom = std::make_shared<UltraCanvasSlider>("ProfilePictureCropZoom", 0, 0, 260, 24);
        zoom->SetRange(1.0f, static_cast<float>(cropper->GetMaxZoom()));
        zoom->SetStep(0.01f);
        zoom->SetValue(1.0f);
        DemoKit::AddFixed(cropCard, zoom);
        auto previews = DemoKit::AddRow(cropCard, "ProfilePictureCropPreviews");
        auto large = CreateProfilePicture("ProfilePictureCropLarge", 64.0f);
        auto compact = CreateProfilePicture("ProfilePictureCropSmall", 28.0f);
        large->SetPictureFrame(cropper->GetSourcePath(), cropper->GetFrame());
        compact->SetPictureFrame(cropper->GetSourcePath(), cropper->GetFrame());
        DemoKit::AddFixed(previews, large);
        DemoKit::AddFixed(previews, compact);
        auto open = std::make_shared<UltraCanvasButton>("ProfilePictureCropDialog", 0, 0, 170, 30,
                                                        Tr("Open the crop dialog"));
        DemoKit::AddFixed(cropCard, open);
        DemoKit::AddCaption(cropCard, "ProfilePictureCropNote",
            Tr("Drag the picture, turn the wheel or use the slider; the arrow keys move it and + and - "
            "zoom once it has the focus. The circles follow without writing a file."));

        // The slider, the cropper and the previews sit in this card: raw pointers.
        cropper->onFrameChanged = [slider = zoom.get(), large = large.get(), compact = compact.get(),
                                   source = cropper->GetSourcePath()](const ProfilePictureFrame& f) {
            large->SetPictureFrame(source, f);
            compact->SetPictureFrame(source, f);
            if (std::abs(slider->GetValue() - static_cast<float>(f.zoom)) > 0.001f)
                slider->SetValue(static_cast<float>(f.zoom));
        };
        auto fromSlider = [crop = cropper.get()](float v) {
            if (std::abs(crop->GetZoom() - v) > 0.001) crop->SetZoom(v, true);
        };
        zoom->onValueChanging = fromSlider;
        zoom->onValueChanged = fromSlider;
        open->SetOnClick([open = open.get(), crop = cropper.get(), large = large.get(), compact = compact.get()]() {
            const std::string source = crop->GetSourcePath();
            ShowProfilePictureCropDialog(open->GetWindow(), source, ProfilePictureStyle::FromTheme(),
                [crop, large, compact, source](bool placed, const ProfilePictureFrame& f) {
                    if (!placed) return;
                    crop->SetFrame(f);
                    large->SetPictureFrame(source, f);
                    compact->SetPictureFrame(source, f);
                });
        });

        DemoKit::AddNotes(page, "ProfilePictureNotes", Tr("Implementation notes"), {
            Tr("UltraCanvasProfilePicture is the circle; UltraCanvasProfilePictureEditor the row with its "
               "buttons; UltraCanvasProfilePictureCropper the placing area; ShowProfilePictureCropDialog "
               "puts it in a dialog."),
            Tr("CropProfilePicture writes the framed square (JPEG, or PNG for a picture with "
               "transparency) into SetOutputDirectory, a folder of the application's own."),
            Tr("In the element registry as profile-picture and profile-picture-editor; a lean build "
               "makes it the ultracanvas-widget-profile-picture module.")});
        return page;
    }

} // namespace UltraCanvas
