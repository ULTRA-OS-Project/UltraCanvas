// core/UltraCanvasMapView.cpp
// See the header.
// Version: 1.0.1 - builds without UltraNet (WebAssembly): no tiles fetched, the maps grey
// Version: 1.0.0 - from ChatCube's location window, made a framework element
// Last Modified: 2026-11-23
// Author: UltraCanvas Framework

#include "UltraCanvasMapView.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasRenderContext.h"
#ifdef ULTRACANVAS_HAS_NET
#include "UltraNet/UltraNetHttp.h"
#endif

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

namespace UltraCanvas {

namespace {

constexpr int kMaxRequests = 2;     // OpenStreetMap's tile policy: two connections at most
constexpr double kPi = 3.14159265358979323846;
constexpr double kMaxLatitude = 85.05112878;

const Color kEmpty(224, 224, 224, 255);

std::string TileKey(int zoom, int x, int y) {
    return std::to_string(zoom) + "/" + std::to_string(x) + "/" + std::to_string(y);
}

std::string ReplaceAll(std::string s, const std::string& what, const std::string& with) {
    for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + with.size()))
        s.replace(at, what.size(), with);
    return s;
}

// The tiles, on the UI thread only (the requests' answers are posted to it)
struct TileStore {
    std::string userAgent = "UltraCanvas";
    std::string server = "https://tile.openstreetmap.org/{z}/{x}/{y}.png";
    std::string attribution = "\xC2\xA9 OpenStreetMap contributors";
    int generation = 0;                                 // a server change forgets what is in flight
    std::map<std::string, std::vector<uint8_t>> bytes;  // the PNGs as they came
    std::map<std::string, UCImagePtr> images;           // ... decoded, for drawing
    std::set<std::string> failed;
    std::deque<std::pair<std::string, std::string>> queue;   // key, URL
    std::set<std::string> queued;
    int active = 0;
    int nextListener = 1;
    std::map<int, std::function<void()>> listeners;

    void Request(int zoom, int x, int y) {
        const std::string key = TileKey(zoom, x, y);
        if (bytes.count(key) || failed.count(key) || queued.count(key)) return;
        queued.insert(key);
        std::string url = ReplaceAll(server, "{z}", std::to_string(zoom));
        url = ReplaceAll(url, "{x}", std::to_string(x));
        url = ReplaceAll(url, "{y}", std::to_string(y));
        queue.emplace_back(key, url);
        Pump();
    }

    void Pump();
    void Arrived(int gen, const std::string& key, bool ok, std::vector<uint8_t> body) {
        if (gen != generation) return;
        --active;
        queued.erase(key);
        if (ok) {
            images[key] = UCImage::LoadFromMemory(body);
            bytes[key] = std::move(body);
        } else {
            failed.insert(key);
        }
        Pump();
        // A copy: a listener may remove itself
        const auto copy = listeners;
        for (const auto& [id, fn] : copy) {
            if (fn) fn();
        }
    }
};

TileStore& Store() {
    static TileStore store;
    return store;
}

void TileStore::Pump() {
#ifndef ULTRACANVAS_HAS_NET
    // A build without UltraNet (WebAssembly) fetches nothing: the maps stay
    // grey, and each tile is asked for once
    for (const auto& [key, url] : queue) {
        queued.erase(key);
        failed.insert(key);
    }
    queue.clear();
    return;
#else
    auto* app = UltraCanvasApplicationBase::GetCurrent();
    while (app && active < kMaxRequests && !queue.empty()) {
        auto [key, url] = queue.front();
        queue.pop_front();
        ++active;
        UltraNetHttpRequest request;
        request.url = url;
        request.headers.Set("User-Agent", userAgent);
        const int gen = generation;
        UltraNet_HttpRequestAsync(request, [app, gen, key](const UltraNetResponse& response) {
            const bool ok = response.IsComplete() && response.IsSuccess() && !response.body.empty();
            std::vector<uint8_t> body = ok ? response.body : std::vector<uint8_t>();
            app->PostToUIThread([gen, key, ok, body = std::move(body)]() mutable {
                Store().Arrived(gen, key, ok, std::move(body));
            });
        });
    }
#endif
}

