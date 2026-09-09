// Tests/LaTeXDocumentTest.cpp
// Standalone test for the LaTeX document-subset importer
// (UltraCanvasLaTeXDocumentReader → UCRichDocument): structure mapping,
// text-mode commands, macros, tables, references, footnotes, math runs and
// blocks, diagnostics for what is outside the subset, the Markdown
// round-trip of the new model parts (math runs, MathBlock), format detection
// through UCWordDocumentIO, and the shipped media/LaTex corpus.
// Builds without the UI stack: the reader, the rich-document model and the
// Word module sources (see Tests/CMakeLists.txt).
// Usage: LaTeXDocumentTest [output-dir] [media/LaTex dir]
#include "Plugins/Documents/LaTeX/UltraCanvasLaTeXDocumentReader.h"
#include "Plugins/Documents/Word/UltraCanvasRichDocument.h"
#include "Plugins/Documents/Word/UltraCanvasWordDocumentIO.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; \
        ++failures; \
    } \
} while (0)

#define CHECK_MSG(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "  [" << (msg) << "]\n"; \
        ++failures; \
    } \
} while (0)

// Minimal valid 1x1 red PNG (for \includegraphics).
static const unsigned char kTinyPng[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
    0xDE, 0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0x00,
    0x00, 0x03, 0x01, 0x01, 0x00, 0xC9, 0xFE, 0x92, 0xEF, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E,
    0x44, 0xAE, 0x42, 0x60, 0x82
};

static std::string gTmpDir;

static void WriteFile(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

static std::string Text(const RichDocBlock& block) { return UCRichDocument::ConcatenateRunText(block.runs); }

static const RichDocBlock* FindBlock(const UCRichDocument& doc, RichBlockType type, const std::string& contains, int skip = 0) {
    for (const RichDocBlock& b : doc.blocks) {
        if (b.type != type) continue;
        if (!contains.empty() && Text(b).find(contains) == std::string::npos) continue;
        if (skip-- > 0) continue;
        return &b;
    }
    return nullptr;
}

static int Count(const UCRichDocument& doc, RichBlockType type) {
    int n = 0;
    for (const RichDocBlock& b : doc.blocks) if (b.type == type) ++n;
    return n;
}

static UCRichDocument ParseSource(const std::string& source, std::vector<LaTeXDocumentDiagnostic>* diags = nullptr,
                                  LaTeXDocumentReadOptions options = {}) {
    UCRichDocument doc;
    UltraCanvasLaTeXDocumentReader::Parse(source, doc, diags, options);
    return doc;
}

// ===== 1. Structure of an article =====
static void TestArticleStructure() {
    const std::string source = R"(\documentclass{article}
\usepackage{amsmath}
\title{The Title}
\author{Ann Author \and Bob Writer}
\date{2026}
\begin{document}
\maketitle
\begin{abstract}
An abstract.
\end{abstract}
\section{Intro}\label{sec:intro}
First paragraph
continues here.

Second paragraph.
\subsection{Sub}
\subsubsection{SubSub}
\section*{Unnumbered}
\paragraph{Runin} text after.
\begin{itemize}
\item one
\item two
  \begin{enumerate}
  \item nested
  \end{enumerate}
\end{itemize}
\begin{description}
\item[Term] definition
\end{description}
\begin{quote}
Quoted.
\end{quote}
\begin{center}
Centred.
\end{center}
\newpage
\end{document}
)";
    std::vector<LaTeXDocumentDiagnostic> diags;
    UCRichDocument doc = ParseSource(source, &diags);
    CHECK_MSG(diags.empty(), UltraCanvasLaTeXDocumentReader::FormatDiagnostics(diags));
    CHECK(doc.metadata.title == "The Title");
    CHECK(doc.metadata.author == "Ann Author, Bob Writer");

    const RichDocBlock* title = FindBlock(doc, RichBlockType::Heading, "The Title");
    CHECK(title && title->headingLevel == 1 && title->align == RichTextAlign::Center);
    const RichDocBlock* author = FindBlock(doc, RichBlockType::Paragraph, "Ann Author");
    CHECK(author && Text(*author) == "Ann Author, Bob Writer");
    CHECK(FindBlock(doc, RichBlockType::Paragraph, "2026"));
    const RichDocBlock* abs = FindBlock(doc, RichBlockType::Paragraph, "Abstract");
    CHECK(abs && abs->runs.size() == 1 && abs->runs[0].bold);

    const RichDocBlock* intro = FindBlock(doc, RichBlockType::Heading, "Intro");
    CHECK(intro && intro->headingLevel == 2 && Text(*intro) == "1 Intro");
    const RichDocBlock* sub = FindBlock(doc, RichBlockType::Heading, "Sub");
    CHECK(sub && sub->headingLevel == 3 && Text(*sub) == "1.1 Sub");
    const RichDocBlock* subsub = FindBlock(doc, RichBlockType::Heading, "SubSub");
    CHECK(subsub && subsub->headingLevel == 4 && Text(*subsub) == "1.1.1 SubSub");
    const RichDocBlock* unnum = FindBlock(doc, RichBlockType::Heading, "Unnumbered");
    CHECK(unnum && Text(*unnum) == "Unnumbered");
    const RichDocBlock* runin = FindBlock(doc, RichBlockType::Heading, "Runin");
    CHECK(runin && runin->headingLevel == 5 && Text(*runin) == "Runin");
    CHECK(FindBlock(doc, RichBlockType::Paragraph, "text after."));

    // Paragraph breaks: a single newline joins, a blank line splits.
    const RichDocBlock* p1 = FindBlock(doc, RichBlockType::Paragraph, "First paragraph");
    CHECK(p1 && Text(*p1) == "First paragraph continues here.");
    CHECK(FindBlock(doc, RichBlockType::Paragraph, "Second paragraph."));

    // Lists.
    CHECK(Count(doc, RichBlockType::ListItem) == 4);
    const RichDocBlock* one = FindBlock(doc, RichBlockType::ListItem, "one");
    CHECK(one && !one->orderedList && one->listLevel == 0);
    const RichDocBlock* nested = FindBlock(doc, RichBlockType::ListItem, "nested");
    CHECK(nested && nested->orderedList && nested->listLevel == 1);
    const RichDocBlock* term = FindBlock(doc, RichBlockType::ListItem, "Term");
    CHECK(term && term->runs.size() >= 2 && term->runs[0].bold && term->runs[0].text == "Term");
    CHECK(term && Text(*term) == "Term definition");

    const RichDocBlock* quote = FindBlock(doc, RichBlockType::BlockQuote, "Quoted.");
    CHECK(quote);
    const RichDocBlock* centred = FindBlock(doc, RichBlockType::Paragraph, "Centred.");
    CHECK(centred && centred->align == RichTextAlign::Center);
    CHECK(Count(doc, RichBlockType::PageBreak) == 1);
}

