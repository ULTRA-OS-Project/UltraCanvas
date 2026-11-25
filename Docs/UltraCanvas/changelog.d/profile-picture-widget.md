- **A profile picture element.** `UltraCanvasProfilePicture` shows a
  person's picture in a circle, or their initials on one of eight colours
  that a key (their id) picks, the same one every time. Editable, it wears a
  badge, and a click, Return or Space chooses a new picture: the FileLoader's
  Open dialog, then a crop dialog that places the picture in the circle
  (drag to move it, the wheel or a slider to zoom, previews at 40 and 28
  pixels), then the framed square written at 256 pixels - JPEG, or PNG for a
  picture with transparency - into a folder of the application's own, and
  reported through `onPictureChanged`. Delete, or **Remove**, goes back to
  the initials. The parts are there on their own too:
  - `UltraCanvasProfilePictureEditor`, the row of a profile page: the
    picture, **Choose a picture** (**Change picture** once there is one),
    **Remove** and a line of hint;
  - `UltraCanvasProfilePictureCropper`, the placing area, and
    `ShowProfilePictureCropDialog`, the dialog around it;
  - `CropProfilePicture`, which writes the square a `ProfilePictureFrame`
    frames, and `ProfilePictureInitials`.

  A selectable widget, `ProfilePicture`, registered as `profile-picture` and
  `profile-picture-editor`: compiled in by default, the
  `ultracanvas-widget-profile-picture` module in a lean build. DemoApp shows
  it under *Widgets > Profile Picture*. Covered by `ProfilePictureTest`.
