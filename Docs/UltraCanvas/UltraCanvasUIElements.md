# UltraCanvas UI elements — the catalogue

Every other document here describes **one** component. This one answers the
question that comes first: *does an element for this already exist?* Read it
before you draw anything.

The framework ships around sixty UI elements in `UltraCanvas/include/`, and
about seventy more under `UltraCanvas/include/Plugins/` — charts, diagrams,
gauges, codes and document views, all built into the same library. **Both are
listed here.** New UI is assembled from them; it is not painted from scratch.

> **If it takes input, shows a picture, or presents a value, it is an element.**
> Never build one out of `ctx->DrawText` / `FillRoundedRectangle` plus a private
> buffer, caret, `hovered`/`focused` flag or key handler. Take one from the
> tables below, or add a new element so the next caller finds it too.

A hand-painted control looks right in a screenshot and then fails everywhere the
framework already solved the problem: no caret or selection, no clipboard, no
undo, no IME or multi-byte input, no keyboard-focus semantics, no theming, no
DPI scaling, no tooltip. A real case from this repository: the Filer's compress
dialog painted its own name field, so the name could only be appended to and
backspaced at the end, and it stopped answering the keyboard entirely as soon as
another element took the focus. It is an `UltraCanvasTextInput` child now.

The **header** column is authoritative — it is checked by the compiler, unlike a
doc file name. For the prose on a given element, use
`grep -l <Class> Docs/UltraCanvas/*.md` or search `llms.txt`.

## Text entry

| You need | Element | Header |
|---|---|---|
| Single-line text | `UltraCanvasTextInput` | `UltraCanvasTextInput.h` |
| Multi-line text or code | `UltraCanvasTextArea` | `UltraCanvasTextArea.h` |
| A **formatted document** edited as it looks — fonts, sizes, colours, headings, lists, tables, images ([doc](UltraCanvasRichTextEdit.md)) | `UltraCanvasRichTextEdit` | `UltraCanvasRichTextEdit.h` |
| Text with a suggestion list | `UltraCanvasAutoComplete` | `UltraCanvasAutoComplete.h` |
| Tokens / tags typed into a field | `UltraCanvasTagInput` | `UltraCanvasChip.h` |
| A number with up/down steppers | `UltraCanvasSpinner` | `UltraCanvasSpinner.h` |
| Password quality feedback | `UltraCanvasPasswordStrengthMeter`, `UltraCanvasPasswordRuleLegend` | matching `*.h` |

Which of the two multi-line surfaces you want depends on what the document
*is*. `UltraCanvasTextArea` edits **text** — plain, syntax-highlighted, or
Markdown rendered live with the caret line showing its source; formatting is a
function of the characters in the buffer, so what Markdown cannot spell cannot
be typed. `UltraCanvasRichTextEdit` edits a **`UCRichDocument`** — the model the
ODT/DOCX/DOC readers and writers already produce — so bold is a state of the
selection, and fonts, sizes, colours and alignment survive a round trip through
`.odt` or `.docx`. Source files, logs and Markdown belong in the TextArea; word
-processing documents belong in the RichTextEdit.

`UltraCanvasTextInput` covers what a text field is expected to do: caret,
click-to-position, drag selection, Home/End/arrows, Delete, cut/copy/paste,
undo/redo, placeholder, max length, validation state, horizontal scrolling and
multi-byte text. Reproducing even half of that by hand is a week of bugs.

Password fields also get a reveal control: `SetShowPasswordToggle(true)` paints
an eye button inside the field, and `SetPasswordRevealed()` drives the same state
from an external "Show password" checkbox. See
[UltraCanvasTextInputExamples.md](UltraCanvasTextInputExamples.md).

## Buttons and choices

| You need | Element | Header |
|---|---|---|
| Push, split or icon button | `UltraCanvasButton` | `UltraCanvasButton.h` |
| Checkbox | `UltraCanvasCheckbox` | `UltraCanvasCheckbox.h` |
| Radio button / group | `UltraCanvasRadio`, `UltraCanvasRadioGroup` | `UltraCanvasRadio.h` |
| On-off switch | `UltraCanvasSwitch` | `UltraCanvasSwitch.h` |
| Pick one of a list | `UltraCanvasDropdown` | `UltraCanvasDropdown.h` |
| Pick one of a few, shown side by side | `UltraCanvasSegmentedControl` | `UltraCanvasSegmentedControl.h` |
| A value on a range | `UltraCanvasSlider` | `UltraCanvasSlider.h` |
| A tone / mapping curve (Curves) | `UltraCanvasCurveEditor` | `UltraCanvasCurveEditor.h` |
| The stops of a gradient (a colour ramp) | `UltraCanvasGradientEditor` | `UltraCanvasGradientEditor.h` |
| A score out of N | `UltraCanvasRating` | `UltraCanvasRating.h` |
| Step through a sequence | `UltraCanvasStepper` | `UltraCanvasStepper.h` |