// ===== 2. Text-mode commands, characters and formatting =====
static void TestTextFormatting() {
    const std::string source =
        "\\textbf{Bold} \\textit{It} \\emph{Em \\emph{nested}} \\texttt{Mono} \\underline{Under} "
        "\\textcolor{red}{Red} {\\bfseries group} {\\small small} \\textsuperscript{2} "
        "\\href{https://x.org}{link} \\url{https://y.org}\n"
        "Caf\\'e na\\\"{\\i}ve Stra\\ss{}e \\c{c} \\v{s} \\H{o} --- -- ``q'' `s' 50\\% \\$5 a\\&b \\_ \\# \\{ \\} "
        "\\LaTeX{} x~y \\ldots \\verb|a*b| \\today\n";
    std::vector<LaTeXDocumentDiagnostic> diags;
    UCRichDocument doc = ParseSource(source, &diags);
    CHECK_MSG(diags.empty(), UltraCanvasLaTeXDocumentReader::FormatDiagnostics(diags));
    CHECK(doc.blocks.size() == 1);
    const std::vector<RichTextRun>& runs = doc.blocks[0].runs;

    auto findRun = [&](const std::string& text) -> const RichTextRun* {
        for (const RichTextRun& r : runs) if (r.text.find(text) != std::string::npos) return &r;
        return nullptr;
    };
    const RichTextRun* r;
    r = findRun("Bold");   CHECK(r && r->bold && !r->italic);
    r = findRun("It");     CHECK(r && r->italic && !r->bold);
    r = findRun("Em ");    CHECK(r && r->italic);
    r = findRun("nested"); CHECK(r && !r->italic);          // \emph inside \emph toggles back
    r = findRun("Mono");   CHECK(r && r->code);
    r = findRun("Under");  CHECK(r && r->underline);
    r = findRun("Red");    CHECK(r && r->color == "#FF0000");
    r = findRun("group");  CHECK(r && r->bold);
    r = findRun("small");  CHECK(r && r->fontSizePt > 0 && r->fontSizePt < 10);
    r = findRun("2");      CHECK(r && r->superscript);
    r = findRun("link");   CHECK(r && r->linkTarget == "https://x.org");
    r = findRun("https://y.org"); CHECK(r && r->linkTarget == "https://y.org");
    r = findRun("a*b");    CHECK(r && r->code);

    const std::string text = Text(doc.blocks[0]);
    CHECK(text.find("Café") != std::string::npos);
    CHECK(text.find("naïve") != std::string::npos);
    CHECK(text.find("Straße") != std::string::npos);
    CHECK(text.find("ç") != std::string::npos);
    CHECK(text.find("š") != std::string::npos);
    CHECK(text.find("ő") != std::string::npos);
    CHECK(text.find("—") != std::string::npos);
    CHECK(text.find("–") != std::string::npos);
    CHECK(text.find("“q”") != std::string::npos);
    CHECK(text.find("‘s’") != std::string::npos);
    CHECK(text.find("50% $5 a&b _ # { }") != std::string::npos);
    CHECK(text.find("LaTeX x\xC2\xA0y …") != std::string::npos);
    CHECK(text.find("202") != std::string::npos);   // \today prints a year
    // Styles do not leak past their group.
    r = findRun("Caf");    CHECK(r && !r->bold && !r->italic && !r->code);
}

