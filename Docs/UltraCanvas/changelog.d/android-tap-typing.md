- **CI taps and types in the Android demo.** The emulator smoke test now
  taps the demo's tree filter field and types a word into it, after the
  first-frame checks and before the rotation. The backend logs the first tap
  with the element it landed on (`first tap: X,Y px -> x,y, element '...'`),
  and the demo logs where the field is and every change to its text, so a
  touch that lands in the wrong place, or a key that never reaches the
  focused field, fails the job. The rotation check now looks only at frames
  logged after the turn, as the on-screen keyboard can resize the window
  first.
- **Android: launch arguments.** An app's `ultracanvas_app_main` now gets
  the launching intent's `ultracanvas.args` string extra, split on
  whitespace, as its command line
  (`am start -e ultracanvas.args "--no-about" ...`); the smoke test starts
  the demo with `--no-about`, so its About window does not cover the field.
