- **Switching Demo pages could crash on a freed focused element.** Leaving
  the Album page for the Colour Picker sometimes crashed with an access
  violation in `IsDescendantOf`: the window still held a focus pointer to an
  element that had been destroyed, because destroying an element only cleared
  the focus of the window it named at that moment. Every window alive in the
  process, whether registered with the application or not (popups, tool
  windows), now forgets a destroyed element as its focus
  (`UltraCanvasWindowBase::ForgetFocusedElement`), and an element gives up its
  focus whenever it leaves its window, including a move to another window.
  An element destroyed while focused in a window it does not belong to is
  reported on `debugOutput`, so the next bookkeeping slip of this kind shows
  up in a debug log instead of as a crash.
