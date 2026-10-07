- **A radio added to an `UltraCanvasRadioGroup` was never freed, and a radio
  clicked after its group was gone called into freed memory.** The `onChecked`
  handler `AddRadioButton` installs held a `shared_ptr` to the radio it was
  stored on, and a raw pointer to the group. It now holds the radio raw, and
  the group takes the handler back when it is destroyed or the radio is
  removed (`RemoveRadioButton`), leaving any `onChecked` the application set
  itself alone. Moving a group now hands its radios' clicks to the new object;
  before, they still went to the moved-from one. The public API is unchanged.
- **An `UltraCanvasImageElement` whose `LoadFromFile` failed drew nothing.**
  `SetError` replaced the image, and `Render` looked for the error message on
  the replacement, so the error placeholder never appeared. It is drawn again,
  with the reason, into the context `Render` was given (it used the window's,
  which an offscreen or print render does not have).
- `UltraCanvasImageElement::SetTintColor` tints the picture: its colours are
  multiplied by the tint, inside the picture only (white, the default, leaves
  it unchanged; the colour's alpha sets the strength). It used to be stored
  and never drawn.
- `UltraCanvasImageElement::LoadFromImage` reports like `LoadFromFile`: it
  fires `onImageLoaded` for a valid image, and for one whose decode failed it
  fires `onImageLoadFailed`, sets `GetLastError()` and shows the error
  placeholder. A null or empty image still clears the element. It returns
  true only for a valid image (it returned true for any non-null one).
- `CreateImageFromMemory`'s `format` argument is documented as unused: the
  loader detects the format from the bytes and takes no hint. The argument
  stays, so existing calls compile.
- **`SliderHandleShape` is an `enum class`.** As a plain enum it put
  `Circle`, `Square`, `Triangle` and `Diamond` into namespace `UltraCanvas`,
  where they clashed with any other name of that spelling in the namespace or
  brought in beside it by `using namespace UltraCanvas`. Code that
  writes `SliderHandleShape::Square` is unaffected; a bare `Square` or an
  implicit conversion to `int` must now be spelled out.
- `ImageAndRadioBehaviourTest` covers the above headless, with a check that
  fails without each fix. The documentation pages for the image element and
  the slider follow the changes.