// ===== 3. Math: runs, blocks, environments, numbering, macros prelude =====
static void TestMath() {
    const std::string source = R"(\newcommand{\R}{\mathbb{R}}
\newcommand{\norm}[1]{\left\lVert #1 \right\rVert}
\DeclareMathOperator{\tr}{tr}
\begin{document}
Inline $a^2$ and \(b_1\) over $\R$, $\norm{v}$, $\tr A$, plain $x$.
\[ \int_0^1 x\,dx \]
$$ y = 2 $$
\begin{equation}\label{eq:one} E = mc^2 \end{equation}
\begin{align}
  a &= b \label{eq:two} \\
  c &= d \label{eq:three}
\end{align}
\begin{equation*} f(x) \end{equation*}
\begin{gather} g \end{gather}
Refs: \eqref{eq:one}, \eqref{eq:two}, \eqref{eq:three}, \ref{eq:missing}.
\end{document}
)";
    std::vector<LaTeXDocumentDiagnostic> diags;
    UCRichDocument doc = ParseSource(source, &diags);
    const RichDocBlock* p = FindBlock(doc, RichBlockType::Paragraph, "Inline");
    CHECK(p);
    if (p) {
        int mathRuns = 0;
        for (const RichTextRun& r : p->runs) if (r.math) ++mathRuns;
        CHECK(mathRuns == 6);
        const RichTextRun* norm = nullptr; const RichTextRun* plain = nullptr; const RichTextRun* rr = nullptr; const RichTextRun* tr = nullptr;
        for (const RichTextRun& r : p->runs) {
            if (!r.math) continue;
            if (r.text.find("\\norm{v}") != std::string::npos) norm = &r;
            if (r.text == "x") plain = &r;
            if (r.text.find("\\R") != std::string::npos && r.text.find("norm") == std::string::npos) rr = &r;
            if (r.text.find("\\tr A") != std::string::npos) tr = &r;
        }
        // The prelude carries exactly the macros the formula uses.
        CHECK(norm && norm->text.find("\\newcommand{\\norm}[1]{\\left\\lVert #1 \\right\\rVert}") == 0);
        CHECK(norm && norm->text.find("\\newcommand{\\R}") == std::string::npos);
        CHECK(rr && rr->text.find("\\newcommand{\\R}{\\mathbb{R}}") == 0);
        CHECK(tr && tr->text.find("\\DeclareMathOperator{\\tr}{tr}") == 0);
        CHECK(plain);
        // Spacing around the runs survives.
        CHECK(Text(*p).find("Inline ") == 0);
    }
    CHECK(Count(doc, RichBlockType::MathBlock) == 6);
    const RichDocBlock* integral = FindBlock(doc, RichBlockType::MathBlock, "\\int_0^1");
    CHECK(integral && Text(*integral) == "\\int_0^1 x\\,dx");
    const RichDocBlock* eq = FindBlock(doc, RichBlockType::MathBlock, "mc^2");
    CHECK(eq && Text(*eq) == "E = mc^2");                       // \label stripped
    const RichDocBlock* al = FindBlock(doc, RichBlockType::MathBlock, "align");
    CHECK(al && Text(*al).find("\\begin{align}") == 0 && Text(*al).find("\\end{align}") != std::string::npos);
    CHECK(al && Text(*al).find("\\label") == std::string::npos);
    const RichDocBlock* refs = FindBlock(doc, RichBlockType::Paragraph, "Refs:");
    CHECK(refs && Text(*refs) == "Refs: (1), (2), (3), ??.");
    bool undefinedReported = false;
    for (const auto& d : diags) if (d.message.find("eq:missing") != std::string::npos) undefinedReported = true;
    CHECK(undefinedReported);
}

