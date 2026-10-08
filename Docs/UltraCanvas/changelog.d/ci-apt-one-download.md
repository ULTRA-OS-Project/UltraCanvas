- **The Linux CI build fetches its Ubuntu packages in one call.** The
  dependency step made two dozen separate installs. Since every apt call
  reads the package lists and resolves dependencies again, and since
  `scripts/ci-apt.sh` ran apt twice per install (download, then install from
  the cache), that came to 47 apt runs and about a minute more than before the
  stall watchdog arrived. The step now lists its 56 packages once, in two
  groups, installed before and after the MuPDF build as they always were
  (MuPDF's make detects optional libraries from what is installed when it
  runs). All 56 are downloaded in one watched call, so the step makes five apt
  runs. `scripts/ci-apt.sh` gains a `download` command for this.
