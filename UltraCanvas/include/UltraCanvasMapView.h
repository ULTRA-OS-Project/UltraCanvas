// include/UltraCanvasMapView.h
// A map of the world from a slippy-map tile server - OpenStreetMap's by
// default - as an element (UltraCanvasMapView: dragged to move, the wheel
// to zoom, a click to put the pin), as a picture drawn into any rectangle
// (DrawMap: a map card in a list's row), and as pixels (RenderMapToRgba: a
// map picture to save or send). The tiles are fetched in the background
// through UltraNet and kept in memory, shared by every map of the program.
//
//   MapTiles::SetUserAgent("MyApp/1.0 (ULTRA OS)");   // the tile policy wants the program named
//   auto map = std::make_shared<UltraCanvasMapView>("map", 0, 0, 480, 320);
//   map->CentreOn(GeoPoint{52.5219, 13.4132}, 16);
//   map->SetPin(GeoPoint{52.5219, 13.4132});
//   map->onPinMoved = [](const GeoPoint& p) { /* the person clicked here */ };
//
// Coordinates are WGS 84 degrees; the projection is Web Mercator, as every
// slippy-map server draws it. OpenStreetMap's tile usage policy is kept by
// default: requests carry the program's User-Agent, at most two tiles load
// at once, a tile fetched once is not fetched again while the program runs,
// and the map shows the attribution the server asks for. A build without
// UltraNet (ULTRACANVAS_HAS_NET unset: WebAssembly) fetches no tiles and
// draws the maps grey.
//
// Docs: Docs/UltraCanvas/UltraCanvasMapView.md
// Version: 1.0.1 - no tiles without UltraNet (WebAssembly)
// Version: 1.0.0 - from ChatCube's location window, made a framework element
// Last Modified: 2026-11-23
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASMAPVIEW_H
#define ULTRACANVASMAPVIEW_H

#include "UltraCanvasClipboardDib.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"

#include <cstdint>
#include <functional>
#include <string>

namespace UltraCanvas {

class IRenderContext;

// A place on the earth, in degrees
struct GeoPoint {
    double latitude = 0.0;
    double longitude = 0.0;
};

namespace WebMercator {
    constexpr int kTileSize = 256;
    constexpr int kMinZoom = 1;
    constexpr int kMaxZoom = 19;
    // The point in world pixels at `zoom` (0,0 the top-left corner of the
    // world, which is 256 * 2^zoom pixels square), and back
    Point2Dd ToWorld(const GeoPoint& point, int zoom);
    GeoPoint FromWorld(const Point2Dd& world, int zoom);
} // namespace WebMercator

// The tiles every map of the program shares
namespace MapTiles {
    // The program's name for the tile server (OpenStreetMap's policy asks
    // for one); "UltraCanvas" until set
    void SetUserAgent(const std::string& userAgent);
    // The server: a URL with {z}, {x} and {y}; OpenStreetMap's
    // "https://tile.openstreetmap.org/{z}/{x}/{y}.png" by default. Changing
    // it forgets the tiles kept so far.
    void SetServer(const std::string& urlTemplate, const std::string& attribution);
    // The words the map shows in its corner ("© OpenStreetMap contributors")
    std::string Attribution();

    // Called on the UI thread whenever a tile has arrived, so whatever draws
    // maps can draw again. Returns an id for RemoveListener.
    int AddListener(std::function<void()> tileArrived);
    void RemoveListener(int id);
} // namespace MapTiles

// The map around `centre` at `zoom` drawn into `rect`, the pin (if given)
// where it lies, the attribution in the bottom-right corner when
// `attribution`. Tiles not here yet are asked for and drawn grey; a
// MapTiles listener hears when they come. For a list's row or a card.
void DrawMap(IRenderContext* ctx, const Rect2Dd& rect, const GeoPoint& centre, int zoom,
             const GeoPoint* pin = nullptr, bool attribution = true);

// The same as straight RGBA, width x height, from the tiles here now (the
// missing ones grey; they are asked for). False only for a size of 0.
bool RenderMapToRgba(const GeoPoint& centre, int zoom, int width, int height, const GeoPoint* pin,
                     ClipboardDib::RgbaImage& out);

// The pin both draw: a red disc with a white ring, its centre the place
void DrawMapPin(IRenderContext* ctx, const Point2Dd& at);

// ui-reuse-exempt: the map itself - the server's tiles drawn where Web
// Mercator puts them, the pin over them; there is no element to compose it of.
class UltraCanvasMapView : public UltraCanvasUIElement {
public:
    UltraCanvasMapView(const std::string& identifier, float x = 0, float y = 0, float w = 0, float h = 0);
    ~UltraCanvasMapView() override;

    void CentreOn(const GeoPoint& point, int zoom);
    GeoPoint GetCentre() const;
    int GetZoom() const { return zoom_; }
    // By one step (+1 in, -1 out), about the centre
    void Zoom(int by);

    void SetPin(const GeoPoint& point);
    void ClearPin();
    bool HasPin() const { return hasPin_; }
    GeoPoint GetPin() const { return pin_; }
    // Whether a click puts the pin where it lands (true by default)
    void SetPinOnClick(bool on) { pinOnClick_ = on; }

    // A click put the pin here
    std::function<void(const GeoPoint&)> onPinMoved;
    // The centre or the zoom changed (a drag, the wheel, Zoom, CentreOn)
    std::function<void()> onViewChanged;

    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

private:
    Point2Dd centre_;           // world pixels at zoom_
    int zoom_ = 3;
    bool hasPin_ = false;
    GeoPoint pin_;
    bool pinOnClick_ = true;
    bool pressed_ = false, dragged_ = false;
    Point2Di pressAt_, lastAt_;
    int listener_ = 0;
};

} // namespace UltraCanvas

#endif // ULTRACANVASMAPVIEW_H
