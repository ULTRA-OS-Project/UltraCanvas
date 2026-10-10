- **The doc-example check reads two more kinds of correct C++.** Both
  failed only because of how `scripts/check_doc_examples.py` supplies the
  names a snippet takes from its application (a `textArea`, a `window`, a
  variable in a `<!-- doc-check: ... -->` comment). The docs had to work
  around them; now they need not.
  - **A `[this]` lambda may use such a name.** The checker declared it as a
    local of the snippet's statements, so `[this] { textArea->CutSelection(); }`
    failed with "cannot be implicitly captured". When clang says so, the name
    is now declared as a member, as it would be in the application.
    `[name]` still captures a local, and a `[]` lambda that uses the name
    is still reported.
  - **A name before `<` is declared.** C++20 reads an undeclared name before
    `<` as a template's, so for `globalIdx < static_cast<int>(n)` clang
    reported "expected '>'" and never named `globalIdx`, and the doc-check
    variable was never supplied. The checker now looks for the names
    compared with `<` on the line of such an error and the three before it,
    and supplies those the doc declares.
