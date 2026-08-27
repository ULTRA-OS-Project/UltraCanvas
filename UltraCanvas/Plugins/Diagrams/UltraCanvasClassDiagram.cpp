// Plugins/Diagrams/UltraCanvasClassDiagram.cpp
// UML class diagram element
// Version: 1.0.0
// Last Modified: 2026-08-02
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasClassDiagram.h"
#include "Plugins/Diagrams/UltraCanvasUMLNotation.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace UltraCanvas {

namespace {

constexpr double kPi = 3.14159265358979323846;

// A qualitative palette for "one colour per class" (reference image 4).
const Color kPalette[] = {
    Color(126, 87, 194, 255),   // purple
    Color(38, 166, 154, 255),   // teal
    Color(255, 167, 38, 255),   // orange
    Color(102, 187, 106, 255),  // green
    Color(239, 108, 0, 255),    // deep orange
    Color(66, 165, 245, 255),   // blue
    Color(236, 64, 122, 255),   // pink
    Color(141, 110, 99, 255),   // brown
    Color(255, 213, 79, 255),   // amber
    Color(120, 144, 156, 255),  // blue grey
};
constexpr size_t kPaletteCount = sizeof(kPalette) / sizeof(kPalette[0]);

// Lightened variant of a header colour, used for the body band.
Color Lighten(const Color& color, double amount) {
    auto mix = [&](int channel) {
        double value = channel + (255.0 - channel) * amount;
        return static_cast<unsigned char>(std::clamp(value, 0.0, 255.0));
    };
    return Color(mix(color.r), mix(color.g), mix(color.b), color.a);
}

Color Darken(const Color& color, double amount) {
    auto mix = [&](int channel) {
        double value = channel * (1.0 - amount);
        return static_cast<unsigned char>(std::clamp(value, 0.0, 255.0));
    };
    return Color(mix(color.r), mix(color.g), mix(color.b), color.a);
}

// Dash patterns for the two dashed UML relationship kinds.
const UCDashPattern& DashedPattern() {
    static const UCDashPattern pattern({6.0, 4.0}, 0.0);
    return pattern;
}

} // namespace

// =============================================================================
// CONSTRUCTION
// =============================================================================

UltraCanvasClassDiagram::UltraCanvasClassDiagram(const std::string& id,
                                                 int x, int y, int width, int height)
    : UltraCanvasUIElement(id, static_cast<float>(x), static_cast<float>(y),
                           static_cast<float>(width), static_cast<float>(height)) {
    layoutOptions.kind = ClassLayoutKind::Layered;
    layoutOptions.direction = ClassLayoutDirection::TopToBottom;
    RegisterDefaultStereotypeStyles();
    ApplyThemeColors(ClassDiagramTheme::Default);
}

void UltraCanvasClassDiagram::RegisterDefaultStereotypeStyles() {
    stereotypeStyles.clear();

    ClassStereotypeStyle interfaceRule;
    interfaceRule.stereotype = "interface";
    interfaceRule.headerColor = Color(255, 171, 64, 255);
    interfaceRule.hasHeaderColor = true;
    stereotypeStyles.push_back(interfaceRule);

    ClassStereotypeStyle dataTypeRule;
    dataTypeRule.stereotype = "DataType";
    dataTypeRule.headerColor = Color(174, 203, 232, 255);
    dataTypeRule.hasHeaderColor = true;
    stereotypeStyles.push_back(dataTypeRule);

    ClassStereotypeStyle enumRule;
    enumRule.stereotype = "enumeration";
    enumRule.headerColor = Color(178, 223, 219, 255);
    enumRule.hasHeaderColor = true;
    stereotypeStyles.push_back(enumRule);
}

// =============================================================================
// MODEL
// =============================================================================

void UltraCanvasClassDiagram::SetModel(const UltraCanvasUMLModel& value) {
    model = value;
    selectedClassifierId.clear();
    selectedRelationshipId.clear();
    hoveredClassifierId.clear();
    MarkModelChanged();
    fitPending = true;
}

void UltraCanvasClassDiagram::Clear() {
    model.Clear();
    boxes.clear();
    boxIndexById.clear();
    edges.clear();
    selectedClassifierId.clear();
    selectedRelationshipId.clear();
    hoveredClassifierId.clear();
    MarkModelChanged();
}

void UltraCanvasClassDiagram::MarkModelChanged() {
    metricsDirty = true;
    layoutDirty = true;
    edgesDirty = true;
    RequestRedraw();
}

std::string UltraCanvasClassDiagram::AddClass(const std::string& name,
                                              const std::vector<std::string>& attributes,
                                              const std::vector<std::string>& operations) {
    std::string id = model.AddClass(name, attributes, operations);
    MarkModelChanged();
    return id;
}

std::string UltraCanvasClassDiagram::AddAbstractClass(const std::string& name,
                                                      const std::vector<std::string>& attributes,
                                                      const std::vector<std::string>& operations) {
    std::string id = model.AddAbstractClass(name, attributes, operations);
    MarkModelChanged();
    return id;
}

std::string UltraCanvasClassDiagram::AddInterface(const std::string& name,
                                                  const std::vector<std::string>& operations) {
    std::string id = model.AddInterface(name, operations);
    MarkModelChanged();
    return id;
}

std::string UltraCanvasClassDiagram::AddEnumeration(const std::string& name,
                                                    const std::vector<std::string>& literals) {
    std::string id = model.AddEnumeration(name, literals);
    MarkModelChanged();
    return id;
}

UMLRelationshipHandle UltraCanvasClassDiagram::AddAssociation(const std::string& source,
                                                              const std::string& target) {
    MarkModelChanged();
    return model.AddAssociation(source, target);
}

UMLRelationshipHandle UltraCanvasClassDiagram::AddDirectedAssociation(const std::string& source,
                                                                      const std::string& target) {
    MarkModelChanged();
    return model.AddDirectedAssociation(source, target);
}

UMLRelationshipHandle UltraCanvasClassDiagram::AddAggregation(const std::string& whole,
                                                              const std::string& part) {
    MarkModelChanged();
    return model.AddAggregation(whole, part);
}

UMLRelationshipHandle UltraCanvasClassDiagram::AddComposition(const std::string& whole,
                                                              const std::string& part) {
    MarkModelChanged();
    return model.AddComposition(whole, part);
}

UMLRelationshipHandle UltraCanvasClassDiagram::AddGeneralization(const std::string& child,
                                                                 const std::string& parent) {
    MarkModelChanged();
    return model.AddGeneralization(child, parent);
}

UMLRelationshipHandle UltraCanvasClassDiagram::AddRealization(const std::string& implementer,
                                                              const std::string& interfaceName) {
    MarkModelChanged();
    return model.AddRealization(implementer, interfaceName);
}

UMLRelationshipHandle UltraCanvasClassDiagram::AddDependency(const std::string& client,
                                                             const std::string& supplier,
                                                             const std::string& stereotype) {
    MarkModelChanged();
    return model.AddDependency(client, supplier, stereotype);
}

// =============================================================================
// PRESENTATION
// =============================================================================

