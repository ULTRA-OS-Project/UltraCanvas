// Apps/ChatCube/ui/ChatCubeLocationWindow.cpp
// See the header.
// Version: 0.1.0
// Last Modified: 2026-11-23
// Author: UltraCanvas Framework / ULTRA OS

#include "ChatCubeLocationWindow.h"

#include "ChatCubeControls.h"
#include "ChatCubeTheme.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasClipboardDib.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTextUtils.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasWindow.h"
#include "UltraLocalisation/UltraLocalisation.h"
#include "UltraNet/UltraNetHttp.h"
#include "UltraNet/UltraNetUrl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <deque>
#include <filesystem>
#include <map>
#include <set>
#include <vector>

using namespace UltraCanvas;

namespace ChatCube {

namespace {

constexpr int kTileSize = 256;
constexpr int kMinZoom = 2;
constexpr int kMaxZoom = 19;
constexpr int kMaxTileRequests = 2;           // OpenStreetMap's tile policy: two connections at most
constexpr int kWindowWidth = 680;
constexpr int kWindowHeight = 620;
constexpr int kMapHeight = 340;
constexpr int kMaxResults = 5;
constexpr int kPictureWidth = 640;            // the map picture sent with the message
constexpr int kPictureHeight = 400;
constexpr double kPi = 3.14159265358979323846;
const char* const kTileServer = "https://tile.openstreetmap.org/";
const char* const kSearchServer = "https://nominatim.openstreetmap.org/";

// ----- Web Mercator: degrees to world pixels at a zoom and back -----

double WorldSize(int zoom) {
    return static_cast<double>(kTileSize) * std::pow(2.0, zoom);
}

Point2Dd ToWorld(double latitude, double longitude, int zoom) {
    const double size = WorldSize(zoom);
    const double lat = std::clamp(latitude, -85.05112878, 85.05112878) * kPi / 180.0;
    const double x = (longitude + 180.0) / 360.0 * size;
    const double y = (1.0 - std::log(std::tan(lat) + 1.0 / std::cos(lat)) / kPi) / 2.0 * size;
    return Point2Dd(x, y);
}

void FromWorld(const Point2Dd& world, int zoom, double& latitude, double& longitude) {
    const double size = WorldSize(zoom);
    longitude = world.x / size * 360.0 - 180.0;
    const double n = kPi - 2.0 * kPi * world.y / size;
    latitude = 180.0 / kPi * std::atan(0.5 * (std::exp(n) - std::exp(-n)));
    while (longitude < -180.0) longitude += 360.0;
    while (longitude >= 180.0) longitude -= 360.0;
}

std::string TileKey(int zoom, int x, int y) {
    return std::to_string(zoom) + "/" + std::to_string(x) + "/" + std::to_string(y);
}

// ----- the tiles: fetched two at a time, kept while ChatCube runs -----

// Shared with the requests in flight, which may finish after the window has
// closed: they hold it weakly.
struct TileStore {
    std::string userAgent;
    std::map<std::string, std::vector<uint8_t>> bytes;     // the PNGs as they came
    std::map<std::string, UCImagePtr> images;              // ... decoded, for drawing
    std::set<std::string> failed;
    std::deque<std::string> queue;
    std::set<std::string> queued;
    int active = 0;
    std::function<void()> changed;                         // a tile arrived: redraw

    void Request(const std::string& key, const std::shared_ptr<TileStore>& self) {
        if (bytes.count(key) || failed.count(key) || queued.count(key)) return;
        queued.insert(key);
        queue.push_back(key);
        Pump(self);
    }

