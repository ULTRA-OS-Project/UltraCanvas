- **CI: the sixteen static source checks run as one job.** Callback cycles,
  label identifiers, the locale catalogues and numbers, UTF-8 paths, platform
  names, HTML and UI reuse, the element catalogue, window tests, notification
  activators, changelog versions, llms.txt, cache keys, plug-in ABIs and the
  UltraVideo tokens were each a workflow of their own, and a pull request
  touching `UltraCanvas/` or `Apps/` started most of them at once. Each took a
  runner slot next to the build rows, Android, WebAssembly and iOS, and on
  2026-11-24 checks that run for a minute waited up to 38 minutes to start.
  They are now steps of `.github/workflows/source-checks.yml`: one runner for
  about four minutes, every check still under its own step name, and every
  step running even after an earlier one failed, so a red run names each
  check that failed. The job runs all sixteen whenever any of their paths
  changes. The documentation that named the old workflow files names the new
  one.
