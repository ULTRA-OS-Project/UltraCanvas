// core/IODeviceManager/UltraCanvasIODevicePrinterJobSource.cpp
// What a print job holds, and which page source can lay it out.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "IODeviceManager/UltraCanvasIODevicePrinterJobSource.h"

#include "UltraCanvasRasterDocument.h"
#include "UltraCanvasRasterLayer.h"
#include "UltraCanvasUtils.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace UltraCanvas {

namespace {

bool StartsWith(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() &&
           text.compare(0, prefix.size(), prefix) == 0;
}

std::string LowerExtension(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return std::string();

    // A dot in a directory name is not an extension: "/home/a.b/report" has
    // none, and treating "b/report" as one would refuse a printable file.
    const size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos && dot < slash) return std::string();

    std::string extension = path.substr(dot + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

// Uses the declared MIME type when there is one and the file extension when
// there is not, because PrintFile() sets no MIME type and asking every caller
// to is how a print button ends up refusing valid files.
std::string ResolveType(const IOPrintJob& job) {
    if (!job.mimeType.empty()) {
        if (StartsWith(job.mimeType, "image/")) return "image";
        if (StartsWith(job.mimeType, "text/plain")) return "text";
        return job.mimeType;
    }

    const std::string extension = LowerExtension(job.filePath);
    if (extension.empty()) return std::string();
    if (extension == "txt" || extension == "log" || extension == "md") {
        return "text";
    }
    if (extension == "png" || extension == "jpg" || extension == "jpeg" ||
        extension == "bmp" || extension == "gif" || extension == "tif" ||
        extension == "tiff" || extension == "webp" || extension == "qoi") {
        return "image";
    }
    return extension;
}

IODeviceResult MakeTextSource(const IOPrintJob& job,
                              IPrintPageSourcePtr& outPages,
                              std::string& outContentType) {
    std::string text;
    if (!job.data.empty()) {
        text.assign(job.data.begin(), job.data.end());
    } else {
        // PathFromUtf8 rather than the path as bytes: on Windows a file name
        // that is not representable in the active code page would otherwise
        // fail to open, and a user's own documents are exactly where such a
        // name turns up.
        std::ifstream file(PathFromUtf8(job.filePath), std::ios::binary);
        if (!file) {
            return IODeviceResult::Error(
                IODeviceResultCode::IOError,
                "Could not open '" + job.filePath + "' to print it");
        }
        std::ostringstream contents;
        contents << file.rdbuf();
        text = contents.str();
    }

    outPages = std::make_shared<TextPageSource>(std::move(text));
    outContentType = "text/plain";
    return IODeviceResult::Ok();
}

IODeviceResult MakeImageSource(const IOPrintJob& job,
                               IPrintPageSourcePtr& outPages,
                               std::string& outContentType) {
    if (job.filePath.empty()) {
        // Decoding from memory would mean writing the bytes out and reading
        // them back, since the document loader is path-based. Saying so beats
        // doing it silently behind the caller's back.
        return IODeviceResult::Error(
            IODeviceResultCode::NotImplemented,
            "Printing an image from memory is not wired up yet; print it "
            "from a file");
    }

    UCRasterDocument document;
    std::string error;
    if (!document.LoadFromFile(job.filePath, error)) {
        return IODeviceResult::Error(
            IODeviceResultCode::MediaError,
            "Could not decode '" + job.filePath + "': " +
                (error.empty() ? std::string("unsupported image") : error));
    }

    std::shared_ptr<UCRasterLayer> layer = document.GetLayer(0);
    if (!layer || !layer->IsValid()) {
        return IODeviceResult::Error(
            IODeviceResultCode::MediaError,
            "'" + job.filePath + "' decoded to nothing printable");
    }

    outPages = std::make_shared<ImagePageSource>(
        layer->Data(), layer->GetWidth(), layer->GetHeight());
    outContentType = "image/x-raster";
    return IODeviceResult::Ok();
}

}  // namespace

IODeviceResult MakePageSourceForJob(const IOPrintJob& job,
                                    IPrintPageSourcePtr& outPages,
                                    std::string& outContentType) {
    const std::string type = ResolveType(job);
    if (type.empty()) {
        return IODeviceResult::Error(
            IODeviceResultCode::InvalidArgument,
            "The job says nothing about its type, and its name carries no "
            "extension to infer one from");
    }
    if (type == "text") return MakeTextSource(job, outPages, outContentType);
    if (type == "image") return MakeImageSource(job, outPages, outContentType);

    return IODeviceResult::Error(
        IODeviceResultCode::NotSupported,
        "Pages can be drawn for images and plain text; '" + type +
            "' needs a renderer that can paginate it. Printing it as a "
            "device-native stream is unaffected.");
}

}  // namespace UltraCanvas