    void Pump(const std::shared_ptr<TileStore>& self) {
        auto* app = UltraCanvasApplicationBase::GetCurrent();
        while (active < kMaxTileRequests && !queue.empty() && app) {
            const std::string key = queue.front();
            queue.pop_front();
            ++active;
            UltraNetHttpRequest request;
            request.url = std::string(kTileServer) + key + ".png";
            request.headers.Set("User-Agent", userAgent);
            std::weak_ptr<TileStore> weak = self;
            UltraNet_HttpRequestAsync(request, [weak, key, app](const UltraNetResponse& response) {
                const bool ok = response.IsComplete() && response.IsSuccess() && !response.body.empty();
                std::vector<uint8_t> body = ok ? response.body : std::vector<uint8_t>();
                app->PostToUIThread([weak, key, ok, body = std::move(body)]() mutable {
                    auto store = weak.lock();
                    if (!store) return;
                    --store->active;
                    store->queued.erase(key);
                    if (ok) {
                        store->images[key] = UCImage::LoadFromMemory(body);
                        store->bytes[key] = std::move(body);
                    } else {
                        store->failed.insert(key);
                    }
                    store->Pump(store);
                    if (store->changed) store->changed();
                });
            });
        }
    }
};

// ===== THE MAP =====

// ui-reuse-exempt: a slippy map of OpenStreetMap's tiles with a pin - the
// framework has no map element; the tiles are pictures from the network,
// drawn where Web Mercator puts them.
class MapView : public UltraCanvasUIElement {
public:
    MapView(const std::string& id, std::shared_ptr<TileStore> tiles)
        : UltraCanvasUIElement(id, 0, 0, 0, 0), tiles_(std::move(tiles)) {}

    // The pin moved by a click (the caller names the spot)
    std::function<void(double latitude, double longitude)> onPinMoved;

    void CentreOn(double latitude, double longitude, int zoom) {
        zoom_ = std::clamp(zoom, kMinZoom, kMaxZoom);
        centre_ = ToWorld(latitude, longitude, zoom_);
        RequestRedraw();
    }
    void SetPin(double latitude, double longitude) {
        pinLatitude_ = latitude;
        pinLongitude_ = longitude;
        hasPin_ = true;
        RequestRedraw();
    }
    void Zoom(int by) {
        double lat = 0, lon = 0;
        FromWorld(centre_, zoom_, lat, lon);
        CentreOn(lat, lon, zoom_ + by);
    }
    int GetZoom() const { return zoom_; }
    bool HasPin() const { return hasPin_; }

    // The map around the pin as straight RGBA, from the tiles loaded so far
    // (those not loaded yet are grey), the pin drawn in the middle
    bool PictureAroundPin(int width, int height, ClipboardDib::RgbaImage& out) const {
        if (!hasPin_) return false;
        out.width = width;
        out.height = height;
        out.pixels.assign(static_cast<size_t>(width) * height * 4, 0);
        for (size_t i = 0; i < out.pixels.size(); i += 4) {
            out.pixels[i] = out.pixels[i + 1] = out.pixels[i + 2] = 224;
            out.pixels[i + 3] = 255;
        }
        const Point2Dd pin = ToWorld(pinLatitude_, pinLongitude_, zoom_);
        const double left = pin.x - width / 2.0, top = pin.y - height / 2.0;
        const int tiles = 1 << zoom_;
        const int firstX = static_cast<int>(std::floor(left / kTileSize));
        const int firstY = static_cast<int>(std::floor(top / kTileSize));
        const int lastX = static_cast<int>(std::floor((left + width) / kTileSize));
        const int lastY = static_cast<int>(std::floor((top + height) / kTileSize));
        for (int ty = firstY; ty <= lastY; ++ty) {
            if (ty < 0 || ty >= tiles) continue;
            for (int tx = firstX; tx <= lastX; ++tx) {
                const int wrapped = ((tx % tiles) + tiles) % tiles;
                auto it = tiles_->bytes.find(TileKey(zoom_, wrapped, ty));
                if (it == tiles_->bytes.end()) continue;
                ClipboardDib::RgbaImage tile;
                if (!ClipboardDib::DecodePng(it->second.data(), it->second.size(), tile)) continue;
                const int ox = static_cast<int>(std::lround(tx * kTileSize - left));
                const int oy = static_cast<int>(std::lround(ty * kTileSize - top));
                for (int row = 0; row < tile.height; ++row) {
                    const int y = oy + row;
                    if (y < 0 || y >= height) continue;
                    for (int col = 0; col < tile.width; ++col) {
                        const int x = ox + col;
                        if (x < 0 || x >= width) continue;
                        const uint8_t* s = &tile.pixels[(static_cast<size_t>(row) * tile.width + col) * 4];
                        uint8_t* d = &out.pixels[(static_cast<size_t>(y) * width + x) * 4];
                        d[0] = s[0];
                        d[1] = s[1];
                        d[2] = s[2];
                        d[3] = 255;
                    }
                }
            }
        }
        DrawPinPixels(out, width / 2, height / 2);
        return true;
    }

