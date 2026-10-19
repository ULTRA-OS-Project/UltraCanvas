// Tests/TextInputBehaviourTest.cpp
// What a text field does with its placeholder, its rules, the focus, the undo
// stack and its geometry.
//
// A docs review of UltraCanvasTextInput found six things that did not do what
// they said:
//   1. SetFormatter overwrote a placeholder the caller had set with the
//      formatter's, and left an empty one empty - the condition was inverted.
//   2. The Currency formatter's unformat read front() of an empty string.
//   3. validateOnBlur was set but never read: leaving a field did not validate.
//   4. SetInputType only ever added rules: Email -> Text kept the email check,
//      Number -> Email checked both.
//   5. One key press saved two undo states (three over a selection), so one
//      Space took two Ctrl+Z; a key press the length limit refused saved one.
//   6. TextInputBuilder kept its geometry in longs and three factories took
//      ints, so 150.5 became 150.
//   7. What fixing those turned up: an empty optional Email/Phone/Number field
//      failed its type's rule (and so, once leaving validated, showed an error);
//      a new input type kept the old type's formatter; the builder took the
//      type's formatter away again; Backspace/Delete of one character did not
//      validate, and reported a change twice on a selection and once when
//      nothing changed.
//   8. An input method's composition (TextComposition) is drawn in the field
//      at the caret and is never part of its text: committing it types it,
//      an empty composition or losing the focus ends it, composing over a
//      selection replaces the selection, and read-only and password fields
//      leave the composing to the input method's own window.
//   9. TextInputType::Search was described as a "search field with clear
//      button" but nothing read it: a Search field had no clear button. It
//      shows one now, and leaving Search takes it away again - unless the
//      caller turned it on. The button empties the field when clicked.
// Each section below fails without its fix.
//
// Runs headless: the field is driven through its public API and key and focus
// events, without a window; the composition is drawn into an offscreen context.
// Version: 1.2.0 - TextInputType::Search shows the clear button
// Version: 1.1.0 - input method composition
// Version: 1.0.0
// Last Modified: 2026-10-15
// Author: UltraCanvas Framework

// Checked mode for this file's inline standard-library code: reading front() of
// an empty string - fix 2 - then aborts instead of quietly reading the
// terminating zero, so the check below can see it. libstdc++ only; elsewhere
// the macro is ignored. It changes no layout, so mixing with the library is safe.
#ifndef _GLIBCXX_ASSERTIONS
#define _GLIBCXX_ASSERTIONS 1
#endif

#include "UltraCanvasImage.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasTextInput.h"

#include <iostream>
#include <memory>
#include <string>

using namespace UltraCanvas;

static int checkCount = 0;
static int failCount = 0;

#define CHECK(name, condition)                                                \
    do {                                                                      \
        bool passed = (condition);                                            \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl; \
        if (!passed) failCount++;                                             \
        checkCount++;                                                         \
    } while (0)

