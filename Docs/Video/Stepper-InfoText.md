# UltraCanvas Demo — Info Text: Stepper / Wizard

YouTuber-style narration for the "Stepper / Wizard" page of the UltraCanvas
demo application (Basic UI Elements section).

---

Next element: the Stepper — also known as the wizard. Any time you walk a user
through a multi-step process — a checkout, an installer, an onboarding flow —
you need one thing above everything else: the user has to know where they are.
Which steps are done, which one is active, what's still coming. That's exactly
what this control shows: a row of markers connected by lines, one per step.

Quick heads-up so nobody gets confused: this is the *progress* stepper, like
Material UI's Stepper or Ant Design's Steps — not the little minus/plus
quantity stepper. That one lives over on the Spinner page.

Let's start with the first example: a classic five-step checkout wizard —
Account, Profile, Payment, Review, Done. One call, `CreateStepperWizard`, pass
the titles, done. The Back and Next buttons next to it just call `PrevStep`
and `NextStep` — and watch what happens as you click through: the active step
moves, and every step behind it automatically flips to a green check mark.
You don't track the completed state yourself — it's derived from the current
step index. Fewer states to manage, fewer bugs. The `onStepChanged` callback
tells you the new step, which is where you'd swap in the matching page of your
wizard — here it just updates the status label.

The second example adds two things. First, descriptions: every step can carry
a subtitle under its title — "Cart: 3 items", "Shipping: Address saved". And
second — the error state. Look at the Payment step: red marker, exclamation
mark, "Card declined". One call, `SetStepError`, and the step visually screams
that something went wrong right there. There's a disabled state, too, for
steps that don't apply and should be greyed out and unclickable.

Then we rotate the whole thing: the vertical stepper. Same component,
`SetOrientation` — or just `CreateVerticalStepper` — and now the flow runs top
to bottom with the labels beside the markers. That's the layout you want for
a settings-style sidebar flow, like setting up an ad campaign step by step.

Next to it, the minimalist version: dot markers, labels switched off. Five
little dots, one highlighted — that's the compact progress indicator you know
from mobile onboarding screens. Same stepper, `SetMarkerStyle(Dot)` and
`SetShowLabels(false)`. And the marker styles go further: numbered circles,
check icons, dots — or fully custom, with your own SVG icon per step.

The last example changes who's in control. By default the stepper is *linear*:
the user can click back to a step they've already completed, but they advance
only through your Next button — you decide when a step is valid. Switch to
*non-linear* navigation, and every step becomes clickable; the user jumps
around freely, like tabs with progress. Click any marker here and the label
below reports the jump. And if you want no interaction at all, there's a
display-only mode — pure progress indicator, zero clicks.

Keyboard's covered too, by the way: arrow keys move between enabled steps,
Home and End jump to the first and last.

So that's the Stepper: horizontal or vertical, four marker styles including
your own SVGs, titles with descriptions, automatic completed states plus
error and disabled overrides, and three navigation modes from strict wizard
to click-anywhere to read-only. Point it at your flow, wire up
`onStepChanged`, and your users always know exactly where they are.
