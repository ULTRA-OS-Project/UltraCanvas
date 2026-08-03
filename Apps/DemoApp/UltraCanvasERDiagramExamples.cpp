// Apps/DemoApp/UltraCanvasERDiagramExamples.cpp
// Examples for UltraCanvasERDiagram v1.0.0
// Last Modified: 2026-07-31
//
// One tab per reference diagram from the feature proposal
// (Docs/UltraCanvas/UltraCanvasERDiagramProposal.md §2), so every Phase 1
// capability is visible somewhere:
//
//   Internet Sales   - Chen bodies + crow's-foot line ends, legend, title,
//                      orthogonal connectors  (image 1)
//   Dense Schema     - attribute satellite haloes at scale, force-directed
//                      layout, controls overlay  (image 2)
//   Student / Course - symmetric layout, underlined key attributes,
//                      annotation callouts  (image 3)
//   Hospital         - pastel theme, spine layout, straight connectors
//                      (image 4)
//   Academic         - (min,max) ISO cardinality, ternary relationship,
//                      recursive self-relationship with role names  (image 5)
//   Crow's Foot      - physical schema: typed attribute rows, PK/FK markers
//
// The button bar cycles notation / theme / layout on whichever tab is active,
// which is also the quickest way to see that all three notations render the
// same underlying model.

#include "Plugins/Diagrams/UltraCanvasERDiagram.h"
#include "UltraCanvasDemo.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasTabbedContainer.h"
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// 1. INTERNET SALES MODEL  (reference image 1)
// =============================================================================
//
// The distinctive thing about this reference is that it is a *hybrid*: Chen
// rectangles/diamonds/ovals, but crow's-foot markers on the line ends, and a
// legend that documents both. SetLineEndStyle is deliberately independent of
// SetNotation so this combination is expressible.

static std::shared_ptr<UltraCanvasERDiagram> CreateInternetSalesDiagram(
        int x, int y, int w, int h) {
    auto er = CreateERDiagram("er_sales", x, y, w, h);

    er->SetNotation(ERNotation::Chen);
    er->SetLineEndStyle(ERLineEndStyle::CrowsFoot);       // Hybrid: image 1
    er->SetCardinalityLabelStyle(ERCardinalityLabels::NoLabels);
    er->SetTheme(ERDiagramTheme::Default);
    er->SetConnectorStyle(ERConnectorStyle::Orthogonal);
    er->SetTitle("Entity Relationship Diagram - Internet Sales Model");

    er->AddEntity("customer", "Customer", 470, 300);
    er->AddAttribute("customer", "Name", ERKeyRole::Primary);
    er->AddAttribute("customer", "E-mail");
    er->AddAttribute("customer", "Address", ERKeyRole::NoKey,
                     ERAttributeKind::Composite);

    er->AddEntity("item", "Item", 900, 300);
    er->AddAttribute("item", "Name", ERKeyRole::Primary);
    er->AddAttribute("item", "Price");

    er->AddEntity("order", "Order", 690, 620);
    er->AddAttribute("order", "Order Number", ERKeyRole::Primary);

    er->AddEntity("shipping", "Shipping", 130, 300);
    er->AddEntity("company", "Company", 900, 60);
    er->AddAttribute("company", "Name");
    er->AddEntity("creditcard", "Credit Card", 130, 470);
    er->AddEntity("ecommerce", "E-Commerce", 130, 640);
    er->AddEntity("cart", "Shopping Cart", 940, 620);

    er->AddRelationship("places", "Orders", "customer", "order",
                        ERCardinality::ExactlyOne, ERCardinality::ZeroOrMany);
    er->AddRelationship("ships", "Ships Item", "shipping", "customer",
                        ERCardinality::ExactlyOne, ERCardinality::ZeroOrMany);
    er->AddRelationship("contains", "Contains", "order", "item",
                        ERCardinality::ExactlyOne, ERCardinality::OneOrMany);
    er->AddRelationship("produces", "Produces", "company", "item",
                        ERCardinality::ExactlyOne, ERCardinality::ZeroOrMany);
    er->AddRelationship("forwards", "Forwards Order", "shipping", "creditcard",
                        ERCardinality::ExactlyOne, ERCardinality::ZeroOrOne);
    er->AddRelationship("verifies", "Verifies", "creditcard", "ecommerce",
                        ERCardinality::ExactlyOne, ERCardinality::ExactlyOne);
    er->AddRelationship("processes", "Processes", "ecommerce", "order",
                        ERCardinality::ExactlyOne, ERCardinality::ZeroOrMany);
    er->AddRelationship("carries", "Carries", "cart", "item",
                        ERCardinality::ZeroOrOne, ERCardinality::ZeroOrMany);

    // The legend is generated from the active notation, so it can never drift
    // out of sync with what is drawn.
    er->SetLegendVisible(true, ERPanelPosition::TopLeft);

    er->AutoSizeAll();
    er->SetLayout(ERDiagramLayout::AttributeSatellite);
    er->RunLayout();
    return er;
}

// =============================================================================
// 2. DENSE SCHEMA  (reference image 2)
// =============================================================================
//
// The stress test: eight entities, six to nine attributes each. Hand-placing
// ~50 ovals is not viable, so this tab exists to exercise
// ApplyAttributeSatelliteLayout, which fans each entity's ovals onto the arc
// facing away from that entity's relationships.

