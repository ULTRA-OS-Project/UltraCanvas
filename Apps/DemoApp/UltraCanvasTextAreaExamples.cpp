// Apps/DemoApp/UltraCanvasTextAreaExamples.cpp
// Implementation of all component example creators
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): one card per
//                  editor, the editors as wide as the page, the controls in a
//                  card, buttons sized to their text
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasTextArea.h"
#include <sstream>
#include <random>
#include <map>

namespace UltraCanvas {

    // Create TextArea Examples for Extended Functionality category
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTextAreaExamples() {
        auto container = DemoKit::CreatePage("TextAreaContainer",
                "UltraCanvas TextArea - Syntax Highlighting Demo",
                "Advanced text editing with syntax highlighting, line numbers, and theme support");

        // ===== TEXTAREA 1: C++ Code with Dark Theme =====
        auto cppCard = DemoKit::AddCard(container, "CppLabel", "C++ Syntax (Dark Theme)");

        // C++ TextArea
        auto cppTextArea = std::make_shared<UltraCanvasTextArea>("CppTextArea", 0, 0, 950, 200);
        cppTextArea->ApplyDarkCodeStyle("C++");
        cppTextArea->SetShowLineNumbers(true);
        cppTextArea->SetHighlightCurrentLine(true);
        cppTextArea->SetFontSize(10);

        // Sample C++ code
        std::string cppCode = R"(// UltraCanvas Example
#include <iostream>
#include <vector>
#include <memory>
#include "UltraCanvasDebug.h"

class Widget {
private:
    std::string name;
    int id;

public:
    Widget(const std::string& n, int i)
        : name(n), id(i) {}

    virtual void Render() {
        debugOutput << "Rendering: "
                  << name << std::endl;
    }

    int GetId() const {
        return id;
    }
};

int main() {
    std::vector<std::unique_ptr<Widget>> widgets;

    // Create widgets
    for (int i = 0; i < 10; ++i) {
        auto widget = std::make_unique<Widget>(
            "Widget_" + std::to_string(i), i
        );
        widgets.push_back(std::move(widget));
    }

    // Render all widgets
    for (const auto& w : widgets) {
        w->Render();
    }

    return 0;
})";

        cppTextArea->SetText(cppCode);
        DemoKit::AddStretched(cppCard, cppTextArea);

        // ===== TEXTAREA 2: Python Code with Light Theme =====
        auto pythonCard = DemoKit::AddCard(container, "PythonLabel", "Python Syntax (Light Theme)");

        // Python TextArea
        auto pythonTextArea = std::make_shared<UltraCanvasTextArea>("PythonTextArea", 0, 0, 950, 200);
        pythonTextArea->ApplyCodeStyle("Python");
        pythonTextArea->SetShowLineNumbers(true);
        pythonTextArea->SetHighlightCurrentLine(true);
        pythonTextArea->SetFontSize(10);

        // Sample Python code
        std::string pythonCode = R"(# UltraCanvas Python Example
import sys
import json
from typing import List, Dict, Optional

class CanvasElement:
    """Base class for canvas elements"""

    def __init__(self, name: str, x: int, y: int):
        self.name = name
        self.position = (x, y)
        self.visible = True
        self.children: List[CanvasElement] = []

    def render(self) -> None:
        """Render the element"""
        if not self.visible:
            return

        print(f"Rendering {self.name}")
        for child in self.children:
            child.render()

    def add_child(self, element: 'CanvasElement'):
        """Add a child element"""
        self.children.append(element)

    @property
    def child_count(self) -> int:
        return len(self.children)

def main():
    # Create root element
    root = CanvasElement("Root", 0, 0)

    # Create child elements
    for i in range(5):
        child = CanvasElement(
            f"Child_{i}",
            i * 100,
            i * 50
        )
        root.add_child(child)

