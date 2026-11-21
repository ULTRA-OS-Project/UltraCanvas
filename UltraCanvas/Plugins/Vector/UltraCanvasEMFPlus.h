// UltraCanvas/Plugins/Vector/UltraCanvasEMFPlus.h
// EMF+ ([MS-EMFPLUS]) playback for the EMF reader. GDI+ writes its drawing
// as EMF+ records inside EMR_COMMENT records, and in a "dual" file follows
// each with a plain GDI copy for players that cannot read EMF+. The GDI copy
// loses what GDI cannot say - alpha above all, so a translucent shape comes
// out opaque - and need not even sit where the EMF+ drawing does.
//
// The player draws the EMF+ records it knows into model elements, and tells
// the EMF reader whether to draw the GDI records that follow:
//   - the EMF+ records between one GDI drawing and the next form a stretch;
//     when every drawing record in it was played, its elements are kept and
//     the GDI copy that follows is not drawn;
//   - when one was not (an image, a region fill, a driver string, a texture
//     or path-gradient brush), the stretch is dropped and its GDI copy drawn,
//     so nothing goes missing;
//   - after an EMF+ GetDC the GDI records are real drawing again (GDI+ hands
//     the device context to GDI there), until the next EMF+ drawing.
// A file that is EMF+ only (no dual flag in its header) has no GDI copy to
// fall back on, so whatever was played is kept.
//
// Clipping is not applied (the GDI side of the reader keeps none either).
// Version: 1.0.0
// Last Modified: 2026-11-20
// Author: UltraCanvas Framework

#pragma once

#include "DataFormats/UltraCanvasVectorStorage.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace VectorConverter {

class EmfPlusPlayer {
public:
    using Emit = std::function<void(std::shared_ptr<VectorStorage::VectorElement>)>;

    // `pxToPt` is the reader's device-pixel size in points; `page` the page
    // in points (what an EMF+ Clear covers). `emit` receives each element
    // kept, in drawing order.
    EmfPlusPlayer(double pxToPt, const Rect2Dd& page, Emit emit);

    // The records of one EMR_COMMENT's EMF+ payload: `data` points past the
    // "EMF+" identifier, `size` bytes of records.
    void Play(const uint8_t* data, size_t size);

    // Called by the reader before each GDI record that draws: settles the
    // stretch of EMF+ drawing before it. True when that record is drawn.
    bool GdiDraws();

    // End of the metafile: settles the last stretch.
    void Finish();

    bool SawEmfPlus() const { return seen; }

    // One line naming the EMF+ records drawn from the GDI copy instead (and
    // how often), or empty.
    std::string Report() const;

    struct Brush;
    struct Pen;
    struct Path;
    struct Font;

private:
    struct Object;
    struct State {
        VectorStorage::Matrix3x3 world, base;
        int pageUnit;
        double pageScale;
    };

    double pxToPt;
    Rect2Dd page;
    Emit emit;

    bool seen = false;
    bool dual = false;
    bool videoDisplay = true;
    double dpiX = 96, dpiY = 96;

    // The transform state: world (inside the current container), the
    // containers' accumulated transform, the page transform.
    VectorStorage::Matrix3x3 world, base;
    int pageUnit = 1;          // UnitDisplay
    double pageScale = 1;
    std::map<uint32_t, std::vector<State>> saved;
    std::vector<State> containers;

    std::map<uint8_t, std::shared_ptr<Object>> objects;
    // A continued object being assembled: its id, type, size and bytes.
    uint8_t partialId = 0;
    uint16_t partialFlags = 0;
    uint32_t partialTotal = 0;
    std::vector<uint8_t> partial;

    // The current stretch.
    bool stretch = false, stretchOk = true;
    std::vector<std::shared_ptr<VectorStorage::VectorElement>> pending;
    bool gdiSuppressed = false;
    std::map<uint16_t, int> fellBack;

    void Record(uint16_t type, uint16_t flags, const uint8_t* d, size_t n);
    void Drawing(bool played, std::shared_ptr<VectorStorage::VectorElement> element = nullptr);
    void Settle();
    void ObjectPart(uint16_t flags, const uint8_t* d, size_t n);
    void DefineObject(uint8_t id, int type, const uint8_t* d, size_t n);

    double UnitToPx(int unit, bool horizontal = true) const;
    VectorStorage::Matrix3x3 ToPoints() const;   // world units -> points
    double LengthScale() const;                  // how much ToPoints() enlarges a length
    Point2Dd Pt(double x, double y) const;

    std::shared_ptr<Brush> BrushFor(uint16_t flags, uint32_t id) const;
    std::shared_ptr<Pen> PenFor(uint32_t id) const;
    bool FillWith(const Brush& b, VectorStorage::VectorStyle& style) const;
    bool StrokeWith(const Pen& p, VectorStorage::VectorStyle& style) const;
};

} // namespace VectorConverter
} // namespace UltraCanvas
