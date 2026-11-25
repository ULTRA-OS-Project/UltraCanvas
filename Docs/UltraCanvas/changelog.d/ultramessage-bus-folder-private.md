- **UltraMessage: the bus is hosted and joined only in a folder that is this
  user's alone (Linux, macOS, BSD).** Whoever holds the folder the bus socket
  lies in decides which broker an application meets there. Without
  `$XDG_RUNTIME_DIR` the bus lies at `/tmp/ultramessage-<uid>/ultramessage/`,
  a name any account on the machine can work out, in a folder everyone may
  write to - and nothing looked at whose that folder was. Another account
  that made it first and ran a broker in it was the bus: this user's
  applications connected to it, and it read what they posted (notifications,
  mail and chat previews) and could answer what they asked. Reproduced with
  two accounts: the first account's broker printed the note the second one
  posted.
  - Before a broker hosts or an endpoint joins, the bus folder must be a real
    folder (not a link) owned by this user that nobody else may write to, and
    the folder above it this user's or root's and not open to everyone -
    except with the sticky bit, as `/tmp` has, where an entry is its owner's
    alone to rename (`BusFolderIsPrivate`, `UltraMessageTransport.cpp`).
    Otherwise the connect fails with `the bus folder ... is not this user's
    alone: ...`, naming which it was, and nothing is written there.
  - The broker makes every folder it creates on the way owner-only, not only
    the last (`EnsureDirectory`): the upper of the two in `/tmp` was left as
    the umask made it. A folder of this user's with a looser mode is
    tightened, as before.
  - A program running as root is let through: it joins a user's bus on
    purpose (a tool started with `sudo`).
  - A custom `busPath` follows the same rule, so one in a folder shared with
    other accounts is now refused. `Docs/Modules/UltraMessage/README.md` has
    the rule.
  - `Tests/UltraMessage/test_ultramessage.cpp` covers a folder others may
    write to, a link in the folder's place, a private folder in one open to
    everyone with and without the sticky bit, and the modes of the folders
    the broker makes. It needs a user that is not root, which CI's is.