static std::shared_ptr<UltraCanvasERDiagram> CreateDenseSchemaDiagram(
        int x, int y, int w, int h) {
    auto er = CreateERDiagram("er_dense", x, y, w, h);

    er->SetNotation(ERNotation::Chen);
    er->SetTheme(ERDiagramTheme::Colorful);
    er->SetCardinalityLabelStyle(ERCardinalityLabels::Letters);
    er->SetConnectorStyle(ERConnectorStyle::Orthogonal);
    er->SetTitle("University Schema", "Force-directed layout with attribute haloes");

    struct EntitySpec {
        const char* id;
        const char* name;
        std::vector<const char*> attributes;
    };
    const std::vector<EntitySpec> specs = {
        { "student",   "Student",    { "StudentID", "First Name", "Last Name", "DOB",
                                       "Email", "Phone", "Address" } },
        { "course",    "Course",     { "CourseID", "Title", "Credits", "Level",
                                       "Syllabus", "Capacity" } },
        { "professor", "Professor",  { "StaffID", "Name", "Rank", "Office",
                                       "Email", "Salary" } },
        { "dept",      "Department", { "DeptID", "Name", "Building", "Budget",
                                       "Phone" } },
        { "section",   "Section",    { "SectionNo", "Semester", "Year", "Room",
                                       "Schedule" } },
        { "enrolment", "Enrolment",  { "EnrolID", "Date", "Grade", "Status" } },
        { "textbook",  "Textbook",   { "ISBN", "Title", "Publisher", "Edition",
                                       "Price" } },
        { "library",   "Library",    { "LibID", "Name", "Location", "Hours" } }
    };

    for (const auto& spec : specs) {
        er->AddEntity(spec.id, spec.name);
        bool first = true;
        for (const auto* attributeName : spec.attributes) {
            er->AddAttribute(spec.id, attributeName,
                             first ? ERKeyRole::Primary : ERKeyRole::NoKey);
            first = false;
        }
    }

    er->AddRelationship("belongs", "Belongs To", "course", "dept",
                        ERCardinality::OneOrMany, ERCardinality::ExactlyOne);
    er->AddRelationship("works", "Works For", "professor", "dept",
                        ERCardinality::OneOrMany, ERCardinality::ExactlyOne);
    er->AddRelationship("teaches", "Teaches", "professor", "section",
                        ERCardinality::ExactlyOne, ERCardinality::ZeroOrMany);
    er->AddRelationship("offers", "Offers", "course", "section",
                        ERCardinality::ExactlyOne, ERCardinality::OneOrMany);
    er->AddRelationship("enrols", "Enrols", "student", "enrolment",
                        ERCardinality::ExactlyOne, ERCardinality::ZeroOrMany);
    er->AddRelationship("forsection", "For Section", "enrolment", "section",
                        ERCardinality::OneOrMany, ERCardinality::ExactlyOne);
    er->AddRelationship("requires", "Requires", "course", "textbook",
                        ERCardinality::ZeroOrMany, ERCardinality::ZeroOrMany);
    er->AddRelationship("stocks", "Stocks", "library", "textbook",
                        ERCardinality::ExactlyOne, ERCardinality::ZeroOrMany);

    er->SetControlsVisible(true);
    er->SetHighlightRelatedOnHover(true);   // Hover an entity to isolate it

    er->AutoSizeAll();
    er->SetLayout(ERDiagramLayout::ForceDirected);
    er->RunLayout();
    return er;
}

// =============================================================================
// 3. STUDENT / COURSE  (reference image 3)
// =============================================================================
//
// The textbook diagram: two entities, one central diamond, and annotation
// callouts naming the parts. The prime attributes (SSN, CID) carry the solid
// underline that means "primary key" in Chen notation.

static std::shared_ptr<UltraCanvasERDiagram> CreateStudentCourseDiagram(
        int x, int y, int w, int h) {
    auto er = CreateERDiagram("er_student", x, y, w, h);

    er->SetNotation(ERNotation::Chen);
    er->SetTheme(ERDiagramTheme::Professional);
    er->SetCardinalityLabelStyle(ERCardinalityLabels::Letters);
    er->SetConnectorStyle(ERConnectorStyle::Straight);
    er->SetTitle("Entity Relationship Diagram (ERD)");

    er->AddEntity("student", "Student");
    er->AddAttribute("student", "SSN", ERKeyRole::Primary);   // Underlined
    er->AddAttribute("student", "Name");
    er->AddAttribute("student", "Email");

    er->AddEntity("course", "Course");
    er->AddAttribute("course", "CID", ERKeyRole::Primary);    // Underlined
    er->AddAttribute("course", "Title");
    er->AddAttribute("course", "Duration");

    er->AddRelationship("joins", "Joins", "student", "course",
                        ERCardinality::OneOrMany, ERCardinality::OneOrMany);

    // Callouts anchored to model elements, with leader lines.
    er->AddAnnotation("a1", "Student Entity", "student", ERAnnotationSide::Bottom);
    er->AddAnnotation("a2", "Course Entity", "course", ERAnnotationSide::Bottom);
    er->AddAnnotation("a3", "Relationship", "joins", ERAnnotationSide::Bottom);
    er->AddAnnotation("a4", "Prime Attribute", "student.SSN", ERAnnotationSide::Left);
    er->AddAnnotation("a5", "Prime Attribute", "course.CID", ERAnnotationSide::Right);
    er->AddAnnotation("a6", "Attribute", "student.Name", ERAnnotationSide::Top);

    er->AutoSizeAll();
    er->SetLayout(ERDiagramLayout::Symmetric);
    er->RunLayout();
    return er;
}

