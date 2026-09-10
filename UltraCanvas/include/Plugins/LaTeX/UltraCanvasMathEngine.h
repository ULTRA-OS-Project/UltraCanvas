// include/Plugins/LaTeX/UltraCanvasMathEngine.h
// The facade of the native math engine: owns the math font, runs the parser
// and the layout, and hands back a box tree with its dimensions. This is
// what the LaTeX view and, later, the text stack call; drawing the result
// is UltraCanvasMathRender.h's job so that typesetting never needs a render
// context and can run on any thread.
//
// Version: 1.0.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/LaTeX/UltraCanvasMathFont.h"
#include "Plugins/LaTeX/UltraCanvasMathLayout.h"
#include "Plugins/LaTeX/UltraCanvasMathModel.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

struct MathTypesetOptions {
    float fontSize = 20.f;                     // pixels per em
    MathColor color = kMathColorBlack;
    float maxWidth = 0.f;                      // >0: break long top-level rows
    MathStyle style = MathStyle::Display();
    IMathTextFallback* textFallback = nullptr; // measures runs the math font lacks
    std::string preamble;                      // \newcommand etc. parsed before the formula
};

struct MathTypesetResult {
    MathBoxPtr root;                           // null when the engine has no font
    float width = 0.f, height = 0.f, depth = 0.f;   // pixels; baseline is `height` below the top
    std::vector<MathDiagnostic> diagnostics;   // parse / layout problems, may be non-empty with a root
    bool HasErrors() const { return !diagnostics.empty(); }
    float TotalHeight() const { return height + depth; }
};

class UltraCanvasMathEngine {
public:
    UltraCanvasMathEngine();
    ~UltraCanvasMathEngine();
    UltraCanvasMathEngine(const UltraCanvasMathEngine&) = delete;
    UltraCanvasMathEngine& operator=(const UltraCanvasMathEngine&) = delete;

    // Loads the math font from an explicit file.
    bool LoadFont(const std::string& path);
    // Looks for latinmodern-math.otf (or any OpenType math font listed in
    // `candidates`, first match wins) in the given directories.
    bool LoadFontFrom(const std::vector<std::string>& searchDirs,
                      const std::vector<std::string>& candidates = {"latinmodern-math.otf"});
    bool IsReady() const;
    const std::string& GetLastError() const;
    const UltraCanvasMathFont& GetFont() const;

    // Parses and lays out `latex` (math mode). Never throws; a formula with
    // errors still yields a root box with the recoverable parts and red
    // markers, plus diagnostics.
    MathTypesetResult Typeset(const std::string& latex, const MathTypesetOptions& options) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// The process-wide engine the LaTeX view uses; created on first call, font
// loaded by the caller (UltraCanvasLaTeXBackend does it from the framework's
// resource directories).
UltraCanvasMathEngine& GetSharedMathEngine();

} // namespace UltraCanvas
