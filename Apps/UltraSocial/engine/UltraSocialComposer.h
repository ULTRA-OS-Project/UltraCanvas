// Apps/UltraSocial/engine/UltraSocialComposer.h
// Compose once, adapt per network: turns the master PostDraft into an
// AdaptedPost for one network's capabilities (truncation with an ellipsis at
// a word boundary, media-count trimming) and validates adapted posts before
// publish. Text limits count Unicode code points — an approximation of the
// grapheme counting some networks (Bluesky) apply; the divergence is rare
// (ZWJ emoji sequences, combining marks) and errs on the safe side only
// when the composer truncates.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraSocialTypes.h"

#include <string>
#include <vector>

namespace UltraSocial {

// Unicode code points in a UTF-8 string (continuation bytes not counted).
int CountTextPoints(const std::string& utf8);

// Truncates to at most maxPoints code points, appending "…" (which counts
// toward the limit) and preferring the last word boundary in the kept range.
// Returns the input unchanged when it already fits.
std::string TruncateToPoints(const std::string& utf8, int maxPoints);

// One master draft -> a network-ready variant. Anything lossy (truncated
// text, dropped media beyond the network's count limit) is reported in
// outWarnings so the compose UI can show what will happen before posting.
AdaptedPost AdaptDraft(const PostDraft& draft,
                       SocialNetwork network,
                       const SocialCapabilities& caps,
                       std::vector<std::string>& outWarnings);

// Hard problems that must block publishing (over-limit text the caller
// chose to keep, missing/oversized media files). Empty = publishable.
std::vector<std::string> ValidateAdaptedPost(const AdaptedPost& post,
                                             const SocialCapabilities& caps);

} // namespace UltraSocial
