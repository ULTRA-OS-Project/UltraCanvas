- **Docs: `UltraCanvasCheckbox.md` describes the checkbox and radio API that
  exists.** It documented the checkbox from before the radio and the switch
  became their own classes: `Switch` and `Radio` styles, `CreateSwitch`,
  `CreateRadioButton`, `SetAutoSize`, a numeric id and `long` coordinates,
  label fields on the checkbox style, and a radio group of checkboxes. None
  of these compile any more, and `check_doc_examples.py` found 31 problems on
  the page. It now covers `UltraCanvasCheckbox`, `UltraCanvasRadio` (with
  `UltraCanvasRadio::Create` and `RadioVisualStyle`) and the base they share,
  the click cycle of the three states, sizing, and `UltraCanvasRadioGroup`,
  which no other page documents: how the selection works, why the initial
  choice is set with `SelectButton`, that a grouped radio's `onChecked` is
  the group's, and the lifetime rules from this release (the group is not
  owned by its radios; what happens when it goes first, when a radio is
  removed, and when a group is moved or copied). Every C++ block on the page
  compiles against the headers.
- DemoApp: the checkbox page's tri-state section no longer leaks its boxes.
  Each item's callback held `updateParentState`, which held the item, and the
  parent's held `updateStatusLabel`, which held the parent; they name the
  boxes raw now. The parent's handler was also assigned twice, the first
  never taking effect; it is set once.