// ===== 4. Tables, floats, captions, images, cross references =====
static void TestTablesAndFloats() {
    WriteFile(gTmpDir + "/plot.png", std::string(reinterpret_cast<const char*>(kTinyPng), sizeof(kTinyPng)));
    const std::string source = R"(\begin{document}
\section{S}
\begin{table}[h]
\centering
\caption{Cases}\label{tab:c}
\begin{tabular}{|l|c|r|}
\hline
Head A & Head B & Head C \\ \hline
a1 & $x$ & c1 \\
\multicolumn{2}{|c|}{span} & c2 \\
\hline
\end{tabular}
\end{table}
\begin{figure}[htb]
\centering
\includegraphics[width=0.5\textwidth]{plot}
\caption{A plot}\label{fig:p}
\end{figure}
See Table~\ref{tab:c}, Figure~\ref{fig:p} and Section~\ref{S}.
\includegraphics{nothere.png}
\end{document}
)";
    std::vector<LaTeXDocumentDiagnostic> diags;
    LaTeXDocumentReadOptions options;
    options.baseDirectory = gTmpDir;
    UCRichDocument doc = ParseSource(source, &diags, options);

    const RichDocBlock* table = FindBlock(doc, RichBlockType::Table, "");
    CHECK(table);
    if (table) {
        CHECK(table->tableRows.size() == 3);
        CHECK(table->tableRows[0].header);
        CHECK(table->tableRows[0].cells.size() == 3);
        CHECK(UCRichDocument::ConcatenateRunText(table->tableRows[0].cells[1].runs) == "Head B");
        CHECK(table->tableRows[1].cells.size() == 3);
        CHECK(table->tableRows[1].cells[1].runs.size() == 1 && table->tableRows[1].cells[1].runs[0].math);
        CHECK(table->tableRows[2].cells.size() == 2);
        CHECK(table->tableRows[2].cells[0].columnSpan == 2);
        CHECK(UCRichDocument::ConcatenateRunText(table->tableRows[2].cells[0].runs) == "span");
        CHECK(table->align == RichTextAlign::Center);
    }
    const RichDocBlock* tcap = FindBlock(doc, RichBlockType::Paragraph, "Cases");
    CHECK(tcap && Text(*tcap) == "Table 1: Cases" && tcap->runs[0].bold && tcap->align == RichTextAlign::Center);
    const RichDocBlock* fcap = FindBlock(doc, RichBlockType::Paragraph, "A plot");
    CHECK(fcap && Text(*fcap) == "Figure 1: A plot");

    CHECK(doc.media.size() == 1);
    const RichDocBlock* image = FindBlock(doc, RichBlockType::Image, "");
    CHECK(image && image->mediaIndex == 0 && image->imageAltText == "plot.png");
    CHECK(image && image->imageWidthPt > 172.0f && image->imageWidthPt < 173.0f);   // 0.5 × 345pt
    CHECK(image && image->imageHeightPt > 172.0f && image->imageHeightPt < 173.0f); // square picture
    const RichDocBlock* missing = FindBlock(doc, RichBlockType::Image, "", 1);
    CHECK(missing && missing->mediaIndex < 0 && missing->imageAltText == "nothere.png");

    const RichDocBlock* refs = FindBlock(doc, RichBlockType::Paragraph, "See Table");
    CHECK(refs && Text(*refs) == "See Table\xC2\xA0" "1, Figure\xC2\xA0" "1 and Section\xC2\xA0??.");
    CHECK(diags.size() == 2);   // missing image + undefined label
}