// The tiles a rectangle of world pixels covers, the visit given each
// (wrapped across the date line) with where its corner falls
template <typename Visit>
void ForEachTile(double left, double top, double width, double height, int zoom, Visit visit) {
    const int tiles = 1 << zoom;
    const int firstX = static_cast<int>(std::floor(left / WebMercator::kTileSize));
    const int firstY = static_cast<int>(std::floor(top / WebMercator::kTileSize));
    const int lastX = static_cast<int>(std::floor((left + width) / WebMercator::kTileSize));
    const int lastY = static_cast<int>(std::floor((top + height) / WebMercator::kTileSize));
    for (int ty = firstY; ty <= lastY; ++ty) {
        if (ty < 0 || ty >= tiles) continue;
        for (int tx = firstX; tx <= lastX; ++tx) {
            const int wrapped = ((tx % tiles) + tiles) % tiles;
            visit(wrapped, ty, tx * WebMercator::kTileSize - left, ty * WebMercator::kTileSize - top);
        }
    }
}

int ClampZoom(int zoom) {
    return std::clamp(zoom, WebMercator::kMinZoom, WebMercator::kMaxZoom);
}

} // namespace

// ===== WEB MERCATOR =====

namespace WebMercator {

Point2Dd ToWorld(const GeoPoint& point, int zoom) {
    const double size = kTileSize * std::pow(2.0, zoom);
    const double lat = std::clamp(point.latitude, -kMaxLatitude, kMaxLatitude) * kPi / 180.0;
    const double x = (point.longitude + 180.0) / 360.0 * size;
    const double y = (1.0 - std::log(std::tan(lat) + 1.0 / std::cos(lat)) / kPi) / 2.0 * size;
    return Point2Dd(x, y);
}

GeoPoint FromWorld(const Point2Dd& world, int zoom) {
    const double size = kTileSize * std::pow(2.0, zoom);
    GeoPoint p;
    p.longitude = world.x / size * 360.0 - 180.0;
    const double n = kPi - 2.0 * kPi * world.y / size;
    p.latitude = 180.0 / kPi * std::atan(0.5 * (std::exp(n) - std::exp(-n)));
    while (p.longitude < -180.0) p.longitude += 360.0;
    while (p.longitude >= 180.0) p.longitude -= 360.0;
    return p;
}

} // namespace WebMercator

// ===== THE TILES =====

namespace MapTiles {

void SetUserAgent(const std::string& userAgent) {
    if (!userAgent.empty()) Store().userAgent = userAgent;
}

void SetServer(const std::string& urlTemplate, const std::string& attribution) {
    TileStore& s = Store();
    if (urlTemplate.empty() || urlTemplate == s.server) return;
    s.server = urlTemplate;
    s.attribution = attribution;
    ++s.generation;
    s.bytes.clear();
    s.images.clear();
    s.failed.clear();
    s.queue.clear();
    s.queued.clear();
    s.active = 0;
}

std::string Attribution() {
    return Store().attribution;
}

int AddListener(std::function<void()> tileArrived) {
    TileStore& s = Store();
    const int id = s.nextListener++;
    s.listeners[id] = std::move(tileArrived);
    return id;
}

void RemoveListener(int id) {
    Store().listeners.erase(id);
}

} // namespace MapTiles

// ===== DRAWING =====

void DrawMapPin(IRenderContext* ctx, const Point2Dd& at) {
    ctx->SetFillPaint(Color(0, 0, 0, 70));
    ctx->FillCircle(Point2Dd(at.x, at.y + 2), 11);
    ctx->SetFillPaint(Color(220, 50, 47, 255));
    ctx->FillCircle(at, 10);
    ctx->SetStrokePaint(Color(255, 255, 255, 255));
    ctx->SetStrokeWidth(2.5);
    ctx->DrawCircle(at, 10);
    ctx->SetFillPaint(Color(255, 255, 255, 255));
    ctx->FillCircle(at, 3.5);
}

