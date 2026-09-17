# UIScript - Specification and Implementation Guide

**Document Version:** 1.0.0  
**Last Modified:** 2024-12-19  
**Author:** UltraCanvas Framework  
**Status:** Master Specification

---

## Table of Contents

1. [Overview](#1-overview)
2. [Core Concepts](#2-core-concepts)
3. [Architecture](#3-architecture)
4. [Syntax Specifications](#4-syntax-specifications)
5. [Recording System](#5-recording-system)
6. [Scripting Dictionary](#6-scripting-dictionary)
7. [API Definitions](#7-api-definitions)
8. [Implementation Details](#8-implementation-details)
9. [File Structure](#9-file-structure)
10. [Integration Points](#10-integration-points)
11. [Examples](#11-examples)
12. [Development Guidelines](#12-development-guidelines)

---

## 1. Overview

### 1.1 What is UIScript?

**UIScript** is a modern, cross-platform scripting language designed for automating UltraCanvas applications. It provides the same functionality as macOS AppleScript but with contemporary syntax styles and cross-platform support.

**Key Features:**
- **Modern Syntax** - Three syntax styles (Modern, Natural, Classic)
- **Automatic Recording** - Record user actions as script code
- **Cross-Platform** - Works on Linux, macOS, Windows, BSD, WebAssembly
- **Type-Safe** - Strong typing with runtime validation
- **Extensible** - Plugin architecture for custom commands
- **Object-Oriented** - Elements, windows, and applications as first-class objects

### 1.2 Design Philosophy

**Core Principles:**

1. **Automatic by Default** - Recording happens transparently through event system
2. **Zero Boilerplate** - Elements need minimal code to be scriptable
3. **Natural Language** - Scripts should read like English sentences
4. **Type Safety** - Compile-time and runtime type checking
5. **Cross-Platform** - Same scripts work on all platforms
6. **Backward Compatible** - AppleScript users feel at home

### 1.3 Comparison with AppleScript

| Feature | AppleScript | UIScript |
|---------|-------------|----------|
| Platform | macOS only | Cross-platform |
| Syntax | Single style | Three styles (Modern/Natural/Classic) |
| Recording | Application-dependent | Automatic for all elements |
| Type System | Weak typing | Strong typing |
| Object Model | Application-specific | Unified UltraCanvas model |
| Performance | Interpreted | Compiled + Interpreted options |
| IDE | Script Editor | UltraCanvas Script Editor |

---

## 2. Core Concepts

### 2.1 Object Model

UIScript operates on a hierarchical object model:

```
Application
    └── Window
            └── UIElement
                    ├── Button
                    ├── TextField
                    ├── Slider
                    └── [Custom Elements]
```

**Object Hierarchy Rules:**
- Every element has a script name (unique within parent)
- Every element has a script class (type identifier)
- Elements can be accessed by name or index
- Parent-child relationships are maintained automatically

### 2.2 Event-Action Model

```
User Action → Platform Event → UCEvent → Recording → Element Handler
```

**Key Points:**
- Recording happens BEFORE element handling
- All UCEvents are potentially recordable
- Elements control recordability via opt-out flag
- Recording is centralized in UIScriptRecorder

### 2.3 Script Execution Model

```
UIScript Code → Parser → AST → Interpreter → UltraCanvas API Calls
```

**Execution Modes:**
- **Interpreted** - Direct execution (for Script Editor)
- **Compiled** - Pre-compiled to bytecode (for production)
- **Hybrid** - JIT compilation for performance

---

## 3. Architecture

### 3.1 Component Overview

```
┌─────────────────────────────────────────────────────┐
│                  UIScript System                     │
├─────────────────────────────────────────────────────┤
│                                                      │
│  ┌──────────────┐  ┌──────────────┐  ┌───────────┐ │
│  │   Recorder   │  │    Parser    │  │ Executor  │ │
│  └──────────────┘  └──────────────┘  └───────────┘ │
│          │                  │                │      │
│          └──────────────────┼────────────────┘      │
│                             │                        │
│  ┌──────────────────────────▼──────────────────┐   │
│  │        Scripting Dictionary                  │   │
│  │  (Element Metadata + Command Definitions)    │   │
│  └──────────────────────────────────────────────┘   │
│                             │                        │
│  ┌──────────────────────────▼──────────────────┐   │
│  │         UltraCanvas Event System             │   │
│  │    (UCEvent → Recording → Dispatching)       │   │
│  └──────────────────────────────────────────────┘   │
│                             │                        │
│  ┌──────────────────────────▼──────────────────┐   │
│  │         Scriptable UI Elements               │   │
│  │   (Buttons, TextFields, Custom Elements)     │   │
│  └──────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────┘
```

### 3.2 Core Components

#### 3.2.1 UIScriptRecorder
- **Purpose:** Automatic event recording
- **Location:** `include/UltraCanvasScriptRecorder.h`
- **Responsibility:** Monitor UCEvents, generate script code
- **Singleton:** Yes

#### 3.2.2 UIScriptParser
- **Purpose:** Parse UIScript code into AST
- **Location:** `include/UIScriptParser.h`
- **Responsibility:** Syntax validation, AST generation
- **Supports:** All three syntax styles

#### 3.2.3 UIScriptExecutor
- **Purpose:** Execute parsed scripts
- **Location:** `include/UIScriptExecutor.h`
- **Responsibility:** Interpret AST, call UltraCanvas APIs
- **Thread-Safe:** Yes

#### 3.2.4 UIScriptDictionary
- **Purpose:** Element metadata registry
- **Location:** `include/UIScriptDictionary.h`
- **Responsibility:** Property/command definitions, type validation
- **Format:** XML (compatible with AppleScript SDEF)

#### 3.2.5 IUIScriptable Interface
- **Purpose:** Mark elements as scriptable
- **Location:** `include/UltraCanvasScriptableElement.h`
- **Responsibility:** Provide script identity and metadata
- **Implementation:** Minimal (name + class)

### 3.3 Data Flow

#### 3.3.1 Recording Flow
```
1. User clicks button
2. X11/Cocoa generates platform event
3. Platform-specific code converts to UCEvent
4. UltraCanvasWindow::DispatchEvent() receives UCEvent
5. UIScriptRecorder::RecordEventIfEnabled() called
   ├─ Check: Is recording active? → NO → Skip
   ├─ Check: Is element scriptable? → NO → Skip
   ├─ Check: Should record this event type? → NO → Skip
   └─ YES → Create UIScriptAction, add to list, call live callback
6. Element->HandleEvent() processes event normally
```

#### 3.3.2 Execution Flow
```
1. User enters UIScript code in Script Editor
2. UIScriptParser::Parse() generates AST
3. UIScriptExecutor::Execute() interprets AST
4. For each command:
   ├─ Resolve target element (by name, class, index)
   ├─ Validate command exists for element type
   ├─ Validate parameter types
   ├─ Execute command via UltraCanvas API
   └─ Return result or error
5. Display result in Script Editor
```

---

## 4. Syntax Specifications

### 4.1 Modern Style (Default)

**Philosophy:** Object-oriented, method chaining, JavaScript-inspired

**Syntax Rules:**
- Objects accessed via dot notation
- Methods called with parentheses
- Strings in double quotes
- Chaining supported with dot operator
- Semicolons optional

**Grammar:**
```bnf
<statement>     ::= <expression> ";"?
<expression>    ::= <object-chain> | <assignment> | <control-flow>
<object-chain>  ::= <object> ("." <property-or-method>)*
<object>        ::= <identifier> | <constructor>
<constructor>   ::= <type> "(" <string-literal> ")"
<method-call>   ::= <identifier> "(" <arguments>? ")"
<assignment>    ::= <object-chain> "=" <value>
```

**Examples:**
```javascript
// Element access
window("Main").button("OK").click();

// Property access
window("Main").textField("Name").text = "John Doe";

// Method chaining
window("Settings")
    .textField("Email").setValue("user@example.com")
    .button("Save").click();

// Conditionals
if (window("Main").checkbox("Remember").checked) {
    savePreferences();
}

// Loops
buttons = window("Main").buttons();
for (btn in buttons) {
    if (btn.enabled) {
        btn.click();
    }
}
```

### 4.2 Natural Style

**Philosophy:** English-like sentences, verb-first, AppleScript-familiar

**Syntax Rules:**
- Verb comes first
- Objects referenced with "of" or "in"
- Strings in double quotes
- Modifiers use "with" or "without"
- Line-oriented (no semicolons)

**Grammar:**
```bnf
<statement>     ::= <command> <object-spec> <modifiers>?
<command>       ::= "click" | "set" | "get" | "activate" | <custom-verb>
<object-spec>   ::= <element-ref> <location>?
<element-ref>   ::= <type> <string-literal>
<location>      ::= "of" <object-spec> | "in" <object-spec>
<modifiers>     ::= "to" <value> | "with" <properties>
```

**Examples:**
```applescript
click button "OK" of window "Main"

set text of field "Name" to "John Doe" in window "Main"

get value of slider "Volume" of window "Settings"

if checkbox "Remember Me" is checked then
    save preferences
end if

repeat with btn in buttons of window "Main"
    if btn is enabled then
        click btn
    end if
end repeat
```

### 4.3 Classic Style

**Philosophy:** AppleScript compatibility, tell blocks, traditional

**Syntax Rules:**
- Tell blocks define scope
- Commands indented within blocks
- "end tell" closes blocks
- Objects referenced by type and name
- Properties use "of" syntax

**Grammar:**
```bnf
<statement>     ::= <tell-block> | <simple-command>
<tell-block>    ::= "tell" <object-spec> <statements> "end tell"
<simple-command> ::= <command> <object-spec> <modifiers>?
<object-spec>   ::= <application> | <window> | <element>
```

**Examples:**
```applescript
tell application "MyApp"
    tell window "Main"
        click button "OK"
        set value of field "Name" to "John Doe"
    end tell
end tell

tell window "Settings" of application "MyApp"
    set value of slider "Volume" to 75
    click button "Apply"
end tell
```

### 4.4 Type System

#### 4.4.1 Basic Types
```
string      - Text values: "Hello"
integer     - Whole numbers: 42
real        - Floating point: 3.14
boolean     - true or false
color       - RGBA: rgb(255, 0, 0, 128)
point       - x,y coordinates: point(100, 200)
rect        - Rectangle: rect(10, 20, 300, 400)
```

#### 4.4.2 Object Types
```
Application - Top-level application object
Window      - Window instances
Button      - Button elements
TextField   - Text input fields
Slider      - Slider controls
Checkbox    - Checkbox elements
List        - List/array of elements
```

#### 4.4.3 Type Coercion Rules
```
string → integer   : Parse numeric value
integer → string   : Convert to text
real → integer     : Truncate decimals
boolean → string   : "true" or "false"
color → string     : "rgba(r, g, b, a)"
```

### 4.5 Standard Library

#### 4.5.1 Built-in Functions
```javascript
// System functions
delay(seconds)              // Pause execution
log(message)                // Write to log
beep()                      // System beep

// String functions
length(string)              // String length
substring(str, start, end)  // Extract substring
uppercase(string)           // Convert to uppercase
lowercase(string)           // Convert to lowercase

// Math functions
abs(number)                 // Absolute value
round(number)               // Round to nearest integer
min(a, b)                   // Minimum value
max(a, b)                   // Maximum value

// List functions
count(list)                 // Number of items
item(index, list)           // Get item at index
first(list)                 // First item
last(list)                  // Last item
```

#### 4.5.2 Global Objects
```javascript
app                         // Current application
system                      // System information
clipboard                   // System clipboard
```

---

## 5. Recording System

### 5.1 Recording Architecture

#### 5.1.1 Recording Trigger
```cpp
// Recording is controlled globally
UIScriptRecorder::StartRecording()  // Begin recording
UIScriptRecorder::StopRecording()   // End recording
UIScriptRecorder::IsRecording()     // Check status
```

#### 5.1.2 Automatic Recording Points
```cpp
// In UltraCanvasWindow::DispatchEvent()
bool DispatchEvent(const UCEvent& event) {
    UltraCanvasUIElement* target = FindEventTarget(event);
    
    // AUTOMATIC RECORDING HAPPENS HERE
    UIScriptRecorder::RecordEventIfEnabled(event, target);
    
    // Normal event handling
    if (target) {
        return target->HandleEvent(event);
    }
    return false;
}
```

#### 5.1.3 Recording Decision Tree
```
UCEvent received
    │
    ├─ Is recording enabled? ────── NO → Skip
    │                               YES ↓
    ├─ Is target element valid? ─── NO → Skip
    │                               YES ↓
    ├─ Is element scriptable? ────── NO → Skip
    │                               YES ↓
    ├─ Is element recordable? ────── NO → Skip
    │                               YES ↓
    ├─ Should record event type? ─── NO → Skip
    │                               YES ↓
    └─ CREATE UIScriptAction → Add to list → Call live callback
```

### 5.2 Recordable Events

#### 5.2.1 Always Recorded
```
UCEventType::MouseUp         - Button clicks
UCEventType::MouseDoubleClick - Double clicks
UCEventType::TextInput       - Text typing
UCEventType::KeyDown         - Keyboard shortcuts
UCEventType::ValueChanged    - Slider/control changes
UCEventType::SelectionChanged - List/combo selections
```

#### 5.2.2 Optionally Recorded
```
UCEventType::MouseMove       - Mouse movements (if enabled)
UCEventType::MouseDown       - Mouse press (if enabled)
```

#### 5.2.3 Never Recorded
```
UCEventType::MouseEnter      - Hover states
UCEventType::MouseLeave      - Hover states
UCEventType::WindowRepaint   - Internal events
UCEventType::WindowResize    - Internal events
```

### 5.3 UIScriptAction Structure

```cpp
struct UIScriptAction {
    // Element identification
    std::string elementName;        // "OKButton"
    std::string elementClass;       // "Button"
    std::string parentName;         // "MainWindow"
    
    // Event information
    UCEventType eventType;          // Original event type
    
    // Event data
    int x, y;                       // Mouse coordinates
    UCMouseButton button;           // Which mouse button
    UCKeys key;                     // Keyboard key
    char character;                 // Typed character
    std::string text;               // Text input
    bool ctrl, shift, alt, meta;    // Modifiers
    
    // Timing
    std::chrono::steady_clock::time_point timestamp;
    
    // Script generation
    std::string ToUIScript(UIScriptSyntaxStyle style);
};
```

### 5.4 Script Generation

#### 5.4.1 Action Optimization
```cpp
// Combine consecutive text inputs
"type 'H'" + "type 'e'" + "type 'l'" + "type 'l'" + "type 'o'"
    ↓
"type 'Hello'"

// Combine mouse movements
"move to (100, 100)" + "move to (105, 102)" + "move to (110, 105)"
    ↓
"move to (110, 105)"

// Remove redundant actions
"click button 'OK'" + "click button 'OK'"
    ↓
"click button 'OK'"
```

#### 5.4.2 Context Grouping
```javascript
// Modern style - group by parent
window("Main").button("OK").click();
window("Main").textField("Name").setValue("John");
    ↓
window("Main") {
    button("OK").click();
    textField("Name").setValue("John");
}

// Natural style - group in tell blocks
click button "OK" of window "Main"
set text of field "Name" to "John" in window "Main"
    ↓
tell window "Main"
    click button "OK"
    set text of field "Name" to "John"
end tell
```

### 5.5 Live Recording Callback

```cpp
// Real-time script display
UIScriptRecorder::SetLiveCallback([](const UIScriptAction& action) {
    std::string code = action.ToUIScript(UIScriptSyntaxStyle::Modern);
    scriptEditorWindow->AppendLine(code);
});
```

---

## 6. Scripting Dictionary

### 6.1 Dictionary Purpose

The **Scripting Dictionary** defines:
- What properties an element has
- What commands an element supports
- Parameter types and return values
- Human-readable descriptions
- Access permissions (read-only, read-write)

### 6.2 Dictionary Format (XML)

```xml
<?xml version="1.0" encoding="UTF-8"?>
<dictionary>
    <suite name="Standard Suite" description="Common commands and classes">
        
        <!-- Element Class Definition -->
        <class name="Button" code="butN" description="A clickable button">
            
            <!-- Properties -->
            <property name="text" code="pTXT" type="string" access="rw">
                <description>The button's label text</description>
            </property>
            
            <property name="enabled" code="pENA" type="boolean" access="rw">
                <description>Whether the button is enabled</description>
            </property>
            
            <property name="position" code="pPOS" type="point" access="rw">
                <description>Button position as x,y coordinates</description>
            </property>
            
            <property name="bounds" code="pBND" type="rect" access="r">
                <description>Button bounding rectangle</description>
            </property>
            
            <!-- Commands -->
            <command name="click" code="cliC" description="Simulate a click">
                <result type="void"/>
            </command>
            
            <command name="enable" code="enaB" description="Enable the button">
                <result type="void"/>
            </command>
            
            <command name="disable" code="disB" description="Disable the button">
                <result type="void"/>
            </command>
        </class>
        
        <!-- TextField Class -->
        <class name="TextField" code="txtF" description="Text input field">
            <property name="text" code="pTXT" type="string" access="rw">
                <description>The field's text content</description>
            </property>
            
            <property name="placeholder" code="pPLH" type="string" access="rw">
                <description>Placeholder text when empty</description>
            </property>
            
            <command name="clear" code="clrT" description="Clear the text">
                <result type="void"/>
            </command>
            
            <command name="setValue" code="setV" description="Set text value">
                <parameter name="value" code="valu" type="string" description="New text"/>
                <result type="void"/>
            </command>
        </class>
        
    </suite>
    
    <!-- Application-specific suite -->
    <suite name="MyApp Suite" description="MyApp specific commands">
        <class name="CustomElement" code="cusE" description="Application-specific element">
            <!-- Custom properties and commands -->
        </class>
    </suite>
</dictionary>
```

### 6.3 Dictionary Generation

```cpp
// Automatic dictionary generation from element metadata
std::string UltraCanvasScriptableApplication::GenerateDictionary() {
    DictionaryBuilder builder;
    builder.BeginDictionary(GetApplicationScriptName());
    
    builder.BeginSuite("Standard Suite");
    
    // Enumerate all scriptable element types
    for (auto& elementType : GetScriptableElementTypes()) {
        builder.BeginClass(elementType->GetScriptClass());
        
        // Add properties
        for (auto& prop : elementType->GetScriptableProperties()) {
            builder.AddProperty(
                prop.name,
                prop.type,
                prop.readable,
                prop.writable,
                prop.description
            );
        }
        
        // Add commands
        for (auto& cmd : elementType->GetScriptableCommands()) {
            builder.AddCommand(
                cmd.name,
                cmd.parameters,
                cmd.returnType,
                cmd.description
            );
        }
        
        builder.EndClass();
    }
    
    builder.EndSuite();
    builder.EndDictionary();
    
    return builder.GetXML();
}
```

### 6.4 Dictionary Browser UI

The Script Editor includes a **Dictionary Browser** that:
- Lists all scriptable applications
- Shows element classes in tree view
- Displays properties and commands
- Provides syntax examples
- Allows drag-and-drop code insertion

---

## 7. API Definitions

### 7.1 IUIScriptable Interface

```cpp
// include/UltraCanvasScriptableElement.h

class IUIScriptable {
public:
    virtual ~IUIScriptable() = default;
    
    // ===== REQUIRED METHODS =====
    virtual std::string GetScriptName() const = 0;
    virtual std::string GetScriptClass() const = 0;
    
    // ===== OPTIONAL METHODS =====
    virtual std::string GetScriptParent() const { return ""; }
    virtual bool IsScriptRecordable() const { return true; }
    virtual std::string GetScriptDescription() const { return ""; }
    
    // ===== PROPERTY ACCESS =====
    virtual std::vector<std::string> GetScriptPropertyNames() const { return {}; }
    virtual std::string GetScriptPropertyValue(const std::string& name) const { return ""; }
    virtual bool SetScriptPropertyValue(const std::string& name, const std::string& value) { return false; }
    
    // ===== COMMAND EXECUTION =====
    virtual std::vector<std::string> GetScriptCommandNames() const { return {}; }
    virtual std::string ExecuteScriptCommand(const std::string& name, const std::vector<std::string>& params) { return ""; }
};
```

### 7.2 UIScriptRecorder API

```cpp
// include/UltraCanvasScriptRecorder.h

class UIScriptRecorder {
public:
    // ===== RECORDING CONTROL =====
    static void StartRecording();
    static void StopRecording();
    static void PauseRecording();
    static void ResumeRecording();
    static bool IsRecording();
    static void ClearRecording();
    
    // ===== SETTINGS =====
    static void SetRecordMouseMoves(bool enable);
    static void SetRecordTimingDelays(bool enable);
    static void SetMouseMoveThreshold(int pixels);
    static void SetSyntaxStyle(UIScriptSyntaxStyle style);
    
    // ===== CALLBACKS =====
    static void SetLiveCallback(std::function<void(const UIScriptAction&)> callback);
    static void SetErrorCallback(std::function<void(const std::string&)> callback);
    
    // ===== CORE RECORDING FUNCTION =====
    // Called automatically by event dispatcher
    static void RecordEventIfEnabled(const UCEvent& event, UltraCanvasUIElement* target);
    
    // ===== SCRIPT GENERATION =====
    static std::string GenerateScript(UIScriptSyntaxStyle style = UIScriptSyntaxStyle::Modern);
    static std::vector<UIScriptAction> GetActions();
    static void OptimizeActions();  // Remove redundant actions
    
    // ===== IMPORT/EXPORT =====
    static bool SaveRecording(const std::string& filepath);
    static bool LoadRecording(const std::string& filepath);
};
```

### 7.3 UIScriptParser API

```cpp
// include/UIScriptParser.h

class UIScriptParser {
public:
    // ===== PARSING =====
    static UIScriptAST Parse(const std::string& script, UIScriptSyntaxStyle style);
    static UIScriptAST ParseFile(const std::string& filepath);
    
    // ===== VALIDATION =====
    static bool Validate(const std::string& script, std::string& error);
    static bool CheckSyntax(const std::string& script, UIScriptSyntaxStyle style, std::string& error);
    
    // ===== SYNTAX DETECTION =====
    static UIScriptSyntaxStyle DetectSyntaxStyle(const std::string& script);
    
    // ===== CONVERSION =====
    static std::string ConvertSyntax(const std::string& script, 
                                     UIScriptSyntaxStyle from, 
                                     UIScriptSyntaxStyle to);
};
```

### 7.4 UIScriptExecutor API

```cpp
// include/UIScriptExecutor.h

class UIScriptExecutor {
public:
    // ===== EXECUTION =====
    static UIScriptResult Execute(const UIScriptAST& ast);
    static UIScriptResult ExecuteScript(const std::string& script, UIScriptSyntaxStyle style);
    static UIScriptResult ExecuteFile(const std::string& filepath);
    
    // ===== STEP EXECUTION (DEBUGGING) =====
    static void StepInto();
    static void StepOver();
    static void StepOut();
    static void Continue();
    static void Pause();
    
    // ===== BREAKPOINTS =====
    static void SetBreakpoint(int lineNumber);
    static void ClearBreakpoint(int lineNumber);
    static void ClearAllBreakpoints();
    
    // ===== VARIABLE INSPECTION =====
    static std::string GetVariableValue(const std::string& name);
    static std::map<std::string, std::string> GetAllVariables();
    
    // ===== CONTEXT =====
    static void SetExecutionContext(UltraCanvasApplication* app);
    static UltraCanvasApplication* GetExecutionContext();
};
```

### 7.5 UIScriptResult Structure

```cpp
struct UIScriptResult {
    bool success;
    std::string value;          // Return value (as string)
    std::string errorMessage;   // Error description
    int errorLine;              // Line number where error occurred
    std::string errorType;      // "SyntaxError", "RuntimeError", "TypeError"
    
    // Execution statistics
    double executionTime;       // In seconds
    int commandsExecuted;       // Number of commands run
};
```

---

## 8. Implementation Details

### 8.1 File Locations

```
UltraCanvas/
├── include/
│   ├── UltraCanvasScriptRecorder.h         # Recording system
│   ├── UltraCanvasScriptParser.h           # Parser
│   ├── UltraCanvasScriptExecutor.h         # Executor
│   ├── UltraCanvasScriptableElement.h      # Scriptable interface
│   ├── UIScriptTypes.h                     # Type definitions
│   └── UIScriptStandardLibrary.h           # Built-in functions
│
├── core/
│   ├── UltraCanvasScriptRecorder.cpp       # Recording implementation
│   ├── UltraCanvasScriptParser.cpp         # Parser implementation
│   ├── UltraCanvasScriptExecutor.cpp       # Executor implementation
│   └── UIScriptStandardLibrary.cpp         # Standard library
│
├── Apps/
│   └── ScriptEditor/                        # Script Editor application
│       ├── ScriptEditorApplication.h/cpp
│       ├── Components/
│       │   ├── CodeEditor.h/cpp            # Uses UltraCanvasTextArea
│       │   ├── DictionaryBrowser.h/cpp     # Dictionary viewer
│       │   ├── EventLogPanel.h/cpp         # Event log
│       │   └── ResultPanel.h/cpp           # Output display
│       └── main.cpp
│
└── OS/
    ├── Linux/
    │   └── UltraCanvasScriptBridge.cpp     # Linux-specific bridging
    ├── MacOS/
    │   └── UltraCanvasScriptBridge.mm      # macOS bridging (NSAppleScript compatibility)
    └── Windows/
        └── UltraCanvasScriptBridge.cpp     # Windows bridging
```

### 8.2 Integration with UCEvent System

```cpp
// include/UltraCanvasWindow.h

class UltraCanvasWindow : public UltraCanvasWindowBase {
protected:
    bool DispatchEventToChildren(const UCEvent& event) override {
        UltraCanvasUIElement* target = FindEventTarget(event);
        
        if (!target) return false;
        
        // ===== AUTOMATIC RECORDING INTEGRATION =====
        // This is the ONLY place recording happens
        #ifdef UISCRIPT_ENABLED
        UIScriptRecorder::RecordEventIfEnabled(event, target);
        #endif
        
        // Normal event handling
        return target->HandleEvent(event);
    }
};
```

### 8.3 Element Registration

```cpp
// Elements automatically register when created if they implement IUIScriptable

class UltraCanvasButton : public UltraCanvasScriptableElement {
public:
    UltraCanvasButton(const std::string& name, int id, int x, int y, int w, int h)
        : UltraCanvasScriptableElement(name, id, x, y, w, h) {
        
        // Set script metadata
        SetScriptName(name);
        SetScriptClass("Button");
        
        // Automatically registered in parent window/application
        // No manual registration needed!
    }
};
```

### 8.4 Property and Command Registration

```cpp
// Two approaches: Automatic (via introspection) or Manual (via registration)

// Approach 1: AUTOMATIC (Recommended for simple properties)
class UltraCanvasButton : public UltraCanvasScriptableElement {
public:
    // Public getters/setters are automatically scriptable
    std::string GetText() const { return text; }
    void SetText(const std::string& t) { text = t; }
    
    bool IsEnabled() const { return enabled; }
    void SetEnabled(bool e) { enabled = e; }
    
    // Methods with specific signatures are automatically commands
    void Click() { PerformClick(); }
};

// Approach 2: MANUAL (For complex cases or fine control)
class UltraCanvasButton : public UltraCanvasScriptableElement {
protected:
    void InitializeScriptingSupport() override {
        // Register properties
        RegisterScriptProperty("text", "string",
            [this]() { return GetText(); },
            [this](const std::string& v) { SetText(v); }
        );
        
        RegisterScriptProperty("enabled", "boolean",
            [this]() { return IsEnabled() ? "true" : "false"; },
            [this](const std::string& v) { SetEnabled(v == "true"); }
        );
        
        // Register commands
        RegisterScriptCommand("click",
            [this](const std::vector<std::string>&) { Click(); return ""; }
        );
    }
};
```

### 8.5 Compilation Flags

```cmake
# CMakeLists.txt

option(UISCRIPT_ENABLE "Enable UIScript support" ON)
option(UISCRIPT_RECORDING "Enable script recording" ON)
option(UISCRIPT_JIT "Enable JIT compilation" OFF)
option(UISCRIPT_DEBUGGER "Enable script debugger" ON)

if(UISCRIPT_ENABLE)
    add_definitions(-DUISCRIPT_ENABLED)
    
    if(UISCRIPT_RECORDING)
        add_definitions(-DUISCRIPT_RECORDING_ENABLED)
    endif()
    
    if(UISCRIPT_JIT)
        add_definitions(-DUISCRIPT_JIT_ENABLED)
    endif()
endif()
```

---

## 9. File Structure

### 9.1 Core Headers

#### 9.1.1 UIScriptTypes.h
```cpp
// Fundamental types and enums

enum class UIScriptSyntaxStyle {
    Modern,
    Natural,
    Classic
};

enum class UIScriptValueType {
    Void,
    String,
    Integer,
    Real,
    Boolean,
    Color,
    Point,
    Rect,
    Object,
    List
};

struct UIScriptValue {
    UIScriptValueType type;
    std::string stringValue;
    int intValue;
    double realValue;
    bool boolValue;
    // ... etc
};
```

#### 9.1.2 UIScriptAST.h
```cpp
// Abstract Syntax Tree definitions

enum class ASTNodeType {
    Program,
    Statement,
    Expression,
    Identifier,
    Literal,
    BinaryOp,
    UnaryOp,
    FunctionCall,
    PropertyAccess,
    Assignment,
    IfStatement,
    WhileLoop,
    ForLoop,
    TellBlock
};

class ASTNode {
public:
    ASTNodeType type;
    std::vector<std::shared_ptr<ASTNode>> children;
    UIScriptValue value;
    int lineNumber;
    
    virtual ~ASTNode() = default;
};
```

### 9.2 Implementation Files

#### 9.2.1 UltraCanvasScriptRecorder.cpp
**Key Functions:**
- `RecordEventIfEnabled()` - Main recording function
- `ShouldRecordEvent()` - Event filtering
- `CreateAction()` - UCEvent → UIScriptAction conversion
- `GenerateScript()` - Action list → script code
- `OptimizeActions()` - Remove redundant actions

#### 9.2.2 UltraCanvasScriptParser.cpp
**Key Functions:**
- `Parse()` - Text → AST conversion
- `Tokenize()` - Lexical analysis
- `ParseExpression()` - Expression parsing
- `ParseStatement()` - Statement parsing
- `DetectSyntaxStyle()` - Auto-detect syntax
- `Validate()` - Syntax checking

#### 9.2.3 UltraCanvasScriptExecutor.cpp
**Key Functions:**
- `Execute()` - AST → execution
- `EvaluateNode()` - Recursive AST evaluation
- `ResolveElement()` - Find element by script reference
- `CallCommand()` - Execute element command
- `GetProperty()` / `SetProperty()` - Property access

---

## 10. Integration Points

### 10.1 Event System Integration

**Location:** `include/UltraCanvasWindow.h`

```cpp
bool DispatchEventToChildren(const UCEvent& event) override {
    // ... find target element ...
    
    #ifdef UISCRIPT_RECORDING_ENABLED
    UIScriptRecorder::RecordEventIfEnabled(event, target);
    #endif
    
    return target->HandleEvent(event);
}
```

### 10.2 Application Integration

**Location:** User application code

```cpp
class MyApplication : public UltraCanvasApplication {
public:
    void Initialize() override {
        // Enable UIScript
        SetApplicationScriptName("MyApp");
        SetApplicationVersion("1.0.0");
        
        // Create scriptable windows
        CreateMainWindow();
    }
    
    void CreateMainWindow() {
        auto window = CreateWindow(config);
        window->SetScriptName("MainWindow");
        
        // All children are automatically scriptable
        auto button = std::make_shared<UltraCanvasButton>("OK", 1, 10, 10, 80, 30);
        button->SetScriptName("OKButton");
        window->AddChild(button);
    }
};
```

### 10.3 Element Integration

**Minimal Implementation:**
```cpp
class MyCustomElement : public UltraCanvasScriptableElement {
public:
    MyCustomElement(/* args */)
        : UltraCanvasScriptableElement(/* args */) {
        SetScriptClass("CustomElement");
    }
    
    // That's it! Now recordable and scriptable.
};
```

**Full Implementation:**
```cpp
class MyCustomElement : public UltraCanvasScriptableElement {
protected:
    void InitializeScriptingSupport() override {
        // Register custom properties
        RegisterScriptProperty("customValue", "integer",
            [this]() { return std::to_string(value); },
            [this](const std::string& v) { value = std::stoi(v); }
        );
        
        // Register custom commands
        RegisterScriptCommand("performAction",
            [this](const std::vector<std::string>& params) {
                DoCustomAction();
                return "Action performed";
            }
        );
    }
};
```

---

## 11. Examples

### 11.1 Simple Recording Example

```cpp
// Start recording
UIScriptRecorder::StartRecording();

// User performs actions:
//   1. Clicks "Open" button
//   2. Types "myfile.txt" in filename field
//   3. Clicks "OK" button

// Stop recording
UIScriptRecorder::StopRecording();

// Generate script
std::string script = UIScriptRecorder::GenerateScript(UIScriptSyntaxStyle::Modern);

// Result:
// window("Main").button("Open").click();
// window("Main").textField("Filename").setValue("myfile.txt");
// window("Main").button("OK").click();
```

### 11.2 Script Execution Example

```cpp
// Define script
std::string script = R"(
    window("Settings").slider("Volume").setValue(75);
    window("Settings").checkbox("Mute").check();
    window("Settings").button("Apply").click();
)";

// Execute
UIScriptResult result = UIScriptExecutor::ExecuteScript(
    script, 
    UIScriptSyntaxStyle::Modern
);

if (result.success) {
    std::cout << "Script executed successfully" << std::endl;
    std::cout << "Commands: " << result.commandsExecuted << std::endl;
    std::cout << "Time: " << result.executionTime << "s" << std::endl;
} else {
    std::cerr << "Error: " << result.errorMessage << std::endl;
    std::cerr << "Line: " << result.errorLine << std::endl;
}
```

### 11.3 Dictionary Generation Example

```cpp
// Application generates dictionary automatically
std::string dictionary = app->GenerateScriptingDictionary();

// Save to file
std::ofstream file("MyApp.sdef");
file << dictionary;
file.close();

// Dictionary can be viewed in Script Editor's Dictionary Browser
```

### 11.4 Live Recording Example

```cpp
// Script Editor setup
UIScriptRecorder::SetLiveCallback([this](const UIScriptAction& action) {
    // Convert action to code
    std::string code = action.ToUIScript(UIScriptSyntaxStyle::Modern);
    
    // Append to editor in real-time
    codeEditor->AppendText(code + "\n");
    
    // Scroll to bottom
    codeEditor->ScrollToEnd();
});

// Start recording
UIScriptRecorder::StartRecording();

// Now as user performs actions, they appear in editor immediately
```

### 11.5 Conditional Script Example

```javascript
// Modern syntax
if (window("Main").checkbox("RememberMe").checked) {
    savePreferences();
    showNotification("Preferences saved");
}

// Iterate over elements
buttons = window("Toolbar").buttons();
for (btn in buttons) {
    if (!btn.enabled) {
        btn.enable();
    }
}
```

### 11.6 Natural Language Example

```applescript
tell application "MyApp"
    if checkbox "Auto-save" of window "Settings" is checked then
        set interval of timer "Save" to 60
    else
        stop timer "Save"
    end if
    
    repeat with item in list "Recent Files"
        if item is selected then
            open document item
        end if
    end repeat
end tell
```

---

## 12. Development Guidelines

### 12.1 Making Elements Scriptable

**Checklist:**

1. ✅ Inherit from `UltraCanvasScriptableElement`
2. ✅ Set script name in constructor: `SetScriptName("MyElement")`
3. ✅ Set script class: `SetScriptClass("CustomElement")`
4. ✅ Override `InitializeScriptingSupport()` if custom properties/commands needed
5. ✅ Use standard naming: Properties are nouns, commands are verbs

**Best Practices:**

- **Property names**: lowercase, descriptive (e.g., "text", "enabled", "value")
- **Command names**: verbs (e.g., "click", "open", "save", "close")
- **Script names**: CamelCase, unique within parent (e.g., "SaveButton", "EmailField")
- **Script classes**: PascalCase, semantic type (e.g., "Button", "TextField", "Slider")

### 12.2 Recording Considerations

**What to Record:**
- User-initiated actions (clicks, typing, selections)
- State changes from user interaction
- Commands that modify application state

**What NOT to Record:**
- Hover events (MouseEnter/MouseLeave)
- Focus changes
- Window repaints
- Internal state updates

**Optimization:**
- Combine consecutive text inputs: `type("H") + type("e")` → `type("He")`
- Remove duplicate actions: `click() + click()` → `click()`
- Group actions by context: Multiple operations on same window

### 12.3 Error Handling

```cpp
// Always validate script execution
UIScriptResult result = UIScriptExecutor::ExecuteScript(script);

if (!result.success) {
    std::cerr << "Error on line " << result.errorLine << ": " 
              << result.errorMessage << std::endl;
    
    // Show error in UI
    ShowErrorDialog(result.errorMessage);
}
```

### 12.4 Testing

**Unit Tests:**
```cpp
TEST(UIScriptRecorder, BasicRecording) {
    UIScriptRecorder::StartRecording();
    
    // Simulate event
    UCEvent event;
    event.type = UCEventType::MouseUp;
    event.x = 100;
    event.y = 50;
    
    auto button = std::make_shared<UltraCanvasButton>("Test", 1, 90, 40, 100, 30);
    button->SetScriptName("TestButton");
    
    UIScriptRecorder::RecordEventIfEnabled(event, button.get());
    
    std::string script = UIScriptRecorder::GenerateScript();
    EXPECT_TRUE(script.find("TestButton") != std::string::npos);
    EXPECT_TRUE(script.find("click") != std::string::npos);
}

TEST(UIScriptParser, ModernSyntax) {
    std::string script = "window(\"Main\").button(\"OK\").click();";
    std::string error;
    
    bool valid = UIScriptParser::Validate(script, error);
    EXPECT_TRUE(valid);
    EXPECT_TRUE(error.empty());
}

TEST(UIScriptExecutor, SimpleExecution) {
    auto app = CreateTestApplication();
    UIScriptExecutor::SetExecutionContext(app.get());
    
    std::string script = "button(\"Test\").click();";
    UIScriptResult result = UIScriptExecutor::ExecuteScript(script);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.commandsExecuted, 1);
}
```

### 12.5 Performance Considerations

**Recording:**
- Recording overhead: < 1ms per event
- Memory: ~100 bytes per action
- Typical session: 1000 actions = 100KB

**Execution:**
- Parsing: ~1ms per 100 lines
- Execution: ~1ms per command (depends on command complexity)
- No significant performance impact on normal operations

**Optimization:**
- Use compiled scripts for frequently-run operations
- Cache parsed AST for repeated execution
- Batch commands when possible

### 12.6 Security

**Script Validation:**
- Always validate scripts before execution
- Sandbox execution environment
- Limit access to system resources
- No arbitrary code execution (no `eval()`)

**User Permissions:**
- Request permission before recording
- Notify user when recording is active
- Allow users to review recorded scripts
- Provide clear indication of script execution

### 12.7 Debugging

**Script Debugger Features:**
- Breakpoints on line numbers
- Step-by-step execution
- Variable inspection
- Call stack viewing
- Expression evaluation

**Debug Output:**
```cpp
// Enable debug logging
UIScriptExecutor::SetDebugMode(true);

// Execution will log:
// [UIScript] Executing line 1: window("Main").button("OK").click()
// [UIScript] Resolved window: Main
// [UIScript] Resolved button: OK
// [UIScript] Executing command: click
// [UIScript] Command returned: void
```

---

## 13. Future Enhancements

### 13.1 Planned Features

**Phase 1 (Current):**
- ✅ Basic recording and playback
- ✅ Three syntax styles
- ✅ Script Editor application
- ✅ Dictionary browser

**Phase 2:**
- 🔲 JIT compilation for performance
- 🔲 Advanced debugger with breakpoints
- 🔲 Script libraries and imports
- 🔲 Remote scripting (control apps over network)

**Phase 3:**
- 🔲 Visual script builder (flowchart-style)
- 🔲 AI-assisted script generation
- 🔲 Script marketplace/repository
- 🔲 Cross-application scripting

### 13.2 Extension Points

**Custom Syntax Styles:**
```cpp
// Register custom syntax parser
UIScriptParser::RegisterSyntaxHandler(
    "PythonStyle",
    new PythonSyntaxParser()
);
```

**Custom Commands:**
```cpp
// Register global command
UIScriptStandardLibrary::RegisterCommand(
    "sendEmail",
    [](const std::vector<std::string>& params) {
        std::string to = params[0];
        std::string subject = params[1];
        std::string body = params[2];
        // Send email...
        return "Email sent";
    }
);
```

**Custom Types:**
```cpp
// Register custom type
UIScriptTypes::RegisterType(
    "Image",
    UIScriptValueType::Object,
    new ImageTypeHandler()
);
```

---

## 14. Appendices

### 14.1 Keyword Reference

**Modern Style Keywords:**
```
if, else, for, while, in, return, true, false, null
function, var, let, const
```

**Natural Style Keywords:**
```
tell, end, if, then, else, repeat, with, to, of, in
is, is not, contains, starts, ends
and, or, not
```

**Classic Style Keywords:**
```
tell, end tell, if, then, else if, end if
repeat, end repeat, with, from, to, by
set, get, copy
return, error
```

### 14.2 Built-in Functions Reference

```javascript
// System
delay(seconds)              // Pause execution
beep()                      // System beep sound
log(message)                // Write to log

// String
length(string)              // String length
uppercase(string)           // Convert to upper
lowercase(string)           // Convert to lower
substring(str, start, end)  // Extract substring
replace(str, old, new)      // Replace text

// Math
abs(num)                    // Absolute value
round(num)                  // Round to integer
floor(num)                  // Round down
ceil(num)                   // Round up
min(a, b)                   // Minimum
max(a, b)                   // Maximum
random()                    // Random 0-1

// List
count(list)                 // Number of items
first(list)                 // First item
last(list)                  // Last item
item(index, list)           // Get by index
contains(list, value)       // Check membership

// Conversion
toString(value)             // Convert to string
toInt(value)                // Convert to integer
toReal(value)               // Convert to real
toBool(value)               // Convert to boolean
```

### 14.3 Standard Properties Reference

**All Elements:**
```
name          (string, r)   - Element script name
class         (string, r)   - Element type
parent        (object, r)   - Parent element
bounds        (rect, r)     - Bounding rectangle
position      (point, rw)   - X,Y coordinates
size          (point, rw)   - Width, height
visible       (bool, rw)    - Visibility state
enabled       (bool, rw)    - Enabled state
```

**Button:**
```
text          (string, rw)  - Button label
```

**TextField:**
```
text          (string, rw)  - Text content
placeholder   (string, rw)  - Placeholder text
readOnly      (bool, rw)    - Read-only state
```

**Slider:**
```
value         (real, rw)    - Current value
minimum       (real, rw)    - Minimum value
maximum       (real, rw)    - Maximum value
```

**Checkbox:**
```
checked       (bool, rw)    - Checked state
text          (string, rw)  - Label text
```

### 14.4 Standard Commands Reference

**All Elements:**
```
click()                     - Simulate click
enable()                    - Enable element
disable()                   - Disable element
show()                      - Show element
hide()                      - Hide element
focus()                     - Give focus
```

**Button:**
```
click()                     - Click button
press()                     - Press (same as click)
```

**TextField:**
```
clear()                     - Clear text
setValue(text)              - Set text value
append(text)                - Append text
selectAll()                 - Select all text
```

**Window:**
```
close()                     - Close window
minimize()                  - Minimize window
maximize()                  - Maximize window
restore()                   - Restore window
bringToFront()              - Bring to front
```

**Application:**
```
activate()                  - Activate app
quit()                      - Quit app
launch()                    - Launch app
```

---

## 15. Version History

**Version 1.0.0 (2024-12-19)**
- Initial specification
- Three syntax styles defined
- Recording system architecture
- Core API definitions
- Implementation guidelines

---

## 16. References

**Related Documents:**
- UltraCanvas Development Guidelines
- UltraCanvas Event System Documentation
- UltraCanvas UI Element Architecture
- AppleScript Language Guide (for compatibility reference)

**External Standards:**
- XML Schema for Scripting Dictionaries (SDEF)
- ECMAScript Language Specification (for Modern syntax inspiration)
- AppleScript Language Guide (for Natural/Classic syntax)

---

**Document End**
