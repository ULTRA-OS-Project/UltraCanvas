- **CI keeps the Linux packages it downloaded.** On 2026-10-23 the Ubuntu
  mirror served the x64 Linux build so slowly that its dependency step ran
  out of its 45 minutes twice in a row, downloading the same packages as the
  run before. The Linux build legs now restore the `.deb` files the last run
  on `main` downloaded into apt's cache before they install anything, so apt
  fetches only what changed since (`scripts/ci-apt.sh cache-in`). `main`
  saves them after the last install, pruned to the versions installed
  (`cache-out`), keyed by the set of files, one cache per runner image and
  architecture; a pull request saves none, as with ccache.
