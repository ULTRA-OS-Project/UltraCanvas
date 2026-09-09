// Tests/UltraCleaner/test_album.cpp
// The album engine's decision logic: thresholds, the two-signal similarity
// test, screenshot classification, EXIF parsing and keeper selection.
//
// These tests deliberately build descriptors by hand rather than decoding
// files, so the suite stays free of image fixtures and of libvips. Decoding
// itself is exercised end to end against the reference album with
// `UltraCleaner --album` (see Docs/UltraCleaner/README.md).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraCleanerAlbumScanner.h"
#include "UltraCleanerImageDescriptor.h"

#include <cstring>

using namespace UltraCleaner;

namespace {

ImageDescriptor MakePicture(const std::string& path, uint64_t pHash,
                            uint8_t gridValue, int width = 1000,
                            int height = 800) {
    ImageDescriptor picture;
    picture.path = path;
    picture.pHash = pHash;
    picture.dHash = pHash;
    picture.width = width;
    picture.height = height;
    picture.fileSize = static_cast<uint64_t>(width) * height;
    picture.kind = PictureKind::Photograph;
    picture.valid = true;
    std::memset(picture.colourGrid, gridValue, kColourGridBytes);
    return picture;
}

} // namespace

TEST(LevelsGetProgressivelyMoreTolerant) {
    const auto tight = ThresholdsFor(SimilarityLevel::Duplicates);
    const auto middle = ThresholdsFor(SimilarityLevel::Moments);
    const auto wide = ThresholdsFor(SimilarityLevel::Scenes);

    REQUIRE(tight.maxPHashDistance < middle.maxPHashDistance);
    REQUIRE(middle.maxPHashDistance < wide.maxPHashDistance);
    // The colour floor relaxes in step, or the wider level could not group
    // anything the tighter one refused.
    REQUIRE(tight.minGridAgreement >= middle.minGridAgreement);
    REQUIRE(middle.minGridAgreement >= wide.minGridAgreement);
}

TEST(MomentsIsTheDefaultLevel) {
    AlbumScanOptions options;
    REQUIRE_EQ(static_cast<int>(options.level),
               static_cast<int>(SimilarityLevel::Moments));
}

TEST(SimilarityNeedsBothSignals) {
    const auto thresholds = ThresholdsFor(SimilarityLevel::Moments);

    // Same hash, same colour: similar.
    auto a = MakePicture("a.jpg", 0x0F0F0F0F0F0F0F0Full, 100);
    auto b = MakePicture("b.jpg", 0x0F0F0F0F0F0F0F0Full, 100);
    REQUIRE(AreSimilar(a, b, thresholds));

    // Same hash, wildly different colour: rejected by the grid. This is the
    // case that stopped an unrelated photo of the same person joining a burst.
    auto colourClash = MakePicture("c.jpg", 0x0F0F0F0F0F0F0F0Full, 250);
    REQUIRE(AreSimilar(a, colourClash, thresholds) == false);

    // Same colour, unrelated structure: rejected by pHash.
    auto structureClash = MakePicture("d.jpg", 0xFFFFFFFFFFFFFFFFull, 100);
    REQUIRE(AreSimilar(a, structureClash, thresholds) == false);
}

TEST(ScreenshotsAreNeverSimilarToAnything) {
    const auto thresholds = ThresholdsFor(SimilarityLevel::Scenes);
    auto a = MakePicture("one.png", 0x0102030405060708ull, 100);
    auto b = MakePicture("two.png", 0x0102030405060708ull, 100);
    // Identical descriptors — as photographs they would group.
    REQUIRE(AreSimilar(a, b, thresholds));

    a.kind = PictureKind::Screenshot;
    b.kind = PictureKind::Screenshot;
    REQUIRE(AreSimilar(a, b, thresholds) == false);
}