## Text, images and media

| You need | Element | Header |
|---|---|---|
| Static text | `UltraCanvasLabel` | `UltraCanvasLabel.h` |
| Count or status pill | `UltraCanvasBadge`, `UltraCanvasChip` | `UltraCanvasBadge.h`, `UltraCanvasChip.h` |
| Show an image (file, memory, SVG, animation) | `UltraCanvasImageElement` | `UltraCanvasImageElement.h` |
| Zoomable / pannable image | `UltraCanvasZoomPanImage` | `UltraCanvasImageViewer.h` |
| **Edit** a bitmap: layers, selection, brushes, zoom / pan with pixel grid (the model is `UCRasterDocument`, the brushes `UltraCanvasBrushEngine.h`) | `UltraCanvasPaintSurface` | `UltraCanvasPaintSurface.h` |
| Show a vector drawing (`VectorStorage::VectorDocument` — SVG, XAR, DXF, DWG (also `.dwt` / `.dws` / `.sv$`), EMF, WMF, AI via the Vector plugin) with zoom / pan | `UltraCanvasVectorElement` | `UltraCanvasVectorElement.h` |
| **Edit** a vector drawing: page, rulers, guides, grid, snapping, selection handles, tool hooks in document coordinates (the model is `VectorStorage::VectorDocument`, the editing layer `DataFormats/UltraCanvasVectorEdit.h`, the node model `UltraCanvasBezierPath.h`) | `UltraCanvasVectorCanvas` | `UltraCanvasVectorCanvas.h` |
| Any media file — image, video, audio, PDF, text, spreadsheet, eBook, font | `UltraCanvasMediaViewer` | `UltraCanvasMediaViewer.h` |
| Video / audio playback | `UltraCanvasVideoPlayerElement`, `UltraCanvasAudioPlayerElement` | matching `*.h` |
| Video / audio capture | `UltraCanvasVideoRecorderElement`, `UltraCanvasAudioRecorderElement` | matching `*.h` |
| Audio waveform, input level | `UltraCanvasWaveformElement`, `UltraCanvasLevelMeter` | `UltraCanvasWaveformElement.h`, `UltraCanvasAudioRecorderElement.h` |
| A gallery or a timed slideshow | `UltraCanvasAlbum`, `UltraCanvasSlideshow` | matching `*.h` |
| eBooks | `UltraCanvasEBookViewer` | `UltraCanvasEBookViewer.h` |
| A LaTeX formula (equation, matrix, ...) as an element | `UltraCanvasLaTeXView` via `CreateLaTeXView()` | `Plugins/LaTeX/UltraCanvasLaTeXView.h` |
| A LaTeX document (`article`: sections, lists, tables, figures, formulas) as a rich document | `UltraCanvasLaTeXDocumentReader` → `UCRichDocument` → `ToMarkdown()` into a `UltraCanvasTextArea` in Markdown mode; `UCWordDocumentIO::Load` and `UltraCanvasFileLoader::LoadTextDocument` dispatch to it for `.tex` | `Plugins/Documents/LaTeX/UltraCanvasLaTeXDocumentReader.h` |
| A formula inside a line of text you lay out yourself | `UltraCanvasInlineMath` (typeset, measure, draw at a baseline); `UltraCanvasTextArea`'s Markdown mode already does this for `$...$` | `UltraCanvasInlineMath.h` |
| Every glyph in a font file, scrolling, without installing it | `UltraCanvasFontViewer` | `UltraCanvasFontViewer.h` |
| Raw OpenGL | `UltraCanvasGLSurface` | `UltraCanvasGLSurface.h` |
| A 3D mesh from an `.stl` file — shaded, orbited with the mouse (with or without GL) | `UltraCanvasSTLElement` | `Models/STL/UltraCanvasSTLElement.h` |

