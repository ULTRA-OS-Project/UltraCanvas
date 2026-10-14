// Apps/UltraPress/ui/UltraPressWindow.cpp
// The editor window: lists, preview and the property pane from the registry.
// Version: 0.7.3 - a richtext block edits in place
// Version: 0.7.2 - an FAQ question edits in place; a paragraph field keeps its text when left
// Version: 0.7.0 - text edited in place
// Version: 0.6.0 - the media library
// Version: 0.5.1 - Publish prunes
// Version: 0.5.0 - Publish and Host...
// Version: 0.3.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressWindow.h"
#include "UltraPressMediaDialog.h"

#include "UltraPressBlocks.h"
#include "UltraPressPublish.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasPathUtf8.h"
#include "HTMLReader/HTMLRichDocumentImporter.h"

// The hosts and their dialog: module headers after the UI headers (X11
// macro ordering), and only where the build has them.
#include "UltraPressDeploy.h"
#if ULTRAPRESS_HAS_ULTRACLOUD
#include "UltraPressHosting.h"
#endif
#if ULTRAPRESS_HAS_ULTRACLOUD_UI
#include "UltraCloudAccountDialog.h"
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace UltraCanvas;

namespace UltraPress {

namespace {

const Color kNight(13, 12, 34, 255);
const Color kPaper(255, 255, 255, 255);
const Color kPaperSoft(243, 243, 246, 255);
const Color kOnDark(208, 208, 220, 255);
const Color kInk(32, 32, 32, 255);
const Color kInkSoft(90, 90, 98, 255);
const Color kAccent(169, 7, 7, 255);
const Color kRule(220, 220, 224, 255);
const char* kFont = "Ubuntu";

std::shared_ptr<UltraCanvasLabel> Caption(const std::string& id, const std::string& text, const Color& color, float size = 12,
                                          bool bold = true) {
    auto label = CreateLabel(id, text);
    label->SetFont(kFont, size, bold ? FontWeight::Bold : FontWeight::Normal);
    label->SetTextColor(color);
    label->SetWrap(TextWrap::WrapWord);
    label->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
    return label;
}

std::shared_ptr<UltraCanvasButton> SideButton(const std::string& id, const std::string& text, bool active) {
    auto button = CreateButton(id, 0, 0, 0, 30, text);
    button->SetFont(kFont, 14, active ? FontWeight::Bold : FontWeight::Normal);
    button->SetColors(active ? Color(255, 255, 255, 36) : Colors::Transparent, Color(255, 255, 255, 24));
    button->SetTextColors(active ? kPaper : kOnDark, kPaper);
    button->SetBorder(0, Colors::Transparent);
    button->SetCornerRadius(6);
    button->SetTextAlign(TextAlignment::Left);
    button->SetPadding(0, 8);
    button->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(30));
    button->SetMouseCursor(UCMouseCursor::Hand);
    return button;
}

std::shared_ptr<UltraCanvasButton> SmallButton(const std::string& id, const std::string& text) {
    auto button = CreateButton(id, 0, 0, 28, 26, text);
    button->SetFont(kFont, 13);
    button->SetColors(Colors::Transparent, Color(255, 255, 255, 30));
    button->SetTextColors(kOnDark, kPaper);
    button->SetBorder(0, Colors::Transparent);
    button->SetCornerRadius(4);
    button->SetPadding(0, 4);
    button->SetElementSize(CSSLayout::Dimension::Px(28), CSSLayout::Dimension::Px(26));
    button->layoutItem.SetFlexShrink(0);
    button->SetMouseCursor(UCMouseCursor::Hand);
    return button;
}

std::shared_ptr<UltraCanvasContainer> Gap(float height) {
    auto spacer = CreateContainer("");
    spacer->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(height));
    return spacer;
}

// A default value for a property of a new block, so the page shows something.
JSONValue DefaultFor(const BlockType& type, const BlockProperty& property) {
    switch (property.kind) {
        case PropertyKind::Text:
            return property.name == "title" || property.name == "text" || property.name == "heading"
                       ? JSONValue(type.title) : JSONValue("");
        case PropertyKind::LongText: return JSONValue("Text of the " + type.title + " block.");
        case PropertyKind::Html: return JSONValue("<p>Text of the " + type.title + " block.</p>");
        case PropertyKind::Url:
        case PropertyKind::Image: return JSONValue("");
        case PropertyKind::Boolean: return JSONValue(false);
        case PropertyKind::Number: return JSONValue(property.name == "level" ? 2 : 32);
        case PropertyKind::Link: return JSONValue::MakeObject().Set("page", "home");
        case PropertyKind::Links: return JSONValue::MakeArray().Append(JSONValue::MakeObject().Set("title", "Learn more").Set("page", "home").Set("primary", true));
        case PropertyKind::Strings: return JSONValue::MakeArray().Append("The first point").Append("The second point");
        case PropertyKind::Items:
            if (type.name == "faq") return JSONValue::MakeArray().Append(JSONValue::MakeObject().Set("q", "A question?").Set("a", "The answer."));
            if (type.name == "form") return JSONValue::MakeArray().Append(JSONValue::MakeObject().Set("name", "email").Set("label", "E-mail").Set("type", "email").Set("required", true));
            if (type.name == "downloads") return JSONValue::MakeArray().Append(JSONValue::MakeObject().Set("title", "A file").Set("file", "media/file.zip"));
            return JSONValue::MakeArray();
    }
    return JSONValue();
}

} // namespace