TEST(ColourGridAgreementIsAFractionNotASum) {
    auto a = MakePicture("a.jpg", 0, 100);
    auto b = MakePicture("b.jpg", 0, 100);
    REQUIRE(ColourGridAgreement(a, b) == 1.0);

    // Spoil four of sixteen cells outright, as a changing background does.
    for (int cell = 0; cell < 4; ++cell) {
        b.colourGrid[cell * 3 + 0] = 255;
        b.colourGrid[cell * 3 + 1] = 255;
        b.colourGrid[cell * 3 + 2] = 255;
    }
    // Twelve of sixteen still agree: 75%, not "far away".
    REQUIRE(ColourGridAgreement(a, b) == 0.75);
}

TEST(ExifTimestampsParseAndRejectPlaceholders) {
    REQUIRE(ParseExifTimestamp("2026:08:22 15:04:43") > 0);
    // The zeroed placeholder some software writes is "unknown", not a date.
    REQUIRE_EQ(ParseExifTimestamp("0000:00:00 00:00:00"), static_cast<int64_t>(0));
    REQUIRE_EQ(ParseExifTimestamp(""), static_cast<int64_t>(0));
    REQUIRE_EQ(ParseExifTimestamp("not a date"), static_cast<int64_t>(0));
}

TEST(ScreenshotClassificationUsesNameFormatAndCamera) {
    // A camera stamp settles it whatever the file is called.
    REQUIRE_EQ(static_cast<int>(ClassifyPicture("Screenshot 1.jpg", 1920, 1080,
                                                "Canon", "EOS 5D")),
               static_cast<int>(PictureKind::Photograph));
    // Name alone is enough.
    REQUIRE_EQ(static_cast<int>(ClassifyPicture("Screenshot 2023-03-23.png",
                                                800, 600, "", "")),
               static_cast<int>(PictureKind::Screenshot));
    REQUIRE_EQ(static_cast<int>(ClassifyPicture("Bildschirmfoto 5.png",
                                                800, 600, "", "")),
               static_cast<int>(PictureKind::Screenshot));
    // A JPEG with no camera tag is still treated as a photograph.
    REQUIRE_EQ(static_cast<int>(ClassifyPicture("holiday.jpg", 1600, 1200, "", "")),
               static_cast<int>(PictureKind::Photograph));
}

TEST(KeeperPrefersResolutionOverSharpness) {
    // Downscaling raises Laplacian variance, so a small copy looks "sharper"
    // than the original it came from. Resolution has to win, or the scanner
    // proposes throwing the original away.
    auto original = MakePicture("original.jpg", 0x11ull, 100, 1197, 1662);
    original.sharpness = 4019.0;
    auto webCopy = MakePicture("copy.jpg", 0x11ull, 100, 479, 665);
    webCopy.sharpness = 4156.0;      // higher, and still the wrong one to keep

    AlbumScanOptions options;
    auto report = AlbumScanner::Regroup({ original, webCopy }, options);
    REQUIRE_EQ(report.groups.size(), static_cast<size_t>(1));
    REQUIRE(report.groups[0].Keeper().path == "original.jpg");
}

TEST(KeeperUsesSharpnessBetweenFramesOfEqualSize) {
    auto blurry = MakePicture("blurry.jpg", 0x11ull, 100, 1000, 800);
    blurry.sharpness = 100.0;
    auto sharp = MakePicture("sharp.jpg", 0x11ull, 100, 1000, 800);
    sharp.sharpness = 900.0;

    auto report = AlbumScanner::Regroup({ blurry, sharp }, {});
    REQUIRE_EQ(report.groups.size(), static_cast<size_t>(1));
    REQUIRE(report.groups[0].Keeper().path == "sharp.jpg");
}

TEST(RegroupSeparatesUnrelatedPictures) {
    auto a = MakePicture("a.jpg", 0x0000000000000000ull, 30);
    auto b = MakePicture("b.jpg", 0xFFFFFFFFFFFFFFFFull, 220);
    auto report = AlbumScanner::Regroup({ a, b }, {});
    REQUIRE(report.groups.empty());
    REQUIRE_EQ(report.picturesScanned, static_cast<size_t>(2));
}

TEST(RegroupCountsPhotographsAndScreenshots) {
    auto photo = MakePicture("a.jpg", 1, 30);
    auto shot = MakePicture("b.png", 2, 30);
    shot.kind = PictureKind::Screenshot;
    auto report = AlbumScanner::Regroup({ photo, shot }, {});
    REQUIRE_EQ(report.photographs, static_cast<size_t>(1));
    REQUIRE_EQ(report.screenshots, static_cast<size_t>(1));
}

