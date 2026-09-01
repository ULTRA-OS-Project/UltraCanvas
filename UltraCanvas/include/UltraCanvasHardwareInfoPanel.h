// include/UltraCanvasHardwareInfoPanel.h
// Ready-made "System information" view: the UltraCanvasHardwareInfo report
// rendered as a Name/Value tree. Nothing is painted here - the panel is an
// UltraCanvasColumnsTreeView that fills itself from a snapshot.
// Version: 0.1.0
// Last Modified: 2026-08-29
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASHARDWAREINFOPANEL_H
#define ULTRACANVASHARDWAREINFOPANEL_H

#include "UltraCanvasColumnsTreeView.h"
#include "UltraCanvasHardwareInfo.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {

// ===== HARDWARE INFO PANEL =====
// Drop-in system-information view for a settings screen, an about box or an
// operating-system control panel: construct it, and it captures and shows the
// machine. Categories are section rows the user can fold away; every value is a
// row in the Value column, with the long form on the tooltip.
//
// Refresh() re-probes and rebuilds. RefreshSensors() is the one to call on a
// timer: it re-reads only temperatures, clocks, utilisation and free memory,
// and writes them into the rows that are already there - so the tree keeps its
// expansion state, its selection and its scroll position while the numbers move.
class UltraCanvasHardwareInfoPanel : public UltraCanvasColumnsTreeView {
public:
    UltraCanvasHardwareInfoPanel(const std::string& identifier,
                                 float x, float y, float width, float height);
    UltraCanvasHardwareInfoPanel(const std::string& identifier, float width, float height);
    explicit UltraCanvasHardwareInfoPanel(const std::string& identifier = "HardwareInfoPanel");

    // ----- Content -----
    // Which categories to capture. Changing it re-probes immediately.
    void SetQuery(HardwareQuery what);
    HardwareQuery GetQuery() const { return query; }

    // Re-probe the machine and rebuild the tree, keeping the sections that were
    // open open.
    void Refresh(bool forceRefresh = true);

    // Re-read the live values only (temperatures, clocks, load, free memory,
    // link state) and update the affected rows in place.
    void RefreshSensors();

    // Show a snapshot the caller already has - one captured on another thread,
    // or read back from a saved support bundle - instead of probing.
    void SetSnapshot(const HardwareSnapshot& snapshot);
    const HardwareSnapshot& GetSnapshot() const { return snapshot; }

    // ----- Presentation -----
    // Expand the top-level sections when the tree is built. On by default.
    void SetSectionsExpanded(bool expanded);
    bool GetSectionsExpanded() const { return sectionsExpanded; }

    // ----- Export -----
    std::string ToText() const { return UltraCanvasHardwareInfo::ToText(snapshot); }
    std::string ToJSON() const { return UltraCanvasHardwareInfo::ToJSON(snapshot); }

    // Fires after every successful capture, with the snapshot just shown.
    std::function<void(const HardwareSnapshot&)> onSnapshotChanged;

private:
    void InstallColumns();
    void Rebuild();
    void AddGroupNodes(const HardwarePropertyGroup& group, const std::string& parentNodeId,
                       int depth);
    // Writes the current report's values into existing rows. Returns false when
    // the report's shape changed and a full rebuild is needed.
    bool UpdateValuesInPlace();
    static std::string PropertyNodeId(const std::string& groupId, const std::string& propertyName);

    HardwareSnapshot snapshot;
    HardwareQuery query = HardwareQuery::All;
    bool sectionsExpanded = true;
};

// Factory helper, matching the CreateXxx convention used across the framework.
std::shared_ptr<UltraCanvasHardwareInfoPanel> CreateHardwareInfoPanel(
    const std::string& identifier, float x, float y, float width, float height,
    HardwareQuery what = HardwareQuery::All);

} // namespace UltraCanvas

#endif // ULTRACANVASHARDWAREINFOPANEL_H