EditorWindow::EditorWindow(Site siteModel, std::string siteDirectory)
    : site(std::move(siteModel)), siteDir(std::move(siteDirectory)) {}

EditorWindow::~EditorWindow() {
    if (publishWorker.joinable()) publishWorker.join();
}

Page* EditorWindow::CurrentPageMutable() {
    for (Page& page : site.pages) {
        if (page.slug == current) return &page;
    }
    return nullptr;
}

void EditorWindow::Later(std::function<void()> task) {
    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->PostToUIThread(std::move(task));
    else task();
}

void EditorWindow::SetStatus(const std::string& text) {
    if (status) status->SetText(text);
}

void EditorWindow::Build(const std::shared_ptr<UltraCanvasWindow>& window) {
    windowRef = window;
    auto frame = CreateContainer("Frame");
    frame->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Pct(100));
    frame->layout.SetFlexRow();

    side = CreateScrollableContainer("Side", 0, 0, 0, 0, true, false);
    side->SetBackgroundColor(kNight);
    side->SetElementSize(CSSLayout::Dimension::Px(270), CSSLayout::Dimension::Pct(100));
    side->layout.SetDisplay(CSSLayout::DisplayType::Block);
    side->SetPadding(16);
    side->layoutItem.SetFlexShrink(0);
    frame->AddChild(side);

    pageArea = CreateScrollableContainer("PageArea", 0, 0, 0, 0, true, false);
    pageArea->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Pct(100));
    pageArea->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0));
    {
        ContainerStyle style = pageArea->GetContainerStyle();
        style.autoShowHorizontalScrollbar = false;
        pageArea->SetContainerStyle(style);
    }
    pageArea->SetBackgroundColor(kPaper);
    pageArea->layout.SetDisplay(CSSLayout::DisplayType::Block);
    frame->AddChild(pageArea);

    properties = CreateScrollableContainer("Properties", 0, 0, 0, 0, true, false);
    properties->SetBackgroundColor(kPaperSoft);
    properties->SetElementSize(CSSLayout::Dimension::Px(340), CSSLayout::Dimension::Pct(100));
    properties->layout.SetDisplay(CSSLayout::DisplayType::Block);
    properties->SetPadding(16);
    properties->layoutItem.SetFlexShrink(0);
    frame->AddChild(properties);

    window->AddChild(frame);
    const std::string first = site.FindPage("home") ? "home" : site.pages.empty() ? "" : site.pages.front().slug;
    ShowPage(first);
}

void EditorWindow::ShowPage(const std::string& slug) {
    current = slug;
    selected = -1;
    RenderPreview();
    RebuildSide();
    RebuildProperties();
}

void EditorWindow::SelectBlock(int index) {
    const Page* page = CurrentPageMutable();
    selected = page && index >= 0 && index < static_cast<int>(page->blocks.size()) ? index : -1;
    RebuildSide();
    RebuildProperties();
}

void EditorWindow::AddBlock(const std::string& type) {
    Page* page = CurrentPageMutable();
    const BlockType* blockType = BlockRegistry::BuiltIn().Find(type);
    if (!page || !blockType) return;
    Block block{type};
    for (const BlockProperty& property : blockType->properties) {
        if (property.required || property.kind == PropertyKind::LongText || property.name == "heading" || property.name == "title") {
            block.Set(property.name, DefaultFor(*blockType, property));
        }
    }
    const int at = selected >= 0 ? selected + 1 : static_cast<int>(page->blocks.size());
    page->blocks.insert(page->blocks.begin() + at, block);
    dirty = true;
    selected = at;
    RenderPreview();
    RebuildSide();
    RebuildProperties();
    SetStatus("Added " + blockType->title);
}

void EditorWindow::MoveBlock(int index, int delta) {
    Page* page = CurrentPageMutable();
    if (!page) return;
    const int target = index + delta;
    if (index < 0 || index >= static_cast<int>(page->blocks.size()) || target < 0 || target >= static_cast<int>(page->blocks.size())) return;
    std::swap(page->blocks[index], page->blocks[target]);
    selected = target;
    dirty = true;
    RenderPreview();
    RebuildSide();
    RebuildProperties();
}