// ===== 5. Footnotes, citations, bibliography, verbatim, theorems =====
static void TestNotesAndCode() {
    const std::string source = R"(\newtheorem{thm}{Theorem}
\begin{document}
Text\footnote{Note \emph{one}.} more\footnote{Two} and \cite{k1,k2} then \cite{unknown}.
\begin{thm}[Named]
Body.
\end{thm}
\begin{proof}
Done.
\end{proof}
\begin{verbatim}
  keep   spacing \textbf{raw}
second
\end{verbatim}
\begin{lstlisting}[language=Python]
print(1)
\end{lstlisting}
\begin{thebibliography}{9}
\bibitem{k1} First ref.
\bibitem{k2} Second ref.
\end{thebibliography}
\end{document}
)";
    std::vector<LaTeXDocumentDiagnostic> diags;
    UCRichDocument doc = ParseSource(source, &diags);
    const RichDocBlock* p = FindBlock(doc, RichBlockType::Paragraph, "Text");
    CHECK(p);
    if (p) {
        int marks = 0;
        for (const RichTextRun& r : p->runs) if (r.superscript) ++marks;
        CHECK(marks == 2);
        CHECK(Text(*p) == "Text1 more2 and [1, 2] then [unknown].");
    }
    // Footnote texts follow a rule at the end.
    CHECK(Count(doc, RichBlockType::HorizontalRule) == 1);
    const RichDocBlock* note1 = FindBlock(doc, RichBlockType::Paragraph, "Note");
    CHECK(note1 && note1->runs[0].superscript && note1->runs[0].text == "1");
    CHECK(note1 && Text(*note1) == "1 Note one.");
    bool emph = false;
    if (note1) for (const RichTextRun& r : note1->runs) if (r.text == "one" && r.italic) emph = true;
    CHECK(emph);

    const RichDocBlock* thm = FindBlock(doc, RichBlockType::Paragraph, "Theorem 1");
    CHECK(thm && Text(*thm) == "Theorem 1 (Named). Body." && thm->runs[0].bold);
    bool bodyItalic = false;
    if (thm) for (const RichTextRun& r : thm->runs) if (r.text.find("Body") != std::string::npos && r.italic) bodyItalic = true;
    CHECK(bodyItalic);
    const RichDocBlock* proof = FindBlock(doc, RichBlockType::Paragraph, "Proof");
    CHECK(proof && Text(*proof) == "Proof. Done. ∎");

    CHECK(Count(doc, RichBlockType::CodeBlock) == 2);
    const RichDocBlock* code = FindBlock(doc, RichBlockType::CodeBlock, "keep");
    CHECK(code && Text(*code) == "  keep   spacing \\textbf{raw}\nsecond");
    const RichDocBlock* py = FindBlock(doc, RichBlockType::CodeBlock, "print");
    CHECK(py && py->codeLanguage == "python");

    CHECK(FindBlock(doc, RichBlockType::Heading, "References"));
    const RichDocBlock* ref2 = FindBlock(doc, RichBlockType::ListItem, "Second");
    CHECK(ref2 && ref2->orderedList);
    CHECK(diags.empty());
}

