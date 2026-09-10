// include/Plugins/Documents/LaTeX/UltraCanvasLaTeXDocumentReader.h
// LaTeX document-subset importer: reads an `article`-style .tex source and
// produces the shared UCRichDocument model (paragraphs, headings, lists,
// tables, images, captions, footnotes, cross-references, verbatim, and
// formulas as math runs / display blocks), the same model the ODT and DOCX
// readers fill. It is an importer, not a TeX interpreter: a fixed vocabulary
// of commands and environments is mapped onto the model, user macros
// (\newcommand, \def, \newenvironment) are expanded, and everything unknown
// degrades to its arguments' text with a diagnostic instead of a blank pane.
// Formulas are not typeset here: they travel as LaTeX source and are set by
// the math engine wherever the document is shown (Markdown mode of
// UltraCanvasTextArea via UltraCanvasInlineMath).
// Out of scope by design: page layout (A4 breaking, floats placement),
// TikZ/pgfplots pictures (reported, see UltraCanvasLaTeXEngineProposal.md
// Phase 4), and arbitrary packages.
// See Docs/UltraCanvas/UltraCanvasLaTeXDocumentReader.md.
// Version: 1.0.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Documents/Word/UltraCanvasRichDocument.h"

#include <string>
#include <vector>

namespace UltraCanvas {

// One thing the reader could not map faithfully. Never fatal: the document
// is still produced with the affected content degraded to text.
struct LaTeXDocumentDiagnostic {
    int line = 0;               // 1-based source line (best effort after macro expansion)
    std::string message;
};

struct LaTeXDocumentReadOptions {
    // Resolves \includegraphics, \input and \include; Load() fills it with
    // the .tex file's directory when empty.
    std::string baseDirectory;
    // Prefix headings with article-style numbers ("2.1 Method"), as LaTeX does
    // for unstarred sectioning commands. Starred commands are never numbered.
    bool numberSections = true;
    // Upper bound on \input / \include splices (guards against cycles).
    int maxInputFiles = 64;
};

class UltraCanvasLaTeXDocumentReader {
public:
    // Parses LaTeX source into outDocument (replaced). Returns false only when
    // the source holds no document content at all; every recognisable part of
    // a partially supported document is still imported, and outDiagnostics
    // (optional) lists what was degraded, in source order.
    static bool Parse(const std::string& source, UCRichDocument& outDocument,
                      std::vector<LaTeXDocumentDiagnostic>* outDiagnostics = nullptr,
                      const LaTeXDocumentReadOptions& options = {});

    // Reads the file and parses it; false with a user-facing outError when the
    // file cannot be read or holds no document content.
    static bool Load(const std::string& filePath, UCRichDocument& outDocument,
                     std::string& outError,
                     std::vector<LaTeXDocumentDiagnostic>* outDiagnostics = nullptr,
                     LaTeXDocumentReadOptions options = {});

    // Cheap content probe for format detection: true when the text (its head
    // is enough) starts like a LaTeX document — a \documentclass line,
    // \begin{document}, or a sectioning/\usepackage command before any other
    // markup.
    static bool LooksLikeLaTeXDocument(const std::string& text);

    // "line 12: unknown command \foo (arguments kept as text)" per entry,
    // newline-separated — for error panes and logs.
    static std::string FormatDiagnostics(const std::vector<LaTeXDocumentDiagnostic>& diagnostics);
};

} // namespace UltraCanvas