void UltraCanvasClassDiagram::ApplyThemeColors(ClassDiagramTheme value) {
    switch (value) {
        case ClassDiagramTheme::Default:
            style.backgroundColor = Color(250, 250, 252, 255);
            style.headerColor = Color(224, 231, 240, 255);
            style.bodyColor = Color(255, 255, 255, 255);
            style.borderColor = Color(90, 100, 115, 255);
            style.separatorColor = Color(90, 100, 115, 255);
            style.gridColor = Color(236, 239, 244, 255);
            style.lineColor = Color(70, 80, 95, 255);
            break;

        case ClassDiagramTheme::Professional:
            style.backgroundColor = Color(252, 252, 253, 255);
            style.headerColor = Color(236, 240, 245, 255);
            style.bodyColor = Color(255, 255, 255, 255);
            style.borderColor = Color(120, 132, 148, 255);
            style.separatorColor = Color(198, 206, 216, 255);
            style.borderWidth = 1.2;
            style.cornerRadius = 3.0;
            style.gridColor = Color(243, 245, 248, 255);
            style.lineColor = Color(100, 112, 128, 255);
            break;

        case ClassDiagramTheme::Blueprint:
            // Reference image 1: yellow header band, navy border, graph paper.
            style.backgroundColor = Color(252, 253, 255, 255);
            style.headerColor = Color(255, 224, 130, 255);
            style.bodyColor = Color(255, 255, 255, 255);
            style.borderColor = Color(45, 62, 80, 255);
            style.separatorColor = Color(45, 62, 80, 255);
            style.borderWidth = 2.0;
            style.cornerRadius = 6.0;
            style.gridColor = Color(226, 234, 244, 255);
            style.showGrid = true;
            style.lineColor = Color(45, 62, 80, 255);
            style.nameTextColor = Color(35, 45, 60, 255);
            style.memberTextColor = Color(45, 55, 70, 255);
            break;

        case ClassDiagramTheme::Colorful:
            style.backgroundColor = Color(250, 250, 252, 255);
            style.bodyColor = Color(255, 255, 255, 255);
            style.borderColor = Color(120, 128, 140, 255);
            style.separatorColor = Color(210, 216, 224, 255);
            style.borderWidth = 1.4;
            style.cornerRadius = 4.0;
            style.gridColor = Color(240, 242, 246, 255);
            style.showGrid = false;
            style.lineColor = Color(110, 120, 135, 255);
            break;

        case ClassDiagramTheme::DataType:
            // Reference image 5: pale blue boxes with a «DataType» band.
            style.backgroundColor = Color(255, 255, 255, 255);
            style.headerColor = Color(198, 217, 236, 255);
            style.bodyColor = Color(233, 241, 249, 255);
            style.borderColor = Color(70, 100, 135, 255);
            style.separatorColor = Color(120, 150, 180, 255);
            style.borderWidth = 1.4;
            style.cornerRadius = 0.0;
            style.showGrid = false;
            style.lineColor = Color(70, 100, 135, 255);
            break;

        case ClassDiagramTheme::Minimal:
            style.backgroundColor = Color(255, 255, 255, 255);
            style.headerColor = Color(255, 255, 255, 255);
            style.bodyColor = Color(255, 255, 255, 255);
            style.borderColor = Color(60, 60, 60, 255);
            style.separatorColor = Color(60, 60, 60, 255);
            style.borderWidth = 1.0;
            style.cornerRadius = 0.0;
            style.showGrid = false;
            style.lineColor = Color(60, 60, 60, 255);
            break;

        case ClassDiagramTheme::Dark:
            style.backgroundColor = Color(30, 33, 40, 255);
            style.headerColor = Color(58, 66, 82, 255);
            style.bodyColor = Color(42, 47, 58, 255);
            style.borderColor = Color(120, 132, 150, 255);
            style.separatorColor = Color(90, 100, 118, 255);
            style.gridColor = Color(40, 44, 54, 255);
            style.lineColor = Color(150, 162, 180, 255);
            style.nameTextColor = Color(232, 236, 244, 255);
            style.memberTextColor = Color(206, 213, 224, 255);
            style.stereotypeTextColor = Color(160, 172, 190, 255);
            style.labelTextColor = Color(206, 213, 224, 255);
            style.labelBackgroundColor = Color(30, 33, 40, 220);
            style.titleColor = Color(226, 232, 242, 255);
            break;
    }
}

void UltraCanvasClassDiagram::SetTheme(ClassDiagramTheme value) {
    theme = value;
    ApplyThemeColors(value);
    if (value == ClassDiagramTheme::Colorful) {
        ApplyPaletteByClassifier();
    }
    metricsDirty = true;
    RequestRedraw();
}

void UltraCanvasClassDiagram::SetStyle(const ClassDiagramStyle& value) {
    style = value;
    metricsDirty = true;
    layoutDirty = true;
    RequestRedraw();
}

void UltraCanvasClassDiagram::SetGridVisible(bool visible, double spacing) {
    style.showGrid = visible;
    if (spacing > 0.0) style.gridSpacing = spacing;
    RequestRedraw();
}

void UltraCanvasClassDiagram::SetBackgroundColor(const Color& color) {
    style.backgroundColor = color;
    RequestRedraw();
}

void UltraCanvasClassDiagram::SetTitle(const std::string& title) {
    model.SetTitle(title);
    RequestRedraw();
}

void UltraCanvasClassDiagram::SetDetailLevel(ClassDiagramDetail value) {
    detail = value;
    metricsDirty = true;
    layoutDirty = true;
    RequestRedraw();
}

void UltraCanvasClassDiagram::SetClassifierColors(const std::string& idOrName,
                                                  const Color& header, const Color& body) {
    UMLClassifier* classifier = model.GetClassifier(model.ResolveClassifierId(idOrName));
    if (!classifier) return;
    classifier->style.hasCustomColors = true;
    classifier->style.headerColor =
        (static_cast<unsigned int>(header.r) << 24) | (static_cast<unsigned int>(header.g) << 16) |
        (static_cast<unsigned int>(header.b) << 8) | static_cast<unsigned int>(header.a);
    classifier->style.bodyColor =
        (static_cast<unsigned int>(body.r) << 24) | (static_cast<unsigned int>(body.g) << 16) |
        (static_cast<unsigned int>(body.b) << 8) | static_cast<unsigned int>(body.a);
    metricsDirty = true;
    RequestRedraw();
}

void UltraCanvasClassDiagram::SetClassifierCollapsed(const std::string& idOrName, bool collapsed) {
    UMLClassifier* classifier = model.GetClassifier(model.ResolveClassifierId(idOrName));
    if (!classifier) return;
    classifier->collapsed = collapsed;
    metricsDirty = true;
    layoutDirty = true;
    RequestRedraw();
}

void UltraCanvasClassDiagram::AddStereotypeStyle(const ClassStereotypeStyle& rule) {
    stereotypeStyles.push_back(rule);
    metricsDirty = true;
    RequestRedraw();
}

void UltraCanvasClassDiagram::ClearStereotypeStyles() {
    stereotypeStyles.clear();
    metricsDirty = true;
    RequestRedraw();
}

void UltraCanvasClassDiagram::ApplyPaletteByClassifier() {
    size_t index = 0;
    for (auto& classifier : model.Classifiers()) {
        Color header = kPalette[index % kPaletteCount];
        SetClassifierColors(classifier.id, header, Lighten(header, 0.86));
        ++index;
    }
}

void UltraCanvasClassDiagram::ApplyPaletteByPackage() {
    std::map<std::string, size_t> groupIndex;
    for (auto& classifier : model.Classifiers()) {
        std::string key = !classifier.packageId.empty() ? classifier.packageId
                                                        : classifier.sourceFile;
        auto it = groupIndex.find(key);
        if (it == groupIndex.end()) {
            it = groupIndex.emplace(key, groupIndex.size()).first;
        }
        Color header = kPalette[it->second % kPaletteCount];
        SetClassifierColors(classifier.id, header, Lighten(header, 0.86));
    }
}

void UltraCanvasClassDiagram::SetShowPrivateMembers(bool show) {
    showPrivateMembers = show;
    metricsDirty = true;
    layoutDirty = true;
    RequestRedraw();
}

void UltraCanvasClassDiagram::SetShowOperations(bool show) {
    showOperations = show;
    metricsDirty = true;
    layoutDirty = true;
    RequestRedraw();
}

void UltraCanvasClassDiagram::SetShowAttributes(bool show) {
    showAttributes = show;
    metricsDirty = true;
    layoutDirty = true;
    RequestRedraw();
}

// =============================================================================
// LAYOUT AND VIEW
// =============================================================================

void UltraCanvasClassDiagram::SetLayout(ClassLayoutKind kind, ClassLayoutDirection direction) {
    layoutOptions.kind = kind;
    layoutOptions.direction = direction;
    layoutDirty = true;
    RequestRedraw();
}

void UltraCanvasClassDiagram::RunLayout() {
    layoutDirty = true;
    fitPending = true;
    RequestRedraw();
}

void UltraCanvasClassDiagram::SetClassifierPosition(const std::string& idOrName,
                                                    double x, double y, bool pin) {
    UMLClassifier* classifier = model.GetClassifier(model.ResolveClassifierId(idOrName));
    if (!classifier) return;
    classifier->x = x;
    classifier->y = y;
    classifier->positionPinned = pin;
    metricsDirty = true;
    edgesDirty = true;
    RequestRedraw();
}

