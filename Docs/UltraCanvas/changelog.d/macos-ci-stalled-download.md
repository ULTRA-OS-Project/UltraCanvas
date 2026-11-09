- **macOS CI: a source download that runs out of time no longer fails the
  build.** When the step that prefetches vcpkg's source archives hit its 30
  minutes, GitHub stopped only the step's shell. vcpkg went on running and
  kept its lock, so the next step, the libraries' build, failed at once with
  "failed to take lock". That happened even when every library it needed was
  in the binary cache. On 2026-11-09 `codeberg.org` sent nothing for
  `gumbo`'s source, run after run, and both macOS rows went red on `main`
  and on every pull request. A new step after the fetch stops the leftover
  vcpkg, so the build runs from the binary cache.
