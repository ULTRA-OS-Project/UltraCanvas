- **UltraDesktop builds without the message centre.** Its window kept the
  notification toasts' host as a member whatever the build had, so a build
  without UltraMessage (Android's, MobilePC's) did not compile; the member is
  now there only when the toasts are.
