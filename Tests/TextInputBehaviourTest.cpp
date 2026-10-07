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
// Each section below fails without its fix.
//
// Runs headless: the field is driven through its public API and key and focus
// events, without a window.
// Version: 1.0.0
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework

// Checked mode for this file's inline standard-library code: reading front() of
// an empty string - fix 2 - then aborts instead of quietly reading the
// terminating zero, so the check below can see it. libstdc++ only; elsewhere
// the macro is ignored. It changes no layout, so mixing with the library is safe.
#ifndef _GLIBCXX_ASSERTIONS
#define _GLIBCXX_ASSERTIONS 1
#endif

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

} // namespace

int main() {
    TestFormatterPlaceholder();
    TestCurrencyUnformat();
    TestValidateOnBlur();
    TestInputTypeRules();
    TestOneKeyOneUndo();
    TestFloatGeometry();
    TestFollowUps();

    std::cerr << "\nTextInputBehaviourTest: " << checkCount << " checks, " << failCount << " failures" << std::endl;
    return failCount == 0 ? 0 : 1;
}