    # Render all
    root.render()
    print(f"Total children: {root.child_count}")

if __name__ == "__main__":
    main())";

        pythonTextArea->SetText(pythonCode);
        DemoKit::AddStretched(pythonCard, pythonTextArea);

        // ===== TEXTAREA 3: Pascal Code with Custom Settings =====
        auto pascalCard = DemoKit::AddCard(container, "PascalLabel", "Pascal Syntax (Custom Theme)");

        // Pascal TextArea with custom settings
        auto pascalTextArea = std::make_shared<UltraCanvasTextArea>("PascalTextArea", 0, 0, 950, 200);

        // Apply custom style for Pascal
        pascalTextArea->ApplyCodeStyle("Pascal");
        pascalTextArea->SetShowLineNumbers(true);
        pascalTextArea->SetHighlightCurrentLine(true); // Different setting
        pascalTextArea->SetFontSize(10);

        // Custom color scheme for Pascal
        TextAreaStyle pascalStyle = pascalTextArea->GetStyle();
        pascalStyle.backgroundColor = Color(250, 250, 245);  // Cream background
        pascalStyle.fontColor = Color(40, 40, 40);
        pascalStyle.lineNumbersBackgroundColor = Color(240, 240, 235);
        pascalStyle.lineNumbersColor = Color(120, 120, 120);
        pascalStyle.currentLineColor = Color(245, 245, 240);
        pascalStyle.borderColor = Color(180, 180, 180);

        // Custom token colors for Pascal
        pascalStyle.tokenStyles.keywordStyle = TokenStyle(Color(0, 0, 200), true);      // Blue bold
        pascalStyle.tokenStyles.typeStyle = TokenStyle(Color(128, 0, 128));             // Purple
        pascalStyle.tokenStyles.functionStyle = TokenStyle(Color(180, 90, 0));          // Brown
        pascalStyle.tokenStyles.stringStyle = TokenStyle(Color(200, 0, 0));             // Red
        pascalStyle.tokenStyles.commentStyle = TokenStyle(Color(0, 128, 0), false, true); // Green italic
        pascalStyle.tokenStyles.numberStyle = TokenStyle(Color(0, 128, 128));           // Teal
        pascalStyle.tokenStyles.operatorStyle = TokenStyle(Color(80, 80, 80));          // Dark gray

        pascalTextArea->SetStyle(pascalStyle);

        // Sample Pascal code
        std::string pascalCode = R"({ UltraCanvas Pascal Example }
program CanvasDemo;

uses
  SysUtils, Classes, Graphics;

type
  TCanvasElement = class(TObject)
  private
    FName: string;
    FPosition: TPoint;
    FVisible: Boolean;
    FChildren: TList;
  public
    constructor Create(const AName: string);
    destructor Destroy; override;

    procedure Render; virtual;
    procedure AddChild(AElement: TCanvasElement);

    property Name: string read FName;
    property Visible: Boolean
      read FVisible write FVisible;
  end;

constructor TCanvasElement.Create(
  const AName: string);
begin
  inherited Create;
  FName := AName;
  FVisible := True;
  FChildren := TList.Create;
end;

destructor TCanvasElement.Destroy;
var
  I: Integer;
begin
  for I := 0 to FChildren.Count - 1 do
    TCanvasElement(FChildren[I]).Free;
  FChildren.Free;
  inherited;
end;

procedure TCanvasElement.Render;
var
  I: Integer;
  Child: TCanvasElement;
begin
  if not FVisible then
    Exit;

  WriteLn('Rendering: ', FName);

  { Render all children }
  for I := 0 to FChildren.Count - 1 do
  begin
    Child := TCanvasElement(FChildren[I]);
    Child.Render;
  end;
end;

procedure TCanvasElement.AddChild(
  AElement: TCanvasElement);
begin
  if Assigned(AElement) then
    FChildren.Add(AElement);
end;

var
  Root: TCanvasElement;
  Child: TCanvasElement;
  I: Integer;

begin
  Root := TCanvasElement.Create('Root');
  try
    { Create child elements }
    for I := 1 to 5 do
    begin
      Child := TCanvasElement.Create(
        Format('Child_%d', [I])
      );
      Root.AddChild(Child);
    end;

    { Render the tree }
    Root.Render;
  finally
    Root.Free;
  end;
end.)";

        pascalTextArea->SetText(pascalCode);
        DemoKit::AddStretched(pascalCard, pascalTextArea);

        // ===== CONTROLS SECTION =====
        // The controls in a card under the editors; each button is as wide as
        // its text, at least the width it had.
        auto controlsCard = DemoKit::AddCard(container, "TextAreaControls");
        auto controlsRow = DemoKit::AddRow(controlsCard, "TextAreaControlsRow", 8);
        controlsRow->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
        auto addButton = [&controlsRow](const std::string& id, const std::string& text, float minWidth) {
            auto button = std::make_shared<UltraCanvasButton>(id, 0, 0, 0, 25, text);
            CSSLayout::BoxConstraints limits;
            limits.minWidth = CSSLayout::Dimension::Px(minWidth);
            button->boxConstraints = limits;
            DemoKit::Add(controlsRow, button);
            return button;
        };

        // Read-only checkbox
