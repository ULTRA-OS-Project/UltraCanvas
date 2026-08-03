// Apps/DemoApp/UltraCanvasClassDiagramExamples.cpp
// UML class diagram demo - five scenarios reproducing the reference notation
// Version: 1.0.0
// Last Modified: 2026-08-02
// Author: UltraCanvas Framework
//
// Scenarios:
//   1. Banking System        - the canonical UML teaching diagram: generalization
//                              triangles, aggregation and composition diamonds,
//                              per-end multiplicities, Blueprint theme, graph paper.
//   2. Interfaces            - «interface» stereotypes, dashed realization and
//                              dependency lines, collapsed name-only badges.
//   3. Service Metadata      - «DataType» boxes with member-level multiplicity,
//                              association names, role names and multiplicities
//                              on the same edge.
//   4. Domain Model          - a colour per class, named associations, a denser
//                              graph laid out left to right.
//   5. Reverse Engineered    - a model built at runtime by the C++ header
//                              scanner, so the demo documents the framework
//                              with the framework.
//
// Every scenario shares one diagram element; the buttons repopulate it.

#include "UltraCanvasDemo.h"
#include "Plugins/Diagrams/UltraCanvasClassDiagram.h"
#include "Plugins/Diagrams/UltraCanvasCppReverseEngineer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasCheckbox.h"

#include <filesystem>
#include <string>
#include <vector>

