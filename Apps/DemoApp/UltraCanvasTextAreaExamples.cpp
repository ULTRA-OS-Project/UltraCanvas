// Apps/DemoApp/UltraCanvasDemoExamples.cpp
// Implementation of all component example creators
// Version: 1.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
//#include "UltraCanvasButton3Sections.h"
#include "UltraCanvasFormulaEditor.h"
#include "Plugins/Charts/UltraCanvasDivergingBarChart.h"
#include <sstream>
#include <random>
#include <map>

namespace UltraCanvas {

    // Create TextArea Examples for Extended Functionality category
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTextAreaExamples() {
        // Create main container with fixed size
        auto container = std::make_shared<UltraCanvasContainer>("TextAreaContainer", 1000, 0, 0, 1000, 780);

//        // Title Label - centered at top
//        auto titleLabel = std::make_shared<UltraCanvasLabel>("TitleLabel", 1001, 20, 10, 760, 30);
//        titleLabel->SetText("UltraCanvas TextArea - Syntax Highlighting Demo");
//        titleLabel->SetFontSize(18);
//        titleLabel->SetFontWeight(FontWeight::Bold);
//        titleLabel->SetAlignment(TextAlignment::Center);
//        container->AddChild(titleLabel);

        // Description Label - below title
        auto descLabel = std::make_shared<UltraCanvasLabel>("DescLabel", 1002, 20, 5, 760, 20);
        descLabel->SetText("Advanced text editing with syntax highlighting, line numbers, and theme support");
        descLabel->SetFontSize(12);
        descLabel->SetTextColor(Color(100, 100, 100));
        descLabel->SetAlignment(TextAlignment::Center);
        container->AddChild(descLabel);

        // ===== TEXTAREA 1: C++ Code with Dark Theme =====
        // Left column
        auto cppLabel = std::make_shared<UltraCanvasLabel>("CppLabel", 1011, 20, 30, 245, 20);
        cppLabel->SetText("C++ Syntax (Dark Theme)");
        cppLabel->SetFontSize(12);
        cppLabel->SetFontWeight(FontWeight::Bold);
        cppLabel->SetAlignment(TextAlignment::Left);
        container->AddChild(cppLabel);

        // C++ TextArea
        auto cppTextArea = std::make_shared<UltraCanvasTextArea>("CppTextArea", 1012, 20, 50, 950, 200);
        cppTextArea->ApplyDarkCodeStyle("C++");
        cppTextArea->SetShowLineNumbers(true);
        cppTextArea->SetHighlightCurrentLine(true);
        cppTextArea->SetFontSize(12);

        // Sample C++ code
        std::string cppCode = R"(// UltraCanvas Example
#include <iostream>
#include <vector>
#include <memory>

class Widget {
private:
    std::string name;
    int id;

public:
    Widget(const std::string& n, int i)
        : name(n), id(i) {}

    virtual void Render() {
        std::cout << "Rendering: "
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
        container->AddChild(cppTextArea);

        // ===== TEXTAREA 2: Python Code with Light Theme =====
        // Middle column
        auto pythonLabel = std::make_shared<UltraCanvasLabel>("PythonLabel", 1021, 20, 255, 245, 20);
        pythonLabel->SetText("Python Syntax (Light Theme)");
        pythonLabel->SetFontSize(12);
        pythonLabel->SetFontWeight(FontWeight::Bold);
        pythonLabel->SetAlignment(TextAlignment::Left);
        container->AddChild(pythonLabel);

        // Python TextArea
        auto pythonTextArea = std::make_shared<UltraCanvasTextArea>("PythonTextArea", 1022, 20, 275, 950, 200);
        pythonTextArea->ApplyCodeStyle("Python");
        pythonTextArea->SetShowLineNumbers(true);
        pythonTextArea->SetHighlightCurrentLine(true);
        pythonTextArea->SetFontSize(12);

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
        container->AddChild(pythonTextArea);

        // ===== TEXTAREA 3: Pascal Code with Custom Settings =====
        // Right column
        auto pascalLabel = std::make_shared<UltraCanvasLabel>("PascalLabel", 1031, 20, 480, 265, 20);
        pascalLabel->SetText("Pascal Syntax (Custom Theme)");
        pascalLabel->SetFontSize(12);
        pascalLabel->SetFontWeight(FontWeight::Bold);
        pascalLabel->SetAlignment(TextAlignment::Left);
        container->AddChild(pascalLabel);

        // Pascal TextArea with custom settings
        auto pascalTextArea = std::make_shared<UltraCanvasTextArea>("PascalTextArea", 1032, 20, 500, 950, 200);

        // Apply custom style for Pascal
        pascalTextArea->ApplyCodeStyle("Pascal");
        pascalTextArea->SetShowLineNumbers(true);
        pascalTextArea->SetHighlightCurrentLine(true); // Different setting
        pascalTextArea->SetFontSize(12);

        // Custom color scheme for Pascal
        TextAreaStyle pascalStyle = pascalTextArea->GetStyle();
        pascalStyle.backgroundColor = Color(250, 250, 245);  // Cream background
        pascalStyle.fontColor = Color(40, 40, 40);
        pascalStyle.lineNumbersBackgroundColor = Color(240, 240, 235);
        pascalStyle.lineNumbersColor = Color(120, 120, 120);
        pascalStyle.currentLineColor = Color(245, 245, 240);
        pascalStyle.borderColor = Color(180, 180, 180);
        pascalStyle.borderWidth = 2;

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
        container->AddChild(pascalTextArea);

        // ===== CONTROLS SECTION =====
        // Controls positioned at bottom of the window

        // Read-only checkbox
//        auto readOnlyCheckbox = std::make_shared<UltraCanvasCheckBox>("ReadOnlyCheckbox", 1041, 20, 540, 120, 25);
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
//        auto wordWrapCheckbox = std::make_shared<UltraCanvasCheckBox>("WordWrapCheckbox", 1042, 150, 540, 120, 25);
//        wordWrapCheckbox->SetText("Word Wrap");
//        wordWrapCheckbox->SetChecked(false);
//        wordWrapCheckbox->SetOnCheckedChanged([cppTextArea, pythonTextArea, pascalTextArea](bool checked) {
//            cppTextArea->SetWordWrap(checked);
//            pythonTextArea->SetWordWrap(checked);
//            pascalTextArea->SetWordWrap(checked);
//        });
//        container->AddChild(wordWrapCheckbox);

        // Theme selector dropdown
//        auto themeDropdown = std::make_shared<UltraCanvasDropdown>("ThemeDropdown", 1043, 280, 540, 150, 25);
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
        auto fontSizeLabel = std::make_shared<UltraCanvasLabel>("FontSizeLabel", 1044, 420, 720, 90, 20);
        fontSizeLabel->SetText("Font Size:");
        fontSizeLabel->SetAlignment(TextAlignment::Right);
        container->AddChild(fontSizeLabel);

        // Decrease font button
        auto decreaseFontBtn = std::make_shared<UltraCanvasButton>("DecreaseFontBtn", 1045, 515, 720, 30, 25);
        decreaseFontBtn->SetText("-");
        decreaseFontBtn->onClick = [cppTextArea, pythonTextArea, pascalTextArea]() {
            int newSize = cppTextArea->GetFontSize() - 1;
            if (newSize >= 8) {
                cppTextArea->SetFontSize(newSize);
                pythonTextArea->SetFontSize(newSize);
                pascalTextArea->SetFontSize(newSize);
            }
        };
        container->AddChild(decreaseFontBtn);

        // Increase font button
        auto increaseFontBtn = std::make_shared<UltraCanvasButton>("IncreaseFontBtn", 1046, 550, 720, 30, 25);
        increaseFontBtn->SetText("+");
        increaseFontBtn->onClick = [cppTextArea, pythonTextArea, pascalTextArea]() {
            int newSize = cppTextArea->GetFontSize() + 1;
            if (newSize <= 20) {
                cppTextArea->SetFontSize(newSize);
                pythonTextArea->SetFontSize(newSize);
                pascalTextArea->SetFontSize(newSize);
            }
        };
        container->AddChild(increaseFontBtn);

        // Clear all button
        auto clearAllBtn = std::make_shared<UltraCanvasButton>("ClearAllBtn", 1047, 590, 720, 90, 25);
        clearAllBtn->SetText("Clear All");
//        clearAllBtn->SetButtonStyle(ButtonStyle::Danger);
        clearAllBtn->onClick = [cppTextArea, pythonTextArea, pascalTextArea]() {
            cppTextArea->Clear();
            pythonTextArea->Clear();
            pascalTextArea->Clear();
        };
        container->AddChild(clearAllBtn);

        // Line numbers toggle button
        auto lineNumBtn = std::make_shared<UltraCanvasButton>("LineNumBtn", 1048, 700, 720, 100, 25);
        lineNumBtn->SetText("Toggle Lines");
//        lineNumBtn->SetButtonStyle(ButtonStyle::Secondary);
        lineNumBtn->onClick = [cppTextArea, pythonTextArea, pascalTextArea]() {
            bool showLines = !cppTextArea->GetShowLineNumbers();
            cppTextArea->SetShowLineNumbers(showLines);
            pythonTextArea->SetShowLineNumbers(showLines);
            pascalTextArea->SetShowLineNumbers(showLines);
        };
        container->AddChild(lineNumBtn);

        auto syntaxToggleBtn = std::make_shared<UltraCanvasButton>("syntaxToggleBtn", 1048, 810, 720, 100, 25);
        syntaxToggleBtn->SetText("Toggle syntax");
//        lineNumBtn->SetButtonStyle(ButtonStyle::Secondary);
        syntaxToggleBtn->onClick = [cppTextArea, pythonTextArea, pascalTextArea]() {
            bool showSyntax = !cppTextArea->GetHighlightSyntax();
            cppTextArea->SetHighlightSyntax(showSyntax);
            pythonTextArea->SetHighlightSyntax(showSyntax);
            pascalTextArea->SetHighlightSyntax(showSyntax);
        };
        container->AddChild(syntaxToggleBtn);

        // Info labels at bottom
        auto infoLabel = std::make_shared<UltraCanvasLabel>("InfoLabel", 1050, 20, 750, 760, 10);
        infoLabel->SetText("Try typing in any text area - supports full editing, selection, copy/paste, and undo/redo");
        infoLabel->SetFontSize(10);
        infoLabel->SetTextColor(Color(128, 128, 128));
        infoLabel->SetAlignment(TextAlignment::Center);
        container->AddChild(infoLabel);

        return container;
    }
} // namespace UltraCanvas