// PixelFX/include/PixelFXMetadataDecode.h
// Decoders for the metadata blocks libvips hands over as raw blobs: IPTC-IIM
// ("iptc-data") and XMP ("xmp-data"). libvips parses EXIF into one field per
// tag but leaves these two as bytes, so without this a caption, keywords or a
// rating never reach PixelFX::Header::ReadMetadata().
// Plain C++ and tinyxml2 - no libvips - so it can be tested on its own.
// Version: 1.0.0
// Last Modified: 2026-09-23
// Author: UltraCanvas Framework
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace PixelFX {
namespace Header {

    // One decoded item: the tag name as a person reads it, and its value as
    // UTF-8 text. Repeated tags (IPTC keywords, an XMP bag) come back as one
    // item with the values joined by ", ".
    using DecodedTag = std::pair<std::string, std::string>;

    // IPTC-IIM datasets, as a JPEG's APP13 block ("Photoshop 3.0" with 8BIM
    // resources, resource 0x0404 holding the IIM) or as bare IIM records
    // (TIFF tag 33723). Record 2 datasets get their IIM names ("Keywords",
    // "City", "Caption/Abstract", ...); unknown ones are named "2:<number>".
    // Latin-1 text is converted to UTF-8 unless the block declares UTF-8.
    // Returns nothing for data that is neither form.
    std::vector<DecodedTag> DecodeIPTC(const void* data, std::size_t length);

    // XMP packet (RDF/XML). Every property of every rdf:Description, as
    // "prefix:Name" - written as an attribute or as an element, simple,
    // language alternative (x-default first), or rdf:Seq / rdf:Bag. Fields of
    // a structure are named "prefix:Struct/prefix:Field". Returns nothing for
    // a packet that is not well-formed XML.
    std::vector<DecodedTag> DecodeXMP(const void* data, std::size_t length);

} // namespace Header
} // namespace PixelFX
