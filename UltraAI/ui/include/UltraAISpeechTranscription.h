// UltraAI/ui/include/UltraAISpeechTranscription.h
// UltraAI as UltraCanvas's speech transcriber: the words spoken in a sound
// file or a video come from an UltraAI speech-to-text provider -
// UltraCanvasFileLoader::ExtractText, and so UltraClipboard's Extract text on
// copied recordings and videos.
//
// "Available" is UltraAI's default route (UltraAIRouting.h), as for text
// recognition (UltraAITextRecognition.h): the provider chosen for
// "speechtotext", then the ones on this computer, a cloud service only when
// chosen or cloud fallback is allowed - never the in-process "mock". The
// first that constructs transcribes. The choice is made at every file, so a
// provider set up while the application runs is used from the next one on.
//
// What is sent: a sound file of a type the provider takes
// (STTProviderCapabilities::supportedEncodings), within its size limit, goes
// as it is. Anything else - every video, another sound type, a file over
// the limit - has its sound track taken out first (VideoFX, FFmpeg) as a
// mono 16 kHz MP3 at 64 kbit/s, about 8 KB a second, so 25 MB holds some 50
// minutes. A build without VideoFX transcribes the sound files the provider
// takes and says so for the rest.
//
// When no provider is there, the transcriber installed before this one
// (UltraCanvasFileLoader::GetSpeechTranscriber), if any, transcribes - so
// install an application's own first.
//
// doc-check: example
//   UltraAI::RegisterUltraAISpeechTranscription();
//   std::string provider;
//   if (UltraAI::FindSpeechProvider(provider)) { /* provider transcribes */ }
//
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraAI Module
#pragma once

#include <string>

namespace UltraAI {

// True, with the provider's id in `providerId`, when a provider on the
// default route transcribes (see the top of this file).
bool FindSpeechProvider(std::string& providerId);

// Installs UltraAI as UltraCanvasFileLoader's speech transcriber, in front
// of the one installed now. Call once, at start-up. The transcriber runs on
// the thread that calls ExtractText.
void RegisterUltraAISpeechTranscription();

// The provider that transcribed the last file on this thread ("" when it was
// the fallback or nothing): for a status line.
std::string LastSpeechTranscriptionProvider();

} // namespace UltraAI