TEST(TimeGateIgnoresPicturesWithoutACaptureTime) {
    AlbumScanOptions options;
    options.maxSecondsApart = 600;          // ten minutes

    // Alike but not pixel-identical, so the pair reaches the similarity tier
    // where the gate actually applies. (Identical pixels are matched a tier
    // earlier and are deliberately never time-gated — see the next test.)
    auto a = MakePicture("a.jpg", 0x11ull, 100, 1000, 800);
    auto b = MakePicture("b.jpg", 0x11ull, 100, 998, 800);

    // Both without EXIF: the gate must not exclude them, or a stripped album
    // would silently find nothing.
    REQUIRE_EQ(AlbumScanner::Regroup({ a, b }, options).groups.size(),
               static_cast<size_t>(1));

    // Both dated, far apart: excluded.
    a.capturedAt = 1000000;
    b.capturedAt = 1000000 + 3600;
    REQUIRE(AlbumScanner::Regroup({ a, b }, options).groups.empty());

    // Both dated, close together: kept.
    b.capturedAt = 1000000 + 60;
    REQUIRE_EQ(AlbumScanner::Regroup({ a, b }, options).groups.size(),
               static_cast<size_t>(1));
}

TEST(IdenticalPicturesAreNeverTimeGated) {
    // Two files with the same pixels are the same picture whenever they were
    // taken, so the time gate must not reach them.
    AlbumScanOptions options;
    options.maxSecondsApart = 60;

    auto a = MakePicture("a.jpg", 0x11ull, 100);
    auto b = MakePicture("b.jpg", 0x11ull, 100);
    a.capturedAt = 1000000;
    b.capturedAt = 1000000 + 999999;        // years apart

    const auto report = AlbumScanner::Regroup({ a, b }, options);
    REQUIRE_EQ(report.groups.size(), static_cast<size_t>(1));
    REQUIRE_EQ(static_cast<int>(report.groups[0].reason),
               static_cast<int>(GroupReason::IdenticalPixels));
}

TEST(RemovalReportKeepsOneOfEachGroupAndAllowsItsFolder) {
    auto a = MakePicture("/albums/trip/a.jpg", 0x11ull, 100, 1000, 800);
    auto b = MakePicture("/albums/trip/b.jpg", 0x11ull, 100, 900, 700);
    auto album = AlbumScanner::Regroup({ a, b }, {});
    REQUIRE_EQ(album.groups.size(), static_cast<size_t>(1));

    const ScanReport removal = ToRemovalReport(album, { 0 });
    // One of the two survives.
    REQUIRE_EQ(removal.items.size(), static_cast<size_t>(1));
    REQUIRE(removal.items[0].path != album.groups[0].Keeper().path);
    // …and the guard is given the folder it lives in.
    REQUIRE(removal.allowedRoots.empty() == false);
}

TEST(RemovalReportIgnoresGroupsThatWereNotNamed) {
    auto a = MakePicture("/albums/x/a.jpg", 0x11ull, 100);
    auto b = MakePicture("/albums/x/b.jpg", 0x11ull, 100);
    auto album = AlbumScanner::Regroup({ a, b }, {});
    REQUIRE(ToRemovalReport(album, {}).items.empty());
    REQUIRE(ToRemovalReport(album, { 99 }).items.empty());
}

TEST(RecoverableBytesExcludeTheKeeper) {
    auto a = MakePicture("/x/a.jpg", 0x11ull, 100, 1000, 800);
    auto b = MakePicture("/x/b.jpg", 0x11ull, 100, 900, 700);
    auto album = AlbumScanner::Regroup({ a, b }, {});
    const auto& group = album.groups[0];
    uint64_t expected = 0;
    for (size_t i = 0; i < group.members.size(); ++i) {
        if (i != group.keeperIndex) expected += group.members[i].fileSize;
    }
    REQUIRE_EQ(group.RecoverableBytes(), expected);
    REQUIRE(group.RecoverableBytes() < a.fileSize + b.fileSize);
}
