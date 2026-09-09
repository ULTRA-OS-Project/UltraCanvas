# UltraCanvas Demo — Info Text: Rating

YouTuber-style narration for the "Rating" page of the UltraCanvas demo
application (Basic UI Elements section).

---

Next one up: the Rating element. You know this one — it's the row of stars
under every product, every movie, every app review on the internet. Simple
concept, but there are more details to get right than you'd think, and
UltraCanvas gets them all.

The basics first: click a star to set the score, and just hovering gives you a
live preview of what you're about to pick before you commit. And here's a
detail everybody forgets to implement: click the star that's already your
current rating, and it clears back to zero. No separate reset button needed —
that behavior is built in. Keyboard works too, of course: Left and Right nudge
the rating a step at a time, Home clears it, End slams it to the maximum.

The first example is the classic: five stars, whole steps. Click, done, and
the readout on the right updates through the `onRatingChanged` callback —
the usual one-line lambda.

The second row adds half steps. Same control, created with `CreateHalfRating` —
and now it matters *where* on the star you click: left half of the symbol
gives you three-and-a-half, right half gives you the full four. Hover across a
star and you can see the preview flip between the half and the whole value.
That's exactly how you want to enter an average-style score.

Row three shows that stars are just the default. The built-in symbols also
include circles and squares — here's a teal circle rating and a purple square
one. `SetSymbol`, `SetColors` for the on and off states, pick a symbol size,
and you've got a completely different look. These built-ins are drawn as
vectors by the framework itself, so they're crisp at any size.

The fourth example flips the direction: read-only mode. Sometimes you don't
want input, you just want to *display* a score — the average from your
database, three-and-a-half out of five. `SetReadOnly(true)` and the control
ignores clicks and hover; it's purely a display now. Same component, same
rendering, zero extra code.

And then my favorite — the custom vector rating. These hearts are not built
in: they're three SVG files, one for the "on" state, one for "off", one for
"half", loaded with `SetCustomSymbols`. Since they're SVGs, they render as
crisp vectors at any symbol size. And a really smart touch: the half file is
*optional*. If you don't provide one, the framework synthesizes the half state
by itself — it draws the off image and clips the on image over its left half.
So half ratings just work with any two icons you throw at it. You can even
treat your files as monochrome masks and tint them with the state colors, if
you want one icon shape in your theme's colors.

So that's Rating: whole and half steps, hover preview, click-to-set and
click-again-to-clear, three built-in vector symbols with custom colors, a
read-only display mode, full keyboard control — and the headline feature,
your own SVG symbols per state, with the half state generated automatically.
From a five-star review widget to custom hearts, it's the same one component.
