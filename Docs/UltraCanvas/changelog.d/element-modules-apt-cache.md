- **CI: the element-modules rows keep their apt packages between runs**, as
  `build.yml`'s Linux rows do: the packages and lists the last run on `main`
  installed are restored before anything is installed (`ci-apt.sh cache-in`),
  so a slow mirror serves only what changed. On 2026-11-18 both rows ran out
  of their 45 minutes in the dependency step, downloading the same packages as
  every run before. Both rows share one cache (`apt-debs-element-modules-ubuntu-24.04-`),
  saved by the static row on `main` only.
