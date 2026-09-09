# UltraCanvasLaTeXDocumentReader

The LaTeX **document-subset importer**: reads an `article`-style `.tex`
source and produces the shared rich-document model (`UCRichDocument`) that
the ODT and DOCX readers fill, so a LaTeX document opens, previews and
renders wherever those do. It is Phase 3 of
[`UltraCanvasLaTeXEngineProposal.md`](UltraCanvasLaTeXEngineProposal.md).

It is an importer, not a TeX interpreter. A fixed vocabulary of commands and
environments is mapped onto paragraphs, headings, lists, tables, images,
code blocks and formula runs; user macros are expanded; everything outside
the vocabulary degrades to its arguments' text and is reported as a
diagnostic. Formulas are **not** typeset here: they travel as LaTeX source
(inline `math` runs and `MathBlock` blocks) and are set by the
[math engine](UltraCanvasMathEngine.md) wherever the document is shown,
through `UltraCanvasTextArea`'s Markdown mode and `UltraCanvasInlineMath`.

| Piece | Where |
|---|---|
| `UltraCanvasLaTeXDocumentReader` | `include/Plugins/Documents/LaTeX/UltraCanvasLaTeXDocumentReader.h`, `Plugins/Documents/LaTeX/UltraCanvasLaTeXDocumentReader.cpp` |
| Front door for all word-processing formats | `UCWordDocumentIO::Load` / `LoadLaTeX`, `DetectWordDocumentFormat` (`WordDocumentFormat::LaTeX`) in `Plugins/Documents/Word/UltraCanvasWordDocumentIO.h` |
| The model | `UCRichDocument` (`RichTextRun::math`, `RichBlockType::MathBlock` are the parts added for LaTeX) |
| Tests | `Tests/LaTeXDocumentTest.cpp` (runs the shipped `media/LaTex` corpus too) |

The reader is compiled into the core library next to the Word module; it
needs no plugin and no render context.

## Reading a document

```cpp
#include "Plugins/Documents/LaTeX/UltraCanvasLaTeXDocumentReader.h"

UCRichDocument document;
std::string error;
std::vector<LaTeXDocumentDiagnostic> diagnostics;
if (!UltraCanvasLaTeXDocumentReader::Load("paper.tex", document, error, &diagnostics)) {
    // unreadable file or no document content at all
}
for (const auto& d : diagnostics) {
    // d.line, d.message — e.g. "unknown command \foo (arguments kept as text)"
}
std::string markdown = document.ToMarkdown(options);   // what the TextArea renders
```

`Parse(source, document, diagnostics, options)` does the same on a string.
`Load` fills `LaTeXDocumentReadOptions::baseDirectory` with the file's
directory, which is where `\includegraphics`, `\graphicspath`, `\input` and
`\include` resolve. `numberSections` (default on) prefixes unstarred
sectioning commands with article-style numbers ("2.1 Method").

Most callers never touch the reader directly: `UCWordDocumentIO::Load`
dispatches to it when `DetectWordDocumentFormat` classifies the file as
LaTeX, which it does by content (a `\documentclass`, `\begin{document}` or
sectioning command at the head, comments skipped) or by a `.tex` / `.latex`
/ `.ltx` extension on a text file holding commands. That is the path taken
by `UltraCanvasFileLoader::LoadTextDocument`, the Filer's preview page, the
Media Viewer (a `.tex` opens as the rendered document, not its markup) and
the demo's "LaTeX Documents" page. There is no LaTeX writer;
`UCWordDocumentIO::Save` to `.tex` fails with a message.

The Texter editor keeps opening `.tex` files as source: a text editor edits
the markup, and the rendered form has no way back to it.

## What is mapped

