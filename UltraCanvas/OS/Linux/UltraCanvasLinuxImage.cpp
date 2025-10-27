#include "UltraCanvasImage.h"
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {
    // Destructor - clean up Cairo surface if owned
    UCLinuxImage::~UCLinuxImage() {
        if (surface && ownsSurface) {
            cairo_surface_destroy(surface);
        }
    }

    // Set Cairo surface (takes ownership by default)
    void UCLinuxImage::SetSurface(cairo_surface_t* surf, bool takeOwnership) {
        // Clean up existing surface if owned
        if (surface && ownsSurface) {
            cairo_surface_destroy(surface);
        }

        surface = surf;
        ownsSurface = takeOwnership;
        errorMessage.clear();

        // Update dimensions from surface
        if (surface) {
            if (cairo_surface_get_type(surface) == CAIRO_SURFACE_TYPE_IMAGE) {
                width = cairo_image_surface_get_width(surface);
                height = cairo_image_surface_get_height(surface);

                // Determine channels from format
//                    cairo_format_t fmt = cairo_image_surface_get_format(surface);
//                    switch (fmt) {
//                        case CAIRO_FORMAT_ARGB32:
//                            channels = 4;
//                            break;
//                        case CAIRO_FORMAT_RGB24:
//                            channels = 3;
//                            break;
//                        case CAIRO_FORMAT_A8:
//                            channels = 1;
//                            break;
//                        default:
//                            channels = 4;
//                    }
            }
        }

    }

    // Get Cairo surface
    // Optional overrides
    bool UCLinuxImage::IsValid() const { return surface != nullptr && errorMessage.empty(); }
    bool UCLinuxImage::IsLoading() const { return surface == nullptr && errorMessage.empty(); }

    std::shared_ptr<UCLinuxImage> UCLinuxImage::Clone() const {
        auto cloned = std::shared_ptr<UCLinuxImage>();

        // Copy base properties
        cloned->width = width;
        cloned->height = height;

        // Clone Cairo surface if present
        if (surface) {
            // Create a new surface with same content
            cairo_surface_t* newSurface = cairo_surface_create_similar_image(
                    surface,
                    cairo_image_surface_get_format(surface),
                    width,
                    height
            );

            if (newSurface) {
                // Copy content
                cairo_t* cr = cairo_create(newSurface);
                cairo_set_source_surface(cr, surface, 0, 0);
                cairo_paint(cr);
                cairo_destroy(cr);

                cloned->surface = newSurface;
                cloned->ownsSurface = true;
            }
        }

        return cloned;
    }

    int UCLinuxImage::GetDataSize() const {
        if (!surface) {
            return 0;
        }
        cairo_format_t format = cairo_image_surface_get_format(surface);
        int stride = cairo_format_stride_for_width (format, width);
        return stride * height;
    }
}