// =============================================================================
// 4. HOSPITAL  (reference image 4)
// =============================================================================
//
// Flat pastel theme, straight connectors, and entity-diamond-entity chains laid
// on a horizontal spine.

static std::shared_ptr<UltraCanvasERDiagram> CreateHospitalDiagram(
        int x, int y, int w, int h) {
    auto er = CreateERDiagram("er_hospital", x, y, w, h);

    er->SetNotation(ERNotation::Chen);
    er->SetTheme(ERDiagramTheme::Pastel);
    er->SetCardinalityLabelStyle(ERCardinalityLabels::Letters);
    er->SetConnectorStyle(ERConnectorStyle::Straight);
    er->SetTitle("ER diagram of Hospital");

    er->AddEntity("test", "Test");
    er->AddAttribute("test", "Test_ID", ERKeyRole::Primary);
    er->AddAttribute("test", "Test_Name");
    er->AddAttribute("test", "Test_Date");

    er->AddEntity("patient", "Patient");
    er->AddAttribute("patient", "Patient_ID", ERKeyRole::Primary);
    er->AddAttribute("patient", "Name");
    er->AddAttribute("patient", "Date_Admitted");
    er->AddAttribute("patient", "Date_Checked_Out");
    er->AddAttribute("patient", "Age", ERKeyRole::NoKey, ERAttributeKind::Derived);

    er->AddEntity("doctor", "Doctors");
    er->AddAttribute("doctor", "Doctor_ID", ERKeyRole::Primary);
    er->AddAttribute("doctor", "Name");
    er->AddAttribute("doctor", "Specialisation");
    er->AddAttribute("doctor", "Phone", ERKeyRole::NoKey, ERAttributeKind::MultiValued);

    er->AddRelationship("testfor", "Test_for", "test", "patient",
                        ERCardinality::ZeroOrMany, ERCardinality::ExactlyOne);
    er->AddRelationship("attended", "Attended_by", "patient", "doctor",
                        ERCardinality::OneOrMany, ERCardinality::ExactlyOne);
    er->AddRelationship("drpatient", "Dr_patient", "doctor", "patient",
                        ERCardinality::ExactlyOne, ERCardinality::ZeroOrMany);

    er->AutoSizeAll();
    er->SetLayout(ERDiagramLayout::Spine);
    er->RunLayout();
    return er;
}

// =============================================================================
// 5. ACADEMIC MODEL  (reference image 5)
// =============================================================================
//
// The formally strictest reference: (min,max) ISO cardinality on every leg, a
// ternary relationship (one diamond, three legs), and a recursive relationship
// whose two ends are told apart by role names.

static std::shared_ptr<UltraCanvasERDiagram> CreateAcademicDiagram(
        int x, int y, int w, int h) {
    auto er = CreateERDiagram("er_academic", x, y, w, h);

    er->SetNotation(ERNotation::ChenMinMax);
    er->SetCardinalityLabelStyle(ERCardinalityLabels::MinMax);
    er->SetTheme(ERDiagramTheme::Professional);
    er->SetConnectorStyle(ERConnectorStyle::Orthogonal);
    er->SetTitle("Academic Model", "(min,max) cardinality, ternary and recursive relationships");

    er->AddEntity("school", "School", 120, 90);
    er->AddAttribute("school", "SchoolID", ERKeyRole::Primary);
    er->AddEntity("professor", "Professor", 120, 330);
    er->AddAttribute("professor", "StaffNo", ERKeyRole::Primary);
    er->AddAttribute("professor", "Name");
    er->AddEntity("subject", "Subject", 560, 90);
    er->AddAttribute("subject", "Code", ERKeyRole::Primary);
    er->AddEntity("course", "Course", 560, 330);
    er->AddAttribute("course", "CourseNo", ERKeyRole::Primary);
    er->AddEntity("student", "Student", 980, 330);
    er->AddAttribute("student", "StudentNo", ERKeyRole::Primary);
    er->AddAttribute("student", "Name");
    er->AddEntity("evaluation", "Evaluation", 980, 90);
    er->AddAttribute("evaluation", "EvalID", ERKeyRole::Primary);
    er->AddAttribute("evaluation", "Mark");

    // Binary relationships with explicit (min,max) pairs.
    ERDiagramRelationship belongs("belongs", "Belongs");
    belongs.legs = {
        ERDiagramLeg("professor", "", 1, 1, ERParticipation::Total),
        ERDiagramLeg("school", "", 1, ERCardinalityN, ERParticipation::Total)
    };
    er->AddRelationship(belongs);

    // Ternary: one diamond, three legs (Professor / Subject / Course).
    er->AddNaryRelationship("assigns", "Assigns", {
        ERDiagramLeg("professor", "teacher", 1, ERCardinalityN, ERParticipation::Partial),
        ERDiagramLeg("subject",   "topic",   1, ERCardinalityN, ERParticipation::Partial),
        ERDiagramLeg("course",    "in",      0, ERCardinalityN, ERParticipation::Partial)
    });

    ERDiagramRelationship attends("attends", "Attends");
    attends.legs = {
        ERDiagramLeg("student", "", 1, ERCardinalityN, ERParticipation::Total),
        ERDiagramLeg("course",  "", 1, ERCardinalityN, ERParticipation::Partial)
    };
    er->AddRelationship(attends);

    ERDiagramRelationship graded("graded", "Graded");
    graded.legs = {
        ERDiagramLeg("student",    "", 1, ERCardinalityN, ERParticipation::Partial),
        ERDiagramLeg("evaluation", "", 1, 1, ERParticipation::Total)
    };
    er->AddRelationship(graded);

    // Recursive: both legs on Student, told apart by their role names.
    er->AddRecursiveRelationship("tutoring", "Tutor_Tutored", "student",
                                 "tutor", "tutored",
                                 ERCardinality::ZeroOrOne, ERCardinality::ZeroOrMany);

    er->SetShowRoleNames(true);
    er->AutoSizeAll();
    er->SetLayout(ERDiagramLayout::AttributeSatellite);
    er->RunLayout();
    return er;
}