void UltraCanvasClassDiagram::SetRouting(ClassDiagramRouting value) {
    routing = value;
    edgesDirty = true;
    RequestRedraw();
}

void UltraCanvasClassDiagram::SetZoomLevel(double zoom) {
    zoomLevel = std::clamp(zoom, 0.1, 10.0);
    RequestRedraw();
}

void UltraCanvasClassDiagram::SetPanOffset(double x, double y) {
    panOffset = Point2Dd(x, y);
    RequestRedraw();
}

void UltraCanvasClassDiagram::FitView() {
    fitPending = true;
    RequestRedraw();
}

void UltraCanvasClassDiagram::ResetView() {
    zoomLevel = 1.0;
    panOffset = Point2Dd(0, 0);
    RequestRedraw();
}

void UltraCanvasClassDiagram::SetInteractive(bool value) {
    interactive = value;
}

void UltraCanvasClassDiagram::ApplyFit() {
    if (contentBounds.width <= 0.0 || contentBounds.height <= 0.0) return;
    Rect2Di bounds = GetLocalBounds();
    if (bounds.width <= 0 || bounds.height <= 0) return;

    const double margin = 24.0;
    // Leave room under the drawing for the title block, which is rendered
    // below the content bounds.
    double titleAllowance = model.Title().empty() ? 0.0 : style.titleFontSize * 2.4;
    double scaleX = (bounds.width - margin * 2.0) / contentBounds.width;
    double scaleY = (bounds.height - margin * 2.0) /
                    (contentBounds.height + titleAllowance);
    // A few percent of slack: edge decorations, end labels and the rounded
    // elbows all draw slightly outside the box rectangles the content bounds
    // are computed from, so fitting exactly to those bounds clips them.
    const double slack = 0.94;
    zoomLevel = std::clamp(std::min(scaleX, scaleY) * slack, 0.1, 2.0);

    double contentCentreX = contentBounds.x + contentBounds.width * 0.5;
    double contentCentreY = contentBounds.y + (contentBounds.height + titleAllowance) * 0.5;
    panOffset.x = bounds.width * 0.5 - contentCentreX * zoomLevel;
    panOffset.y = bounds.height * 0.5 - contentCentreY * zoomLevel;
    // ApplyFit runs inside a paint, so ask for another one: the transform it
    // just changed must be applied by a frame that starts after it.
    RequestRedraw();
}

// =============================================================================
// SELECTION
// =============================================================================

void UltraCanvasClassDiagram::SelectClassifier(const std::string& idOrName) {
    selectedClassifierId = model.ResolveClassifierId(idOrName);
    selectedRelationshipId.clear();
    if (onSelectionChanged) onSelectionChanged(selectedClassifierId);
    RequestRedraw();
}

void UltraCanvasClassDiagram::SelectRelationship(const std::string& relationshipId) {
    selectedRelationshipId = relationshipId;
    selectedClassifierId.clear();
    if (onSelectionChanged) onSelectionChanged(selectedRelationshipId);
    RequestRedraw();
}

void UltraCanvasClassDiagram::DeselectAll() {
    selectedClassifierId.clear();
    selectedRelationshipId.clear();
    if (onSelectionChanged) onSelectionChanged(std::string());
    RequestRedraw();
}

// =============================================================================
// MEASUREMENT
// =============================================================================

bool UltraCanvasClassDiagram::IsMemberVisible(const UMLMember& member) const {
    if (!showPrivateMembers && member.visibility == UMLVisibility::Private) return false;
    if (!showAttributes && member.kind == UMLMemberKind::Attribute) return false;
    if (!showOperations && member.kind == UMLMemberKind::Operation) return false;
    return true;
}

std::string UltraCanvasClassDiagram::RowText(const UMLMember& member) const {
    std::string out;
    const char* glyph = UMLVisibilityGlyph(member.visibility);
    if (glyph && *glyph) out += glyph;

    if (member.kind == UMLMemberKind::Literal) {
        out += member.name;
        if (!member.defaultValue.empty()) out += " = " + member.defaultValue;
        return out;
    }

    if (detail == ClassDiagramDetail::Signatures &&
        member.kind == UMLMemberKind::Operation) {
        // Signature level: keep the name and return type, drop the parameters.
        if (member.isDerived) out += "/";
        out += member.name + "()";
        if (!member.type.empty()) out += ": " + member.type;
        return out;
    }

    out += UMLNotation::FormatSignature(member);
    return out;
}

void UltraCanvasClassDiagram::ResolveBoxColors(const UMLClassifier& classifier,
                                               BoxMetrics& box) const {
    box.headerColor = style.headerColor;
    box.bodyColor = style.bodyColor;
    box.borderColor = style.borderColor;

    // Stereotype rules first, so «interface» and «DataType» read differently
    // from a plain class even under the same theme.
    for (const auto& rule : stereotypeStyles) {
        if (!classifier.HasStereotype(rule.stereotype)) continue;
        if (rule.hasHeaderColor) box.headerColor = rule.headerColor;
        if (rule.hasBodyColor) box.bodyColor = rule.bodyColor;
        if (rule.hasBorderColor) box.borderColor = rule.borderColor;
        break;
    }

    // Per-classifier overrides win over everything.
    if (classifier.style.hasCustomColors) {
        auto unpack = [](unsigned int packed) {
            return Color(static_cast<unsigned char>((packed >> 24) & 0xFF),
                         static_cast<unsigned char>((packed >> 16) & 0xFF),
                         static_cast<unsigned char>((packed >> 8) & 0xFF),
                         static_cast<unsigned char>(packed & 0xFF));
        };
        box.headerColor = unpack(classifier.style.headerColor);
        box.bodyColor = unpack(classifier.style.bodyColor);
    }

    box.nameItalic = (classifier.kind == UMLClassifierKind::AbstractClass);
}

