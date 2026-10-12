// include/HTMLReader/HTMLForms.h
// HTML forms with real controls: what a <form> sends (FormSubmission, its
// encodings) and the FormController that holds a form's widgets - built by
// HTMLElementBuilder with BuildOptions::interactiveForms - collects the
// controls that count, submits, resets, and keeps what the reader typed
// across a rebuild of the page.
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace UltraCanvas {

class UltraCanvasUIElement;
class UltraCanvasRadioGroup;

namespace HTML {

// What a form sends: the form's attributes and its successful controls'
// name / value pairs in tree order (HTML's "constructing the entry list").
struct FormSubmission {
    std::string action;     // as written; "" means the document's own address
    std::string method;     // "get" or "post" (dialog and others read as get)
    std::string enctype;    // "application/x-www-form-urlencoded", "multipart/form-data" or "text/plain"
    std::string target;     // as written; the web view ignores it
    std::vector<std::pair<std::string, std::string>> fields;

    // application/x-www-form-urlencoded: name=value&..., UTF-8,
    // percent-encoded, space as '+'.
    std::string Encoded() const;
    // multipart/form-data with `boundary` (text fields only; file inputs are
    // not sent). The Content-Type to go with it is ContentType(boundary).
    std::string Multipart(const std::string& boundary) const;
    // text/plain: name=value lines, CRLF.
    std::string PlainText() const;
    // The Content-Type for a POST body in this form's enctype.
    std::string ContentType(const std::string& boundary) const;
    // The body for a POST, in this form's enctype.
    std::string Body(const std::string& boundary) const;
};

// The controls of one <form> (or of none: those outside every form, which
// cannot submit). Owned by BuildResult::forms; it holds its widgets weakly,
// so the element tree stays their owner.
class FormController {
public:
    enum class Kind {
        Text,        // <input> text-like, <textarea>
        Checkbox,
        Radio,
        Select,
        Hidden,      // <input type=hidden>: a fixed value
        Submit,      // <input type=submit|image>, <button type=submit>
        Reset,
        Button,      // <button type=button>, <input type=button>: nothing
        Other        // file and the like: shown, not sent
    };
    struct Control {
        Kind kind = Kind::Other;
        std::string name;
        std::string value;               // a checkbox / radio / button's value, a hidden input's
        bool disabled = false;
        std::weak_ptr<UltraCanvasUIElement> element;
        // As the page wrote it, for Reset.
        std::string initialText;
        bool initialChecked = false;
        int initialIndex = -1;
        // A select's option values, by item index.
        std::vector<std::string> optionValues;
    };

    std::string action, method = "get", enctype = "application/x-www-form-urlencoded", target;
    bool inForm = false;                 // false: the controls outside every form
    std::vector<Control> controls;
    std::map<std::string, std::shared_ptr<UltraCanvasRadioGroup>> radioGroups;   // by name
    std::function<void(const FormSubmission&)> onSubmit;

    // The entry list as it would be sent now; `submitter` is the button that
    // sent it (index into controls), -1 for an implicit submission.
    FormSubmission Collect(int submitter = -1) const;
    // Collect and hand to onSubmit (nothing outside a form).
    void Submit(int submitter = -1);
    // Every control back to what the page wrote.
    void Reset();

    // What the reader changed, to put back after the page is rebuilt.
    struct State {
        std::vector<std::string> texts;
        std::vector<bool> checked;
        std::vector<int> indexes;
        int focused = -1;
    };
    State Save() const;
    // Into this (rebuilt) form, when it has the same controls.
    void Restore(const State& state);
};

} // namespace HTML
} // namespace UltraCanvas