    void Render(IRenderContext* ctx, const Rect2Df&) override {
        const float w = GetWidth(), h = GetHeight();
        if (w <= 0 || h <= 0) return;
        ctx->PushState();
        ctx->ClipRect(Rect2Dd(0, 0, w, h));
        ctx->SetFillPaint(Color(224, 224, 224, 255));
        ctx->FillRectangle(Rect2Dd(0, 0, w, h));
        const double left = centre_.x - w / 2.0, top = centre_.y - h / 2.0;
        const int tiles = 1 << zoom_;
        const int firstX = static_cast<int>(std::floor(left / kTileSize));
        const int firstY = static_cast<int>(std::floor(top / kTileSize));
        const int lastX = static_cast<int>(std::floor((left + w) / kTileSize));
        const int lastY = static_cast<int>(std::floor((top + h) / kTileSize));
        for (int ty = firstY; ty <= lastY; ++ty) {
            if (ty < 0 || ty >= tiles) continue;
            for (int tx = firstX; tx <= lastX; ++tx) {
                const int wrapped = ((tx % tiles) + tiles) % tiles;
                const std::string key = TileKey(zoom_, wrapped, ty);
                const Rect2Dd cell(tx * kTileSize - left, ty * kTileSize - top, kTileSize, kTileSize);
                auto it = tiles_->images.find(key);
                if (it != tiles_->images.end() && it->second && it->second->IsValid()) {
                    ctx->DrawImage(*it->second, cell, ImageFitMode::Fill);
                } else {
                    tiles_->Request(key, tiles_);
                }
            }
        }
        if (hasPin_) {
            const Point2Dd pin = ToWorld(pinLatitude_, pinLongitude_, zoom_);
            DrawPin(ctx, pin.x - left, pin.y - top);
        }
        // The attribution OpenStreetMap asks for, on the map itself
        const std::string credit = "\xC2\xA9 OpenStreetMap contributors";
        ctx->SetFontSize(8.5);
        const Point2Di size = ctx->GetTextDimension(credit);
        const Rect2Dd box(w - size.x - 12, h - size.y - 6, size.x + 10, size.y + 4);
        ctx->SetFillPaint(Color(255, 255, 255, 200));
        ctx->FillRectangle(box);
        ctx->SetTextPaint(Color(40, 40, 40, 255));
        ctx->DrawText(credit, Point2Dd(box.x + 5, box.y + 2));
        ctx->PopState();
    }