void UltraCanvasClassDiagram::BuildRows(IRenderContext* ctx, const UMLClassifier& classifier,
                                        BoxMetrics& box) const {
    ctx->SetFontFace(style.memberFontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(style.memberFontSize);

    auto addRow = [&](const UMLMember& member, size_t index,
                      std::vector<RowMetrics>& target) {
        RowMetrics row;
        row.text = RowText(member);
        row.isStatic = member.isStatic;
        row.isAbstract = member.isAbstract;
        row.kind = member.kind;
        row.memberIndex = index;
        Size2Di size = ctx->GetTextLineDimensions(row.text);
        row.width = static_cast<double>(size.width);
        target.push_back(row);
    };

    // An enumeration shows its literals where a class shows its attributes.
    if (classifier.kind == UMLClassifierKind::Enumeration) {
        for (size_t i = 0; i < classifier.literals.size(); ++i) {
            addRow(classifier.literals[i], i, box.attributeRows);
        }
    } else if (showAttributes) {
        for (size_t i = 0; i < classifier.attributes.size(); ++i) {
            if (!IsMemberVisible(classifier.attributes[i])) continue;
            addRow(classifier.attributes[i], i, box.attributeRows);
        }
    }

    if (showOperations) {
        for (size_t i = 0; i < classifier.operations.size(); ++i) {
            if (!IsMemberVisible(classifier.operations[i])) continue;
            addRow(classifier.operations[i], i, box.operationRows);
        }
    }
}

void UltraCanvasClassDiagram::RebuildMetrics(IRenderContext* ctx) {
    boxes.clear();
    boxIndexById.clear();
    boxes.reserve(model.ClassifierCount());

    const double paddingX = style.compartmentPaddingX;
    const double paddingY = style.compartmentPaddingY;

    for (const auto& classifier : model.Classifiers()) {
        BoxMetrics box;
        box.classifierId = classifier.id;
        ResolveBoxColors(classifier, box);

        // ---- name band ----
        ctx->SetFontFace(style.nameFontFamily, FontWeight::Bold,
                         box.nameItalic ? FontSlant::Italic : FontSlant::Normal);
        ctx->SetFontSize(style.nameFontSize);
        Size2Di nameSize = ctx->GetTextLineDimensions(classifier.name);
        double nameHeight = static_cast<double>(nameSize.height);
        double contentWidth = static_cast<double>(nameSize.width);

        // ---- stereotype line ----
        if (!classifier.stereotypes.empty()) {
            box.stereotypeText = "«" + classifier.stereotypes.front() + "»";
            ctx->SetFontFace(style.nameFontFamily, FontWeight::Normal, FontSlant::Normal);
            ctx->SetFontSize(style.stereotypeFontSize);
            Size2Di stereotypeSize = ctx->GetTextLineDimensions(box.stereotypeText);
            box.hasStereotypeLine = true;
            box.stereotypeHeight = static_cast<double>(stereotypeSize.height);
            contentWidth = std::max(contentWidth, static_cast<double>(stereotypeSize.width));
        }

        box.headerHeight = nameHeight + box.stereotypeHeight + paddingY * 2.0;

        // ---- member rows ----
        bool collapsed = classifier.collapsed || detail == ClassDiagramDetail::NameOnly;
        if (!collapsed) {
            BuildRows(ctx, classifier, box);
            for (const auto& row : box.attributeRows) contentWidth = std::max(contentWidth, row.width);
            for (const auto& row : box.operationRows) contentWidth = std::max(contentWidth, row.width);

            ctx->SetFontFace(style.memberFontFamily, FontWeight::Normal, FontSlant::Normal);
            ctx->SetFontSize(style.memberFontSize);
            Size2Di probe = ctx->GetTextLineDimensions("Mg");
            box.rowHeight = static_cast<double>(probe.height) + 2.0;

            auto compartmentHeight = [&](size_t rowCount) {
                if (rowCount == 0) {
                    return style.drawEmptyCompartments
                        ? style.minCompartmentHeight + paddingY * 2.0
                        : 0.0;
                }
                return static_cast<double>(rowCount) * box.rowHeight + paddingY * 2.0;
            };
            box.attributeHeight = compartmentHeight(box.attributeRows.size());
            box.operationHeight = (classifier.kind == UMLClassifierKind::Enumeration)
                ? 0.0
                : compartmentHeight(box.operationRows.size());
        }

        double width = std::max(style.minBoxWidth, contentWidth + paddingX * 2.0);
        double height = box.headerHeight + box.attributeHeight + box.operationHeight;

        box.bounds = Rect2Dd(classifier.x, classifier.y, width, height);
        boxIndexById.emplace(classifier.id, boxes.size());
        boxes.push_back(std::move(box));
    }

    // Keep the content bounds honest even when no layout runs (manual
    // placement, or a re-measure that only changed box sizes).
    contentBounds = Rect2Dd(0, 0, 0, 0);
    if (!boxes.empty()) {
        double minX = boxes.front().bounds.x, minY = boxes.front().bounds.y;
        double maxX = minX, maxY = minY;
        for (const auto& box : boxes) {
            minX = std::min(minX, box.bounds.x);
            minY = std::min(minY, box.bounds.y);
            maxX = std::max(maxX, box.bounds.x + box.bounds.width);
            maxY = std::max(maxY, box.bounds.y + box.bounds.height);
        }
        contentBounds = Rect2Dd(minX, minY, maxX - minX, maxY - minY);
    }

    metricsDirty = false;
}

void UltraCanvasClassDiagram::ApplyLayout() {
    if (boxes.empty()) {
        contentBounds = Rect2Dd(0, 0, 0, 0);
        layoutDirty = false;
        return;
    }

    std::vector<ClassLayoutNode> nodes;
    nodes.reserve(boxes.size());
    for (const auto& box : boxes) {
        ClassLayoutNode node(box.classifierId, box.bounds.width, box.bounds.height);
        const UMLClassifier* classifier = model.GetClassifier(box.classifierId);
        if (classifier) {
            node.x = classifier->x;
            node.y = classifier->y;
            node.pinned = classifier->positionPinned;
        }
        nodes.push_back(node);
    }

    std::vector<ClassLayoutEdge> layoutEdges;
    layoutEdges.reserve(model.RelationshipCount());
    for (const auto& relationship : model.Relationships()) {
        bool hierarchical = relationship.kind == UMLRelationshipKind::Generalization ||
                            relationship.kind == UMLRelationshipKind::Realization;
        layoutEdges.emplace_back(relationship.source.classifierId,
                                 relationship.target.classifierId, hierarchical);
    }

    contentBounds = UltraCanvasClassLayout::Apply(nodes, layoutEdges, layoutOptions);

    // Write the result back to both the model (so it persists) and the cached
    // boxes (so this frame can draw).
    for (size_t i = 0; i < nodes.size(); ++i) {
        boxes[i].bounds.x = nodes[i].x;
        boxes[i].bounds.y = nodes[i].y;
        if (UMLClassifier* classifier = model.GetClassifier(nodes[i].id)) {
            classifier->x = nodes[i].x;
            classifier->y = nodes[i].y;
            classifier->width = nodes[i].width;
            classifier->height = nodes[i].height;
        }
    }

    layoutDirty = false;
}

void UltraCanvasClassDiagram::RebuildEdges() {
    edges.clear();
    edges.reserve(model.RelationshipCount());

    // How many relationships touch each (box, side)? Anchors are spread along
    // the face so parallel lines do not overlap (proposal E4).
    struct FaceKey {
        size_t boxIndex;
        int side;
        bool operator<(const FaceKey& other) const {
            if (boxIndex != other.boxIndex) return boxIndex < other.boxIndex;
            return side < other.side;
        }
    };
    std::map<FaceKey, int> faceCount;
    std::map<FaceKey, int> faceCursor;

    // First pass: decide the sides.
    struct PendingEdge {
        size_t sourceIndex;
        size_t targetIndex;
        DiagramCardinalSide sourceSide;
        DiagramCardinalSide targetSide;
        const UMLRelationship* relationship;
    };
    std::vector<PendingEdge> pending;
    pending.reserve(model.RelationshipCount());

    for (const auto& relationship : model.Relationships()) {
        auto sourceIt = boxIndexById.find(relationship.source.classifierId);
        auto targetIt = boxIndexById.find(relationship.target.classifierId);
        if (sourceIt == boxIndexById.end() || targetIt == boxIndexById.end()) continue;
        if (sourceIt->second == targetIt->second) continue;  // self edge: R12, not P1

        const Rect2Dd& sourceBox = boxes[sourceIt->second].bounds;
        const Rect2Dd& targetBox = boxes[targetIt->second].bounds;
        Point2Dd sourceCentre(sourceBox.x + sourceBox.width * 0.5,
                              sourceBox.y + sourceBox.height * 0.5);
        Point2Dd targetCentre(targetBox.x + targetBox.width * 0.5,
                              targetBox.y + targetBox.height * 0.5);

        PendingEdge edge;
        edge.sourceIndex = sourceIt->second;
        edge.targetIndex = targetIt->second;
        edge.sourceSide = UltraCanvasDiagramRouter::GetCardinalSide(sourceCentre, targetCentre);
        edge.targetSide = UltraCanvasDiagramRouter::GetCardinalSide(targetCentre, sourceCentre);
        edge.relationship = &relationship;
        pending.push_back(edge);

        faceCount[{edge.sourceIndex, static_cast<int>(edge.sourceSide)}]++;
        faceCount[{edge.targetIndex, static_cast<int>(edge.targetSide)}]++;
    }

    // Second pass: anchor, route.
    for (const auto& edge : pending) {
        FaceKey sourceKey{edge.sourceIndex, static_cast<int>(edge.sourceSide)};
        FaceKey targetKey{edge.targetIndex, static_cast<int>(edge.targetSide)};

        Point2Dd start = UltraCanvasDiagramRouter::GetDistributedCardinalPoint(
            boxes[edge.sourceIndex].bounds, edge.sourceSide,
            faceCursor[sourceKey]++, faceCount[sourceKey]);
        Point2Dd end = UltraCanvasDiagramRouter::GetDistributedCardinalPoint(
            boxes[edge.targetIndex].bounds, edge.targetSide,
            faceCursor[targetKey]++, faceCount[targetKey]);

        EdgeGeometry geometry;
        geometry.relationshipId = edge.relationship->id;
        geometry.sourceSide = edge.sourceSide;
        geometry.targetSide = edge.targetSide;

        if (routing == ClassDiagramRouting::Straight) {
            geometry.path = {start, end};
        } else {
            std::vector<DiagramObstacle> obstacles;
            obstacles.reserve(boxes.size());
            for (size_t i = 0; i < boxes.size(); ++i) {
                if (i == edge.sourceIndex || i == edge.targetIndex) continue;
                obstacles.emplace_back(boxes[i].bounds);
            }

            DiagramRoutingOptions options;
            options.gridSize = std::max(10.0, style.gridSpacing);
            // Give A* room around the drawing, not just the visible element.
            Rect2Dd area = contentBounds;
            area.x -= options.gridSize * 4.0;
            area.y -= options.gridSize * 4.0;
            area.width += options.gridSize * 8.0;
            area.height += options.gridSize * 8.0;
            options.routingArea = area;

            geometry.path = UltraCanvasDiagramRouter::ComputeOrthogonalPath(
                start, end, edge.sourceSide, edge.targetSide, obstacles, options);
        }

        edges.push_back(std::move(geometry));
    }
}

// =============================================================================
// RENDERING
// =============================================================================

void UltraCanvasClassDiagram::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    (void)dirtyRect;
    if (!ctx || !IsVisible()) return;

    Rect2Di bounds = GetLocalBounds();

    ctx->SetFillPaint(style.backgroundColor);
    ctx->FillRectangle(bounds);

    // Measurement needs the real context, so it happens here rather than in
    // the mutators. Both flags are sticky until satisfied.
    if (metricsDirty) { RebuildMetrics(ctx); edgesDirty = true; }
    if (layoutDirty)   { ApplyLayout();       edgesDirty = true; }
    if (edgesDirty) {
        // Routing runs A* per edge, so it is cached: only geometry changes
        // (a re-measure, a re-layout, a drag, a routing-mode switch) redo it.
        RebuildEdges();
        edgesDirty = false;
    }
    // Only consume the fit request once the element actually has bounds: the
    // first paint can arrive before the parent container has laid this element
    // out, and fitting against a zero-sized viewport silently does nothing.
    if (fitPending && bounds.width > 0 && bounds.height > 0) {
        ApplyFit();
        fitPending = false;
    }

    ctx->PushState();
    ctx->Translate(finalBounds.x + panOffset.x, finalBounds.y + panOffset.y);
    ctx->Scale(zoomLevel, zoomLevel);

    if (style.showGrid) RenderGrid(ctx);

    // Relationships first so the boxes cover the line ends.
    for (const auto& geometry : edges) {
        const UMLRelationship* relationship = model.GetRelationship(geometry.relationshipId);
        if (!relationship) continue;
        RenderEdge(ctx, *relationship, geometry);
    }
    for (const auto& geometry : edges) {
        const UMLRelationship* relationship = model.GetRelationship(geometry.relationshipId);
        if (!relationship) continue;
        RenderEdgeLabels(ctx, *relationship, geometry);
    }

    for (const auto& box : boxes) {
        RenderBox(ctx, box);
    }

    RenderTitle(ctx);

    ctx->PopState();
}

