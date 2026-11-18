- **Only the networks you use are loaded.** ChatCube used to load every
  network's module at start - Telegram, Matrix, Signal and the rest - even
  for an account on ChatCube alone, each starting its own worker. Now it
  loads ChatCube Direct and the networks you are signed in to; another
  network's module is loaded when you press its button, and one you sign
  out of is not loaded at the next start. The first start after the update
  still finds every network you were signed in to.
