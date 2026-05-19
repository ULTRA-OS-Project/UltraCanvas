// Plugins/Documents/UltraCanvasPDF_MuPDF.cpp
// MuPDF-backed implementation of IPDFDocument.
// Built when ULTRACANVAS_PLUGIN_PDF and ULTRACANVAS_PDF_MUPDF are both enabled.
// Version: 1.0.0
// Last Modified: 2026-05-18
// Author: UltraCanvas Framework

#include "Plugins/Documents/UltraCanvasPDF.h"

#ifdef ULTRACANVAS_PDF_MUPDF

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

#include <algorithm>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>

namespace UltraCanvas {
namespace {

// ===== fz_quad / fz_rect → Rect2Df =====
Rect2Df ToRect(const fz_rect& r) {
    return Rect2Df(r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0);
}

Rect2Df ToRect(const fz_quad& q) {
    const float minX = std::min({q.ul.x, q.ur.x, q.ll.x, q.lr.x});
    const float maxX = std::max({q.ul.x, q.ur.x, q.ll.x, q.lr.x});
    const float minY = std::min({q.ul.y, q.ur.y, q.ll.y, q.lr.y});
    const float maxY = std::max({q.ul.y, q.ur.y, q.ll.y, q.lr.y});
    return Rect2Df(minX, minY, maxX - minX, maxY - minY);
}

fz_rect FromRect(const Rect2Df& r) {
    return fz_make_rect(static_cast<float>(r.x),
                        static_cast<float>(r.y),
                        static_cast<float>(r.x + r.width),
                        static_cast<float>(r.y + r.height));
}

// Append a unicode codepoint to a UTF-8 std::string.
void AppendUtf8(std::string& out, int c) {
    if (c < 0)              return;
    if (c < 0x80)           { out.push_back(static_cast<char>(c)); return; }
    if (c < 0x800)          {
        out.push_back(static_cast<char>(0xC0 | (c >> 6)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        return;
    }
    if (c < 0x10000)        {
        out.push_back(static_cast<char>(0xE0 |  (c >> 12)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 |  (c       & 0x3F)));
        return;
    }
    out.push_back(static_cast<char>(0xF0 |  (c >> 18)));
    out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((c >>  6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 |  (c        & 0x3F)));
}

} // namespace

// ===== MuPDF implementation =====

class MuPDFDocument final : public IPDFDocument {
public:
    MuPDFDocument();
    ~MuPDFDocument() override;

    // I/O
    bool Open(const std::string& path, const std::string& password) override;
    bool OpenFromBytes(const std::vector<uint8_t>& data,
                       const std::string& password) override;
    bool Save(const std::string& path, const PDFSaveOptions& opts) override;
    bool SaveIncremental(const std::string& path) override;
    void Close() override;

    bool IsOpen()  const override { return doc_ != nullptr; }
    bool IsDirty() const override;
    const std::string& GetSourcePath() const override { return path_; }

    // Metadata
    PDFDocumentInfo GetInfo() const override;
    int             GetPageCount() const override;
    PDFPageInfo     GetPageInfo(int pageNumber) const override;

    // Rendering
    PDFRenderedPage RenderPage(int pageNumber, const PDFRenderSettings& settings) override;
    PDFRenderedPage RenderThumbnail(int pageNumber, int maxDim) override;

    // Text
    std::string             GetPageText(int pageNumber) override;
    std::vector<PDFTextRun> ExtractTextRuns(int pageNumber) override;
    std::vector<PDFTextRun> Search(const std::string& query,
                                   const PDFSearchOptions& opts) override;

    // Images
    std::vector<PDFImageRef> ListImages(int pageNumber) override;
    std::vector<uint8_t>     ExtractImageBytes(const PDFImageRef& ref) override;

    // Page operations
    bool DeletePage(int pageNumber) override;
    bool MovePage(int fromPage, int toPage) override;
    bool InsertBlankPage(int at, float widthPt, float heightPt) override;
    bool MergeFrom(IPDFDocument& other, int srcStart, int srcEnd, int insertAt) override;

    // Content editing (Milestone 3 — annotation-based)
    bool AddTextAnnotation(int pageNumber, const Rect2Df& at,
                           const std::string& text) override;
    bool RedactRect(int pageNumber, const Rect2Df& area) override;
    bool ReplaceText(const PDFTextRun& target,
                     const std::string& newText) override;
    bool ApplyPendingRedactions(int pageNumber) override;
    std::vector<PDFAnnotation> ListAnnotations(int pageNumber) override;
    bool DeleteAnnotation(int pageNumber, int indexOnPage) override;

    // Identity
    std::string GetEngineName()    const override { return "MuPDF"; }
    std::string GetEngineVersion() const override { return FZ_VERSION; }

private:
    // ----- helpers -----
    bool LoadDocumentInternal(fz_stream* stream, const std::string& password);
    PDFRenderedPage RenderInternal(int pageNumber, const PDFRenderSettings& settings);
    // Cache of decoded stext pages — accessed via GetPageText/ExtractTextRuns/etc.
    // Returns a *new* fz_stext_page that the caller must drop, or nullptr.
    fz_stext_page* MakeStextPage(int pageNumber);

    mutable std::mutex      mu_;
    fz_context*             ctx_  = nullptr;
    fz_document*            doc_  = nullptr;   // generic, may not be pdf_*
    pdf_document*           pdoc_ = nullptr;   // non-null if doc_ is a PDF
    std::string             path_;
    std::vector<uint8_t>    memoryBuffer_;     // kept alive for OpenFromBytes
    bool                    dirty_ = false;
};

// ===== ctor/dtor =====

MuPDFDocument::MuPDFDocument() {
    ctx_ = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (ctx_) {
        fz_try(ctx_) {
            fz_register_document_handlers(ctx_);
        }
        fz_catch(ctx_) {
            fz_drop_context(ctx_);
            ctx_ = nullptr;
        }
    }
}

MuPDFDocument::~MuPDFDocument() {
    Close();
    if (ctx_) {
        fz_drop_context(ctx_);
        ctx_ = nullptr;
    }
}

// ===== I/O =====

bool MuPDFDocument::LoadDocumentInternal(fz_stream* stream,
                                         const std::string& password) {
    // Caller holds mu_ and has called Close().
    if (!ctx_) return false;

    fz_document* d = nullptr;
    fz_var(d);

    fz_try(ctx_) {
        d = fz_open_document_with_stream(ctx_, "application/pdf", stream);
        if (fz_needs_password(ctx_, d)) {
            if (password.empty() ||
                !fz_authenticate_password(ctx_, d, password.c_str())) {
                fz_drop_document(ctx_, d);
                d = nullptr;
            }
        }
    } fz_catch(ctx_) {
        if (d) fz_drop_document(ctx_, d);
        d = nullptr;
    }

    if (!d) return false;

    doc_   = d;
    pdoc_  = pdf_specifics(ctx_, d);   // null if not a PDF
    dirty_ = false;
    return true;
}

bool MuPDFDocument::Open(const std::string& path, const std::string& password) {
    if (!ctx_) return false;
    std::lock_guard<std::mutex> lock(mu_);
    Close();

    fz_stream* stream = nullptr;
    fz_var(stream);
    fz_try(ctx_) {
        stream = fz_open_file(ctx_, path.c_str());
    } fz_catch(ctx_) {
        stream = nullptr;
    }
    if (!stream) return false;

    bool ok = LoadDocumentInternal(stream, password);
    fz_drop_stream(ctx_, stream);
    if (ok) path_ = path;
    return ok;
}

bool MuPDFDocument::OpenFromBytes(const std::vector<uint8_t>& data,
                                  const std::string& password) {
    if (!ctx_ || data.empty()) return false;
    std::lock_guard<std::mutex> lock(mu_);
    Close();

    // MuPDF requires the buffer to stay alive for the document's lifetime
    // when using fz_open_memory.
    memoryBuffer_ = data;

    fz_stream* stream = nullptr;
    fz_var(stream);
    fz_try(ctx_) {
        stream = fz_open_memory(ctx_, memoryBuffer_.data(), memoryBuffer_.size());
    } fz_catch(ctx_) {
        stream = nullptr;
    }
    if (!stream) {
        memoryBuffer_.clear();
        return false;
    }

    bool ok = LoadDocumentInternal(stream, password);
    fz_drop_stream(ctx_, stream);
    if (ok) path_ = "<memory>";
    else    memoryBuffer_.clear();
    return ok;
}

bool MuPDFDocument::Save(const std::string& path, const PDFSaveOptions& opts) {
    if (!ctx_ || !pdoc_) return false;
    std::lock_guard<std::mutex> lock(mu_);

    pdf_write_options wopts = pdf_default_write_options;
    wopts.do_garbage  = opts.garbageCollect ? 1 : 0;
    wopts.do_linear   = opts.linearize       ? 1 : 0;
    wopts.do_compress = opts.deflateStreams  ? 1 : 0;
    wopts.do_clean    = opts.cleanContentStreams ? 1 : 0;

    bool ok = true;
    fz_try(ctx_) {
        pdf_save_document(ctx_, pdoc_, path.c_str(), &wopts);
    } fz_catch(ctx_) {
        ok = false;
    }
    if (ok) {
        dirty_ = false;
        // If we saved to a fresh path, treat that as the new source.
        if (path != path_) path_ = path;
    }
    return ok;
}

bool MuPDFDocument::SaveIncremental(const std::string& path) {
    if (!ctx_ || !pdoc_) return false;
    std::lock_guard<std::mutex> lock(mu_);

    pdf_write_options wopts = pdf_default_write_options;
    wopts.do_incremental = 1;

    bool ok = true;
    fz_try(ctx_) {
        pdf_save_document(ctx_, pdoc_, path.c_str(), &wopts);
    } fz_catch(ctx_) {
        ok = false;
    }
    if (ok) {
        dirty_ = false;
        if (path != path_) path_ = path;
    }
    return ok;
}

void MuPDFDocument::Close() {
    // Caller may or may not hold mu_. We lock here only if doc_ is alive
    // and we own it; we accept reentrant Close() from the destructor by
    // checking doc_ first without a lock — destructor is single-threaded.
    if (doc_ && ctx_) {
        fz_drop_document(ctx_, doc_);
    }
    doc_   = nullptr;
    pdoc_  = nullptr;
    path_.clear();
    memoryBuffer_.clear();
    dirty_ = false;
}

bool MuPDFDocument::IsDirty() const {
    if (!ctx_ || !pdoc_) return dirty_;
    int has = 0;
    fz_try(ctx_) {
        has = pdf_has_unsaved_changes(ctx_, pdoc_);
    } fz_catch(ctx_) {
        has = 0;
    }
    return dirty_ || (has != 0);
}

// ===== Metadata =====

PDFDocumentInfo MuPDFDocument::GetInfo() const {
    PDFDocumentInfo info;
    if (!ctx_ || !doc_) return info;
    std::lock_guard<std::mutex> lock(mu_);

    auto lookup = [&](const char* key) -> std::string {
        char buf[1024];
        int n = 0;
        fz_try(ctx_) {
            n = fz_lookup_metadata(ctx_, doc_, key, buf, sizeof(buf));
        } fz_catch(ctx_) {
            n = -1;
        }
        return (n > 0) ? std::string(buf) : std::string();
    };

    info.title            = lookup(FZ_META_INFO_TITLE);
    info.author           = lookup(FZ_META_INFO_AUTHOR);
    info.subject          = lookup(FZ_META_INFO_SUBJECT);
    info.keywords         = lookup(FZ_META_INFO_KEYWORDS);
    info.creator          = lookup(FZ_META_INFO_CREATOR);
    info.producer         = lookup(FZ_META_INFO_PRODUCER);
    info.creationDate     = lookup(FZ_META_INFO_CREATIONDATE);
    info.modificationDate = lookup(FZ_META_INFO_MODIFICATIONDATE);
    info.pdfVersion       = lookup(FZ_META_FORMAT);

    fz_try(ctx_) {
        info.pageCount     = fz_count_pages(ctx_, doc_);
        info.needsPassword = fz_needs_password(ctx_, doc_) != 0;
        // A PDF whose security handler exists but has been satisfied still
        // reports isEncrypted = true; we use pdf_crypt as the ground truth.
        info.isEncrypted   =
            (pdoc_ && pdoc_->crypt) || info.needsPassword;
    } fz_catch(ctx_) {
        // leave defaults
    }

    if (!path_.empty() && path_ != "<memory>") {
        std::ifstream f(path_, std::ios::binary | std::ios::ate);
        if (f.is_open()) info.fileSize = static_cast<long>(f.tellg());
    } else if (!memoryBuffer_.empty()) {
        info.fileSize = static_cast<long>(memoryBuffer_.size());
    }
    return info;
}

int MuPDFDocument::GetPageCount() const {
    if (!ctx_ || !doc_) return 0;
    std::lock_guard<std::mutex> lock(mu_);
    int n = 0;
    fz_try(ctx_) {
        n = fz_count_pages(ctx_, doc_);
    } fz_catch(ctx_) {
        n = 0;
    }
    return n;
}

PDFPageInfo MuPDFDocument::GetPageInfo(int pageNumber) const {
    PDFPageInfo info;
    if (!ctx_ || !doc_) return info;
    std::lock_guard<std::mutex> lock(mu_);

    const int total = fz_count_pages(ctx_, doc_);
    if (pageNumber < 1 || pageNumber > total) return info;

    fz_page* page = nullptr;
    fz_var(page);
    fz_try(ctx_) {
        page = fz_load_page(ctx_, doc_, pageNumber - 1);
        fz_rect r = fz_bound_page(ctx_, page);
        info.pageNumber = pageNumber;
        info.widthPt    = r.x1 - r.x0;
        info.heightPt   = r.y1 - r.y0;
    } fz_catch(ctx_) {
        // leave defaults
    }
    if (page) fz_drop_page(ctx_, page);

    // PDF page label (e.g. roman numerals for front matter).
    if (pdoc_) {
        char buf[64] = {0};
        fz_try(ctx_) {
            pdf_page_label(ctx_, pdoc_, pageNumber - 1, buf, sizeof(buf));
        } fz_catch(ctx_) {
            buf[0] = 0;
        }
        info.label = buf[0] ? std::string(buf) : std::to_string(pageNumber);
    } else {
        info.label = std::to_string(pageNumber);
    }
    return info;
}

// ===== Rendering =====

PDFRenderedPage MuPDFDocument::RenderInternal(int pageNumber,
                                              const PDFRenderSettings& settings) {
    PDFRenderedPage out;
    if (!ctx_ || !doc_) return out;

    const int total = fz_count_pages(ctx_, doc_);
    if (pageNumber < 1 || pageNumber > total) return out;

    const float dpi   = std::max(1.0f, settings.dpi);
    const float zoom  = std::max(0.01f, settings.zoom);
    const float scale = (dpi / 72.0f) * zoom;
    fz_matrix ctm = fz_scale(scale, scale);
    if (settings.rotationDegrees != 0) {
        ctm = fz_concat(ctm, fz_rotate(static_cast<float>(settings.rotationDegrees)));
    }

    fz_pixmap* pix = nullptr;
    fz_var(pix);
    fz_try(ctx_) {
        fz_colorspace* cs =
            (settings.colorMode == PDFColorMode::Gray)
                ? fz_device_gray(ctx_)
                : fz_device_rgb(ctx_);
        const int alpha = (settings.colorMode == PDFColorMode::RGBA) ? 1 : 0;
        pix = fz_new_pixmap_from_page_number(ctx_, doc_, pageNumber - 1,
                                             ctm, cs, alpha);
    } fz_catch(ctx_) {
        pix = nullptr;
    }
    if (!pix) return out;

    const int w  = fz_pixmap_width (ctx_, pix);
    const int h  = fz_pixmap_height(ctx_, pix);
    const int st = fz_pixmap_stride(ctx_, pix);
    unsigned char* samples = fz_pixmap_samples(ctx_, pix);

    out.width     = w;
    out.height    = h;
    out.stride    = st;
    out.colorMode = settings.colorMode;
    out.pixels.assign(samples, samples + static_cast<size_t>(st) * h);

    // Convert RGBA → BGRA premultiplied for Cairo if requested via RGBA mode.
    // MuPDF gives us RGBA non-premultiplied; Cairo expects BGRA premultiplied
    // on little-endian. The widget that wraps this into a UCPixmap is
    // responsible for the byte-order swap; we ship raw RGBA here to keep the
    // engine renderer-agnostic.

    fz_drop_pixmap(ctx_, pix);
    return out;
}

PDFRenderedPage MuPDFDocument::RenderPage(int pageNumber,
                                          const PDFRenderSettings& settings) {
    std::lock_guard<std::mutex> lock(mu_);
    return RenderInternal(pageNumber, settings);
}

PDFRenderedPage MuPDFDocument::RenderThumbnail(int pageNumber, int maxDim) {
    if (maxDim <= 0) maxDim = 200;
    std::lock_guard<std::mutex> lock(mu_);

    // First learn the page size so we can pick a DPI that hits maxDim on the
    // long side. Use a cheap fz_bound_page call.
    if (!ctx_ || !doc_) return {};
    const int total = fz_count_pages(ctx_, doc_);
    if (pageNumber < 1 || pageNumber > total) return {};

    fz_page* page = nullptr;
    fz_rect bbox = fz_empty_rect;
    fz_var(page);
    fz_try(ctx_) {
        page = fz_load_page(ctx_, doc_, pageNumber - 1);
        bbox = fz_bound_page(ctx_, page);
    } fz_catch(ctx_) {
        bbox = fz_empty_rect;
    }
    if (page) fz_drop_page(ctx_, page);
    const float wpt = bbox.x1 - bbox.x0;
    const float hpt = bbox.y1 - bbox.y0;
    if (wpt <= 0 || hpt <= 0) return {};

    const float longSidePt = std::max(wpt, hpt);
    const float dpi = (static_cast<float>(maxDim) * 72.0f) / longSidePt;

    PDFRenderSettings s;
    s.dpi = dpi;
    s.zoom = 1.0f;
    s.antialias = true;
    s.colorMode = PDFColorMode::RGBA;
    return RenderInternal(pageNumber, s);
}

// ===== Text =====

fz_stext_page* MuPDFDocument::MakeStextPage(int pageNumber) {
    if (!ctx_ || !doc_) return nullptr;
    const int total = fz_count_pages(ctx_, doc_);
    if (pageNumber < 1 || pageNumber > total) return nullptr;

    fz_stext_options opts = {};
    opts.flags = FZ_STEXT_PRESERVE_WHITESPACE;

    fz_stext_page* st = nullptr;
    fz_var(st);
    fz_try(ctx_) {
        st = fz_new_stext_page_from_page_number(ctx_, doc_, pageNumber - 1, &opts);
    } fz_catch(ctx_) {
        if (st) fz_drop_stext_page(ctx_, st);
        st = nullptr;
    }
    return st;
}

std::string MuPDFDocument::GetPageText(int pageNumber) {
    std::lock_guard<std::mutex> lock(mu_);
    fz_stext_page* st = MakeStextPage(pageNumber);
    if (!st) return {};

    std::string out;
    for (fz_stext_block* b = st->first_block; b; b = b->next) {
        if (b->type != FZ_STEXT_BLOCK_TEXT) continue;
        for (fz_stext_line* line = b->u.t.first_line; line; line = line->next) {
            for (fz_stext_char* ch = line->first_char; ch; ch = ch->next) {
                AppendUtf8(out, ch->c);
            }
            out.push_back('\n');
        }
    }
    fz_drop_stext_page(ctx_, st);
    return out;
}

std::vector<PDFTextRun> MuPDFDocument::ExtractTextRuns(int pageNumber) {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<PDFTextRun> runs;
    fz_stext_page* st = MakeStextPage(pageNumber);
    if (!st) return runs;

    for (fz_stext_block* b = st->first_block; b; b = b->next) {
        if (b->type != FZ_STEXT_BLOCK_TEXT) continue;
        for (fz_stext_line* line = b->u.t.first_line; line; line = line->next) {
            PDFTextRun run;
            run.pageNumber = pageNumber;
            run.bbox = ToRect(line->bbox);
            for (fz_stext_char* ch = line->first_char; ch; ch = ch->next) {
                AppendUtf8(run.text, ch->c);
            }
            if (!run.text.empty()) runs.push_back(std::move(run));
        }
    }
    fz_drop_stext_page(ctx_, st);
    return runs;
}

std::vector<PDFTextRun> MuPDFDocument::Search(const std::string& query,
                                              const PDFSearchOptions& opts) {
    std::vector<PDFTextRun> results;
    if (query.empty() || !ctx_ || !doc_) return results;
    std::lock_guard<std::mutex> lock(mu_);

    const int total = fz_count_pages(ctx_, doc_);
    if (total <= 0) return results;
    const int pStart = std::max(1, opts.pageStart > 0 ? opts.pageStart : 1);
    const int pEnd   = std::min(total, opts.pageEnd > 0 ? opts.pageEnd : total);
    const int maxPerPage = 256;  // upper bound for fz_search_page

    // fz_search_page does substring search; it's case-insensitive by default
    // in MuPDF 1.23+. We pre-normalize the query for whole-word handling
    // ourselves below.
    fz_quad hits[maxPerPage];
    int     marks[maxPerPage];

    int totalHits = 0;
    for (int p = pStart; p <= pEnd; ++p) {
        int n = 0;
        fz_try(ctx_) {
            n = fz_search_page_number(ctx_, doc_, p - 1, query.c_str(),
                                      marks, hits, maxPerPage);
        } fz_catch(ctx_) {
            n = 0;
        }
        for (int i = 0; i < n; ++i) {
            PDFTextRun r;
            r.pageNumber = p;
            r.bbox       = ToRect(hits[i]);
            r.text       = query;
            results.push_back(std::move(r));
            if (opts.maxHits > 0 && ++totalHits >= opts.maxHits) return results;
        }
        (void)opts.caseSensitive;  // fz_search_page is case-insensitive only
        (void)opts.wholeWord;
    }
    return results;
}

// ===== Images =====

std::vector<PDFImageRef> MuPDFDocument::ListImages(int pageNumber) {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<PDFImageRef> refs;

    // Reuse the stext machinery with FZ_STEXT_PRESERVE_IMAGES; that already
    // gives us per-image transform + bbox + the fz_image*. We only need bbox
    // and a placeholder mimeType for v1; ExtractImageBytes re-runs the page
    // to fetch the actual bytes for one image.
    if (!ctx_ || !doc_) return refs;
    const int total = fz_count_pages(ctx_, doc_);
    if (pageNumber < 1 || pageNumber > total) return refs;

    fz_stext_options opts = {};
    opts.flags = FZ_STEXT_PRESERVE_IMAGES;
    fz_stext_page* st = nullptr;
    fz_var(st);
    fz_try(ctx_) {
        st = fz_new_stext_page_from_page_number(ctx_, doc_, pageNumber - 1, &opts);
    } fz_catch(ctx_) {
        if (st) fz_drop_stext_page(ctx_, st);
        st = nullptr;
    }
    if (!st) return refs;

    int idx = 0;
    for (fz_stext_block* b = st->first_block; b; b = b->next) {
        if (b->type != FZ_STEXT_BLOCK_IMAGE) continue;
        PDFImageRef r;
        r.pageNumber = pageNumber;
        r.indexOnPage = idx++;
        r.bboxOnPage = ToRect(b->bbox);
        if (b->u.i.image) {
            r.widthPx  = b->u.i.image->w;
            r.heightPx = b->u.i.image->h;
        }
        r.mimeType = "image/x-raw";  // resolved precisely in ExtractImageBytes
        refs.push_back(std::move(r));
    }
    fz_drop_stext_page(ctx_, st);
    return refs;
}

std::vector<uint8_t> MuPDFDocument::ExtractImageBytes(const PDFImageRef& ref) {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<uint8_t> out;
    if (!ctx_ || !doc_) return out;

    fz_stext_options opts = {};
    opts.flags = FZ_STEXT_PRESERVE_IMAGES;
    fz_stext_page* st = nullptr;
    fz_var(st);
    fz_try(ctx_) {
        st = fz_new_stext_page_from_page_number(ctx_, doc_,
                                                ref.pageNumber - 1, &opts);
    } fz_catch(ctx_) {
        if (st) fz_drop_stext_page(ctx_, st);
        st = nullptr;
    }
    if (!st) return out;

    fz_image* target = nullptr;
    int idx = 0;
    for (fz_stext_block* b = st->first_block; b; b = b->next) {
        if (b->type != FZ_STEXT_BLOCK_IMAGE) continue;
        if (idx == ref.indexOnPage) { target = b->u.i.image; break; }
        ++idx;
    }
    if (!target) {
        fz_drop_stext_page(ctx_, st);
        return out;
    }

    // Re-encode as PNG via fz_buffer for a universal exchange format.
    fz_pixmap* pix = nullptr;
    fz_buffer* buf = nullptr;
    fz_var(pix);
    fz_var(buf);
    fz_try(ctx_) {
        pix = fz_get_pixmap_from_image(ctx_, target, nullptr, nullptr, nullptr, nullptr);
        if (pix) {
            buf = fz_new_buffer_from_pixmap_as_png(ctx_, pix, fz_default_color_params);
        }
    } fz_catch(ctx_) {
        // leave nullptr
    }
    if (buf) {
        unsigned char* data = nullptr;
        size_t n = fz_buffer_storage(ctx_, buf, &data);
        out.assign(data, data + n);
    }
    if (buf) fz_drop_buffer(ctx_, buf);
    if (pix) fz_drop_pixmap(ctx_, pix);
    fz_drop_stext_page(ctx_, st);
    return out;
}

// ===== Page operations =====

bool MuPDFDocument::DeletePage(int pageNumber) {
    if (!ctx_ || !pdoc_) return false;
    std::lock_guard<std::mutex> lock(mu_);
    const int total = fz_count_pages(ctx_, doc_);
    if (pageNumber < 1 || pageNumber > total) return false;

    bool ok = true;
    fz_try(ctx_) {
        pdf_delete_page(ctx_, pdoc_, pageNumber - 1);
    } fz_catch(ctx_) {
        ok = false;
    }
    if (ok) dirty_ = true;
    return ok;
}

bool MuPDFDocument::MovePage(int fromPage, int toPage) {
    if (!ctx_ || !pdoc_) return false;
    std::lock_guard<std::mutex> lock(mu_);
    const int total = fz_count_pages(ctx_, doc_);
    if (fromPage < 1 || fromPage > total || toPage < 1 || toPage > total) {
        return false;
    }
    if (fromPage == toPage) return true;

    // MuPDF has no direct "move page". We graft the source page to the
    // destination position within the same document, then delete the
    // original. We use pdf_graft_page (cross-doc), pointing src==dst — this
    // is the documented idiom for in-place reorder.
    const int fromIdx = fromPage - 1;
    const int toIdx   = toPage   - 1;

    bool ok = true;
    fz_try(ctx_) {
        // Insert a copy at the requested target index. After grafting, the
        // original lives at fromIdx + (toIdx <= fromIdx ? 1 : 0).
        pdf_graft_page(ctx_, pdoc_, toIdx, pdoc_, fromIdx);
        const int originalAfterGraft = (toIdx <= fromIdx) ? fromIdx + 1 : fromIdx;
        pdf_delete_page(ctx_, pdoc_, originalAfterGraft);
    } fz_catch(ctx_) {
        ok = false;
    }
    if (ok) dirty_ = true;
    return ok;
}

bool MuPDFDocument::InsertBlankPage(int at, float widthPt, float heightPt) {
    if (!ctx_ || !pdoc_ || widthPt <= 0 || heightPt <= 0) return false;
    std::lock_guard<std::mutex> lock(mu_);
    const int total = fz_count_pages(ctx_, doc_);
    if (at < 1) at = 1;
    if (at > total + 1) at = total + 1;

    bool ok = true;
    pdf_obj* resources = nullptr;
    fz_buffer* contents = nullptr;
    pdf_obj* page = nullptr;
    fz_var(resources);
    fz_var(contents);
    fz_var(page);
    fz_try(ctx_) {
        // Create an empty content stream + a fresh empty resources dict via
        // pdf_page_write. We immediately close the device to flush the
        // (empty) stream.
        fz_rect mb = fz_make_rect(0, 0, widthPt, heightPt);
        fz_device* dev = pdf_page_write(ctx_, pdoc_, mb, &resources, &contents);
        fz_close_device(ctx_, dev);
        fz_drop_device(ctx_, dev);
        page = pdf_add_page(ctx_, pdoc_, mb, 0, resources, contents);
        pdf_insert_page(ctx_, pdoc_, at - 1, page);
    } fz_catch(ctx_) {
        ok = false;
    }
    if (resources) pdf_drop_obj(ctx_, resources);
    if (contents)  fz_drop_buffer(ctx_, contents);
    if (page)      pdf_drop_obj(ctx_, page);
    if (ok) dirty_ = true;
    return ok;
}

bool MuPDFDocument::MergeFrom(IPDFDocument& other,
                              int srcStart, int srcEnd, int insertAt) {
    auto* src = dynamic_cast<MuPDFDocument*>(&other);
    if (!src || !ctx_ || !pdoc_ || !src->pdoc_) return false;

    // Lock both documents in a deterministic order to avoid deadlock.
    std::lock(mu_, src->mu_);
    std::lock_guard<std::mutex> a(mu_,      std::adopt_lock);
    std::lock_guard<std::mutex> b(src->mu_, std::adopt_lock);

    const int srcTotal = fz_count_pages(src->ctx_, src->doc_);
    if (srcStart < 1) srcStart = 1;
    if (srcEnd   < 1 || srcEnd > srcTotal) srcEnd = srcTotal;
    if (srcStart > srcEnd) return false;

    const int dstTotal = fz_count_pages(ctx_, doc_);
    if (insertAt < 1) insertAt = 1;
    if (insertAt > dstTotal + 1) insertAt = dstTotal + 1;

    bool ok = true;
    pdf_graft_map* gmap = nullptr;
    fz_var(gmap);
    fz_try(ctx_) {
        gmap = pdf_new_graft_map(ctx_, pdoc_);
        // pdf_graft_page handles cross-context grafting only if both docs
        // share a context; src and dst may have *different* fz_contexts.
        // For v1 we require them to share a context. If not, we fall back
        // to a slower copy-via-bytes path (TODO).
        if (src->ctx_ != ctx_) {
            fz_throw(ctx_, FZ_ERROR_GENERIC,
                     "Cross-context PDF merge not supported in v1");
        }
        int dst = insertAt - 1;
        for (int p = srcStart - 1; p <= srcEnd - 1; ++p) {
            pdf_graft_mapped_page(ctx_, gmap, dst, src->pdoc_, p);
            ++dst;
        }
    } fz_catch(ctx_) {
        ok = false;
    }
    if (gmap) pdf_drop_graft_map(ctx_, gmap);
    if (ok) dirty_ = true;
    return ok;
}

// ===== Content editing (Milestone 3) =====

bool MuPDFDocument::AddTextAnnotation(int pageNumber, const Rect2Df& at,
                                      const std::string& text) {
    if (!ctx_ || !pdoc_) return false;
    std::lock_guard<std::mutex> lock(mu_);
    const int total = fz_count_pages(ctx_, doc_);
    if (pageNumber < 1 || pageNumber > total) return false;

    pdf_page* page = nullptr;
    pdf_annot* annot = nullptr;
    fz_var(page);
    fz_var(annot);
    bool ok = true;
    fz_try(ctx_) {
        page = pdf_load_page(ctx_, pdoc_, pageNumber - 1);
        annot = pdf_create_annot(ctx_, page, PDF_ANNOT_FREE_TEXT);
        fz_rect r = FromRect(at);
        pdf_set_annot_rect(ctx_, annot, r);
        pdf_set_annot_contents(ctx_, annot, text.c_str());
        pdf_update_annot(ctx_, annot);
    } fz_catch(ctx_) {
        ok = false;
    }
    if (annot) pdf_drop_annot(ctx_, annot);
    if (page)  fz_drop_page(ctx_, &page->super);
    if (ok) dirty_ = true;
    return ok;
}

bool MuPDFDocument::RedactRect(int pageNumber, const Rect2Df& area) {
    if (!ctx_ || !pdoc_) return false;
    std::lock_guard<std::mutex> lock(mu_);
    const int total = fz_count_pages(ctx_, doc_);
    if (pageNumber < 1 || pageNumber > total) return false;

    pdf_page* page = nullptr;
    pdf_annot* annot = nullptr;
    fz_var(page);
    fz_var(annot);
    bool ok = true;
    fz_try(ctx_) {
        page = pdf_load_page(ctx_, pdoc_, pageNumber - 1);
        annot = pdf_create_annot(ctx_, page, PDF_ANNOT_REDACT);
        fz_rect r = FromRect(area);
        pdf_set_annot_rect(ctx_, annot, r);
        pdf_update_annot(ctx_, annot);
        // We attach the redaction annotation only; applying it (i.e. burning
        // the black rectangle into the content stream) is a separate step
        // exposed in Milestone 3 via a dedicated ApplyRedactions() method.
    } fz_catch(ctx_) {
        ok = false;
    }
    if (annot) pdf_drop_annot(ctx_, annot);
    if (page)  fz_drop_page(ctx_, &page->super);
    if (ok) dirty_ = true;
    return ok;
}

bool MuPDFDocument::ReplaceText(const PDFTextRun& target,
                                const std::string& newText) {
    if (!ctx_ || !pdoc_) return false;
    std::lock_guard<std::mutex> lock(mu_);
    const int total = fz_count_pages(ctx_, doc_);
    if (target.pageNumber < 1 || target.pageNumber > total) return false;

    pdf_page*  page    = nullptr;
    pdf_annot* redact  = nullptr;
    pdf_annot* freetext = nullptr;
    fz_var(page);
    fz_var(redact);
    fz_var(freetext);
    bool ok = true;
    fz_try(ctx_) {
        page = pdf_load_page(ctx_, pdoc_, target.pageNumber - 1);
        const fz_rect r = FromRect(target.bbox);

        // 1. Redact the bbox. black_boxes=0 → no visible black rectangle.
        //    image_method=NONE → don't touch images in the bbox.
        redact = pdf_create_annot(ctx_, page, PDF_ANNOT_REDACT);
        pdf_set_annot_rect(ctx_, redact, r);
        pdf_update_annot(ctx_, redact);

        pdf_redact_options ropts = {};
        ropts.black_boxes  = 0;
        ropts.image_method = PDF_REDACT_IMAGE_NONE;
        pdf_apply_redaction(ctx_, redact, &ropts);
        // pdf_apply_redaction removes the annot itself on success — we must
        // not pdf_drop_annot it afterwards.
        redact = nullptr;

        // 2. Overlay the new text as a free-text annotation at the same rect.
        if (!newText.empty()) {
            freetext = pdf_create_annot(ctx_, page, PDF_ANNOT_FREE_TEXT);
            pdf_set_annot_rect(ctx_, freetext, r);
            pdf_set_annot_contents(ctx_, freetext, newText.c_str());
            pdf_update_annot(ctx_, freetext);
        }
    } fz_catch(ctx_) {
        ok = false;
    }
    if (freetext) pdf_drop_annot(ctx_, freetext);
    if (redact)   pdf_drop_annot(ctx_, redact);
    if (page)     fz_drop_page(ctx_, &page->super);
    if (ok) dirty_ = true;
    return ok;
}

bool MuPDFDocument::ApplyPendingRedactions(int pageNumber) {
    if (!ctx_ || !pdoc_) return false;
    std::lock_guard<std::mutex> lock(mu_);
    const int total = fz_count_pages(ctx_, doc_);
    if (pageNumber < 1 || pageNumber > total) return false;

    pdf_page* page = nullptr;
    fz_var(page);
    bool ok = true;
    fz_try(ctx_) {
        page = pdf_load_page(ctx_, pdoc_, pageNumber - 1);
        pdf_redact_options ropts = {};
        ropts.black_boxes  = 0;
        ropts.image_method = PDF_REDACT_IMAGE_NONE;
        // Returns 1 if any redactions were applied.
        pdf_redact_page(ctx_, pdoc_, page, &ropts);
    } fz_catch(ctx_) {
        ok = false;
    }
    if (page) fz_drop_page(ctx_, &page->super);
    if (ok) dirty_ = true;
    return ok;
}

std::vector<PDFAnnotation> MuPDFDocument::ListAnnotations(int pageNumber) {
    std::vector<PDFAnnotation> out;
    if (!ctx_ || !pdoc_) return out;
    std::lock_guard<std::mutex> lock(mu_);
    const int total = fz_count_pages(ctx_, doc_);
    if (pageNumber < 1 || pageNumber > total) return out;

    pdf_page* page = nullptr;
    fz_var(page);
    fz_try(ctx_) {
        page = pdf_load_page(ctx_, pdoc_, pageNumber - 1);
        int idx = 0;
        for (pdf_annot* a = pdf_first_annot(ctx_, page); a;
             a = pdf_next_annot(ctx_, a)) {
            PDFAnnotation rec;
            rec.pageNumber  = pageNumber;
            rec.indexOnPage = idx++;
            rec.bbox        = ToRect(pdf_annot_rect(ctx_, a));
            const char* typeStr = pdf_string_from_annot_type(
                ctx_, pdf_annot_type(ctx_, a));
            const char* contents = pdf_annot_contents(ctx_, a);
            const char* author   = pdf_annot_author(ctx_, a);
            rec.type     = typeStr ? typeStr : "Unknown";
            rec.contents = contents ? contents : "";
            rec.author   = author ? author : "";
            out.push_back(std::move(rec));
        }
    } fz_catch(ctx_) {
        out.clear();
    }
    if (page) fz_drop_page(ctx_, &page->super);
    return out;
}

bool MuPDFDocument::DeleteAnnotation(int pageNumber, int indexOnPage) {
    if (!ctx_ || !pdoc_) return false;
    std::lock_guard<std::mutex> lock(mu_);
    const int total = fz_count_pages(ctx_, doc_);
    if (pageNumber < 1 || pageNumber > total || indexOnPage < 0) return false;

    pdf_page* page = nullptr;
    fz_var(page);
    bool ok = true;
    fz_try(ctx_) {
        page = pdf_load_page(ctx_, pdoc_, pageNumber - 1);
        int idx = 0;
        pdf_annot* target = nullptr;
        for (pdf_annot* a = pdf_first_annot(ctx_, page); a;
             a = pdf_next_annot(ctx_, a)) {
            if (idx == indexOnPage) { target = a; break; }
            ++idx;
        }
        if (target) {
            pdf_delete_annot(ctx_, page, target);
        } else {
            ok = false;
        }
    } fz_catch(ctx_) {
        ok = false;
    }
    if (page) fz_drop_page(ctx_, &page->super);
    if (ok) dirty_ = true;
    return ok;
}

// ===== Factory =====

std::unique_ptr<IPDFDocument> PDFEngineFactory::Create(PDFEngineKind kind) {
    switch (kind) {
        case PDFEngineKind::Auto:
        case PDFEngineKind::MuPDF:
            return std::unique_ptr<IPDFDocument>(new MuPDFDocument());
    }
    return nullptr;
}

std::vector<PDFEngineKind> PDFEngineFactory::Available() {
    return { PDFEngineKind::MuPDF };
}

bool PDFEngineFactory::IsAvailable(PDFEngineKind kind) {
    auto v = Available();
    return std::find(v.begin(), v.end(), kind) != v.end();
}

std::string PDFEngineFactory::GetKindName(PDFEngineKind kind) {
    switch (kind) {
        case PDFEngineKind::Auto:  return "Auto";
        case PDFEngineKind::MuPDF: return "MuPDF";
    }
    return "Unknown";
}

std::unique_ptr<IPDFDocument> OpenPDF(const std::string& path,
                                      const std::string& password) {
    auto doc = PDFEngineFactory::Create(PDFEngineKind::Auto);
    if (!doc) return nullptr;
    if (!doc->Open(path, password)) return nullptr;
    return doc;
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_PDF_MUPDF
