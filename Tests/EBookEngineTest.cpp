// Tests/EBookEngineTest.cpp
// Unit tests for the EPUB, FB2, MOBI and TXT engines: builds synthetic books
// in memory (miniz writer for the EPUB, a hand-assembled PDB for the MOBI)
// and exercises metadata, spine, TOC, resources, cover, text extraction,
// search, and the engine registry.
// Version: 1.0.0
// Last Modified: 2026-07-03
// Author: UltraCanvas Framework

#include "EPUBEngine.h"
#include "FB2Engine.h"
#include "MOBIEngine.h"
#include "TXTEngine.h"

#include "miniz.h"

#include <cstdint>
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
// FB2 TESTS
// ============================================================================

static std::string MakeTestFB2() {
    return
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<FictionBook xmlns=\"http://www.gribuser.ru/xml/fictionbook/2.0\""
        " xmlns:l=\"http://www.w3.org/1999/xlink\">\n"
        " <description>\n"
        "  <title-info>\n"
        "   <genre>sf</genre>\n"
        "   <genre>adventure</genre>\n"
        "   <author><first-name>Ivan</first-name><middle-name>I.</middle-name>"
        "<last-name>Petrov</last-name></author>\n"
        "   <author><nickname>ghost</nickname></author>\n"
        "   <book-title>Test FB2 Book</book-title>\n"
        "   <annotation><p>A short annotation.</p></annotation>\n"
        "   <date>1999</date>\n"
        "   <coverpage><image l:href=\"#cover.jpg\"/></coverpage>\n"
        "   <lang>ru</lang>\n"
        "   <sequence name=\"Test Series\" number=\"3\"/>\n"
        "  </title-info>\n"
        "  <publish-info>\n"
        "   <publisher>Test Press</publisher>\n"
        "   <year>2001</year>\n"
        "   <isbn>123-456</isbn>\n"
        "  </publish-info>\n"
        " </description>\n"
        " <body>\n"
        "  <title><p>Test FB2 Book</p></title>\n"
        "  <epigraph><p>Body epigraph.</p></epigraph>\n"
        "  <section id=\"ch1\">\n"
        "   <title><p>Chapter One</p></title>\n"
        "   <p>Plain with <emphasis>italic</emphasis> and <strong>bold</strong>"
        " and <a l:href=\"#n1\" type=\"note\">1</a>.</p>\n"
        "   <empty-line/>\n"
        "   <subtitle>Sub heading</subtitle>\n"
        "   <poem><stanza><v>Verse line one</v><v>Verse line two</v></stanza>"
        "<text-author>Poet</text-author></poem>\n"
        "   <image l:href=\"#pic.png\" alt=\"A picture\"/>\n"
        "  </section>\n"
        "  <section>\n"
        "   <title><p>Chapter</p><p>Two</p></title>\n"
        "   <p>The quick brown fox jumps over the lazy dog.</p>\n"
        "   <cite><p>Quoted text.</p></cite>\n"
        "  </section>\n"
        " </body>\n"
        " <body name=\"notes\">\n"
        "  <title><p>Notes</p></title>\n"
        "  <section id=\"n1\"><title><p>1</p></title><p>The note text.</p></section>\n"
        " </body>\n"
        " <binary id=\"cover.jpg\" content-type=\"image/jpeg\">Q292ZXI=</binary>\n"
        " <binary id=\"pic.png\" content-type=\"image/png\">SGVs\nbG8=</binary>\n"
        "</FictionBook>\n";
}

