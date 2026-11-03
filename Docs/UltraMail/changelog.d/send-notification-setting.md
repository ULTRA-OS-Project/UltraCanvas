- **Settings > Handling > Email send notification: a sent message is
  reported in a popup window or in the status bar.** Until now every message
  sent successfully raised a "Message sent to ..." window that had to be
  closed with OK. The new *Handling* section of the settings window chooses
  between *Show in popup window* (the default, as before) and *Display
  success in status bar*, where the same words appear at the foot of the main
  window and nothing has to be closed. A message that could not be sent is
  still reported in a window either way, since it offers Retry. Saved as
  `send_notification = popup | status-bar` in `preferences.ini`.
