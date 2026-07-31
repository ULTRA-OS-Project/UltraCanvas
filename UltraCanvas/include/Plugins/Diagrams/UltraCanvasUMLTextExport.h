// include/Plugins/Diagrams/UltraCanvasUMLTextExport.h
// Text export of a UML model: PlantUML and Mermaid
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework
//
// Pure functions over UltraCanvasUMLModel — no UI, no files, no state. They
// exist so a model built by the reverse engineer (or by hand) is inspectable
// and shareable before the UltraCanvasClassDiagram element lands, and so the
// model itself is testable against a stable textual form.
//
// Import (PlantUML/Mermaid -> model) is a separate, larger job and is not part
// of this header; see items X2/X4 of
// Docs/UltraCanvas/UltraCanvasClassDiagramProposal.md.

#pragma once

#include "Plugins/Diagrams/UltraCanvasUMLModel.h"

#include <string>

namespace UltraCanvas {

namespace UMLTextExport {

struct TextExportOptions {
    bool includeMembers = true;
    bool includeAttributes = true;
    bool includeOperations = true;
    bool includeStereotypes = true;
    bool includeRelationshipLabels = true;  // names, roles, multiplicities
    bool includeTitle = true;
    // Emit the surrounding @startuml/@enduml (PlantUML) or the "classDiagram"
    // header (Mermaid). Off when embedding the body in a larger document.
    bool includeWrapper = true;
    std::string indent = "    ";
};

// PlantUML class-diagram source. Relationship arrows follow the PlantUML
// convention: --|> generalization, ..|> realization, *-- composition,
// o-- aggregation, --> directed association, -- association, ..> dependency.
std::string ToPlantUML(const UltraCanvasUMLModel& model,
                       const TextExportOptions& options = TextExportOptions());

// Mermaid `classDiagram` source. Arrow direction follows the Mermaid
// convention, in which the parent is written first: `Parent <|-- Child`.
std::string ToMermaid(const UltraCanvasUMLModel& model,
                      const TextExportOptions& options = TextExportOptions());

} // namespace UMLTextExport

} // namespace UltraCanvas