void EditorWindow::RemoveBlock(int index) {
    Page* page = CurrentPageMutable();
    if (!page || index < 0 || index >= static_cast<int>(page->blocks.size())) return;
    page->blocks.erase(page->blocks.begin() + index);
    if (selected >= static_cast<int>(page->blocks.size())) selected = static_cast<int>(page->blocks.size()) - 1;
    dirty = true;
    RenderPreview();
    RebuildSide();
    RebuildProperties();
}

bool EditorWindow::Save() {
    std::vector<std::string> errors;
    const bool ok = SaveSite(siteDir, site, errors);
    for (const std::string& error : errors) std::fprintf(stderr, "%s\n", error.c_str());
    if (ok) dirty = false;
    SetStatus(ok ? "Saved" : "Not saved: " + (errors.empty() ? std::string("unknown error") : errors.front()));
    return ok;
}

bool EditorWindow::Publish() {
#if ULTRAPRESS_HAS_ULTRACLOUD
    if (publishing) {
        SetStatus("Still uploading");
        return false;
    }
    if (dirty && !Save()) return false;
    if (!hosts) hosts = std::make_shared<Hosts>();
    std::string error;
    if (!hosts->Open(error)) {
        SetStatus(error);
        return false;
    }
    UltraCloud::Account host;
    if (site.hosting.account.empty()) {
        if (!hosts->Service().Accounts().GetDefault(host)) {
            ChooseHost();
            return false;
        }
        site.hosting.account = host.accountId;
        site.hosting.path = host.remoteFolder.empty() ? "/" : host.remoteFolder;
        dirty = true;
        if (!Save()) return false;
        RebuildSide();
    } else if (!hosts->Find(site.hosting.account, host)) {
        SetStatus("No host '" + site.hosting.account + "' on this computer: choose one");
        ChooseHost();
        return false;
    }
    const std::string out = PathToUtf8(PathFromUtf8(siteDir) / ".ultrapress" / "publish");
    const PublishResult written = PublishStatic(site, siteDir, out);
    for (const std::string& line : written.errors) std::fprintf(stderr, "%s\n", line.c_str());
    if (!written.ok) {
        SetStatus("Not published: " + (written.errors.empty() ? std::string("cannot write the site") : written.errors.front()));
        return false;
    }
    if (publishWorker.joinable()) publishWorker.join();
    publishing = true;
    SetStatus("Uploading to " + host.accountId + JoinRemote(site.hosting.path, "") + "\xE2\x80\xA6");
    const std::string accountId = host.accountId;
    const std::string root = site.hosting.path;
    std::shared_ptr<Hosts> theHosts = hosts;
    publishWorker = std::thread([this, out, accountId, root, theHosts]() {
        DeployOptions deploy;
        deploy.remoteRoot = root;
        deploy.prune = true;      // what the site no longer has goes, where the host can delete
        deploy.onProgress = [this](const std::string& relative, size_t index, size_t total) {
            Later([this, relative, index, total]() {
                SetStatus("Uploading " + std::to_string(index) + "/" + std::to_string(total) + ": " + relative);
            });
        };
        const DeployResult sent = DeployFolder(out, theHosts->UploaderFor(accountId),
                                               accountId + ":" + JoinRemote(root, ""), deploy);
        for (const std::string& line : sent.errors) std::fprintf(stderr, "%s\n", line.c_str());
        Later([this, sent, accountId]() {
            publishing = false;
            SetStatus(sent.ok ? "Published to " + accountId + ": " + sent.Summary()
                              : "Publish failed: " + sent.Summary() +
                                    (sent.errors.empty() ? std::string() : " - " + sent.errors.front()));
        });
    });
    return true;
#else
    SetStatus("This build has no UltraCloud: `ultrapress publish` writes the folder to upload yourself");
    return false;
#endif
}

void EditorWindow::ChooseHost() {
#if ULTRAPRESS_HAS_ULTRACLOUD_UI
    if (!hosts) hosts = std::make_shared<Hosts>();
    std::string error;
    if (!hosts->Open(error)) {
        SetStatus(error);
        return;
    }
    std::shared_ptr<UltraCanvasWindow> window = windowRef.lock();
    if (!window) return;
    UltraCloud::ShowAddAccountDialog(
        window.get(), hosts->Service(),
        [this](const UltraCloud::Account& added) {
            site.hosting.account = added.accountId;
            site.hosting.path = added.remoteFolder.empty() ? "/" : added.remoteFolder;
            dirty = true;
            Save();
            Later([this]() { RebuildSide(); });
            SetStatus("Publishes to " + site.hosting.account + JoinRemote(site.hosting.path, ""));
        },
        [](const std::string& providerId) { return Hosts::IsHostProvider(providerId); },
        "Add a web host");
#else
    SetStatus("This build has no host dialog: `ultrapress hosts add` and `ultrapress hosts use`");
#endif
}

std::vector<MediaFile> EditorWindow::Media() const {
    return ListMedia(siteDir, &site);
}