void UltraCanvasClassDiagram::RenderGrid(IRenderContext* ctx) {
    ctx->SetStrokePaint(style.gridColor);
    ctx->SetStrokeWidth(0.5);

    double spacing = style.gridSpacing > 0.0 ? style.gridSpacing : 20.0;
    double width = static_cast<double>(finalBounds.width) / zoomLevel;
    double height = static_cast<double>(finalBounds.height) / zoomLevel;
    double startX = -panOffset.x / zoomLevel;
    double startY = -panOffset.y / zoomLevel;

    double firstX = std::floor(startX / spacing) * spacing;
    double firstY = std::floor(startY / spacing) * spacing;

    for (double x = firstX; x <= startX + width; x += spacing) {
        ctx->DrawLine(Point2Dd(x, startY), Point2Dd(x, startY + height));
    }
    for (double y = firstY; y <= startY + height; y += spacing) {
        ctx->DrawLine(Point2Dd(startX, y), Point2Dd(startX + width, y));
    }
}

void UltraCanvasClassDiagram::RenderTitle(IRenderContext* ctx) {
    if (model.Title().empty()) return;

    ctx->SetFontFace(style.nameFontFamily, FontWeight::Bold, FontSlant::Normal);
    ctx->SetFontSize(style.titleFontSize);
    ctx->SetTextPaint(style.titleColor);

    // Bottom-left of the drawing, as in reference image 1.
    Size2Di size = ctx->GetTextLineDimensions(model.Title());
    double x = contentBounds.x;
    double y = contentBounds.y + contentBounds.height + 28.0;
    ctx->DrawText(model.Title(), Point2Dd(x, y));
    (void)size;
}