static void TestFB2() {
    std::string xml = MakeTestFB2();

    FB2Engine engine;
    CHECK(engine.LoadFromMemory(std::vector<uint8_t>(xml.begin(), xml.end())));
    CHECK(engine.IsLoaded());
    CHECK(engine.GetFormat() == EBookFormat::FB2);

    // ---- metadata ----
    const EBookMetadata& meta = engine.GetMetadata();
    CHECK_EQ(meta.title, std::string("Test FB2 Book"));
    CHECK_EQ(meta.authors.size(), size_t(2));
    if (meta.authors.size() == 2) {
        CHECK_EQ(meta.authors[0], std::string("Ivan I. Petrov"));
        CHECK_EQ(meta.authors[1], std::string("ghost"));
    }
    CHECK_EQ(meta.language, std::string("ru"));
    CHECK_EQ(meta.publisher, std::string("Test Press"));
    CHECK_EQ(meta.publicationDate, std::string("2001"));   // publish year wins
    CHECK_EQ(meta.isbn, std::string("123-456"));
    CHECK_EQ(meta.subjects.size(), size_t(2));
    CHECK_EQ(meta.series, std::string("Test Series"));
    CHECK_EQ(meta.seriesIndex, 3);
    CHECK_EQ(meta.description, std::string("A short annotation."));
    CHECK(meta.hasCover);
    CHECK_EQ(meta.chapterCount, 3);   // 2 sections + notes body

    // ---- chapters / conversion ----
    CHECK_EQ(engine.GetChapterCount(), 3);

    EBookChapter ch1 = engine.GetChapter(0);
    CHECK_EQ(ch1.title, std::string("Chapter One"));
    CHECK_EQ(ch1.href, std::string("#ch1"));
    // Body title + epigraph land as the first chapter's preamble.
    CHECK(ch1.content.find("<h1>Test FB2 Book</h1>") != std::string::npos);
    CHECK(ch1.content.find("<blockquote class=\"epigraph\"><p>Body epigraph.</p></blockquote>")
          != std::string::npos);
    CHECK(ch1.content.find("<h2>Chapter One</h2>") != std::string::npos);
    CHECK(ch1.content.find("<em>italic</em>") != std::string::npos);
    CHECK(ch1.content.find("<strong>bold</strong>") != std::string::npos);
    CHECK(ch1.content.find("<a href=\"#n1\">1</a>") != std::string::npos);
    CHECK(ch1.content.find("<p class=\"empty-line\"></p>") != std::string::npos);
    CHECK(ch1.content.find("<h4 class=\"subtitle\">Sub heading</h4>") != std::string::npos);
    CHECK(ch1.content.find("<div class=\"poem\">") != std::string::npos);
    CHECK(ch1.content.find("<p class=\"verse\">Verse line one</p>") != std::string::npos);
    CHECK(ch1.content.find("<p class=\"text-author\">Poet</p>") != std::string::npos);
    CHECK(ch1.content.find("<img src=\"#pic.png\" alt=\"A picture\"/>") != std::string::npos);

    EBookChapter ch2 = engine.GetChapter(1);
    CHECK_EQ(ch2.title, std::string("Chapter Two"));   // multi-<p> title joined
    CHECK(ch2.content.find("<h2>Chapter<br/>Two</h2>") != std::string::npos);
    CHECK(ch2.content.find("<blockquote class=\"cite\"><p>Quoted text.</p></blockquote>")
          != std::string::npos);

    EBookChapter notes = engine.GetChapter(2);
    CHECK_EQ(notes.title, std::string("Notes"));
    CHECK(notes.content.find("The note text.") != std::string::npos);

    CHECK(engine.GetChapter(3).content.empty());

    // ---- TOC ----
    const auto& toc = engine.GetTableOfContents();
    CHECK_EQ(toc.size(), size_t(3));
    if (toc.size() == 3) {
        CHECK_EQ(toc[0].title, std::string("Chapter One"));
        CHECK_EQ(toc[0].pageNumber, 0);
        CHECK_EQ(toc[2].pageNumber, 2);
    }

    // ---- resources / cover ----
    std::vector<uint8_t> pic = engine.GetResource("#pic.png");
    CHECK_EQ(std::string(pic.begin(), pic.end()), std::string("Hello"));
    CHECK(!engine.GetResource("pic.png").empty());   // bare id works too
    CHECK(engine.GetResource("#missing").empty());

    std::vector<uint8_t> cover = engine.GetCoverImage();
    CHECK_EQ(std::string(cover.begin(), cover.end()), std::string("Cover"));

    // ---- stylesheet / text / search ----
    CHECK(engine.GetStylesheets().find(".verse") != std::string::npos);

    std::string text = engine.ExtractChapterText(0);
    CHECK(text.find("italic") != std::string::npos);
    CHECK(text.find("<em>") == std::string::npos);

    auto results = engine.Search("quick brown");
    CHECK_EQ(results.size(), size_t(1));
    if (!results.empty()) CHECK_EQ(results[0].chapterIndex, 1);

    engine.Close();
    CHECK(!engine.IsLoaded());
    CHECK_EQ(engine.GetChapterCount(), 0);
}

