- **CI: the Windows-on-ARM build job may run for 180 minutes, not 120.** A pull
  request starts without a saved ccache, and one that changes a header most
  targets include rebuilds nearly everything: on Windows ARM that is about 65
  minutes of compiling before the tests and the packaging, which ran past the
  old limit with its build and tests already green.