// =============================================================================
// 6. CROW'S FOOT (PHYSICAL SCHEMA)
// =============================================================================
//
// The same kind of model rendered the other way: attributes as typed rows
// inside the entity box, relationships as the edges themselves, cardinality as
// line-end glyphs. Identifying relationships are solid, non-identifying dashed.

static std::shared_ptr<UltraCanvasERDiagram> CreateCrowsFootDiagram(
        int x, int y, int w, int h) {
    auto er = CreateERDiagram("er_crowsfoot", x, y, w, h);

    er->SetNotation(ERNotation::CrowsFoot);
    er->SetLineEndStyle(ERLineEndStyle::CrowsFoot);
    er->SetTheme(ERDiagramTheme::Professional);
    er->SetConnectorStyle(ERConnectorStyle::Orthogonal);
    er->SetShowDataTypes(true);
    er->SetTitle("Order Schema", "Crow's foot / physical level");

    er->AddEntity("customer", "CUSTOMER", 90, 120);
    er->AddTypedAttribute("customer", "customer_id", "INTEGER", ERKeyRole::Primary, false);
    er->AddTypedAttribute("customer", "name", "VARCHAR(80)", ERKeyRole::NoKey, false);
    er->AddTypedAttribute("customer", "email", "VARCHAR(120)", ERKeyRole::Unique, false);
    er->AddTypedAttribute("customer", "created_at", "TIMESTAMP");

    er->AddEntity("order", "ORDER", 480, 120);
    er->AddTypedAttribute("order", "order_no", "INTEGER", ERKeyRole::Primary, false);
    er->AddTypedAttribute("order", "customer_id", "INTEGER", ERKeyRole::Foreign, false);
    er->AddTypedAttribute("order", "placed_on", "DATE", ERKeyRole::NoKey, false);
    er->AddTypedAttribute("order", "total", "DECIMAL(10,2)");

    er->AddEntity("line", "ORDER_LINE", 480, 400, ERDiagramEntityKind::Weak);
    er->AddTypedAttribute("line", "order_no", "INTEGER", ERKeyRole::Primary, false);
    er->AddTypedAttribute("line", "line_no", "INTEGER", ERKeyRole::Partial, false);
    er->AddTypedAttribute("line", "item_id", "INTEGER", ERKeyRole::Foreign, false);
    er->AddTypedAttribute("line", "quantity", "INTEGER", ERKeyRole::NoKey, false);

    er->AddEntity("item", "ITEM", 900, 400);
    er->AddTypedAttribute("item", "item_id", "INTEGER", ERKeyRole::Primary, false);
    er->AddTypedAttribute("item", "sku", "VARCHAR(32)", ERKeyRole::Unique, false);
    er->AddTypedAttribute("item", "price", "DECIMAL(10,2)", ERKeyRole::NoKey, false);

    er->AddRelationship("places", "places", "customer", "order",
                        ERCardinality::ExactlyOne, ERCardinality::ZeroOrMany);
    // Identifying: ORDER_LINE has no identity without its ORDER.
    er->AddIdentifyingRelationship("contains", "contains", "order", "line");
    er->AddRelationship("refers", "refers to", "item", "line",
                        ERCardinality::ExactlyOne, ERCardinality::ZeroOrMany);

    er->AutoSizeAll();
    er->SetLayout(ERDiagramLayout::Manual);
    er->RunLayout();
    return er;
}

// =============================================================================
// 7. RELATIONAL / MERISE  (reference image A)
// =============================================================================
//
// A table notation carrying (min,max) labels and plain arrowheads - no crow's
// feet anywhere. This is the combination that proves SetLineEndStyle and
// SetCardinalityLabelStyle really are independent of SetNotation: the boxes are
// the crow's-foot projection, but nothing about the line ends is.

