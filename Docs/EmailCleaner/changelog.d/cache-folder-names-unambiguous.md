- **Follows UltraMail's layout of cached messages for folder names with
  `_`, `\`, `:` or a character Windows does not allow in a file name.** Such
  a folder is now cached under a name with a `~` code in that place ("a_b" as
  `a~ub`), so it no longer shares a directory with "a/b". The cache of
  EmailCleaner's own accounts is moved at the first start.
- **Follows UltraMail's cache directories for folder names with accented or
  non-Latin letters** ("über" next to "Über"); the cache of EmailCleaner's own
  accounts is moved at the first start.
