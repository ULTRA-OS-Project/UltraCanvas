// Tests/EBookEngineTest.cpp
// Unit tests for the EPUB and TXT engines: builds a synthetic EPUB 2 book
// in memory (miniz writer) and exercises metadata, spine, TOC, resources,
// cover, text extraction, search, and the engine registry.
// Version: 1.0.0
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework

#include "EPUBEngine.h"
#include "TXTEngine.h"

#include "miniz.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define CHECK_EQ(a, b) do { \
    ++checks; \
    auto va = (a); auto vb = (b); \
    if (!(va == vb)) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b); \
    } \
} while (0)

// ============================================================================
// SYNTHETIC EPUB
// ============================================================================

static void AddFile(mz_zip_archive& zip, const char* name, const std::string& data,
                    mz_uint level = MZ_BEST_COMPRESSION) {
    mz_zip_writer_add_mem(&zip, name, data.data(), data.size(), level);
}

static std::vector<uint8_t> MakeTestEPUB() {
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    mz_zip_writer_init_heap(&zip, 0, 0);

    AddFile(zip, "mimetype", "application/epub+zip", MZ_NO_COMPRESSION);

    AddFile(zip, "META-INF/container.xml",
        "<?xml version=\"1.0\"?>\n"
        "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">\n"
        "  <rootfiles>\n"
        "    <rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>\n"
        "  </rootfiles>\n"
        "</container>");

    AddFile(zip, "OEBPS/content.opf",
        "<?xml version=\"1.0\"?>\n"
        "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"2.0\" unique-identifier=\"id\">\n"
        "  <metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:opf=\"http://www.idpf.org/2007/opf\">\n"
        "    <dc:title>Test &amp; Book</dc:title>\n"
        "    <dc:creator>Alice Author</dc:creator>\n"
        "    <dc:creator>Bob Cowriter</dc:creator>\n"
        "    <dc:language>en</dc:language>\n"
        "    <dc:publisher>Test Press</dc:publisher>\n"
        "    <dc:date>2020-01-01</dc:date>\n"
        "    <dc:subject>Testing</dc:subject>\n"
        "    <meta name=\"cover\" content=\"cover-img\"/>\n"
        "  </metadata>\n"
        "  <manifest>\n"
        "    <item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>\n"
        "    <item id=\"ch1\" href=\"text/ch1.xhtml\" media-type=\"application/xhtml+xml\"/>\n"
        "    <item id=\"ch2\" href=\"text/ch2.xhtml\" media-type=\"application/xhtml+xml\"/>\n"
        "    <item id=\"css\" href=\"styles/book.css\" media-type=\"text/css\"/>\n"
        "    <item id=\"cover-img\" href=\"images/cover.png\" media-type=\"image/png\"/>\n"
        "  </manifest>\n"
        "  <spine toc=\"ncx\">\n"
        "    <itemref idref=\"ch1\"/>\n"
        "    <itemref idref=\"ch2\"/>\n"
        "  </spine>\n"
        "</package>");

    AddFile(zip, "OEBPS/toc.ncx",
        "<?xml version=\"1.0\"?>\n"
        "<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\">\n"
        "  <navMap>\n"
        "    <navPoint id=\"n1\" playOrder=\"1\">\n"
        "      <navLabel><text>Chapter One</text></navLabel>\n"
        "      <content src=\"text/ch1.xhtml\"/>\n"
        "      <navPoint id=\"n1a\" playOrder=\"2\">\n"
        "        <navLabel><text>Section 1.1</text></navLabel>\n"
        "        <content src=\"text/ch1.xhtml#s11\"/>\n"
        "      </navPoint>\n"
        "    </navPoint>\n"
        "    <navPoint id=\"n2\" playOrder=\"3\">\n"
        "      <navLabel><text>Chapter Two</text></navLabel>\n"
        "      <content src=\"text/ch2.xhtml\"/>\n"
        "    </navPoint>\n"
        "  </navMap>\n"
        "</ncx>");

    AddFile(zip, "OEBPS/text/ch1.xhtml",
        "<?xml version=\"1.0\"?><html xmlns=\"http://www.w3.org/1999/xhtml\">"
        "<head><title>One</title></head>"
        "<body><h1>Chapter One</h1><p>The quick brown fox.</p>"
        "<img src=\"../images/cover.png\" alt=\"pic\"/></body></html>");

    AddFile(zip, "OEBPS/text/ch2.xhtml",
        "<?xml version=\"1.0\"?><html xmlns=\"http://www.w3.org/1999/xhtml\">"
        "<head><title>Two</title></head>"
        "<body><h1>Chapter Two</h1><p>Jumps over the lazy dog.</p></body></html>");

    AddFile(zip, "OEBPS/styles/book.css", "p { margin: 1em 0; }");

    // 1×1 PNG.
    static const uint8_t kPng[] = {
        0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A,
        0, 0, 0, 0x0D, 'I', 'H', 'D', 'R',
        0, 0, 0, 1, 0, 0, 0, 1, 8, 6, 0, 0, 0,
        0x1F, 0x15, 0xC4, 0x89,
        0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82};
    mz_zip_writer_add_mem(&zip, "OEBPS/images/cover.png", kPng, sizeof(kPng),
                          MZ_NO_COMPRESSION);

    void* buffer = nullptr;
    size_t size = 0;
    mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size);
    std::vector<uint8_t> result(static_cast<uint8_t*>(buffer),
                                static_cast<uint8_t*>(buffer) + size);
    mz_zip_writer_end(&zip);
    mz_free(buffer);
    return result;
}

