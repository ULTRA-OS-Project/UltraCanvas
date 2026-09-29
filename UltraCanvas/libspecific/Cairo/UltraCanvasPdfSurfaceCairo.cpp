// libspecific/Cairo/UltraCanvasPdfSurfaceCairo.cpp
// UltraCanvasPdfSurface on Cairo's PDF surface. The surface writes through a
// stream callback rather than being given a file name, so a UTF-8 path opens
// correctly on Windows (OpenFileUtf8) and the in-memory case costs nothing
// extra.
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasPdfSurface.h"
#include "RenderContextCairo.h"
#include "UltraCanvasPathUtf8.h"

#include <cairo-pdf.h>
#include <cmath>
#include <cstdio>

namespace UltraCanvas {

namespace {

class PdfSurfaceCairo : public UltraCanvasPdfSurface {
public:
    ~PdfSurfaceCairo() override {
        std::string ignored;
        if (!finished) Finish(ignored);
        if (file) std::fclose(file);
    }

    bool Open(std::FILE* output, double widthPt, double heightPt, std::string& error) {
        file = output;
        if (!(widthPt > 0.0) || !(heightPt > 0.0)) {
            error = "The page has no size";
            return false;
        }
        cairo_surface_t* surface = cairo_pdf_surface_create_for_stream(&PdfSurfaceCairo::Write, this,
                                                                       widthPt, heightPt);
        if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
            error = std::string("The PDF could not be started: ")
                  + cairo_status_to_string(cairo_surface_status(surface));
            cairo_surface_destroy(surface);
            return false;
        }
        context = std::make_unique<RenderContextCairo>();
        // The context owns the surface from here on.
        if (!context->AttachSurface(surface, Size2Di(static_cast<int>(std::ceil(widthPt)),
                                                     static_cast<int>(std::ceil(heightPt))))) {
            error = "The PDF could not be drawn into";
            context.reset();
            return false;
        }
        return true;
    }

    IRenderContext* GetContext() override { return finished ? nullptr : context.get(); }

    void NextPage(double widthPt, double heightPt) override {
        if (finished || !context) return;
        cairo_t* cr = static_cast<cairo_t*>(context->GetNativeContext());
        cairo_show_page(cr);
        if (widthPt > 0.0 && heightPt > 0.0) {
            cairo_pdf_surface_set_size(cairo_get_target(cr), widthPt, heightPt);
        }
        // Each page starts from a clean state: no transform, no clip.
        context->ResetState();
        cairo_identity_matrix(cr);
        cairo_reset_clip(cr);
    }

    void SetMetadata(const std::string& title, const std::string& author, const std::string& subject) override {
        if (!context) return;
        cairo_surface_t* surface = cairo_get_target(static_cast<cairo_t*>(context->GetNativeContext()));
        if (!title.empty()) cairo_pdf_surface_set_metadata(surface, CAIRO_PDF_METADATA_TITLE, title.c_str());
        if (!author.empty()) cairo_pdf_surface_set_metadata(surface, CAIRO_PDF_METADATA_AUTHOR, author.c_str());
        if (!subject.empty()) cairo_pdf_surface_set_metadata(surface, CAIRO_PDF_METADATA_SUBJECT, subject.c_str());
        cairo_pdf_surface_set_metadata(surface, CAIRO_PDF_METADATA_CREATOR, "UltraCanvas");
    }

    bool Finish(std::string& error) override {
        if (finished) return !failed;
        finished = true;
        if (context) {
            cairo_surface_t* surface = cairo_get_target(static_cast<cairo_t*>(context->GetNativeContext()));
            cairo_surface_finish(surface);
            if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) failed = true;
            context.reset();
        }
        if (file) {
            if (std::fclose(file) != 0) failed = true;
            file = nullptr;
        }
        if (failed) error = "The PDF could not be written";
        return !failed;
    }

    const std::vector<uint8_t>& GetBytes() const override { return bytes; }

private:
    static cairo_status_t Write(void* closure, const unsigned char* data, unsigned int length) {
        auto* self = static_cast<PdfSurfaceCairo*>(closure);
        if (self->file) {
            if (std::fwrite(data, 1, length, self->file) != length) {
                self->failed = true;
                return CAIRO_STATUS_WRITE_ERROR;
            }
        } else {
            self->bytes.insert(self->bytes.end(), data, data + length);
        }
        return CAIRO_STATUS_SUCCESS;
    }

    std::unique_ptr<RenderContextCairo> context;
    std::FILE* file = nullptr;
    std::vector<uint8_t> bytes;
    bool finished = false;
    bool failed = false;
};

} // namespace

std::unique_ptr<UltraCanvasPdfSurface> UltraCanvasPdfSurface::CreateFile(const std::string& utf8Path,
                                                                         double widthPt, double heightPt,
                                                                         std::string& error) {
    std::FILE* file = OpenFileUtf8(utf8Path, "wb");
    if (!file) {
        error = "Could not create " + utf8Path;
        return nullptr;
    }
    auto pdf = std::make_unique<PdfSurfaceCairo>();
    if (!pdf->Open(file, widthPt, heightPt, error)) return nullptr;
    return pdf;
}

std::unique_ptr<UltraCanvasPdfSurface> UltraCanvasPdfSurface::CreateInMemory(double widthPt, double heightPt,
                                                                             std::string& error) {
    auto pdf = std::make_unique<PdfSurfaceCairo>();
    if (!pdf->Open(nullptr, widthPt, heightPt, error)) return nullptr;
    return pdf;
}

} // namespace UltraCanvas
