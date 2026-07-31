// Plugins/Diagrams/UltraCanvasUMLTextExport.cpp
// PlantUML / Mermaid export of a UML model
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasUMLTextExport.h"
#include "Plugins/Diagrams/UltraCanvasUMLNotation.h"

#include <cctype>
#include <sstream>

namespace UltraCanvas {
namespace UMLTextExport {

namespace {

// Both target formats choke on '::' and whitespace in an identifier position.
std::string SanitizeName(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (size_t i = 0; i < name.size(); ++i) {
        if (name[i] == ':' && i + 1 < name.size() && name[i + 1] == ':') {
            out += "_";
            ++i;
        } else if (std::isspace(static_cast<unsigned char>(name[i]))) {
            out += "_";
        } else {
            out.push_back(name[i]);
        }
    }
    return out.empty() ? std::string("Unnamed") : out;
}

std::string NameOf(const UltraCanvasUMLModel& model, const std::string& classifierId) {
    const UMLClassifier* c = model.GetClassifier(classifierId);
    return SanitizeName(c ? c->name : classifierId);
}

// PlantUML keyword introducing a classifier.
const char* PlantUMLKeyword(UMLClassifierKind kind) {
    switch (kind) {
        case UMLClassifierKind::AbstractClass: return "abstract class";
        case UMLClassifierKind::Interface:     return "interface";
        case UMLClassifierKind::Enumeration:   return "enum";
        case UMLClassifierKind::Package:       return "package";
        default:                               return "class";
    }
}

std::string StereotypeSuffix(const UMLClassifier& classifier, bool includeStereotypes) {
    if (!includeStereotypes || classifier.stereotypes.empty()) return std::string();
    // "interface" and "enumeration" are already carried by the keyword.
    std::string out;
    for (const auto& stereotype : classifier.stereotypes) {
        if (stereotype == "interface" && classifier.kind == UMLClassifierKind::Interface) continue;
        if (stereotype == "enumeration" && classifier.kind == UMLClassifierKind::Enumeration) continue;
        out += " <<" + stereotype + ">>";
    }
    return out;
}

// The end labels PlantUML/Mermaid place around an arrow.
struct EdgeLabels {
    std::string sourceMultiplicity;
    std::string targetMultiplicity;
    std::string text;  // name and/or roles, already joined
};

EdgeLabels BuildLabels(const UMLRelationship& relationship, bool include) {
    EdgeLabels labels;
    if (!include) return labels;

    if (!relationship.source.multiplicity.empty()) {
        labels.sourceMultiplicity = "\"" + relationship.source.multiplicity + "\"";
    }
    if (!relationship.target.multiplicity.empty()) {
        labels.targetMultiplicity = "\"" + relationship.target.multiplicity + "\"";
    }

    std::string text = relationship.name;
    if (!relationship.stereotype.empty()) {
        std::string marker = "<<" + relationship.stereotype + ">>";
        text = text.empty() ? marker : (marker + " " + text);
    }
    if (relationship.nameDirection == UMLReadingDirection::SourceToTarget && !relationship.name.empty()) {
        text += " >";
    } else if (relationship.nameDirection == UMLReadingDirection::TargetToSource && !relationship.name.empty()) {
        text = "< " + text;
    }

    std::string roles;
    if (!relationship.source.roleName.empty()) roles += relationship.source.roleName;
    if (!relationship.target.roleName.empty()) {
        if (!roles.empty()) roles += " / ";
        roles += relationship.target.roleName;
    }
    if (!roles.empty()) {
        text = text.empty() ? roles : (text + " [" + roles + "]");
    }

    labels.text = text;
    return labels;
}

} // namespace

// =============================================================================
// PLANTUML
// =============================================================================

std::string ToPlantUML(const UltraCanvasUMLModel& model, const TextExportOptions& options) {
    std::ostringstream out;

    if (options.includeWrapper) out << "@startuml\n";
    if (options.includeTitle && !model.Title().empty()) {
        out << "title " << model.Title() << "\n";
    }
    if (options.includeWrapper || options.includeTitle) out << "\n";

    for (const auto& classifier : model.Classifiers()) {
        out << PlantUMLKeyword(classifier.kind) << " " << SanitizeName(classifier.name)
            << StereotypeSuffix(classifier, options.includeStereotypes);

        bool hasBody = options.includeMembers &&
                       ((options.includeAttributes && !classifier.attributes.empty()) ||
                        (options.includeOperations && !classifier.operations.empty()) ||
                        !classifier.literals.empty());
        if (!hasBody) {
            out << "\n";
            continue;
        }

        out << " {\n";
        if (classifier.kind == UMLClassifierKind::Enumeration) {
            for (const auto& literal : classifier.literals) {
                out << options.indent << literal.name;
                if (!literal.defaultValue.empty()) out << " = " << literal.defaultValue;
                out << "\n";
            }
        }
        if (options.includeAttributes) {
            for (const auto& attribute : classifier.attributes) {
                out << options.indent << UMLNotation::FormatMember(attribute) << "\n";
            }
        }
        if (options.includeOperations) {
            for (const auto& operation : classifier.operations) {
                out << options.indent << UMLNotation::FormatMember(operation) << "\n";
            }
        }
        out << "}\n";
    }

    if (!model.Relationships().empty()) out << "\n";

    for (const auto& relationship : model.Relationships()) {
        std::string source = NameOf(model, relationship.source.classifierId);
        std::string target = NameOf(model, relationship.target.classifierId);
        EdgeLabels labels = BuildLabels(relationship, options.includeRelationshipLabels);

        const char* arrow = "--";
        switch (relationship.kind) {
            case UMLRelationshipKind::Generalization:      arrow = "--|>"; break;
            case UMLRelationshipKind::Realization:         arrow = "..|>"; break;
            case UMLRelationshipKind::Composition:         arrow = "*--";  break;
            case UMLRelationshipKind::Aggregation:         arrow = "o--";  break;
            case UMLRelationshipKind::DirectedAssociation: arrow = "-->";  break;
            case UMLRelationshipKind::Dependency:          arrow = "..>";  break;
            case UMLRelationshipKind::Association:
            case UMLRelationshipKind::NAry:                arrow = "--";   break;
        }

        out << source;
        if (!labels.sourceMultiplicity.empty()) out << " " << labels.sourceMultiplicity;
        out << " " << arrow << " ";
        if (!labels.targetMultiplicity.empty()) out << labels.targetMultiplicity << " ";
        out << target;
        if (!labels.text.empty()) out << " : " << labels.text;
        out << "\n";
    }

    if (options.includeWrapper) out << "\n@enduml\n";
    return out.str();
}

// =============================================================================
// MERMAID
// =============================================================================

namespace {

// Mermaid member rows: "+name : Type" for attributes, "+name(params) Type"
// for operations. Static members take a '$' suffix, abstract ones '*'.
std::string MermaidMember(const UMLMember& member) {
    std::string out;
    char glyph = UMLNotation::VisibilityToGlyph(member.visibility);
    if (glyph != '\0') out.push_back(glyph);

    if (member.kind == UMLMemberKind::Operation) {
        out += member.name + "(" + UMLNotation::FormatParameters(member.parameters) + ")";
        if (member.isAbstract) out += "*";
        if (member.isStatic) out += "$";
        if (!member.type.empty()) out += " " + member.type;
    } else {
        out += member.name;
        if (!member.multiplicity.empty()) out += "[" + member.multiplicity + "]";
        if (member.isStatic) out += "$";
        if (!member.type.empty()) out += " : " + member.type;
    }
    return out;
}

const char* MermaidAnnotation(UMLClassifierKind kind) {
    switch (kind) {
        case UMLClassifierKind::Interface:     return "interface";
        case UMLClassifierKind::AbstractClass: return "abstract";
        case UMLClassifierKind::Enumeration:   return "enumeration";
        case UMLClassifierKind::DataType:      return "dataType";
        case UMLClassifierKind::Utility:       return "utility";
        default:                               return nullptr;
    }
}

} // namespace

std::string ToMermaid(const UltraCanvasUMLModel& model, const TextExportOptions& options) {
    std::ostringstream out;

    if (options.includeWrapper) out << "classDiagram\n";
    if (options.includeTitle && !model.Title().empty()) {
        out << options.indent << "%% " << model.Title() << "\n";
    }

    for (const auto& classifier : model.Classifiers()) {
        std::string name = SanitizeName(classifier.name);
        const char* annotation = MermaidAnnotation(classifier.kind);

        bool hasBody = annotation != nullptr ||
                       (options.includeMembers &&
                        ((options.includeAttributes && !classifier.attributes.empty()) ||
                         (options.includeOperations && !classifier.operations.empty()) ||
                         !classifier.literals.empty()));

        if (!hasBody) {
            out << options.indent << "class " << name << "\n";
            continue;
        }

        out << options.indent << "class " << name << " {\n";
        if (annotation && options.includeStereotypes) {
            out << options.indent << options.indent << "<<" << annotation << ">>\n";
        }
        for (const auto& literal : classifier.literals) {
            out << options.indent << options.indent << literal.name << "\n";
        }
        if (options.includeAttributes) {
            for (const auto& attribute : classifier.attributes) {
                out << options.indent << options.indent << MermaidMember(attribute) << "\n";
            }
        }
        if (options.includeOperations) {
            for (const auto& operation : classifier.operations) {
                out << options.indent << options.indent << MermaidMember(operation) << "\n";
            }
        }
        out << options.indent << "}\n";
    }

    for (const auto& relationship : model.Relationships()) {
        std::string source = NameOf(model, relationship.source.classifierId);
        std::string target = NameOf(model, relationship.target.classifierId);
        EdgeLabels labels = BuildLabels(relationship, options.includeRelationshipLabels);

        // Mermaid writes the *parent* first for inheritance, so generalization
        // and realization swap the two ends relative to the model.
        bool swap = (relationship.kind == UMLRelationshipKind::Generalization ||
                     relationship.kind == UMLRelationshipKind::Realization);

        const char* arrow = "--";
        switch (relationship.kind) {
            case UMLRelationshipKind::Generalization:      arrow = "<|--"; break;
            case UMLRelationshipKind::Realization:         arrow = "<|.."; break;
            case UMLRelationshipKind::Composition:         arrow = "*--";  break;
            case UMLRelationshipKind::Aggregation:         arrow = "o--";  break;
            case UMLRelationshipKind::DirectedAssociation: arrow = "-->";  break;
            case UMLRelationshipKind::Dependency:          arrow = "..>";  break;
            case UMLRelationshipKind::Association:
            case UMLRelationshipKind::NAry:                arrow = "--";   break;
        }

        std::string left = swap ? target : source;
        std::string right = swap ? source : target;
        std::string leftMultiplicity = swap ? labels.targetMultiplicity : labels.sourceMultiplicity;
        std::string rightMultiplicity = swap ? labels.sourceMultiplicity : labels.targetMultiplicity;

        out << options.indent << left;
        if (!leftMultiplicity.empty()) out << " " << leftMultiplicity;
        out << " " << arrow << " ";
        if (!rightMultiplicity.empty()) out << rightMultiplicity << " ";
        out << right;
        if (!labels.text.empty()) out << " : " << labels.text;
        out << "\n";
    }

    return out.str();
}

} // namespace UMLTextExport
} // namespace UltraCanvas