namespace {

std::shared_ptr<UltraCanvasTextInput> MakeField() {
    return CreateTextInput("behaviourField", 0, 0, 200, 24);
}

// A key with no text of its own (Space, Backspace, ...), as a backend sends it.
UCEvent KeyEvent(UCKeys key) {
    UCEvent e;
    e.type = UCEventType::KeyDown;
    e.virtualKey = key;
    return e;
}

// A printable ASCII key, carried in event.character.
UCEvent CharEvent(char c) {
    UCEvent e;
    e.type = UCEventType::KeyDown;
    e.virtualKey = UCKeys::Unknown;
    e.character = c;
    return e;
}

// A typed character from an input method, carried as UTF-8 in event.text.
UCEvent TextEvent(const std::string& utf8) {
    UCEvent e;
    e.type = UCEventType::KeyDown;
    e.virtualKey = UCKeys::Unknown;
    e.text = utf8;
    return e;
}

UCEvent FocusLostEvent() {
    UCEvent e;
    e.type = UCEventType::FocusLost;
    return e;
}

// What an input method sends while it composes: the whole composing string
// and its caret (a byte offset; -1 is the end). "" ends the composition.
UCEvent CompositionEvent(const std::string& utf8, int cursor = -1) {
    UCEvent e;
    e.type = UCEventType::TextComposition;
    e.text = utf8;
    e.compositionCursor = cursor;
    return e;
}

ValidationState StateOf(UltraCanvasTextInput& field) {
    return field.GetLastValidationResult().state;
}

// ===== 1. SetFormatter and the placeholder =====
void TestFormatterPlaceholder() {
    auto chosen = MakeField();
    chosen->SetPlaceholder("Your phone number");
    chosen->SetFormatter(TextFormatter::Phone());
    CHECK("SetFormatter keeps a placeholder the caller chose",
          chosen->GetPlaceholder() == "Your phone number");

    auto empty = MakeField();
    empty->SetFormatter(TextFormatter::Phone());
    CHECK("SetFormatter gives an empty field the formatter's placeholder",
          empty->GetPlaceholder() == "(555) 123-4567");

    auto typed = MakeField();
    typed->SetInputType(TextInputType::Date);
    CHECK("SetInputType(Date) gives an empty field the date placeholder",
          typed->GetPlaceholder() == "MM/DD/YYYY");

    auto none = MakeField();
    none->SetPlaceholder("Anything");
    none->SetFormatter(TextFormatter::NoFormat());
    CHECK("a formatter without a placeholder leaves the field's alone",
          none->GetPlaceholder() == "Anything");
}

// ===== 2. The Currency formatter's unformat =====
void TestCurrencyUnformat() {
    const TextFormatter currency = TextFormatter::Currency();
    CHECK("Currency unformat strips the dollar sign",
          currency.unformatFunction("$12.50") == "12.50");
    CHECK("Currency unformat leaves a bare number alone",
          currency.unformatFunction("12.50") == "12.50");
    // Without the empty check this aborts under _GLIBCXX_ASSERTIONS.
    CHECK("Currency unformat of an empty string is empty",
          currency.unformatFunction("").empty());

    // The way a caller reaches it: the raw value of an empty currency field.
    auto field = MakeField();
    field->SetInputType(TextInputType::Currency);
    CHECK("an empty currency field unformats to empty",
          field->GetFormatter().unformatFunction(field->GetText()).empty());
}

// ===== 3. Validation when the focus leaves =====
void TestValidateOnBlur() {
    auto field = MakeField();
    field->AddValidationRule(ValidationRule::Required());
    CHECK("a fresh required field shows no verdict yet",
          StateOf(*field) == ValidationState::NoValidation);

    // Tabbed through without a keystroke: nothing validated it before.
    field->OnEvent(FocusLostEvent());
    CHECK("leaving an empty required field marks it invalid",
          StateOf(*field) == ValidationState::Invalid);

    // onFocusLost runs after the blur validation, so it can act on the result.
    ValidationState seenByCallback = ValidationState::NoValidation;
    auto watched = MakeField();
    watched->AddValidationRule(ValidationRule::Required());
    UltraCanvasTextInput* watchedRaw = watched.get();
    watched->onFocusLost = [&seenByCallback, watchedRaw]() {
        seenByCallback = watchedRaw->GetLastValidationResult().state;
    };
    watched->OnEvent(FocusLostEvent());
    CHECK("onFocusLost reads the result of the blur validation",
          seenByCallback == ValidationState::Invalid);

    auto plain = MakeField();
    plain->OnEvent(FocusLostEvent());
    CHECK("a field without rules stays without a verdict after the blur",
          StateOf(*plain) == ValidationState::NoValidation);

    // Hidden or disabled: the user did not leave it, so it is not judged.
    auto hidden = MakeField();
    hidden->AddValidationRule(ValidationRule::Required());
    hidden->SetVisible(false);
    hidden->OnEvent(FocusLostEvent());
    CHECK("a field losing the focus because it is hidden is not judged",
          StateOf(*hidden) == ValidationState::NoValidation);

    auto disabled = MakeField();
    disabled->AddValidationRule(ValidationRule::Required());
    disabled->SetDisabled(true);
    disabled->OnEvent(FocusLostEvent());
    CHECK("a field losing the focus because it is disabled is not judged",
          StateOf(*disabled) == ValidationState::NoValidation);
}

// ===== 4. SetInputType replaces its own rules =====
void TestInputTypeRules() {
    auto field = MakeField();
    field->SetInputType(TextInputType::Email);
    field->SetText("not an address");
    CHECK("an Email field rejects a non-address",
          StateOf(*field) == ValidationState::Invalid);
    field->SetInputType(TextInputType::Text);
    CHECK("Email -> Text drops the email rule",
          field->Validate().state == ValidationState::NoValidation);

    auto swapped = MakeField();
    swapped->SetInputType(TextInputType::Number);
    swapped->SetInputType(TextInputType::Email);
    swapped->SetText("someone@example.com");
    CHECK("Number -> Email checks the address only, not the number rule too",
          swapped->Validate().state == ValidationState::Valid);

    auto twice = MakeField();
    twice->SetInputType(TextInputType::Email);
    twice->SetInputType(TextInputType::Email);
    twice->SetInputType(TextInputType::Text);
    twice->SetText("plain words");
    CHECK("setting Email twice leaves nothing behind when the type moves on",
          twice->Validate().state == ValidationState::NoValidation);

    // The caller's rules stay, whether added before or after the type.
    auto before = MakeField();
    before->AddValidationRule(ValidationRule::MinLength(5));
    before->SetInputType(TextInputType::Email);
    before->SetInputType(TextInputType::Text);
    before->SetText("abc");
    CHECK("a rule added before the type survives the type change",
          before->Validate().ruleName == "MinLength");
    before->SetText("long enough, no address");
    CHECK("while the email rule the type added is gone",
          before->Validate().state == ValidationState::Valid);

    auto after = MakeField();
    after->SetInputType(TextInputType::Email);
    after->AddValidationRule(ValidationRule::Required());
    after->SetInputType(TextInputType::Text);
    after->SetText("");
    CHECK("a rule added after the type survives it too",
          after->Validate().ruleName == "Required");
    after->SetText("no address here");
    CHECK("and only it: the address is no longer checked",
          after->Validate().state == ValidationState::Valid);

    // A caller-added Email rule looks just like the type's; it must stay.
    auto own = MakeField();
    own->AddValidationRule(ValidationRule::Email());
    own->SetInputType(TextInputType::Email);
    own->SetInputType(TextInputType::Text);
    own->SetText("still not an address");
    CHECK("the caller's own Email rule stays when the Email type goes",
          own->Validate().state == ValidationState::Invalid);

    auto cleared = MakeField();
    cleared->SetInputType(TextInputType::Email);
    cleared->ClearValidationRules();
    cleared->SetText("no address");
    CHECK("ClearValidationRules removes the type's rules as well",
          cleared->Validate().state == ValidationState::NoValidation);
}

// ===== 5. One key press, one undo step =====
void TestOneKeyOneUndo() {
    auto field = MakeField();
    field->OnEvent(CharEvent('a'));
    field->OnEvent(KeyEvent(UCKeys::Space));
    CHECK("typing a, Space gives \"a \"", field->GetText() == "a ");
    field->Undo();
    CHECK("one undo takes the Space back", field->GetText() == "a");
    field->Undo();
    CHECK("a second undo takes the a back", field->GetText().empty());
    CHECK("and nothing is left to undo", !field->CanUndo());

    auto ime = MakeField();
    ime->OnEvent(TextEvent("\xC3\xB6"));   // "ö" from an input method
    ime->Undo();
    CHECK("one undo takes back a character typed through the input method",
          ime->GetText().empty() && !ime->CanUndo());

    // Typing over a selection replaces it in one step.
    auto over = MakeField();
    over->SetText("hello");
    over->SelectAll();
    over->OnEvent(CharEvent('x'));
    CHECK("typing over a selection replaces it", over->GetText() == "x");
    over->Undo();
    CHECK("one undo brings the selected text back", over->GetText() == "hello");

    auto spaceOver = MakeField();
    spaceOver->SetText("hello");
    spaceOver->SelectAll();
    spaceOver->OnEvent(KeyEvent(UCKeys::Space));
    spaceOver->Undo();
    CHECK("one undo reverses a Space typed over a selection",
          spaceOver->GetText() == "hello");

    // Backspace on a selection: one step, so the next undo reaches SetText's.
    auto back = MakeField();
    back->SetText("hello");
    back->SelectAll();
    back->OnEvent(KeyEvent(UCKeys::Backspace));
    CHECK("Backspace deletes the selection", back->GetText().empty());
    back->Undo();
    CHECK("one undo restores it", back->GetText() == "hello");
    back->Undo();
    CHECK("the next undo goes back past SetText", back->GetText().empty());

    auto del = MakeField();
    del->SetText("hello");
    del->SelectAll();
    del->OnEvent(KeyEvent(UCKeys::Delete));
    del->Undo();
    del->Undo();
    CHECK("Delete on a selection is one undo step too", del->GetText().empty());

    // A key press the length limit refuses changes nothing - not even the stack.
    auto full = MakeField();
    full->SetMaxLength(1);
    full->OnEvent(CharEvent('a'));
    full->OnEvent(CharEvent('b'));
    CHECK("the limit refuses the second character", full->GetText() == "a");
    full->Undo();
    CHECK("so one undo takes back the first", full->GetText().empty());

    // ...but typing over a selection in a full field still replaces it.
    auto fullOver = MakeField();
    fullOver->SetMaxLength(3);
    fullOver->SetText("abc");
    fullOver->SelectAll();
    fullOver->OnEvent(CharEvent('z'));
    CHECK("typing over a selection in a full field replaces it",
          fullOver->GetText() == "z");
    fullOver->Undo();
    CHECK("and one undo restores the full text", fullOver->GetText() == "abc");
}

// ===== 6. Float geometry =====
void TestFloatGeometry() {
    auto built = TextInputBuilder()
                         .SetIdentifier("builtField")
                         .SetPosition(10.5f, 20.25f)
                         .SetSize(150.5f, 28.75f)
                         .Build();
    CHECK("the builder keeps a fractional position",
          built->GetX() == 10.5f && built->GetY() == 20.25f);
    CHECK("the builder keeps a fractional size",
          built->GetWidth() == 150.5f && built->GetHeight() == 28.75f);

    auto text = CreateTextInput("t", 1.5f, 2.5f, 100.5f, 24.5f);
    CHECK("CreateTextInput keeps fractional geometry",
          text->GetX() == 1.5f && text->GetY() == 2.5f &&
          text->GetWidth() == 100.5f && text->GetHeight() == 24.5f);

    auto password = CreatePasswordInput("p", 1.5f, 2.5f, 100.5f, 24.5f);
    CHECK("CreatePasswordInput keeps fractional geometry",
          password->GetWidth() == 100.5f && password->GetHeight() == 24.5f);

    auto revealable = CreateRevealablePasswordInput("r", 1.5f, 2.5f, 100.5f, 24.5f);
    CHECK("CreateRevealablePasswordInput keeps fractional geometry",
          revealable->GetWidth() == 100.5f && revealable->GetHeight() == 24.5f);
}

// ===== 7. Empty fields, formatters across types, one change per erase =====
void TestFollowUps() {
    // A format rule says nothing about an empty field; Required does.
    auto email = MakeField();
    email->SetInputType(TextInputType::Email);
    CHECK("an empty Email field is not invalid",
          email->Validate().state != ValidationState::Invalid);
    email->OnEvent(FocusLostEvent());
    CHECK("leaving an empty optional Email field shows no error",
          StateOf(*email) != ValidationState::Invalid);
    auto number = MakeField();
    number->SetInputType(TextInputType::Number);
    CHECK("an empty Number field is not invalid",
          number->Validate().state != ValidationState::Invalid);
    CHECK("an empty value passes Phone, Range and Pattern",
          ValidationRule::Phone().validator("") && ValidationRule::Range(1, 9).validator("") &&
          ValidationRule::Pattern("[a-z]+").validator(""));
    auto required = MakeField();
    required->SetInputType(TextInputType::Email);
    required->AddValidationRule(ValidationRule::Required());
    CHECK("Required still rejects the empty field",
          required->Validate().ruleName == "Required");

    // A new type replaces the formatter the old type chose, placeholder and all.
    auto phone = MakeField();
    phone->SetInputType(TextInputType::Phone);
    const std::string phonePlaceholder = phone->GetPlaceholder();
    phone->SetInputType(TextInputType::Text);
    phone->SetText("5551234567");
    CHECK("Phone -> Text shows the digits as typed", phone->GetDisplayText() == "5551234567");
    CHECK("and drops the placeholder the phone formatter brought",
          phonePlaceholder.empty() || phone->GetPlaceholder() != phonePlaceholder);
    auto own = MakeField();
    own->SetPlaceholder("Your number");
    own->SetInputType(TextInputType::Phone);
    own->SetInputType(TextInputType::Text);
    CHECK("a placeholder the caller set survives both types",
          own->GetPlaceholder() == "Your number");
    auto mine = MakeField();
    mine->SetFormatter(TextFormatter::Phone());
    mine->SetInputType(TextInputType::Text);
    mine->SetText("5551234567");
    CHECK("a formatter the caller set stays when the type changes",
          mine->GetDisplayText() == "(555) 123-4567");

    // The builder keeps the formatter its type chose.
    auto built = TextInputBuilder().SetIdentifier("builtPhone").SetType(TextInputType::Phone).Build();
    built->SetText("5551234567");
    CHECK("a built Phone field formats its digits", built->GetDisplayText() == "(555) 123-4567");
    auto builtOwn = TextInputBuilder().SetType(TextInputType::Phone).SetPlaceholder("Mobile").Build();
    CHECK("a built field keeps the placeholder it was given", builtOwn->GetPlaceholder() == "Mobile");

    // One erase, one change report - and a validation, like every other edit.
    auto counted = MakeField();
    int changes = 0;
    counted->onTextChanged = [&changes](const std::string&) { ++changes; };
    counted->SetText("hello");
    changes = 0;
    counted->SelectAll();
    counted->OnEvent(KeyEvent(UCKeys::Backspace));
    CHECK("Backspace on a selection reports one change", changes == 1);
    changes = 0;
    counted->OnEvent(KeyEvent(UCKeys::Backspace));
    CHECK("Backspace in an empty field reports none", changes == 0);
    counted->SetText("ab");
    changes = 0;
    counted->SelectAll();
    counted->OnEvent(KeyEvent(UCKeys::Delete));
    CHECK("Delete on a selection reports one change", changes == 1);

    auto erased = MakeField();
    erased->AddValidationRule(ValidationRule::Required());
    erased->SetText("a");
    erased->SetCaretPosition(1);
    erased->OnEvent(KeyEvent(UCKeys::Backspace));
    CHECK("Backspace of the last character validates the now empty field",
          StateOf(*erased) == ValidationState::Invalid);
    auto erasedForward = MakeField();
    erasedForward->AddValidationRule(ValidationRule::Required());
    erasedForward->SetText("a");
    erasedForward->SetCaretPosition(0);
    erasedForward->OnEvent(KeyEvent(UCKeys::Delete));
    CHECK("Delete of the last character validates too",
          StateOf(*erasedForward) == ValidationState::Invalid);
}

// ===== 8. Input method composition =====
// Dark pixels of the field drawn into a white offscreen surface.
int InkOf(UltraCanvasTextInput& field) {
    const int w = 200, h = 24;
    auto ctx = CreateRenderContext(Size2Di(w, h), nullptr);
    if (!ctx) return -1;
    ctx->Clear(Color(255, 255, 255, 255));
    field.Render(ctx.get(), Rect2Df(0, 0, w, h));
    UCPixmap pixmap;
    pixmap.Init(w, h);
    ctx->FlushToSurface(pixmap.GetSurface(), Point2Dd(0, 0));
    pixmap.MarkDirty();
    pixmap.Flush();
    int ink = 0;
    // Inside the frame only: the border is not text.
    for (int y = 3; y < h - 3; ++y) {
        for (int x = 3; x < w - 3; ++x) {
            if (((pixmap.GetPixel(x, y) >> 8) & 0xFF) < 128) ++ink;
        }
    }
    return ink;
}

void TestComposition() {
    const std::string kana = "\xE3\x81\x8B\xE3\x81\xAA";   // two kana, being composed
    const std::string kanji = "\xE4\xBB\xAE";                // what they convert to

    auto field = MakeField();
    field->SetText("ab");
    field->SetCaretPosition(1);
    int changes = 0;
    field->onTextChanged = [&changes](const std::string&) { ++changes; };

    CHECK("an editable field draws compositions itself", field->DrawsTextComposition());
    CHECK("a composition is taken", field->OnEvent(CompositionEvent(kana)));
    CHECK("it is the composition text", field->GetCompositionText() == kana);
    CHECK("it is not in the text", field->GetText() == "ab");
    CHECK("it reports no change", changes == 0);
    CHECK("it leaves the caret where it was", field->GetCaretPosition() == 1);

    field->OnEvent(CompositionEvent(kana, 3));   // caret after the first kana
    CHECK("a composition's caret may sit inside it", field->GetCompositionText() == kana);

    // The input method commits: the converted text arrives typed.
    field->OnEvent(TextEvent(kanji));
    CHECK("the committed text is typed at the caret", field->GetText() == "a" + kanji + "b");
    CHECK("and the composition is gone", field->GetCompositionText().empty());
    CHECK("the commit is one change", changes == 1);

    field->OnEvent(CompositionEvent(kana));
    field->OnEvent(CompositionEvent(""));
    CHECK("an empty composition ends it", field->GetCompositionText().empty());
    CHECK("without typing anything", field->GetText() == "a" + kanji + "b");

    // A composition leaves no undo step: one Undo takes back SetText itself.
    auto undone = MakeField();
    undone->SetText("ab");
    undone->OnEvent(CompositionEvent(kana));
    undone->OnEvent(CompositionEvent(""));
    undone->Undo();
    CHECK("a composition leaves no undo step", undone->GetText().empty() && !undone->CanUndo());

    // Composing over a selection replaces it, as typing would.
    auto selected = MakeField();
    selected->SetText("hello world");
    selected->SetSelection(0, 5);
    selected->OnEvent(CompositionEvent(kana));
    CHECK("composing over a selection removes the selected text", selected->GetText() == " world");
    CHECK("and the selection", !selected->HasSelection() && selected->GetCaretPosition() == 0);
    CHECK("the composition stands in its place", selected->GetCompositionText() == kana);
    selected->OnEvent(TextEvent(kanji));
    CHECK("the commit lands where the selection was", selected->GetText() == kanji + " world");

    // Leaving the field drops the composition, unfinished.
    auto left = MakeField();
    left->SetText("x");
    left->OnEvent(CompositionEvent(kana));
    left->OnEvent(FocusLostEvent());
    CHECK("losing the focus ends the composition", left->GetCompositionText().empty());
    CHECK("and types nothing", left->GetText() == "x");

    // A line break in a composition cannot go into one line.
    auto broken = MakeField();
    broken->OnEvent(CompositionEvent("a\nb"));
    CHECK("a line break in a composition shows as a space", broken->GetCompositionText() == "a b");

    // Read-only and password fields leave composing to the input method.
    auto readOnly = MakeField();
    readOnly->SetReadOnly(true);
    CHECK("a read-only field does not draw compositions", !readOnly->DrawsTextComposition());
    CHECK("and does not take one", !readOnly->OnEvent(CompositionEvent(kana))
          && readOnly->GetCompositionText().empty());
    auto password = MakeField();
    password->SetInputType(TextInputType::Password);
    CHECK("a password field does not draw compositions", !password->DrawsTextComposition());
    password->SetPasswordRevealed(true);
    CHECK("not even revealed", !password->DrawsTextComposition());
    CHECK("and does not take one", !password->OnEvent(CompositionEvent(kana))
          && password->GetCompositionText().empty());
    auto turned = MakeField();
    turned->OnEvent(CompositionEvent(kana));
    turned->SetReadOnly(true);
    CHECK("turning read-only drops a composition", turned->GetCompositionText().empty());

    // Drawn: an empty field with a composition shows it.
    UCImage::InitializeImageSubsysterm("TextInputBehaviourTest");
    auto drawn = MakeField();
    CSSLayout::LayoutContext lctx;
    drawn->Arrange(Rect2Df(0, 0, 200, 24), lctx);
    const int empty = InkOf(*drawn);
    drawn->OnEvent(CompositionEvent("WWW"));
    const int composing = InkOf(*drawn);
    CHECK("an empty field draws nothing", empty == 0);
    CHECK("a composition is drawn in the field", composing > 20);
    drawn->OnEvent(CompositionEvent(""));
    CHECK("and is gone when it ends", InkOf(*drawn) == 0);
}

} // namespace