static std::shared_ptr<UltraCanvasERDiagram> CreateRelationalDiagram(
        int x, int y, int w, int h) {
    auto er = CreateERDiagram("er_relational", x, y, w, h);

    er->SetNotation(ERNotation::CrowsFoot);
    er->SetLineEndStyle(ERLineEndStyle::Arrow);                  // Not crow's feet
    er->SetCardinalityLabelStyle(ERCardinalityLabels::MinMax);   // (1,n) / (0,1)
    er->SetTheme(ERDiagramTheme::Colorful);
    er->SetConnectorStyle(ERConnectorStyle::Orthogonal);
    er->SetShowDataTypes(false);                                 // Column names only
    er->SetTitle("Forum Schema", "Relational tables with (min,max) cardinality");

    // Badge column on the right, striped rows - the other house style.
    er->SetRowMarkerSide(ERRowMarkerSide::Right);
    er->SetStripeRows(true);

    er->AddEntity("groups", "groups", 40, 300);
    er->AddTypedAttribute("groups", "id_group", "", ERKeyRole::Primary, false);
    er->AddAttribute("groups", "title");

    er->AddEntity("users", "users", 330, 280);
    er->AddTypedAttribute("users", "id_user", "", ERKeyRole::Primary, false);
    er->AddTypedAttribute("users", "id_group", "", ERKeyRole::Foreign, false);
    er->AddAttribute("users", "first_name");
    er->AddAttribute("users", "fam_name");
    er->AddAttribute("users", "email");
    er->AddAttribute("users", "pass");
    er->AddAttribute("users", "about");

    er->AddEntity("topics", "topics", 170, 60);
    er->AddTypedAttribute("topics", "id_topic", "", ERKeyRole::Primary, false);
    er->AddTypedAttribute("topics", "id_user", "", ERKeyRole::Foreign, false);
    er->AddAttribute("topics", "date_modified");
    er->AddAttribute("topics", "title");

    er->AddEntity("posts", "posts", 540, 20);
    er->AddTypedAttribute("posts", "id_post", "", ERKeyRole::Primary, false);
    er->AddTypedAttribute("posts", "id_topic", "", ERKeyRole::Foreign, false);
    er->AddTypedAttribute("posts", "id_user", "", ERKeyRole::Foreign, false);
    er->AddAttribute("posts", "date_created");
    er->AddAttribute("posts", "body");

    er->AddEntity("pages", "pages", 800, 150);
    er->AddTypedAttribute("pages", "id_page", "", ERKeyRole::Primary, false);
    er->AddTypedAttribute("pages", "id_user", "", ERKeyRole::Foreign, false);
    er->AddAttribute("pages", "date_modified");
    er->AddAttribute("pages", "title");
    er->AddAttribute("pages", "body");
    er->AddAttribute("pages", "is_home");

    er->AddEntity("files", "files", 800, 420);
    er->AddTypedAttribute("files", "id_file", "", ERKeyRole::Primary, false);
    er->AddTypedAttribute("files", "id_user", "", ERKeyRole::Foreign, false);
    er->AddAttribute("files", "date_modified");
    er->AddAttribute("files", "title");
    er->AddAttribute("files", "filename");

    er->AddEntity("schedule", "schedule", 170, 620);
    er->AddTypedAttribute("schedule", "id_sch", "", ERKeyRole::Primary, false);
    er->AddAttribute("schedule", "time_start");
    er->AddAttribute("schedule", "time_end");
    er->AddAttribute("schedule", "weekday");
    er->AddAttribute("schedule", "title");

    er->AddEntity("calendar", "calendar", 540, 620);
    er->AddTypedAttribute("calendar", "id_cal", "", ERKeyRole::Primary, false);
    er->AddAttribute("calendar", "date_start");
    er->AddAttribute("calendar", "date_end");
    er->AddAttribute("calendar", "title");

    // Highlighted columns - the reference tints the ones worth the eye.
    er->SetAttributeHighlighted("users", "id_group", true);
    er->SetAttributeHighlighted("topics", "id_user", true);
    er->SetAttributeHighlighted("posts", "id_topic", true);
    er->SetAttributeHighlighted("pages", "id_user", true);
    er->SetAttributeHighlighted("files", "id_user", true);

    er->AddRelationship("user_group", "", "users", "groups",
                        ERCardinality::ExactlyOne, ERCardinality::ZeroOrMany);
    er->AddRelationship("topic_user", "", "topics", "users",
                        ERCardinality::ZeroOrOne, ERCardinality::ZeroOrMany);
    er->AddRelationship("post_topic", "", "posts", "topics",
                        ERCardinality::ExactlyOne, ERCardinality::OneOrMany);
    er->AddRelationship("post_user", "", "posts", "users",
                        ERCardinality::ExactlyOne, ERCardinality::ZeroOrMany);
    er->AddRelationship("page_user", "", "pages", "users",
                        ERCardinality::ZeroOrOne, ERCardinality::ZeroOrMany);
    er->AddRelationship("file_user", "", "files", "users",
                        ERCardinality::ZeroOrOne, ERCardinality::ZeroOrMany);
    er->AddRelationship("sch_user", "", "schedule", "users",
                        ERCardinality::ExactlyOne, ERCardinality::ZeroOrMany);
    er->AddRelationship("cal_user", "", "calendar", "users",
                        ERCardinality::ExactlyOne, ERCardinality::ZeroOrMany);

    er->SetShowRelationshipNames(false);   // The reference labels edges by cardinality only
    er->AutoSizeAll();
    er->SetLayout(ERDiagramLayout::Manual);
    er->RunLayout();
    return er;
}

// =============================================================================
// 8. IDEF1X / DATABASE MODEL  (reference image B)
// =============================================================================
//
// The convention used by IDEF1X and the Access/Visio database-model diagrams:
// key columns hoisted into their own compartment above a rule and underlined,
// foreign keys numbered so a reader can tell which constraint a column serves.

