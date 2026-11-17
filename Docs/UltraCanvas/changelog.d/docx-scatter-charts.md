- **A DOCX scatter chart has a real horizontal value axis.** Its points were
  set evenly over categories, like a line chart's. Each is now placed by its
  own x (`c:xVal`) and y on two value axes with round steps - from nought
  unless the values lie far from it, below nought where they are negative,
  or as the file states (`c:scaling` `c:min` / `c:max`) - marked with a dot
  and joined by a line where the series has one.
- **A chart whose series has no name no longer crashes the DOCX reader.**
