- **The UltraDBAdmin end-to-end job keeps its packages between runs.** On
  2026-11-04 the Azure Ubuntu mirror served the job's package install at a
  few kilobytes per second and its 20 minutes ran out on package 130 of the
  list, before anything was built or tested; the re-run passed in 17
  minutes. `ci-apt.sh` retries a download that stops dead and waits out one
  that trickles on purpose, so the job now does what the Build workflow's
  Linux rows do: the .deb files of the last run on `main` are restored into
  apt's cache first (its own cache, as its runner image is Ubuntu 24.04, not
  the rows' 22.04) and saved again on `main`, and the step has the rows' 45
  minutes within the job's 60.
- `AGENTS.md` says why the demo app's pages keep bare literals: `DemoApp`
  is skipped by the string extraction on purpose, so a review that finds no
  `Tr` there has found nothing.
