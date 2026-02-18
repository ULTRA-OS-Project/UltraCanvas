// Apps/Texter/UltraCanvasEncoding.cpp
// Text encoding detection and conversion utilities using POSIX iconv
// Version: 1.0.0

#include "UltraCanvasEncoding.h"
#include <iconv.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <algorithm>

namespace UltraCanvas {

// ===== SUPPORTED ENCODINGS =====

std::vector<EncodingInfo> GetSupportedEncodings() {
    static const std::vector<EncodingInfo> encodings = {
        {"UTF-8",        "UTF-8"},
        {"ISO-8859-1",   "ISO 8859-1 (Latin-1)"},
        {"ISO-8859-2",   "ISO 8859-2 (Latin-2)"},
        {"ISO-8859-5",   "ISO 8859-5 (Cyrillic)"},
        {"ISO-8859-9",   "ISO 8859-9 (Turkish)"},
        {"ISO-8859-15",  "ISO 8859-15 (Latin-9)"},
        {"CP1250",       "Windows-1250 (Central European)"},
        {"CP1251",       "Windows-1251 (Cyrillic)"},
        {"CP1252",       "Windows-1252 (Western)"},
        {"CP1253",       "Windows-1253 (Greek)"},
        {"CP1254",       "Windows-1254 (Turkish)"},
        {"CP1256",       "Windows-1256 (Arabic)"},
        {"CP866",        "DOS-866 (Cyrillic)"},
        {"KOI8-R",       "KOI8-R (Russian)"},
        {"KOI8-U",       "KOI8-U (Ukrainian)"},
        {"UTF-16LE",     "UTF-16 LE"},
        {"UTF-16BE",     "UTF-16 BE"},
    };
    return encodings;
}

int FindEncodingIndex(const std::string& iconvName) {
    auto encodings = GetSupportedEncodings();
    for (int i = 0; i < static_cast<int>(encodings.size()); i++) {
        if (encodings[i].iconvName == iconvName) {
            return i;
        }
    }
    return -1;
}

// ===== BOM DETECTION =====

std::string DetectBOM(const std::vector<uint8_t>& data, size_t& bomLength) {
    bomLength = 0;

    if (data.size() >= 3 &&
        data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        bomLength = 3;
        return "UTF-8";
    }

    if (data.size() >= 2) {
        if (data[0] == 0xFF && data[1] == 0xFE) {
            bomLength = 2;
            return "UTF-16LE";
        }
        if (data[0] == 0xFE && data[1] == 0xFF) {
            bomLength = 2;
            return "UTF-16BE";
        }
    }

    return "";
}

// ===== UTF-8 VALIDATION =====

bool IsValidUtf8(const std::vector<uint8_t>& data) {
    size_t i = 0;
    while (i < data.size()) {
        uint8_t byte = data[i];

        int seqLen = 0;
        uint32_t codepoint = 0;

        if (byte <= 0x7F) {
            // ASCII: single byte
            i++;
            continue;
        } else if ((byte & 0xE0) == 0xC0) {
            // 2-byte sequence: 110xxxxx
            seqLen = 2;
            codepoint = byte & 0x1F;
        } else if ((byte & 0xF0) == 0xE0) {
            // 3-byte sequence: 1110xxxx
            seqLen = 3;
            codepoint = byte & 0x0F;
        } else if ((byte & 0xF8) == 0xF0) {
            // 4-byte sequence: 11110xxx
            seqLen = 4;
            codepoint = byte & 0x07;
        } else {
            // Invalid leading byte
            return false;
        }

        // Check we have enough continuation bytes
        if (i + seqLen > data.size()) {
            return false;
        }

        // Validate continuation bytes (10xxxxxx)
        for (int j = 1; j < seqLen; j++) {
            if ((data[i + j] & 0xC0) != 0x80) {
                return false;
            }
            codepoint = (codepoint << 6) | (data[i + j] & 0x3F);
        }

        // Reject overlong encodings
        if (seqLen == 2 && codepoint < 0x80) return false;
        if (seqLen == 3 && codepoint < 0x800) return false;
        if (seqLen == 4 && codepoint < 0x10000) return false;

        // Reject surrogates (U+D800 to U+DFFF)
        if (codepoint >= 0xD800 && codepoint <= 0xDFFF) return false;

        // Reject beyond U+10FFFF
        if (codepoint > 0x10FFFF) return false;

        i += seqLen;
    }
    return true;
}

// ===== ENCODING DETECTION HEURISTICS =====

// Score how well raw bytes match CP1251 (Windows Cyrillic)
// CP1251: 0xC0-0xFF = А-я (main Cyrillic letters)
// 0xA8 = Ё, 0xB8 = ё
static float ScoreCP1251(const std::vector<uint8_t>& data) {
    int cyrillicLetters = 0;
    int controlOrUnused = 0;
    int highBytes = 0;

    for (uint8_t b : data) {
        if (b < 0x80) continue;
        highBytes++;

        if (b >= 0xC0 && b <= 0xFF) {
            cyrillicLetters++;
        } else if (b == 0xA8 || b == 0xB8) {
            cyrillicLetters++; // Ё / ё
        } else if (b >= 0x80 && b <= 0x9F) {
            // CP1252 has printable chars here, but CP1251 has some too
            // Check for common CP1251 chars in this range
            if (b == 0x96 || b == 0x97 || b == 0xAB || b == 0xBB) {
                // dash, quotes - common in text
            } else {
                controlOrUnused++;
            }
        }
    }

    if (highBytes == 0) return 0.0f;
    float ratio = static_cast<float>(cyrillicLetters) / static_cast<float>(highBytes);
    // Penalize for control/unused bytes
    float penalty = static_cast<float>(controlOrUnused) / static_cast<float>(highBytes);
    return std::max(0.0f, ratio - penalty * 0.5f);
}

// Score how well raw bytes match CP866 (DOS Cyrillic)
// CP866: 0x80-0xAF = А-п, 0xE0-0xEF = р-я
// 0xF0 = Ё, 0xF1 = ё
static float ScoreCP866(const std::vector<uint8_t>& data) {
    int cyrillicLetters = 0;
    int boxDrawing = 0;
    int highBytes = 0;

    for (uint8_t b : data) {
        if (b < 0x80) continue;
        highBytes++;

        if ((b >= 0x80 && b <= 0xAF) || (b >= 0xE0 && b <= 0xEF)) {
            cyrillicLetters++;
        } else if (b == 0xF0 || b == 0xF1) {
            cyrillicLetters++; // Ё / ё
        } else if (b >= 0xB0 && b <= 0xDF) {
            boxDrawing++; // Box drawing chars - less common in text
        }
    }

    if (highBytes == 0) return 0.0f;
    float ratio = static_cast<float>(cyrillicLetters) / static_cast<float>(highBytes);
    // Heavy box drawing suggests this might actually be CP866 but not text
    float penalty = static_cast<float>(boxDrawing) / static_cast<float>(highBytes);
    return std::max(0.0f, ratio - penalty * 0.3f);
}

// Score how well raw bytes match KOI8-R (Russian)
// KOI8-R: 0xC0-0xFF = Cyrillic letters (different order than CP1251)
// Key distinction: in KOI8-R, lowercase letters are 0xC0-0xDF, uppercase 0xE0-0xFF
static float ScoreKOI8R(const std::vector<uint8_t>& data) {
    int cyrillicLetters = 0;
    int pseudoGraphics = 0;
    int highBytes = 0;

    for (uint8_t b : data) {
        if (b < 0x80) continue;
        highBytes++;

        if (b >= 0xC0 && b <= 0xFF) {
            cyrillicLetters++;
        } else if (b == 0xB3 || b == 0xA3) {
            cyrillicLetters++; // ё / Ё in KOI8-R
        } else if (b >= 0x80 && b <= 0xBF) {
            pseudoGraphics++; // Pseudo-graphics in 0x80-0xBF range
        }
    }

    if (highBytes == 0) return 0.0f;
    float ratio = static_cast<float>(cyrillicLetters) / static_cast<float>(highBytes);
    float penalty = static_cast<float>(pseudoGraphics) / static_cast<float>(highBytes);
    return std::max(0.0f, ratio - penalty * 0.3f);
}

// Score for CP1252/ISO-8859-1 (Western European)
static float ScoreLatin1(const std::vector<uint8_t>& data) {
    int printable = 0;
    int control = 0;
    int highBytes = 0;

    for (uint8_t b : data) {
        if (b < 0x80) continue;
        highBytes++;

        // ISO-8859-1: 0x80-0x9F are C1 control codes (unusual in text)
        if (b >= 0x80 && b <= 0x9F) {
            control++;
        } else if (b >= 0xA0) {
            printable++; // Latin characters with diacritics
        }
    }

    if (highBytes == 0) return 0.0f;
    float ratio = static_cast<float>(printable) / static_cast<float>(highBytes);
    float penalty = static_cast<float>(control) / static_cast<float>(highBytes);
    return std::max(0.0f, ratio - penalty * 0.8f);
}

DetectionResult DetectEncoding(const std::vector<uint8_t>& rawBytes) {
    // Empty file
    if (rawBytes.empty()) {
        return {"UTF-8", 1.0f};
    }

    // Check for BOM
    size_t bomLength = 0;
    std::string bomEncoding = DetectBOM(rawBytes, bomLength);
    if (!bomEncoding.empty()) {
        return {bomEncoding, 1.0f};
    }

    // Check if valid UTF-8
    if (IsValidUtf8(rawBytes)) {
        // Check if it contains any multi-byte sequences (non-ASCII)
        bool hasHighBytes = false;
        for (uint8_t b : rawBytes) {
            if (b >= 0x80) {
                hasHighBytes = true;
                break;
            }
        }
        if (hasHighBytes) {
            return {"UTF-8", 0.95f};
        }
        // Pure ASCII — valid as UTF-8
        return {"UTF-8", 1.0f};
    }

    // Not valid UTF-8 — try heuristic detection
    // Use a sample of the file (first 64KB) for heuristics
    size_t sampleSize = std::min(rawBytes.size(), size_t(65536));
    std::vector<uint8_t> sample(rawBytes.begin(), rawBytes.begin() + sampleSize);

    float cp1251Score = ScoreCP1251(sample);
    float cp866Score = ScoreCP866(sample);
    float koi8rScore = ScoreKOI8R(sample);
    float latin1Score = ScoreLatin1(sample);

    // Find the best match
    struct Candidate {
        std::string encoding;
        float score;
    };

    std::vector<Candidate> candidates = {
        {"CP1251", cp1251Score},
        {"CP866",  cp866Score},
        {"KOI8-R", koi8rScore},
        {"ISO-8859-1", latin1Score},
    };

    auto best = std::max_element(candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b) { return a.score < b.score; });