// ===== 9. A Search field has the clear button =====
void TestSearchClearButton() {
    auto search = MakeField();
    CHECK("a plain field has no clear button", !search->IsShowClearButton());
    search->SetInputType(TextInputType::Search);
    CHECK("SetInputType(Search) shows the clear button", search->IsShowClearButton());
    search->SetInputType(TextInputType::Text);
    CHECK("Search -> Text takes the button it showed away again", !search->IsShowClearButton());

    auto own = MakeField();
    own->SetShowClearButton(true);
    own->SetInputType(TextInputType::Search);
    own->SetInputType(TextInputType::Text);
    CHECK("a clear button the caller turned on stays when the type moves on",
          own->IsShowClearButton());

    auto off = MakeField();
    off->SetInputType(TextInputType::Search);
    off->SetShowClearButton(false);
    off->SetInputType(TextInputType::Search);
    CHECK("the caller can still turn a Search field's button off, and Search again shows it",
          off->IsShowClearButton());

    // The button works: a click on it empties the field and says so.
    auto field = MakeField();
    field->SetInputType(TextInputType::Search);
    field->SetText("query");
    int cleared = 0;
    std::string changedTo = "unset";
    field->onCleared = [&cleared]() { ++cleared; };
    field->onTextChanged = [&changedTo](const std::string& text) { changedTo = text; };
    // The cross sits at the right end, inside the padding: well within the
    // last 30 pixels of a field at least 100 wide, vertically centred.
    UCEvent click;
    click.type = UCEventType::MouseDown;
    click.button = UCMouseButton::Left;
    click.pointer = Point2Di(static_cast<int>(field->GetWidth()) - 8 - 7 - 2,
                             static_cast<int>(field->GetHeight()) / 2);
    field->OnEvent(click);
    CHECK("a click on a Search field's clear button empties it",
          field->GetText().empty() && cleared == 1 && changedTo.empty());
}

int main() {
    TestFormatterPlaceholder();
    TestCurrencyUnformat();
    TestValidateOnBlur();
    TestInputTypeRules();
    TestOneKeyOneUndo();
    TestFloatGeometry();
    TestFollowUps();
    TestComposition();
    TestSearchClearButton();

    std::cerr << "\nTextInputBehaviourTest: " << checkCount << " checks, " << failCount << " failures" << std::endl;
    return failCount == 0 ? 0 : 1;
}
