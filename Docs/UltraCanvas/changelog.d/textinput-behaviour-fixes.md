- **A text input's formatter replaced the placeholder you set, and left an
  empty one empty.** `UltraCanvasTextInput::SetFormatter` had the test
  inverted. The formatter's placeholder ("(555) 123-4567", "$0.00",
  "MM/DD/YYYY") now fills a field that has none and never replaces one you
  set, so `CreatePhoneInput` and a field set to Currency or Date show it by
  default.
- **Changing a text input's type stacked validation rules.** `SetInputType`
  only ever added rules, so switching Email to Text kept the email check and
  Number to Email checked both. The rules a type brings are now replaced when
  the type changes. Rules added with `AddValidationRule` stay, whether they
  were added before or after the type.
- **Leaving a text input now validates it.** `validateOnBlur` was set but never
  read, so a required field the user tabbed through without typing never
  showed its error. The rules run when the field loses the focus, before
  `onFocusLost`, but not when it loses the focus because it is being hidden or
  disabled.
- **An empty optional Email, Phone or Number field is not an error.** The
  format rules (`Email`, `Phone`, `Numeric`, `Range`, `Pattern`) rejected an
  empty value, so once leaving a field validated, every optional field of
  those types showed an error. They accept it now; `Required` is what makes a
  field mandatory, as in HTML forms. `MinLength` still counts empty as too
  short.
- **A text input's type no longer leaves its formatter behind, and the
  builder no longer takes it away.** Phone -> Text stayed phone-formatted with
  the phone placeholder; the type's formatter and the placeholder it brought
  are replaced now (a formatter or placeholder the caller set stays).
  `TextInputBuilder::Build` applied `NoFormat` and an empty placeholder even
  when it was given neither, so a built Phone, Currency or Date field lost its
  formatting.
- **Backspace and Delete report a change once, and validate.** On a selection
  they reported it twice; at the start or end of the field they reported a
  change that was not one; and erasing one character did not validate, so
  emptying a required field showed no error until the next edit.
- **One key press is one undo step in a text input.** A typed character or
  Space saved two undo states, and typing over a selection, Backspace or
  Delete on a selection, or a paste over a selection saved two or three, so
  one Space took two Ctrl+Z. A key press the length limit refused also left an
  empty step and cleared the redo stack. Typing over a selection in a full
  field still works.
- `TextFormatter::Currency()`'s unformat function read `front()` of an empty
  string, which is undefined behaviour; it returns an empty string now. The
  field itself never calls `unformatFunction`: it keeps the typed text and
  formats a copy for display. The function is for callers that hold formatted
  text and want the raw value (`UltraCanvasTextInputExamples.md` shows how).
- `CreateTextInput`, `CreatePasswordInput` and `CreateRevealablePasswordInput`
  take float geometry like the email, phone and number factories, and
  `TextInputBuilder` keeps its position and size as floats. The builder stored
  them as `long`, so a size of 150.5 became 150.
- `Tests/TextInputBehaviourTest.cpp` checks all of these. Each check fails
  without its fix.