`UltraCanvasImageElement` is the answer to "I just need to draw a picture" — it
goes through the shared `UCImage` cache, handles scaling modes, SVG and animated
formats, and does not re-decode on every frame the way an ad-hoc
`DrawImage(path, …)` in a `Render()` does.

## Layout and structure

| You need | Element | Header |
|---|---|---|
| Group children, scroll them | `UltraCanvasContainer` (scrolling is built in) | `UltraCanvasContainer.h` |
| A standalone scrollbar | `UltraCanvasScrollbar` | `UltraCanvasScrollbar.h` |
| Resizable panes | `UltraCanvasSplitPane`, `UltraCanvasSplitter` | `UltraCanvasSplitPane.h` |
| Tabs | `UltraCanvasTabbedContainer` | `UltraCanvasTabbedContainer.h` |
| A titled frame | `UltraCanvasGroupBox` | `UltraCanvasGroupBox.h` |
| A toolbar | `UltraCanvasToolbar` — the height (width, when vertical) you construct it with is a *minimum*: it grows to fit its items rather than clipping them | `UltraCanvasToolbar.h` |
| A rule or a gap | `UltraCanvasSeparator`, `UltraCanvasSpacer` | matching `*.h` |

Positioning inside a container is the CSS layout engine's job (`layout` /
`layoutItem` — see [CSSLayout.md](../CSSLayout.md)), not manual arithmetic on
`SetBounds`.

## Navigation and data views

| You need | Element | Header |
|---|---|---|
| Menu bar, popup and context menus | `UltraCanvasMenu` (+ `MenuBuilder`, `MenuItemData`) | `UltraCanvasMenu.h` |
| Editable menu configuration | `UltraCanvasMenuConfigWidget` | `UltraCanvasMenuConfigWidget.h` |
| Path bar | `UltraCanvasBreadcrumb` | `UltraCanvasBreadcrumb.h` |
| Page selector | `UltraCanvasPagination` | `UltraCanvasPagination.h` |
| List **or table** — `UltraCanvasListView` is the multi-column, virtualised, model-driven one (`IListModel` + `ListColumnDef` + delegates; `SetShowHeader(true)` gives resizable columns, header tooltips and a sort indicator; sort and filter it with [`UltraCanvasListSortFilterProxy`](UltraCanvasListSortFilterProxy.md)); tree; multi-column tree; sheet | `UltraCanvasListView`, `UltraCanvasTreeView`, `UltraCanvasColumnsTreeView`, `UltraCanvasSpreadsheet` | matching `*.h` |
| Folder contents / file browsing | `UltraCanvasFilerWidget` | `UltraCanvasFilerWidget.h` |
| System information (CPU, GPU, NPU, memory, drives, network, USB, Bluetooth) | `UltraCanvasHardwareInfoPanel` | `UltraCanvasHardwareInfoPanel.h` |
| The desktop message centre: every chat, mail and notification on the UltraMessage feed, with sources, filters, search and the notification's actions ([doc](UltraCanvasMessageCenter.md)) | `UltraCanvasMessageCenter` | `Plugins/UltraMessage/UltraCanvasMessageCenter.h` |

## Pickers, dialogs and feedback