    bool OnEvent(const UCEvent& event) override {
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
                    centre_.y = std::clamp(centre_.y, 0.0, WorldSize(zoom_));
                    RequestRedraw();
                }
                lastAt_ = event.pointer;
                return true;
            case UCEventType::MouseUp: {
                if (!pressed_ || event.button != UCMouseButton::Left) return false;
                pressed_ = false;
                if (dragged_) return true;
                // A click: the pin goes there
                const Point2Dd world(centre_.x - GetWidth() / 2.0 + event.pointer.x,
                                     centre_.y - GetHeight() / 2.0 + event.pointer.y);
                double lat = 0, lon = 0;
                FromWorld(world, zoom_, lat, lon);
                SetPin(lat, lon);
                if (onPinMoved) onPinMoved(lat, lon);
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

private:
    static void DrawPin(IRenderContext* ctx, double x, double y) {
        ctx->SetFillPaint(Color(0, 0, 0, 70));
        ctx->FillCircle(Point2Dd(x, y + 2), 11);
        ctx->SetFillPaint(Color(220, 50, 47, 255));
        ctx->FillCircle(Point2Dd(x, y), 10);
        ctx->SetStrokePaint(Color(255, 255, 255, 255));
        ctx->SetStrokeWidth(2.5);
        ctx->DrawCircle(Point2Dd(x, y), 10);
        ctx->SetFillPaint(Color(255, 255, 255, 255));
        ctx->FillCircle(Point2Dd(x, y), 3.5);
    }

    // The same pin into RGBA pixels: a red disc with a white ring and a
    // white dot, edges softened by coverage
    static void DrawPinPixels(ClipboardDib::RgbaImage& image, int cx, int cy) {
        for (int y = cy - 13; y <= cy + 13; ++y) {
            if (y < 0 || y >= image.height) continue;
            for (int x = cx - 13; x <= cx + 13; ++x) {
                if (x < 0 || x >= image.width) continue;
                const double d = std::hypot(x - cx, y - cy);
                int r = -1, g = 0, b = 0;
                double cover = 1.0;
                if (d <= 3.5) { r = 255; g = 255; b = 255; }
                else if (d <= 9.0) { r = 220; g = 50; b = 47; }
                else if (d <= 11.5) { r = 255; g = 255; b = 255; cover = std::clamp(11.5 - d + 0.5, 0.0, 1.0); }
                if (r < 0) continue;
                uint8_t* p = &image.pixels[(static_cast<size_t>(y) * image.width + x) * 4];
                p[0] = static_cast<uint8_t>(std::lround(p[0] * (1 - cover) + r * cover));
                p[1] = static_cast<uint8_t>(std::lround(p[1] * (1 - cover) + g * cover));
                p[2] = static_cast<uint8_t>(std::lround(p[2] * (1 - cover) + b * cover));
            }
        }
    }

    std::shared_ptr<TileStore> tiles_;
    Point2Dd centre_ = ToWorld(30.0, 10.0, 3);
    int zoom_ = 3;
    bool hasPin_ = false;
    double pinLatitude_ = 0, pinLongitude_ = 0;
    bool pressed_ = false, dragged_ = false;
    Point2Di pressAt_, lastAt_;
};

// The tile store outlives every window: a tile loaded once is not asked
// for again while ChatCube runs
std::shared_ptr<TileStore> SharedTiles(const std::string& userAgent) {
    static std::shared_ptr<TileStore> store = std::make_shared<TileStore>();
    store->userAgent = userAgent;
    return store;
}

std::string Coordinates(double latitude, double longitude) {
    return FormatFixedClassic(latitude, 6) + ", " + FormatFixedClassic(longitude, 6);
}

} // namespace

std::string LocationText(const MapPlace& place, bool attribution) {
    const std::string lat = FormatFixedClassic(place.latitude, 6);
    const std::string lon = FormatFixedClassic(place.longitude, 6);
    std::string text = "\xF0\x9F\x93\x8D " + (place.name.empty() ? Coordinates(place.latitude, place.longitude)
                                                                  : place.name);
    text += "\nhttps://www.openstreetmap.org/?mlat=" + lat + "&mlon=" + lon + "#map=" +
            std::to_string(place.zoom) + "/" + lat + "/" + lon;
    if (attribution) text += "\n" + Tr("Map: \xC2\xA9 OpenStreetMap contributors");
    return text;
}

// ===== THE WINDOW =====

struct ChatCubeLocationWindow::State : public std::enable_shared_from_this<State> {
    std::string version;
    std::string cacheFolder;
    InsertHandler onInsert;
    std::shared_ptr<TileStore> tiles;

    std::shared_ptr<UltraCanvasWindow> window;
    std::shared_ptr<UltraCanvasTextInput> search;
    std::shared_ptr<UltraCanvasContainer> results;
    std::shared_ptr<MapView> map;
    std::shared_ptr<UltraCanvasLabel> chosen;
    std::shared_ptr<UltraCanvasCheckbox> withPicture;
    std::shared_ptr<UltraCanvasButton> insert;
    MapPlace place;
    bool hasPlace = false;
    int searchSerial = 0;     // a newer search or click wins over an older answer

    std::string UserAgent() const {
        return "ChatCube/" + version + " (ULTRA OS messenger; UltraCanvas)";
    }

    void SetPlace(const MapPlace& p) {
        place = p;
        hasPlace = true;
        chosen->SetText((p.name.empty() ? Tr("A place on the map") : p.name) + "  -  " +
                        Coordinates(p.latitude, p.longitude));
        insert->SetDisabled(false);
    }

