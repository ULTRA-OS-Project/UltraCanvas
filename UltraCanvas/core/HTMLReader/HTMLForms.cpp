// core/HTMLReader/HTMLForms.cpp
// Form submissions and the form controller (see include/HTMLReader/HTMLForms.h).
// Version: 1.1.0 - file uploads (multipart file parts, SetFiles), ActivateControl
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "HTMLReader/HTMLForms.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasRadio.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTextInput.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>

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

// A file's type from its extension: the common ones a form uploads.
std::string ContentTypeFor(const std::string& fileName) {
    const size_t dot = fileName.rfind('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string ext = fileName.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    static const std::pair<const char*, const char*> kTypes[] = {
        { "txt", "text/plain" }, { "csv", "text/csv" }, { "htm", "text/html" }, { "html", "text/html" },
        { "css", "text/css" }, { "xml", "application/xml" }, { "json", "application/json" },
        { "pdf", "application/pdf" }, { "zip", "application/zip" }, { "gz", "application/gzip" },
        { "png", "image/png" }, { "jpg", "image/jpeg" }, { "jpeg", "image/jpeg" }, { "gif", "image/gif" },
        { "webp", "image/webp" }, { "svg", "image/svg+xml" }, { "bmp", "image/bmp" }, { "avif", "image/avif" },
        { "mp3", "audio/mpeg" }, { "ogg", "audio/ogg" }, { "wav", "audio/wav" }, { "mp4", "video/mp4" },
        { "webm", "video/webm" }, { "doc", "application/msword" },
        { "docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document" },
        { "xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet" },
        { "odt", "application/vnd.oasis.opendocument.text" },
    };
    for (const auto& [e, type] : kTypes)
        if (ext == e) return type;
    return "application/octet-stream";
}

// The entry list in order: each file before the field at its position.
template <typename Field, typename File>
void ForEachEntry(const FormSubmission& s, Field field, File file) {
    for (size_t i = 0; i <= s.fields.size(); ++i) {
        for (const auto& f : s.files)
            if (f.position == i || (i == s.fields.size() && f.position > i)) file(f);
        if (i < s.fields.size()) field(s.fields[i].first, s.fields[i].second);
    }
}

std::string FileLabelText(const std::vector<std::string>& paths, bool multiple) {
    if (paths.empty()) return multiple ? "No files chosen" : "No file chosen";
    if (paths.size() > 1) return std::to_string(paths.size()) + " files";
    return PathToUtf8(PathFromUtf8(paths[0]).filename());
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
    auto add = [&](const std::string& name, const std::string& value) {
        if (!out.empty()) out += '&';
        out += FormEncode(Crlf(name));
        out += '=';
        out += FormEncode(Crlf(value));
    };
    ForEachEntry(*this, add, [&](const File& f) { add(f.name, f.fileName); });
    return out;
}

std::string FormSubmission::Multipart(const std::string& boundary) const {
    std::string out;
    ForEachEntry(*this,
        [&](const std::string& name, const std::string& value) {
            out += "--" + boundary + "\r\n";
            out += "Content-Disposition: form-data; name=\"" + QuotedName(name) + "\"\r\n\r\n";
            out += Crlf(value) + "\r\n";
        },
        [&](const File& f) {
            out += "--" + boundary + "\r\n";
            out += "Content-Disposition: form-data; name=\"" + QuotedName(f.name) +
                   "\"; filename=\"" + QuotedName(f.fileName) + "\"\r\n";
            out += "Content-Type: " + f.contentType + "\r\n\r\n";
            out.append(f.bytes.begin(), f.bytes.end());
            out += "\r\n";
        });
    out += "--" + boundary + "--\r\n";
    return out;
}

