## 📚 eBook Reader Support

UltraCanvas includes a comprehensive eBook reading system with native support for 10+ formats spanning three decades of digital publishing.

### Supported Formats

| Format | Extension | Description |
|--------|-----------|-------------|
| **EPUB** | `.epub` | Modern standard (EPUB 2.0/3.0) |
| **MOBI** | `.mobi`, `.prc`, `.azw` | Mobipocket / Amazon Kindle |
| **AZW3** | `.azw3`, `.kf8` | Kindle Format 8 |
| **FB2** | `.fb2` | FictionBook (popular in Russia/Europe) |
| **LIT** | `.lit` | Microsoft Reader (2000-2012) |
| **LRF** | `.lrf` | Sony Reader / BBeB |
| **RB** | `.rb` | RocketEdition / NuvoMedia |
| **TCR** | `.tcr` | Psion Series 3 |
| **OEB** | `.oeb`, `.opf` | Open eBook (EPUB predecessor) |
| **PDF** | `.pdf` | Via PDFViewer plugin |

### Features

- 📖 **Unified API** — Single `IEBookEngine` interface for all formats
- 🔍 **Full-text search** across book content
- 📑 **Table of contents** extraction and navigation
- 🖼️ **Cover image** extraction
- 📝 **Metadata** parsing (title, author, publisher, ISBN)
- 🗜️ **Decompression** via VirtualFS (DEFLATE, LZX, PalmDOC)
- 🎨 **HTML/CSS rendering** through `UltraCanvasHTMLConverter`

### Quick Start

```cpp
#include "UltraCanvasEBookViewer.h"

// Create viewer widget
auto viewer = UltraCanvas::CreateEBookViewer("reader", 0, 0, 800, 600);

// Load any supported format
viewer->LoadDocument("/path/to/book.epub");

// Or use engine directly
auto engine = UltraCanvas::CreateEPUBEngine();
engine->LoadFile("/path/to/book.epub");

auto metadata = engine->GetMetadata();
std::cout << "Title: " << metadata.title << std::endl;
std::cout << "Author: " << metadata.author << std::endl;

// Search
auto results = engine->Search("adventure");

// Navigate
int pageCount = engine->GetPageCount();
std::string content = engine->GetPageContent(1);
```

### Architecture

```
UltraCanvasEBookViewer
        │
        ▼
  IEBookEngine (Interface)
        │
        ├── EPUBEngine ──► OEBEngine
        ├── FB2Engine
        ├── MOBIEngine ──► AZW3Engine
        ├── LITEngine
        ├── LRFEngine
        ├── RBEngine
        └── TCREngine
```

All engines integrate with **VirtualFS** for transparent archive access and decompression.
