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
#include "UltraCanvasDebug.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasGeolocation.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasMapView.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTextUtils.h"
#include "UltraCanvasWindow.h"
#include "UltraLocalisation/UltraLocalisation.h"
#include "UltraNet/UltraNetHttp.h"
#include "UltraNet/UltraNetUrl.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace UltraCanvas;

namespace ChatCube {

namespace {

constexpr int kWindowWidth = 680;
constexpr int kWindowHeight = 620;
constexpr int kMapHeight = 340;
constexpr int kMaxResults = 5;
const char* const kSearchServer = "https://nominatim.openstreetmap.org/";

std::string Coordinates(double latitude, double longitude) {
    return FormatFixedClassic(latitude, 6) + ", " + FormatFixedClassic(longitude, 6);
}

} // namespace

std::string LocationText(const MapPlace& place) {
    const std::string lat = FormatFixedClassic(place.latitude, 6);
    const std::string lon = FormatFixedClassic(place.longitude, 6);
    std::string text = "\xF0\x9F\x93\x8D " + (place.name.empty() ? Coordinates(place.latitude, place.longitude)
                                                                  : place.name);
    text += "\nhttps://www.openstreetmap.org/?mlat=" + lat + "&mlon=" + lon + "#map=" +
            std::to_string(place.zoom) + "/" + lat + "/" + lon;
    return text;
}

namespace {

// The number after `key` in `text` up to the next & or # or end; false
// when there is none or it is no number
bool NumberAfter(const std::string& text, const std::string& key, size_t from, double& out) {
    const size_t at = text.find(key, from);
    if (at == std::string::npos) return false;
    const size_t begin = at + key.size();
    size_t end = begin;
    while (end < text.size() && (std::isdigit(static_cast<unsigned char>(text[end])) || text[end] == '.' ||
                                 text[end] == '-' || text[end] == '+'))
        ++end;
    return end > begin && TryParseFloat(text.substr(begin, end - begin), out);
}

} // namespace

bool ParseLocationText(const std::string& text, MapPlace& place) {
    // An openstreetmap.org link with its marker - what LocationText writes,
    // and what openstreetmap.org's own Share gives
    const size_t link = text.find("openstreetmap.org/");
    if (link == std::string::npos) return false;
    double lat = 0, lon = 0;
    if (!NumberAfter(text, "mlat=", link, lat) || !NumberAfter(text, "mlon=", link, lon)) return false;
    if (lat < -90 || lat > 90 || lon < -180 || lon > 180) return false;
    place.latitude = lat;
    place.longitude = lon;
    place.zoom = 16;
    const size_t map = text.find("#map=", link);
    if (map != std::string::npos) {
        double zoom = 0;
        if (NumberAfter(text, "#map=", map, zoom) && zoom >= 1 && zoom <= 19) place.zoom = static_cast<int>(zoom);
    }
    // The name: the text before the link, without the pin and the line breaks
    std::string name = text.substr(0, text.rfind("http", link) == std::string::npos ? link : text.rfind("http", link));
    // The pin goes wherever it is ("You said: 📍 Alexanderplatz"): the card has its own
    const std::string pin = "\xF0\x9F\x93\x8D ";
    for (size_t at = name.find(pin); at != std::string::npos; at = name.find(pin)) name.erase(at, pin.size());
    const std::string bare = "\xF0\x9F\x93\x8D";
    for (size_t at = name.find(bare); at != std::string::npos; at = name.find(bare)) name.erase(at, bare.size());
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();
    size_t first = 0;
    while (first < name.size() && std::isspace(static_cast<unsigned char>(name[first]))) ++first;
    place.name = name.substr(first);
    place.url = text.substr(text.rfind("http", link) == std::string::npos ? link : text.rfind("http", link));
    const size_t stop = place.url.find_first_of(" \n\t");
    if (stop != std::string::npos) place.url = place.url.substr(0, stop);
    return true;
}

// ===== THE WINDOW =====

struct ChatCubeLocationWindow::State : public std::enable_shared_from_this<State> {
    std::string version;
    InsertHandler onInsert;

