// Tests/HTMLFormsTest.cpp
// HTML forms with real widgets (BuildOptions::interactiveForms): what a form
// sends (FormSubmission's encodings), the widgets the builder makes and where
// they sit, the entry list a FormController collects, submitting by button
// and by Enter, reset, radio groups, disabled controls, and what was typed
// carried across a rebuild (Save / Restore). Headless.
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "HTMLReader/HTMLElementBuilder.h"
#include "HTMLReader/HTMLForms.h"
#include "CSSLayout/CSSLayout.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasRadio.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTextInput.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool cond, const std::string& what) {
    std::printf("  %s %s\n", cond ? "ok:  " : "FAIL:", what.c_str());
    if (!cond) ++g_failures;
}

struct Host : UltraCanvasContainer {
    Host() : UltraCanvasContainer("host") {}
    void Adopt(std::unique_ptr<IRenderContext> c) { renderContext = std::move(c); }
};

struct Built {
    std::shared_ptr<Host> host;
    HTML::BuildResult result;
    std::vector<HTML::FormSubmission> sent;
    std::vector<std::pair<UltraCanvasUIElement*, Rect2Df>> all;   // window-relative

    template <typename T> std::vector<T*> All() const {
        std::vector<T*> out;
        for (const auto& [e, r] : all)
            if (auto* t = dynamic_cast<T*>(e)) out.push_back(t);
        return out;
    }
    Rect2Df RectOf(const UltraCanvasUIElement* element) const {
        for (const auto& [e, r] : all) if (e == element) return r;
        return Rect2Df(-1, -1, 0, 0);
    }
    Rect2Df LabelRect(const std::string& text) const {
        for (const auto& [e, r] : all)
            if (auto* l = dynamic_cast<UltraCanvasLabel*>(e))
                if (l->GetText().find(text) != std::string::npos) return r;
        return Rect2Df(-1, -1, 0, 0);
    }
};

void Collect(UltraCanvasUIElement* e, float ox, float oy,
             std::vector<std::pair<UltraCanvasUIElement*, Rect2Df>>& out) {
    const Rect2Df b = e->GetBounds();
    out.push_back({ e, Rect2Df(ox + b.x, oy + b.y, b.width, b.height) });
    if (auto* c = dynamic_cast<UltraCanvasContainer*>(e))
        for (auto& child : c->GetChildren()) Collect(child.get(), ox + b.x, oy + b.y, out);
}

std::unique_ptr<Built> Build(const std::string& body, bool interactive = true) {
    auto built = std::make_unique<Built>();
    built->host = std::make_shared<Host>();
    built->host->Adopt(CreateRenderContext(Size2Di(600, 800), nullptr));
    HTML::BuildOptions opts;
    opts.style.baseFontSizePx = 14.f;
    opts.viewportWidth = 600.f;
    opts.interactiveForms = interactive;
    Built* raw = built.get();
    opts.onFormSubmit = [raw](const HTML::FormSubmission& s) { raw->sent.push_back(s); };
    HTML::ElementBuilder builder;
    built->result = builder.Build("<!DOCTYPE html><html><body style='margin:0'>" + body + "</body></html>", opts);
    auto& root = built->result.root;
    if (!root) return built;
    root->size.width = CSSLayout::Dimension::Px(600.f);
    built->host->AddChild(root);
    CSSLayout::LayoutContext ctx;
    ctx.viewportWidth = 600;
    ctx.viewportHeight = 800;
    CSSLayout::MeasureConstraints mc{ { CSSLayout::ConstraintMode::Exact, 600 },
                                      { CSSLayout::ConstraintMode::Unbounded, INFINITY } };
    root->Measure(mc, ctx);
    root->Arrange(Rect2Df{ 0, 0, 600, root->measured.measuredHeight }, ctx);
    Collect(root.get(), 0, 0, built->all);
    return built;
}

std::string Fields(const HTML::FormSubmission& s) {
    std::string out;
    for (const auto& [n, v] : s.fields) out += (out.empty() ? "" : "|") + n + "=" + v;
    return out;
}

