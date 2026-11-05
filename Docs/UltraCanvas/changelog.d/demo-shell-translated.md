- **DemoApp's shell is translated.** The demo now has its own catalogue
  (`media/locale/<lang>/UltraCanvasDemo.json`) in all thirty languages: the
  window title, the category tree with every page's name, description and
  variants, the filter field, the status legend, the source and
  documentation windows' titles, the placeholder pages and the information
  window. `extract_ui_strings.py` no longer skips DemoApp, and it reads an
  application's name from a helper that passes it on
  (`InitializeSystem(g_app, "UltraCanvasDemo")`). The pages themselves -
  each element's examples - follow page by page.
