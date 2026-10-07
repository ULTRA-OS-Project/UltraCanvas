- **`scripts/check_callback_cycles.py` now sees a `shared_ptr` the function
  was handed, and a callback passed to a setter.** It passed
  `UltraCanvasRadioGroup::AddRadioButton`, which leaked every radio it was
  given: `button->onChecked = [this, button]` stored a copy of the by-value
  `std::shared_ptr<UltraCanvasRadio> button` parameter on the radio it owns.
  The check looked for the capture's type only inside the function body, and
  a parameter is declared before the `{`, so it never knew `button` was a
  `shared_ptr`.
  - A parameter declared `shared_ptr<T>`, `const shared_ptr<T>&` or
    `shared_ptr<T>&&` now counts, beside `make_shared`, declared locals and
    factories. A pointer to one, a `weak_ptr` or a container of them does not.
  - A lambda handed to a setter on the object (`x->SetOnClick(...)`,
    `x->SetEventCallback(...)`, `x->SetValueFormatter(...)`,
    `x->AddListener(...)`) is checked like an assignment to `x->onClick`.
    Most of the framework's click handlers are wired this way, and none of
    them were checked before.
  - `[p = x]`, `[p = std::move(x)]` and a `[=]` whose body uses `x` are
    reported like `[x]`, since each copies the `shared_ptr`. `[x = x.get()]`,
    `[w = std::weak_ptr<T>(x)]` and by-reference captures (`[&x]`, `[&]`)
    are not: they own nothing. A `[&x]` used to be read as `[x]`.
  - Run against `AddRadioButton` as it stood before its fix, the check now
    reports it. The DemoApp's earlier hand fixes of the same shapes
    (`AttachStatus` in the mind-map examples, the `SetEventCallback` tiles in
    the CDR, DWG, EPS, SVG and XAR examples) are now enforced rather than
    merely done.
- The check found two more leaks of this kind, both fixed: the DemoApp's git
  graph page stored a file-list provider capturing `graph` on `graph`, and
  `UltraCanvasFlowChartPalette` stored each shape button's click handler,
  capturing the button, on that button. It now also scans
  `UltraCanvas/Plugins` (where the palette leak sat unseen),
  `UltraCanvas/OS`, `UltraCanvas/libspecific` and `Tests`, in CI as well.
