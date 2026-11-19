- **A settings window left open follows a theme change.** The settings page
  kit (`UltraCanvasSettingsPageKit`) read the theme's colours and sizes only
  when it built a window, so a window open while the desktop - or an
  application's own light / dark setting - changed kept its old look: light
  backgrounds and buttons around dark text, and a note's grey turned to full
  ink. Every element the kit builds now remembers which roles it took and
  takes them again after the change: the window, the sidebar, the tree, the
  page area, the notes block, the buttons, the choices and checkboxes with
  their boxes, and the colour boxes' frames. A label follows as the role its
  colour and size were; one of the page's own stays as given. A control an
  application styles in the kit's look itself registers with the new
  `FollowKitLook(element, restyle)`. UltraFiler, UltraSnip and UOS-Settings
  get it for the controls they build from the kit.
