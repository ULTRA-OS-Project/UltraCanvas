- **iOS: the scene lifecycle.** DemoApp's first run on the simulator drew its
  window: the screenshot check found 288 colours. Its log also carried UIKit's
  warning "`UIScene` lifecycle will soon be required. Failure to adopt will
  result in an assert in the future."
  - A scene delegate now creates the window and the root view, and takes the
    foreground and background events.
  - The application delegate only sets up the sandbox's folders and names
    the scene delegate, in code.
  - `Info.plist.in` opts into scenes with a single scene. The application's
    main body still starts once, after the first scene connects.
  - Not yet run: the next simulator run of `ios-build.yml` is its first.
