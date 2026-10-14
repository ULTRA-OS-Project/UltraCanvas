// include/HTMLReader/HTMLForms.h
// HTML forms with real controls: what a <form> sends (FormSubmission, its
// encodings) and the FormController that holds a form's widgets - built by
// HTMLElementBuilder with BuildOptions::interactiveForms - collects the
// controls that count, submits, resets, and keeps what the reader typed
// across a rebuild of the page.
// Version: 1.1.0 - file uploads: FormSubmission::files (multipart file parts),
//                  FormController's File kind, SetFiles; ActivateControl for a
//                  <label>'s click
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

    // A file an <input type=file> sends: in the entry list after the first
    // `position` fields. With no file chosen, one with an empty name and
    // no bytes (as browsers send it).
    struct File {
        size_t position = 0;
        std::string name;           // the control's name
        std::string fileName;       // the file's own name, no folders
        std::string contentType;    // from its extension, else application/octet-stream
        std::vector<uint8_t> bytes;
    };
    std::vector<File> files;
    // Files that could not be read, or were over the size limit, and went
    // empty.
    std::vector<std::string> warnings;

    // application/x-www-form-urlencoded: name=value&..., UTF-8,
    // percent-encoded, space as '+'. A file is its name, as browsers send
    // it outside multipart.
    std::string Encoded() const;
    // multipart/form-data with `boundary`: text parts, and file parts with
    // their file name, type and bytes. The Content-Type to go with it is
    // ContentType(boundary).
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
        File,        // <input type=file>: the files chosen (SetFiles)
        Other        // colour, range: shown, not sent
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
        // A file control: the files chosen (UTF-8 paths), whether it takes
        // several, its accept="" list, and the label naming the choice.
        std::vector<std::string> files;
        bool multiple = false;
        std::string accept;
        std::weak_ptr<UltraCanvasUIElement> fileNames;
        std::function<void()> openChooser;   // what its button does
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
    // The files chosen for file control `index` (UTF-8 paths); its label
    // shows their names.
    void SetFiles(int index, std::vector<std::string> paths);
    // What a click on a <label> of control `index` does: a checkbox
    // toggles, a radio is chosen, a button is pressed, a file control opens
    // its chooser, any other control takes the focus.
    void ActivateControl(int index);
    // Files larger than this, together, are sent empty (and named in the
    // submission's warnings).
    static constexpr size_t kMaxUploadBytes = 64u * 1024u * 1024u;

    // What the reader changed, to put back after the page is rebuilt.
    struct State {
        std::vector<std::string> texts;
        std::vector<bool> checked;
        std::vector<int> indexes;
        std::vector<std::vector<std::string>> files;
        int focused = -1;
    };
    State Save() const;
    // Into this (rebuilt) form, when it has the same controls.
    void Restore(const State& state);
};

} // namespace HTML
} // namespace UltraCanvas
