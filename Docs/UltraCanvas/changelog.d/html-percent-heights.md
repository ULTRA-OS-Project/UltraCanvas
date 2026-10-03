- **Percentage heights on blocks; content-box percentage widths without a sizing switch.**
  - HTML `height` in percent is kept (`ComputedStyle::heightPercent`; the `height`
    attribute too) and applied to blocks: `height: 50%` in a box of `height: 200px` is
    100px, nested percentages compound, and in a box of auto height it is auto, as in
    CSS. Tables, cells and images take no percentage height (browsers mostly ignore
    one there).
  - CSSLayout: a percentage `size.height` resolves against a block parent's set height
    (`Element::percentHeightBase`, which percentage limits already used) when no
    definite height comes down as a constraint - in block, flex, grid and table
    containers alike. Before, it was auto there.
  - A content-box percentage width or height stays a border-box size with the padding
    and border added as pixels (`Dimension::PctPlus`), instead of switching that box
    to content-box sizing: `width: 50%; padding: 0 10px; border: 2px` on a 400px line
    is 224px wide.
  - A later `width` / `height` replaces an earlier one of either kind
    (`width: 30%; width: 120px` is 120px); `auto` clears it.
