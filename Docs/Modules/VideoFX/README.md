# VideoFX - Professional Video Editing for Modern Applications

## Powerful Video Editing Without the Complexity

**VideoFX** is a comprehensive, cross-platform video editing engine that brings professional-grade editing capabilities to any application. Whether you're building a video editor, content creation tool, or media processing pipeline, VideoFX provides a complete API for timeline editing, effects, color grading, and export—all through a clean, consistent interface.

---

## Purpose

Stop building video editing from scratch or wrestling with complex multimedia frameworks. VideoFX gives you:

- **Complete Editing Pipeline** - Timeline, tracks, clips, transitions, and effects in one unified module
- **Professional Color Grading** - LUT support, color wheels, curves, and automatic correction
- **Hardware Acceleration** - NVENC, QuickSync, VideoToolbox, and VAAPI for blazing-fast export
- **Cross-Platform Compatibility** - Same code works on Windows, Linux, and macOS

---

## Key Functionality

### Non-Linear Timeline Editing
Build complex video projects with multi-track timelines, clip manipulation, and precise editing:

```cpp
VideoFXProject project;
VideoFX_CreateProject(project, VideoFXProjectSettings::HD1080p30());

VideoFXSequence sequence;
VideoFX_CreateSequence(project, "Main Edit", sequence);

VideoFXClip clip;
VideoFX_AddClip(sequence, mediaItem, videoTrack, 0.0, clip);
```

### Professional Effects & Color Grading
Apply cinematic looks with 80+ built-in effects, LUT support, and color wheels:

```cpp
// Apply a cinematic LUT
VideoFX_ApplyLUT(clip, "cinematic_teal_orange.cube", 0.8f);

// Fine-tune with color wheels
VideoFX_ApplyColorWheels(clip, lift, gamma, gain);

// Quick adjustments
VideoFX_AdjustExposure(clip, 0.5f);
VideoFX_AdjustContrast(clip, 0.2f);
```

### Seamless Transitions
Choose from 40+ transition types with customizable duration and easing:

```cpp
VideoFXTransition transition;
VideoFX_AddTransition(sequence, clipA, clipB, 
    VideoFXTransitionType::Dissolve, 1.0, transition);

VideoFX_SetTransitionEasing(transition, VideoFXEasingType::EaseInOutCubic);
```

### Keyframe Animation
Animate any property with professional bezier curves and motion paths:

```cpp
VideoFXKeyframe kf1, kf2;
VideoFX_AddKeyframeToClip(clip, VideoFXClipProperty::PositionX, 0.0, 0.0f, kf1);
VideoFX_AddKeyframeToClip(clip, VideoFXClipProperty::PositionX, 2.0, 100.0f, kf2);
VideoFX_SetKeyframeEasing(kf2, VideoFXEasingType::EaseOutBack);
```

### Hardware-Accelerated Export
Export to any format with automatic hardware acceleration:

```cpp
VideoFXExportSettings settings = VideoFXExportSettings::YouTube4K();
settings.useHardwareEncoder = true;

VideoFX_Export(project, "output.mp4", settings, 
    [](float progress) { UpdateProgressBar(progress); });
```

### Intelligent Audio Mixing
Complete audio processing with EQ, compression, ducking, and sync:

```cpp
VideoFX_ApplyAudioCompressor(clip, compressorSettings);
VideoFX_SetAudioDucking(musicTrack, voiceTrack, duckingSettings);
VideoFX_NormalizeAudio(clip, -3.0f);
```

---

## Supported Formats

**Video Codecs:** H.264, H.265/HEVC, VP9, AV1, ProRes, DNxHD, MPEG-2, MJPEG  
**Audio Codecs:** AAC, MP3, Opus, FLAC, PCM, AC3, DTS  
**Containers:** MP4, MOV, MKV, WebM, AVI, MXF, GIF  
**Images:** PNG, JPEG, TIFF, WebP, OpenEXR, DPX  
**Hardware:** NVENC, QuickSync, AMF, VideoToolbox, VAAPI

*...powered by FFmpeg with extensible plugin architecture*

---

## Why VideoFX?

### For Developers
- **264 Ready-to-Use Functions** - Complete API covering all editing operations
- **Consistent Design** - Predictable naming patterns and return types
- **Memory Efficient** - Smart caching, proxy support, and resource management
- **Fully Documented** - Comprehensive registry with examples for every function

### For Applications
- **Professional Results** - Broadcast-quality output with proper color handling
- **Fast Rendering** - Hardware acceleration on all major platforms
- **Flexible Pipeline** - PixelFX integration for custom frame processing
- **Future-Proof** - Easy to add new codecs and effects as standards evolve

### For Users
- **Smooth Editing** - Proxy workflows and preview rendering for responsive UI
- **Creative Tools** - Professional color grading and animation capabilities
- **Fast Export** - Hardware-accelerated encoding reduces wait times
- **Reliable** - Robust error handling and undo/redo support

---

## Perfect For

✅ **Video Editors** - Timeline-based editing applications  
✅ **Content Creation** - Social media tools, streaming software  
✅ **Media Processing** - Batch conversion, transcoding pipelines  
✅ **Broadcast** - Professional video production workflows  
✅ **Education** - Tutorial creators, e-learning platforms  
✅ **Enterprise** - Corporate video, training content systems

---

## Get Started

VideoFX integrates seamlessly into any C++ application:

```cpp
// Create a project
VideoFXProject project;
VideoFX_CreateProject(project, VideoFXProjectSettings::HD1080p30());

// Import media
VideoFXMediaItem media;
VideoFX_ImportMedia(project, "footage.mp4", media);

// Build your timeline
VideoFXSequence sequence;
VideoFX_GetActiveSequence(project, sequence);

VideoFXTrack track;
VideoFX_AddTrack(sequence, VideoFXTrackType::Video, track);

VideoFXClip clip;
VideoFX_AddClip(sequence, media, track, 0.0, clip);

// Apply effects
VideoFX_AdjustSaturation(clip, 0.2f);
VideoFX_ApplyLUT(clip, "film_look.cube", 1.0f);

// Export
VideoFX_Export(project, "final.mp4", VideoFXExportSettings::YouTube1080());

// Clean up
VideoFX_CloseProject(project);
```

---

## The Bottom Line

**Stop building video editing infrastructure. Start shipping features.**

VideoFX handles the complexity of video manipulation so you can focus on what makes your application unique. With professional editing capabilities, hardware acceleration, and a clean API, VideoFX is the complete video editing solution for modern C++ applications.

**Professional. Accelerated. Complete.**

*VideoFX - Part of the UltraCanvas Framework*
