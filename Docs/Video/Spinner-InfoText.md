# UltraCanvas Demo — Info Text: Spinner / SpinBox

YouTuber-style narration for the "Spinner / SpinBox" page of the UltraCanvas
demo application (Basic UI Elements section).

---

Alright, next up in our tour through the Basic UI Elements: the Spinner — or
SpinBox, NumericUpDown, spin button… every toolkit has its own name for it, but
you know exactly what I mean: that little input field with the tiny up and down
arrows next to it.

Now, remember the slider from the last section? The slider is great when
"roughly here" is good enough. The spinner is its precise sibling — when you
need *exactly* 20, not "somewhere around 20", this is your control.

And here's the first thing I really like about the UltraCanvas spinner: there
isn't just one way to change the value — there are four. You can click the
arrow buttons, obviously. You can use the keyboard — arrow keys step up and
down, PageUp and PageDown jump by a bigger page amount, and Home and End take
you straight to the minimum or maximum. You can hover the control and just roll
the mouse wheel — super convenient. Or you can simply click into the field and
type the value directly. And check this detail: when you click in, the current
value gets selected automatically, so you just start typing and it's replaced —
no backspacing required. Enter commits, Escape cancels. That's the kind of
small usability polish you usually have to build yourself.

Let's look at what's on screen. The first one is a plain integer spinner —
range zero to one hundred, and it steps in fives. One factory call,
`CreateIntSpinner`, give it min, max, start value and step — done.

Below that, a decimal spinner. This one goes from zero to five seconds in steps
of a quarter, always displayed with two decimal places. And notice the little
"s" after the number — that's a suffix string. You can put units, currency
symbols, whatever, directly into the field, as a prefix or a suffix.

Number three is where it gets interesting: the list spinner. Instead of
numbers, it cycles through arbitrary string values — here it's Small, Medium,
Large, X-Large. And it wraps around, so stepping past the last entry takes you
back to the first. Perfect for sizes, quality presets, anything that's a fixed
set of options.

The fourth one looks completely different, right? Same component, different
layout. This is the horizontal stepper: a minus button on the left, plus on the
right, value in the middle — the classic "quantity" control you know from every
shopping cart. That's just `CreateStepper`, or one property switch on a normal
spinner.

Number five is the angle spinner: zero to 359 degrees, with a degree suffix,
and it wraps — step past 359 and you're back at zero, exactly how angles should
behave. Also, look closely at the buttons: those aren't the filled triangles
from before, those are chevrons. The button glyphs are configurable — triangle,
chevron, or plus/minus.

And the last one is my favorite, because it shows how far you can push this
component. Internally it's just an integer spinner from 1 to 12 — but a custom
formatter callback turns each number into a month name. The user sees "Jul",
the code sees 7. And on top of that, it has the value dropdown enabled: click
the field, and boom — a combobox-style popup opens with all twelve months, and
you just pick one. Spinner and dropdown in a single control. The list spinner
above has that enabled too, by the way — try it.

Wiring it up is one line: assign a lambda to `onValueChanged` and you get the
new value the moment it changes — you can see the live labels on the right
updating instantly.

So that's the spinner: one component, three value modes — integer, decimal,
list — two layouts, three button styles, prefixes, suffixes, custom formatting,
wrap-around, an optional dropdown, and full keyboard, mouse and wheel support.
Precise input, without making your users work for it.