    if (best != candidates.end() && best->score > 0.3f) {
        // Scale confidence: a score of 0.8+ is high confidence
        float confidence = std::min(0.9f, best->score);
        return {best->encoding, confidence};
    }

    // Fallback: ISO-8859-1 always accepts all byte values
    return {"ISO-8859-1", 0.3f};
}

// ===== ICONV CONVERSION =====

bool ConvertToUtf8(const std::vector<uint8_t>& rawBytes,
                   const std::string& sourceEncoding,
                   std::string& outUtf8) {
    if (rawBytes.empty()) {
        outUtf8.clear();
        return true;
    }

    if (sourceEncoding == "UTF-8") {
        outUtf8.assign(reinterpret_cast<const char*>(rawBytes.data()), rawBytes.size());
        return true;
    }

    iconv_t cd = iconv_open("UTF-8", sourceEncoding.c_str());
    if (cd == reinterpret_cast<iconv_t>(-1)) {
        std::cerr << "iconv_open failed for " << sourceEncoding
                  << " -> UTF-8: " << strerror(errno) << std::endl;
        return false;
    }

    // Worst case: each input byte could become up to 4 UTF-8 bytes
    size_t outBufSize = rawBytes.size() * 4 + 4;
    std::vector<char> outBuf(outBufSize);

    char* inPtr = const_cast<char*>(reinterpret_cast<const char*>(rawBytes.data()));
    size_t inBytesLeft = rawBytes.size();
    char* outPtr = outBuf.data();
    size_t outBytesLeft = outBufSize;

    size_t result = iconv(cd, &inPtr, &inBytesLeft, &outPtr, &outBytesLeft);
    iconv_close(cd);

    if (result == static_cast<size_t>(-1)) {
        if (errno == E2BIG) {
            // Output buffer too small — shouldn't happen with 4x allocation
            std::cerr << "iconv: output buffer too small" << std::endl;
        } else if (errno == EILSEQ) {
            std::cerr << "iconv: invalid byte sequence in input for encoding "
                      << sourceEncoding << std::endl;
        } else if (errno == EINVAL) {
            std::cerr << "iconv: incomplete byte sequence at end of input" << std::endl;
        }
        // Even on error, use whatever was converted so far
        size_t converted = outBufSize - outBytesLeft;
        if (converted > 0) {
            outUtf8.assign(outBuf.data(), converted);
            return true; // Partial conversion is better than nothing
        }
        return false;
    }

    outUtf8.assign(outBuf.data(), outBufSize - outBytesLeft);
    return true;
}

