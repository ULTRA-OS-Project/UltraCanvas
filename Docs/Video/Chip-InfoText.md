# UltraCanvas Demo — Info Text: Chip / Tag Input

YouTuber-style narration for the "Chip / Tag Input" page of the UltraCanvas
demo application (Basic UI Elements section).

---

Alright, next up: Chips — sometimes called tags or tokens. Those little
rounded pills you see everywhere in modern UIs: the recipients in an email
field, the filters above a search result, the keywords under a blog post.
Small element, but once you have it, you'll use it constantly. And this demo
page is actually two controls in one: the chip itself, and the tag input
field built on top of it.

First row, the chip basics. A chip is a compact pill with a label — and it
comes in two variants: filled, with a solid background, and outlined, with
just a border. Add `closable` and it grows a little × button on the right;
click it and the `onClose` callback fires — in the demo the chip simply
disappears, in your app you'd remove it from your data model. And a nice
detail you don't have to think about: chips size their own width to their
content — label, optional icon, optional close button — so you never
hand-measure a pill again. Speaking of icons: every chip can carry a leading
SVG icon next to its label.

Row two turns chips into controls: filter chips. Created with
`CreateFilterChip`, these are *selectable* — click one and it toggles on,
click again and it's off, like a switch wearing a pill costume. Each toggle
fires `onSelectedChanged` with a bool, and the readout below live-updates
with the currently selected set — Design, Engineering, Sales, whatever you
click together. That's your entire filter bar for a search page, right there.

And now the star of the page: the tag input. This is the pattern you know
from every tagging UI on the web — a text field where confirmed entries turn
into chips. Type something, press Enter — or a comma — and boom, it becomes a
removable chip. Click the × on any chip to remove it, or, my favorite touch,
press Backspace in the empty field and the *last* tag pops off — exactly how
your muscle memory expects it to work. The field starts with three tags here,
and the label on the right counts along through `onTagsChanged`. Watch what
happens when you add a bunch: the chips wrap across multiple rows and the
field grows its height to fit. No clipping, no horizontal scrolling.

The second tag field shows the guard rails. This one is limited to five tags
maximum, and duplicates are rejected — try adding the same tag twice, nothing
happens. There's also a validator hook where you plug in your own rule — say,
a length limit or a character whitelist — and here's the thoughtful part: a
rejected entry does *not* wipe out what the user typed. The text stays in the
field so they can fix it instead of retyping it.

Everything is code-friendly, too: `AddTag`, `RemoveTag`, `SetTags`,
`GetTags` — the whole tag list is a plain vector of strings going in and out,
with `onTagAdded` and `onTagRemoved` if you want per-tag events.

So that's the Chip family: filled and outlined pills with icons and close
buttons, selectable filter chips for toggle-style filtering, and a complete
tag input field with Enter-to-add, Backspace-to-remove, wrapping,
auto-growing height, limits, duplicate protection, and validation. Two
controls, one header file, and your tagging UI is done.