    std::shared_ptr<UltraCanvasWindow> window;
    std::shared_ptr<UltraCanvasTextInput> search;
    std::shared_ptr<UltraCanvasContainer> results;
    std::shared_ptr<UltraCanvasMapView> map;
    std::shared_ptr<UltraCanvasLabel> chosen;
    std::shared_ptr<UltraCanvasButton> myPosition;
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
                        Coordinates(p.latitude, p.longitude) +
                        (accuracy.empty() ? std::string() : "  (" + accuracy + ")"));
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
                    d->accuracy.clear();
                    d->map->CentreOn(GeoPoint{q.latitude, q.longitude}, q.zoom);
                    d->map->SetPin(GeoPoint{q.latitude, q.longitude});
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
            if (!ok) {
                debugOutput << "ChatCube: Nominatim answered " << response.statusCode
                            << (response.transferError.empty() ? "" : " (" + response.transferError + ")") << std::endl;
            }
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
        const MapPlace chosenPlace = place;
        InsertHandler handler = onInsert;
        if (window) window->Close();
        handler(chosenPlace);
    }

    // My position: the system's location service asked, the map centred
    // on the answer with the pin there, named like a click
    void LocateMe() {
        myPosition->SetDisabled(true);
        chosen->SetText(Tr("Finding where this computer is\xE2\x80\xA6"));
        if (window) window->RequestRedraw();
        std::weak_ptr<State> weak = weak_from_this();
        UltraCanvasGeolocation::RequestPosition([weak](const GeoPositionResult& r) {
            auto d = weak.lock();
            if (!d || !d->window) return;
            d->myPosition->SetDisabled(false);
            if (!r.ok) {
                d->chosen->SetText(TrF("Your position is not known: {0}", r.error));
                d->window->RequestRedraw();
                return;
            }
            // The zoom by how sure the answer is: a street for a GPS fix,
            // the town for a guess from the network
            const int zoom = r.accuracyMeters < 0 ? 15 : r.accuracyMeters < 150 ? 17 : r.accuracyMeters < 2000 ? 14 : 11;
            d->map->CentreOn(GeoPoint{r.latitude, r.longitude}, zoom);
            d->map->SetPin(GeoPoint{r.latitude, r.longitude});
            d->accuracy = UltraCanvasGeolocation::AccuracyText(r.accuracyMeters);
            d->PinMoved(r.latitude, r.longitude);
        });
    }
    std::string accuracy;   // "about 30 m": said beside the place found by My position
};

ChatCubeLocationWindow::ChatCubeLocationWindow(std::string version, InsertHandler onInsert) {
    state_ = std::make_shared<State>();
    state_->version = std::move(version);
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
    MapTiles::SetUserAgent(d->UserAgent());
    d->accuracy.clear();
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
        if (auto s = weak.lock()) s->window.reset();
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
    d->map = std::make_shared<UltraCanvasMapView>("cc-loc-map");
    const CSSLayout::Position fill{CSSLayout::Dimension::Px(0), CSSLayout::Dimension::Px(0),
                                   CSSLayout::Dimension::Px(0), CSSLayout::Dimension::Px(0)};
    d->map->layoutItem.SetPositionType(CSSLayout::PositionType::Absolute).SetPositionInsets(fill);
    d->map->onPinMoved = [weak](const GeoPoint& at) {
        if (auto s = weak.lock()) {
            s->accuracy.clear();
            s->PinMoved(at.latitude, at.longitude);
        }
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

    // ----- the place chosen, and the buttons -----
    d->chosen = MakeLabel("cc-loc-chosen", Tr("No place chosen yet."), 10.0f);
    // A long address wraps onto a second line rather than running off
    d->chosen->SetWrap(TextWrap::WrapWord);
    d->chosen->size.width = CSSLayout::Dimension::Px(static_cast<float>(kWindowWidth - 32));
    d->window->AddChild(d->chosen);
    auto bottom = MakeRow("cc-loc-bottom", 8);
    // My position: where the system's location service says this computer
    // is (hidden where the framework has no location backend)
    d->myPosition = MakeIconButton("cc-loc-me", "my-position", Tr("My position"), 140);
    d->myPosition->SetTooltip(Tr("Put the pin where this computer is"));
    d->myPosition->SetVisible(UltraCanvasGeolocation::IsAvailable());
    d->myPosition->SetOnClick([weak]() {
        if (auto s = weak.lock()) s->LocateMe();
    });
    bottom->AddChild(d->myPosition);
    auto spacer = std::make_shared<UltraCanvasContainer>("cc-loc-spacer");
    spacer->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    bottom->AddChild(spacer);
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
