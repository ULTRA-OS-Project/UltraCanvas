// Apps/Texter/UltraCanvasTextEditorSpellCheck.cpp
// Spell checking and the editor context menu for UltraTexter
// Version: 1.0.0
// Last Modified: 2026-08-28
// Author: UltraCanvas Framework
//
// The checking itself lives in the framework (UltraCanvasSpellChecker plus the
// UltraCanvasTextArea integration). What is here is the application half: when
// the service is started, which documents get squiggles, how the user turns it
// on and picks a dictionary, and the right-click menu the suggestions appear in.

#include "UltraCanvasTextEditor.h"
#include "UltraCanvasMarkdownSpellRanges.h"
#include "UltraCanvasSpellChecker.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasDebug.h"

#include <algorithm>
#include <mutex>

namespace UltraCanvas {

namespace {

    // Holds one check's markdown skip ranges, scanned on first query rather
    // than when the options are built: that moves the pass off the UI thread,
    // which would otherwise re-scan the whole document on every keystroke.
    // shouldSkipRange runs on the spell worker thread, so the scan is guarded
    // by call_once rather than assuming one worker.
    struct MarkdownSkipRanges {
        explicit MarkdownSkipRanges(std::string documentText)
            : text(std::move(documentText)) {}

        bool Covers(size_t startByte, size_t byteLength) {
            std::call_once(scanned, [this]() {
                ranges = ScanMarkdownNoSpellRanges(text);
                std::string().swap(text);   // the scan is the only reader
            });
            return SpanCoversRange(ranges, startByte, byteLength);
        }