void TestEncodings() {
    std::printf("what a form sends\n");
    HTML::FormSubmission s;
    s.fields = { { "q", "caf\xC3\xA9 & tea" }, { "a=b", "1+1" }, { "note", "two\nlines" } };
    Check(s.Encoded() == "q=caf%C3%A9+%26+tea&a%3Db=1%2B1&note=two%0D%0Alines", "urlencoded: " + s.Encoded());
    s.enctype = "multipart/form-data";
    const std::string body = s.Body("XyZ");
    Check(body.find("--XyZ\r\nContent-Disposition: form-data; name=\"q\"\r\n\r\ncaf\xC3\xA9 & tea\r\n") == 0,
          "multipart starts with the first field");
    Check(body.find("two\r\nlines\r\n") != std::string::npos, "multipart line breaks as CRLF");
    Check(body.size() >= 9 && body.compare(body.size() - 9, 9, "--XyZ--\r\n") == 0, "and ends with the closing boundary");
    Check(s.ContentType("XyZ") == "multipart/form-data; boundary=XyZ", "multipart content type");
    s.enctype = "text/plain";
    Check(s.Body("") == "q=caf\xC3\xA9 & tea\r\na=b=1+1\r\nnote=two\nlines\r\n", "text/plain");
    s.enctype = "application/x-www-form-urlencoded";
    Check(s.ContentType("") == "application/x-www-form-urlencoded", "urlencoded content type");
}

const char* kForm =
    "<form action='/search' method='POST' enctype='multipart/form-data'>"
    "<p>Name: <input name='q' value='hi' size='10'> <input type='password' name='pw'></p>"
    "<p><label><input type='checkbox' name='news' checked> News</label>"
    " <label><input type='checkbox' name='spam' value='yes'> Spam</label></p>"
    "<p><input type='radio' name='size' value='s'> S <input type='radio' name='size' value='l' checked> L</p>"
    "<p><select name='lang'><option value='en'>English</option><option selected>Deutsch</option>"
    "<optgroup label='x'><option value='fr'>Fran\xC3\xA7" "ais</option></optgroup></select></p>"
    "<textarea name='msg' rows='3'>\nHello\nthere</textarea>"
    "<input type='hidden' name='token' value='t0k'>"
    "<input name='off' value='x' disabled>"
    "<fieldset disabled><input name='off2' value='y'></fieldset>"
    "<p><button name='go' value='1'>Search</button> <input type='submit' name='alt' value='Other'>"
    " <input type='reset'> <button type='button' name='nothing'>Nothing</button></p>"
    "</form>"
    "<p>Outside: <input name='lonely' value='z'></p>";

void TestWidgets() {
    std::printf("the controls are the framework's widgets\n");
    auto b = Build(kForm);
    Check(b->All<UltraCanvasTextInput>().size() == 5, "five text inputs (two disabled, one outside)");
    Check(b->All<UltraCanvasCheckbox>().size() == 2, "two checkboxes");
    Check(b->All<UltraCanvasRadio>().size() == 2, "two radios");
    Check(b->All<UltraCanvasDropdown>().size() == 1, "a dropdown");
    Check(b->All<UltraCanvasTextArea>().size() == 1, "a text area");
    Check(b->All<UltraCanvasButton>().size() == 4, "four buttons");
    Check(b->result.forms.size() == 2, "a controller for the form, one for the controls outside it");

    // "Name: [______]" on one line.
    auto inputs = b->All<UltraCanvasTextInput>();
    if (!inputs.empty()) {
        const Rect2Df input = b->RectOf(inputs[0]);
        const Rect2Df label = b->LabelRect("Name");
        Check(std::fabs((input.y + input.height / 2) - (label.y + label.height / 2)) < 8.f,
              "the input on the line with its label");
        Check(input.x > label.x + label.width - 1.f, "after it");
        Check(input.width > 50.f && input.width < 200.f, "about size='10' wide");
    }
    auto inputs2 = b->All<UltraCanvasTextInput>();
    if (inputs2.size() >= 2) Check(inputs2[1]->GetInputType() == TextInputType::Password, "type=password masks");

    auto areas = b->All<UltraCanvasTextArea>();
    if (!areas.empty()) Check(areas[0]->GetText() == "Hello\nthere", "a textarea's first newline is dropped");

    auto display = Build(kForm, /*interactive=*/false);
    Check(display->All<UltraCanvasTextInput>().empty() && display->result.forms.empty(),
          "interactiveForms off: drawn for display only");
}

