// Apps/UltraCleaner/album/UltraCleanerImageDescriptor.h
// What UltraCleaner knows about one picture, reduced to a few dozen bytes so
// a whole album can be compared pair by pair without going back to disk.
//
// Four signals, all derived from one small decode:
//
//   pHash   64 bits, the sign pattern of the low-frequency DCT coefficients.
//           Carries composition, and survives rescaling and re-encoding —
//           this is what recognises the same shot at a different size.
//   dHash   64 bits, horizontal brightness gradients. Cheap, kept for
//           reporting and as a tie-breaker.
//   grid    a 4x4 array of mean colours. pHash is blind to colour, and a
//           global histogram is blind to *where* the colour is; the grid
//           keeps enough layout to tell two scenes apart that happen to
//           share a palette.
//   sharp   variance of the Laplacian, used to choose which member of a
//           group to keep rather than to compare.
//
// Scored together, pHash and the grid reject pairs that either alone would
// accept: measured on a 60-photo reference album, pHash could not separate a
// true burst member from an unrelated photo of the same person — both sat 14
// bits away — while the colour grid put them at 100% and 81%.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <string>

namespace UltraCleaner {

// ===== WHAT KIND OF PICTURE =====
// Similarity matching is only meaningful for photographs. Screenshots share
// their whole layout with every other screenshot of the same application and
// differ only in text, which these descriptors deliberately discard —
// measured on a real set, two unrelated bank transfers scored 8 bits apart
// while the same transfer captured twice, one of them cropped, scored 36.
// The ranking inverts, so screenshots are classified out rather than
// mis-ranked. Exact-duplicate detection still applies to them: that is a
// fact about bytes, not a judgement about content.
enum class PictureKind {
    Photograph,
    Screenshot,
    Unknown
};

const char* PictureKindName(PictureKind kind);

// ===== SIMILARITY LEVEL =====
// How far apart two pictures may be and still count as the same thing. The
// thresholds are measured rather than guessed — see
// Docs/UltraCleaner/README.md for the album they were measured on.
enum class SimilarityLevel {
    Duplicates,   // the same photo: copies, rescales, small local edits
    Moments,      // the same moment: a burst of shots            (default)
    Scenes        // the same scene or subject, loosely
};

struct LevelThresholds {
    int maxPHashDistance = 14;
    double minGridAgreement = 0.85;
};

LevelThresholds ThresholdsFor(SimilarityLevel level);
const char* SimilarityLevelName(SimilarityLevel level);
const char* SimilarityLevelDescription(SimilarityLevel level);

// ===== THE DESCRIPTOR =====
constexpr int kColourGridSide = 4;
constexpr int kColourGridBytes = kColourGridSide * kColourGridSide * 3;

struct ImageDescriptor {
    std::string path;
    uint64_t fileSize = 0;
    int width = 0;
    int height = 0;

    uint64_t pHash = 0;
    uint64_t dHash = 0;
    uint8_t colourGrid[kColourGridBytes] = {};
    double sharpness = 0.0;

    // EXIF DateTimeOriginal as seconds since the epoch, or 0 when the picture
    // carries no usable capture time. Plenty of real albums have none — every
    // photo in the reference set reported either no EXIF at all or a zeroed
    // "0000:00:00 00:00:00" — so nothing may *depend* on this being present.
    int64_t capturedAt = 0;
    std::string capturedAtText;

    PictureKind kind = PictureKind::Unknown;
    bool valid = false;
};

// Reads `path` once, at a small size, and fills in every field. Returns a
// descriptor with valid == false when the file is not a readable image.
ImageDescriptor DescribeImage(const std::string& path);

// ===== COMPARISON =====
int HammingDistance(uint64_t a, uint64_t b);

// Fraction of the 16 grid cells whose colour agrees within `tolerance`.
// A fraction rather than a summed distance on purpose: in a burst the
// background often changes behind a fixed subject — people walking past —
// which spoils three or four cells while the rest hold. A sum would be
// dominated by exactly the part that legitimately moved, and would reject a
// true match.
double ColourGridAgreement(const ImageDescriptor& a, const ImageDescriptor& b,
                           int tolerance = 40);

// True when the pair is close enough at this level. Both signals must agree.
bool AreSimilar(const ImageDescriptor& a, const ImageDescriptor& b,
                const LevelThresholds& thresholds);

// ===== HELPERS =====
// "2026:08:22 15:04:43" -> seconds since the epoch; 0 when unparseable, or
// when the field is the zeroed placeholder some software writes.
int64_t ParseExifTimestamp(const std::string& exif);

// Whether a path looks like a picture UltraCleaner should describe at all.
bool HasImageExtension(const std::string& path);

// Whether a picture looks like a screen capture rather than a photograph.
// Exposed for the tests, which check the classifier without decoding.
PictureKind ClassifyPicture(const std::string& path, int width, int height,
                            const std::string& cameraMake,
                            const std::string& cameraModel);

} // namespace UltraCleaner