| You need | Element | Header |
|---|---|---|
| Colour | `UltraCanvasColorPicker` | `UltraCanvasColorPicker.h` |
| A colour out of a small palette (a strip of swatches, sized to the space it gets) | `UltraCanvasColorSwatchBar` | `UltraCanvasColorSwatchBar.h` |
| Date, date range, month grid | `UltraCanvasDatePicker`, `UltraCanvasDateRangePicker`, `UltraCanvasCalendarView` | `UltraCanvasDatePicker.h` |
| Time, clock face | `UltraCanvasTimePicker`, `UltraCanvasTimeClockView` | `UltraCanvasTimePicker.h` |
| Modal dialog | `UltraCanvasModalDialog` | `UltraCanvasModalDialog.h` |
| Pick a file type to create, from a filterable list | `UltraCanvasNewDocumentDialog` (`CreateNewDocumentDialog`; call `Initialize()` after constructing) | `UltraCanvasNewDocumentDialog.h` |
| Progress of a long operation (ring + percentage + Cancel) | `UltraCanvasProgressDialog` | `UltraCanvasProgressDialog.h` |
| A gauge: speedometer, ring, battery, thermometer, LED/segmented bar — and `GaugeMode::LinearBar`, the progress bar for a status line, a row or a panel footer | `UltraCanvasGaugeDiagramElement` (`CreateGaugeDiagramElement`) | `Plugins/Diagrams/UltraCanvasGaugeDiagramElement.h` |
| Open / save a file, prompt for a value | `UltraCanvasFileDialog`, `UltraCanvasInputDialog` | `UltraCanvasModalDialog.h` |
| Native OS file dialog | `UltraCanvasNativeDialogs` | `UltraCanvasNativeDialogs.h` |
| Edit an image's tone curves (per channel, over a histogram) | `UltraCanvasCurvesDialog` | `dialogs/UltraCanvasCurvesDialog.h` |
| Save a bitmap with per-format options | `UltraCanvasImageExportDialog` | `dialogs/UltraCanvasImageExportDialog.h` |
| Show a file's metadata (Markdown or plain text) | `UltraCanvasMetadataDialog`, `ShowMetadataDialog()` — entries from `PixelFX::Header::MetadataToText()` ([doc](UltraCanvasMetadataDialog.md)) | `dialogs/UltraCanvasMetadataDialog.h` |
| Turn a 3D model into a bitmap, letting the user frame the view first | `UltraCanvasModelViewDialog`, `ShowModelViewDialog()` ([doc](UltraCanvasModelViewDialog.md)) | `dialogs/UltraCanvasModelViewDialog.h` |
| A "caption: control" form whose captions line up and still fit a translation | `CreateFormGrid` / `AddFormRow` / `AddFormWideRow` ([doc](UltraCanvasFormLayout.md)) | `UltraCanvasFormLayout.h` |
| Hover help | `UltraCanvasTooltipManager` (+ `TooltipContent`) | `UltraCanvasTooltipManager.h` |
| Startup splash (logo, version, "GUI by" attribution, timeout) | `UltraCanvasSplashScreen` | `UltraCanvasSplashScreen.h` |

## Charts, diagrams and codes — the plugin elements

These live under `UltraCanvas/include/Plugins/` and are built into the same
library; an application includes the header and adds the element like any
other. They were missing from this page for a long time, which is exactly the
trap it exists to close: a progress bar was written from scratch in 2026-09
because `GaugeMode::LinearBar` was not listed here, and nothing else on this
page suggested that the answer was a *gauge*. **Search this section before
drawing a graph, a diagram, a gauge or a code by hand.**

Each takes its data through setters and renders itself; most have a
`Create<Name>(…)` factory beside the class. Several have a doc of their own —
`grep -l <Class> Docs/UltraCanvas/*.md`.

### Charts — `Plugins/Charts/`

