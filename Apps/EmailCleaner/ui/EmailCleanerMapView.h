// Apps/EmailCleaner/ui/EmailCleanerMapView.h
// The map view: every sender in the analysed corpus drawn as a block, sized by
// how much of the mailbox it accounts for and coloured by what it mostly
// sends. Senders are nested under their sending domain, so a mailing house
// that writes from twenty addresses reads as one block until you drill into it.
//
// The drawing is UltraCanvasTreeMapElement (Plugins/Diagrams) — this view
// feeds it the hierarchy the Analytics layer shapes, and turns a click on a
// block back into a sender selection for the timetable and detail views.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers first (X11 macro hygiene — see the engine headers).
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSegmentedControl.h"
#include "Plugins/Diagrams/UltraCanvasTreeMapElement.h"

#include "EmailCleanerAnalytics.h"

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace EmailCleaner {

class MapView {
public:
    void SetAnalytics(Analytics* analytics) { analytics_ = analytics; }

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build(float x, float y,
                                                             float width, float height);

    // Rebuild the map for a filter (the account bar's, as the app assembled it).
    void Refresh(const MessageFilter& filter);

    // A block was selected: the sender address for a leaf, the domain for a
    // parent, empty for the "Other" pool. The app scopes the other views to it.
    std::function<void(const std::string& senderAddr, const std::string& domain)> onBlockSelected;

private:
    void RebuildTreeMap();
    void RebuildLegend();
    // Convert the analytics hierarchy into the element's node tree.
    std::shared_ptr<UltraCanvas::TreeMapNode> ToTreeMapNode(const MapNode& node);

    Analytics*    analytics_ = nullptr;
    MessageFilter filter_;
    SenderMetric  metric_ = SenderMetric::MessageCount;
    MapShape      shape_;
    MapNode       root_;

    // Block label -> which sender/domain it came from, so a selection callback
    // carrying only a name can be resolved back to the data.
    std::map<std::string, std::pair<std::string, std::string>> blockKeys_;

    std::shared_ptr<UltraCanvas::UltraCanvasContainer>         container_;
    std::shared_ptr<UltraCanvas::UltraCanvasTreeMapElement>    treeMap_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer>         legend_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>             summary_;
    std::shared_ptr<UltraCanvas::UltraCanvasSegmentedControl>  metricPicker_;
    std::shared_ptr<UltraCanvas::UltraCanvasSegmentedControl>  groupingPicker_;
};

} // namespace EmailCleaner