// A book hidden inside a single wrapper section must still chapterize.
static void TestFB2SingleWrapperSection() {
    std::string xml =
        "<?xml version=\"1.0\"?>\n"
        "<FictionBook>\n"
        " <body>\n"
        "  <section>\n"
        "   <title><p>Part One</p></title>\n"
        "   <section><title><p>First</p></title><p>Alpha.</p></section>\n"
        "   <section><title><p>Second</p></title><p>Beta.</p></section>\n"
        "  </section>\n"
        " </body>\n"
        "</FictionBook>\n";

    FB2Engine engine;
    CHECK(engine.LoadFromMemory(std::vector<uint8_t>(xml.begin(), xml.end())));
    CHECK_EQ(engine.GetChapterCount(), 2);

    EBookChapter first = engine.GetChapter(0);
    CHECK_EQ(first.title, std::string("First"));
    // Wrapper title precedes the first chapter's own content.
    CHECK(first.content.find("Part One") != std::string::npos);
    CHECK(first.content.find("Alpha.") != std::string::npos);
    CHECK_EQ(engine.GetChapter(1).title, std::string("Second"));
}

static void TestFB2Zip() {
    std::string xml = MakeTestFB2();

    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    mz_zip_writer_init_heap(&zip, 0, 0);
    AddFile(zip, "book.fb2", xml);
    void* buf = nullptr;
    size_t size = 0;
    mz_zip_writer_finalize_heap_archive(&zip, &buf, &size);
    std::vector<uint8_t> data(static_cast<uint8_t*>(buf),
                              static_cast<uint8_t*>(buf) + size);
    mz_zip_writer_end(&zip);

    FB2Engine engine;
    CHECK(engine.LoadFromMemory(std::move(data)));
    CHECK_EQ(engine.GetChapterCount(), 3);
    CHECK_EQ(engine.GetMetadata().title, std::string("Test FB2 Book"));
}

static void TestFB2Encoding() {
    // windows-1251 "Привет" + "ё" inside a <p>.
    std::string head =
        "<?xml version=\"1.0\" encoding=\"windows-1251\"?><p>";
    std::vector<uint8_t> data(head.begin(), head.end());
    const uint8_t cp1251[] = {0xCF, 0xF0, 0xE8, 0xE2, 0xE5, 0xF2, 0x20, 0xB8};
    data.insert(data.end(), std::begin(cp1251), std::end(cp1251));
    std::string tail = "</p>";
    data.insert(data.end(), tail.begin(), tail.end());

    std::string utf8 = FB2Engine::ToUTF8(data);
    CHECK(utf8.find("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82") !=
          std::string::npos);                        // Привет
    CHECK(utf8.find("\xD1\x91") != std::string::npos);   // ё

    // UTF-8 input passes through untouched.
    std::string plain = "<?xml version=\"1.0\"?><p>abc</p>";
    CHECK_EQ(FB2Engine::ToUTF8(std::vector<uint8_t>(plain.begin(), plain.end())),
             plain);

    // UTF-16LE with BOM.
    std::vector<uint8_t> utf16 = {0xFF, 0xFE, 'a', 0x00, 'b', 0x00};
    CHECK_EQ(FB2Engine::ToUTF8(utf16), std::string("ab"));

    // Base64 (with embedded line break and padding).
    std::vector<uint8_t> decoded = FB2Engine::DecodeBase64("SGVsbG8sIHdv\ncmxkIQ==");
    CHECK_EQ(std::string(decoded.begin(), decoded.end()),
             std::string("Hello, world!"));
    CHECK(FB2Engine::DecodeBase64("").empty());
}

