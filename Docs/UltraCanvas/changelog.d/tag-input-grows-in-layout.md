- **A tag field grows in a layout, too.** `UltraCanvasTagInput` wraps its
  chips onto more rows and was meant to grow to fit them (`autoHeight`, on by
  default), but it only changed its bounds while painting. In a flex
  container the next layout pass set them back from `size.height` - 36 px
  from `CreateTagInput` - so the rows past the first were cut off and their
  entries could not be seen. The growth now sets `size.height` and asks for a
  new layout, and the field shrinks the same way when chips are removed. A
  one-row field takes the height its row needs (40 px at the default style,
  where it kept the 36 px it was made with). `Tests/TagInputGrowTest.cpp`
  puts a field between two labels in a flex column and checks that it grows,
  moves the label below it down, and shrinks back. Seen in UltraMail's *Settings > Warnings > Trusted & blocked*
  (a third blocked address was invisible) and *Privacy > Images*, and
  UltraFiler's own ignore patterns (UltraMail 0.10.46).
