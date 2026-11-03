// include/UltraCanvasMenuConfigWidget.h
// The menu editor's former name. UltraCanvasMenuConfigWidget became
// UltraCanvasCommandConfigWidget when it began to edit toolbars as well as
// menus - one editor for both; this header keeps the old name compiling. Its
// onApply / onSave now carry a CommandLayoutSet, whose `menus` is the
// MenuLayoutSet they carried before.
// Version: 2.0.0 - an alias of UltraCanvasCommandConfigWidget
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommandConfigWidget.h"

namespace UltraCanvas {

    using UltraCanvasMenuConfigWidget = UltraCanvasCommandConfigWidget;

    inline std::shared_ptr<UltraCanvasCommandConfigWidget> CreateMenuConfigWidget(
            const std::string& id, float x, float y, float w, float h) {
        return CreateCommandConfigWidget(id, x, y, w, h);
    }

} // namespace UltraCanvas
