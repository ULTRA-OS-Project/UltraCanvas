// Apps/DemoApp/UltraCanvasWYSIWYGExamples.cpp
// The WYSIWYG editing element, UltraCanvasRichTextEdit: the caret sits in
// rendered text and bold is a state of the selection rather than two asterisks
// in a buffer.
//
// The page is a small word processor. Its toolbars are built from real
// UltraCanvas elements (UltraCanvasToolbar / UltraCanvasButton /
// UltraCanvasDropdown) because the element deliberately draws no chrome of its
// own, and they are driven from GetFormatState(), which reports every
// character attribute as on, off or *mixed* across the selection.
//
// The sample document is assembled as a UCRichDocument rather than from
// Markdown on purpose: the 14 pt Georgia run in red, the centred heading and
// the table are exactly the formatting Markdown cannot spell, and they are the
// reason this element exists next to UltraCanvasTextArea.
//
// Version: 1.0.0
// Last Modified: 2026-09-16
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasRichTextEdit.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasConfig.h"   // GetResourcesDir
#include "UltraCanvasUtils.h"    // NormalizePath, OpenURL
#include "Plugins/Documents/Word/UltraCanvasWordDocumentIO.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

    namespace {

        // ===== PAGE STATE =====
        // Everything here is owned by the page container (the editor, the
        // toolbars and through them their widgets). This struct therefore holds
        // RAW back-pointers: the toolbar callbacks capture it, and the editor's
        // callbacks capture it too, so owning the widgets here as well would
        // close a shared_ptr cycle that never frees the page. The pointers stay
        // valid for exactly as long as callbacks can fire — the lifetime of the
        // container that owns every one of them.
        struct WysiwygPage {
            UltraCanvasRichTextEdit* edit = nullptr;

            UltraCanvasButton* bold = nullptr;
            UltraCanvasButton* italic = nullptr;
            UltraCanvasButton* underline = nullptr;
            UltraCanvasButton* strikethrough = nullptr;
            UltraCanvasButton* inlineCode = nullptr;
            UltraCanvasButton* subscript = nullptr;
            UltraCanvasButton* superscript = nullptr;

            UltraCanvasDropdown* fontBox = nullptr;
            UltraCanvasDropdown* sizeBox = nullptr;
            UltraCanvasDropdown* colorBox = nullptr;
            UltraCanvasDropdown* styleBox = nullptr;
            UltraCanvasDropdown* alignBox = nullptr;

            UltraCanvasLabel* status = nullptr;

            // The font sizes and colours the two dropdowns offer, in item order.
            // Index 0 of each is "inherit", which is what a run with no font
            // size / colour of its own means.
            std::vector<float> sizes;
            std::vector<std::string> colors;
            std::vector<std::string> fonts;

            void Sync();
            void SetStatus(const std::string& text);
        };

        void WysiwygPage::SetStatus(const std::string& text) {
            if (!status) return;
            status->SetText(text);
            status->RequestRedraw();
        }

        // Reflects the caret's formatting back into the toolbars. Called after
        // every selection change, every document change and every toolbar
        // action — arming a format at a collapsed caret mutates nothing, so it
        // raises no change notification of its own.
        void WysiwygPage::Sync() {
            if (!edit) return;
            const RichCharFormatState state = edit->GetFormatState();

            auto setToggle = [](UltraCanvasButton* button, RichCharFormatState::Tri value) {
                if (button) button->SetPressed(RichCharFormatState::IsOn(value));
            };
            setToggle(bold, state.bold);
            setToggle(italic, state.italic);
            setToggle(underline, state.underline);
            setToggle(strikethrough, state.strikethrough);
            setToggle(inlineCode, state.code);
            setToggle(subscript, state.subscript);
            setToggle(superscript, state.superscript);

            // A mixed selection selects nothing rather than picking a side.
            if (fontBox) {
                int index = state.fontFamilyMixed ? -1 : 0;
                if (!state.fontFamilyMixed) {
                    for (size_t i = 1; i < fonts.size(); i++) {
                        if (fonts[i] == state.fontFamily) { index = static_cast<int>(i); break; }
                    }
                }
                fontBox->SetSelectedIndex(index, false);
            }
            if (sizeBox) {
                int index = state.fontSizeMixed ? -1 : 0;
                if (!state.fontSizeMixed) {
                    for (size_t i = 1; i < sizes.size(); i++) {
                        if (sizes[i] == state.fontSizePt) { index = static_cast<int>(i); break; }
                    }
                }
                sizeBox->SetSelectedIndex(index, false);
            }
            if (colorBox) {
                int index = state.colorMixed ? -1 : 0;
                if (!state.colorMixed) {
                    for (size_t i = 1; i < colors.size(); i++) {
                        if (colors[i] == state.color) { index = static_cast<int>(i); break; }
                    }
                }
                colorBox->SetSelectedIndex(index, false);
            }

            if (styleBox) {
                // 0 = body text, 1..4 = Heading 1..4, 5 = code block, 6 = quote.
                int index = 0;
                switch (edit->GetCurrentBlockType()) {
                    case RichBlockType::Heading: {
                        int level = edit->GetCurrentHeadingLevel();
                        index = (level >= 1 && level <= 4) ? level : 0;
                        break;
                    }
                    case RichBlockType::CodeBlock:  index = 5; break;
                    case RichBlockType::BlockQuote: index = 6; break;
                    default: break;
                }
                styleBox->SetSelectedIndex(index, false);
            }

            if (alignBox) {
                const UCRichDocumentEditor& core = edit->GetEditor();
                int block = core.GetCaret().blockIndex;
                RichTextAlign align = RichTextAlign::Default;
                if (block >= 0 && block < core.GetBlockCount()) align = core.GetBlock(block).align;
                int index = 0;
                switch (align) {
                    case RichTextAlign::Center:  index = 1; break;
                    case RichTextAlign::Right:   index = 2; break;
                    case RichTextAlign::Justify: index = 3; break;
                    default:                     index = 0; break;   // Default reads as Left
                }
                alignBox->SetSelectedIndex(index, false);
            }

            const UCRichDocumentEditor& core = edit->GetEditor();
            std::string summary = std::to_string(core.GetBlockCount()) + " blocks";
            if (edit->HasSelection()) {
                summary += ", selection: " + std::to_string(core.RangeToPlainText(
                        core.GetSelectionRange()).size()) + " bytes";
            } else {
                summary += ", caret in block " + std::to_string(core.GetCaret().blockIndex + 1);
            }
            if (state.bold == RichCharFormatState::Tri::Mixed
                || state.italic == RichCharFormatState::Tri::Mixed
                || state.fontSizeMixed || state.colorMixed || state.fontFamilyMixed) {
                summary += " — mixed formatting";
            }
            summary += edit->IsModified() ? " • modified" : " • unmodified";
            SetStatus(summary);
        }

        // ===== SAMPLE DOCUMENT =====

        RichTextRun MakeRun(const std::string& text) {
            RichTextRun run;
            run.text = text;
            return run;
        }

        RichDocBlock MakeParagraph(std::vector<RichTextRun> runs,
                                   RichTextAlign align = RichTextAlign::Default) {
            RichDocBlock block;
            block.type = RichBlockType::Paragraph;
            block.runs = std::move(runs);
            block.align = align;
            return block;
        }

        RichDocBlock MakeHeading(int level, const std::string& text,
                                 RichTextAlign align = RichTextAlign::Default) {
            RichDocBlock block;
            block.type = RichBlockType::Heading;
            block.headingLevel = level;
            block.runs.push_back(MakeRun(text));
            block.align = align;
            return block;
        }

        RichDocBlock MakeListItem(const std::string& text, bool ordered, int level = 0) {
            RichDocBlock block;
            block.type = RichBlockType::ListItem;
            block.orderedList = ordered;
            block.listLevel = level;
            block.runs.push_back(MakeRun(text));
            return block;
        }

        RichTableCell MakeCell(const std::string& text, bool bold = false) {
            RichTableCell cell;
            RichTextRun run = MakeRun(text);
            run.bold = bold;
            cell.runs.push_back(run);
            return cell;
        }

        // A document that exercises what the element can do — and, in its
        // second paragraph, exactly the attributes a Markdown buffer cannot
        // carry: a family, a point size and a colour on a single run.
        std::shared_ptr<UCRichDocument> BuildSampleDocument() {
            auto document = std::make_shared<UCRichDocument>();
            document->metadata.title = "UltraCanvas WYSIWYG sample";
            document->metadata.author = "UltraCanvas Framework";

            document->blocks.push_back(MakeHeading(1, "Quarterly Report", RichTextAlign::Center));

            {
                RichTextRun subtitle = MakeRun("Prepared with UltraCanvasRichTextEdit");
                subtitle.italic = true;
                subtitle.color = "#666666";
                document->blocks.push_back(MakeParagraph({subtitle}, RichTextAlign::Center));
            }

            {
                std::vector<RichTextRun> runs;
                runs.push_back(MakeRun("Type anywhere in this page. "));

                RichTextRun bold = MakeRun("Bold");
                bold.bold = true;
                runs.push_back(bold);
                runs.push_back(MakeRun(", "));

                RichTextRun italic = MakeRun("italic");
                italic.italic = true;
                runs.push_back(italic);
                runs.push_back(MakeRun(" and "));

                RichTextRun underlined = MakeRun("underlined");
                underlined.underline = true;
                runs.push_back(underlined);
                runs.push_back(MakeRun(" text are states of the runs they cover, not markup — and so are "));

                // The point of the element, in one run.
                RichTextRun styled = MakeRun("14 pt Georgia in red");
                styled.fontFamily = "Georgia";
                styled.fontSizePt = 14.0f;
                styled.color = "#CC0000";
                runs.push_back(styled);
                runs.push_back(MakeRun(", which no Markdown buffer can spell. Formatting survives a "
                                       "round trip through .odt and .docx because the document is a "
                                       "UCRichDocument, the same model the ODT, DOCX and LaTeX "
                                       "readers produce."));
                document->blocks.push_back(MakeParagraph(std::move(runs)));
            }

            document->blocks.push_back(MakeHeading(2, "What the toolbars drive"));

            document->blocks.push_back(MakeListItem("Character formatting applies to the selection, "
                                                    "or arms itself for the next typed character.", false));
            document->blocks.push_back(MakeListItem("Paragraph formatting applies to every block the "
                                                    "selection touches.", false));
            document->blocks.push_back(MakeListItem("A selection spanning bold and plain text reports "
                                                    "as mixed — try it and watch the status line.",
                                                    false, 1));

            {
                RichDocBlock quote;
                quote.type = RichBlockType::BlockQuote;
                quote.runs.push_back(MakeRun("Undo records the blocks an edit replaced rather than the "
                                             "whole document, so its cost is the edit — and the "
                                             "embedded pictures are never copied."));
                document->blocks.push_back(quote);
            }

            document->blocks.push_back(MakeHeading(2, "Ordered list"));
            document->blocks.push_back(MakeListItem("Press Enter to continue a list.", true));
            document->blocks.push_back(MakeListItem("Press Tab to nest, Shift+Tab to come back out.", true));
            document->blocks.push_back(MakeListItem("Press Enter on an empty item to leave the list.", true));

            document->blocks.push_back(MakeHeading(2, "Code block"));
            {
                RichDocBlock code;
                code.type = RichBlockType::CodeBlock;
                code.codeLanguage = "cpp";
                code.runs.push_back(MakeRun("auto editor = CreateRichTextEdit(\"editor\", 0, 0, 800, 600);"));
                RichTextRun second = MakeRun("editor->ToggleBold();");
                second.lineBreakBefore = true;
                code.runs.push_back(second);
                document->blocks.push_back(code);
            }

            document->blocks.push_back(MakeHeading(2, "Table"));
            {
                RichDocBlock table;
                table.type = RichBlockType::Table;

                RichTableRow header;
                header.header = true;
                header.cells.push_back(MakeCell("Editing", true));
                header.cells.push_back(MakeCell("Element", true));
                table.tableRows.push_back(header);

                RichTableRow first;
                first.cells.push_back(MakeCell("Source code, logs, plain text"));
                first.cells.push_back(MakeCell("UltraCanvasTextArea (PlainText)"));
                table.tableRows.push_back(first);

                RichTableRow second;
                second.cells.push_back(MakeCell("Markdown with a live preview"));
                second.cells.push_back(MakeCell("UltraCanvasTextArea (MarkdownHybrid)"));
                table.tableRows.push_back(second);

                RichTableRow third;
                third.cells.push_back(MakeCell("A word-processing document"));
                third.cells.push_back(MakeCell("UltraCanvasRichTextEdit"));
                table.tableRows.push_back(third);

                document->blocks.push_back(table);
            }

            {
                RichDocBlock rule;
                rule.type = RichBlockType::HorizontalRule;
                document->blocks.push_back(rule);
            }

            document->blocks.push_back(MakeHeading(2, "Pictures and links"));

            // The framework logo, embedded in the document's media store so the
            // document stays self-contained when it is saved.
            const std::string logoPath = NormalizePath(GetResourcesDir() + "media/images/UltraCanvas-logo.png");
            std::ifstream logoFile(logoPath, std::ios::binary);
            if (logoFile) {
                std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(logoFile)),
                                           std::istreambuf_iterator<char>());
                if (!bytes.empty()) {
                    RichDocBlock image;
                    image.type = RichBlockType::Image;
                    image.mediaIndex = document->AddMedia("UltraCanvas-logo.png", "image/png",
                                                          std::move(bytes));
                    image.imageAltText = "UltraCanvas logo";
                    // Scaled down from 847 x 917; the element keeps the aspect
                    // ratio and never draws wider than the text column.
                    image.imageWidthPt = 120.0f;
                    image.imageHeightPt = 130.0f;
                    document->blocks.push_back(image);
                }
            }

            {
                std::vector<RichTextRun> runs;
                runs.push_back(MakeRun("Ctrl+click a link to follow it — a plain click only places the "
                                       "caret, so links stay editable: "));
                RichTextRun link = MakeRun("the element's documentation");
                link.linkTarget = "https://github.com/ULTRA-OS-Project/UltraCanvas/blob/main/"
                                  "Docs/UltraCanvas/UltraCanvasRichTextEdit.md";
                runs.push_back(link);
                runs.push_back(MakeRun("."));
                document->blocks.push_back(MakeParagraph(std::move(runs)));
            }

            return document;
        }

        // ===== TOOLBAR HELPERS =====

        std::string Icon(const std::string& name) {
            return NormalizePath(GetResourcesDir() + "media/icons/texter/" + name);
        }

        // Toolbar buttons must never take the keyboard focus away from the
        // editor: pressing Bold and carrying on typing is the whole point.
        void MakeToolbarButton(const std::shared_ptr<UltraCanvasButton>& button,
                               const std::string& tooltip) {
            if (!button) return;
            button->SetTooltip(tooltip);
            button->SetAcceptsFocus(false);
        }

        std::shared_ptr<UltraCanvasToolbar> MakeToolbar(const std::string& id, float y) {
            auto toolbar = std::make_shared<UltraCanvasToolbar>(id, 20, y, 960, 40);
            toolbar->SetAppearance(ToolbarAppearance::Default());
            return toolbar;
        }

        std::shared_ptr<UltraCanvasDropdown> AddBox(const std::shared_ptr<UltraCanvasToolbar>& toolbar,
                                                    const std::string& id,
                                                    const std::vector<std::string>& items,
                                                    const std::string& tooltip) {
            auto box = toolbar->AddDropdownButton(id, "", items, nullptr);
            if (box) {
                // AddDropdownButton installs a string-based handler; the pages
                // below want the index, so the caller replaces it.
                box->onSelectionChanged = nullptr;
                box->SetTooltip(tooltip);
                box->SetSelectedIndex(0, false);
            }
            return box;
        }

    } // namespace

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateWYSIWYGExamples() {
        const float kWidth = 1020.0f;
        const float kHeight = 780.0f;

        auto root = std::make_shared<UltraCanvasContainer>("WYSIWYGExamples", 0, 0, kWidth, kHeight);
        root->SetBackgroundColor(Color(248, 248, 250, 255));

        auto page = std::make_shared<WysiwygPage>();

        // ===== HEADER =====
        auto title = std::make_shared<UltraCanvasLabel>("wysiwygTitle", 20, 12, 960, 28);
        title->SetText("WYSIWYG Editor — UltraCanvasRichTextEdit");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        root->AddChild(title);

        auto subtitle = std::make_shared<UltraCanvasLabel>("wysiwygSubtitle", 20, 44, 960, 20);
        subtitle->SetText("The caret sits in rendered text and bold is a state of the selection. "
                          "Edits a UCRichDocument, so .odt / .docx round-trip with their formatting intact.");
        subtitle->SetFontSize(12);
        subtitle->SetTextColor(Color(100, 100, 100, 255));
        root->AddChild(subtitle);

        // ===== THE EDITOR =====
        auto edit = CreateRichTextEdit("wysiwygEditor", 20, 212, 960, 460);
        edit->SetDocument(BuildSampleDocument());
        edit->SetModified(false);
        root->AddChild(edit);
        page->edit = edit.get();

        auto* raw = page.get();

        // ===== ROW 1: DOCUMENT AND STRUCTURE =====
        auto documentBar = MakeToolbar("wysiwygDocumentBar", 72);

        MakeToolbarButton(documentBar->AddButton("wysiwygOpen", "Open…", Icon("folder-open.svg"),
            [raw]() {
                FileDialogOptions options;
                options.SetTitle("Open Document")
                       .AddFilter("Documents", std::vector<std::string>{"odt", "docx", "doc", "tex", "md"})
                       .AddFilter("OpenDocument Text (*.odt)", "odt")
                       .AddFilter("Word Document (*.docx)", "docx")
                       .AddFilter("LaTeX (*.tex)", "tex")
                       .AddFilter("Markdown (*.md)", "md")
                       .AddFilter("All files", "*");

                UltraCanvasFileLoader::OpenFileDialog(options,
                    [raw](DialogResult result, const std::string& path) {
                        if (result != DialogResult::OK || path.empty()) {
                            raw->SetStatus("Open cancelled.");
                            return;
                        }
                        std::string error;
                        auto document = UltraCanvasFileLoader::LoadTextDocument(path, error);
                        if (!document) {
                            raw->SetStatus("Could not open document: " + error);
                            return;
                        }
                        raw->edit->SetDocument(document);
                        raw->edit->SetModified(false);
                        raw->edit->ScrollToTop();
                        raw->Sync();
                        raw->SetStatus("Loaded " + std::filesystem::path(path).filename().string()
                                       + " — " + std::to_string(document->blocks.size()) + " blocks, "
                                       + std::to_string(document->media.size()) + " images");
                    });
            }), "Open an .odt / .docx / .tex / .md document");

        MakeToolbarButton(documentBar->AddButton("wysiwygSave", "Save as…", Icon("save.svg"),
            [raw]() {
                FileDialogOptions options;
                options.SetTitle("Save Document As")
                       .SetDefaultFileName("document.odt")
                       .AddFilter("OpenDocument Text (*.odt)", "odt")
                       .AddFilter("Word Document (*.docx)", "docx");

                UltraCanvasFileLoader::SaveFileDialog(options,
                    [raw](DialogResult result, const std::string& path) {
                        if (result != DialogResult::OK || path.empty()) {
                            raw->SetStatus("Save cancelled.");
                            return;
                        }
                        // The element does no file I/O of its own: it hands
                        // back the document it has been editing and the writer
                        // takes it from there.
                        std::string error;
                        if (!UCWordDocumentIO::Save(path, *raw->edit->GetDocument(), error)) {
                            raw->SetStatus("Could not save: " + error);
                            return;
                        }
                        raw->edit->SetModified(false);
                        raw->SetStatus("Saved " + std::filesystem::path(path).filename().string());
                        raw->Sync();
                    });
            }), "Save the edited document as .odt or .docx");

        documentBar->AddSeparator();

        MakeToolbarButton(documentBar->AddButton("wysiwygUndo", "", Icon("undo.svg"),
            [raw]() { raw->edit->Undo(); raw->Sync(); }), "Undo (Ctrl+Z)");
        MakeToolbarButton(documentBar->AddButton("wysiwygRedo", "", Icon("redo.svg"),
            [raw]() { raw->edit->Redo(); raw->Sync(); }), "Redo (Ctrl+Y)");

        documentBar->AddSeparator();

        MakeToolbarButton(documentBar->AddButton("wysiwygRule", "Rule", "",
            [raw]() { raw->edit->InsertHorizontalRule(); raw->Sync(); }), "Insert a horizontal rule");
        MakeToolbarButton(documentBar->AddButton("wysiwygPageBreak", "Page break", "",
            [raw]() { raw->edit->InsertPageBreak(); raw->Sync(); }), "Insert a page break");
        MakeToolbarButton(documentBar->AddButton("wysiwygImage", "", Icon("md-image.svg"),
            [raw]() {
                FileDialogOptions options;
                options.SetTitle("Insert Image")
                       .AddFilter("Images", std::vector<std::string>{"png", "jpg", "jpeg", "gif", "bmp"})
                       .AddFilter("All files", "*");
                UltraCanvasFileLoader::OpenFileDialog(options,
                    [raw](DialogResult result, const std::string& path) {
                        if (result != DialogResult::OK || path.empty()) return;
                        if (!raw->edit->InsertImageFromFile(path,
                                std::filesystem::path(path).filename().string())) {
                            raw->SetStatus("Could not read the image: " + path);
                            return;
                        }
                        raw->Sync();
                    });
            }), "Insert a picture (copied into the document)");
        MakeToolbarButton(documentBar->AddButton("wysiwygLink", "", Icon("md-link.svg"),
            [raw]() {
                if (!raw->edit->HasSelection()) {
                    raw->SetStatus("Select the text the link should cover first.");
                    return;
                }
                raw->edit->SetLink("https://github.com/ULTRA-OS-Project/UltraCanvas");
                raw->Sync();
            }), "Make the selection a hyperlink");

        documentBar->AddSeparator();

        MakeToolbarButton(documentBar->AddButton("wysiwygClear", "Clear formatting", "",
            [raw]() { raw->edit->ClearFormatting(); raw->Sync(); }),
            "Strip every character attribute from the selection");

        root->AddChild(documentBar);

        // ===== ROW 2: CHARACTER FORMATTING =====
        auto characterBar = MakeToolbar("wysiwygCharacterBar", 118);

        auto addToggle = [&](const std::string& id, const std::string& text,
                             const std::string& icon, const std::string& tooltip,
                             std::function<void()> action) {
            auto button = characterBar->AddToggleButton(id, text, icon,
                [action](bool) { action(); });
            MakeToolbarButton(button, tooltip);
            return button;
        };

        auto boldButton = addToggle("wysiwygBold", "", Icon("md-bold.svg"), "Bold (Ctrl+B)",
                                    [raw]() { raw->edit->ToggleBold(); raw->Sync(); });
        auto italicButton = addToggle("wysiwygItalic", "", Icon("md-italic.svg"), "Italic (Ctrl+I)",
                                      [raw]() { raw->edit->ToggleItalic(); raw->Sync(); });
        auto underlineButton = addToggle("wysiwygUnderline", "U", "", "Underline (Ctrl+U)",
                                         [raw]() { raw->edit->ToggleUnderline(); raw->Sync(); });
        auto strikeButton = addToggle("wysiwygStrike", "S", "", "Strikethrough",
                                      [raw]() { raw->edit->ToggleStrikethrough(); raw->Sync(); });
        auto codeButton = addToggle("wysiwygCode", "", Icon("md-code.svg"), "Inline code",
                                    [raw]() { raw->edit->ToggleInlineCode(); raw->Sync(); });
        auto subButton = addToggle("wysiwygSub", "", Icon("md-subscript.svg"), "Subscript",
                                   [raw]() { raw->edit->ToggleSubscript(); raw->Sync(); });
        auto superButton = addToggle("wysiwygSuper", "", Icon("md-superscript.svg"), "Superscript",
                                     [raw]() { raw->edit->ToggleSuperscript(); raw->Sync(); });

        page->bold = boldButton.get();
        page->italic = italicButton.get();
        page->underline = underlineButton.get();
        page->strikethrough = strikeButton.get();
        page->inlineCode = codeButton.get();
        page->subscript = subButton.get();
        page->superscript = superButton.get();

        characterBar->AddSeparator();

        page->fonts = {"", "Sans", "Serif", "Monospace", "Georgia", "Verdana"};
        auto fontBox = AddBox(characterBar, "wysiwygFont",
                              {"Document font", "Sans", "Serif", "Monospace", "Georgia", "Verdana"},
                              "Font family of the selection (\"Document font\" = inherit)");
        if (fontBox) {
            fontBox->onSelectionChanged = [raw](int index, const DropdownItem&) {
                if (index < 0 || index >= static_cast<int>(raw->fonts.size())) return;
                raw->edit->SetFontFamily(raw->fonts[static_cast<size_t>(index)]);
                raw->Sync();
            };
            page->fontBox = fontBox.get();
        }

        page->sizes = {0.0f, 9.0f, 10.0f, 11.0f, 12.0f, 14.0f, 16.0f, 18.0f, 24.0f, 32.0f};
        auto sizeBox = AddBox(characterBar, "wysiwygSize",
                              {"Document size", "9", "10", "11", "12", "14", "16", "18", "24", "32"},
                              "Font size in points (\"Document size\" = inherit)");
        if (sizeBox) {
            sizeBox->onSelectionChanged = [raw](int index, const DropdownItem&) {
                if (index < 0 || index >= static_cast<int>(raw->sizes.size())) return;
                raw->edit->SetFontSize(raw->sizes[static_cast<size_t>(index)]);
                raw->Sync();
            };
            page->sizeBox = sizeBox.get();
        }

        page->colors = {"", "#000000", "#CC0000", "#E07000", "#1E7A1E", "#0066CC", "#7A2E9D", "#808080"};
        auto colorBox = AddBox(characterBar, "wysiwygColor",
                               {"Automatic", "Black", "Red", "Orange", "Green", "Blue", "Purple", "Grey"},
                               "Text colour of the selection");
        if (colorBox) {
            colorBox->onSelectionChanged = [raw](int index, const DropdownItem&) {
                if (index < 0 || index >= static_cast<int>(raw->colors.size())) return;
                raw->edit->SetTextColor(raw->colors[static_cast<size_t>(index)]);
                raw->Sync();
            };
            page->colorBox = colorBox.get();
        }

        root->AddChild(characterBar);

        // ===== ROW 3: PARAGRAPH FORMATTING =====
        auto paragraphBar = MakeToolbar("wysiwygParagraphBar", 164);

        auto styleBox = AddBox(paragraphBar, "wysiwygStyle",
                               {"Body text", "Heading 1", "Heading 2", "Heading 3", "Heading 4",
                                "Code block", "Block quote"},
                               "Paragraph style of every block the selection touches");
        if (styleBox) {
            styleBox->onSelectionChanged = [raw](int index, const DropdownItem&) {
                switch (index) {
                    case 5:
                        if (raw->edit->GetCurrentBlockType() != RichBlockType::CodeBlock) {
                            raw->edit->ToggleCodeBlock("");
                        }
                        break;
                    case 6:
                        if (raw->edit->GetCurrentBlockType() != RichBlockType::BlockQuote) {
                            raw->edit->ToggleBlockQuote();
                        }
                        break;
                    default:
                        raw->edit->SetHeadingLevel(index < 0 ? 0 : index);
                        break;
                }
                raw->Sync();
            };
            page->styleBox = styleBox.get();
        }

        auto alignBox = AddBox(paragraphBar, "wysiwygAlign",
                               {"Align left", "Centre", "Align right", "Justify"},
                               "Paragraph alignment");
        if (alignBox) {
            alignBox->onSelectionChanged = [raw](int index, const DropdownItem&) {
                RichTextAlign align = RichTextAlign::Left;
                if (index == 1) align = RichTextAlign::Center;
                else if (index == 2) align = RichTextAlign::Right;
                else if (index == 3) align = RichTextAlign::Justify;
                raw->edit->SetAlignment(align);
                raw->Sync();
            };
            page->alignBox = alignBox.get();
        }

        paragraphBar->AddSeparator();

        MakeToolbarButton(paragraphBar->AddButton("wysiwygBullets", "", Icon("md-list-unordered.svg"),
            [raw]() { raw->edit->ToggleBulletList(); raw->Sync(); }), "Bulleted list");
        MakeToolbarButton(paragraphBar->AddButton("wysiwygNumbers", "", Icon("md-list-ordered.svg"),
            [raw]() { raw->edit->ToggleNumberedList(); raw->Sync(); }), "Numbered list");
        MakeToolbarButton(paragraphBar->AddButton("wysiwygOutdent", "", Icon("arrow_left.svg"),
            [raw]() { raw->edit->OutdentList(); raw->Sync(); }), "Outdent (Shift+Tab, inside a list)");
        MakeToolbarButton(paragraphBar->AddButton("wysiwygIndent", "", Icon("arrow_right.svg"),
            [raw]() { raw->edit->IndentList(); raw->Sync(); }), "Indent (Tab, inside a list)");

        paragraphBar->AddSeparator();

        MakeToolbarButton(paragraphBar->AddButton("wysiwygQuote", "", Icon("md-quote.svg"),
            [raw]() { raw->edit->ToggleBlockQuote(); raw->Sync(); }), "Block quote");
        MakeToolbarButton(paragraphBar->AddButton("wysiwygCodeBlock", "", Icon("code2.svg"),
            [raw]() { raw->edit->ToggleCodeBlock("cpp"); raw->Sync(); }), "Code block");

        paragraphBar->AddStretch(1.0f);

        MakeToolbarButton(paragraphBar->AddButton("wysiwygReadOnly", "Read-only", "",
            [raw]() {
                bool readOnly = !raw->edit->IsReadOnly();
                raw->edit->SetReadOnly(readOnly);
                raw->SetStatus(readOnly
                    ? "Read-only: no caret and no keys, still selectable and scrollable — "
                      "the shortest path to an .odt / .docx preview pane."
                    : "Editing again.");
            }), "Toggle the read-only rendering mode");

        root->AddChild(paragraphBar);

        // ===== STATUS AND NOTES =====
        auto status = std::make_shared<UltraCanvasLabel>("wysiwygStatus", 20, 680, 960, 22);
        status->SetFontSize(12);
        status->SetTextColor(Color(70, 70, 70, 255));
        root->AddChild(status);
        page->status = status.get();

        auto notes = std::make_shared<UltraCanvasLabel>("wysiwygNotes", 20, 704, 960, 56);
        notes->SetText("Known limits of this first version: tables render but are not edited in place, "
                       "images are not resized interactively, math runs render as their LaTeX source, "
                       "there is no spell checking yet, and rich paste between applications still needs "
                       "clipboard MIME flavours the backend does not carry — copy and paste inside the "
                       "application does keep formatting.");
        notes->SetFontSize(11);
        notes->SetTextColor(Color(120, 120, 120, 255));
        root->AddChild(notes);

        // ===== KEEPING THE TOOLBARS IN STEP =====
        // The element owns these callbacks, so capturing `page` here is what
        // keeps the page state alive; `page` itself holds only raw pointers
        // back, so nothing forms a cycle.
        edit->onSelectionChanged = [page]() { page->Sync(); };
        edit->onDocumentChanged = [page]() { page->Sync(); };
        edit->onLinkClicked = [page](const std::string& target) {
            page->SetStatus("Opening " + target);
            OpenURL(target);
            return true;
        };

        page->Sync();
        return root;
    }

} // namespace UltraCanvas
