- **A folder called "a/b" and one called "a_b" no longer share - and
  overwrite - their downloaded messages.** The folder's cache directory wrote
  `/` as `_`, so both were cached in one directory, and so were names with
  `\` or `:` in that place. A literal `_`, `\` and `:` now have codes of their
  own (`a_b` is cached as `a~ub`), and so have the characters Windows does not
  allow in a file name (`* ? " < > |`): a folder called "What?" could not
  have its messages cached on Windows at all. Folders without those
  characters keep their directories. The first start moves the cached
  messages of both earlier layouts; where two folders shared a directory each
  takes its own, and a message both had under the same number is downloaded
  again when it is read.