static void TestFB2Errors() {
    FB2Engine engine;

    std::string junk = "<html><body>not fb2</body></html>";
    CHECK(!engine.LoadFromMemory(std::vector<uint8_t>(junk.begin(), junk.end())));
    CHECK(engine.GetLastError().find("FictionBook") != std::string::npos);

    std::string empty = "<?xml version=\"1.0\"?><FictionBook></FictionBook>";
    CHECK(!engine.LoadFromMemory(std::vector<uint8_t>(empty.begin(), empty.end())));
    CHECK(!engine.GetLastError().empty());
}

// ============================================================================
// MOBI TESTS
// ============================================================================

static void PutBE16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x & 0xFF));
}
static void PutBE32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x >> 24));
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x & 0xFF));
}
static void SetBE32(std::vector<uint8_t>& v, size_t at, uint32_t x) {
    v[at] = static_cast<uint8_t>(x >> 24);
    v[at + 1] = static_cast<uint8_t>(x >> 16);
    v[at + 2] = static_cast<uint8_t>(x >> 8);
    v[at + 3] = static_cast<uint8_t>(x & 0xFF);
}

// Assemble a minimal but valid MOBI6 file: PDB header + 3 records
// (record 0 with PalmDOC/MOBI/EXTH headers, one ASCII text record, one image).
static std::vector<uint8_t> MakeTestMOBI() {
    // ASCII HTML decodes to itself under PalmDOC (all bytes are 0x20..0x7E
    // literals), so compression=2 exercises the decompressor without needing
    // a compressor here.
    std::string text =
        "<html><body>"
        "<h1>Chapter One</h1><p>The quick brown fox.</p>"
        "<mbp:pagebreak/>"
        "<h1>Chapter Two</h1><p>Jumps over the lazy dog.</p>"
        "<img recindex=\"00001\"/>"
        "</body></html>";

    // --- MOBI header (232 bytes) ---
    std::vector<uint8_t> mobi(232, 0);
    std::memcpy(&mobi[0], "MOBI", 4);
    SetBE32(mobi, 4, 232);      // header length
    SetBE32(mobi, 8, 2);        // mobiType = Mobipocket book
    SetBE32(mobi, 12, 1252);    // textEncoding = cp1252
    SetBE32(mobi, 20, 6);       // fileVersion (MOBI6)
    SetBE32(mobi, 108, 2);      // firstImageIndex = record 2
    SetBE32(mobi, 128, 0x40);   // exthFlags: has EXTH
    // fullNameOffset/Length filled once the record-0 layout is known.

    // --- EXTH ---
    std::vector<uint8_t> exthRecs;
    auto addExth = [&](uint32_t type, const std::string& s) {
        PutBE32(exthRecs, type);
        PutBE32(exthRecs, static_cast<uint32_t>(8 + s.size()));
        exthRecs.insert(exthRecs.end(), s.begin(), s.end());
    };
    addExth(100, "Test Author");
    addExth(101, "Test Press");
    addExth(104, "978-0-00-000000-0");
    addExth(106, "2021");
    addExth(524, "en");
    // Cover offset (type 201): BE32 = 0 → first image record is the cover.
    PutBE32(exthRecs, 201);
    PutBE32(exthRecs, 12);
    PutBE32(exthRecs, 0);
    uint32_t exthCount = 6;

    std::vector<uint8_t> exth;
    exth.insert(exth.end(), {'E', 'X', 'T', 'H'});
    PutBE32(exth, static_cast<uint32_t>(12 + exthRecs.size()));
    PutBE32(exth, exthCount);
    exth.insert(exth.end(), exthRecs.begin(), exthRecs.end());
    while (exth.size() % 4 != 0) exth.push_back(0);   // pad

    std::string fullName = "Test MOBI Book";

    // --- record 0 = PalmDOC(16) + MOBI(232) + EXTH + fullName ---
    std::vector<uint8_t> rec0;
    PutBE16(rec0, 2);                                   // compression = PalmDOC
    PutBE16(rec0, 0);                                   // unused
    PutBE32(rec0, static_cast<uint32_t>(text.size()));  // text length
    PutBE16(rec0, 1);                                   // text record count
    PutBE16(rec0, 4096);                               // record size
    PutBE32(rec0, 0);                                   // encryption + unused

    uint32_t fullNameOffset = static_cast<uint32_t>(16 + mobi.size() + exth.size());
    SetBE32(mobi, 84, fullNameOffset);
    SetBE32(mobi, 88, static_cast<uint32_t>(fullName.size()));

    rec0.insert(rec0.end(), mobi.begin(), mobi.end());
    rec0.insert(rec0.end(), exth.begin(), exth.end());
    rec0.insert(rec0.end(), fullName.begin(), fullName.end());

    std::vector<uint8_t> rec1(text.begin(), text.end());
    std::vector<uint8_t> rec2 = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 'i', 'm'};

    std::vector<std::vector<uint8_t>> recs = {rec0, rec1, rec2};

    // --- PDB header (78 bytes) + record list ---
    std::vector<uint8_t> file(78, 0);
    std::memcpy(&file[0], "TestBook", 8);
    std::memcpy(&file[60], "BOOK", 4);
    std::memcpy(&file[64], "MOBI", 4);
    file[76] = 0;
    file[77] = static_cast<uint8_t>(recs.size());   // record count = 3

    size_t listSize = recs.size() * 8;
    size_t dataStart = 78 + listSize;
    std::vector<uint32_t> offsets;
    size_t cursor = dataStart;
    for (const auto& r : recs) {
        offsets.push_back(static_cast<uint32_t>(cursor));
        cursor += r.size();
    }
    for (size_t i = 0; i < recs.size(); ++i) {
        PutBE32(file, offsets[i]);
        file.push_back(0);                 // attributes
        file.push_back(0);                 // uid[3]
        file.push_back(0);
        file.push_back(static_cast<uint8_t>(i));
    }
    for (const auto& r : recs) file.insert(file.end(), r.begin(), r.end());
    return file;
}

