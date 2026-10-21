#### 2026-10-21 *0.1.0*
- **ChatCube, the ULTRA OS native messenger, rebuilt on UltraIM.** One
  window: a sign-in page that makes your ChatCube ID - a key pair on this
  computer, no account, no password - and a chat view with your
  conversations on the left and the open one on the right. Add a contact by
  the ID they give you, or start a group. Messages are end-to-end encrypted
  and travel directly between the two computers when both are online (a
  WebRTC data channel), through the relay when the other side is offline,
  and through the relay for groups. Bubbles show their delivery ticks:
  … sending, ✓ queued on the relay, ✓✓ on the other computer, green ✓✓ read.
  Typing and presence show under the name.
- **Voice calls.** *Call* rings the open contact; a bar across the top
  shows the call - *Accept* / *Decline* when it rings, *Mute* and
  *Hang up* during it, with its duration. The voice goes directly between
  the two computers (Opus over WebRTC, encrypted), and the status line says
  why a call ended: declined, busy, no answer, offline, missed. Video calls
  are not in this release.