| You need | Element | Header |
|---|---|---|
| Line, bar, scatter, area — the four basics | `UltraCanvasLineChartElement`, `UltraCanvasBarChartElement`, `UltraCanvasScatterPlotElement`, `UltraCanvasAreaChartElement` | `Plugins/Charts/UltraCanvasSpecificChartElements.h` |
| Parts of a whole: pie, donut, 3D pie | `UltraCanvasPieChartElement` | `Plugins/Charts/UltraCanvasPieChart.h` |
| Parts of a whole, nested: sunburst, treemap, layered proportional areas | `UltraCanvasSunburstChart`, `UltraCanvasTreeMapElement`, `UltraCanvasNestedAreaChart` | `Plugins/Charts/UltraCanvasSunburstChart.h`, `Plugins/Diagrams/UltraCanvasTreeMapElement.h`, `Plugins/Charts/UltraCanvasNestedAreaChart.h` |
| Three variables at once (x, y, size): XY bubbles, packed bubbles, bubble matrix | `UltraCanvasBubbleChartElement` | `Plugins/Charts/UltraCanvasBubbleChart.h` |
| A staged process that shrinks step by step | `UltraCanvasFunnelChart` | `Plugins/Charts/UltraCanvasFunnelChart.h` |
| A static hierarchy whose levels are parts of a whole | `UltraCanvasPyramidChart` | `Plugins/Charts/UltraCanvasPyramidChart.h` |
| A running total built from ups and downs | `UltraCanvasWaterfallChartElement` | `Plugins/Charts/UltraCanvasWaterfallChart.h` |
| Two paired values per category (before / after) | `UltraCanvasDumbbellChart` | `Plugins/Charts/UltraCanvasDumbbellChart.h` |
| Values either side of a centre: likert scales, demographics | `UltraCanvasDivergingBarChart`, `UltraCanvasPopulationChart` | `Plugins/Charts/UltraCanvasDivergingBarChart.h`, `Plugins/Charts/UltraCanvasPopulationChart.h` |
| 100% stacked columns of *different widths* (Marimekko / mosaic) | `UltraCanvasMekkoChartElement` | `Plugins/Charts/UltraCanvasMekkoChart.h` |
| Many measures per item: radar, or parallel coordinates | `UltraCanvasRadarChartElement`, `UltraCanvasParallelCoordinateChartElement` | `Plugins/Charts/UltraCanvasRadarChartElement.h`, `Plugins/Charts/UltraCanvasParallelCoordinateChart.h` |
| Angle / radius space: polar series, radial bars, activity rings, ringed infographic | `UltraCanvasPolarChart`, `UltraCanvasRadialBarChart`, `UltraCanvasCircularProgressChart`, `UltraCanvasCircularInfoGraphic` | matching `Plugins/Charts/*.h` |
| A matrix of values as colour: heatmap, hexbin, contribution calendar | `UltraCanvasHeatmapChartElement`, `UltraCanvasHexbinChartElement`, `UltraCanvasCalendarHeatmapElement` | `Plugins/Charts/UltraCanvasHeatmapChart.h`, `Plugins/Charts/UltraCanvasHexbinChart.h`, `Plugins/Charts/UltraCanvasCalendarHeatmap.h` |
| The spread of individual points (strip / jitter plot, with box or violin overlay) | `UltraCanvasJitterPlotElement` | `Plugins/Charts/UltraCanvasJitterPlotElement.h` |
| A scalar field: isolines and filled bands, or a shaded 3D height field | `UltraCanvasContourChartElement`, `UltraCanvasContourSurface3DElement` (`UltraCanvasContourSurfaceGLElement` for depth-tested GL) | `Plugins/Charts/UltraCanvasContourChart.h`, `Plugins/Charts/UltraCanvasContourSurface3D.h`, `Plugins/Charts/UltraCanvasContourSurfaceGL.h` |
| An (x, y, z) point cloud in an orbitable axes box | `UltraCanvasScatterPlot3DElement` | `Plugins/Charts/UltraCanvasScatterPlot3D.h` |
| Audio as time × frequency (STFT) | `UltraCanvasSpectrogramElement` | `Plugins/Charts/UltraCanvasSpectrogram.h` |
| OHLC candlesticks with volume | `UltraCanvasFinancialChartElement` | `Plugins/Charts/UltraCanvasFinancialChart.h` |
| A two-axis strategic grid: SWOT, BCG, Eisenhower, risk / priority | `UltraCanvasQuadrantChart` | `Plugins/Charts/UltraCanvasQuadrantChart.h` |
| Flows between categories arranged on a circle | `UltraCanvasChordChart` | `Plugins/Charts/UltraCanvasChordChart.h` |
| A project schedule: task table, dependencies, milestones, critical path | `UltraCanvasGanttChartElement` | `Plugins/Charts/UltraCanvasGanttChart.h` |
| A Kanban board, and the cumulative flow diagram over its history | `UltraCanvasKanbanBoardElement`, `UltraCanvasCumulativeFlowChartElement` | `Plugins/Charts/UltraCanvasKanbanBoard.h`, `Plugins/Charts/UltraCanvasCumulativeFlowChart.h` |
| Milestones and spans on a real date axis | `UltraCanvasTimelineChart` | `Plugins/Charts/UltraCanvasTimelineChart.h` |
| **A chart type that does not exist yet** — derive, do not paint | `UltraCanvasChartEngineElement` (three-phase engine; implement phase 2) or `UltraCanvasChartElementBase` | `Plugins/Charts/Engine/UltraCanvasChartEngineElement.h`, `Plugins/Charts/UltraCanvasChartElementBase.h` |

### Diagrams — `Plugins/Diagrams/`

