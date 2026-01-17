// core/UltraCanvasEBookTypes.cpp
// Implementation of eBook types and DocumentPageSize utilities
// Version: 1.0.0
// Last Modified: 2025-01-08
// Author: UltraCanvas Framework

#include "UltraCanvasEBookTypes.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

// ===== DOCUMENT PAGE SIZE IMPLEMENTATIONS =====

DocumentPageSize DocumentPageSize::FromFormat(DocumentPageFormat format, float dpi) {
    DocumentPageSize size;
    size.format = format;
    size.dpi = dpi;
    size.isLandscape = false;
    
    // Set dimensions based on format (all in mm)
    switch (format) {
        // ISO A Series
        case DocumentPageFormat::A0:
            size.widthMM = 841.0f; size.heightMM = 1189.0f;
            size.formatName = "A0 (841 × 1189 mm)";
            break;
        case DocumentPageFormat::A1:
            size.widthMM = 594.0f; size.heightMM = 841.0f;
            size.formatName = "A1 (594 × 841 mm)";
            break;
        case DocumentPageFormat::A2:
            size.widthMM = 420.0f; size.heightMM = 594.0f;
            size.formatName = "A2 (420 × 594 mm)";
            break;
        case DocumentPageFormat::A3:
            size.widthMM = 297.0f; size.heightMM = 420.0f;
            size.formatName = "A3 (297 × 420 mm)";
            break;
        case DocumentPageFormat::A4:
            size.widthMM = 210.0f; size.heightMM = 297.0f;
            size.formatName = "A4 (210 × 297 mm)";
            break;
        case DocumentPageFormat::A5:
            size.widthMM = 148.0f; size.heightMM = 210.0f;
            size.formatName = "A5 (148 × 210 mm)";
            break;
        case DocumentPageFormat::A6:
            size.widthMM = 105.0f; size.heightMM = 148.0f;
            size.formatName = "A6 (105 × 148 mm)";
            break;
        case DocumentPageFormat::A7:
            size.widthMM = 74.0f; size.heightMM = 105.0f;
            size.formatName = "A7 (74 × 105 mm)";
            break;
        case DocumentPageFormat::A8:
            size.widthMM = 52.0f; size.heightMM = 74.0f;
            size.formatName = "A8 (52 × 74 mm)";
            break;
            
        // ISO B Series
        case DocumentPageFormat::B0:
            size.widthMM = 1000.0f; size.heightMM = 1414.0f;
            size.formatName = "B0 (1000 × 1414 mm)";
            break;
        case DocumentPageFormat::B1:
            size.widthMM = 707.0f; size.heightMM = 1000.0f;
            size.formatName = "B1 (707 × 1000 mm)";
            break;
        case DocumentPageFormat::B2:
            size.widthMM = 500.0f; size.heightMM = 707.0f;
            size.formatName = "B2 (500 × 707 mm)";
            break;
        case DocumentPageFormat::B3:
            size.widthMM = 353.0f; size.heightMM = 500.0f;
            size.formatName = "B3 (353 × 500 mm)";
            break;
        case DocumentPageFormat::B4:
            size.widthMM = 250.0f; size.heightMM = 353.0f;
            size.formatName = "B4 (250 × 353 mm)";
            break;
        case DocumentPageFormat::B5:
            size.widthMM = 176.0f; size.heightMM = 250.0f;
            size.formatName = "B5 (176 × 250 mm)";
            break;
        case DocumentPageFormat::B6:
            size.widthMM = 125.0f; size.heightMM = 176.0f;
            size.formatName = "B6 (125 × 176 mm)";
            break;
        case DocumentPageFormat::B7:
            size.widthMM = 88.0f; size.heightMM = 125.0f;
            size.formatName = "B7 (88 × 125 mm)";
            break;
        case DocumentPageFormat::B8:
            size.widthMM = 62.0f; size.heightMM = 88.0f;
            size.formatName = "B8 (62 × 88 mm)";
            break;
            
        // ISO C Series (Envelopes)
        case DocumentPageFormat::C4:
            size.widthMM = 229.0f; size.heightMM = 324.0f;
            size.formatName = "C4 (229 × 324 mm)";
            break;
        case DocumentPageFormat::C5:
            size.widthMM = 162.0f; size.heightMM = 229.0f;
            size.formatName = "C5 (162 × 229 mm)";
            break;
        case DocumentPageFormat::C6:
            size.widthMM = 114.0f; size.heightMM = 162.0f;
            size.formatName = "C6 (114 × 162 mm)";
            break;
            
        // US/North American Sizes
        case DocumentPageFormat::Letter:
            size.widthMM = 215.9f; size.heightMM = 279.4f;
            size.formatName = "Letter (8.5 × 11 in)";
            break;
        case DocumentPageFormat::Legal:
            size.widthMM = 215.9f; size.heightMM = 355.6f;
            size.formatName = "Legal (8.5 × 14 in)";
            break;
        case DocumentPageFormat::Tabloid:
            size.widthMM = 279.4f; size.heightMM = 431.8f;
            size.formatName = "Tabloid (11 × 17 in)";
            break;
        case DocumentPageFormat::Ledger:
            size.widthMM = 431.8f; size.heightMM = 279.4f;
            size.formatName = "Ledger (17 × 11 in)";
            size.isLandscape = true;
            break;
        case DocumentPageFormat::Executive:
            size.widthMM = 184.15f; size.heightMM = 266.7f;
            size.formatName = "Executive (7.25 × 10.5 in)";
            break;
            
        // Book-Specific Formats
        case DocumentPageFormat::PocketBook:
            size.widthMM = 108.0f; size.heightMM = 175.0f;
            size.formatName = "Pocket Book (4.25 × 6.87 in)";
            break;
        case DocumentPageFormat::MassMarket:
            size.widthMM = 108.0f; size.heightMM = 171.0f;
            size.formatName = "Mass Market Paperback (4.25 × 6.75 in)";
            break;
        case DocumentPageFormat::TradeBook:
            size.widthMM = 152.0f; size.heightMM = 229.0f;
            size.formatName = "Trade Book (6 × 9 in)";
            break;
        case DocumentPageFormat::Digest:
            size.widthMM = 140.0f; size.heightMM = 216.0f;
            size.formatName = "Digest (5.5 × 8.5 in)";
            break;
        case DocumentPageFormat::Royal:
            size.widthMM = 156.0f; size.heightMM = 234.0f;
            size.formatName = "Royal (6.14 × 9.21 in)";
            break;
        case DocumentPageFormat::Crown:
            size.widthMM = 191.0f; size.heightMM = 254.0f;
            size.formatName = "Crown (7.5 × 10 in)";
            break;
            
        // eReader-Specific Formats
        case DocumentPageFormat::KindlePaperwhite:
            size.widthMM = 90.0f; size.heightMM = 122.0f;
            size.formatName = "Kindle Paperwhite (6\")";
            break;
        case DocumentPageFormat::KindleOasis:
            size.widthMM = 107.0f; size.heightMM = 143.0f;
            size.formatName = "Kindle Oasis (7\")";
            break;
        case DocumentPageFormat::KoboClara:
            size.widthMM = 90.0f; size.heightMM = 122.0f;
            size.formatName = "Kobo Clara (6\")";
            break;
        case DocumentPageFormat::KoboLibra:
            size.widthMM = 107.0f; size.heightMM = 143.0f;
            size.formatName = "Kobo Libra (7\")";
            break;
        case DocumentPageFormat::KoboElipsa:
            size.widthMM = 157.0f; size.heightMM = 209.0f;
            size.formatName = "Kobo Elipsa (10.3\")";
            break;
        case DocumentPageFormat::ReMarkable2:
            size.widthMM = 157.0f; size.heightMM = 209.0f;
            size.formatName = "reMarkable 2 (10.3\")";
            break;
            
        // Tablet Formats
        case DocumentPageFormat::iPadMini:
            size.widthMM = 135.0f; size.heightMM = 200.0f;
            size.formatName = "iPad Mini (7.9\")";
            break;
        case DocumentPageFormat::iPad:
            size.widthMM = 170.0f; size.heightMM = 228.0f;
            size.formatName = "iPad (10.2\")";
            break;
        case DocumentPageFormat::iPadPro11:
            size.widthMM = 178.0f; size.heightMM = 247.0f;
            size.formatName = "iPad Pro (11\")";
            break;
        case DocumentPageFormat::iPadPro13:
            size.widthMM = 214.0f; size.heightMM = 280.0f;
            size.formatName = "iPad Pro (12.9\")";
            break;
            
        // Japanese Book Formats
        case DocumentPageFormat::Bunko:
            size.widthMM = 105.0f; size.heightMM = 148.0f;
            size.formatName = "Bunko (105 × 148 mm)";
            break;
        case DocumentPageFormat::Shinsho:
            size.widthMM = 103.0f; size.heightMM = 182.0f;
            size.formatName = "Shinsho (103 × 182 mm)";
            break;
        case DocumentPageFormat::Tankobon:
            size.widthMM = 128.0f; size.heightMM = 182.0f;
            size.formatName = "Tankobon (128 × 182 mm)";
            break;
            
        // Custom (default to A5)
        case DocumentPageFormat::Custom:
        default:
            size.widthMM = 148.0f; size.heightMM = 210.0f;
            size.formatName = "Custom";
            break;
    }
    
    // Calculate inch and pixel dimensions
    size.widthInch = size.widthMM / 25.4f;
    size.heightInch = size.heightMM / 25.4f;
    size.widthPx = size.widthInch * dpi;
    size.heightPx = size.heightInch * dpi;
    
    return size;
}

