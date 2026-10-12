// core/HTMLReader/HTMLForms.cpp
// Form submissions and the form controller (see include/HTMLReader/HTMLForms.h).
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "HTMLReader/HTMLForms.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasRadio.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTextInput.h"

#include <cstdio>

namespace UltraCanvas {
namespace HTML {

namespace {

// application/x-www-form-urlencoded byte serializer: unreserved bytes as
// they are, space as '+', everything else %XX.
std::string FormEncode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '*' || c == '-' || c == '.' || c == '_') {
            out += static_cast<char>(c);
        } else if (c == ' ') {
            out += '+';
        } else {
            char hex[4];
            std::snprintf(hex, sizeof(hex), "%%%02X", c);
            out += hex;
        }
    }
    return out;
}

// Line breaks as CRLF, as forms send them.
std::string Crlf(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r') {
            out += "\r\n";
            if (i + 1 < s.size() && s[i + 1] == '\n') ++i;
        } else if (s[i] == '\n') {
            out += "\r\n";
        } else {
            out += s[i];
        }
    }
    return out;
}

// A multipart field name in quotes: '"' and line breaks escaped as HTML does.
std::string QuotedName(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (c == '"') out += "%22";
        else if (c == '\r') out += "%0D";
        else if (c == '\n') out += "%0A";
        else out += c;
    }
    return out;
}

std::string TextOf(const std::shared_ptr<UltraCanvasUIElement>& e) {
    if (auto* input = dynamic_cast<UltraCanvasTextInput*>(e.get())) return input->GetText();
    if (auto* area = dynamic_cast<UltraCanvasTextArea*>(e.get())) return area->GetText();
    return std::string();
}

void SetTextOf(const std::shared_ptr<UltraCanvasUIElement>& e, const std::string& text) {
    if (auto* input = dynamic_cast<UltraCanvasTextInput*>(e.get())) input->SetText(text);
    else if (auto* area = dynamic_cast<UltraCanvasTextArea*>(e.get())) area->SetText(text, false);
}

} // namespace

std::string FormSubmission::Encoded() const {
    std::string out;
    for (const auto& [name, value] : fields) {
        if (!out.empty()) out += '&';
        out += FormEncode(Crlf(name));
        out += '=';
        out += FormEncode(Crlf(value));
    }
    return out;
}

std::string FormSubmission::Multipart(const std::string& boundary) const {
    std::string out;
    for (const auto& [name, value] : fields) {
        out += "--" + boundary + "\r\n";
        out += "Content-Disposition: form-data; name=\"" + QuotedName(name) + "\"\r\n\r\n";
        out += Crlf(value) + "\r\n";
    }
    out += "--" + boundary + "--\r\n";
    return out;
}

std::string FormSubmission::PlainText() const {
    std::string out;
    for (const auto& [name, value] : fields) out += name + "=" + value + "\r\n";
    return out;
}

std::string FormSubmission::ContentType(const std::string& boundary) const {
    if (enctype == "multipart/form-data") return "multipart/form-data; boundary=" + boundary;
    if (enctype == "text/plain") return "text/plain; charset=utf-8";
    return "application/x-www-form-urlencoded";
}

std::string FormSubmission::Body(const std::string& boundary) const {
    if (enctype == "multipart/form-data") return Multipart(boundary);
    if (enctype == "text/plain") return PlainText();
    return Encoded();
}

FormSubmission FormController::Collect(int submitter) const {
    FormSubmission s;
    s.action = action;
    s.method = method;
    s.enctype = enctype;
    s.target = target;
    for (size_t i = 0; i < controls.size(); ++i) {
        const Control& c = controls[i];
        if (c.name.empty() || c.disabled) continue;
        auto element = c.element.lock();
        switch (c.kind) {
            case Kind::Text:
                s.fields.emplace_back(c.name, TextOf(element));
                break;
            case Kind::Hidden:
                s.fields.emplace_back(c.name, c.value);
                break;
            case Kind::Checkbox:
            case Kind::Radio: {
                auto* toggle = dynamic_cast<UltraCanvasLabeledToggleBase*>(element.get());
                if (toggle && toggle->IsChecked()) s.fields.emplace_back(c.name, c.value.empty() ? "on" : c.value);
                break;
            }
            case Kind::Select: {
                auto* dropdown = dynamic_cast<UltraCanvasDropdown*>(element.get());
                const int index = dropdown ? dropdown->GetSelectedIndex() : -1;
                if (index >= 0 && index < static_cast<int>(c.optionValues.size()))
                    s.fields.emplace_back(c.name, c.optionValues[static_cast<size_t>(index)]);
                break;
            }
            case Kind::Submit:
                if (static_cast<int>(i) == submitter) s.fields.emplace_back(c.name, c.value);
                break;
            default:
                break;
        }
    }
    return s;
}

void FormController::Submit(int submitter) {
    if (!inForm || !onSubmit) return;
    if (submitter >= 0 && submitter < static_cast<int>(controls.size()) &&
        controls[static_cast<size_t>(submitter)].disabled) return;
    onSubmit(Collect(submitter));
}

void FormController::Reset() {
    for (const Control& c : controls) {
        auto element = c.element.lock();
        if (!element) continue;
        switch (c.kind) {
            case Kind::Text: SetTextOf(element, c.initialText); break;
            case Kind::Checkbox:
            case Kind::Radio:
                if (auto* toggle = dynamic_cast<UltraCanvasLabeledToggleBase*>(element.get()))
                    toggle->SetChecked(c.initialChecked);
                break;
            case Kind::Select:
                if (auto* dropdown = dynamic_cast<UltraCanvasDropdown*>(element.get()))
                    dropdown->SetSelectedIndex(c.initialIndex, false);
                break;
            default: break;
        }
    }
}

FormController::State FormController::Save() const {
    State state;
    for (size_t i = 0; i < controls.size(); ++i) {
        auto element = controls[i].element.lock();
        state.texts.push_back(TextOf(element));
        auto* toggle = dynamic_cast<UltraCanvasLabeledToggleBase*>(element.get());
        state.checked.push_back(toggle && toggle->IsChecked());
        auto* dropdown = dynamic_cast<UltraCanvasDropdown*>(element.get());
        state.indexes.push_back(dropdown ? dropdown->GetSelectedIndex() : -1);
        if (element && element->IsFocused()) state.focused = static_cast<int>(i);
    }
    return state;
}

void FormController::Restore(const State& state) {
    if (state.texts.size() != controls.size()) return;   // not the same form any more
    for (size_t i = 0; i < controls.size(); ++i) {
        auto element = controls[i].element.lock();
        if (!element) continue;
        switch (controls[i].kind) {
            case Kind::Text: SetTextOf(element, state.texts[i]); break;
            case Kind::Checkbox:
            case Kind::Radio:
                if (auto* toggle = dynamic_cast<UltraCanvasLabeledToggleBase*>(element.get()))
                    toggle->SetChecked(state.checked[i]);
                break;
            case Kind::Select:
                if (auto* dropdown = dynamic_cast<UltraCanvasDropdown*>(element.get()))
                    dropdown->SetSelectedIndex(state.indexes[i], false);
                break;
            default: break;
        }
        if (state.focused == static_cast<int>(i)) element->SetFocus(true);
    }
}

} // namespace HTML
} // namespace UltraCanvas