bool EditorWindow::AddMedia(const std::string& sourcePath, std::string& mediaPath, std::string& error) {
    if (!ImportMedia(siteDir, sourcePath, mediaPath, error)) return false;
    SetStatus("Added " + mediaPath);
    return true;
}

bool EditorWindow::RemoveMediaFile(const std::string& mediaPath, std::string& error) {
    if (!RemoveMedia(siteDir, mediaPath, error, &site)) return false;
    SetStatus("Removed " + mediaPath);
    RenderPreview();
    return true;
}

bool EditorWindow::SetProperty(const std::string& name, JSONValue value) {
    Page* page = CurrentPageMutable();
    if (!page || selected < 0 || selected >= static_cast<int>(page->blocks.size())) return false;
    page->blocks[static_cast<size_t>(selected)].Set(name, std::move(value));
    dirty = true;
    RenderPreview();
    return true;
}

void EditorWindow::ShowMediaLibrary() {
    std::shared_ptr<UltraCanvasWindow> window = windowRef.lock();
    if (!window) return;
    UltraPress::ShowMediaLibrary(window.get(), siteDir, site, false, nullptr, [this]() { Later([this]() { RenderPreview(); }); });
}

void EditorWindow::PickImage(const std::string& name) {
    std::shared_ptr<UltraCanvasWindow> window = windowRef.lock();
    if (!window) return;
    const Page* page = CurrentPageMutable();
    const std::string current = page && selected >= 0 && selected < static_cast<int>(page->blocks.size())
                                    ? page->blocks[static_cast<size_t>(selected)].Text(name) : std::string();
    UltraPress::ShowMediaLibrary(
        window.get(), siteDir, site, true,
        [this, name](const std::string& mediaPath) {
            Later([this, name, mediaPath]() {
                SetProperty(name, mediaPath);
                RebuildProperties();
                SetStatus(name + " = " + mediaPath);
            });
        },
        [this]() { Later([this]() { RenderPreview(); }); }, current);
}

void EditorWindow::RenderPreview() {
    // A field still open: its text goes into the block first, and that
    // commit renders the fresh page itself.
    if (editing >= 0 && CommitTextEdit()) return;
    // The page is rendered without a window too (the tests edit it), only
    // not shown.
    if (pageArea) pageArea->ClearChildren();
    const Page* page = CurrentPageMutable();
    if (!page) {
        shown = RenderedPage();
        return;
    }
    RenderOptions options;
    options.mediaRoot = siteDir;
    options.editable = true;
    options.onEditText = [this](int binding) { Later([this, binding]() { BeginTextEdit(binding); }); };
    options.onLink = [this](const std::string& href) {
        if (!href.empty() && href[0] == '/') {
            const std::string target = href == "/" ? "home" : href.substr(1, href.size() - 2);
            Later([this, target]() { ShowPage(target); });
        } else {
            OpenURL(href);
        }
    };
    options.onFormSubmit = [this](const std::string& id, const std::vector<std::pair<std::string, std::string>>& fields) {
        SetStatus("Form " + id + ": " + std::to_string(fields.size()) + " fields (preview; no server tier yet)");
    };
    options.onNewsletterSubscribe = [this](const std::string& list, const std::string&) {
        SetStatus("Newsletter " + list + " (preview; no server tier yet)");
    };
    PageRenderer renderer(site, options);
    shown = renderer.Render(*page);
    if (pageArea) pageArea->AddChild(shown.root);
}