| LaTeX | Model |
|---|---|
| `\title`, `\author` (with `\and`), `\date`, `\maketitle` | Centred H1 and paragraphs; `metadata.title` / `author` |
| `\part`, `\chapter`, `\section`, `\subsection`, `\subsubsection`, `\paragraph`, `\subparagraph`, starred forms, `\appendix` | Headings 1–6, numbered as `article` does |
| Blank line, `\par`, `\\`, `\newline` | Paragraph break, hard line break |
| `\textbf` `\textit` `\emph` (toggles) `\texttt` `\underline` `\sout` `\textsuperscript` `\textsubscript` `\textcolor` `\href` `\url`, the declarations `\bfseries` `\itshape` `\ttfamily` `\color` and the size commands `\tiny` … `\Huge`, scoped by `{…}` groups and environments | Run flags, `color`, `fontSizePt`, `linkTarget` |
| `itemize`, `enumerate`, `description` (nested), `\item[label]` | `ListItem` with `listLevel`, `orderedList`; description labels bold |
| `quote`, `quotation`, `verse` | `BlockQuote` |
| `center`, `flushleft`, `flushright`, `\centering` | Block alignment |
| `abstract` | Bold centred "Abstract" line, then paragraphs |
| `tabular` and friends (`tabular*`, `tabularx`, `longtable`), `\hline`/booktabs rules, `\multicolumn` (`columnSpan`), `\multirow` (`rowSpan`) | `Table`; the first row is the header |
| `figure`, `table`, `wrapfigure`, `\caption` | Centred bold "Figure n:" / "Table n:" paragraph |
| `\includegraphics[width=…,height=…,scale=…]{file}` | `Image` block with the file embedded as media (`width=0.5\textwidth` uses the article text width of 345 pt); a missing file keeps a placeholder and a diagnostic |
| `verbatim`, `lstlisting[language=…]`, `minted{lang}`, `\verb` | `CodeBlock` with `codeLanguage`; inline code run |
| `$…$`, `\(…\)`, `\ensuremath` | Inline `math` run |
| `\[…\]`, `$$…$$`, `equation`, `align`, `gather`, `multline`, `eqnarray`, `flalign`, `alignat` and starred forms | `MathBlock` (alignment environments keep their `\begin…\end` wrapper for the engine) |
| `\newcommand`, `\renewcommand`, `\providecommand`, `\def`, `\let`, `\newenvironment`, `\DeclareMathOperator` | Expanded in text; the definitions a formula uses are prepended to its source so the engine expands them too |
| `\label`, `\ref`, `\eqref`, `\autoref`, `\cref` | Section, caption, item and equation numbers resolved after the whole source is read; an undefined label prints `??` with a diagnostic |
| `\cite` family, `thebibliography`, `\bibitem` | `[n]` by bibliography order (the key when unknown); a "References" heading and an ordered list |
| `\footnote`, `\thanks`, `\footnotemark`/`\footnotetext` | Superscript number in the text; the notes follow a rule at the end of the document |
| `\newtheorem` environments, `proof` | Bold "Theorem n (note)." head with an italic body; "Proof." … ∎ |
| `\input`, `\include` | Spliced in place (relative to `baseDirectory`, `.tex` appended when missing; bounded by `maxInputFiles`) |
| `\newpage`, `\clearpage`; `\hrule`, `\rule{\textwidth}{…}` | `PageBreak`; `HorizontalRule` |
| Accents (`\'e`, `\"a`, `\^o`, `\c{c}`, `\v{s}`, `\H{o}`, …), `\ss` `\ae` `\o` `\l` …, `--`/`---`, `` `` ``/`''`, `~`, `\%` `\$` `\&` `\_` `\#` `\{` `\}`, `\ldots`, `\LaTeX`, `\today` | Unicode text (precomposed letters where Latin-1 / Extended-A has them) |
| `\definecolor` (rgb, RGB, HTML, gray, cmyk), `\colorlet`, dvips base names | `#RRGGBB` run colours |
| `minipage`, `multicols`, `titlepage`, `subequations`, size/shape environments, `\vspace`, `\hspace`, `\noindent`, `\tableofcontents`, `\usepackage`, layout settings | Transparent or ignored, no diagnostic |
| `tikzpicture`, `axis`, `pgfpicture` and the other picture environments | Skipped with a centred "[tikzpicture picture]" placeholder and a diagnostic (Phase 4 territory) |

Anything else: an unknown command is reported once per name, its optional
arguments are dropped and its braced arguments are kept as text; an unknown
environment is reported and its body is kept. The result is always a
document — never a blank pane.

Diagnostics carry the 1-based source line (best effort after macro
splicing). `FormatDiagnostics()` renders them as `line 12: …` lines for an
error pane or a log.

## Rendering the result

`UCRichDocument::ToMarkdown` writes a `math` run as `$source$` (unescaped)
and a `MathBlock` as a `$$` fence pair, which is exactly what
`UltraCanvasTextArea`'s Markdown mode typesets through the LaTeX module
(see "Math in Markdown mode" in
[`UltraCanvasTextAreaExamples.md`](UltraCanvasTextAreaExamples.md)).
Superscript and subscript runs become `^x^` / `~x~` around single words.
`ToHTML` wraps formulas in `<span class="math">$…$</span>` and
`<p class="math">$$…$$</p>`; the ODT and DOCX writers, which have no formula
writer, store a display block as a centred `$$…$$` paragraph that typesets
again after a re-import.

A `.tex` file with an embedded `\includegraphics` yields `document.media`
entries; pass `RichDocumentMarkdownOptions::imageDirectory` so the
serializer writes them where the renderer can load them (the Media Viewer
and the demo use a per-load temp directory, as the ODT demo does).

## Limits

Deliberately outside the subset: page breaking to a paper size, float
placement, hyphenation, `\newpage` semantics beyond a page-break marker,
arbitrary packages, and TikZ / pgfplots pictures (see the proposal's Phase
4). Equation numbers are resolved for `\eqref` but not drawn beside the
formula (the engine ignores `\tag`); `\pageref` prints `?`. Font families
(`\textsf`, `\textsc`) do not survive into the run model, and the Markdown
renderer drops colours and sizes it cannot express, as it does for Word
documents.
