- Docs: `UltraCanvasMatrixDiagram.md` passes the doc checker. The roll-up
  example used `row` and `col` without saying what they were, and the
  checker read `row` as a container (a common name for one in the other
  docs); the example now declares them as the item indices the score
  functions take. The validation example's `Log` is marked as the
  application's logger and declared for the checker.
