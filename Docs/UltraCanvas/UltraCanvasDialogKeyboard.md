# UltraCanvas Dialog Keyboard Handling

## Overview

Every dialog built on `UltraCanvasModalDialog` — message boxes, alerts, input
dialogs, the file dialog, and application dialogs derived from it — is fully
operable from the keyboard. **Return** activates the default button, **Escape**
cancels, and every button carries a **mnemonic letter** that is drawn underlined
in its label and activates the button when typed. Dialogs also discard keystrokes
that were still in flight when they opened, so a key held down from the action
that raised the dialog can neither dismiss it nor leak back to the parent window
once it closes.

**File Location**: `include/UltraCanvasModalDialog.h`
**Version**: 1.0.0
**Author**: UltraCanvas Framework

## What the keyboard does

| Key | Effect |
|-----|--------|
| `Return` / `Enter` | Activates the default button (`DialogConfig::defaultButton`). |
| `Escape` | Activates the cancel button; closes with `DialogResult::Cancel` when the dialog has none. |
| *letter* | Activates the button whose underlined letter it is — but only while the dialog has no editable text field. |
| `Alt`+*letter* | Activates that button always, including while a text field has the focus. |
| `Tab` / `Shift+Tab` | Moves focus between buttons and fields (inherited window behaviour). |
| `Space` | Activates the focused button (inherited button behaviour). |

When the dialog opens, focus is placed where the user is most likely to act
first: the field of an input dialog, otherwise the default button. The file
dialog leaves focus unset so the arrow keys drive its file list.

### Which button is "default"

`DialogConfig::defaultButton` is `DialogButton::OK`. When the dialog carries no
OK button, Return falls back to the first affirmative button it does carry —
`OK`, `Yes`, `Retry`, `Apply`, `Close` — and only then to the first button in the
row. So Return on a `YesNoCancel` question means **Yes**, not the Cancel button
that happens to be laid out first.

`Escape` resolves the mirror image: `DialogConfig::cancelButton` first, then the
first of `Cancel`, `No`, `Close`, `Abort`, `Ignore` present on the dialog. Every
path — click, Return, Escape, mnemonic — runs the button's own `onClick`
handler, so a dialog cannot behave differently depending on how a button was
reached.

## Mnemonics

Each button is assigned a distinct letter, preferring the initial of its label,
then the initial of a later word, then any remaining letter. The standard button
sets never collide:

| Buttons | Mnemonics |
|---------|-----------|
| `OK` | <u>O</u>K |
| `OKCancel` | <u>O</u>K, <u>C</u>ancel |
| `YesNo` | <u>Y</u>es, <u>N</u>o |
| `YesNoCancel` | <u>Y</u>es, <u>N</u>o, <u>C</u>ancel |
| `RetryCancel` | <u>R</u>etry, <u>C</u>ancel |
| `AbortRetryIgnore` | <u>A</u>bort, <u>R</u>etry, <u>I</u>gnore |

Custom buttons added with `AddCustomButton()` join the same allocation, so a
"Close" button added next to "Cancel" gets a letter that is still free.

Bare letters are only mnemonics while nothing in the dialog is waiting for text.
A dialog holding an editable `UltraCanvasTextInput` or `UltraCanvasTextArea`
(an input dialog, or the file dialog's file-name field) types the letter instead
and reserves `Alt`+letter for the buttons.

### Overriding a letter

```cpp
DialogConfig config;
config.title = "Deploy";
config.message = "Push the build to production?";
config.dialogType = DialogType::Question;
config.buttons = DialogButtons::YesNoCancel;

auto dialog = UltraCanvasDialogManager::CreateDialog(config);
dialog->SetButtonMnemonic(DialogButton::No, 'O');   // "N<u>o</u>"
char yesKey = dialog->GetButtonMnemonic(DialogButton::Yes);  // 'Y'
dialog->ShowModal(parentWindow);
```

`SetButtonMnemonic()` returns `false` and changes nothing when the letter does
not occur in the button's label, or when the dialog has no such button.

`ActivateButton(DialogButton)` triggers a button from code exactly as the
keyboard would — useful for wiring an application-level shortcut to a dialog:

```cpp
dialog->ActivateButton(DialogButton::Yes);
```

## Discarding stale input

A dialog raised from a keyboard action normally appears while that key is still
down. Without care the release of that key reaches the newly focused default
button and the dialog closes before it is ever read.

`UltraCanvasModalDialog` snapshots the keys the application holds at the moment
it is shown and drops their remaining events until each is released. A *fresh*
press of the same key is real input and acts normally. When the dialog closes it
calls `UltraCanvasApplicationBase::ClearKeyboardState()`, so the window that
regains the focus does not see the dismissing key as still held.

Both halves are governed by one flag:

```cpp
DialogConfig config;
config.clearPendingInput = false;   // deliver every keystroke verbatim
```

## Configuration

All four keyboard behaviours live on `DialogConfig` (and therefore on
`InputDialogConfig` and `FileDialogConfig`), defaulting to enabled:

| Field | Default | Effect |
|-------|---------|--------|
| `closeOnEscape` | `true` | Escape cancels the dialog. |
| `activateDefaultOnEnter` | `true` | Return activates the default button. |
| `useButtonMnemonics` | `true` | Buttons get an underlined letter that activates them. |
| `clearPendingInput` | `true` | Drop keystrokes left over from before the dialog opened; forget the keyboard state on close. |

```cpp
DialogConfig config;
config.title = "Confirm";
config.message = "Delete the selected files?";
config.dialogType = DialogType::Warning;
config.buttons = DialogButtons::YesNo;
config.defaultButton = DialogButton::No;   // Return means No here
config.useButtonMnemonics = false;         // no underlines, no letter shortcuts

UltraCanvasDialogManager::ShowDialog(
        UltraCanvasDialogManager::CreateDialog(config),
        [](DialogResult result) {
            if (result == DialogResult::Yes) DeleteSelection();
        },
        parentWindow);
```

## Mnemonics on plain buttons

The underline is a feature of `UltraCanvasButton`, so any owner can use it —
menus, toolbars, custom panels. The button only draws the accent; matching the
key and invoking the action stays with the owner.

```cpp
auto button = CreateButton("Apply", 1, 0, 0, 90, 28, "Apply");
button->SetMnemonicChar('A');            // underlines the first 'A'
char letter = button->GetMnemonicChar(); // 'A'
button->SetMnemonicIndex(3);             // or pick the character by index
```

`SetText()` clears the mnemonic, because the stored index belongs to the old
string — set the text first, then the mnemonic. `SetMnemonicChar()` returns
`false` when the letter is not in the label.

Only the underlined label is drawn as markup; a button without a mnemonic keeps
rendering its text literally, so labels containing `&`, `<` or `>` are unchanged.

## See also

- [`UltraCanvasAlert.md`](UltraCanvasAlert.md) — severity-first façade over the dialog system
- [`UltraCanvasButtonExamples.md`](UltraCanvasButtonExamples.md) — the button component
- [`UltraCanvasTextInputExamples.md`](UltraCanvasTextInputExamples.md) — the field used by input dialogs