std::string FormSubmission::PlainText() const {
    std::string out;
    ForEachEntry(*this,
        [&](const std::string& name, const std::string& value) { out += name + "=" + value + "\r\n"; },
        [&](const File& f) { out += f.name + "=" + f.fileName + "\r\n"; });
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
            case Kind::File: {
                if (c.files.empty()) {
                    s.files.push_back({ s.fields.size(), c.name, std::string(), "application/octet-stream", {} });
                    break;
                }
                for (const std::string& path : c.files) {
                    FormSubmission::File file;
                    file.position = s.fields.size();
                    file.name = c.name;
                    file.fileName = PathToUtf8(PathFromUtf8(path).filename());
                    file.contentType = ContentTypeFor(file.fileName);
                    std::error_code ec;
                    const auto size = std::filesystem::file_size(PathFromUtf8(path), ec);
                    size_t used = 0;
                    for (const auto& f : s.files) used += f.bytes.size();
                    if (ec) {
                        s.warnings.push_back("cannot read " + path);
                    } else if (used + size > kMaxUploadBytes) {
                        s.warnings.push_back(file.fileName + " is over the upload limit and was sent empty");
                    } else if (std::FILE* in = OpenFileUtf8(path, "rb")) {
                        file.bytes.resize(static_cast<size_t>(size));
                        const size_t got = size ? std::fread(file.bytes.data(), 1, file.bytes.size(), in) : 0;
                        file.bytes.resize(got);
                        std::fclose(in);
                    } else {
                        s.warnings.push_back("cannot read " + path);
                    }
                    s.files.push_back(std::move(file));
                }
                break;
            }
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

void FormController::SetFiles(int index, std::vector<std::string> paths) {
    if (index < 0 || index >= static_cast<int>(controls.size())) return;
    Control& c = controls[static_cast<size_t>(index)];
    if (c.kind != Kind::File) return;
    if (!c.multiple && paths.size() > 1) paths.resize(1);
    c.files = std::move(paths);
    if (auto names = c.fileNames.lock())
        if (auto* label = dynamic_cast<UltraCanvasLabel*>(names.get()))
            label->SetText(FileLabelText(c.files, c.multiple));
}

void FormController::ActivateControl(int index) {
    if (index < 0 || index >= static_cast<int>(controls.size())) return;
    Control& c = controls[static_cast<size_t>(index)];
    if (c.disabled) return;
    if (c.kind == Kind::File) {
        if (c.openChooser) c.openChooser();
        return;
    }
    auto element = c.element.lock();
    if (!element || element->IsDisabled()) return;
    switch (c.kind) {
        case Kind::Checkbox:
            if (auto* toggle = dynamic_cast<UltraCanvasLabeledToggleBase*>(element.get()))
                toggle->SetChecked(!toggle->IsChecked());
            break;
        case Kind::Radio: {
            // Chosen through its group, which clears the others.
            auto radio = std::dynamic_pointer_cast<UltraCanvasRadio>(element);
            if (!radio) break;
            auto group = radioGroups.find(c.name);
            if (group != radioGroups.end() && group->second) group->second->SelectButton(radio);
            else radio->SetChecked(true);
            break;
        }
        case Kind::Submit: case Kind::Reset: case Kind::Button:
            if (auto* button = dynamic_cast<UltraCanvasButton*>(element.get()))
                if (button->onClick) button->onClick();
            break;
        default:
            element->SetFocus(true);
            break;
    }
}

void FormController::Reset() {
    for (Control& c : controls) {
        if (c.kind == Kind::File) {
            SetFiles(static_cast<int>(&c - controls.data()), {});
            continue;
        }
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
        state.files.push_back(controls[i].files);
        if (element && element->IsFocused()) state.focused = static_cast<int>(i);
    }
    return state;
}

void FormController::Restore(const State& state) {
    if (state.texts.size() != controls.size()) return;   // not the same form any more
    for (size_t i = 0; i < controls.size(); ++i) {
        if (controls[i].kind == Kind::File && i < state.files.size()) {
            SetFiles(static_cast<int>(i), state.files[i]);
            continue;
        }
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
