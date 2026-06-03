# AudioFX - Professional Audio Processing for Modern Applications

## Complete Audio Processing in a Single Library

**AudioFX** is a comprehensive, cross-platform audio processing library that brings professional-grade audio capabilities to any application. Whether you're building music software, podcasting tools, video editors, or game engines, AudioFX provides a unified API for loading, processing, analyzing, and exporting audio across all major formats.

---

## Purpose

Stop juggling multiple audio libraries and complex DSP code. AudioFX gives you:

- **One API for Everything** - Load, process, analyze, and export audio through a single, consistent interface
- **Professional Effects** - Studio-quality reverb, compression, EQ, and 30+ effects ready to use
- **Intelligent Analysis** - BPM detection, spectrum analysis, loudness metering, and waveform visualization
- **Cross-Platform Compatibility** - Same code works on Windows, Linux, and macOS

---

## Key Functionality

### Universal Audio Loading
Load any supported audio format with one simple call. AudioFX automatically detects the format and applies the appropriate decoder:

```cpp
AudioFXBuffer buffer;
AudioFX_Load("podcast.mp3", buffer);
AudioFX_Load("music.flac", buffer);
AudioFX_Load("sfx.wav", buffer);
```

### Professional Effects Processing
Apply studio-quality effects without understanding complex DSP algorithms:

```cpp
// Add warmth with compression and reverb
AudioFX_Compress(buffer, AudioFXCompressorParams::Vocal());
AudioFX_Reverb(buffer, AudioFXReverbParams::Room());

// Master with EQ and limiting
AudioFX_ApplyEQ(buffer, eqParams);
AudioFX_Limit(buffer, -1.0f, 100.0f);
```

### Intelligent Audio Analysis
Extract meaningful data from audio for visualization, synchronization, or content analysis:

```cpp
// Detect tempo for beat-synced effects
AudioFXBPMData bpm;
AudioFX_DetectBPM(buffer, bpm);

// Get spectrum for visualizers
AudioFXSpectrumData spectrum;
AudioFX_GetSpectrum(buffer, spectrum, position, 2048);
```

### Noise Reduction & Cleanup
Clean up recordings with professional noise reduction tools:

```cpp
// Learn noise profile from silent section
AudioFX_LearnNoiseProfile(buffer, 0.0, 0.5, noiseProfile);

// Apply noise reduction
AudioFX_NoiseReduction(buffer, noiseProfile, -20.0f, 6.0f);

// Remove clicks, pops, and hum
AudioFX_RemoveClicks(buffer, 0.7f);
AudioFX_RemoveHum(buffer, 60.0f, 4);
```

### Effect Chains & Batch Processing
Build reusable processing pipelines for consistent results:

```cpp
// Create effect chain
AudioFXEffectChain chain;
chain.AddEffect([](AudioFXBuffer& b) { return AudioFX_HighPass(b, 80.0f); });
chain.AddEffect([](AudioFXBuffer& b) { return AudioFX_Compress(b, params); });
chain.AddEffect([](AudioFXBuffer& b) { return AudioFX_Normalize(b, -3.0f); });

// Apply to any buffer
chain.Process(buffer);
```

---

## Supported Operations

**Effects:** Reverb, Delay, Chorus, Flanger, Phaser, Tremolo, Vibrato, Distortion, Saturation, BitCrush  
**Dynamics:** Compressor, Limiter, Gate, Expander, AGC, Multiband Compression, Maximizer  
**Filters:** Low/High/Band Pass, Notch, Parametric EQ, Graphic EQ, Shelving, De-Esser  
**Analysis:** Waveform, Spectrum, Spectrogram, BPM, Loudness (LUFS), Silence Detection  
**Pitch/Time:** Pitch Shift, Time Stretch, Vocoder, Auto-Tune, Harmony Generator  
**Synthesis:** Tone Generator, Sweep, Noise (White/Pink/Brown), ADSR, DTMF  
**Formats:** WAV, MP3, FLAC, OGG, AAC, AIFF, Opus (with optional FFmpeg)

*...140+ functions covering every audio processing need*

---

## Why AudioFX?

### For Developers
- **Reduce Development Time** - Stop implementing DSP algorithms from scratch
- **Single Dependency** - One module handles all audio operations consistently
- **Bulletproof Error Handling** - Comprehensive result codes and recovery
- **Memory Efficient** - Optimized buffers with smart resource management

### For Applications
- **Professional Quality** - Studio-grade algorithms used in real productions
- **Better Performance** - SIMD-optimized processing for real-time use
- **Future-Proof** - Easy to add new effects and formats as needs evolve
- **Production Ready** - Extensively tested code with no external dependencies

### For Users
- **Seamless Experience** - Process any audio format without manual conversion
- **Professional Results** - Same quality as expensive audio software
- **Fast** - Optimized performance for smooth operation
- **Reliable** - Robust error handling ensures stability

---

## Perfect For

✅ **Music Software** - DAWs, DJ tools, music production apps  
✅ **Podcasting** - Recording software, audio cleanup tools  
✅ **Video Editors** - Audio tracks, sound design, mixing  
✅ **Game Engines** - Sound effects, dynamic audio, music systems  
✅ **Voice Applications** - Speech processing, voice changers, transcription prep  
✅ **Media Players** - EQ, visualization, format conversion

---

## Get Started

AudioFX integrates seamlessly into any C++ application:

```cpp
// Initialize once
AudioFX_Initialize();

// Load and process
AudioFXBuffer buffer;
AudioFX_Load("input.wav", buffer);
AudioFX_Normalize(buffer, -3.0f);
AudioFX_Reverb(buffer, AudioFXReverbParams::Room());
AudioFX_Save(buffer, "output.wav");

// Clean shutdown
AudioFX_Shutdown();
```

---

## Technical Specifications

| Specification | Details |
|---------------|---------|
| Language | C++20 |
| Platforms | Windows, Linux, macOS |
| Build System | CMake |
| Dependencies | None (optional: FFmpeg, libsndfile, FFTW3) |
| Sample Formats | 8/16/24/32-bit, Float32/64 |
| Sample Rates | 8kHz - 384kHz |
| Channels | Mono to 7.1 Surround |
| Functions | 140+ |
| Lines of Code | 7,200+ |

---

## The Bottom Line

**Stop writing DSP code. Start shipping features.**

AudioFX handles the complexity of audio processing so you can focus on what makes your application unique. With comprehensive effects, intelligent analysis, and a simple API, AudioFX is the complete audio solution for modern C++ applications.

**Professional. Comprehensive. Simple.**

*AudioFX - Part of the UltraCanvas Framework*
