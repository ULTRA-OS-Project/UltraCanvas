# UltraCanvas Demo — Info Text: Busy Indicator

YouTuber-style narration for the "Busy Indicator" page of the UltraCanvas demo
application (Basic UI Elements section).

---

Every app has that moment: you clicked something, the work has started, and
there is no number to show yet. How long does a mail check take? Nobody knows.
You can't draw a progress bar without a percentage — but you also can't leave
the user staring at a frozen screen. That's the job of the Busy Indicator: a
small animation that says "I'm on it."

Five kinds, one element. The first row is the classic Ring: a blue arc turning
over a faint gray track. Small at 16 pixels for a status line, large next to
it, and on the right the same ring in orange — `arcColor` and `trackColor`,
that's all it takes to match your theme.

Row two, the Dual Ring: two concentric arcs, the outer one turning clockwise,
the inner one turning the other way. At rest they face each other; in motion
they look like a little mechanism. Same element — the only difference is
`BusyIndicatorKind::DualRing` in the factory call.

Row three is Dots: three dots in a row, swelling and brightening one after the
other, left to right — the "someone is typing" look. Want five dots? Set
`dotCount`. Unlike the rings this one wants a wide box, 36 by 10 for a status
line.

Row four, the Bar: a segment sliding along a thin track, entering at the left
edge and leaving at the right. Perfect under a header or along the bottom of a
panel. `barFraction` sets how long the segment is.

And row five, the Pulse: a circle that grows and brightens, then shrinks and
pales again, like breathing. Quiet enough to sit next to a connection status
all day long.

Below that, the indicators in their natural habitat: a status line. Each kind
next to a line of text — "Checking mail…", "Receiving messages… forty-two" —
the way UltraMail uses its ring while it syncs.

Now the buttons. Stop all: every animation halts — and the indicators vanish.
That's the default: a stopped indicator draws nothing, so you can leave it in
your layout forever and just call Start and Stop. Behind the scenes Stop also
kills the timer, so an idle indicator costs nothing at all. Click "Show when
stopped" and they come back, frozen exactly where they stopped. Start all, and
they pick up from there — no jump. One more detail: the animation is computed
from elapsed time, not counted per tick, so a busy UI thread may drop a frame
but never slows the animation down.

One tip before we move on: if you *do* know the percentage, don't use a busy
indicator — use the Gauge in LinearBar mode, or the Progress Dialog. And if
you came here looking for a number box with up and down arrows, that's the
Spinner — a different element despite the name.

So that's the Busy Indicator: Ring, Dual Ring, Dots, Bar and Pulse, all with
Start, Stop and a style struct — the smallest possible way to tell your users
the app hasn't forgotten them.