void TestEntryList() {
    std::printf("the entry list, submitting, reset\n");
    auto b = Build(kForm);
    if (b->result.forms.size() != 2) { Check(false, "built"); return; }
    HTML::FormController& form = *b->result.forms[0];
    Check(form.method == "post" && form.enctype == "multipart/form-data" && form.action == "/search",
          "the form's method, enctype and action");
    Check(Fields(form.Collect()) == "q=hi|pw=|news=on|size=l|lang=Deutsch|msg=Hello\nthere|token=t0k",
          "as the page wrote it: " + Fields(form.Collect()));

    auto inputs = b->All<UltraCanvasTextInput>();
    auto boxes = b->All<UltraCanvasCheckbox>();
    auto radios = b->All<UltraCanvasRadio>();
    auto dropdown = b->All<UltraCanvasDropdown>();
    inputs[0]->SetText("caf\xC3\xA9");
    boxes[0]->SetChecked(false);
    boxes[1]->SetChecked(true);
    form.radioGroups["size"]->SelectButton(std::static_pointer_cast<UltraCanvasRadio>(radios[0]->shared_from_this()));
    Check(!radios[1]->IsChecked(), "a radio group keeps one checked");
    dropdown[0]->SetSelectedIndex(2, false);
    Check(Fields(form.Collect()) == "q=caf\xC3\xA9|pw=|spam=yes|size=s|lang=fr|msg=Hello\nthere|token=t0k",
          "after the reader's changes: " + Fields(form.Collect()));

    // A button sends itself; Enter in a field sends no button.
    auto buttons = b->All<UltraCanvasButton>();
    buttons[0]->onClick();
    Check(b->sent.size() == 1 && Fields(b->sent[0]).find("go=1") != std::string::npos, "the clicked button is sent");
    buttons[1]->onClick();
    Check(b->sent.size() == 2 && Fields(b->sent[1]).find("alt=Other") != std::string::npos &&
          Fields(b->sent[1]).find("go=") == std::string::npos, "only the clicked one");
    Check(inputs[0]->onEnterPressed && inputs[0]->onEnterPressed("x"), "Enter in a field submits");
    Check(b->sent.size() == 3 && Fields(b->sent[2]).find("go=") == std::string::npos &&
          Fields(b->sent[2]).find("alt=") == std::string::npos, "with no button");
    buttons[3]->onClick();
    Check(b->sent.size() == 3, "type=button sends nothing");
    inputs.back()->onEnterPressed("x");
    Check(b->sent.size() == 3, "a field outside every form sends nothing");

    buttons[2]->onClick();   // reset
    Check(Fields(form.Collect()) == "q=hi|pw=|news=on|size=l|lang=Deutsch|msg=Hello\nthere|token=t0k",
          "reset puts back what the page wrote");
}

void TestSaveRestore() {
    std::printf("what was typed survives a rebuild\n");
    auto first = Build(kForm);
    auto second = Build(kForm);
    if (first->result.forms.empty() || second->result.forms.empty()) { Check(false, "built"); return; }
    first->All<UltraCanvasTextInput>()[0]->SetText("typed");
    first->All<UltraCanvasCheckbox>()[1]->SetChecked(true);
    first->All<UltraCanvasDropdown>()[0]->SetSelectedIndex(0, false);
    const HTML::FormController::State state = first->result.forms[0]->Save();
    second->result.forms[0]->Restore(state);
    Check(second->All<UltraCanvasTextInput>()[0]->GetText() == "typed", "the text");
    Check(second->All<UltraCanvasCheckbox>()[1]->IsChecked(), "the checkbox");
    Check(second->All<UltraCanvasDropdown>()[0]->GetSelectedIndex() == 0, "the choice");

    auto other = Build("<form><input name='a'></form>");
    other->result.forms[0]->Restore(state);
    Check(other->All<UltraCanvasTextInput>()[0]->GetText().empty(), "not into a different form");
}

void TestFormAttribute() {
    std::printf("form=\"id\" joins a form from outside it\n");
    auto b = Build("<form id='f' action='/x'><input name='a' value='1'></form>"
                   "<p><input name='b' value='2' form='f'> <input name='c' value='3' form='nope'></p>");
    if (b->result.forms.empty()) { Check(false, "built"); return; }
    Check(Fields(b->result.forms[0]->Collect()) == "a=1|b=2", "b is in form f: " + Fields(b->result.forms[0]->Collect()));
}

void TestFlexItems() {
    std::printf("controls as flex items: a search bar\n");
    auto b = Build("<form action='/s' style='display:flex;gap:8px'><input name='q' style='flex:1'>"
                   "<button>Go</button></form>");
    auto inputs = b->All<UltraCanvasTextInput>();
    auto buttons = b->All<UltraCanvasButton>();
    Check(inputs.size() == 1 && buttons.size() == 1, "a field and a button");
    if (inputs.size() == 1 && buttons.size() == 1) {
        const Rect2Df field = b->RectOf(inputs[0]), go = b->RectOf(buttons[0]);
        Check(field.width > 300.f, "the field grows (flex: 1)");
        Check(go.x > field.x + field.width, "the button after it");
        Check(std::fabs(go.y - field.y) < 12.f, "on one line");
    }
}

} // namespace

int main() {
    TestEncodings();
    TestWidgets();
    TestEntryList();
    TestSaveRestore();
    TestFormAttribute();
    TestFlexItems();
    if (g_failures) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
