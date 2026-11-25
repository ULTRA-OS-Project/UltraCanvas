// Apps/ChatCube/ui/ChatCubeLocationWindow.h
// A place for the message: the Location feature's window. A search field
// asks OpenStreetMap's Nominatim for places by name or address, the results
// list under it; the map (OpenStreetMap's tiles) shows the chosen one with a
// pin (the framework's UltraCanvasMapView). A click on the map moves the
// pin there (and names the spot, asking Nominatim what is there), a drag
// moves the map, the wheel and the +/- buttons zoom; My position puts the
// pin where the system's location service says this computer is
// (UltraCanvasGeolocation). Insert hands the place to the message being
// written: its name and an openstreetmap.org link - which ChatCube's chat
// shows as a map card (ParseLocationText), and any other program as a link.
//
//   +--------------------------------------------------------------+
//   | [ Search a place or an address         ] [Search]            |
//   | Alexanderplatz, Mitte, Berlin, Deutschland                   |
//   | Alexanderplatz, Wien, ...                                    |
//   | +----------------------------------------------------------+ |
//   | |                     the map, the pin                 [+] | |
//   | |                                                      [-] | |
//   | |                         (c) OpenStreetMap contributors   | |
//   | +----------------------------------------------------------+ |
//   | Alexanderplatz, Berlin - 52.521918, 13.413215                 |
//   | [My position]                             [Cancel] [Insert]  |
//   +--------------------------------------------------------------+
//
// OpenStreetMap's tile and Nominatim usage policies are kept: every request
// names ChatCube in its User-Agent, a search goes out only on Enter or the
// button (never while typing), at most two tiles load at once, and a tile
// loaded once is not asked for again while ChatCube runs. The map shows the
// "(c) OpenStreetMap contributors" attribution, and so does the card.
//
// Version: 0.2.0 - the map is the framework's UltraCanvasMapView; My position
//                  (UltraCanvasGeolocation); no map picture: the chat shows a
//                  location as a map card (ParseLocationText)
// Version: 0.1.0
// Last Modified: 2026-11-23
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {
class UltraCanvasWindowBase;
}

namespace ChatCube {

struct MapPlace {
    std::string name;       // "Alexanderplatz, Mitte, Berlin" (Nominatim's), or the coordinates
    double latitude = 0;
    double longitude = 0;
    int zoom = 16;          // the map's zoom when it was chosen, for the link and the card
    std::string url;        // the link, as found in a message (ParseLocationText)
};

// The text a place goes into a message as: "📍 <name>" and the
// openstreetmap.org link with the pin, the numbers dot-decimal
std::string LocationText(const MapPlace& place);
// A message that holds an openstreetmap.org link with a marker (mlat,
// mlon) - LocationText's, or one shared from openstreetmap.org: the place,
// its zoom, the words before the link as its name, and the link. False for
// any other text.
bool ParseLocationText(const std::string& text, MapPlace& place);

class ChatCubeLocationWindow {
public:
    // Insert: the place chosen
    using InsertHandler = std::function<void(const MapPlace& place)>;

    // `version` names ChatCube in the requests' User-Agent
    ChatCubeLocationWindow(std::string version, InsertHandler onInsert);
    ~ChatCubeLocationWindow();
    ChatCubeLocationWindow(const ChatCubeLocationWindow&) = delete;
    ChatCubeLocationWindow& operator=(const ChatCubeLocationWindow&) = delete;

    // Opens the window over `parent`, or raises it when it is open
    void Show(UltraCanvas::UltraCanvasWindowBase* parent);
    void Close();

    struct State;

private:
    std::shared_ptr<State> state_;
};

} // namespace ChatCube
