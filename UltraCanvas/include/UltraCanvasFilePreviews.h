// include/UltraCanvasFilePreviews.h
// What a file looks like, from whoever reads its format: a registry of
// preview providers.
//
// The Filer's tiles, its preview pane and RenderFileThumbnail used to call
// every format they preview directly - the PDF engine for a first page, the
// e-book engines for a cover, the video backend for a poster frame, the Word
// and PowerPoint readers for a page of text. That made the file display
// depend on each of them, and a format could not preview without the Filer
// knowing it by name.
//
// This inverts that, the way UltraCanvasVectorPreview.h and
// UltraCanvasModelPreview.h already do for one family each: a format
// registers a provider for its extensions, and the caller asks the registry.
// Any number of providers; each answers for the extensions it lists, the
// latest registration of an extension winning. Two things can be asked:
//
//   a thumbnail         a picture of the file at a requested size (a PDF's
//                       first page, a book's cover, a video's poster frame)
//   a preview document  the start of the file's text as a rich document
//                       (a Word document's paragraphs, a deck's slide
//                       outline), which the caller lays out as it likes
//
// The formats compiled into the framework register themselves the first time
// anything is asked (RegisterBuiltinFilePreviewProviders, in its own file), so
// a caller never has to; a plugin calls RegisterFilePreviewProvider from its
// init. Every function here is safe to call from several threads at once -
// the Filer's thumbnail workers do - and a provider is called outside the
// registry's lock, so it must itself be safe to run concurrently (each call
// opening its own document, as all the built-in ones do).
//
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"   // ImageFitMode
#include "UltraCanvasImage.h"         // UCPixmap

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

class UCRichDocument;

// The size a thumbnail is wanted at: `width` x `height` logical pixels, drawn
// at `scale` device pixels per logical pixel, fitted as `fit` says (a provider
// that has no aspect of its own to keep, a page, may ignore it).
struct FileThumbnailRequest {
    int width = 0;
    int height = 0;
    ImageFitMode fit = ImageFitMode::Contain;
    float scale = 1.0f;
};

// What a provider answers. Every member but `name` and one of the two
// extension lists is optional.
struct FilePreviewProvider {
    // For logs and for replacing a provider: a registration with the name of
    // one already registered takes its place.
    std::string name;

    // Extensions RenderThumbnail draws, lowercase and without a dot.
    std::vector<std::string> thumbnailExtensions;
    // Extensions ReadDocument reads, lowercase and without a dot.
    std::vector<std::string> documentExtensions;

    // Whether the provider can answer at all in this process (a backend it
    // needs may be missing at run time). Absent: always.
    std::function<bool()> Available;

    // The file's picture, or null when this file cannot be drawn. Never
    // throws. `path` is UTF-8.
    std::function<std::shared_ptr<UCPixmap>(const std::string& path,
                                            const FileThumbnailRequest& request)> RenderThumbnail;

    // The start of the file's text into `document`. False when the file
    // cannot be read (a damaged or password-protected one among them), so
    // the caller keeps its placeholder rather than show an empty page. A
    // provider may read the whole document; the caller takes what it needs.
    std::function<bool(const std::string& path, UCRichDocument& document)> ReadDocument;
};

// Adds a provider, or replaces the one registered under its name. A provider
// without a name, or with neither a RenderThumbnail and thumbnail extensions
// nor a ReadDocument and document extensions, is ignored.
void RegisterFilePreviewProvider(FilePreviewProvider provider);

// Registers the providers of the formats compiled into the framework: the
// PDF page (with the PDF plugin), e-book covers, video poster frames, Word
// documents and PowerPoint decks. Called once by the queries below, so no
// application has to; calling it again does nothing.
void RegisterBuiltinFilePreviewProviders();

// ===== WHAT A CALLER ASKS =====
// `extensionOrPath` may be an extension (with or without its dot, any case)
// or a whole path, whose last extension is used.

// Whether a provider available now draws thumbnails of this kind of file.
bool CanRenderFileThumbnail(const std::string& extensionOrPath);
// The file's picture from the provider for its extension, or null.
std::shared_ptr<UCPixmap> RenderFileThumbnailFromProvider(const std::string& path,
                                                          const FileThumbnailRequest& request);
// The same with the provider of another extension: a file whose extension
// lies about its content (an Illustrator .ai that is a PDF from its first
// byte, drawn by the "pdf" provider).
std::shared_ptr<UCPixmap> RenderFileThumbnailAs(const std::string& path,
                                                const std::string& extension,
                                                const FileThumbnailRequest& request);

// Whether a provider available now reads this kind of file into a preview
// document.
bool CanReadPreviewDocument(const std::string& extensionOrPath);
// The start of the file's text, from the provider for its extension. False
// when there is none or it cannot read the file.
bool ReadPreviewDocument(const std::string& path, UCRichDocument& document);

// Every extension some provider draws a thumbnail of / reads a preview
// document of, lowercase and without a dot, sorted. Providers not available
// now are left out.
std::vector<std::string> FileThumbnailExtensions();
std::vector<std::string> PreviewDocumentExtensions();

} // namespace UltraCanvas