namespace UltraCanvas {

namespace {

// =============================================================================
// SCENARIO 1 - Banking system (reference images 1 and 2)
// =============================================================================

void BuildBankingSystem(UltraCanvasClassDiagram& diagram) {
    diagram.Clear();
    diagram.SetTheme(ClassDiagramTheme::Blueprint);
    diagram.SetDetailLevel(ClassDiagramDetail::Full);
    diagram.SetTitle("Class Diagram for a Banking System");

    diagram.AddClass("Bank",
        {"+BankId: int", "+Name: string", "+Location: string"},
        {});

    diagram.AddClass("Teller",
        {"+Id: int", "+Name: string"},
        {"+CollectMoney()", "+OpenAccount()", "+CloseAccount()",
         "+LoanRequest()", "+ProvideInfo()", "+IssueCard()"});

    diagram.AddClass("Customer",
        {"+Id: int", "+Name: string", "+Adress: string",
         "+PhoneNo: int", "+AcctNo: int"},
        {"+GeneralInquiry()", "+DepositMoney()", "+WithdrawMoney()",
         "+OpenAccount()", "+CloseAccount()", "+ApplyForLoan()", "+RequestCard()"});

    // Account is the generalization root; Checking and Savings specialise it.
    // It has no operations of its own, and UML still draws the empty band.
    diagram.AddAbstractClass("Account", {"+Id: int", "+CustomerId: int"}, {});
    diagram.AddClass("Checking", {"+Id: int", "+CustomerId: int"}, {});
    diagram.AddClass("Savings", {"+Id: int", "+CustomerId: int"}, {});

    diagram.AddClass("Loan",
        {"+Id: int", "+Type: string", "+AccountId: int", "+CustomerId: int"},
        {});

    // Inheritance: hollow triangle at the parent.
    diagram.AddGeneralization("Checking", "Account");
    diagram.AddGeneralization("Savings", "Account");

    // A bank has tellers and customers; the parts outlive the whole, so these
    // are aggregations (hollow diamond) rather than compositions.
    diagram.AddAggregation("Bank", "Teller").SetMultiplicity("1", "1..*");
    diagram.AddAggregation("Bank", "Customer").SetMultiplicity("1", "1..*");

    // A customer owns their accounts and loans outright: filled diamond.
    diagram.AddComposition("Customer", "Account").SetMultiplicity("1", "1..*");
    diagram.AddComposition("Customer", "Loan").SetMultiplicity("1", "0..*");

    diagram.AddAssociation("Customer", "Teller").SetMultiplicity("1..*", "1..*");
    diagram.AddAssociation("Teller", "Account").SetMultiplicity("1..*", "1..*");

    diagram.SetLayout(ClassLayoutKind::Layered, ClassLayoutDirection::TopToBottom);
    diagram.RunLayout();
}

// =============================================================================
// SCENARIO 2 - Interfaces and realization (reference image 3)
// =============================================================================

void BuildInterfaceDiagram(UltraCanvasClassDiagram& diagram) {
    diagram.Clear();
    diagram.SetTheme(ClassDiagramTheme::Professional);
    diagram.SetDetailLevel(ClassDiagramDetail::Full);
    diagram.SetTitle("Rendering Subsystem - Interfaces");

    diagram.AddInterface("IRenderable",
        {"+Render(ctx: RenderContext): void", "+GetBounds(): Rect"});
    diagram.AddInterface("ISerializable",
        {"+ToJson(): string", "+FromJson(text: string): bool"});
    diagram.AddInterface("IHitTestable",
        {"+Contains(point: Point): bool"});

    diagram.AddAbstractClass("Shape",
        {"#origin: Point", "#style: ShapeStyle"},
        {"+Render(ctx: RenderContext): void", "+ComputeArea(): double {abstract}",
         "+GetBounds(): Rect"});

    diagram.AddClass("Rectangle",
        {"-width: double", "-height: double"},
        {"+ComputeArea(): double", "+Contains(point: Point): bool"});
    diagram.AddClass("Ellipse",
        {"-radiusX: double", "-radiusY: double"},
        {"+ComputeArea(): double", "+Contains(point: Point): bool"});

    diagram.AddClass("ShapeFactory", {},
        {"+{static} Create(kind: ShapeKind): Shape"});

    diagram.AddEnumeration("ShapeKind", {"Rectangle", "Ellipse", "Polygon"});

    // Realization is dashed with a hollow triangle; generalization is solid.
    diagram.AddRealization("Shape", "IRenderable");
    diagram.AddRealization("Shape", "ISerializable");
    diagram.AddRealization("Rectangle", "IHitTestable");
    diagram.AddRealization("Ellipse", "IHitTestable");
    diagram.AddGeneralization("Rectangle", "Shape");
    diagram.AddGeneralization("Ellipse", "Shape");

    // Dependency is dashed with an open arrow, and carries a stereotype.
    diagram.AddDependency("ShapeFactory", "Shape", "create");
    diagram.AddDependency("ShapeFactory", "ShapeKind", "use");

    // Image 3 mixes full boxes with name-only badges.
    diagram.SetClassifierCollapsed("IHitTestable", true);

    diagram.SetLayout(ClassLayoutKind::Layered, ClassLayoutDirection::TopToBottom);
    diagram.RunLayout();
}

// =============================================================================
// SCENARIO 3 - «DataType» service metadata (reference image 5)
// =============================================================================

void BuildServiceMetadata(UltraCanvasClassDiagram& diagram) {
    diagram.Clear();
    diagram.SetTheme(ClassDiagramTheme::DataType);
    diagram.SetDetailLevel(ClassDiagramDetail::Full);
    diagram.SetTitle("Service Metadata");

    auto addDataType = [&](const std::string& name,
                           const std::vector<std::string>& attributes) {
        std::string id = diagram.AddClass(name, attributes, {});
        if (UMLClassifier* classifier = diagram.Model().GetClassifier(id)) {
            classifier->stereotypes.push_back("DataType");
        }
        return id;
    };

    addDataType("MetadataBase", {});
    addDataType("ExtendedMetadataBase", {});
    addDataType("ServiceMetadata", {
        "+onlineResource: OnlineResource",
        "+fees [0..1]: String",
        "+accessConstraints [0..*]: String"
    });
    addDataType("ContactInformation", {
        "+contactPerson [0..1]: String",
        "+contactOrganization [0..1]: String",
        "+contactElectronicMailAddress [0..1]: String"
    });
    addDataType("Person", {"+contactPerson: String", "+contactOrganization: String"});
    addDataType("Address", {
        "+addressType [0..1]: String",
        "+address [0..1]: String",
        "+city [0..1]: String",
        "+stateOrProvince [0..1]: String",
        "+postCode [0..1]: String",
        "+country [0..1]: String"
    });

    diagram.AddGeneralization("ExtendedMetadataBase", "MetadataBase");
    diagram.AddGeneralization("ServiceMetadata", "ExtendedMetadataBase");

    // One edge carrying an association name, both role names and both
    // multiplicities - the collision case the label placement has to survive.
    diagram.AddAssociation("ServiceMetadata", "ContactInformation")
           .SetName("contactInformation")
           .SetMultiplicity("1", "0..1")
           .SetRoles("+service", "+contactInformation");

    diagram.AddAssociation("ContactInformation", "Address")
           .SetName("contactAddress")
           .SetMultiplicity("0..1", "0..1")
           .SetRoles("", "+contactAddress");

    diagram.AddAssociation("ContactInformation", "Person")
           .SetMultiplicity("0..1", "1")
           .SetRoles("", "+contactPerson");

    diagram.SetLayout(ClassLayoutKind::Layered, ClassLayoutDirection::TopToBottom);
    diagram.RunLayout();
}

// =============================================================================
// SCENARIO 4 - Colour-coded domain model (reference image 4)
// =============================================================================

void BuildDomainModel(UltraCanvasClassDiagram& diagram) {
    diagram.Clear();
    diagram.SetTheme(ClassDiagramTheme::Colorful);
    diagram.SetDetailLevel(ClassDiagramDetail::Full);
    diagram.SetTitle("Order Management Domain Model");

    diagram.AddClass("Customer",
        {"+customerId: int", "+name: string", "+email: string", "+phone: string"},
        {"+PlaceOrder(): Order", "+CancelOrder(id: int): bool"});

    diagram.AddClass("Order",
        {"+orderId: int", "+placedAt: DateTime", "+status: OrderStatus",
         "+/total: decimal"},
        {"+AddLine(product: Product, quantity: int)", "+Submit(): bool"});

    diagram.AddClass("OrderLine",
        {"+quantity: int", "+unitPrice: decimal", "+/lineTotal: decimal"},
        {});

    diagram.AddClass("Product",
        {"+sku: string", "+name: string", "+weight: double", "+price: decimal"},
        {"+IsInStock(): bool"});

    diagram.AddClass("Warehouse",
        {"+code: string", "+location: string", "+capacity: int"},
        {"+Reserve(product: Product, quantity: int): bool"});

    diagram.AddClass("Shipment",
        {"+trackingNumber: string", "+shippedAt: DateTime", "+carrier: string"},
        {"+Track(): ShipmentStatus"});

    diagram.AddEnumeration("OrderStatus",
        {"Draft", "Submitted", "Paid", "Shipped", "Cancelled"});

    diagram.AddComposition("Order", "OrderLine")
           .SetMultiplicity("1", "1..*")
           .SetName("contains", UMLReadingDirection::SourceToTarget);
    diagram.AddDirectedAssociation("Customer", "Order")
           .SetMultiplicity("1", "0..*")
           .SetName("places", UMLReadingDirection::SourceToTarget);
    diagram.AddDirectedAssociation("OrderLine", "Product")
           .SetMultiplicity("0..*", "1")
           .SetRoles("", "+product");
    diagram.AddAggregation("Warehouse", "Product")
           .SetMultiplicity("1", "0..*")
           .SetName("stocks", UMLReadingDirection::SourceToTarget);
    diagram.AddDirectedAssociation("Order", "Shipment")
           .SetMultiplicity("1", "0..1")
           .SetName("fulfilled by", UMLReadingDirection::SourceToTarget);
    diagram.AddDirectedAssociation("Shipment", "Warehouse")
           .SetMultiplicity("0..*", "1")
           .SetRoles("", "+origin");

    diagram.ApplyPaletteByClassifier();
    diagram.SetLayout(ClassLayoutKind::Layered, ClassLayoutDirection::LeftToRight);
    diagram.RunLayout();
}

// =============================================================================
// SCENARIO 5 - Reverse engineered from C++ headers
// =============================================================================

// A representative slice of the framework, embedded so the scenario works no
// matter what the demo's working directory is. If the real headers happen to
// be reachable, they are used instead and this is the fallback.
const char* const kEmbeddedSource = R"CPP(
namespace UltraCanvas {

class IRenderContext {
public:
    virtual ~IRenderContext() = default;
    virtual void DrawLine(const Point2Dd& from, const Point2Dd& to) = 0;
    virtual void FillRectangle(const Rect2Dd& rect) = 0;
    virtual void DrawText(const std::string& text, const Point2Dd& pos) = 0;
};

class UltraCanvasUIElement {
public:
    virtual void Render(IRenderContext* ctx, const Rect2Df& dirtyRect);
    virtual bool OnEvent(const UCEvent& event);
    bool IsVisible() const;
protected:
    std::string identifier;
    Rect2Di finalBounds;
};

class UltraCanvasDiagramRouter {
public:
    static std::vector<Point2Dd> ComputeOrthogonalPath(const Point2Dd& start,
                                                       const Point2Dd& end,
                                                       DiagramCardinalSide sourceSide,
                                                       DiagramCardinalSide targetSide);
    static bool PathHasObstacles(const std::vector<Point2Dd>& path);
};

class UltraCanvasClassDiagram : public UltraCanvasUIElement {
public:
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;
    void RunLayout();
private:
    UltraCanvasUMLModel model;
    ClassDiagramStyle style;
    std::vector<BoxMetrics> boxes;
    UltraCanvasDiagramRouter* router;
};

class UltraCanvasFlowChart : public UltraCanvasUIElement {
public:
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    void AddNode(const std::string& id, const std::string& label);
private:
    FlowChartStyle style;
};

}
)CPP";

// Tries the real headers first; the demo may be launched from the build
// directory, the repository root, or an installed bundle.
//
// A curated handful of headers, not the whole tree: scanning every header under
// Plugins/Diagrams yields ~200 classifiers with almost no inheritance between
// them, which lays out as one enormous rank of unreadable boxes. Four related
// headers make a diagram a person can actually read, and they are the ones this
// feature is built from.
bool TryParseRealHeaders(UltraCanvasCppReverseEngineer& engineer,
                         UltraCanvasUMLModel& model,
                         std::string& outSource) {
    static const char* const roots[] = {
        "UltraCanvas/include/Plugins/Diagrams",
        "../UltraCanvas/include/Plugins/Diagrams",
        "../../UltraCanvas/include/Plugins/Diagrams",
        "../../../UltraCanvas/include/Plugins/Diagrams",
    };
    static const char* const headers[] = {
        "UltraCanvasUMLModel.h",
        "UltraCanvasClassLayout.h",
        "UltraCanvasDiagramRouting.h",
        "UltraCanvasCppReverseEngineer.h",
    };

    std::error_code ec;
    for (const char* root : roots) {
        if (!std::filesystem::is_directory(root, ec)) continue;

        std::vector<std::string> paths;
        for (const char* header : headers) {
            std::string path = std::string(root) + "/" + header;
            if (std::filesystem::is_regular_file(path, ec)) paths.push_back(path);
        }
        if (paths.empty()) continue;

        CppReverseEngineerResult result = engineer.ParseFiles(paths, model);
        if (result.classifiersAdded == 0) continue;

        outSource = std::string(root) + " (" + std::to_string(paths.size()) + " headers)";
        return true;
    }
    return false;
}

void BuildReverseEngineered(UltraCanvasClassDiagram& diagram,
                            const std::shared_ptr<UltraCanvasLabel>& status) {
    diagram.Clear();
    diagram.SetTheme(ClassDiagramTheme::Professional);
    diagram.SetDetailLevel(ClassDiagramDetail::Signatures);

    CppReverseEngineerOptions options;
    options.includePrivateMembers = false;
    options.includeConstructors = false;
    options.maxMembersPerCompartment = 5;
    options.excludeNamePrefixes = {"detail", "Impl"};

    UltraCanvasCppReverseEngineer engineer(options);
    UltraCanvasUMLModel model;

    std::string source;
    bool fromDisk = TryParseRealHeaders(engineer, model, source);
    if (!fromDisk) {
        engineer.ParseSource(kEmbeddedSource, model, "embedded sample");
        source = "embedded sample (headers not found on disk)";
    }
    size_t dropped = engineer.ResolveInferredRelationships(model);

    model.SetTitle("Reverse engineered from C++ headers");
    diagram.SetModel(model);
    diagram.ApplyPaletteByPackage();
    // Grid, not Layered: a reverse-engineered header set is mostly independent
    // types with very little inheritance between them, and a layered layout
    // puts everything with no parent into one enormous rank. Grid is the
    // deterministic fallback for exactly this shape of model.
    diagram.LayoutOptions().gridColumns = 5;
    diagram.SetLayout(ClassLayoutKind::Grid, ClassLayoutDirection::TopToBottom);
    diagram.RunLayout();

    if (status) {
        size_t errors = 0;
        for (const auto& diagnostic : diagram.Model().Validate()) {
            if (diagnostic.severity == UMLDiagnosticSeverity::Error) ++errors;
        }
        status->SetText("Source: " + source +
                        "  |  classifiers: " + std::to_string(diagram.Model().ClassifierCount()) +
                        "  |  relationships: " + std::to_string(diagram.Model().RelationshipCount()) +
                        "  |  edges dropped (external types): " + std::to_string(dropped) +
                        "  |  validation errors: " + std::to_string(errors));
    }
}

} // namespace

