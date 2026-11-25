// Plugins/Widgets/UltraCanvasProfilePicturePlugin.cpp
// Runtime-module entry for UltraCanvasProfilePicture: registers the same two
// descriptors ("profile-picture", "profile-picture-editor") a core with
// ProfilePicture compiled in registers from UltraCanvas_RegisterSelectedWidgets(),
// but through the host vtable, so the module never resolves registry symbols
// itself. Compiled only into the ultracanvas-widget-profile-picture MODULE
// target, never into the core.
//
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "UltraCanvasElementPlugins.h"
#include "UltraCanvasProfilePicture.h"

namespace {

bool InitProfilePicturePlugin(const UltraCanvas::UltraCanvasPluginHost* host) {
    const UltraCanvas::UCElementDescriptor picture = UltraCanvas::MakeProfilePictureDescriptor();
    host->RegisterElement(&picture);
    const UltraCanvas::UCElementDescriptor editor = UltraCanvas::MakeProfilePictureEditorDescriptor();
    host->RegisterElement(&editor);
    return true;
}

} // namespace

ULTRACANVAS_DEFINE_ELEMENT_PLUGIN(InitProfilePicturePlugin)