void EditorWindow::BeginTextEdit(int binding) {
    if (editing >= 0) CommitTextEdit();
    if (binding < 0 || binding >= static_cast<int>(shown.bindings.size())) return;
    const TextBinding& bound = shown.bindings[static_cast<size_t>(binding)];
    if (!bound.slot || !bound.element) return;
    // The block the text belongs to becomes the selected one - the lists on
    // both sides follow - without re-rendering the page under the field.
    const size_t close = bound.blockPath.find(']');
    const int index = close == std::string::npos ? -1 : std::atoi(bound.blockPath.substr(7, close - 7).c_str());   // locale-ok: digits
    if (index >= 0 && index != selected) {
        selected = index;
        RebuildSide();
        RebuildProperties();
    }
    editing = binding;
    bound.slot->RemoveChild(bound.element);
    if (bound.rich) {
        // The handle under the content goes too while the editor is open.
        editHandle.reset();
        if (bound.slot->GetChildren().size() == 1) {
            editHandle = bound.slot->GetChildren()[0];
            bound.slot->RemoveChild(editHandle);
        }
        // The block's HTML as a rich document; its pictures come from the
        // site folder, and go back out as the paths they came in with.
        editPictures.clear();
        HTMLRichImportOptions import;
        import.resolveImage = [this](const std::string& src, HTMLRichImportImage& out) {
            if (src.empty() || src.find("://") != std::string::npos) return false;
            const std::string relative = src[0] == '/' ? src.substr(1) : src;
            std::ifstream in(PathFromUtf8(siteDir) / PathFromUtf8(relative), std::ios::binary);
            if (!in) return false;
            out.data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            if (out.data.empty()) return false;
            editPictures.push_back(src);
            return true;
        };
        auto document = std::make_shared<UCRichDocument>(ImportHTMLToRichDocument(bound.text, import));
        editRich = CreateRichTextEdit("EditRich", 0, 0, 0, 320);
        editRich->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(320));
        editRich->SetDocument(document);
        bound.slot->AddChild(editRich);
        editButtons = CreateContainer("EditRichButtons", 0, 0, 0, 36);
        editButtons->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
        editButtons->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(36));
        auto done = CreateButton("EditRichDone", 0, 0, 90, 30, "Done");
        done->SetFont(kFont, 13, FontWeight::Bold);
        done->SetColors(kAccent, Color(130, 5, 5, 255));
        done->SetTextColors(kPaper);
        done->SetBorder(0, Colors::Transparent);
        done->SetCornerRadius(6);
        done->SetElementSize(CSSLayout::Dimension::Px(90), CSSLayout::Dimension::Px(30));
        done->onClick = [this]() { Later([this]() { CommitTextEdit(); }); };
        auto cancel = CreateButton("EditRichCancel", 0, 0, 90, 30, "Cancel");
        cancel->SetFont(kFont, 13);
        cancel->SetColors(kPaperSoft, kRule);
        cancel->SetTextColors(kInk);
        cancel->SetBorder(1, kRule);
        cancel->SetCornerRadius(6);
        cancel->SetElementSize(CSSLayout::Dimension::Px(90), CSSLayout::Dimension::Px(30));
        cancel->onClick = [this]() { Later([this]() { CancelTextEdit(); }); };
        editButtons->AddChild(done);
        editButtons->AddChild(cancel);
        bound.slot->AddChild(editButtons);
        editRich->SetFocus(true);
        SetStatus("Editing rich text; Done keeps it, Cancel drops the change");
    } else if (bound.multiline) {
        const float height = std::max(120.f, bound.size * 1.5f * 4);
        editArea = std::make_shared<UltraCanvasTextArea>("EditArea", 0, 0, 0, height);
        editArea->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(height));
        editArea->SetFont(kFont, bound.size);
        editArea->SetWordWrap(true);
        editArea->SetText(bound.text, false);
        editArea->onBeforeKeyDown = [this](const UCEvent& event) {
            if (event.virtualKey == UCKeys::Escape) { Later([this]() { CancelTextEdit(); }); return true; }
            if (event.virtualKey == UCKeys::Return && event.ctrl) { Later([this]() { CommitTextEdit(); }); return true; }
            return false;
        };
        editArea->onFocusLost = [this]() { Later([this]() { if (editing >= 0 && editArea) CommitTextEdit(); }); };
        bound.slot->AddChild(editArea);
        editArea->SetFocus(true);
        SetStatus("Editing; Ctrl+Enter keeps the text, Escape drops the change");
    } else {
        editInput = CreateTextInput("EditInput", 0, 0, 0, bound.size * 1.8f);
        editInput->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(bound.size * 1.8f));
        editInput->SetFontSize(bound.size);
        editInput->SetText(bound.text);
        editInput->onEnterPressed = [this](const std::string&) { Later([this]() { CommitTextEdit(); }); return true; };
        editInput->onEscapePressed = [this]() { Later([this]() { CancelTextEdit(); }); return true; };
        editInput->onFocusLost = [this]() { Later([this]() { if (editing >= 0) CommitTextEdit(); }); };
        bound.slot->AddChild(editInput);
        editInput->SetFocus(true);
        SetStatus("Editing; Enter keeps the text, Escape drops the change");
    }
}

bool EditorWindow::CommitTextEdit() {
    if (editing < 0) return false;
    std::string text;
    if (editRich) {
        RichDocumentHTMLOptions out;
        out.imageSource = [this](int mediaIndex) {
            return mediaIndex >= 0 && mediaIndex < static_cast<int>(editPictures.size()) ? editPictures[static_cast<size_t>(mediaIndex)]
                                                                                         : std::string();
        };
        text = editRich->GetDocument()->ToHTML(out);
    } else if (editArea) {
        text = editArea->GetText();
    } else if (editInput) {
        text = editInput->GetText();
    }
    return CommitTextEdit(text);
}