// =============================================================================
// PAGE
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateClassDiagramExamples() {
    auto container = std::make_shared<UltraCanvasContainer>("ClassDiagramPage", 0, 0, 980, 760);
    container->SetBackgroundColor(Color(245, 247, 250, 255));

    // ---- title -----------------------------------------------------------
    auto title = std::make_shared<UltraCanvasLabel>("cdTitle", 40, 16, 900, 32);
    title->SetText("UML Class Diagram");
    title->SetFont("Arial", 22.0f, FontWeight::Bold);
    title->SetTextColor(Color(30, 30, 30, 255));
    container->AddChild(title);

    auto description = std::make_shared<UltraCanvasLabel>("cdDesc", 40, 50, 900, 20);
    description->SetText("Compartment boxes sized from their members; seven UML relationship "
                         "kinds with correct line styles and end decorations.");
    description->SetFontSize(13);
    description->SetTextColor(Color(100, 100, 100, 255));
    container->AddChild(description);

    // ---- the diagram -----------------------------------------------------
    auto diagram = CreateClassDiagram("classDiagram", 40, 116, 900, 560);
    container->AddChild(diagram);

    // ---- status line -----------------------------------------------------
    auto status = std::make_shared<UltraCanvasLabel>("cdStatus", 40, 684, 900, 20);
    status->SetText("Click a class to select it; click a member row to report it. "
                    "Wheel zooms, drag pans, drag a box to move it, double-click collapses it.");
    status->SetFontSize(11);
    status->SetTextColor(Color(90, 95, 110, 255));
    container->AddChild(status);

    // Report what the user clicked, which is what makes per-row hit testing
    // visible in the demo.
    diagram->onMemberClick = [diagram, status](const std::string& classifierId,
                                               UMLMemberKind kind, size_t index) {
        const UMLClassifier* classifier = diagram->Model().GetClassifier(classifierId);
        if (!classifier || !status) return;
        const std::vector<UMLMember>& members = (kind == UMLMemberKind::Operation)
            ? classifier->operations : classifier->attributes;
        if (index >= members.size()) return;
        status->SetText("Member: " + classifier->name + "::" + members[index].name +
                        "   (" + (kind == UMLMemberKind::Operation ? "operation" : "attribute") + ")");
    };
    diagram->onClassClick = [diagram, status](const std::string& classifierId) {
        const UMLClassifier* classifier = diagram->Model().GetClassifier(classifierId);
        if (!classifier || !status) return;
        status->SetText(std::string("Class: ") + classifier->name + "   (" +
                        UMLClassifierKindName(classifier->kind) + ", " +
                        std::to_string(classifier->attributes.size()) + " attributes, " +
                        std::to_string(classifier->operations.size()) + " operations)");
    };
    diagram->onRelationshipClick = [diagram, status](const std::string& relationshipId) {
        const UMLRelationship* relationship = diagram->Model().GetRelationship(relationshipId);
        if (!relationship || !status) return;
        const UMLClassifier* source = diagram->Model().GetClassifier(relationship->source.classifierId);
        const UMLClassifier* target = diagram->Model().GetClassifier(relationship->target.classifierId);
        status->SetText(std::string("Relationship: ") + UMLRelationshipKindName(relationship->kind) +
                        "  " + (source ? source->name : "?") + " -> " + (target ? target->name : "?"));
    };

    // ---- scenario buttons ------------------------------------------------
    int buttonY = 78;
    int buttonX = 40;
    auto addScenarioButton = [&](const std::string& id, const std::string& text, int width,
                                 std::function<void()> action) {
        auto button = std::make_shared<UltraCanvasButton>(id, buttonX, buttonY, width, 30);
        button->SetText(text);
        button->SetOnClick(action);
        container->AddChild(button);
        buttonX += width + 8;
    };

    addScenarioButton("cdBank", "Banking System", 130, [diagram, status]() {
        BuildBankingSystem(*diagram);
        if (status) status->SetText("Banking System - generalization, aggregation, composition, "
                                    "per-end multiplicities (reference images 1 and 2).");
    });
    addScenarioButton("cdInterfaces", "Interfaces", 110, [diagram, status]() {
        BuildInterfaceDiagram(*diagram);
        if (status) status->SetText("Interfaces - «interface» stereotypes, dashed realization and "
                                    "dependency, a collapsed name-only badge (reference image 3).");
    });
    addScenarioButton("cdDataType", "Service Metadata", 145, [diagram, status]() {
        BuildServiceMetadata(*diagram);
        if (status) status->SetText("Service Metadata - «DataType» boxes, member-level multiplicity, "
                                    "association name plus roles plus multiplicities (reference image 5).");
    });
    addScenarioButton("cdDomain", "Domain Model", 125, [diagram, status]() {
        BuildDomainModel(*diagram);
        if (status) status->SetText("Domain Model - a colour per class, named associations, "
                                    "left-to-right layout (reference image 4).");
    });
    addScenarioButton("cdReverse", "Reverse Engineered", 160, [diagram, status]() {
        BuildReverseEngineered(*diagram, status);
    });

    // ---- view controls ---------------------------------------------------
    int controlY = 712;
    int controlX = 40;
    auto addControlButton = [&](const std::string& id, const std::string& text, int width,
                                std::function<void()> action) {
        auto button = std::make_shared<UltraCanvasButton>(id, controlX, controlY, width, 28);
        button->SetText(text);
        button->SetOnClick(action);
        container->AddChild(button);
        controlX += width + 6;
    };

    addControlButton("cdLayered", "Layered", 80, [diagram]() {
        diagram->SetLayout(ClassLayoutKind::Layered, ClassLayoutDirection::TopToBottom);
        diagram->RunLayout();
    });
    addControlButton("cdTree", "Tree", 70, [diagram]() {
        diagram->SetLayout(ClassLayoutKind::Tree, ClassLayoutDirection::TopToBottom);
        diagram->RunLayout();
    });
    addControlButton("cdLeftRight", "Left to Right", 110, [diagram]() {
        diagram->SetLayout(ClassLayoutKind::Layered, ClassLayoutDirection::LeftToRight);
        diagram->RunLayout();
    });
    addControlButton("cdGrid", "Grid", 70, [diagram]() {
        diagram->SetLayout(ClassLayoutKind::Grid, ClassLayoutDirection::TopToBottom);
        diagram->RunLayout();
    });
    addControlButton("cdDetail", "Detail: cycle", 110, [diagram, status]() {
        // Full -> Signatures -> NameOnly -> Full (proposal M11).
        ClassDiagramDetail next = ClassDiagramDetail::Full;
        switch (diagram->GetDetailLevel()) {
            case ClassDiagramDetail::Full:       next = ClassDiagramDetail::Signatures; break;
            case ClassDiagramDetail::Signatures: next = ClassDiagramDetail::NameOnly;   break;
            case ClassDiagramDetail::NameOnly:   next = ClassDiagramDetail::Full;       break;
        }
        diagram->SetDetailLevel(next);
        diagram->RunLayout();
        if (status) {
            status->SetText(std::string("Detail level: ") +
                            (next == ClassDiagramDetail::Full ? "Full"
                             : next == ClassDiagramDetail::Signatures ? "Signatures"
                             : "Name only"));
        }
    });
    addControlButton("cdFit", "Fit", 60, [diagram]() { diagram->FitView(); });
    addControlButton("cdReset", "Reset", 70, [diagram]() {
        diagram->ResetView();
        diagram->RunLayout();
    });

    auto gridToggle = std::make_shared<UltraCanvasCheckbox>("cdGridToggle", controlX + 8, controlY + 3, 110, 22);
    gridToggle->SetText("Show Grid");
    gridToggle->SetChecked(true);
    gridToggle->onStateChanged = [diagram](CheckedState, CheckedState newState) {
        diagram->SetGridVisible(newState == CheckedState::Checked, 20.0);
    };
    container->AddChild(gridToggle);

    // Open on the canonical example.
    BuildBankingSystem(*diagram);

    return container;
}

} // namespace UltraCanvas