DocumentPageSize DocumentPageSize::Custom(float widthMM, float heightMM, float dpi) {
    DocumentPageSize size;
    size.format = DocumentPageFormat::Custom;
    size.dpi = dpi;
    size.widthMM = widthMM;
    size.heightMM = heightMM;
    size.widthInch = widthMM / 25.4f;
    size.heightInch = heightMM / 25.4f;
    size.widthPx = size.widthInch * dpi;
    size.heightPx = size.heightInch * dpi;
    size.formatName = "Custom (" + std::to_string(static_cast<int>(widthMM)) + 
                      " × " + std::to_string(static_cast<int>(heightMM)) + " mm)";
    size.isLandscape = widthMM > heightMM;
    return size;
}

DocumentPageSize DocumentPageSize::FromInches(float widthInch, float heightInch, float dpi) {
    return Custom(widthInch * 25.4f, heightInch * 25.4f, dpi);
}

DocumentPageSize DocumentPageSize::FromPixels(float widthPx, float heightPx, float dpi) {
    float widthInch = widthPx / dpi;
    float heightInch = heightPx / dpi;
    return Custom(widthInch * 25.4f, heightInch * 25.4f, dpi);
}

std::vector<std::pair<DocumentPageFormat, std::string>> DocumentPageSize::GetAllFormats() {
    return {
        // ISO A Series
        {DocumentPageFormat::A0, "A0 (841 × 1189 mm)"},
        {DocumentPageFormat::A1, "A1 (594 × 841 mm)"},
        {DocumentPageFormat::A2, "A2 (420 × 594 mm)"},
        {DocumentPageFormat::A3, "A3 (297 × 420 mm)"},
        {DocumentPageFormat::A4, "A4 (210 × 297 mm)"},
        {DocumentPageFormat::A5, "A5 (148 × 210 mm)"},
        {DocumentPageFormat::A6, "A6 (105 × 148 mm)"},
        {DocumentPageFormat::A7, "A7 (74 × 105 mm)"},
        {DocumentPageFormat::A8, "A8 (52 × 74 mm)"},
        
        // ISO B Series
        {DocumentPageFormat::B0, "B0 (1000 × 1414 mm)"},
        {DocumentPageFormat::B1, "B1 (707 × 1000 mm)"},
        {DocumentPageFormat::B2, "B2 (500 × 707 mm)"},
        {DocumentPageFormat::B3, "B3 (353 × 500 mm)"},
        {DocumentPageFormat::B4, "B4 (250 × 353 mm)"},
        {DocumentPageFormat::B5, "B5 (176 × 250 mm)"},
        {DocumentPageFormat::B6, "B6 (125 × 176 mm)"},
        {DocumentPageFormat::B7, "B7 (88 × 125 mm)"},
        {DocumentPageFormat::B8, "B8 (62 × 88 mm)"},
        
        // ISO C Series
        {DocumentPageFormat::C4, "C4 Envelope (229 × 324 mm)"},
        {DocumentPageFormat::C5, "C5 Envelope (162 × 229 mm)"},
        {DocumentPageFormat::C6, "C6 Envelope (114 × 162 mm)"},
        
        // US Sizes
        {DocumentPageFormat::Letter, "US Letter (8.5 × 11 in)"},
        {DocumentPageFormat::Legal, "US Legal (8.5 × 14 in)"},
        {DocumentPageFormat::Tabloid, "Tabloid (11 × 17 in)"},
        {DocumentPageFormat::Ledger, "Ledger (17 × 11 in)"},
        {DocumentPageFormat::Executive, "Executive (7.25 × 10.5 in)"},
        
        // Book Formats
        {DocumentPageFormat::PocketBook, "Pocket Book (4.25 × 6.87 in)"},
        {DocumentPageFormat::MassMarket, "Mass Market (4.25 × 6.75 in)"},
        {DocumentPageFormat::TradeBook, "Trade Book (6 × 9 in)"},
        {DocumentPageFormat::Digest, "Digest (5.5 × 8.5 in)"},
        {DocumentPageFormat::Royal, "Royal (6.14 × 9.21 in)"},
        {DocumentPageFormat::Crown, "Crown (7.5 × 10 in)"},
        
        // eReader Formats
        {DocumentPageFormat::KindlePaperwhite, "Kindle Paperwhite (6\")"},
        {DocumentPageFormat::KindleOasis, "Kindle Oasis (7\")"},
        {DocumentPageFormat::KoboClara, "Kobo Clara (6\")"},
        {DocumentPageFormat::KoboLibra, "Kobo Libra (7\")"},
        {DocumentPageFormat::KoboElipsa, "Kobo Elipsa (10.3\")"},
        {DocumentPageFormat::ReMarkable2, "reMarkable 2 (10.3\")"},
        
        // Tablet Formats
        {DocumentPageFormat::iPadMini, "iPad Mini (7.9\")"},
        {DocumentPageFormat::iPad, "iPad (10.2\")"},
        {DocumentPageFormat::iPadPro11, "iPad Pro 11\""},
        {DocumentPageFormat::iPadPro13, "iPad Pro 12.9\""},
        
        // Japanese Formats
        {DocumentPageFormat::Bunko, "Bunko (105 × 148 mm)"},
        {DocumentPageFormat::Shinsho, "Shinsho (103 × 182 mm)"},
        {DocumentPageFormat::Tankobon, "Tankobon (128 × 182 mm)"},
        
        // Custom
        {DocumentPageFormat::Custom, "Custom Size"}
    };
}

} // namespace UltraCanvas