// ============================================================================
// EPUB TESTS
// ============================================================================

static void TestPathUtilities() {
    CHECK_EQ(EPUBEngine::NormalizePath("OEBPS/text/../images/x.png"),
             std::string("OEBPS/images/x.png"));
    CHECK_EQ(EPUBEngine::NormalizePath("./a/./b"), std::string("a/b"));
    CHECK_EQ(EPUBEngine::ResolveHref("OEBPS/text/ch1.xhtml", "../images/cover.png"),
             std::string("OEBPS/images/cover.png"));
    CHECK_EQ(EPUBEngine::ResolveHref("OEBPS/content.opf", "text/ch1.xhtml"),
             std::string("OEBPS/text/ch1.xhtml"));
    CHECK_EQ(EPUBEngine::ResolveHref("toc.ncx", "ch1.xhtml"),
             std::string("ch1.xhtml"));
}

static void TestEPUB() {
    EPUBEngine engine;
    CHECK(engine.LoadFromMemory(MakeTestEPUB()));
    CHECK(engine.IsLoaded());
    CHECK_EQ(engine.GetFormatName(), std::string("EPUB 2"));

    const EBookMetadata& meta = engine.GetMetadata();
    CHECK_EQ(meta.title, std::string("Test & Book"));
    CHECK_EQ(meta.authors.size(), size_t(2));
    CHECK_EQ(meta.GetPrimaryAuthor(), std::string("Alice Author"));
    CHECK_EQ(meta.language, std::string("en"));
    CHECK_EQ(meta.publisher, std::string("Test Press"));
    CHECK_EQ(meta.chapterCount, 2);
    CHECK(meta.hasCover);

    // Chapters.
    CHECK_EQ(engine.GetChapterCount(), 2);
    EBookChapter ch1 = engine.GetChapter(0);
    CHECK_EQ(ch1.href, std::string("OEBPS/text/ch1.xhtml"));
    CHECK_EQ(ch1.title, std::string("Chapter One"));
    CHECK(ch1.content.find("quick brown fox") != std::string::npos);
    EBookChapter ch2 = engine.GetChapter(1);
    CHECK_EQ(ch2.title, std::string("Chapter Two"));
    CHECK(engine.GetChapter(5).content.empty());

    // TOC with nesting; pageNumber = chapter index.
    const auto& toc = engine.GetTableOfContents();
    CHECK_EQ(toc.size(), size_t(2));
    if (toc.size() == 2) {
        CHECK_EQ(toc[0].title, std::string("Chapter One"));
        CHECK_EQ(toc[0].pageNumber, 0);
        CHECK_EQ(toc[0].children.size(), size_t(1));
        if (!toc[0].children.empty()) {
            CHECK_EQ(toc[0].children[0].title, std::string("Section 1.1"));
            CHECK_EQ(toc[0].children[0].pageNumber, 0);   // fragment → same file
        }
        CHECK_EQ(toc[1].pageNumber, 1);
    }

    // Stylesheets.
    CHECK(engine.GetStylesheets().find("margin: 1em") != std::string::npos);

    // Resources: chapter-relative href resolution.
    std::string resolved = EPUBEngine::ResolveHref(ch1.href, "../images/cover.png");
    std::vector<uint8_t> image = engine.GetResource(resolved);
    CHECK_EQ(image.size(), size_t(45));
    CHECK(engine.GetResource("images/cover.png").size() == 45);   // OPF-relative fallback
    CHECK(engine.GetResource("missing.png").empty());

    // Cover.
    CHECK_EQ(engine.GetCoverImage().size(), size_t(45));

    // Text extraction + search across chapters.
    std::string text = engine.ExtractChapterText(0);
    CHECK(text.find("quick brown fox") != std::string::npos);
    CHECK(text.find("<p>") == std::string::npos);

    auto results = engine.Search("LAZY DOG");
    CHECK_EQ(results.size(), size_t(1));
    if (!results.empty()) {
        CHECK_EQ(results[0].chapterIndex, 1);
        CHECK_EQ(results[0].matchedText, std::string("lazy dog"));
        CHECK(!results[0].contextBefore.empty());
    }
    CHECK(engine.Search("LAZY DOG", /*caseSensitive=*/true).empty());

    engine.Close();
    CHECK(!engine.IsLoaded());
    CHECK_EQ(engine.GetChapterCount(), 0);
}