// ===== 6. Macros, user environments, \input, diagnostics =====
static void TestMacrosAndDiagnostics() {
    WriteFile(gTmpDir + "/part.tex", "Included \\textbf{part}.\n");
    const std::string source = R"(\newcommand{\hello}[2][World]{Hello #2, #1!}
\def\twice#1{#1#1}
\newenvironment{boxed}{[[}{]]}
\begin{document}
\hello{Ann} \hello[Moon]{Bob} \twice{ab}
\begin{boxed}inside\end{boxed}
\input{part}
\unknownthing{arg} and \begin{weird}body\end{weird} x & y
\begin{tikzpicture}\draw (0,0) -- (1,1);\end{tikzpicture}
\end{document}
)";
    std::vector<LaTeXDocumentDiagnostic> diags;
    LaTeXDocumentReadOptions options;
    options.baseDirectory = gTmpDir;
    UCRichDocument doc = ParseSource(source, &diags, options);
    const RichDocBlock* p = FindBlock(doc, RichBlockType::Paragraph, "Hello");
    // The included file ends with a newline and the source continues on a
    // new line: that blank line is a paragraph break, as in TeX.
    CHECK(p && Text(*p) == "Hello Ann, World! Hello Bob, Moon! abab [[inside]] Included part.");
    CHECK(FindBlock(doc, RichBlockType::Paragraph, "arg and body x & y"));
    bool partBold = false;
    if (p) for (const RichTextRun& r : p->runs) if (r.text == "part" && r.bold) partBold = true;
    CHECK(partBold);
    CHECK(FindBlock(doc, RichBlockType::Paragraph, "[tikzpicture picture]"));

    std::string all = UltraCanvasLaTeXDocumentReader::FormatDiagnostics(diags);
    CHECK_MSG(all.find("unknown command \\unknownthing") != std::string::npos, all);
    CHECK_MSG(all.find("unknown environment \\begin{weird}") != std::string::npos, all);
    CHECK_MSG(all.find("'&' outside a table") != std::string::npos, all);
    CHECK_MSG(all.find("tikzpicture") != std::string::npos, all);
    CHECK(diags.size() == 4);
    // Line numbers point into the source.
    CHECK(!diags.empty() && diags[0].line == 8);

    // Empty input is no document; a fragment without \begin{document} is.
    UCRichDocument empty;
    CHECK(!UltraCanvasLaTeXDocumentReader::Parse("% only a comment\n", empty));
    CHECK(UltraCanvasLaTeXDocumentReader::Parse("Just text.", empty) && empty.blocks.size() == 1);
}

// ===== 7. Markdown round trip of math runs and blocks =====
static void TestMarkdownRoundTrip() {
    UCRichDocument doc = ParseSource("Inline $a_1$ here.\n\\[ x^2 \\]\n\\begin{align} a &= b \\\\ c &= d \\end{align}\n");
    std::string md = doc.ToMarkdown();
    CHECK_MSG(md.find("Inline $a_1$ here.") != std::string::npos, md);
    CHECK_MSG(md.find("$$\nx^2\n$$") != std::string::npos, md);
    CHECK_MSG(md.find("$$\n\\begin{align} a &= b \\\\ c &= d \\end{align}\n$$") != std::string::npos, md);
    // Backslashes inside math are not escaped (plain text ones are).
    UCRichDocument text = ParseSource("A \\textbackslash{} B $\\alpha$");
    std::string md2 = text.ToMarkdown();
    CHECK_MSG(md2.find("A \\\\ B $\\alpha$") != std::string::npos, md2);

    UCRichDocument back = UCRichDocument::FromMarkdown(md);
    CHECK(Count(back, RichBlockType::MathBlock) == 2);
    const RichDocBlock* mb = FindBlock(back, RichBlockType::MathBlock, "x^2");
    CHECK(mb && Text(*mb) == "x^2");
    // Super-/subscript emission.
    UCRichDocument sup = ParseSource("E\\textsuperscript{2} H\\textsubscript{2}O long\\textsuperscript{two words}");
    std::string md3 = sup.ToMarkdown();
    CHECK_MSG(md3.find("E^2^ H~2~O longtwo words") != std::string::npos, md3);
    // Column spans keep the Markdown grid aligned.
    UCRichDocument spanned = ParseSource("\\begin{tabular}{lll} a & b & c \\\\ \\multicolumn{2}{l}{ab} & c \\end{tabular}");
    std::string md4 = spanned.ToMarkdown();
    CHECK_MSG(md4.find("| ab | | c |") != std::string::npos, md4);
    // HTML and plain text know the new parts.
    std::string html = doc.ToHTML();
    CHECK(html.find("<span class=\"math\">$a_1$</span>") != std::string::npos);
    CHECK(html.find("$$x^2$$") != std::string::npos);
    CHECK(doc.ToPlainText().find("x^2") != std::string::npos);
}

