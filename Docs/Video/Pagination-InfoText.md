# UltraCanvas Demo — Info Text: Pagination

YouTuber-style narration for the "Pagination" page of the UltraCanvas demo
application (Basic UI Elements section).

---

Alright, moving on — Pagination. If your app ever shows a table, a search
result, a product list, anything with more entries than fit on one screen,
you've got exactly two options: infinite scroll… or this. And for desktop
applications, this is usually the right answer: a clean navigation strip that
tells the user exactly where they are and lets them jump anywhere.

The UltraCanvas pagination strip gives you the full set: First and Last
buttons — the double chevrons on the ends — Prev and Next, and the numbered
pages in between. And of course you can drive it from the keyboard too: with
the control focused, Left and Right step through the pages, Home and End jump
straight to the first or last one.

Let's walk through the examples. The first one has twenty pages — but count
the buttons: you don't see twenty of them. That's ellipsis windowing. It shows
the first pages, the neighborhood around the current page, the last page, and
collapses everything in between into a "…". Click around and watch the window
slide with you. It's the same boundary-and-sibling algorithm you know from
Material UI, so even thousands of pages stay a compact, fixed-width strip.
Nice touch: the current page, the ellipsis, and disabled end buttons simply
don't react — no dead clicks doing weird things.

Example two solves the annoying part for you. Usually you don't *have* a page
count — you have 95 items and you want 10 per page. So don't do the math:
`CreatePaginationForItems` takes total items and page size, derives the ten
pages itself, and switches on the info readout — "1 to 10 of 95". Even better,
when the page changes you just ask the control for `GetPageStartItem` and
`GetPageEndItem` and you get the exact item range to fetch — that's the live
"Showing items 1–10" label on the right. Oh, and you've probably noticed this
one looks completely different: round orange cells instead of blue squares.
That's the styling system — colors, borders, corner radius, everything is in a
style struct; crank the corner radius up to half the cell size and your
buttons are circles.

Then we shrink things down. Compact mode drops the numbers entirely: Prev,
"Page 3 of 12", Next. Same component, one `SetMode` call — perfect for a
toolbar or a footer where a full strip won't fit.

And Simple mode goes even further: just Prev and Next buttons with an optional
info text between them. That's your minimal reader-style navigation, two
buttons, done.

The last example shows the windowing knobs. This strip has thirty pages,
`siblingCount` set to two — so you get two neighbors on each side of the
current page instead of one — and the First/Last buttons switched off. Every
part of the strip is toggleable: First/Last, Prev/Next, the page info, the
boundary and sibling counts. You compose exactly the navigator you want.

Hooking it up is the usual UltraCanvas one-liner: assign `onPageChanged`, get
the new page number, load your data. And if the built-in info text isn't the
wording you want, there's a formatter callback where you build your own string
from page, page count, and item counts.

So that's Pagination: one component, three modes — numbered, compact,
simple — smart ellipsis windowing that keeps huge page counts tidy, item-count
driven paging with the exact item range served on a plate, full keyboard
support, and styling all the way down to circular buttons. Your tables and
search results are covered.