void DrawMap(IRenderContext* ctx, const Rect2Dd& rect, const GeoPoint& centre, int zoom, const GeoPoint* pin,
             bool attribution) {
    if (!ctx || rect.width <= 0 || rect.height <= 0) return;
    zoom = ClampZoom(zoom);
    TileStore& store = Store();
    ctx->PushState();
    ctx->ClipRect(rect);
    ctx->SetFillPaint(kEmpty);
    ctx->FillRectangle(rect);
    const Point2Dd c = WebMercator::ToWorld(centre, zoom);
    const double left = c.x - rect.width / 2.0, top = c.y - rect.height / 2.0;
    ForEachTile(left, top, rect.width, rect.height, zoom, [&](int x, int y, double ox, double oy) {
        auto it = store.images.find(TileKey(zoom, x, y));
        if (it != store.images.end() && it->second && it->second->IsValid()) {
            ctx->DrawImage(*it->second, Rect2Dd(rect.x + ox, rect.y + oy, WebMercator::kTileSize, WebMercator::kTileSize),
                           ImageFitMode::Fill);
        } else {
            store.Request(zoom, x, y);
        }
    });
    if (pin) {
        const Point2Dd p = WebMercator::ToWorld(*pin, zoom);
        DrawMapPin(ctx, Point2Dd(rect.x + p.x - left, rect.y + p.y - top));
    }
    if (attribution && !store.attribution.empty()) {
        ctx->SetFontSize(8.0);
        ctx->SetFontWeight(FontWeight::Normal);
        ctx->SetTextWrap(TextWrap::WrapNone);
        const Point2Di size = ctx->GetTextDimension(store.attribution);
        const Rect2Dd box(rect.x + rect.width - size.x - 10, rect.y + rect.height - size.y - 4, size.x + 10, size.y + 4);
        ctx->SetFillPaint(Color(255, 255, 255, 200));
        ctx->FillRectangle(box);
        ctx->SetTextPaint(Color(40, 40, 40, 255));
        ctx->DrawText(store.attribution, Point2Dd(box.x + 5, box.y + 2));
    }
    ctx->PopState();
}

bool RenderMapToRgba(const GeoPoint& centre, int zoom, int width, int height, const GeoPoint* pin,
                     ClipboardDib::RgbaImage& out) {
    if (width <= 0 || height <= 0) return false;
    zoom = ClampZoom(zoom);
    TileStore& store = Store();
    out.width = width;
    out.height = height;
    out.pixels.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4, 0);
    for (size_t i = 0; i < out.pixels.size(); i += 4) {
        out.pixels[i] = kEmpty.r;
        out.pixels[i + 1] = kEmpty.g;
        out.pixels[i + 2] = kEmpty.b;
        out.pixels[i + 3] = 255;
    }
    const Point2Dd c = WebMercator::ToWorld(centre, zoom);
    const double left = c.x - width / 2.0, top = c.y - height / 2.0;
    ForEachTile(left, top, width, height, zoom, [&](int x, int y, double ox, double oy) {
        auto it = store.bytes.find(TileKey(zoom, x, y));
        if (it == store.bytes.end()) {
            store.Request(zoom, x, y);
            return;
        }
        ClipboardDib::RgbaImage tile;
        if (!ClipboardDib::DecodePng(it->second.data(), it->second.size(), tile)) return;
        const int tx = static_cast<int>(std::lround(ox)), ty = static_cast<int>(std::lround(oy));
        for (int row = 0; row < tile.height; ++row) {
            const int py = ty + row;
            if (py < 0 || py >= height) continue;
            for (int col = 0; col < tile.width; ++col) {
                const int px = tx + col;
                if (px < 0 || px >= width) continue;
                const uint8_t* s = &tile.pixels[(static_cast<size_t>(row) * tile.width + col) * 4];
                uint8_t* d = &out.pixels[(static_cast<size_t>(py) * width + px) * 4];
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
                d[3] = 255;
            }
        }
    });
    if (pin) {
        // The pin into the pixels: a red disc, a white ring and a white dot,
        // the ring's edge softened by coverage
        const Point2Dd p = WebMercator::ToWorld(*pin, zoom);
        const int cx = static_cast<int>(std::lround(p.x - left)), cy = static_cast<int>(std::lround(p.y - top));
        for (int y = cy - 13; y <= cy + 13; ++y) {
            if (y < 0 || y >= height) continue;
            for (int x = cx - 13; x <= cx + 13; ++x) {
                if (x < 0 || x >= width) continue;
                const double d = std::hypot(x - cx, y - cy);
                int r = -1, g = 0, b = 0;
                double cover = 1.0;
                if (d <= 3.5) { r = 255; g = 255; b = 255; }
                else if (d <= 9.0) { r = 220; g = 50; b = 47; }
                else if (d <= 11.5) { r = 255; g = 255; b = 255; cover = std::clamp(12.0 - d, 0.0, 1.0); }
                if (r < 0) continue;
                uint8_t* px = &out.pixels[(static_cast<size_t>(y) * width + x) * 4];
                px[0] = static_cast<uint8_t>(std::lround(px[0] * (1 - cover) + r * cover));
                px[1] = static_cast<uint8_t>(std::lround(px[1] * (1 - cover) + g * cover));
                px[2] = static_cast<uint8_t>(std::lround(px[2] * (1 - cover) + b * cover));
            }
        }
    }
    return true;
}

