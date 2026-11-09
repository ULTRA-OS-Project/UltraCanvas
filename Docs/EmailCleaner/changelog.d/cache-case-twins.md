- **Follows UltraMail's new layout of cached messages.** A folder whose name
  is not in the usual casing ("work", "sent items") is now cached under its
  name with a `~` and a short code ("work~1"), so two folders whose names
  differ only in case keep their messages apart on Windows and macOS. The
  cache of EmailCleaner's own accounts is moved to it at the first start, and
  telling a Sent or Trash folder by its name reads past the code.
