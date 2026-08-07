// include/Plugins/Diagrams/UltraCanvasSequenceTextExport.h
// PlantUML and Mermaid export for the sequence diagram model
// Version: 1.0.0
// Last Modified: 2026-08-07
// Author: UltraCanvas Framework
//
// Pure functions of an UltraCanvasSequenceModel, mirroring what
// UltraCanvasUMLTextExport does for the class-diagram model. UI-free and
// unit-tested in Tests/SequenceModelTest.cpp.
//
// Import (PlantUML/Mermaid → model) is not part of this unit.

#pragma once

#include "Plugins/Diagrams/UltraCanvasSequenceModel.h"

#include <string>

namespace UltraCanvas {

struct SequenceTextExportOptions {
    bool includeTitle = true;
    bool includeActivations = true;   // activate / deactivate lines
    bool includeFragments = true;
    bool includeNotes = true;
    bool autonumber = false;          // emit the tool's own numbering directive
    std::string indent = "  ";        // Mermaid body indent
};

class SequenceTextExport {
public:
    // @startuml ... @enduml, with actor/participant/database declarations,
    // create/destroy, activate/deactivate derived from ComputeActivations(),
    // loop/alt/opt/par/break/critical blocks and notes.
    static std::string ToPlantUML(const UltraCanvasSequenceModel& model,
                                  const SequenceTextExportOptions& options = {});

    // sequenceDiagram ... with the same coverage. The title travels in YAML
    // front matter, which is how Mermaid titles a sequence diagram.
    static std::string ToMermaid(const UltraCanvasSequenceModel& model,
                                 const SequenceTextExportOptions& options = {});
};

} // namespace UltraCanvas
