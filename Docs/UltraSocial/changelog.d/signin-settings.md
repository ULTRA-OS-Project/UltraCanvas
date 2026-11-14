- **Sign-in setup comes set as standard, and is changed in Settings.** The
  add-account form asked for the same setup data with every account: the
  Mastodon instance and Bluesky server, and the app registration Reddit, X
  and LinkedIn sign in through (client id, and LinkedIn's client secret).
  These are now Settings, shared by every account: standard values are built
  in (`mastodon.social`, `bsky.social`; a build can ship its own app
  registrations with `-DULTRASOCIAL_REDDIT_CLIENT_ID=…`,
  `ULTRASOCIAL_X_CLIENT_ID`, `ULTRASOCIAL_LINKEDIN_CLIENT_ID` and
  `ULTRASOCIAL_LINKEDIN_CLIENT_SECRET`), and a **gear in the top-right
  corner of the start page** - and in the compose toolbar once an account
  exists - opens a Settings dialog to change them, with *Reset to standard*.
  The add-account form drops the client-id fields for a network whose
  registration is set, leaves the server empty to use the default it shows,
  and asks for a client id only while Settings have none - once: the id from
  a successful sign-in is kept in Settings. Settings are kept in the
  credential vault, as LinkedIn's client secret is a secret
  (`engine/UltraSocialSettings.h`, `ui/UltraSocialSettingsDialog.h`).
