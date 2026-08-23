// Apps/UltraCleaner/album/UltraCleanerImageDescriptor.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCleanerImageDescriptor.h"

#include "UltraCanvasImage.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <vector>

namespace UltraCleaner {
namespace {

namespace fs = std::filesystem;

constexpr int kHashSide = 32;      // pHash works on a 32x32 luma image
constexpr int kHashBlock = 8;      // ...and keeps its top-left 8x8 DCT block

// Separable DCT-II. n is 32, so the direct form is fast enough and keeps this
// file free of any transform dependency.
std::vector<double> Transform2D(const std::vector<double>& input, int n) {
    static std::vector<double> cosTable;
    static int cachedN = 0;
    if (cachedN != n) {
        cosTable.assign(static_cast<size_t>(n) * n, 0.0);
        for (int u = 0; u < n; ++u) {
            for (int x = 0; x < n; ++x) {
                cosTable[static_cast<size_t>(u) * n + x] =
                    std::cos((2.0 * x + 1.0) * u * M_PI / (2.0 * n));
            }
        }
        cachedN = n;
    }
    std::vector<double> rows(static_cast<size_t>(n) * n, 0.0);
    std::vector<double> out(static_cast<size_t>(n) * n, 0.0);
    for (int y = 0; y < n; ++y) {
        for (int u = 0; u < n; ++u) {
            double sum = 0.0;
            for (int x = 0; x < n; ++x) {
                sum += input[static_cast<size_t>(y) * n + x] *
                       cosTable[static_cast<size_t>(u) * n + x];
            }
            rows[static_cast<size_t>(y) * n + u] = sum;
        }
    }
    for (int u = 0; u < n; ++u) {
        for (int v = 0; v < n; ++v) {
            double sum = 0.0;
            for (int y = 0; y < n; ++y) {
                sum += rows[static_cast<size_t>(y) * n + u] *
                       cosTable[static_cast<size_t>(v) * n + y];
            }
            out[static_cast<size_t>(v) * n + u] = sum;
        }
    }
    return out;
}

std::string LowerCase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

// Decodes `path` to exactly w x h pixels, ignoring its aspect ratio on
// purpose: a 16:9 frame and a 1:1 crop of the same scene then line up cell for
// cell. Returns false when the image cannot be read.
bool DecodeToGrid(const std::string& path, int w, int h,
                  std::vector<uint32_t>& pixels) {
    auto image = UltraCanvas::UCImageRaster::Load(path, /*loadOnlyHeader=*/false);
    if (!image || !image->IsValid()) return false;

    auto pixmap = image->CreatePixmap(w, h, UltraCanvas::ImageFitMode::Fill, 1.0f);
    if (!pixmap || !pixmap->IsValid()) return false;
    const uint32_t* data = pixmap->GetPixelData();
    if (!data) return false;

    pixels.assign(data, data + static_cast<size_t>(w) * h);
    return true;
}

inline int RedOf(uint32_t p)   { return static_cast<int>((p >> 16) & 0xFF); }
inline int GreenOf(uint32_t p) { return static_cast<int>((p >> 8) & 0xFF); }
inline int BlueOf(uint32_t p)  { return static_cast<int>(p & 0xFF); }

inline double LumaOf(uint32_t p) {
    // Rec. 601 luma; the exact weights matter less than using the same ones
    // everywhere, since only differences between images are compared.
    return 0.299 * RedOf(p) + 0.587 * GreenOf(p) + 0.114 * BlueOf(p);
}

} // namespace

const char* PictureKindName(PictureKind kind) {
    switch (kind) {
        case PictureKind::Photograph: return "photo";
        case PictureKind::Screenshot: return "screenshot";
        default:                      return "unknown";
    }
}

LevelThresholds ThresholdsFor(SimilarityLevel level) {
    // Measured on the reference album (60 photographs, 7 known groups):
    //   Duplicates  finds copies and rescales; splits long bursts.
    //   Moments     reproduces every known group with no false positive.
    //   Scenes      adds two plausible groups and one contaminated one.
    switch (level) {
        case SimilarityLevel::Duplicates: return { 6,  0.90 };
        case SimilarityLevel::Scenes:     return { 22, 0.80 };
        case SimilarityLevel::Moments:
        default:                          return { 14, 0.85 };
    }
}

const char* SimilarityLevelName(SimilarityLevel level) {
    switch (level) {
        case SimilarityLevel::Duplicates: return "Duplicates only";
        case SimilarityLevel::Scenes:     return "Same scene";
        case SimilarityLevel::Moments:
        default:                          return "Same moment";
    }
}

const char* SimilarityLevelDescription(SimilarityLevel level) {
    switch (level) {
        case SimilarityLevel::Duplicates:
            return "Only the same photo again — a copy, a rescale, a small "
                   "edit. Nothing that is merely alike.";
        case SimilarityLevel::Scenes:
            return "Anything of the same scene or subject. Finds more, and "
                   "will sometimes put two different photos together.";
        case SimilarityLevel::Moments:
        default:
            return "Shots of the same moment — a burst, or several tries at "
                   "one picture. The recommended setting.";
    }
}

int HammingDistance(uint64_t a, uint64_t b) {
    return __builtin_popcountll(a ^ b);
}

double ColourGridAgreement(const ImageDescriptor& a, const ImageDescriptor& b,
                           int tolerance) {
    int agreeing = 0;
    for (int cell = 0; cell < kColourGridSide * kColourGridSide; ++cell) {
        const int red   = std::abs(int(a.colourGrid[cell * 3 + 0]) -
                                   int(b.colourGrid[cell * 3 + 0]));
        const int green = std::abs(int(a.colourGrid[cell * 3 + 1]) -
                                   int(b.colourGrid[cell * 3 + 1]));
        const int blue  = std::abs(int(a.colourGrid[cell * 3 + 2]) -
                                   int(b.colourGrid[cell * 3 + 2]));
        if (red <= tolerance && green <= tolerance && blue <= tolerance) {
            ++agreeing;
        }
    }
    return static_cast<double>(agreeing) / (kColourGridSide * kColourGridSide);
}

bool AreSimilar(const ImageDescriptor& a, const ImageDescriptor& b,
                const LevelThresholds& thresholds) {
    if (!a.valid || !b.valid) return false;
    // Screenshots are never compared for similarity — see PictureKind.
    if (a.kind != PictureKind::Photograph || b.kind != PictureKind::Photograph) {
        return false;
    }
    if (HammingDistance(a.pHash, b.pHash) > thresholds.maxPHashDistance) {
        return false;
    }
    return ColourGridAgreement(a, b) >= thresholds.minGridAgreement;
}

int64_t ParseExifTimestamp(const std::string& exif) {
    // EXIF spells it "YYYY:MM:DD HH:MM:SS".
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (std::sscanf(exif.c_str(), "%d:%d:%d %d:%d:%d", &year, &month, &day,
                    &hour, &minute, &second) != 6) {
        return 0;
    }
    // Some software writes an all-zero placeholder rather than omitting the
    // tag; that is "unknown", not 1899.
    if (year <= 1900 || month < 1 || month > 12 || day < 1 || day > 31) return 0;

    std::tm when{};
    when.tm_year  = year - 1900;
    when.tm_mon   = month - 1;
    when.tm_mday  = day;
    when.tm_hour  = hour;
    when.tm_min   = minute;
    when.tm_sec   = second;
    when.tm_isdst = -1;
    const std::time_t stamp = std::mktime(&when);
    return stamp == static_cast<std::time_t>(-1) ? 0 : static_cast<int64_t>(stamp);
}

bool HasImageExtension(const std::string& path) {
    const std::string ext = LowerCase(fs::path(path).extension().string());
    static const char* known[] = {
        ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp", ".tif", ".tiff",
        ".heic", ".heif", ".avif", ".jxl", ".qoi", ".ppm", ".tga"
    };
    for (const char* candidate : known) {
        if (ext == candidate) return true;
    }
    return false;
}

PictureKind ClassifyPicture(const std::string& path, int width, int height,
                            const std::string& cameraMake,
                            const std::string& cameraModel) {
    // A camera stamped it: a photograph, whatever it is called.
    if (!cameraMake.empty() || !cameraModel.empty()) return PictureKind::Photograph;

    const std::string name = LowerCase(fs::path(path).filename().string());
    static const char* screenshotPrefixes[] = {
        "screenshot", "screen shot", "bildschirmfoto", "capture d",
        "captura de pantalla", "schermafbeelding", "zrzut ekranu",
        "screen_", "scrn", "snip"
    };
    for (const char* prefix : screenshotPrefixes) {
        if (name.rfind(prefix, 0) == 0) return PictureKind::Screenshot;
    }

    // A lossless format at an exact display or window size, with no camera
    // metadata at all, is almost always a capture rather than a picture.
    const std::string ext = LowerCase(fs::path(path).extension().string());
    if (ext == ".png" || ext == ".bmp") {
        static const int kScreenWidths[] = {
            1024, 1152, 1280, 1366, 1440, 1600, 1680, 1920, 2048, 2240, 2560,
            2880, 3440, 3840, 5120
        };
        for (int screenWidth : kScreenWidths) {
            if (width == screenWidth) return PictureKind::Screenshot;
        }
        // A PNG with no camera tag is still much more likely a capture, an
        // export or a graphic than a photograph.
        return PictureKind::Screenshot;
    }
    (void)height;
    return PictureKind::Photograph;
}

ImageDescriptor DescribeImage(const std::string& path) {
    ImageDescriptor descriptor;
    descriptor.path = path;

    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    descriptor.fileSize = ec ? 0 : static_cast<uint64_t>(size);

    auto header = UltraCanvas::UCImageRaster::Load(path, /*loadOnlyHeader=*/true);
    if (!header || !header->IsValid()) return descriptor;
    descriptor.width = header->GetWidth();
    descriptor.height = header->GetHeight();

    descriptor.capturedAtText = header->GetMetadataString("exif-ifd2-DateTimeOriginal");
    if (descriptor.capturedAtText.empty()) {
        descriptor.capturedAtText = header->GetMetadataString("exif-ifd0-DateTime");
    }
    descriptor.capturedAt = ParseExifTimestamp(descriptor.capturedAtText);
    if (descriptor.capturedAt == 0) descriptor.capturedAtText.clear();

    descriptor.kind = ClassifyPicture(path, descriptor.width, descriptor.height,
                                      header->GetMetadataString("exif-ifd0-Make"),
                                      header->GetMetadataString("exif-ifd0-Model"));

    // ---- one decode at 32x32 feeds pHash, dHash and sharpness ----
    std::vector<uint32_t> pixels;
    if (!DecodeToGrid(path, kHashSide, kHashSide, pixels)) return descriptor;

    std::vector<double> luma(static_cast<size_t>(kHashSide) * kHashSide);
    for (size_t i = 0; i < luma.size(); ++i) luma[i] = LumaOf(pixels[i]);

    // pHash: the top-left 8x8 DCT block without its DC term, thresholded at
    // the median. Dropping DC is what makes it ignore overall brightness.
    const std::vector<double> dct = Transform2D(luma, kHashSide);
    std::vector<double> coefficients;
    coefficients.reserve(kHashBlock * kHashBlock);
    for (int y = 0; y < kHashBlock; ++y) {
        for (int x = 0; x < kHashBlock; ++x) {
            if (x != 0 || y != 0) {
                coefficients.push_back(dct[static_cast<size_t>(y) * kHashSide + x]);
            }
        }
    }
    std::vector<double> ordered = coefficients;
    std::nth_element(ordered.begin(), ordered.begin() + ordered.size() / 2,
                     ordered.end());
    const double median = ordered[ordered.size() / 2];
    for (size_t bit = 0; bit < coefficients.size() && bit < 64; ++bit) {
        if (coefficients[bit] > median) descriptor.pHash |= (1ull << bit);
    }

    // dHash: is each pixel brighter than the one to its right?
    {
        std::vector<uint32_t> wide;
        if (DecodeToGrid(path, 9, 8, wide)) {
            int bit = 0;
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 8; ++x, ++bit) {
                    const double left  = LumaOf(wide[static_cast<size_t>(y) * 9 + x]);
                    const double right = LumaOf(wide[static_cast<size_t>(y) * 9 + x + 1]);
                    if (left > right) descriptor.dHash |= (1ull << bit);
                }
            }
        }
    }

    // Sharpness: variance of a 3x3 Laplacian. Only ever compared between
    // members of one group, so the absolute scale does not matter.
    {
        double sum = 0.0, sumOfSquares = 0.0;
        int samples = 0;
        for (int y = 1; y < kHashSide - 1; ++y) {
            for (int x = 1; x < kHashSide - 1; ++x) {
                const double response =
                    4 * luma[static_cast<size_t>(y) * kHashSide + x]
                    - luma[static_cast<size_t>(y - 1) * kHashSide + x]
                    - luma[static_cast<size_t>(y + 1) * kHashSide + x]
                    - luma[static_cast<size_t>(y) * kHashSide + x - 1]
                    - luma[static_cast<size_t>(y) * kHashSide + x + 1];
                sum += response;
                sumOfSquares += response * response;
                ++samples;
            }
        }
        if (samples > 0) {
            const double mean = sum / samples;
            descriptor.sharpness = sumOfSquares / samples - mean * mean;
        }
    }

    // ---- colour grid: 4x4 block means ----
    {
        std::vector<uint32_t> cells;
        if (DecodeToGrid(path, kColourGridSide, kColourGridSide, cells)) {
            for (int i = 0; i < kColourGridSide * kColourGridSide; ++i) {
                descriptor.colourGrid[i * 3 + 0] = static_cast<uint8_t>(RedOf(cells[i]));
                descriptor.colourGrid[i * 3 + 1] = static_cast<uint8_t>(GreenOf(cells[i]));
                descriptor.colourGrid[i * 3 + 2] = static_cast<uint8_t>(BlueOf(cells[i]));
            }
        }
    }

    descriptor.valid = true;
    return descriptor;
}

} // namespace UltraCleaner
