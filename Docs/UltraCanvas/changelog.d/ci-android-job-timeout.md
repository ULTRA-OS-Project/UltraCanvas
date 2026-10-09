- **CI: the Android backend check has a time limit of its own.** The job
  had none, so anything that hung in it ran into GitHub's six-hour default:
  on 2026-10-07 a stalled `apt-get` held a pull request's checks for five
  hours. The apt step has since been given a watchdog and a 20-minute limit
  (`scripts/ci-apt.sh`); the job as a whole now stops after 30 minutes, which
  covers its other three steps too (the job normally takes 75 seconds).
