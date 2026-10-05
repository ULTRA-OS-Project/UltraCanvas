- **`ModelDocument::ConvertUpAxis` turned a model's root nodes but not the
  animation keys that drive them.** An animation channel replaces the value
  it targets, so a root's keyed translation or rotation would have undone
  the turn on playback: an FBX, COLLADA or MilkShape model whose root is
  animated, read with `ForceUpAxis`, would have lain back on its side. Nothing in the
  framework plays animations yet, so nothing showed it. The keys now turn
  with the node: R * (T * Q * S) = (R t) * (R * Q) * S, so translation keys
  are rotated, rotation keys are pre-multiplied, and scale keys stay as they
  are in the node's own frame. Both operations are linear in the stored
  values, so cubic-spline tangents turn exactly too.
  - A sampler that a turned channel shares with one that must not turn (a
    child node, or a root's scale) is split, so the other channel keeps its
    keys. Converting back restores every key.
  - `ModelStorageTest` checks that every keyed pose of an animated root
    turns exactly as its static pose does, along with the shared-sampler
    split, the tangents, the untouched scale keys and the round trip.
    `ModelMS3DTest` checks it end to end on an `.ms3d` whose root joint
    carries translation keys. The old code fails both.
