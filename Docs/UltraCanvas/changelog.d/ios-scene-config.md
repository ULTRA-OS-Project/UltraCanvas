- **iOS: a nameless scene configuration, and no mixed-enum warning when
  presenting.**
  - The scene configuration is nameless. A named one is looked up in
    Info.plist first, and UIKit logged an error when it was not there.
  - The bitmap info that presents a frame combines its two enums through a
    `CGBitmapInfo` cast, so the iOS compile has no warnings.
