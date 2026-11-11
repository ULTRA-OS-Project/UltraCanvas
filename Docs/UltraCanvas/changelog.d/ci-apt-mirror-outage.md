- **CI: an Ubuntu mirror outage no longer stops the Linux jobs at
  `apt-get update`.** On 2026-11-11 `azure.archive.ubuntu.com` could not be
  resolved for a quarter of an hour, and the Linux, Wayland and Android
  jobs all failed before building anything, although the packages they
  needed were in the package cache. `scripts/ci-apt.sh` 1.2.0 keeps apt's
  package lists with the cached packages (`cache-out`, `cache-in`). An
  `update` that cannot reach the mirror in its attempts goes on with those
  lists, or without them with the runner's own, and with a warning instead
  of an error. The packages in the cache then install without the mirror;
  one that is not cached still fails at its install, naming it. The Wayland
  and Android workflows now restore and save the package cache as
  `build.yml` does.
