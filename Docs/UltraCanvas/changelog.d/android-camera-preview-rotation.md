- **The Android camera preview follows the device as it turns.** It used to
  stay at the rotation it had when the camera opened, so turning the phone
  left the picture sideways until the camera was closed and opened again.
  - Every preview frame is now turned for the display as it is at that
    moment. `UltraCanvasActivity` reports each turn through a display
    listener, which also catches 180-degree flips that change no
    configuration. The application re-reads the rotation on every
    configuration change too.
  - Preview frames swap width and height when the device turns between
    portrait and landscape.
  - A recording still keeps the orientation it started with, because an MP4
    file has a single orientation.