void UltraCanvasClassDiagram::RenderBox(IRenderContext* ctx, const BoxMetrics& box) {
    const UMLClassifier* classifier = model.GetClassifier(box.classifierId);
    if (!classifier) return;

    const Rect2Dd& rect = box.bounds;
    const double radius = style.cornerRadius;

    if (style.drawShadow) {
        ctx->SetFillPaint(style.shadowColor);
        ctx->FillRoundedRectangle(Rect2Dd(rect.x + 3, rect.y + 3, rect.width, rect.height), radius);
    }

    // Body first, then the header band on top of it.
    ctx->SetFillPaint(box.bodyColor);
    if (radius > 0.0) ctx->FillRoundedRectangle(rect, radius);
    else ctx->FillRectangle(rect);

    ctx->SetFillPaint(box.headerColor);
    if (radius > 0.0) {
        // Rounded top, square bottom: a rounded rect clipped to the band, plus
        // a plain rect covering the lower half of the corners.
        ctx->PushState();
        ctx->ClipRect(Rect2Dd(rect.x, rect.y, rect.width, box.headerHeight));
        ctx->FillRoundedRectangle(rect, radius);
        ctx->PopState();
        double flatTop = rect.y + radius;
        double flatHeight = box.headerHeight - radius;
        if (flatHeight > 0.0) {
            ctx->FillRectangle(Rect2Dd(rect.x, flatTop, rect.width, flatHeight));
        }
    } else {
        ctx->FillRectangle(Rect2Dd(rect.x, rect.y, rect.width, box.headerHeight));
    }

    // ---- header text ----
    double textY = rect.y + style.compartmentPaddingY;
    if (box.hasStereotypeLine) {
        ctx->SetFontFace(style.nameFontFamily, FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(style.stereotypeFontSize);
        ctx->SetTextPaint(style.stereotypeTextColor);
        Size2Di size = ctx->GetTextLineDimensions(box.stereotypeText);
        ctx->DrawText(box.stereotypeText,
                      Point2Dd(rect.x + (rect.width - size.width) * 0.5, textY));
        textY += box.stereotypeHeight;
    }

    ctx->SetFontFace(style.nameFontFamily, FontWeight::Bold,
                     box.nameItalic ? FontSlant::Italic : FontSlant::Normal);
    ctx->SetFontSize(style.nameFontSize);
    ctx->SetTextPaint(style.nameTextColor);
    {
        std::string name = TruncateToWidth(ctx, classifier->name,
                                           rect.width - style.compartmentPaddingX * 2.0);
        Size2Di size = ctx->GetTextLineDimensions(name);
        // DrawText takes the TOP of the text box, never the baseline.
        ctx->DrawText(name, Point2Dd(rect.x + (rect.width - size.width) * 0.5, textY));
    }

    // ---- compartments ----
    double attributeTop = rect.y + box.headerHeight;
    double operationTop = attributeTop + box.attributeHeight;

    if (box.attributeHeight > 0.0) {
        RenderRows(ctx, box, box.attributeRows, attributeTop, box.attributeHeight);
    }
    if (box.operationHeight > 0.0) {
        RenderRows(ctx, box, box.operationRows, operationTop, box.operationHeight);
    }

    // ---- separators and border ----
    ctx->SetStrokePaint(style.separatorColor);
    ctx->SetStrokeWidth(style.separatorWidth);
    if (box.attributeHeight > 0.0 || box.operationHeight > 0.0) {
        ctx->DrawLine(Point2Dd(rect.x, attributeTop),
                      Point2Dd(rect.x + rect.width, attributeTop));
    }
    if (box.attributeHeight > 0.0 && box.operationHeight > 0.0) {
        ctx->DrawLine(Point2Dd(rect.x, operationTop),
                      Point2Dd(rect.x + rect.width, operationTop));
    }

    bool selected = (box.classifierId == selectedClassifierId);
    ctx->SetStrokePaint(selected ? style.selectionColor : box.borderColor);
    ctx->SetStrokeWidth(selected ? style.selectionWidth : style.borderWidth);
    if (radius > 0.0) ctx->DrawRoundedRectangle(rect, radius);
    else ctx->DrawRectangle(rect);

    if (box.classifierId == hoveredClassifierId && !selected) {
        ctx->SetStrokePaint(style.hoverColor);
        ctx->SetStrokeWidth(style.selectionWidth);
        if (radius > 0.0) ctx->DrawRoundedRectangle(rect, radius);
        else ctx->DrawRectangle(rect);
    }
}

void UltraCanvasClassDiagram::RenderRows(IRenderContext* ctx, const BoxMetrics& box,
                                         const std::vector<RowMetrics>& rows,
                                         double top, double height) const {
    (void)height;
    if (rows.empty()) return;

    const Rect2Dd& rect = box.bounds;
    double interior = rect.width - style.compartmentPaddingX * 2.0;
    double x = rect.x + style.compartmentPaddingX;
    double y = top + style.compartmentPaddingY;

    for (size_t i = 0; i < rows.size(); ++i) {
        const RowMetrics& row = rows[i];

        bool highlighted = (box.classifierId == hoveredClassifierId) &&
                           (hoveredRowIndex == i) && (hoveredRowKind == row.kind);
        if (highlighted) {
            ctx->SetFillPaint(style.rowHighlightColor);
            ctx->FillRectangle(Rect2Dd(rect.x + 1, y - 1, rect.width - 2, box.rowHeight));
        }

        ctx->SetFontFace(style.memberFontFamily, FontWeight::Normal,
                         row.isAbstract ? FontSlant::Italic : FontSlant::Normal);
        ctx->SetFontSize(style.memberFontSize);
        ctx->SetTextPaint(style.memberTextColor);

        std::string text = TruncateToWidth(ctx, row.text, interior);
        ctx->DrawText(text, Point2Dd(x, y));

        if (row.isStatic) {
            // UML renders class-scope members underlined.
            Size2Di size = ctx->GetTextLineDimensions(text);
            double underlineY = y + size.height - 1.0;
            ctx->SetStrokePaint(style.memberTextColor);
            ctx->SetStrokeWidth(1.0);
            ctx->DrawLine(Point2Dd(x, underlineY), Point2Dd(x + size.width, underlineY));
        }

        y += box.rowHeight;
    }
}

std::string UltraCanvasClassDiagram::TruncateToWidth(IRenderContext* ctx,
                                                     const std::string& text,
                                                     double maxWidth) const {
    if (text.empty() || maxWidth <= 0.0) return text;
    Size2Di full = ctx->GetTextLineDimensions(text);
    if (static_cast<double>(full.width) <= maxWidth) return text;

    const std::string ellipsis = "…";
    std::string candidate = text;
    // Trim whole UTF-8 code points, not bytes.
    while (!candidate.empty()) {
        do {
            candidate.pop_back();
        } while (!candidate.empty() &&
                 (static_cast<unsigned char>(candidate.back()) & 0xC0) == 0x80);

        Size2Di size = ctx->GetTextLineDimensions(candidate + ellipsis);
        if (static_cast<double>(size.width) <= maxWidth) return candidate + ellipsis;
    }
    return ellipsis;
}

// =============================================================================
// RELATIONSHIP RENDERING
// =============================================================================

void UltraCanvasClassDiagram::DrawRoundedPolyline(IRenderContext* ctx,
                                                  const std::vector<Point2Dd>& path,
                                                  double radius) const {
    if (path.size() < 2) return;
    if (radius <= 0.0 || path.size() == 2) {
        for (size_t i = 1; i < path.size(); ++i) {
            ctx->DrawLine(path[i - 1], path[i]);
        }
        return;
    }

    // Straight run up to each corner, then a short chamfer through it. A
    // two-segment chamfer reads as a rounded elbow at diagram line widths and
    // needs no curve primitive.
    Point2Dd cursor = path.front();
    for (size_t i = 1; i + 1 < path.size(); ++i) {
        const Point2Dd& corner = path[i];
        const Point2Dd& next = path[i + 1];

        double inLength = std::abs(corner.x - cursor.x) + std::abs(corner.y - cursor.y);
        double outLength = std::abs(next.x - corner.x) + std::abs(next.y - corner.y);
        double r = std::min({radius, inLength * 0.5, outLength * 0.5});
        if (r <= 0.5) {
            ctx->DrawLine(cursor, corner);
            cursor = corner;
            continue;
        }

        auto towards = [](const Point2Dd& from, const Point2Dd& to, double distance) {
            double dx = to.x - from.x, dy = to.y - from.y;
            double length = std::sqrt(dx * dx + dy * dy);
            if (length < 1e-9) return from;
            return Point2Dd(from.x + dx / length * distance, from.y + dy / length * distance);
        };

        Point2Dd beforeCorner = towards(corner, cursor, r);
        Point2Dd afterCorner = towards(corner, next, r);
        Point2Dd midpoint((beforeCorner.x + afterCorner.x) * 0.5,
                          (beforeCorner.y + afterCorner.y) * 0.5);
        // Pull the chamfer towards the corner so it looks like an arc, not a
        // cut corner.
        Point2Dd bend((midpoint.x + corner.x) * 0.5, (midpoint.y + corner.y) * 0.5);

        ctx->DrawLine(cursor, beforeCorner);
        ctx->DrawLine(beforeCorner, bend);
        ctx->DrawLine(bend, afterCorner);
        cursor = afterCorner;
    }
    ctx->DrawLine(cursor, path.back());
}

void UltraCanvasClassDiagram::DrawClosedTriangle(IRenderContext* ctx, const Point2Dd& tip,
                                                 double angle, const Color& fill,
                                                 const Color& stroke) const {
    double size = style.decorationSize;
    double half = size * 0.5;
    // `angle` points INTO the tip, so the base sits back along it.
    Point2Dd base(tip.x - std::cos(angle) * size, tip.y - std::sin(angle) * size);
    Point2Dd normal(-std::sin(angle), std::cos(angle));

    std::vector<Point2Dd> points = {
        tip,
        Point2Dd(base.x + normal.x * half, base.y + normal.y * half),
        Point2Dd(base.x - normal.x * half, base.y - normal.y * half)
    };

    ctx->SetFillPaint(fill);
    ctx->FillLinePath(points);
    ctx->SetStrokePaint(stroke);
    ctx->SetStrokeWidth(style.lineWidth);
    ctx->DrawLinePath(points, true);
}

void UltraCanvasClassDiagram::DrawDiamond(IRenderContext* ctx, const Point2Dd& tip,
                                          double angle, bool filled) const {
    double length = style.decorationSize * 1.15;
    double half = style.decorationSize * 0.38;
    Point2Dd direction(std::cos(angle), std::sin(angle));
    Point2Dd normal(-std::sin(angle), std::cos(angle));

    Point2Dd back(tip.x + direction.x * length, tip.y + direction.y * length);
    Point2Dd middle(tip.x + direction.x * length * 0.5, tip.y + direction.y * length * 0.5);

    std::vector<Point2Dd> points = {
        tip,
        Point2Dd(middle.x + normal.x * half, middle.y + normal.y * half),
        back,
        Point2Dd(middle.x - normal.x * half, middle.y - normal.y * half)
    };

    ctx->SetFillPaint(filled ? style.lineColor : style.bodyColor);
    ctx->FillLinePath(points);
    ctx->SetStrokePaint(style.lineColor);
    ctx->SetStrokeWidth(style.lineWidth);
    ctx->DrawLinePath(points, true);
}

void UltraCanvasClassDiagram::DrawOpenArrow(IRenderContext* ctx, const Point2Dd& tip,
                                            double angle) const {
    double size = style.decorationSize * 0.9;
    const double spread = 0.42;  // radians either side

    ctx->SetStrokePaint(style.lineColor);
    ctx->SetStrokeWidth(style.lineWidth);
    ctx->DrawLine(tip, Point2Dd(tip.x - std::cos(angle - spread) * size,
                                tip.y - std::sin(angle - spread) * size));
    ctx->DrawLine(tip, Point2Dd(tip.x - std::cos(angle + spread) * size,
                                tip.y - std::sin(angle + spread) * size));
}

void UltraCanvasClassDiagram::RenderEdge(IRenderContext* ctx,
                                         const UMLRelationship& relationship,
                                         const EdgeGeometry& geometry) {
    if (geometry.path.size() < 2) return;

    bool selected = (relationship.id == selectedRelationshipId);
    Color lineColor = selected ? style.selectionColor : style.lineColor;

    ctx->SetStrokePaint(lineColor);
    ctx->SetStrokeWidth(selected ? style.lineWidth + 1.0 : style.lineWidth);
    if (relationship.IsDashed()) ctx->SetLineDash(DashedPattern());

    // The line stops short of the box so a filled decoration is not drawn over
    // it, and so an arrow tip touches the border cleanly (proposal E10).
    std::vector<Point2Dd> path = geometry.path;
    double targetInset = 0.0;
    switch (relationship.kind) {
        case UMLRelationshipKind::Generalization:
        case UMLRelationshipKind::Realization:
            targetInset = style.decorationSize;
            break;
        case UMLRelationshipKind::DirectedAssociation:
        case UMLRelationshipKind::Dependency:
            targetInset = style.borderWidth * 0.5 + 1.0;
            break;
        default:
            break;
    }

    double targetAngle = (routing == ClassDiagramRouting::Straight)
        ? UltraCanvasDiagramRouter::ComputeFinalSegmentAngle(path)
        : UltraCanvasDiagramRouter::ComputeApproachAngle(geometry.targetSide);

    if (targetInset > 0.0 && path.size() >= 2) {
        Point2Dd& last = path.back();
        last.x -= std::cos(targetAngle) * targetInset;
        last.y -= std::sin(targetAngle) * targetInset;
    }

    // The source end retreats for a diamond, which sits ON the source box.
    double sourceInset = 0.0;
    if (relationship.kind == UMLRelationshipKind::Composition ||
        relationship.kind == UMLRelationshipKind::Aggregation) {
        sourceInset = style.decorationSize * 1.15;
    }
    double sourceAngle = UltraCanvasDiagramRouter::ComputeApproachAngle(geometry.sourceSide);
    // ComputeApproachAngle points INTO the source box; travelling away from it
    // is the opposite direction.
    double outwardAngle = sourceAngle + kPi;
    if (sourceInset > 0.0) {
        Point2Dd& first = path.front();
        first.x += std::cos(outwardAngle) * sourceInset;
        first.y += std::sin(outwardAngle) * sourceInset;
    }

    DrawRoundedPolyline(ctx, path, style.cornerRounding);

    if (relationship.IsDashed()) ctx->SetLineDash(UCDashPattern::EMPTY);

    // ---- end decorations ----
    const Point2Dd& tip = geometry.path.back();
    switch (relationship.kind) {
        case UMLRelationshipKind::Generalization:
        case UMLRelationshipKind::Realization:
            // Hollow closed triangle at the parent / interface.
            DrawClosedTriangle(ctx, tip, targetAngle, style.bodyColor, lineColor);
            break;
        case UMLRelationshipKind::DirectedAssociation:
        case UMLRelationshipKind::Dependency:
            DrawOpenArrow(ctx, tip, targetAngle);
            break;
        case UMLRelationshipKind::Composition:
            DrawDiamond(ctx, geometry.path.front(), outwardAngle, true);
            break;
        case UMLRelationshipKind::Aggregation:
            DrawDiamond(ctx, geometry.path.front(), outwardAngle, false);
            break;
        case UMLRelationshipKind::Association:
        case UMLRelationshipKind::NAry:
            break;
    }
}

void UltraCanvasClassDiagram::DrawLabelChip(IRenderContext* ctx, const std::string& text,
                                            const Point2Dd& centre) const {
    if (text.empty()) return;

    ctx->SetFontFace(style.nameFontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(style.labelFontSize);
    Size2Di size = ctx->GetTextLineDimensions(text);

    const double padX = 3.0, padY = 1.0;
    Rect2Dd chip(centre.x - size.width * 0.5 - padX,
                 centre.y - size.height * 0.5 - padY,
                 size.width + padX * 2.0, size.height + padY * 2.0);

    ctx->SetFillPaint(style.labelBackgroundColor);
    ctx->FillRectangle(chip);

    ctx->SetTextPaint(style.labelTextColor);
    ctx->DrawText(text, Point2Dd(centre.x - size.width * 0.5, chip.y + padY));
}

void UltraCanvasClassDiagram::RenderEdgeLabels(IRenderContext* ctx,
                                               const UMLRelationship& relationship,
                                               const EdgeGeometry& geometry) {
    if (geometry.path.size() < 2) return;

    // Association name (plus stereotype) at the midpoint of the longest
    // segment, which is never a corner.
    std::string centreText = relationship.name;
    if (!relationship.stereotype.empty()) {
        std::string marker = "«" + relationship.stereotype + "»";
        centreText = centreText.empty() ? marker : (marker + " " + centreText);
    }
    if (relationship.nameDirection == UMLReadingDirection::SourceToTarget &&
        !relationship.name.empty()) {
        centreText += " ▸";
    } else if (relationship.nameDirection == UMLReadingDirection::TargetToSource &&
               !relationship.name.empty()) {
        centreText = "◂ " + centreText;
    }
    if (!centreText.empty()) {
        DrawLabelChip(ctx, centreText,
                      UltraCanvasDiagramRouter::ComputeLongestSegmentAnchor(geometry.path));
    }

    // End labels sit just outside their own box, along the first/last segment,
    // offset perpendicular so the multiplicity and the role do not collide.
    auto placeEndLabels = [&](const Point2Dd& anchor, const Point2Dd& towards,
                              const std::string& multiplicity, const std::string& role) {
        if (multiplicity.empty() && role.empty()) return;

        double dx = towards.x - anchor.x;
        double dy = towards.y - anchor.y;
        double length = std::sqrt(dx * dx + dy * dy);
        if (length < 1e-6) return;
        dx /= length;
        dy /= length;
        double perpX = -dy, perpY = dx;

        const double along = 16.0;
        const double away = 9.0;
        if (!multiplicity.empty()) {
            DrawLabelChip(ctx, multiplicity,
                          Point2Dd(anchor.x + dx * along + perpX * away,
                                   anchor.y + dy * along + perpY * away));
        }
        if (!role.empty()) {
            DrawLabelChip(ctx, role,
                          Point2Dd(anchor.x + dx * along - perpX * away,
                                   anchor.y + dy * along - perpY * away));
        }
    };

    placeEndLabels(geometry.path.front(), geometry.path[1],
                   relationship.source.multiplicity, relationship.source.roleName);
    placeEndLabels(geometry.path.back(), geometry.path[geometry.path.size() - 2],
                   relationship.target.multiplicity, relationship.target.roleName);
}

// =============================================================================
// HIT TESTING
// =============================================================================

Point2Dd UltraCanvasClassDiagram::ScreenToWorld(const Point2Di& screenPos) const {
    return Point2Dd((screenPos.x - finalBounds.x - panOffset.x) / zoomLevel,
                    (screenPos.y - finalBounds.y - panOffset.y) / zoomLevel);
}

const UltraCanvasClassDiagram::BoxMetrics*
UltraCanvasClassDiagram::FindBoxAt(const Point2Dd& worldPos) const {
    // Reverse order so the box drawn last (on top) wins the click.
    for (size_t i = boxes.size(); i-- > 0;) {
        const Rect2Dd& rect = boxes[i].bounds;
        if (worldPos.x >= rect.x && worldPos.x <= rect.x + rect.width &&
            worldPos.y >= rect.y && worldPos.y <= rect.y + rect.height) {
            return &boxes[i];
        }
    }
    return nullptr;
}

size_t UltraCanvasClassDiagram::FindRowAt(const BoxMetrics& box, const Point2Dd& worldPos,
                                          UMLMemberKind& outKind) const {
    if (box.rowHeight <= 0.0) return static_cast<size_t>(-1);

    double attributeTop = box.bounds.y + box.headerHeight + style.compartmentPaddingY;
    double operationTop = box.bounds.y + box.headerHeight + box.attributeHeight +
                          style.compartmentPaddingY;

    if (!box.attributeRows.empty() && worldPos.y >= attributeTop) {
        double offset = worldPos.y - attributeTop;
        size_t index = static_cast<size_t>(offset / box.rowHeight);
        if (index < box.attributeRows.size()) {
            outKind = box.attributeRows[index].kind;
            return index;
        }
    }
    if (!box.operationRows.empty() && worldPos.y >= operationTop) {
        double offset = worldPos.y - operationTop;
        size_t index = static_cast<size_t>(offset / box.rowHeight);
        if (index < box.operationRows.size()) {
            outKind = box.operationRows[index].kind;
            return index;
        }
    }
    return static_cast<size_t>(-1);
}

double UltraCanvasClassDiagram::DistanceToPolyline(const Point2Dd& point,
                                                   const std::vector<Point2Dd>& path) {
    double best = std::numeric_limits<double>::max();
    for (size_t i = 1; i < path.size(); ++i) {
        const Point2Dd& a = path[i - 1];
        const Point2Dd& b = path[i];
        double dx = b.x - a.x, dy = b.y - a.y;
        double lengthSquared = dx * dx + dy * dy;
        double t = (lengthSquared < 1e-9)
            ? 0.0
            : std::clamp(((point.x - a.x) * dx + (point.y - a.y) * dy) / lengthSquared, 0.0, 1.0);
        double px = a.x + t * dx, py = a.y + t * dy;
        best = std::min(best, std::sqrt((point.x - px) * (point.x - px) +
                                        (point.y - py) * (point.y - py)));
    }
    return best;
}

std::string UltraCanvasClassDiagram::FindRelationshipAt(const Point2Dd& worldPos) const {
    const double tolerance = 6.0;
    for (const auto& geometry : edges) {
        if (DistanceToPolyline(worldPos, geometry.path) <= tolerance) {
            return geometry.relationshipId;
        }
    }
    return std::string();
}

// =============================================================================
// EVENTS
// =============================================================================

bool UltraCanvasClassDiagram::OnEvent(const UCEvent& event) {
    switch (event.type) {
        case UCEventType::MouseDown:        return HandleMouseDown(event);
        case UCEventType::MouseUp:          return HandleMouseUp(event);
        case UCEventType::MouseMove:        return HandleMouseMove(event);
        case UCEventType::MouseWheel:       return HandleMouseWheel(event);
        case UCEventType::MouseDoubleClick: return HandleDoubleClick(event);
        default: break;
    }
    return UltraCanvasUIElement::OnEvent(event);
}

bool UltraCanvasClassDiagram::HandleMouseDown(const UCEvent& event) {
    Point2Di mousePos = event.pointer;

    if (event.button == UCMouseButton::Middle) {
        isPanning = true;
        panStartScreen = mousePos;
        panStartOffset = panOffset;
        return true;
    }

    Point2Dd worldPos = ScreenToWorld(mousePos);

    if (event.button == UCMouseButton::Right) {
        if (onCanvasRightClick) onCanvasRightClick(worldPos.x, worldPos.y);
        isPanning = true;
        panStartScreen = mousePos;
        panStartOffset = panOffset;
        return true;
    }

    if (event.button != UCMouseButton::Left) return false;

    const BoxMetrics* box = FindBoxAt(worldPos);
    if (box) {
        selectedClassifierId = box->classifierId;
        selectedRelationshipId.clear();

        UMLMemberKind kind = UMLMemberKind::Attribute;
        size_t row = FindRowAt(*box, worldPos, kind);
        if (row != static_cast<size_t>(-1) && onMemberClick) {
            const std::vector<RowMetrics>& rows = (kind == UMLMemberKind::Operation)
                ? box->operationRows : box->attributeRows;
            onMemberClick(box->classifierId, kind, rows[row].memberIndex);
        }

        if (onClassClick) onClassClick(box->classifierId);
        if (onSelectionChanged) onSelectionChanged(box->classifierId);

        if (interactive) {
            isDraggingBox = true;
            draggedClassifierId = box->classifierId;
            dragStartWorld = worldPos;
            dragStartBoxOrigin = Point2Dd(box->bounds.x, box->bounds.y);
        }
        RequestRedraw();
        return true;
    }

    std::string relationshipId = FindRelationshipAt(worldPos);
    if (!relationshipId.empty()) {
        selectedRelationshipId = relationshipId;
        selectedClassifierId.clear();
        if (onRelationshipClick) onRelationshipClick(relationshipId);
        if (onSelectionChanged) onSelectionChanged(relationshipId);
        RequestRedraw();
        return true;
    }

    // Empty canvas: deselect and start panning.
    selectedClassifierId.clear();
    selectedRelationshipId.clear();
    isPanning = true;
    panStartScreen = mousePos;
    panStartOffset = panOffset;
    if (onSelectionChanged) onSelectionChanged(std::string());
    RequestRedraw();
    return true;
}

bool UltraCanvasClassDiagram::HandleMouseUp(const UCEvent& event) {
    (void)event;
    isPanning = false;
    isDraggingBox = false;
    draggedClassifierId.clear();
    return true;
}

bool UltraCanvasClassDiagram::HandleMouseMove(const UCEvent& event) {
    Point2Di mousePos = event.pointer;

    if (isPanning) {
        panOffset.x = panStartOffset.x + (mousePos.x - panStartScreen.x);
        panOffset.y = panStartOffset.y + (mousePos.y - panStartScreen.y);
        RequestRedraw();
        return true;
    }

    Point2Dd worldPos = ScreenToWorld(mousePos);

    if (isDraggingBox && !draggedClassifierId.empty()) {
        double newX = dragStartBoxOrigin.x + (worldPos.x - dragStartWorld.x);
        double newY = dragStartBoxOrigin.y + (worldPos.y - dragStartWorld.y);
        SetClassifierPosition(draggedClassifierId, newX, newY, true);
        return true;
    }

    // Hover, including which member row is under the cursor.
    std::string previousClassifier = hoveredClassifierId;
    size_t previousRow = hoveredRowIndex;

    hoveredClassifierId.clear();
    hoveredRowIndex = static_cast<size_t>(-1);

    if (const BoxMetrics* box = FindBoxAt(worldPos)) {
        hoveredClassifierId = box->classifierId;
        UMLMemberKind kind = UMLMemberKind::Attribute;
        hoveredRowIndex = FindRowAt(*box, worldPos, kind);
        hoveredRowKind = kind;
    }

    if (previousClassifier != hoveredClassifierId || previousRow != hoveredRowIndex) {
        RequestRedraw();
    }
    return true;
}

// One zoom step about the cursor: the world point under it stays put. A wheel
// notch is eased in as a run of these (UltraCanvasSmoothZoom), and applying them
// in a row about the same cursor is exactly applying their product once.
void UltraCanvasClassDiagram::ApplyZoomFactorAtCursor(double factor,
                                                      const Point2Di& cursor) {
    Point2Dd worldBefore = ScreenToWorld(cursor);
    double newZoom = std::clamp(zoomLevel * factor, 0.1, 10.0);
    if (newZoom == zoomLevel) return;

    zoomLevel = newZoom;
    Point2Dd worldAfter = ScreenToWorld(cursor);
    panOffset.x += (worldAfter.x - worldBefore.x) * zoomLevel;
    panOffset.y += (worldAfter.y - worldBefore.y) * zoomLevel;
}

bool UltraCanvasClassDiagram::HandleMouseWheel(const UCEvent& event) {
    if (!zoomAnim.IsBound()) {
        zoomAnim.Bind([this](double f) { ApplyZoomFactorAtCursor(f, zoomCursor); },
                      [this] { RequestRedraw(); });
    }
    zoomCursor = event.pointer;
    zoomAnim.ZoomBy((event.wheelDelta > 0) ? 1.1 : 0.9, zoomLevel, 0.1, 10.0);
    return true;
}

bool UltraCanvasClassDiagram::HandleDoubleClick(const UCEvent& event) {
    Point2Dd worldPos = ScreenToWorld(event.pointer);
    const BoxMetrics* box = FindBoxAt(worldPos);
    if (!box) return false;

    std::string classifierId = box->classifierId;
    if (onClassDoubleClick) onClassDoubleClick(classifierId);

    // Collapse / expand (proposal I13).
    if (interactive) {
        const UMLClassifier* classifier = model.GetClassifier(classifierId);
        if (classifier) SetClassifierCollapsed(classifierId, !classifier->collapsed);
    }
    return true;
}

} // namespace UltraCanvas
