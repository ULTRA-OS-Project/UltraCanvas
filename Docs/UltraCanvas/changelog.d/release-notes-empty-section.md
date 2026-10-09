- **Release notes for a version with no changelog entries point at its
  commits.** The `publish-sdk` job took the version's section of
  `CHANGELOG.md` as the release notes as it was, so a version whose section
  was only its header (a hand-cut one, say) would have ended its notes on an
  empty "Changes" heading. The notes now say that the section is empty and
  name the previous version and a link to the commits up to the release's
  own, and the step prints a warning. The notes the job wrote are shown in
  the run's log.
