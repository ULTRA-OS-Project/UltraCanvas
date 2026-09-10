# UltraCanvas Demo — Info Text: Badge

YouTuber-style narration for the "Badge" page of the UltraCanvas demo
application (Basic UI Elements section).

---

And now the smallest element with maybe the biggest attention-to-size ratio in
any UI: the Badge. That little red circle on your mail icon that says you have
twelve unread messages — the one you physically cannot ignore. That's a badge.
It's tiny, it's passive, and it does exactly one job: pull your eye to a count
or a status. Let's see what UltraCanvas makes of it.

First row: status pills. A badge with a short text label — Draft, New, Active,
Pending, Error, Beta — each in one of the six built-in color variants:
Neutral gray, Primary blue, Successful green, Warning amber, Danger red, and
Info. Notice the Warning pill uses dark text on the amber background — that's
a deliberate contrast choice, not an accident. And yes, returning viewers
already know the running gag: the green one is called `Successful`, not
`Success`, because X11 claimed that word as a macro decades ago. Consistent
naming across the whole framework.

Row two: count badges. Numbers in red circles — one, eight, forty-two… and
then look at the fourth one. The count is actually 150, but it displays
"99+". That's the max-count cap, and it's configurable — nobody needs to see
four-digit unread counts, and the badge stays compact. Now, the interesting
part is what you *don't* see: there are actually six badges in this row. Two
of them have a count of zero — and by default, a zero count renders nothing
at all. The badge hides itself; no empty red circle sitting there. If you
*want* the zero — say, for a stats display — flip on `SetShowZero` and you
get the gray "0" you see at the end.

Row three strips it down to the absolute minimum: status dots. No text, no
number — just a small colored circle. Green for online, amber for away, red
for busy, gray for offline. That's your presence indicator in a chat member
list, done with `CreateDotBadge` and a variant.

And row four is the classic use case: overlay badges. Here the badges aren't
sitting on their own — they're anchored to the corner of another element.
`AnchorTo`, pick a corner, add a pixel offset, done. The badge takes itself
out of the normal layout flow, floats above its anchor with a white separator
ring so it stands out against any background, and — this is the good part —
it re-positions itself every frame. If the icon moves or resizes, the badge
follows. You never manually sync coordinates. The inbox tile has a count of
twelve, the alerts tile is at 150 showing "99+", and the chat tile just has a
red dot — unread-indicator style.

The last example makes it live. A bell icon with a count badge, and two
buttons: Add and Clear. Every click on Add calls `SetCount` and the badge
updates instantly — push it past 99 and it flips to "99+". Hit Clear, the
count goes to zero — and the badge vanishes completely, exactly like your
phone when you finally reach inbox zero. And the moment the count comes back,
so does the badge, same spot, no re-wiring.

One quick tip before we move on: a badge is deliberately passive — it's an
indicator, not a control. If you want a pill the user can click away, select,
or remove, that's the Chip from the previous section.

So that's the Badge: text pills in six semantic colors, count badges with a
"99+" cap and smart zero-hiding, minimal status dots, and overlay anchoring
that glues a live counter to any corner of any element and keeps it there.
The smallest component in the framework — and probably the first one your
users look at.