    void ShowResults(const JSONValue& list) {
        results->ClearChildren();
        int n = 0;
        if (list.IsArray()) {
            for (size_t i = 0; i < list.GetSize() && n < kMaxResults; ++i) {
                const JSONValue& item = list[i];
                double lat = 0, lon = 0;
                if (!TryParseFloat(item["lat"].GetString(), lat) || !TryParseFloat(item["lon"].GetString(), lon)) continue;
                MapPlace p;
                p.name = item["display_name"].GetString();
                p.latitude = lat;
                p.longitude = lon;
                auto b = std::make_shared<UltraCanvasButton>("cc-loc-result-" + std::to_string(n++), 0, 0,
                                                             kWindowWidth - 40, 26, p.name);
                ButtonStyle st = b->GetStyle();
                st.textAlign = TextAlignment::Left;
                st.cornerRadius = 6.0f;
                st.borderWidth = 0.0f;
                st.fontSize = 9.5f;
                st.normalColor = Colors::Transparent;
                st.hoverColor = L().hover;
                st.pressedColor = L().pressed;
                st.normalTextColor = st.hoverTextColor = st.pressedTextColor = L().ink;
                b->SetStyle(st);
                b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                std::weak_ptr<State> weak = weak_from_this();
                b->SetOnClick([weak, p]() {
                    auto d = weak.lock();
                    if (!d) return;
                    MapPlace q = p;
                    q.zoom = 16;
                    d->map->CentreOn(q.latitude, q.longitude, q.zoom);
                    d->map->SetPin(q.latitude, q.longitude);
                    d->SetPlace(q);
                });
                results->AddChild(b);
            }
        }
        if (n == 0) results->AddChild(MakeLabel("cc-loc-none", Tr("Nothing found. Try another name or an address."),
                                                9.5f, &Look::inkMuted));
        results->InvalidateLayout();
        if (window) window->RequestRedraw();
    }

    // GET <kSearchServer><path>, the answer on the UI thread as JSON (Null
    // on any failure); only the newest of several asked
    void AskNominatim(const std::string& path, std::function<void(State&, const JSONValue&)> answered) {
        auto* app = UltraCanvasApplicationBase::GetCurrent();
        if (!app) return;
        const int serial = ++searchSerial;
        UltraNetHttpRequest request;
        request.url = std::string(kSearchServer) + path;
        request.headers.Set("User-Agent", UserAgent());
        const std::string language = UltraLocalisation::CurrentLanguage();
        if (!language.empty()) request.headers.Set("Accept-Language", language);
        std::weak_ptr<State> weak = weak_from_this();
        UltraNet_HttpRequestAsync(request, [weak, serial, app, answered](const UltraNetResponse& response) {
            const bool ok = response.IsComplete() && response.IsSuccess();
            std::string body = ok ? response.GetBodyAsString() : std::string();
            app->PostToUIThread([weak, serial, ok, body = std::move(body), answered]() {
                auto d = weak.lock();
                if (!d || serial != d->searchSerial || !d->window) return;
                if (!ok) {
                    d->results->ClearChildren();
                    d->results->AddChild(MakeLabel("cc-loc-failed",
                                                   Tr("OpenStreetMap could not be reached. Check the connection."),
                                                   9.5f, &Look::danger));
                    d->window->RequestRedraw();
                    return;
                }
                answered(*d, JSON::Parse(body));
            });
        });
    }

    void Search() {
        const std::string text = search->GetText();
        if (text.empty()) return;
        results->ClearChildren();
        results->AddChild(MakeLabel("cc-loc-searching", Tr("Searching\xE2\x80\xA6"), 9.5f, &Look::inkMuted));
        window->RequestRedraw();
        AskNominatim("search?format=jsonv2&limit=" + std::to_string(kMaxResults) + "&q=" + UltraNet_UrlEncode(text),
                     [](State& d, const JSONValue& list) { d.ShowResults(list); });
    }