//        auto readOnlyCheckbox = std::make_shared<UltraCanvasCheckBox>("ReadOnlyCheckbox", 20, 540, 120, 25);
//        readOnlyCheckbox->SetText("Read Only");
//        readOnlyCheckbox->SetChecked(false);
//        readOnlyCheckbox->SetOnCheckedChanged([cppTextArea, pythonTextArea, pascalTextArea](bool checked) {
//            cppTextArea->SetReadOnly(checked);
//            pythonTextArea->SetReadOnly(checked);
//            pascalTextArea->SetReadOnly(checked);
//        });
//        container->AddChild(readOnlyCheckbox);
//
//        // Word wrap checkbox
//        auto wordWrapCheckbox = std::make_shared<UltraCanvasCheckBox>("WordWrapCheckbox", 150, 540, 120, 25);
//        wordWrapCheckbox->SetText("Word Wrap");
//        wordWrapCheckbox->SetChecked(false);
//        wordWrapCheckbox->SetOnCheckedChanged([cppTextArea, pythonTextArea, pascalTextArea](bool checked) {
//            cppTextArea->SetWordWrap(checked);
//            pythonTextArea->SetWordWrap(checked);
//            pascalTextArea->SetWordWrap(checked);
//        });
//        container->AddChild(wordWrapCheckbox);

        // Theme selector dropdown
//        auto themeDropdown = std::make_shared<UltraCanvasDropdown>("ThemeDropdown", 280, 540, 150, 25);
//        themeDropdown->AddItem("Keep Current");
//        themeDropdown->AddItem("All Light");
//        themeDropdown->AddItem("All Dark");
//        themeDropdown->SetSelectedIndex(0);
//        themeDropdown->onSelectionChanged = [cppTextArea, pythonTextArea, pascalTextArea](int index, const DropdownItem&) {
//            if (index == 1) {  // All Light
//                cppTextArea->ApplyLightTheme();
//                pythonTextArea->ApplyLightTheme();
//                pascalTextArea->ApplyLightTheme();
//            } else if (index == 2) {  // All Dark
//                cppTextArea->ApplyDarkTheme();
//                pythonTextArea->ApplyDarkTheme();
//                pascalTextArea->ApplyDarkTheme();
//            }
//        };
//        container->AddChild(themeDropdown);

        // Font size label
        DemoKit::AddText(controlsRow, "FontSizeLabel", "Font Size:");

        // Decrease font button
        auto decreaseFontBtn = addButton("DecreaseFontBtn", "-", 30);
        decreaseFontBtn->onClick = [cppTextArea, pythonTextArea, pascalTextArea]() {
            int newSize = cppTextArea->GetFontSize() - 1;
            if (newSize >= 8) {
                cppTextArea->SetFontSize(newSize);
                pythonTextArea->SetFontSize(newSize);
                pascalTextArea->SetFontSize(newSize);
            }
        };

        // Increase font button
        auto increaseFontBtn = addButton("IncreaseFontBtn", "+", 30);
        increaseFontBtn->onClick = [cppTextArea, pythonTextArea, pascalTextArea]() {
            int newSize = cppTextArea->GetFontSize() + 1;
            if (newSize <= 20) {
                cppTextArea->SetFontSize(newSize);
                pythonTextArea->SetFontSize(newSize);
                pascalTextArea->SetFontSize(newSize);
            }
        };

        // Clear all button
        auto clearAllBtn = addButton("ClearAllBtn", "Clear All", 90);
//        clearAllBtn->SetButtonStyle(ButtonStyle::Danger);
        clearAllBtn->onClick = [cppTextArea, pythonTextArea, pascalTextArea]() {
            cppTextArea->Clear();
            pythonTextArea->Clear();
            pascalTextArea->Clear();
        };

        // Line numbers toggle button
        auto lineNumBtn = addButton("LineNumBtn", "Toggle Lines", 120);
//        lineNumBtn->SetButtonStyle(ButtonStyle::Secondary);
        lineNumBtn->onClick = [cppTextArea, pythonTextArea, pascalTextArea]() {
            bool showLines = !cppTextArea->GetShowLineNumbers();
            cppTextArea->SetShowLineNumbers(showLines);
            pythonTextArea->SetShowLineNumbers(showLines);
            pascalTextArea->SetShowLineNumbers(showLines);
        };

        auto syntaxToggleBtn = addButton("syntaxToggleBtn", "Toggle syntax", 140);
//        lineNumBtn->SetButtonStyle(ButtonStyle::Secondary);
        syntaxToggleBtn->onClick = [cppTextArea, pythonTextArea, pascalTextArea]() {
            bool showSyntax = !cppTextArea->GetHighlightSyntax();
            cppTextArea->SetHighlightSyntax(showSyntax);
            pythonTextArea->SetHighlightSyntax(showSyntax);
            pascalTextArea->SetHighlightSyntax(showSyntax);
        };

        // Info labels at bottom
        auto infoLabel = DemoKit::AddCaption(controlsCard, "InfoLabel",
                "Try typing in any text area - supports full editing, selection, copy/paste, and undo/redo");
        infoLabel->SetAlignment(TextAlignment::Center, VerticalAlignment::Top);

        return container;
    }
} // namespace UltraCanvas