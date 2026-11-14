- **A pull request builds one row per operating system; `ci:distro` builds
  all six.** Without a label a pull request now builds Linux x86_64, Windows
  x86_64 and macOS on Apple silicon. Linux arm64, Windows ARM and macOS Intel
  build on `main`, where the release is made, and on a pull request labelled
  `ci:distro` (the distribution build); adding the label starts a run with
  them. `ci:intel` still adds the Intel row alone. The Windows ARM row alone
  took an hour on #1236 and #1238.
  - A left-out row reports under its own name (`build (<os>, Release)`) as
    not built, from a `build-skipped` job, so a required check never waits
    on it.
  - The clean-machine Linux SDK check tests only the Linux rows that were
    built.