| You need | Element | Header |
|---|---|---|
| Boxes and arrows: a flow chart (with a shape palette for editing) | `UltraCanvasFlowChart`, `UltraCanvasFlowChartPalette` | `Plugins/Diagrams/UltraCanvasFlowChart.h`, `Plugins/Diagrams/UltraCanvasFlowChartPalette.h` |
| Blocks in 3D isometric | `UltraCanvasBlockDiagram` | `Plugins/Diagrams/UltraCanvasBlockDiagram.h` |
| A graph / network, or a node editor whose nodes are panels with typed sockets | `UltraCanvasNodeDiagram`, `UltraCanvasCompositorDiagram` | `Plugins/Diagrams/UltraCanvasNodeDiagram.h`, `Plugins/Diagrams/UltraCanvasCompositorDiagram.h` |
| UML: classes, or a sequence | `UltraCanvasClassDiagram`, `UltraCanvasSequenceDiagram` | `Plugins/Diagrams/UltraCanvasClassDiagram.h`, `Plugins/Diagrams/UltraCanvasSequenceDiagram.h` |
| A database schema (Chen, Chen min-max/ISO, Crow's Foot) | `UltraCanvasERDiagram` | `Plugins/Diagrams/UltraCanvasERDiagram.h` |
| SysML requirements with traceability and coverage | `UltraCanvasRequirementDiagram` | `Plugins/Diagrams/UltraCanvasRequirementDiagram.h` |
| A PERT network | `UltraCanvasPertChart` | `Plugins/Diagrams/UltraCanvasPertChart.h` |
| A mind map | `UltraCanvasMindMap` | `Plugins/Diagrams/UltraCanvasMindMap.h` |
| A tree: clustering / phylogenetic, or a Gource-style radial filesystem | `UltraCanvasDendrogram`, `UltraCanvasGourceTree` | `Plugins/Diagrams/UltraCanvasDendrogram.h`, `Plugins/Diagrams/UltraCanvasGourceTree.h` |
| A repository history as lanes or git-flow swimlanes | `UltraCanvasGitGraph` | `Plugins/Diagrams/UltraCanvasGitGraph.h` |
| Flow volumes splitting and merging | `UltraCanvasSankeyDiagram` | `Plugins/Diagrams/UltraCanvasSankey.h` |
| Nodes on a baseline joined by arcs | `UltraCanvasArcDiagram` | `Plugins/Diagrams/UltraCanvasArcDiagram.h` |
| Area-proportional rooms and their adjacencies (space planning) | `UltraCanvasAdjacencyDiagram` | `Plugins/Diagrams/UltraCanvasAdjacencyDiagram.h` |
| Two item sets crossed in an L or T, cells marked from a scale | `UltraCanvasMatrixDiagram` | `Plugins/Diagrams/UltraCanvasMatrixDiagram.h` |
| Set overlap and containment (Venn / Euler) | `UltraCanvasVennDiagramElement` | `Plugins/Diagrams/UltraCanvasVennDiagram.h` |
| Words sized by frequency, optionally inside a shape or image mask | `UltraCanvasWordCloudElement` | `Plugins/Diagrams/UltraCanvasWordCloudDiagram.h` |
| Seats by party: hemicycle, circle, Westminster benches, grid | `UltraCanvasParliamentDiagram` | `Plugins/Diagrams/UltraCanvasParliamentDiagram.h` |
| The four-panel SWOT infographic | `UltraCanvasSWOTDiagram` | `Plugins/Diagrams/UltraCanvasSWOTDiagram.h` |
| Cause and effect (Ishikawa / fishbone) | `UltraCanvasFishboneDiagram` | `Plugins/Diagrams/UltraCanvasFishboneDiagram.h` |
| A hub with a ring of labelled nodes and satellites | `UltraCanvasCircleDiagram` | `Plugins/Diagrams/UltraCanvasCircleDiagram.h` |
| A narrative timeline along a decorative path (**not** date-accurate — that is `UltraCanvasTimelineChart`) | `UltraCanvasTimelineDiagram` | `Plugins/Diagrams/UltraCanvasTimelineDiagram.h` |
| A bit-accurate map of a protocol data unit | `UltraCanvasPacketDiagram` | `Plugins/Diagrams/UltraCanvasPacketDiagram.h` |
| A gauge, **and the framework's progress bar** | `UltraCanvasGaugeDiagramElement` — see *Pickers, dialogs and feedback* above | `Plugins/Diagrams/UltraCanvasGaugeDiagramElement.h` |

### Codes and documents

| You need | Element | Header |
|---|---|---|
| A QR code | `UltraCanvasQRCode` | `Plugins/QRCode/UltraCanvasQRCode.h` |
| A 1D barcode (EAN, UPC, Code 39/128, ITF, Codabar …) with its human-readable line | `UltraCanvasBarcodeElement` ([doc](UltraCanvasBarcodeElement.md)) | `Plugins/Barcode/UltraCanvasBarcodeElement.h` |
| A PDF on its own, with thumbnail strip, zoom and search-hit overlay (`UltraCanvasMediaViewer` embeds this for `.pdf`) | `UltraCanvasPDFView` | `Plugins/Documents/UltraCanvasPDFView.h` |
| Markdown rendered as a standalone display (`UltraCanvasTextArea`'s Markdown mode is the editable route) | `UltraCanvasMarkdownDisplay` | `Plugins/Text/UltraCanvasMarkdown.h` |

The vector format plugins (`UltraCanvasSVGElement`, `UltraCanvasCDRElement`,
`UltraCanvasEPSElement`, `UltraCanvasXARElement`) are **not** in these tables on
purpose: they are decoders behind `UltraCanvasVectorElement` and
`UltraCanvasImageElement`, which are what an application reaches for. Same for
the LaTeX and STL elements, listed under *Text, images and media* above.

## Creating them

Widgets are `std::shared_ptr`-managed. Use the `Create*` factory where one
exists:

```cpp
auto name   = CreateTextInput("archive-name", 0, 0, 240, 26);
auto shot   = CreateImageElement("preview", 0, 0, 320, 240, "poster.png");
auto accept = CreateButton("ok", 101, 0, 0, 104, 30, "Compress");
```

An element that belongs to a self-rendered view is added as a child of it and
positioned each frame with `UltraCanvasContainer::PlaceChildAt(child, rect)` —
**not** `SetBounds()`, which writes only `finalBounds` and is undone by the next
layout pass. Getting that wrong is subtle rather than obvious: a text input
re-clamps its horizontal scroll when its width changes, so an editor placed with
`SetBounds()` renders scrolled to the tail of its own value. See
`UltraCanvasFilerWidget`'s rename editor and compress dialog, the pickers'
fields and the spreadsheet's cell editor for the pattern.

## The two legitimate exceptions

1. **Self-rendered views.** A widget that draws thousands of cells itself —
   `UltraCanvasFilerWidget`, `UltraCanvasAlbum`, `UltraCanvasListView`, the
   chart and diagram plugins — paints its *content* directly, because creating
   an element per cell would not scale. That licence covers the content only:
   as soon as such a view needs an editable field, a button or a picker, it adds
   a real element as a child rather than painting a fake one.
2. **The element itself.** `UltraCanvasTextInput` owns a buffer and a caret
   because it *is* the text input.

Anything else that reaches for raw drawing to make a control is a bug report
waiting to be filed.

## Enforcement

`scripts/check_ui_reuse.py` looks for the two shapes that are almost always
wrong — a private edit buffer plus caret fed from a `KeyDown` handler, and a
`Draw*Button(IRenderContext*, …, bool hovered)` painter — and CI runs it on
every pull request:

```bash
python3 scripts/check_ui_reuse.py            # report
python3 scripts/check_ui_reuse.py --strict   # what CI runs
```

`scripts/ui_reuse_baseline.txt` records controls that predate the check —
it is **empty**, because every control in the tree is now built from an
element, and the intent is that it stays empty. Do not add to it to silence a
finding. A genuine exception is declared in the source instead, with a reason:

```cpp
// ui-reuse-exempt: the treemap paints its own cells; there is no element per cell.
```

## When the element does not exist

Add it under `UltraCanvas/{include,core}` — or `UltraCanvas/include/Plugins/`
for a chart, diagram or document view — with a doc in `Docs/UltraCanvas/`, a
row in the tables above, and a changelog entry. A one-off painted into a single
dialog helps one screen; an element helps every caller and gets the keyboard,
focus and theming behaviour right once.

**The row is not optional.** `scripts/check_element_catalogue.py` fails when an
element in the tree is not named on this page, because a page with holes in it
is worse than no page: it is read as a complete answer to "does this already
exist?", and a hole reads as "no". A class that genuinely is not something a
caller reaches for — a base to derive from, a format decoder behind one of the
elements above — goes in `scripts/element_catalogue_exempt.txt` with its
reason.
