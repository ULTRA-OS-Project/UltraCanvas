// Apps/ChatCube/ui/ChatCubeLocationWindow.h
// A place for the message: the Location feature's window. A search field
// asks OpenStreetMap's Nominatim for places by name or address, the results
// list under it; the map (OpenStreetMap's tiles) shows the chosen one with a
// pin. A click on the map moves the pin there (and names the spot, asking
// Nominatim what is there), a drag moves the map, the wheel and the +/-
// buttons zoom. Insert hands the place to the message being written: its
// name and an openstreetmap.org link into the text, and - unless unticked -
// a picture of the map around the pin as an attachment.
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
//   | [x] With a map picture                    [Cancel] [Insert]  |
//   +--------------------------------------------------------------+
//
// OpenStreetMap's tile and Nominatim usage policies are kept: every request
// names ChatCube in its User-Agent, a search goes out only on Enter or the
// button (never while typing), at most two tiles load at once, and a tile
// loaded once is not asked for again while ChatCube runs. The map shows the
// "(c) OpenStreetMap contributors" attribution, and the inserted text says it
// under the link when a map picture goes with it.
//
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
    int zoom = 16;          // the map's zoom when it was chosen, for the link
};

// The text a place goes into a message as: "📍 <name>" and the
// openstreetmap.org link with the pin, the numbers dot-decimal; with
// `attribution` the map picture's credit under it
std::string LocationText(const MapPlace& place, bool attribution);

class ChatCubeLocationWindow {
public:
    // Insert: the place, and the map picture's PNG ("" when unticked or it
    // could not be made) - a file of ChatCube's cache, the caller's to stage
    using InsertHandler = std::function<void(const MapPlace& place, const std::string& picturePath)>;

    // `version` names ChatCube in the requests' User-Agent; `cacheFolder`
    // (UTF-8) is where the map picture is written
    ChatCubeLocationWindow(std::string version, std::string cacheFolder, InsertHandler onInsert);
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
