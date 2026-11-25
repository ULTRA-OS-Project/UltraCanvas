// Apps/UltraOffice/UltraOfficeToolbar.h
// Toolbars built the same way for every kind of tab: the framework's
// UltraCanvasToolbar, icons from media/icons, tools that never take the
// keyboard from the document.
//
// Version: 0.1.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasButton.h"
#include "UltraCanvasToolbar.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraOffice {

constexpr int kToolbarHeight = 40;

std::shared_ptr<UltraCanvas::UltraCanvasToolbar> MakeToolbar(const std::string& id);

// A tool: `icon` is a path under media/icons ("menu/cut.svg"); `text` may be
// empty when the icon says it.
std::shared_ptr<UltraCanvas::UltraCanvasButton> AddTool(UltraCanvas::UltraCanvasToolbar& toolbar, const std::string& id,
                                                        const std::string& text, const std::string& icon,
                                                        const std::string& tip, std::function<void()> action);
// A tool with an on/off state the host keeps in step (SetPressed).
std::shared_ptr<UltraCanvas::UltraCanvasButton> AddToggleTool(UltraCanvas::UltraCanvasToolbar& toolbar, const std::string& id,
                                                              const std::string& text, const std::string& icon,
                                                              const std::string& tip, std::function<void()> action);

} // namespace UltraOffice