// ===== 8. Format detection and the Word IO front door =====
static void TestDetection() {
    const std::string texPath = gTmpDir + "/doc.tex";
    WriteFile(texPath, "% comment first\n\\documentclass{article}\n\\begin{document}\nHi $x$.\n\\end{document}\n");
    CHECK(DetectWordDocumentFormat(texPath) == WordDocumentFormat::LaTeX);
    CHECK(WordDocumentFormatFromExtension(".tex") == WordDocumentFormat::LaTeX);
    // A renamed LaTeX file is still recognised by its head; plain text is not.
    const std::string renamed = gTmpDir + "/renamed.txt";
    WriteFile(renamed, "\\begin{document}\nHello\n\\end{document}\n");
    CHECK(DetectWordDocumentFormat(renamed) == WordDocumentFormat::LaTeX);
    const std::string plain = gTmpDir + "/plain.txt";
    WriteFile(plain, "Just a text file\nwith lines.\n");
    CHECK(DetectWordDocumentFormat(plain) == WordDocumentFormat::Unknown);
    const std::string md = gTmpDir + "/notes.md";
    WriteFile(md, "# Heading\n\nSome $x$ math.\n");
    CHECK(DetectWordDocumentFormat(md) == WordDocumentFormat::Unknown);

    UCRichDocument doc;
    std::string error;
    CHECK(UCWordDocumentIO::Load(texPath, doc, error));
    CHECK(error.empty());
    CHECK(doc.blocks.size() == 1 && doc.blocks[0].runs.size() >= 2 && doc.blocks[0].runs[1].math);
    CHECK(doc.metadata.title == "doc");   // file stem when the source has no \title
    // No writer.
    CHECK(!UCWordDocumentIO::Save(gTmpDir + "/out.tex", doc, error));
    CHECK(error.find("not supported") != std::string::npos);
    // Missing file.
    CHECK(!UltraCanvasLaTeXDocumentReader::Load(gTmpDir + "/absent.tex", doc, error));
    CHECK(!error.empty());
    // Through the ODT writer, a math block survives as a $$ paragraph.
    UCRichDocument withBlock = ParseSource("\\[ a+b \\]");
    const std::string odt = gTmpDir + "/math.odt";
    CHECK(UCWordDocumentIO::SaveOdt(odt, withBlock, error));
    UCRichDocument reloaded;
    CHECK(UCWordDocumentIO::LoadOdt(odt, reloaded, error));
    CHECK(reloaded.blocks.size() == 1 && Text(reloaded.blocks[0]) == "$$a+b$$");
}

// ===== 9. The shipped corpus =====
static void TestCorpus(const std::string& dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        std::cout << "  (corpus directory not found, skipped: " << dir << ")\n";
        return;
    }
    int files = 0, formulaOnly = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.path().extension() != ".tex") continue;
        ++files;
        UCRichDocument doc;
        std::string error;
        std::vector<LaTeXDocumentDiagnostic> diags;
        bool ok = UltraCanvasLaTeXDocumentReader::Load(entry.path().string(), doc, error, &diags);
        CHECK_MSG(ok, entry.path().string() + ": " + error);
        CHECK_MSG(diags.empty(), entry.path().string() + ": " + UltraCanvasLaTeXDocumentReader::FormatDiagnostics(diags));
        if (doc.blocks.size() == 1 && doc.blocks[0].type == RichBlockType::MathBlock) ++formulaOnly;
        if (entry.path().filename() == "article-quadratic-note.tex") {
            CHECK(Count(doc, RichBlockType::Heading) >= 4);
            CHECK(Count(doc, RichBlockType::Table) == 1);
            CHECK(Count(doc, RichBlockType::MathBlock) == 3);
            CHECK(Count(doc, RichBlockType::CodeBlock) == 1);
            CHECK(doc.media.size() == 1);
            CHECK(doc.metadata.title == "A Short Note on Quadratic Equations");
        }
    }
    CHECK(files >= 24);
    CHECK(formulaOnly >= 23);
    std::cout << "  corpus: " << files << " files, " << formulaOnly << " formula-only\n";
}

int main(int argc, char** argv) {
    gTmpDir = (argc > 1) ? argv[1] : ".";
    std::error_code ec;
    std::filesystem::create_directories(gTmpDir, ec);
    std::string corpus = (argc > 2) ? argv[2] : "../../media/LaTex";

    std::cout << "LaTeXDocumentTest\n";
    TestArticleStructure();
    TestTextFormatting();
    TestMath();
    TestTablesAndFloats();
    TestNotesAndCode();
    TestMacrosAndDiagnostics();
    TestMarkdownRoundTrip();
    TestDetection();
    TestCorpus(corpus);

    if (failures == 0) {
        std::cout << "LaTeXDocumentTest: all checks passed\n";
        return 0;
    }
    std::cerr << "LaTeXDocumentTest: " << failures << " check(s) failed\n";
    return 1;
}
