- **HTML `min-width`, `min-height` and `max-height`.** Only `max-width` was read; now
  all four limits are (`ComputedStyle::minWidthPx` / `minHeightPx` / `maxHeightPx`),
  in px or em - a percentage, `none` or `auto` sets no limit.
  - Like `width` / `height`, a limit is the content's (CSS content-box) - a box with
    `min-height: 60px` and a 1px border is 62px tall - or, with
    `box-sizing: border-box`, the whole box's.
  - As in CSS, `max-height` beats `height`, and `min-width` / `min-height` beat both:
    an inline-block mail button with `min-width: 160px` and 16px side padding is
    192px wide however short its caption.
