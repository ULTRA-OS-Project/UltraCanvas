- **VideoFX 0.12.0: hardware encoders by choice.** The new
  `VideoFXExportSettings::acceleration` chooses the video encoder:
  - `Auto` keeps the old order (software, then VideoToolbox or Media
    Foundation).
  - `Software` uses software encoders only.
  - `Hardware` takes the first hardware encoder that works on the machine,
    and falls back to software.
  - `NVENC`, `QuickSync`, `VAAPI`, `AMF`, `VideoToolbox` and
    `MediaFoundation` name one family. If it does not work, the export fails
    with `EncoderNotAvailable` and says whether FFmpeg lacks the encoder or
    the machine lacks the hardware.
  - Each hardware encoder is tried once, with one small frame, before it is
    chosen, since FFmpeg builds often carry encoders the computer cannot run.
    The answers are kept for the rest of the process.
  - VA-API frames are uploaded to graphics memory before they are encoded.
  - New: `VideoFXAcceleration`, `VideoFXEncoderInfo`,
    `VideoFX_ListVideoEncoders` (every encoder of the build and whether it
    works here, for a settings dialog), `VideoFX_ChooseVideoEncoder`,
    `VideoFX_AccelerationName`, and an acceleration argument on
    `VideoFX_IsVideoEncoderAvailable`.
  - Saved in project files. Proxies can use it too
    (`VideoFXProxySettings::acceleration`).
  - `videofx encoders [codec]`, and `--hw software|hardware|nvenc|qsv|vaapi|amf|videotoolbox|mf`
    on every export command.
