- **`UltraRtcApiStatus` failed now and then with "the removed dispatcher was
  still called".** `UltraRtc_SetEventDispatcher` replaces the dispatcher for
  events raised from then on; an event an engine thread had already taken the
  old one for is still handed to it. The probe counted the old dispatcher's
  calls the instant it was removed, while its first peer was still gathering
  candidates, and failed a CI run on ubuntu-22.04 of a branch that does not
  touch UltraRTC. It now lets those calls land before counting, and the
  header says what the function promises.
