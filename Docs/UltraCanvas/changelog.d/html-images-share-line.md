- **HTML images without text sit side by side.** In a block with no text of its own,
  every `<img>` got a line of its own, so a row of social icons in a mail's footer
  became a column. Now inline images (the `<img>` default) share one wrapping line,
  as in a browser:
  - whitespace between two images is a space's gap; none, no gap. The gap goes after
    the image before it, so a wrapped line does not start indented;
  - they stand on the line's bottom, and the line follows `text-align`;
  - the line wraps when it is full; text, a block, `<br>` or a `display:block` image
    ends it;
  - an inline image's vertical margins grow its line instead of collapsing with the
    blocks around it.