    // The pin put somewhere by hand: the coordinates at once, the name of
    // what is there when Nominatim says
    void PinMoved(double lat, double lon) {
        MapPlace p;
        p.latitude = lat;
        p.longitude = lon;
        p.zoom = map->GetZoom();
        SetPlace(p);
        AskNominatim("reverse?format=jsonv2&zoom=18&lat=" + FormatFixedClassic(lat, 6) + "&lon=" +
                         FormatFixedClassic(lon, 6),
                     [p](State& d, const JSONValue& answer) {
                         if (!answer.IsObject()) return;
                         MapPlace named = p;
                         named.name = answer["display_name"].GetString();
                         if (!d.hasPlace || d.place.latitude != p.latitude || d.place.longitude != p.longitude) return;
                         d.SetPlace(named);
                         d.window->RequestRedraw();
                     });
    }

    void Insert() {
        if (!hasPlace || !onInsert) return;
        place.zoom = map->GetZoom();
        std::string picture;
        if (withPicture->IsChecked()) {
            ClipboardDib::RgbaImage image;
            std::vector<uint8_t> png;
            if (map->PictureAroundPin(kPictureWidth, kPictureHeight, image) && ClipboardDib::EncodePng(image, png)) {
                std::error_code ec;
                std::filesystem::create_directories(PathFromUtf8(cacheFolder), ec);
                char stamp[32];
                const std::time_t now = std::time(nullptr);
                std::tm local{};
#if defined(_WIN32) || defined(_WIN64)
                localtime_s(&local, &now);
#else
                localtime_r(&now, &local);
#endif
                std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
                const std::string path = PathToUtf8(PathFromUtf8(cacheFolder) / PathFromUtf8(std::string("Map-") + stamp + ".png"));
                std::FILE* f = OpenFileUtf8(path, "wb");
                const bool written = f && std::fwrite(png.data(), 1, png.size(), f) == png.size();
                if (f) std::fclose(f);
                if (written) picture = path;
            }
        }
        const MapPlace chosenPlace = place;
        InsertHandler handler = onInsert;
        if (window) window->Close();
        handler(chosenPlace, picture);
    }
};

ChatCubeLocationWindow::ChatCubeLocationWindow(std::string version, std::string cacheFolder, InsertHandler onInsert) {
    state_ = std::make_shared<State>();
    state_->version = std::move(version);
    state_->cacheFolder = std::move(cacheFolder);
    state_->onInsert = std::move(onInsert);
}

ChatCubeLocationWindow::~ChatCubeLocationWindow() {
    Close();
}

void ChatCubeLocationWindow::Close() {
    if (state_ && state_->window) {
        auto w = state_->window;
        state_->window.reset();
        w->Close();
    }
}

void ChatCubeLocationWindow::Show(UltraCanvasWindowBase* parent) {
    State* d = state_.get();
    if (d->window) {
        d->window->RaiseAndFocus();
        return;
    }
    d->tiles = SharedTiles(d->UserAgent());
    d->hasPlace = false;

    WindowConfig wc;
    wc.title = Tr("ChatCube - Insert a location");
    wc.width = kWindowWidth;
    wc.height = kWindowHeight;
    wc.resizable = true;
    wc.type = WindowType::Dialog;
    wc.parentWindow = parent;
    d->window = CreateWindow(wc);
    if (!d->window || !d->window->IsCreated()) {
        d->window.reset();
        return;
    }
    d->window->layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    d->window->SetPadding(14, 16, 14, 16);
    d->window->SetBackgroundColor(L().panel);
    std::weak_ptr<State> weak = state_;
    d->window->onWindowClosed = [weak]() {
        if (auto s = weak.lock()) {
            s->window.reset();
            if (s->tiles) s->tiles->changed = nullptr;
        }
    };

    // ----- the search -----
    auto searchRow = MakeRow("cc-loc-search-row", 8);
    d->search = MakeInput("cc-loc-search", Tr("Search a place or an address"), 480, true);
    d->search->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    d->search->onEnterPressed = [weak](const std::string&) {
        if (auto s = weak.lock()) s->Search();
        return true;
    };
    searchRow->AddChild(d->search);
    auto searchButton = MakeIconButton("cc-loc-search-go", "search", Tr("Search"), 100, true);
    searchButton->SetOnClick([weak]() {
        if (auto s = weak.lock()) s->Search();
    });
    searchRow->AddChild(searchButton);
    d->window->AddChild(searchRow);

    d->results = std::make_shared<UltraCanvasContainer>("cc-loc-results");
    d->results->layout.SetFlexColumn().SetFlexGap(0).SetFlexAlignItems(CSSLayout::AlignItems::Start);
    d->results->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    d->results->AddChild(MakeLabel("cc-loc-hint",
                                   Tr("Find a place by its name or address, or click on the map to put the pin there."),
                                   9.5f, &Look::inkMuted));
    d->window->AddChild(d->results);

    // ----- the map, with + and - over its corner -----
    auto mapBox = std::make_shared<UltraCanvasContainer>("cc-loc-map-box");
    mapBox->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    mapBox->size.height = CSSLayout::Dimension::Px(static_cast<float>(kMapHeight));
    mapBox->SetBorders(1.0f, L().line, 10.0f);
    d->map = std::make_shared<MapView>("cc-loc-map", d->tiles);
    const CSSLayout::Position fill{CSSLayout::Dimension::Px(0), CSSLayout::Dimension::Px(0),
                                   CSSLayout::Dimension::Px(0), CSSLayout::Dimension::Px(0)};
    d->map->layoutItem.SetPositionType(CSSLayout::PositionType::Absolute).SetPositionInsets(fill);
    d->map->onPinMoved = [weak](double lat, double lon) {
        if (auto s = weak.lock()) s->PinMoved(lat, lon);
    };
    mapBox->AddChild(d->map);
    auto zoomBox = std::make_shared<UltraCanvasContainer>("cc-loc-zoom");
    zoomBox->layout.SetFlexColumn().SetFlexGap(6);
    zoomBox->layoutItem.SetPositionType(CSSLayout::PositionType::Absolute)
        .SetPositionInsets({CSSLayout::Dimension::Px(10), CSSLayout::Dimension::Px(10), CSSLayout::Dimension::Auto(),
                            CSSLayout::Dimension::Auto()});
    auto zoomIn = MakeIconButton("cc-loc-zoom-in", "plus", "", kIconButton, false, false);   // solid over the map
    zoomIn->SetTooltip(Tr("Zoom in"));
    zoomIn->SetOnClick([weak]() {
        if (auto s = weak.lock()) s->map->Zoom(1);
    });
    auto zoomOut = MakeIconButton("cc-loc-zoom-out", "minus", "", kIconButton, false, false);
    zoomOut->SetTooltip(Tr("Zoom out"));
    zoomOut->SetOnClick([weak]() {
        if (auto s = weak.lock()) s->map->Zoom(-1);
    });
    zoomBox->AddChild(zoomIn);
    zoomBox->AddChild(zoomOut);
    mapBox->AddChild(zoomBox);
    d->window->AddChild(mapBox);
    d->tiles->changed = [weak]() {
        if (auto s = weak.lock(); s && s->map) s->map->RequestRedraw();
    };

    // ----- the place chosen, and the buttons -----
    d->chosen = MakeLabel("cc-loc-chosen", Tr("No place chosen yet."), 10.0f);
    // A long address wraps onto a second line rather than running off
    d->chosen->SetWrap(TextWrap::WrapWord);
    d->chosen->size.width = CSSLayout::Dimension::Px(static_cast<float>(kWindowWidth - 32));
    d->window->AddChild(d->chosen);
    auto bottom = MakeRow("cc-loc-bottom", 8);
    d->withPicture = UltraCanvasCheckbox::CreateCheckbox("cc-loc-picture", 0, 0, 300, 24,
                                                          Tr("With a picture of the map"), true);
    d->withPicture->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    bottom->AddChild(d->withPicture);
    auto cancel = MakeIconButton("cc-loc-cancel", "close", Tr("Cancel"), 104);
    cancel->SetOnClick([weak]() {
        if (auto s = weak.lock(); s && s->window) s->window->Close();
    });
    bottom->AddChild(cancel);
    d->insert = MakeIconButton("cc-loc-insert", "location", Tr("Insert"), 104, true);
    d->insert->SetDisabled(true);
    d->insert->SetOnClick([weak]() {
        if (auto s = weak.lock()) s->Insert();
    });
    bottom->AddChild(d->insert);
    d->window->AddChild(bottom);

    d->window->Show();
    d->search->SetFocus(true);
}

} // namespace ChatCube