static void TestMOBI() {
    std::vector<uint8_t> data = MakeTestMOBI();

    MOBIEngine engine;
    CHECK(engine.LoadFromMemory(data));
    CHECK(engine.IsLoaded());
    CHECK(engine.GetFormat() == EBookFormat::MOBI);

    const EBookMetadata& meta = engine.GetMetadata();
    CHECK_EQ(meta.title, std::string("Test MOBI Book"));
    CHECK_EQ(meta.authors.size(), size_t(1));
    if (!meta.authors.empty()) CHECK_EQ(meta.authors[0], std::string("Test Author"));
    CHECK_EQ(meta.publisher, std::string("Test Press"));
    CHECK_EQ(meta.isbn, std::string("978-0-00-000000-0"));
    CHECK_EQ(meta.publicationDate, std::string("2021"));
    CHECK_EQ(meta.language, std::string("en"));
    CHECK(meta.hasCover);

    // Two chapters split on the page break, titled from their headings.
    CHECK_EQ(engine.GetChapterCount(), 2);
    EBookChapter ch1 = engine.GetChapter(0);
    CHECK_EQ(ch1.title, std::string("Chapter One"));
    CHECK(ch1.content.find("The quick brown fox.") != std::string::npos);

    EBookChapter ch2 = engine.GetChapter(1);
    CHECK_EQ(ch2.title, std::string("Chapter Two"));
    CHECK(ch2.content.find("Jumps over the lazy dog.") != std::string::npos);
    // recindex rewritten to a resolvable resource href.
    CHECK(ch2.content.find("src=\"mobiimg/1\"") != std::string::npos);
    CHECK(ch2.content.find("recindex") == std::string::npos);

    // TOC mirrors the chapters.
    const auto& toc = engine.GetTableOfContents();
    CHECK_EQ(toc.size(), size_t(2));

    // Image resource + cover.
    std::vector<uint8_t> img = engine.GetResource("mobiimg/1");
    CHECK_EQ(img.size(), size_t(10));
    CHECK(engine.GetResource("mobiimg/9").empty());
    CHECK_EQ(engine.GetCoverImage().size(), size_t(10));

    // Search + plain text.
    std::string text = engine.ExtractChapterText(0);
    CHECK(text.find("quick brown fox") != std::string::npos);
    CHECK(text.find("<h1>") == std::string::npos);
    auto results = engine.Search("lazy dog");
    CHECK_EQ(results.size(), size_t(1));
    if (!results.empty()) CHECK_EQ(results[0].chapterIndex, 1);

    engine.Close();
    CHECK(!engine.IsLoaded());
    CHECK_EQ(engine.GetChapterCount(), 0);
}