bool EditorWindow::CommitTextEdit(const std::string& text) {
    if (editing < 0 || editing >= static_cast<int>(shown.bindings.size())) return false;
    const TextBinding bound = shown.bindings[static_cast<size_t>(editing)];
    if (text == bound.text) {
        CancelTextEdit();   // nothing changed: the label comes back as it was
        return false;
    }
    Page* page = CurrentPageMutable();
    Block* block = page ? ResolveBlockPath(*page, bound.blockPath) : nullptr;
    if (!block || !SetBlockText(*block, bound.where, text)) {
        SetStatus("The text could not be written to " + bound.blockPath);
        CancelTextEdit();
        return false;
    }
    dirty = true;
    editing = -1;
    editInput.reset();
    editArea.reset();
    editRich.reset();
    editButtons.reset();
    editHandle.reset();
    RenderPreview();
    RebuildSide();
    RebuildProperties();
    SetStatus("Text changed");
    return true;
}

void EditorWindow::CancelTextEdit() {
    if (editing < 0) return;
    const TextBinding& bound = shown.bindings[static_cast<size_t>(editing)];
    editing = -1;
    if (bound.slot) {
        if (editInput) bound.slot->RemoveChild(editInput);
        if (editArea) bound.slot->RemoveChild(editArea);
        if (editRich) bound.slot->RemoveChild(editRich);
        if (editButtons) bound.slot->RemoveChild(editButtons);
        if (bound.element) bound.slot->AddChild(bound.element);
        if (editHandle) bound.slot->AddChild(editHandle);   // after the content, as rendered
    }
    editInput.reset();
    editArea.reset();
    editRich.reset();
    editButtons.reset();
    editHandle.reset();
    SetStatus(dirty ? "Unsaved changes" : "");
}

void EditorWindow::RebuildSide() {
    if (!side) return;
    side->ClearChildren();
    auto title = Caption("SideTitle", site.name, kPaper, 18);
    side->AddChild(title);
    status = Caption("SideStatus", dirty ? "Unsaved changes" : "", kOnDark, 12, false);
    side->AddChild(status);
    side->AddChild(Gap(10));
    auto save = CreateButton("Save", 0, 0, 0, 32, "Save site");
    save->SetFont(kFont, 14, FontWeight::Bold);
    save->SetColors(kAccent, Color(130, 5, 5, 255));
    save->SetTextColors(kPaper);
    save->SetBorder(0, Colors::Transparent);
    save->SetCornerRadius(6);
    save->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(32));
    save->onClick = [this]() { Save(); };
    side->AddChild(save);
    side->AddChild(Gap(6));
    auto publish = CreateButton("Publish", 0, 0, 0, 32, "Publish");
    publish->SetFont(kFont, 14, FontWeight::Bold);
    publish->SetColors(Color(255, 255, 255, 36), Color(255, 255, 255, 60));
    publish->SetTextColors(kPaper);
    publish->SetBorder(0, Colors::Transparent);
    publish->SetCornerRadius(6);
    publish->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(32));
    publish->onClick = [this]() { Later([this]() { Publish(); }); };
    side->AddChild(publish);
    auto host = SideButton("Host", site.hosting.account.empty()
                                       ? std::string("Host\xE2\x80\xA6 (none yet)")
                                       : "Host\xE2\x80\xA6 " + site.hosting.account + JoinRemote(site.hosting.path, ""),
                           false);
    host->SetFont(kFont, 12);
    host->onClick = [this]() { Later([this]() { ChooseHost(); }); };
    side->AddChild(host);
    auto media = SideButton("MediaLibrary", "Media library\xE2\x80\xA6", false);
    media->SetFont(kFont, 12);
    media->onClick = [this]() { Later([this]() { ShowMediaLibrary(); }); };
    side->AddChild(media);

    side->AddChild(Gap(18));
    side->AddChild(Caption("PagesTitle", "PAGES", kAccent));
    side->AddChild(Gap(6));
    for (const Page* page : SortedPages(site, PageOrder::Stored)) {
        auto button = SideButton("Page" + page->slug, (page->pinned ? "\xE2\x98\x85 " : "") + page->title +
                                                           (page->IsPublished() ? "" : " (" + page->status + ")"),
                                 page->slug == current);
        const std::string slug = page->slug;
        button->onClick = [this, slug]() { Later([this, slug]() { ShowPage(slug); }); };
        side->AddChild(button);
    }

    const Page* page = CurrentPageMutable();
    if (!page) return;
    side->AddChild(Gap(18));
    side->AddChild(Caption("BlocksTitle", "BLOCKS OF " + page->title, kAccent));
    side->AddChild(Gap(6));
    for (size_t i = 0; i < page->blocks.size(); ++i) {
        const Block& block = page->blocks[i];
        const BlockType* type = BlockRegistry::BuiltIn().Find(block.type);
        std::string label = type ? type->title : block.type;
        std::string detail = block.Text("title", block.Text("heading", block.Text("text")));
        if (detail.size() > 28) detail = detail.substr(0, 27) + "\xE2\x80\xA6";
        if (!detail.empty()) label += ": " + detail;
        auto row = CreateContainer("BlockRow" + std::to_string(i));
        row->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
        row->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Center).SetFlexGap(2);
        auto button = SideButton("Block" + std::to_string(i), label, static_cast<int>(i) == selected);
        button->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(30));
        button->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0));
        const int index = static_cast<int>(i);
        button->onClick = [this, index]() { Later([this, index]() { SelectBlock(index); }); };
        row->AddChild(button);
        auto up = SmallButton("Up" + std::to_string(i), "\xE2\x96\xB2");
        up->onClick = [this, index]() { Later([this, index]() { MoveBlock(index, -1); }); };
        row->AddChild(up);
        auto down = SmallButton("Down" + std::to_string(i), "\xE2\x96\xBC");
        down->onClick = [this, index]() { Later([this, index]() { MoveBlock(index, 1); }); };
        row->AddChild(down);
        auto remove = SmallButton("Remove" + std::to_string(i), "\xE2\x9C\x95");
        remove->onClick = [this, index]() { Later([this, index]() { RemoveBlock(index); }); };
        row->AddChild(remove);
        side->AddChild(row);
    }
    side->AddChild(Gap(8));
    auto add = CreateDropdown("AddBlock", 0, 0, 0, 30);
    add->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(30));
    add->AddItem("+ Add a block\xE2\x80\xA6", "");
    for (const BlockType& type : BlockRegistry::BuiltIn().All()) add->AddItem(type.title, type.name);
    add->SetSelectedIndex(0, false);
    add->onSelectionChanged = [this](int, const DropdownItem& item) {
        const std::string type = item.value;
        if (type.empty()) return;
        Later([this, type]() { AddBlock(type); });
    };
    side->AddChild(add);
}

