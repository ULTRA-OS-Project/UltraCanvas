// Apps/UltraVideo/UltraVideoFormats.h
// The files UltraVideo takes and makes: FileLoader's lists, never its own
// (AGENTS.md: every file a user loads or saves goes through
// UltraCanvasFileLoader).
//
// The engine is VideoFX on FFmpeg, not the framework's platform video
// backend, so it reads more than the codec registry knows on its own (on
// macOS the registry has MP4, MOV and 3GP; FFmpeg reads MKV, WebM, AVI ...
// too). RegisterEngineFormats tells the registry - the framework's hook for
// an application that brings its own codec - and FileLoader's Video and
// Audio lists then hold them. An import offers FileLoader's films, pictures
// and sound that the engine reads: a picture type FileLoader reads and
// the engine does not (a camera's raw file; HEIC, whose tile grid it does
// not put together yet) is left out rather than offered and refused.
// Version: 0.1.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasFileLoader.h"

#include <string>
#include <vector>

namespace UltraVideo {

// Once, at start-up, after the application is initialised: the engine's
// video and sound files join the codec registry where they add something
// (a type it did not know, or reading / writing it could not do).
void RegisterEngineFormats();

// The extensions an import offers - FileLoader's, of the kinds asked for,
// that the engine reads - lowercase, undotted.
std::vector<std::string> ImportExtensions(bool films, bool pictures, bool sound);

// One Open filter, `label`, with ImportExtensions.
void AddImportTypes(UltraCanvas::FileDialogOptions& opts, const std::string& label,
                    bool films, bool pictures, bool sound);

// A file type's name as FileLoader's inventory has it ("MPEG-4 container",
// "GIF image"); the extension in capitals when it has none.
std::string FormatName(const std::string& extension);

// What a file is, by its name: a picture or sound in the engine's or
// FileLoader's lists (whether or not this build can open it - the bin says
// "unreadable" then, rather than taking a photo for a film).
bool IsPicturePath(const std::string& path);
bool IsSoundPath(const std::string& path);

} // namespace UltraVideo
