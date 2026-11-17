- **A button's one-character label is centred.** A label is centred on its
  line box, which holds the font's ascender and descender room: right for a
  word, whose capitals then sit in the middle, but a lone symbol sits
  wherever the font puts it in that box - a `"+"` of 26 px came out 2.5 px
  low in a round 48 px button (UltraToDo's add button), a `"×"` and a `"‹"`
  1.5 to 2.5 px low. A label of one character (a symbol, a check mark, a
  single letter), centred, now has its drawn shape put in the middle of the
  button, across and down, on whole pixels; a word stays where it was.
  `Tests/ButtonTextCentringTest.cpp` measures it from the drawn pixels.
