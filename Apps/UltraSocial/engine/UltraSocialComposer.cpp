// Apps/UltraSocial/engine/UltraSocialComposer.cpp
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSocialComposer.h"

#include <filesystem>
#include <system_error>

namespace UltraSocial {

namespace {

constexpr const char* kEllipsis = "\xE2\x80\xA6";  // U+2026

bool IsContinuationByte(unsigned char c) { return (c & 0xC0) == 0x80; }

// Byte offset just past the maxPoints-th code point (or npos if the whole
// string fits).
std::size_t OffsetAfterPoints(const std::string& s, int maxPoints) {
    int points = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (IsContinuationByte(static_cast<unsigned char>(s[i]))) continue;
        if (points == maxPoints) return i;
        ++points;
    }
    return std::string::npos;
}

} // namespace

int CountTextPoints(const std::string& utf8) {
    int points = 0;
    for (unsigned char c : utf8) {
        if (!IsContinuationByte(c)) ++points;
    }
    return points;
}

std::string TruncateToPoints(const std::string& utf8, int maxPoints) {
    if (maxPoints <= 0) return utf8;
    if (OffsetAfterPoints(utf8, maxPoints) == std::string::npos) return utf8;

    // Keep maxPoints-1 code points, then the ellipsis takes the last slot.
    std::size_t cut = OffsetAfterPoints(utf8, maxPoints - 1);
    if (cut == std::string::npos) cut = utf8.size();
    std::string kept = utf8.substr(0, cut);

    // Prefer breaking at the last space, unless it sits so early the post
    // would lose most of its room.
    std::size_t space = kept.find_last_of(" \t\n");
    if (space != std::string::npos && space > kept.size() / 2) {
        kept.resize(space);
    }
    while (!kept.empty() &&
           (kept.back() == ' ' || kept.back() == '\t' || kept.back() == '\n')) {
        kept.pop_back();
    }
    return kept + kEllipsis;
}

AdaptedPost AdaptDraft(const PostDraft& draft,
                       SocialNetwork network,
                       const SocialCapabilities& caps,
                       std::vector<std::string>& outWarnings) {
    outWarnings.clear();

    AdaptedPost post;
    post.network    = network;
    post.media      = draft.media;
    post.visibility = caps.supportsVisibility ? draft.visibility : std::string();

    // Networks that cap a media caption tighter than a bare text post
    // (Telegram) get the caption limit whenever media is attached.
    int limit = caps.maxTextChars;
    if (!draft.media.empty() && caps.maxMediaCaptionChars > 0) {
        limit = caps.maxMediaCaptionChars;
    }

    post.text = draft.text;
    if (limit > 0 && CountTextPoints(draft.text) > limit) {
        post.text = TruncateToPoints(draft.text, limit);
        outWarnings.push_back(
            ToString(network) + ": text shortened to " + std::to_string(limit) +
            " characters");
    }

    if (caps.maxImages == 0 && !post.media.empty()) {
        post.media.clear();
        outWarnings.push_back(
            ToString(network) + ": media is not supported and will be skipped");
    } else if (caps.maxImages > 0 &&
               static_cast<int>(post.media.size()) > caps.maxImages) {
        post.media.resize(static_cast<std::size_t>(caps.maxImages));
        outWarnings.push_back(
            ToString(network) + ": only the first " +
            std::to_string(caps.maxImages) + " media attachment" +
            (caps.maxImages == 1 ? "" : "s") + " will be posted");
    }

    if (caps.maxAltTextChars > 0) {
        for (auto& m : post.media) {
            if (CountTextPoints(m.altText) > caps.maxAltTextChars) {
                m.altText = TruncateToPoints(m.altText, caps.maxAltTextChars);
                outWarnings.push_back(
                    ToString(network) + ": alt text shortened to " +
                    std::to_string(caps.maxAltTextChars) + " characters");
            }
        }
    }
    return post;
}

std::vector<std::string> ValidateAdaptedPost(const AdaptedPost& post,
                                             const SocialCapabilities& caps) {
    std::vector<std::string> problems;

    if (post.text.empty() && post.media.empty()) {
        problems.push_back("post is empty");
    }

    int limit = caps.maxTextChars;
    if (!post.media.empty() && caps.maxMediaCaptionChars > 0) {
        limit = caps.maxMediaCaptionChars;
    }
    if (limit > 0 && CountTextPoints(post.text) > limit) {
        problems.push_back("text exceeds the " + std::to_string(limit) +
                           "-character limit");
    }
    if (caps.maxImages == 0 && !post.media.empty()) {
        problems.push_back("media is not supported on this network");
    } else if (caps.maxImages > 0 &&
               static_cast<int>(post.media.size()) > caps.maxImages) {
        problems.push_back("more than " + std::to_string(caps.maxImages) +
                           " media attachments");
    }
    for (const auto& m : post.media) {
        std::error_code ec;
        auto size = std::filesystem::file_size(m.filePath, ec);
        if (ec) {
            problems.push_back("media file not readable: " + m.filePath);
            continue;
        }
        if (caps.maxImageBytes > 0 &&
            static_cast<int64_t>(size) > caps.maxImageBytes) {
            problems.push_back("media file over " +
                               std::to_string(caps.maxImageBytes) +
                               " bytes: " + m.filePath);
        }
    }
    return problems;
}

} // namespace UltraSocial