static std::shared_ptr<UltraCanvasERDiagram> CreateIdef1xDiagram(
        int x, int y, int w, int h) {
    auto er = CreateERDiagram("er_idef1x", x, y, w, h);

    er->SetNotation(ERNotation::CrowsFoot);
    er->SetLineEndStyle(ERLineEndStyle::CrowsFoot);
    er->SetCardinalityLabelStyle(ERCardinalityLabels::NoLabels);
    er->SetTheme(ERDiagramTheme::Minimal);
    er->SetConnectorStyle(ERConnectorStyle::Orthogonal);
    er->SetShowDataTypes(false);
    er->SetTitle("Equipment Hire", "IDEF1X - key compartment, numbered foreign keys");

    er->SetKeyCompartment(true);
    er->SetUnderlineKeyRows(true);
    er->SetNumberForeignKeys(true);

    er->AddEntity("categories", "tblCATEGORIES", 40, 60);
    er->AddTypedAttribute("categories", "category_id", "", ERKeyRole::Primary, false);
    er->AddAttribute("categories", "category_name");
    er->AddAttribute("categories", "category_description");
    er->AddAttribute("categories", "category_type");

    er->AddEntity("equipment", "tblEQUIPMENT", 40, 400);
    er->AddTypedAttribute("equipment", "equipment_id", "", ERKeyRole::Primary, false);
    er->AddAttribute("equipment", "equipment_name");
    er->AddAttribute("equipment", "equipment_description");
    er->AddTypedAttribute("equipment", "status_id", "", ERKeyRole::Foreign, false);

    er->AddEntity("equipcat", "tblEQUIP_CAT", 40, 230);
    er->AddTypedAttribute("equipcat", "equip_cat_id", "", ERKeyRole::Primary, false);
    er->AddTypedAttribute("equipcat", "category_id", "", ERKeyRole::Foreign, false);
    er->AddTypedAttribute("equipcat", "equipment_id", "", ERKeyRole::Foreign, false);

    er->AddEntity("status", "tblSTATUS", 40, 600);
    er->AddTypedAttribute("status", "status_id", "", ERKeyRole::Primary, false);
    er->AddAttribute("status", "status_name");
    er->AddAttribute("status", "status_notes");

    er->AddEntity("projects", "tblPROJECTS", 700, 60);
    er->AddTypedAttribute("projects", "project_id", "", ERKeyRole::Primary, false);
    er->AddAttribute("projects", "project_name");
    er->AddAttribute("projects", "project_start_date");
    er->AddAttribute("projects", "project_end_date");
    er->AddTypedAttribute("projects", "contact_id", "", ERKeyRole::Foreign, false);

    er->AddEntity("projequip", "tblPROJECT_EQUIPMENT", 700, 340);
    er->AddTypedAttribute("projequip", "project_equip_id", "", ERKeyRole::Primary, false);
    er->AddTypedAttribute("projequip", "project_id", "", ERKeyRole::Foreign, false);
    er->AddTypedAttribute("projequip", "equipment_id", "", ERKeyRole::Foreign, false);
    er->AddAttribute("projequip", "proj_equip_booking_rate");

    er->AddEntity("hirerates", "tblHIRE_RATES", 400, 600);
    er->AddTypedAttribute("hirerates", "hire_rate_id", "", ERKeyRole::Primary, false);
    er->AddAttribute("hirerates", "hire_rate_rate");
    er->AddAttribute("hirerates", "hire_rate_period");

    er->AddEntity("equiprates", "tblEQUIP_HIRE_RATES", 380, 420);
    er->AddTypedAttribute("equiprates", "equip_hire_rate_id", "", ERKeyRole::Primary, false);
    er->AddTypedAttribute("equiprates", "equipment_id", "", ERKeyRole::Foreign, false);
    er->AddTypedAttribute("equiprates", "hire_rate_id", "", ERKeyRole::Foreign, false);

    er->AddRelationship("cat_equipcat", "", "categories", "equipcat",
                        ERCardinality::ExactlyOne, ERCardinality::ZeroOrMany);
    er->AddRelationship("equip_equipcat", "", "equipment", "equipcat",
                        ERCardinality::ExactlyOne, ERCardinality::ZeroOrMany);
    er->AddRelationship("status_equip", "", "status", "equipment",
                        ERCardinality::ExactlyOne, ERCardinality::ZeroOrMany);
    er->AddRelationship("equip_rates", "", "equipment", "equiprates",
                        ERCardinality::ExactlyOne, ERCardinality::ZeroOrMany);
    er->AddRelationship("rates_equiprates", "", "hirerates", "equiprates",
                        ERCardinality::ExactlyOne, ERCardinality::ZeroOrMany);
    er->AddRelationship("proj_projequip", "", "projects", "projequip",
                        ERCardinality::ExactlyOne, ERCardinality::ZeroOrMany);
    er->AddRelationship("equip_projequip", "", "equipment", "projequip",
                        ERCardinality::ExactlyOne, ERCardinality::ZeroOrMany);

    er->SetShowRelationshipNames(false);
    er->AutoSizeAll();
    er->SetLayout(ERDiagramLayout::Manual);
    er->RunLayout();
    return er;
}

// =============================================================================
// SCENE
// =============================================================================

