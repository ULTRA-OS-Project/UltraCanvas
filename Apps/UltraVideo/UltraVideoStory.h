// Apps/UltraVideo/UltraVideoStory.h
// The story wizard (Docs/Research/UltraVideoDesignProposal.md §5.9, §6.11):
// three steps in a modal - pictures and clips, music, style - and *Make
// video*, which hands a StoryChoice to the window. The window plans it with
// the engine on the worker (PlanStory) and opens the result on the timeline
// as an ordinary project (BuildStoryDocument), so every automatic choice can
// be changed afterwards.
// Version: 0.2.0 - no palette parameter: the wizard's elements follow the theme
// Version: 0.1.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework
#pragma once

#include "UltraVideoMapping.h"
#include "UltraVideoTheme.h"

#include "UltraCanvasWindow.h"

#include <functional>
#include <string>
#include <vector>

namespace UltraVideo {

// Opens the wizard over `parent`, with `files` already chosen (what the
// user dropped or had in the bin); `onMake` runs on *Make video*.
void ShowStoryWizard(UltraCanvas::UltraCanvasWindow* parent, std::vector<std::string> files,
                     std::function<void(const StoryChoice&)> onMake);

} // namespace UltraVideo