static void TestEPUBErrors() {
    EPUBEngine engine;
    std::vector<uint8_t> junk = {'n', 'o', 't', 'z', 'i', 'p'};
    CHECK(!engine.LoadFromMemory(junk));
    CHECK(!engine.GetLastError().empty());

    // Valid ZIP but no container.xml.
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    mz_zip_writer_init_heap(&zip, 0, 0);
    const char* data = "hello";
    mz_zip_writer_add_mem(&zip, "file.txt", data, 5, MZ_NO_COMPRESSION);
    void* buffer = nullptr;
    size_t size = 0;
    mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size);
    std::vector<uint8_t> notEpub(static_cast<uint8_t*>(buffer),
                                 static_cast<uint8_t*>(buffer) + size);
    mz_zip_writer_end(&zip);
    mz_free(buffer);

    CHECK(!engine.LoadFromMemory(notEpub));
    CHECK(engine.GetLastError().find("container.xml") != std::string::npos);
}

// ============================================================================
// TXT TESTS
// ============================================================================

static void TestTXT() {
    std::string text =
        "First paragraph line one\nline two.\n"
        "\n"
        "Second paragraph with <angle> & ampersand.\n"
        "\f"
        "Part two text.\n";

    TXTEngine engine;
    std::vector<uint8_t> data(text.begin(), text.end());
    CHECK(engine.LoadFromMemory(data));
    CHECK_EQ(engine.GetChapterCount(), 2);

    EBookChapter ch1 = engine.GetChapter(0);
    CHECK(ch1.content.find("<p>First paragraph line one line two.</p>") !=
          std::string::npos);
    CHECK(ch1.content.find("&lt;angle&gt; &amp; ampersand") != std::string::npos);

    EBookChapter ch2 = engine.GetChapter(1);
    CHECK(ch2.content.find("Part two text.") != std::string::npos);

    auto results = engine.Search("paragraph");
    CHECK_EQ(results.size(), size_t(2));

    engine.Close();
    CHECK(!engine.IsLoaded());
}

// ============================================================================
// REGISTRY
// ============================================================================

static void TestRegistry() {
    RegisterBuiltinEBookEngines();

    auto epub = CreateEBookEngineForFile("/books/novel.EPUB");
    CHECK(epub != nullptr);
    if (epub) CHECK(epub->GetFormat() == EBookFormat::EPUB);

    auto txt = CreateEBookEngineForFile("notes.txt");
    CHECK(txt != nullptr);
    if (txt) CHECK(txt->GetFormat() == EBookFormat::TXT);

    CHECK(CreateEBookEngineForFile("file.xyz") == nullptr);

    auto extensions = GetRegisteredEBookExtensions();
    CHECK_EQ(extensions.size(), size_t(2));
}

// ============================================================================

int main() {
    TestPathUtilities();
    TestEPUB();
    TestEPUBErrors();
    TestTXT();
    TestRegistry();

    std::printf("%s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAIL", checks, failures);
    return failures == 0 ? 0 : 1;
}
