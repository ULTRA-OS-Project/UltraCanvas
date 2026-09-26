- **`UltraCanvasDatePicker::SetPlaceholder` now takes effect.** The picker's
  text field is built in the constructor and copied the placeholder once, at
  that moment; setting another one afterwards changed a member nothing read
  again. So every date picker showed the English "Select a date" whatever the
  application asked for - UltraFIBU's "TT.MM.JJJJ" included. The setter now
  hands the text on to the field. Seen and checked under Xvfb in UltraFIBU's
  tax key editor.