// ===== THE ELEMENT =====

UltraCanvasMapView::UltraCanvasMapView(const std::string& identifier, float x, float y, float w, float h)
    : UltraCanvasUIElement(identifier, x, y, w, h) {
    centre_ = WebMercator::ToWorld(GeoPoint{30.0, 10.0}, zoom_);
    listener_ = MapTiles::AddListener([this]() { RequestRedraw(); });
}

UltraCanvasMapView::~UltraCanvasMapView() {
    MapTiles::RemoveListener(listener_);
}

void UltraCanvasMapView::CentreOn(const GeoPoint& point, int zoom) {
    zoom_ = ClampZoom(zoom);
    centre_ = WebMercator::ToWorld(point, zoom_);
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

GeoPoint UltraCanvasMapView::GetCentre() const {
    return WebMercator::FromWorld(centre_, zoom_);
}

void UltraCanvasMapView::Zoom(int by) {
    CentreOn(GetCentre(), zoom_ + by);
}

void UltraCanvasMapView::SetPin(const GeoPoint& point) {
    pin_ = point;
    hasPin_ = true;
    RequestRedraw();
}

void UltraCanvasMapView::ClearPin() {
    hasPin_ = false;
    RequestRedraw();
}

void UltraCanvasMapView::Render(IRenderContext* ctx, const Rect2Df&) {
    const float w = GetWidth(), h = GetHeight();
    if (w <= 0 || h <= 0) return;
    DrawMap(ctx, Rect2Dd(0, 0, w, h), GetCentre(), zoom_, hasPin_ ? &pin_ : nullptr, true);
}

bool UltraCanvasMapView::OnEvent(const UCEvent& event) {
    switch (event.type) {
        case UCEventType::MouseDown:
            if (event.button != UCMouseButton::Left) return false;
            pressed_ = true;
            dragged_ = false;
            pressAt_ = lastAt_ = event.pointer;
            return true;
        case UCEventType::MouseMove:
            if (!pressed_) return false;
            if (std::abs(event.pointer.x - pressAt_.x) + std::abs(event.pointer.y - pressAt_.y) > 4) dragged_ = true;
            if (dragged_) {
                centre_.x -= event.pointer.x - lastAt_.x;
                centre_.y -= event.pointer.y - lastAt_.y;
                centre_.y = std::clamp(centre_.y, 0.0, WebMercator::kTileSize * std::pow(2.0, zoom_));
                RequestRedraw();
                if (onViewChanged) onViewChanged();
            }
            lastAt_ = event.pointer;
            return true;
        case UCEventType::MouseUp: {
            if (!pressed_ || event.button != UCMouseButton::Left) return false;
            pressed_ = false;
            if (dragged_ || !pinOnClick_) return true;
            const Point2Dd world(centre_.x - GetWidth() / 2.0 + event.pointer.x,
                                 centre_.y - GetHeight() / 2.0 + event.pointer.y);
            const GeoPoint at = WebMercator::FromWorld(world, zoom_);
            SetPin(at);
            if (onPinMoved) onPinMoved(at);
            return true;
        }
        case UCEventType::MouseWheel:
            if (event.wheelDelta == 0) return false;
            Zoom(event.wheelDelta > 0 ? 1 : -1);
            return true;
        default:
            return false;
    }
}

} // namespace UltraCanvas