void EditorWindow::RebuildProperties() {
    if (!properties) return;
    properties->ClearChildren();
    Page* page = CurrentPageMutable();
    if (!page) return;
    auto field = [&](const std::string& id, const std::string& caption) {
        properties->AddChild(Caption(id + "Caption", caption, kInk, 13));
        properties->AddChild(Gap(4));
    };
    auto input = [&](const std::string& id, const std::string& value, std::function<void(const std::string&)> onChange) {
        auto edit = CreateTextInput(id, 0, 0, 0, 32);
        edit->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(32));
        edit->SetFontSize(14);
        edit->SetText(value);
        edit->onTextChanged = std::move(onChange);
        properties->AddChild(edit);
        properties->AddChild(Gap(10));
    };
    auto area = [&](const std::string& id, const std::string& value, float height, std::function<void(const std::string&)> onChange) {
        auto edit = std::make_shared<UltraCanvasTextArea>(id, 0, 0, 0, height);
        edit->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(height));
        edit->SetFontSize(14);
        edit->SetWordWrap(true);
        edit->SetText(value, false);
        edit->SetOnTextChanged(std::move(onChange));
        properties->AddChild(edit);
        properties->AddChild(Gap(10));
    };

    if (selected < 0 || selected >= static_cast<int>(page->blocks.size())) {
        properties->AddChild(Caption("PropsTitle", "PAGE", kAccent));
        properties->AddChild(Gap(10));
        field("PageTitle", "Title");
        input("PageTitle", page->title, [this](const std::string& text) {
            if (Page* p = CurrentPageMutable()) { p->title = text; dirty = true; }
            Later([this]() { RenderPreview(); });
        });
        field("PageDescription", "Description (search engines and link previews)");
        area("PageDescription", page->seo.description, 80, [this](const std::string& text) {
            if (Page* p = CurrentPageMutable()) { p->seo.description = text; dirty = true; }
        });
        field("PageStatus", "Status");
        auto statusPick = CreateDropdown("PageStatus", 0, 0, 0, 30);
        statusPick->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(30));
        const char* kStatuses[] = {"published", "draft", "scheduled"};
        for (int i = 0; i < 3; ++i) {
            statusPick->AddItem(kStatuses[i], kStatuses[i]);
            if (page->status == kStatuses[i]) statusPick->SetSelectedIndex(i, false);
        }
        statusPick->onSelectionChanged = [this](int, const DropdownItem& item) {
            if (Page* p = CurrentPageMutable()) { p->status = item.value; dirty = true; }
            Later([this]() { RebuildSide(); });
        };
        properties->AddChild(statusPick);
        properties->AddChild(Gap(10));
        auto pinned = UltraCanvasCheckbox::CreateCheckbox("PagePinned", 0, 0, 0, 28, "Pinned (first in the page list)", page->pinned);
        pinned->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(28));
        pinned->onStateChanged = [this, box = pinned.get()](CheckedState, CheckedState) {
            if (Page* p = CurrentPageMutable()) { p->pinned = box->IsChecked(); dirty = true; }
            Later([this]() { RebuildSide(); });
        };
        properties->AddChild(pinned);
        properties->AddChild(Gap(16));
        properties->AddChild(Caption("PropsHint", "Pick a block on the left to edit its properties.", kInkSoft, 13, false));
        return;
    }

    Block& block = page->blocks[selected];
    const BlockType* type = BlockRegistry::BuiltIn().Find(block.type);
    properties->AddChild(Caption("PropsTitle", type ? type->title : block.type, kAccent, 16));
    if (type) properties->AddChild(Caption("PropsDescription", type->description, kInkSoft, 12, false));
    properties->AddChild(Gap(12));
    if (!type) {
        properties->AddChild(Caption("PropsUnknown", "This block type is not in the registry; the page skips it.", kInkSoft, 13, false));
        return;
    }
    const int index = selected;
    auto setProperty = [this, index](const std::string& name, JSONValue value) {
        Page* p = CurrentPageMutable();
        if (!p || index >= static_cast<int>(p->blocks.size())) return;
        p->blocks[index].Set(name, std::move(value));
        dirty = true;
        Later([this]() { RenderPreview(); });
    };
    for (const BlockProperty& property : type->properties) {
        const std::string id = "Prop" + property.name;
        const JSONValue& value = block.props[property.name];
        std::string caption = property.name + (property.required ? " *" : "");
        if (!property.description.empty()) caption += " - " + property.description;
        field(id, caption);
        const std::string name = property.name;
        switch (property.kind) {
            case PropertyKind::Text:
            case PropertyKind::Url:
                input(id, value.GetString(), [setProperty, name](const std::string& text) { setProperty(name, text); });
                break;
            case PropertyKind::Image: {
                // The path, typed or picked from the media library.
                auto row = CreateContainer(id + "Row", 0, 0, 0, 32);
                row->layout.SetFlexRow().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Center);
                row->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(32));
                auto edit = CreateTextInput(id, 0, 0, 0, 32);
                edit->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(32));
                edit->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
                edit->SetFontSize(14);
                edit->SetText(value.GetString());
                edit->onTextChanged = [setProperty, name](const std::string& text) { setProperty(name, text); };
                row->AddChild(edit);
                auto pick = CreateButton(id + "Pick", 0, 0, 70, 32, "Pick\xE2\x80\xA6");
                pick->SetFont(kFont, 13);
                pick->SetColors(kPaperSoft, kRule);
                pick->SetTextColors(kInk);
                pick->SetBorder(1, kRule);
                pick->SetCornerRadius(6);
                pick->SetElementSize(CSSLayout::Dimension::Px(70), CSSLayout::Dimension::Px(32));
                pick->layoutItem.SetFlexShrink(0);
                pick->SetMouseCursor(UCMouseCursor::Hand);
                pick->onClick = [this, name]() { Later([this, name]() { PickImage(name); }); };
                row->AddChild(pick);
                properties->AddChild(row);
                properties->AddChild(Gap(10));
                break;
            }
            case PropertyKind::LongText:
                area(id, value.GetString(), 120, [setProperty, name](const std::string& text) { setProperty(name, text); });
                break;
            case PropertyKind::Html:
                area(id, value.GetString(), 160, [setProperty, name](const std::string& text) { setProperty(name, text); });
                break;
            case PropertyKind::Boolean: {
                auto box = UltraCanvasCheckbox::CreateCheckbox(id, 0, 0, 0, 28, property.name, value.GetBoolean(false));
                box->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(28));
                box->onStateChanged = [setProperty, name, raw = box.get()](CheckedState, CheckedState) { setProperty(name, raw->IsChecked()); };
                properties->AddChild(box);
                properties->AddChild(Gap(10));
                break;
            }
            case PropertyKind::Number:
                input(id, std::to_string(static_cast<long long>(value.GetNumber(0))), [setProperty, name](const std::string& text) {
                    char* end = nullptr;
                    const long long number = std::strtoll(text.c_str(), &end, 10);   // locale-ok: an integer property, digits only
                    if (end && *end == '\0' && !text.empty()) setProperty(name, static_cast<int64_t>(number));
                });
                break;
            case PropertyKind::Link:
            case PropertyKind::Links:
            case PropertyKind::Strings:
            case PropertyKind::Items: {
                JSONSerializeOptions pretty;
                pretty.pretty = true;
                pretty.indentWidth = 2;
                const std::string json = value.IsNull() ? std::string(property.kind == PropertyKind::Link ? "{}" : "[]")
                                                        : JSON::Serialize(value, pretty);
                area(id, json, 150, [this, setProperty, name, kind = property.kind](const std::string& text) {
                    JSONParseResult result;
                    JSONValue parsed = JSON::Parse(text, &result);
                    const bool shape = kind == PropertyKind::Link ? parsed.IsObject() : parsed.IsArray();
                    if (!result.success || !shape) {
                        SetStatus(name + ": not valid JSON yet" + (result.success ? "" : " (" + result.errorMessage + ")"));
                        return;
                    }
                    SetStatus("");
                    setProperty(name, parsed);
                });
                break;
            }
        }
    }
}

} // namespace UltraPress