std::shared_ptr<UltraCanvasUIElement>
UltraCanvasDemoApplication::CreateERDiagramExamples() {
    const int CONTAINER_W = 960;
    const int CONTAINER_H = 720;
    const int PAD = 20;
    const int TITLE_H = 32;
    const int SUBTITLE_H = 44;
    const int TABS_H = 560;   // Diagrams need canvas height; the panel gets it
    const int BTNBAR_H = 36;
    const int STATUS_H = 26;

    auto root = std::make_shared<UltraCanvasContainer>(
            "erDemoRoot", 0, 0, CONTAINER_W, CONTAINER_H);
    root->SetBackgroundColor(Color(248, 248, 250, 255));

    int yCursor = PAD;

    auto titleLabel = std::make_shared<UltraCanvasLabel>(
            "erTitle", PAD, yCursor, CONTAINER_W - 2 * PAD, TITLE_H);
    titleLabel->SetText("ER Diagram");
    titleLabel->SetFontSize(20);
    titleLabel->SetFontWeight(FontWeight::Bold);
    titleLabel->SetTextColor(Color(50, 60, 130, 255));
    root->AddChild(titleLabel);
    yCursor += TITLE_H + 4;

    auto subtitleLabel = std::make_shared<UltraCanvasLabel>(
            "erSubtitle", PAD, yCursor, CONTAINER_W - 2 * PAD, SUBTITLE_H);
    subtitleLabel->SetText(
            "Entity-relationship modelling in Chen, Chen (min,max) and Crow's Foot "
            "notation over one shared model. Use the buttons to switch notation, "
            "theme and layout on the active tab - wheel zooms, drag pans.");
    subtitleLabel->SetFontSize(11);
    subtitleLabel->SetTextColor(Color(110, 110, 120, 255));
    subtitleLabel->SetWrap(TextWrap::WrapWord);
    root->AddChild(subtitleLabel);
    yCursor += SUBTITLE_H + 8;

    auto tabs = std::make_shared<UltraCanvasTabbedContainer>(
            "erTabs", PAD, yCursor, CONTAINER_W - 2 * PAD, TABS_H);
    tabs->SetTabStyle(TabStyle::Rounded);

    const int INNER_W = CONTAINER_W - 2 * PAD - 8;
    const int INNER_H = TABS_H - 40;

    int statusY = yCursor + TABS_H + 8 + BTNBAR_H + 4;
    auto statusLabel = std::make_shared<UltraCanvasLabel>(
            "erStatus", PAD, statusY, CONTAINER_W - 2 * PAD, STATUS_H);
    statusLabel->SetText("Internet Sales - Chen shapes with crow's-foot line ends "
                         "and an auto-generated legend.");
    statusLabel->SetFontSize(10);
    statusLabel->SetTextColor(Color(90, 90, 100, 255));

    struct TabSpec {
        const char* title;
        std::shared_ptr<UltraCanvasERDiagram> diagram;
        const char* status;
    };

    std::vector<TabSpec> specs = {
        { "Internet Sales", CreateInternetSalesDiagram(4, 4, INNER_W, INNER_H),
          "Internet Sales - Chen shapes with crow's-foot line ends and an "
          "auto-generated legend." },
        { "Dense Schema", CreateDenseSchemaDiagram(4, 4, INNER_W, INNER_H),
          "Dense Schema - 8 entities, ~45 attribute ovals placed by the "
          "attribute-satellite layout. Hover an entity to isolate it." },
        { "Student / Course", CreateStudentCourseDiagram(4, 4, INNER_W, INNER_H),
          "Student / Course - symmetric layout, underlined prime attributes, "
          "annotation callouts with leader lines." },
        { "Hospital", CreateHospitalDiagram(4, 4, INNER_W, INNER_H),
          "Hospital - pastel theme, spine layout. Age is a derived attribute "
          "(dashed oval); Phone is multivalued (double oval)." },
        { "Academic", CreateAcademicDiagram(4, 4, INNER_W, INNER_H),
          "Academic - (min,max) ISO cardinality, a ternary relationship and a "
          "recursive Tutor_Tutored with role names." },
        { "Crow's Foot", CreateCrowsFootDiagram(4, 4, INNER_W, INNER_H),
          "Crow's Foot - typed attribute rows with PK/FK/UK markers. "
          "Identifying relationships are solid, non-identifying dashed." },
        { "Relational", CreateRelationalDiagram(4, 4, INNER_W, INNER_H),
          "Relational - table boxes with (min,max) labels and plain arrowheads, "
          "badge column on the right, striped rows." },
        { "IDEF1X", CreateIdef1xDiagram(4, 4, INNER_W, INNER_H),
          "IDEF1X - key columns in their own compartment above the rule and "
          "underlined; foreign keys numbered FK1/FK2." }
    };

    std::vector<std::shared_ptr<UltraCanvasERDiagram>> diagrams;
    for (const auto& spec : specs) {
        auto tab = std::make_shared<UltraCanvasContainer>(
                std::string("erTab_") + spec.title, 0, 0, INNER_W, INNER_H);
        tab->SetBackgroundColor(Color(255, 255, 255, 255));
        tab->AddChild(spec.diagram);
        tabs->AddTab(spec.title, tab);
        diagrams.push_back(spec.diagram);
    }
    tabs->SetActiveTab(0);
    root->AddChild(tabs);
    yCursor += TABS_H + 8;

    // Active-diagram tracker so the button bar acts on whatever tab is shown.
    auto activeDiagram =
            std::make_shared<std::shared_ptr<UltraCanvasERDiagram>>(diagrams.front());

    std::vector<std::string> statusTexts;
    for (const auto& spec : specs) statusTexts.emplace_back(spec.status);

    tabs->onTabChange = [activeDiagram, diagrams, statusTexts, statusLabel]
                        (int /*oldIndex*/, int newIndex) {
        if (newIndex < 0 || newIndex >= static_cast<int>(diagrams.size())) return;
        *activeDiagram = diagrams[static_cast<size_t>(newIndex)];
        if (statusLabel) statusLabel->SetText(statusTexts[static_cast<size_t>(newIndex)]);
    };

    // ---- Button bar ----
    const int BTN_W = 150;
    const int BTN_H = 28;
    const int BTN_GAP = 8;
    int buttonX = PAD;

    auto addButton = [&](const std::string& id, const std::string& text,
                         std::function<void()> action) {
        auto button = CreateButton(id, static_cast<float>(buttonX),
                                   static_cast<float>(yCursor),
                                   static_cast<float>(BTN_W), static_cast<float>(BTN_H),
                                   text);
        button->SetOnClick(std::move(action));
        root->AddChild(button);
        buttonX += BTN_W + BTN_GAP;
    };

    auto notationIndex = std::make_shared<int>(0);
    addButton("erBtnNotation", "Notation",
              [activeDiagram, notationIndex, statusLabel]() {
        auto diagram = *activeDiagram;
        if (!diagram) return;
        static const ERNotation order[] = {
            ERNotation::Chen, ERNotation::ChenMinMax, ERNotation::CrowsFoot
        };
        static const char* names[] = { "Chen", "Chen (min,max)", "Crow's Foot" };
        static const ERCardinalityLabels labels[] = {
            ERCardinalityLabels::Letters, ERCardinalityLabels::MinMax,
            ERCardinalityLabels::NoLabels
        };
        *notationIndex = (*notationIndex + 1) % 3;
        diagram->SetCardinalityLabelStyle(labels[*notationIndex]);
        diagram->SetNotation(order[*notationIndex]);
        diagram->AutoSizeAll();
        diagram->RunLayout();
        if (statusLabel) {
            statusLabel->SetText(std::string("Notation: ") + names[*notationIndex] +
                                 " - same model, different projection.");
        }
    });

    auto themeIndex = std::make_shared<int>(0);
    addButton("erBtnTheme", "Theme", [activeDiagram, themeIndex, statusLabel]() {
        auto diagram = *activeDiagram;
        if (!diagram) return;
        static const ERDiagramTheme order[] = {
            ERDiagramTheme::Default, ERDiagramTheme::Professional,
            ERDiagramTheme::Colorful, ERDiagramTheme::Pastel,
            ERDiagramTheme::Dark, ERDiagramTheme::Blueprint,
            ERDiagramTheme::Minimal, ERDiagramTheme::Print
        };
        static const char* names[] = { "Default", "Professional", "Colorful", "Pastel",
                                       "Dark", "Blueprint", "Minimal", "Print" };
        *themeIndex = (*themeIndex + 1) % 8;
        diagram->SetTheme(order[*themeIndex]);
        if (statusLabel) statusLabel->SetText(std::string("Theme: ") + names[*themeIndex]);
    });

    auto layoutIndex = std::make_shared<int>(0);
    addButton("erBtnLayout", "Layout", [activeDiagram, layoutIndex, statusLabel]() {
        auto diagram = *activeDiagram;
        if (!diagram) return;
        static const ERDiagramLayout order[] = {
            ERDiagramLayout::AttributeSatellite, ERDiagramLayout::ForceDirected,
            ERDiagramLayout::Spine, ERDiagramLayout::Symmetric
        };
        static const char* names[] = { "Attribute satellite", "Force directed",
                                       "Spine", "Symmetric" };
        *layoutIndex = (*layoutIndex + 1) % 4;
        diagram->SetLayout(order[*layoutIndex]);
        diagram->RunLayout();
        if (statusLabel) statusLabel->SetText(std::string("Layout: ") + names[*layoutIndex]);
    });

    addButton("erBtnConnector", "Connectors", [activeDiagram, statusLabel]() {
        auto diagram = *activeDiagram;
        if (!diagram) return;
        ERConnectorStyle next;
        const char* name = "";
        switch (diagram->GetConnectorStyle()) {
            case ERConnectorStyle::Straight:
                next = ERConnectorStyle::Orthogonal; name = "Orthogonal"; break;
            case ERConnectorStyle::Orthogonal:
                next = ERConnectorStyle::Bezier; name = "Bezier"; break;
            default:
                next = ERConnectorStyle::Straight; name = "Straight"; break;
        }
        diagram->SetConnectorStyle(next);
        if (statusLabel) statusLabel->SetText(std::string("Connectors: ") + name);
    });

    addButton("erBtnFit", "Fit View", [activeDiagram, statusLabel]() {
        auto diagram = *activeDiagram;
        if (!diagram) return;
        diagram->FitView();
        if (statusLabel) statusLabel->SetText("View fitted to content.");
    });

    addButton("erBtnJson", "Export JSON", [activeDiagram, statusLabel]() {
        auto diagram = *activeDiagram;
        if (!diagram || !statusLabel) return;
        std::string json = diagram->ToJson();
        statusLabel->SetText("Serialized model: " + std::to_string(json.size()) +
                             " bytes of JSON (" +
                             std::to_string(diagram->GetEntityCount()) + " entities, " +
                             std::to_string(diagram->GetRelationshipCount()) +
                             " relationships).");
    });

    root->AddChild(statusLabel);
    return root;
}

} // namespace UltraCanvas