bool ConvertFromUtf8(const std::string& utf8Text,
                     const std::string& targetEncoding,
                     std::vector<uint8_t>& outBytes) {
    if (utf8Text.empty()) {
        outBytes.clear();
        return true;
    }

    if (targetEncoding == "UTF-8") {
        outBytes.assign(utf8Text.begin(), utf8Text.end());
        return true;
    }

    // Use //TRANSLIT to transliterate characters that can't be represented
    std::string targetWithTranslit = targetEncoding + "//TRANSLIT";
    iconv_t cd = iconv_open(targetWithTranslit.c_str(), "UTF-8");
    if (cd == reinterpret_cast<iconv_t>(-1)) {
        // Try without TRANSLIT
        cd = iconv_open(targetEncoding.c_str(), "UTF-8");
        if (cd == reinterpret_cast<iconv_t>(-1)) {
            std::cerr << "iconv_open failed for UTF-8 -> " << targetEncoding
                      << ": " << strerror(errno) << std::endl;
            return false;
        }
    }

    // For single-byte encodings output is same size or smaller,
    // for UTF-16 it could be 2x
    size_t outBufSize = utf8Text.size() * 2 + 4;
    std::vector<char> outBuf(outBufSize);

    char* inPtr = const_cast<char*>(utf8Text.data());
    size_t inBytesLeft = utf8Text.size();
    char* outPtr = outBuf.data();
    size_t outBytesLeft = outBufSize;

    size_t result = iconv(cd, &inPtr, &inBytesLeft, &outPtr, &outBytesLeft);
    iconv_close(cd);

    if (result == static_cast<size_t>(-1) && errno != E2BIG) {
        if (errno == EILSEQ) {
            std::cerr << "iconv: character cannot be represented in "
                      << targetEncoding << std::endl;
        }
        // Use partial conversion
        size_t converted = outBufSize - outBytesLeft;
        if (converted > 0) {
            outBytes.assign(reinterpret_cast<uint8_t*>(outBuf.data()),
                           reinterpret_cast<uint8_t*>(outBuf.data()) + converted);
            return true;
        }
        return false;
    }

    size_t totalBytes = outBufSize - outBytesLeft;
    outBytes.assign(reinterpret_cast<uint8_t*>(outBuf.data()),
                   reinterpret_cast<uint8_t*>(outBuf.data()) + totalBytes);
    return true;
}

} // namespace UltraCanvas