        std::string text;
        std::vector<TextByteSpan> ranges;
        std::once_flag scanned;
    };

} // namespace

// ===================================================================
// LIVE EDITOR REGISTRY
// ===================================================================

// UltraTexter runs several editor windows in one process ("Open in New
// Window", and every tab dragged out - see main.cpp), while the spell service
// is a singleton with one callback slot. Registering the windows here lets a
// language change or a newly learned word reach all of them instead of only
// whichever window installed its callback last.
//
// UI thread only: filled by the constructor, emptied by the destructor.
std::vector<UltraCanvasTextEditor*>& UltraCanvasTextEditor::LiveEditors() {
    static std::vector<UltraCanvasTextEditor*> editors;
    return editors;
}

void UltraCanvasTextEditor::InstallSpellServiceCallbacks() {
    static bool installed = false;
    if (installed) return;
    installed = true;

    UltraCanvasSpellChecker& service = UltraCanvasSpellChecker::Instance();

    service.onSpellLanguageChange = [](const std::string& languageCode) {
        // Copy: a handler may open a window, which would invalidate the vector.
        std::vector<UltraCanvasTextEditor*> editors = LiveEditors();
        for (UltraCanvasTextEditor* editor : editors) {
            editor->OnSpellLanguageChanged(languageCode);
        }
    };

    service.onSpellDictionaryChange = [](const std::string&) {
        std::vector<UltraCanvasTextEditor*> editors = LiveEditors();
        for (UltraCanvasTextEditor* editor : editors) {
            editor->RecheckAllDocuments();
        }
    };
}

// ===================================================================
// SERVICE LIFECYCLE
// ===================================================================

void UltraCanvasTextEditor::SetupSpellChecker() {
    LiveEditors().push_back(this);
    InstallSpellServiceCallbacks();

    // The backend is only loaded once the user has actually asked for spell
    // checking, so a launch with the feature off costs nothing.
    if (!config.spellCheckEnabled) return;
    if (!EnsureSpellServiceReady()) {
        config.spellCheckEnabled = false;
    }
}

void UltraCanvasTextEditor::ShutdownSpellChecker() {
    std::vector<UltraCanvasTextEditor*>& editors = LiveEditors();
    editors.erase(std::remove(editors.begin(), editors.end(), this), editors.end());
}

bool UltraCanvasTextEditor::EnsureSpellServiceReady() {
    UltraCanvasSpellChecker& service = UltraCanvasSpellChecker::Instance();

    if (!service.IsInitialized() && !service.Initialize()) {
        debugOutput << "UltraTexter: no spell check backend available" << std::endl;
        return false;
    }
    if (service.GetAvailableLanguages().empty()) {
        debugOutput << "UltraTexter: spell check backend has no dictionaries" << std::endl;
        return false;
    }

    // An empty setting means "whatever the desktop locale asks for", which is
    // resolved once and then written back so the choice is stable.
    std::string language = config.spellCheckLanguage;
    if (language.empty()) language = service.DetectPreferredLanguage();
    if (!language.empty() && language != service.GetLanguage()) {
        service.SetLanguage(language);
    }

    service.SetMode(SpellCheckMode::AsYouType);
    return true;
}

// ===================================================================
// PER-DOCUMENT WIRING
// ===================================================================

// Hex view has no words and a PDF tab has no text area, so neither is checked.
// Everything else is, including source files: the check already skips
// identifiers, ALL-CAPS words, camelCase and anything with a digit or an
// underscore, so what is left to flag in code is mostly comments and strings.
bool UltraCanvasTextEditor::DocumentWantsSpellCheck(const DocumentTab* doc) const {
    if (!doc || !doc->textArea) return false;
    if (doc->IsPdf()) return false;
    return doc->textArea->GetEditingMode() != TextAreaEditingMode::Hex;
}

void UltraCanvasTextEditor::ApplySpellCheckToDocument(int docIndex) {
    if (docIndex < 0 || docIndex >= static_cast<int>(documents.size())) return;

    auto doc = documents[docIndex];
    if (!doc->textArea) return;

    const bool enabled = config.spellCheckEnabled && DocumentWantsSpellCheck(doc.get());
    const bool wasEnabled = doc->textArea->IsSpellCheckEnabled();

    // Markdown documents get a skip hook so fenced code, link targets and math
    // are left alone. It is rebuilt per check because its byte ranges describe
    // the text of that check (see UltraCanvasTextArea::onPrepareSpellCheck).
    if (enabled && doc->language == "Markdown") {
        doc->textArea->onPrepareSpellCheck =
            [](SpellCheckOptions& options, const std::string& text) {
                auto skipRanges = std::make_shared<MarkdownSkipRanges>(text);
                options.shouldSkipRange =
                    [skipRanges](size_t startByte, size_t byteLength) {
                        return skipRanges->Covers(startByte, byteLength);
                    };
            };
    } else {
        doc->textArea->onPrepareSpellCheck = nullptr;
    }

    doc->textArea->SetSpellCheckEnabled(enabled);

    // Turning it on queues a check by itself; a document that was already being
    // checked will not, and its errors were produced by the previous hook. That
    // is the common case here - a file opens as plain text and only becomes
    // markdown once its extension is known - so re-check explicitly.
    if (enabled && wasEnabled) {
        doc->textArea->RunSpellCheck();
    }
}

void UltraCanvasTextEditor::ApplySpellCheckToAllDocuments() {
    for (int i = 0; i < static_cast<int>(documents.size()); ++i) {
        ApplySpellCheckToDocument(i);
    }
}

void UltraCanvasTextEditor::RecheckAllDocuments() {
    for (auto& doc : documents) {
        if (doc->textArea && doc->textArea->IsSpellCheckEnabled()) {
            doc->textArea->RunSpellCheck();
        }
    }
}

// ===================================================================
// MENU ACTIONS
// ===================================================================

void UltraCanvasTextEditor::OnEditToggleSpellCheck(bool checked) {
    if (checked && !EnsureSpellServiceReady()) {
        config.spellCheckEnabled = false;
        ApplySpellCheckToAllDocuments();
        SaveConfig();
        UltraCanvasDialogManager::ShowInformation(
            "No spell check dictionaries were found on this system.\n\n"
            "Install a dictionary for your language (for example the "
            "hunspell or myspell package for it) and try again.",
            "Spell Check Unavailable",
            nullptr, this);
        return;
    }

    config.spellCheckEnabled = checked;
    ApplySpellCheckToAllDocuments();
    SaveConfig();
}

void UltraCanvasTextEditor::OnSpellLanguageChanged(const std::string& languageCode) {
    config.spellCheckLanguage = languageCode;
    SaveConfig();
    RecheckAllDocuments();
}

std::vector<MenuItemData> UltraCanvasTextEditor::BuildSpellingMenuItems() {
    std::vector<MenuItemData> items;

    items.push_back(MenuItemData::Checkbox("Check Spelling", config.spellCheckEnabled,
        [this](bool checked) {
            OnEditToggleSpellCheck(checked);
        }));

    items.push_back(MenuItemData::Separator());

    // Lambda-provided, so the active-language radio is right every time the
    // submenu opens even though the menu bar is built once at startup.
    items.push_back(MenuItemData::Submenu("Dictionary", []() {
        std::vector<MenuItemData> languages;
        UltraCanvasSpellChecker& service = UltraCanvasSpellChecker::Instance();
        if (service.IsInitialized()) {
            languages = UltraCanvasSpellChecker::BuildLanguageMenuItems(true);
        }
        if (languages.empty()) {
            MenuItemData none = MenuItemData::Action("(no dictionaries installed)", []() {});
            none.enabled = false;
            languages.push_back(std::move(none));
        }
        return languages;
    }));

    MenuItemData recheck = MenuItemData::Action("Recheck Document", []() {
        UltraCanvasSpellChecker::Instance().RequestRecheck();
    });
    recheck.enabled = config.spellCheckEnabled;
    items.push_back(std::move(recheck));

    return items;
}

// ===================================================================
// EDITOR CONTEXT MENU
// ===================================================================

// Called from the text area's right-click hook. Returning true tells the text
// area the click is handled, which is what keeps its own spell popup from
// opening on top of this menu - and what makes it move the caret to the click
// afterwards, so Paste lands where the user clicked.
bool UltraCanvasTextEditor::ShowEditorContextMenu(int docIndex, const UCEvent& event) {
    if (docIndex < 0 || docIndex >= static_cast<int>(documents.size())) return false;
    if (!window) return false;

    auto doc = documents[docIndex];
    auto textArea = doc->textArea;
    if (!textArea) return false;

    editorContextMenu = std::make_shared<UltraCanvasMenu>("EditorContextMenu", 0, 0, 220, 0);
    editorContextMenu->SetMenuType(MenuType::PopupMenu);

    for (MenuItemData& item : BuildEditorContextMenuItems(doc.get(), event)) {
        editorContextMenu->AddItem(std::move(item));
    }

    PopupElementSettings settings;
    settings.closeByEscapeKey = true;
    settings.closeByClickOutside = true;
    editorContextMenu->OpenMenu(event.pointerWindow, *window, settings);
    return true;
}

std::vector<MenuItemData> UltraCanvasTextEditor::BuildEditorContextMenuItems(
        DocumentTab* doc, const UCEvent& event) {

    std::vector<MenuItemData> items;
    auto textArea = doc->textArea;

    // Suggestions first, so the reason the user right-clicked a squiggle is the
    // first thing under the pointer.
    if (const SpellError* hit = textArea->GetSpellErrorAtPosition(event.pointer.x,
                                                                  event.pointer.y)) {
        // The menu outlives this call and applying a suggestion rebuilds the
        // error list `hit` points into, so the error is copied out.
        const SpellError error = *hit;
        std::weak_ptr<UltraCanvasTextArea> weakArea = textArea;

        for (MenuItemData& item : UltraCanvasSpellChecker::BuildSuggestionMenuItems(
                 error,
                 [weakArea, error](const std::string& replacement) {
                     if (auto area = weakArea.lock()) {
                         area->ApplySpellSuggestion(error, replacement);
                     }
                 },
                 [weakArea]() {
                     if (auto area = weakArea.lock()) area->RunSpellCheck();
                 })) {
            items.push_back(std::move(item));
        }
        items.push_back(MenuItemData::Separator());
    }

    const bool hasSelection = textArea->HasSelection();
    const bool editable = !textArea->IsReadOnly();

    MenuItemData undo = MenuItemData::ActionWithShortcut("Undo", "Ctrl+Z",
        [this]() { OnEditUndo(); });
    undo.enabled = editable && textArea->CanUndo();
    items.push_back(std::move(undo));

    MenuItemData redo = MenuItemData::ActionWithShortcut("Redo", "Ctrl+Y",
        [this]() { OnEditRedo(); });
    redo.enabled = editable && textArea->CanRedo();
    items.push_back(std::move(redo));

    items.push_back(MenuItemData::Separator());

    MenuItemData cut = MenuItemData::ActionWithShortcut("Cut", "Ctrl+X",
        [this]() { OnEditCut(); });
    cut.enabled = hasSelection && editable;
    items.push_back(std::move(cut));

    MenuItemData copy = MenuItemData::ActionWithShortcut("Copy", "Ctrl+C",
        [this]() { OnEditCopy(); });
    copy.enabled = hasSelection;
    items.push_back(std::move(copy));

    MenuItemData paste = MenuItemData::ActionWithShortcut("Paste", "Ctrl+V",
        [this]() { OnEditPaste(); });
    paste.enabled = editable;
    items.push_back(std::move(paste));

    items.push_back(MenuItemData::Separator());
    items.push_back(MenuItemData::ActionWithShortcut("Select All", "Ctrl+A",
        [this]() { OnEditSelectAll(); }));

    items.push_back(MenuItemData::Separator());
    items.push_back(MenuItemData::Submenu("Spelling", [this]() {
        return BuildSpellingMenuItems();
    }));

    return items;
}

} // namespace UltraCanvas