static void TestMOBIPalmDOC() {
    // Literal "abc" then a length-3 / distance-3 back-reference → "abcabc".
    std::vector<uint8_t> comp = {'a', 'b', 'c', 0x80, 0x18};
    auto out = MOBIEngine::DecompressPalmDOC(comp.data(), comp.size());
    CHECK_EQ(std::string(out.begin(), out.end()), std::string("abcabc"));

    // 0xC1 → space + (0xC1 ^ 0x80) = 'A'.
    std::vector<uint8_t> sp = {'H', 'i', 0xC1};
    auto out2 = MOBIEngine::DecompressPalmDOC(sp.data(), sp.size());
    CHECK_EQ(std::string(out2.begin(), out2.end()), std::string("Hi A"));

    // Literal-run marker (0x03 copies the next 3 bytes verbatim).
    std::vector<uint8_t> run = {0x03, 'X', 'Y', 'Z'};
    auto out3 = MOBIEngine::DecompressPalmDOC(run.data(), run.size());
    CHECK_EQ(std::string(out3.begin(), out3.end()), std::string("XYZ"));

    // cp1252: 0x92 (right single quote) → U+2019 (UTF-8 e2 80 99).
    std::string cp = "it";
    cp += static_cast<char>(0x92);
    cp += "s";
    CHECK_EQ(MOBIEngine::Cp1252ToUTF8(cp),
             std::string("it\xE2\x80\x99s"));
}

static void TestMOBIErrors() {
    MOBIEngine engine;
    std::vector<uint8_t> junk(200, 0);
    CHECK(!engine.LoadFromMemory(junk));
    CHECK(engine.GetLastError().find("Not a MOBI") != std::string::npos);
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

    auto fb2 = CreateEBookEngineForFile("kniga.fb2");
    CHECK(fb2 != nullptr);
    if (fb2) CHECK(fb2->GetFormat() == EBookFormat::FB2);

    // Compound extension resolves to the FB2 engine, not a "zip" one.
    auto fb2zip = CreateEBookEngineForFile("/books/kniga.FB2.ZIP");
    CHECK(fb2zip != nullptr);
    if (fb2zip) CHECK(fb2zip->GetFormat() == EBookFormat::FB2);

    auto mobi = CreateEBookEngineForFile("book.mobi");
    CHECK(mobi != nullptr);
    if (mobi) CHECK(mobi->GetFormat() == EBookFormat::MOBI);
    CHECK(CreateEBookEngineForFile("book.azw3") != nullptr);
    CHECK(CreateEBookEngineForFile("old.prc") != nullptr);

    CHECK(CreateEBookEngineForFile("file.xyz") == nullptr);

    auto extensions = GetRegisteredEBookExtensions();
    // epub, fb2, fb2.zip, mobi, prc, azw, azw3, txt
    CHECK_EQ(extensions.size(), size_t(8));
}

// ============================================================================

int main() {
    TestPathUtilities();
    TestEPUB();
    TestEPUBErrors();
    TestFB2();
    TestFB2SingleWrapperSection();
    TestFB2Zip();
    TestFB2Encoding();
    TestFB2Errors();
    TestMOBI();
    TestMOBIPalmDOC();
    TestMOBIErrors();
    TestTXT();
    TestRegistry();

    std::printf("%s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAIL", checks, failures);
    return failures == 0 ? 0 : 1;
}
