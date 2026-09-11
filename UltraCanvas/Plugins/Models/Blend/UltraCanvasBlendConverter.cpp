// Plugins/Models/Blend/UltraCanvasBlendConverter.cpp
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "Models/Blend/UltraCanvasBlendConverter.h"

#include <fstream>
#include <iterator>

namespace UltraCanvas {
namespace ModelConverter {

namespace {

// Report what the file is, then decline. The summary is the whole value of
// this converter, so it goes through the warning callback every time.
std::shared_ptr<ModelStorage::ModelDocument> Decline(const BlendFileInfo& info,
                                                     const ConversionOptions& options) {
    options.Warn("Blender: " + info.Summary());
    return nullptr;
}

} // namespace

std::shared_ptr<ModelStorage::ModelDocument> BlendConverter::Import(
        const std::string& filename, const ConversionOptions& options) {
    return Decline(ReadBlendFileInfo(filename), options);
}

std::shared_ptr<ModelStorage::ModelDocument> BlendConverter::ImportFromMemory(
        const std::vector<uint8_t>& data, const ConversionOptions& options) {
    return Decline(ReadBlendFileInfo(data), options);
}

std::shared_ptr<ModelStorage::ModelDocument> BlendConverter::ImportFromStream(
        std::istream& stream, const ConversionOptions& options) {
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(stream)),
                                    std::istreambuf_iterator<char>());
    return ImportFromMemory(data, options);
}

bool BlendConverter::ValidateData(const std::vector<uint8_t>& data) const {
    return LooksLikeBlendFile(data);
}

bool BlendConverter::ValidateFile(const std::string& filename) const {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;
    std::vector<uint8_t> head(16);
    file.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<size_t>(std::max<std::streamsize>(0, file.gcount())));
    return LooksLikeBlendFile(head);
}

BlendFileInfo BlendConverter::Inspect(const std::string& filename) {
    return ReadBlendFileInfo(filename);
}

} // namespace ModelConverter
} // namespace UltraCanvas
