- **`package-macos.sh`: the staple and the validate each get their own
  retry budget, and a longer one.** Apple's CloudKit ticket lookup still
  drops the connection on the Intel runner (stapler exit 68) for minutes
  at a time: the 0.9.307 release build got `UltraFiler.app` stapled on its
  fifth and last try and then had no try left for the validate, and
  0.9.348 lost all five tries on the demo image inside two minutes - both
  red on main after a notarization Apple had accepted. Each of the two now
  has `STAPLE_ATTEMPTS` tries of its own (default 8, was 5 shared) with a
  pause growing from 20 s to 60 s, about six minutes each, before the run
  is given up.
