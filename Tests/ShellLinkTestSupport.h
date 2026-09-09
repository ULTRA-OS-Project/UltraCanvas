// Tests/ShellLinkTestSupport.h
// A Windows shell link (.lnk), built byte by byte, for the tests that read
// one back: the link reader itself (ShellLinkTest) and the file display that
// lists shortcuts (FilerShortcutEntryTest). Checked-in binaries would say
// nothing about what each test asserts; this says all of it.
// Version: 1.0.0
// Last Modified: 2026-09-05
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ShellLinkTestSupport {

struct Blob {
    std::vector<uint8_t> bytes;

    void U8(uint8_t v) { bytes.push_back(v); }
    void U16(uint16_t v) { U8(v & 0xFF); U8((v >> 8) & 0xFF); }
    void U32(uint32_t v) { U16(v & 0xFFFF); U16((v >> 16) & 0xFFFF); }
    void U64(uint64_t v) { U32(static_cast<uint32_t>(v));
                           U32(static_cast<uint32_t>(v >> 32)); }
    void Raw(const std::vector<uint8_t>& v) {
        bytes.insert(bytes.end(), v.begin(), v.end());
    }
    void Ansi(const std::string& s, bool terminate = true) {
        for (char c : s) U8(static_cast<uint8_t>(c));
        if (terminate) U8(0);
    }
    void Utf16(const std::string& s, bool terminate = false) {
        for (char c : s) U16(static_cast<uint8_t>(c));
        if (terminate) U16(0);
    }
    size_t Size() const { return bytes.size(); }
};

enum Flags : uint32_t {
    HasLinkInfo     = 0x00000002,
    HasName         = 0x00000004,
    HasRelativePath = 0x00000008,
    HasWorkingDir   = 0x00000010,
    HasArguments    = 0x00000020,
    HasIconLocation = 0x00000040,
    IsUnicode       = 0x00000080,
};

struct LinkSpec {
    uint32_t flags = IsUnicode;
    uint32_t fileAttributes = 0;
    int32_t iconIndex = 0;
    std::string localBasePath;    // LinkInfo target ("C:\\...")
    std::string description;
    std::string relativePath;
    std::string workingDirectory;
    std::string arguments;
    std::string iconLocation;
    std::string environmentTarget; // EnvironmentVariableDataBlock
    std::string environmentIcon;   // IconEnvironmentDataBlock
};

inline void AppendHeader(Blob& out, const LinkSpec& spec) {
    out.U32(0x0000004C);
    const std::vector<uint8_t> clsid = {0x01, 0x14, 0x02, 0x00, 0x00, 0x00,
                                        0x00, 0x00, 0xC0, 0x00, 0x00, 0x00,
                                        0x00, 0x00, 0x00, 0x46};
    out.Raw(clsid);
    out.U32(spec.flags);
    out.U32(spec.fileAttributes);
    out.U64(0);                                  // creation time
    out.U64(0);                                  // access time
    out.U64(132000000000000000ULL);              // write time
    out.U32(4096);                               // target size
    out.U32(static_cast<uint32_t>(spec.iconIndex));
    out.U32(1);                                  // show command
    out.U16(0);                                  // hotkey
    out.U16(0);                                  // reserved
    out.U32(0);                                  // reserved2
    out.U32(0);                                  // reserved3
}

// A LinkInfo block holding just a local base path and an empty suffix -
// the shape of every shortcut to a file on a local disk.
inline void AppendLinkInfo(Blob& out, const std::string& localBasePath) {
    const uint32_t headerSize = 0x1C;
    const uint32_t basePathOffset = headerSize;
    const uint32_t suffixOffset =
            basePathOffset + static_cast<uint32_t>(localBasePath.size()) + 1;
    const uint32_t total = suffixOffset + 1;
    out.U32(total);
    out.U32(headerSize);
    out.U32(0x1);                 // VolumeIDAndLocalBasePath
    out.U32(0);                   // VolumeIDOffset (unused by the reader)
    out.U32(basePathOffset);
    out.U32(0);                   // CommonNetworkRelativeLinkOffset
    out.U32(suffixOffset);
    out.Ansi(localBasePath);
    out.U8(0);                    // empty common path suffix
}

inline void AppendString(Blob& out, const std::string& value, bool unicode) {
    out.U16(static_cast<uint16_t>(value.size()));
    if (unicode) out.Utf16(value);
    else out.Ansi(value, false);
}

// One of the two 0x314-byte blocks that carry a path in its un-expanded
// form. Both have the same layout, only the signature differs.
inline void AppendEnvironmentBlock(Blob& out, uint32_t signature,
                            const std::string& value) {
    out.U32(0x00000314);
    out.U32(signature);
    Blob ansi;
    ansi.Ansi(value);
    ansi.bytes.resize(260, 0);
    out.Raw(ansi.bytes);
    Blob wide;
    wide.Utf16(value, true);
    wide.bytes.resize(520, 0);
    out.Raw(wide.bytes);
}

inline std::vector<uint8_t> BuildShellLink(const LinkSpec& spec) {
    Blob out;
    AppendHeader(out, spec);
    if (spec.flags & HasLinkInfo) AppendLinkInfo(out, spec.localBasePath);
    const bool unicode = (spec.flags & IsUnicode) != 0;
    if (spec.flags & HasName) AppendString(out, spec.description, unicode);
    if (spec.flags & HasRelativePath) AppendString(out, spec.relativePath, unicode);
    if (spec.flags & HasWorkingDir) AppendString(out, spec.workingDirectory, unicode);
    if (spec.flags & HasArguments) AppendString(out, spec.arguments, unicode);
    if (spec.flags & HasIconLocation) AppendString(out, spec.iconLocation, unicode);
    if (!spec.environmentTarget.empty())
        AppendEnvironmentBlock(out, 0xA0000001, spec.environmentTarget);
    if (!spec.environmentIcon.empty())
        AppendEnvironmentBlock(out, 0xA0000007, spec.environmentIcon);
    out.U32(0);   // terminal block
    return out.bytes;
}

} // namespace ShellLinkTestSupport
