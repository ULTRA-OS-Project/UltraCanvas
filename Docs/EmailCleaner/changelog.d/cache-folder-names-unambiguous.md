- **Follows UltraMail's layout of cached messages for folder names with
  `_`, `\`, `:` or a character Windows does not allow in a file name.** Such
  a folder is now cached under a name with a `~` code in that place ("a_b" as
  `a~ub`), so it no longer shares a directory with "a/b". The cache of
  EmailCleaner's own accounts is moved at the first start.
