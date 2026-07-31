# UltraCanvasERDiagram Examples

Worked examples for [`UltraCanvasERDiagram`](UltraCanvasERDiagram.md). Each one
corresponds to a tab in the DemoApp scene
(`Apps/DemoApp/UltraCanvasERDiagramExamples.cpp`), reachable from
**Diagrams → ER Diagram**.

```cpp
#include "Plugins/Diagrams/UltraCanvasERDiagram.h"
```

---

## 1. Chen bodies with crow's-foot line ends, plus a legend

The marker set is deliberately independent of the notation, so the classic
"Chen shapes, crow's-foot ends" hybrid is a two-line configuration. The legend
is generated from whatever is actually active.

```cpp
auto er = CreateERDiagram("er_sales", 0, 0, 900, 520);

er->SetNotation(ERNotation::Chen);
er->SetLineEndStyle(ERLineEndStyle::CrowsFoot);
er->SetCardinalityLabelStyle(ERCardinalityLabels::NoLabels);  // Glyphs say it already
er->SetConnectorStyle(ERConnectorStyle::Orthogonal);
er->SetTitle("Entity Relationship Diagram - Internet Sales Model");
er->SetLegendVisible(true, ERPanelPosition::TopLeft);

er->AddEntity("customer", "Customer", 470, 300);
er->AddAttribute("customer", "Name", ERKeyRole::Primary);
er->AddAttribute("customer", "E-mail");
er->AddAttribute("customer", "Address", ERKeyRole::NoKey, ERAttributeKind::Composite);

er->AddEntity("order", "Order", 690, 620);
er->AddAttribute("order", "Order Number", ERKeyRole::Primary);

er->AddRelationship("places", "Orders", "customer", "order",
                    ERCardinality::ExactlyOne, ERCardinality::ZeroOrMany);

er->AutoSizeAll();
er->SetLayout(ERDiagramLayout::AttributeSatellite);
er->RunLayout();
```

---

## 2. A dense schema — attribute haloes at scale

Hand-placing fifty attribute ovals is not viable. `AttributeSatellite` fans each
entity's ovals onto the arc facing away from that entity's relationships;
`ForceDirected` places the entities and diamonds first, then re-fans.

```cpp
auto er = CreateERDiagram("er_dense", 0, 0, 900, 520);
er->SetTheme(ERDiagramTheme::Colorful);
er->SetCardinalityLabelStyle(ERCardinalityLabels::Letters);
er->SetConnectorStyle(ERConnectorStyle::Orthogonal);

struct EntitySpec { const char* id; const char* name;
                    std::vector<const char*> attributes; };

const std::vector<EntitySpec> specs = {
    { "student", "Student", { "StudentID", "First Name", "Last Name", "DOB",
                              "Email", "Phone", "Address" } },
    { "course",  "Course",  { "CourseID", "Title", "Credits", "Level" } },
    // ...
};

for (const auto& spec : specs) {
    er->AddEntity(spec.id, spec.name);
    bool first = true;
    for (const auto* name : spec.attributes) {
        er->AddAttribute(spec.id, name, first ? ERKeyRole::Primary : ERKeyRole::NoKey);
        first = false;
    }
}

er->AddRelationship("enrols", "Enrols", "student", "course",
                    ERCardinality::OneOrMany, ERCardinality::ZeroOrMany);

er->SetControlsVisible(true);              // Zoom / fit / lock overlay
er->SetHighlightRelatedOnHover(true);      // Hover an entity to isolate it

er->AutoSizeAll();
er->SetLayout(ERDiagramLayout::ForceDirected);
er->RunLayout();
```

`SetHighlightRelatedOnHover(true)` dims everything not directly related to the
hovered entity — the only practical way to read a schema this dense.

---

## 3. The textbook figure — key underlines and annotation callouts

`ERKeyRole::Primary` renders a solid underline under the attribute name;
`ERKeyRole::Partial` (a weak entity's discriminator) renders a dashed one.
Annotations anchor to an entity, a relationship, or a specific attribute using
`"entityId.attributeId"`.

```cpp
auto er = CreateERDiagram("er_student", 0, 0, 900, 520);
er->SetTheme(ERDiagramTheme::Professional);
er->SetCardinalityLabelStyle(ERCardinalityLabels::Letters);
er->SetTitle("Entity Relationship Diagram (ERD)");

er->AddEntity("student", "Student");
er->AddAttribute("student", "SSN", ERKeyRole::Primary);
er->AddAttribute("student", "Name");
er->AddAttribute("student", "Email");

er->AddEntity("course", "Course");
er->AddAttribute("course", "CID", ERKeyRole::Primary);
er->AddAttribute("course", "Title");
er->AddAttribute("course", "Duration");

er->AddRelationship("joins", "Joins", "student", "course",
                    ERCardinality::OneOrMany, ERCardinality::OneOrMany);

er->AddAnnotation("a1", "Student Entity", "student", ERAnnotationSide::Bottom);
er->AddAnnotation("a2", "Relationship", "joins", ERAnnotationSide::Bottom);
er->AddAnnotation("a3", "Prime Attribute", "student.SSN", ERAnnotationSide::Left);
er->AddAnnotation("a4", "Attribute", "student.Name", ERAnnotationSide::Top);

er->AutoSizeAll();
er->SetLayout(ERDiagramLayout::Symmetric);   // Two entities, diamond centred
er->RunLayout();
```

`RunLayout()` places the callouts and then slides each one along its own side
until it clears every oval, box, diamond and other callout.

---

## 4. Derived and multivalued attributes, spine layout

```cpp
auto er = CreateERDiagram("er_hospital", 0, 0, 900, 520);
er->SetTheme(ERDiagramTheme::Pastel);
er->SetConnectorStyle(ERConnectorStyle::Straight);
er->SetTitle("ER diagram of Hospital");

er->AddEntity("patient", "Patient");
er->AddAttribute("patient", "Patient_ID", ERKeyRole::Primary);
er->AddAttribute("patient", "Name");
er->AddAttribute("patient", "Age", ERKeyRole::NoKey, ERAttributeKind::Derived);

er->AddEntity("doctor", "Doctors");
er->AddAttribute("doctor", "Doctor_ID", ERKeyRole::Primary);
er->AddAttribute("doctor", "Phone", ERKeyRole::NoKey, ERAttributeKind::MultiValued);

er->AddRelationship("attended", "Attended_by", "patient", "doctor",
                    ERCardinality::OneOrMany, ERCardinality::ExactlyOne);

er->AutoSizeAll();
er->SetLayout(ERDiagramLayout::Spine);   // entity - diamond - entity on one axis
er->RunLayout();
```

`Derived` draws a dashed oval, `MultiValued` a double oval. Because the source
cardinality has `minCard > 0`, the `Patient` leg becomes
`ERParticipation::Total` and is drawn as a double line.

---

## 5. (min,max) cardinality, ternary and recursive relationships

The verbose `ERDiagramRelationship` API is the way to express anything beyond
the four common cardinalities.

```cpp
auto er = CreateERDiagram("er_academic", 0, 0, 900, 520);
er->SetNotation(ERNotation::ChenMinMax);
er->SetCardinalityLabelStyle(ERCardinalityLabels::MinMax);
er->SetConnectorStyle(ERConnectorStyle::Orthogonal);
er->SetShowRoleNames(true);

er->AddEntity("professor", "Professor", 120, 330);
er->AddEntity("subject",   "Subject",   560, 90);
er->AddEntity("course",    "Course",    560, 330);
er->AddEntity("student",   "Student",   980, 330);

// Explicit (min,max) per leg
ERDiagramRelationship attends("attends", "Attends");
attends.legs = {
    ERDiagramLeg("student", "", 1, ERCardinalityN, ERParticipation::Total),
    ERDiagramLeg("course",  "", 1, ERCardinalityN, ERParticipation::Partial)
};
er->AddRelationship(attends);

// Ternary: one diamond, three legs
er->AddNaryRelationship("assigns", "Assigns", {
    ERDiagramLeg("professor", "teacher", 1, ERCardinalityN),
    ERDiagramLeg("subject",   "topic",   1, ERCardinalityN),
    ERDiagramLeg("course",    "in",      0, ERCardinalityN)
});

// Recursive: both legs on Student, told apart by their role names
er->AddRecursiveRelationship("tutoring", "Tutor_Tutored", "student",
                             "tutor", "tutored",
                             ERCardinality::ZeroOrOne, ERCardinality::ZeroOrMany);

er->AutoSizeAll();
er->RunLayout();
```

`ERCardinalityN` (`-1`) is the "many" sentinel. `MinMax` labels print `(1,1)`,
`(1,N)`, `(0,N)`; switch to `ERCardinalityLabels::UML` for `1`, `1..*`, `0..*`.

---

## 6. Crow's Foot — a physical schema

```cpp
auto er = CreateERDiagram("er_crowsfoot", 0, 0, 900, 520);
er->SetNotation(ERNotation::CrowsFoot);
er->SetLineEndStyle(ERLineEndStyle::CrowsFoot);
er->SetTheme(ERDiagramTheme::Professional);
er->SetShowDataTypes(true);
er->SetConnectorStyle(ERConnectorStyle::Orthogonal);

er->AddEntity("customer", "CUSTOMER", 90, 120);
er->AddTypedAttribute("customer", "customer_id", "INTEGER", ERKeyRole::Primary, false);
er->AddTypedAttribute("customer", "name", "VARCHAR(80)", ERKeyRole::NoKey, false);
er->AddTypedAttribute("customer", "email", "VARCHAR(120)", ERKeyRole::Unique, false);

er->AddEntity("order", "ORDER", 480, 120);
er->AddTypedAttribute("order", "order_no", "INTEGER", ERKeyRole::Primary, false);
er->AddTypedAttribute("order", "customer_id", "INTEGER", ERKeyRole::Foreign, false);

er->AddEntity("line", "ORDER_LINE", 480, 400, ERDiagramEntityKind::Weak);
er->AddTypedAttribute("line", "order_no", "INTEGER", ERKeyRole::Primary, false);
er->AddTypedAttribute("line", "line_no", "INTEGER", ERKeyRole::Partial, false);

er->AddRelationship("places", "places", "customer", "order",
                    ERCardinality::ExactlyOne, ERCardinality::ZeroOrMany);
// ORDER_LINE has no identity without its ORDER - drawn solid, not dashed
er->AddIdentifyingRelationship("contains", "contains", "order", "line");

er->AutoSizeAll();
er->RunLayout();
```

Rows render as `PK  name : TYPE`, with `*` marking NOT NULL. Identifying
relationships draw solid, non-identifying dashed.

---

## 7. Switching notation at runtime

All three notations project the same model, so switching is a single call. Call
`AutoSizeAll()` afterwards, because crow's-foot boxes need room for their rows.

```cpp
void ShowAs(std::shared_ptr<UltraCanvasERDiagram> er, ERNotation notation) {
    er->SetCardinalityLabelStyle(notation == ERNotation::ChenMinMax
                                 ? ERCardinalityLabels::MinMax
                                 : ERCardinalityLabels::Letters);
    er->SetNotation(notation);
    er->AutoSizeAll();
    er->RunLayout();
}
```

Nothing is lost from the model: an n-ary relationship simply has no crow's-foot
projection and reappears when you switch back.

---

## 8. Saving and restoring

```cpp
std::string json = er->ToJson();      // Entities, attributes, legs, annotations, viewport
// ... write it wherever ...

auto restored = CreateERDiagram("restored", 0, 0, 900, 520);
if (!restored->FromJson(json)) {
    // Parse failure leaves the diagram cleared.
}
```

`FromJson` re-runs relationship placement, the attribute layout and annotation
placement, so a restored diagram is immediately presentable.

---

## 9. Wiring callbacks

```cpp
er->onEntityClick = [](const std::string& entityId) {
    std::cout << "entity " << entityId << "\n";
};
er->onAttributeClick = [](const std::string& entityId, const std::string& attributeId) {
    std::cout << entityId << "." << attributeId << "\n";
};
er->onSelectionChange = [](const std::vector<std::string>& entityIds,
                           const std::vector<std::string>& relationshipIds) {
    std::cout << entityIds.size() << " entities, "
              << relationshipIds.size() << " relationships selected\n";
};
er->onCanvasRightClick = [er](double worldX, double worldY) {
    // Somewhere to put a "new entity here" context menu.
};
```

---

## 10. Read-only presentation mode

```cpp
er->SetInteractive(false);   // Middle-drag still pans
er->SetControlsVisible(false);
er->SetTheme(ERDiagramTheme::Print);   // White fills, black strokes
er->FitView();
```

---

## 11. Relational / MERISE — table boxes with `(min,max)` and arrowheads

The table projection is not tied to crow's-foot markers. This is the
relational/MERISE style: entity boxes, `(min,max)` labels, plain arrowheads,
badge column on the right, striped rows.

```cpp
auto er = CreateERDiagram("er_relational", 0, 0, 900, 520);

er->SetNotation(ERNotation::CrowsFoot);
er->SetLineEndStyle(ERLineEndStyle::Arrow);                  // Not crow's feet
er->SetCardinalityLabelStyle(ERCardinalityLabels::MinMax);   // (1,n) / (0,1)
er->SetShowDataTypes(false);                                 // Column names only
er->SetShowRelationshipNames(false);

er->SetRowMarkerSide(ERRowMarkerSide::Right);
er->SetStripeRows(true);

er->AddEntity("users", "users", 330, 280);
er->AddTypedAttribute("users", "id_user", "", ERKeyRole::Primary, false);
er->AddTypedAttribute("users", "id_group", "", ERKeyRole::Foreign, false);
er->AddAttribute("users", "email");

er->AddEntity("groups", "groups", 40, 300);
er->AddTypedAttribute("groups", "id_group", "", ERKeyRole::Primary, false);
er->AddAttribute("groups", "title");

er->SetAttributeHighlighted("users", "id_group", true);   // Tint the badge cell

er->AddRelationship("user_group", "", "users", "groups",
                    ERCardinality::ExactlyOne, ERCardinality::ZeroOrMany);

er->AutoSizeAll();
er->RunLayout();
```

Neither `SetLineEndStyle` nor `SetCardinalityLabelStyle` is affected by
`SetNotation`, so the order of these calls does not matter.

---

## 12. IDEF1X / database-model style

Key columns hoisted into their own compartment above a rule and underlined,
foreign keys numbered so a reader can tell which constraint a column serves.

```cpp
auto er = CreateERDiagram("er_idef1x", 0, 0, 900, 520);

er->SetNotation(ERNotation::CrowsFoot);
er->SetLineEndStyle(ERLineEndStyle::CrowsFoot);
er->SetTheme(ERDiagramTheme::Minimal);
er->SetShowDataTypes(false);

er->SetKeyCompartment(true);      // Keys above the rule
er->SetUnderlineKeyRows(true);    // ... and underlined
er->SetNumberForeignKeys(true);   // FK1 / FK2 rather than a bare FK

er->AddEntity("equipcat", "tblEQUIP_CAT", 40, 230);
er->AddTypedAttribute("equipcat", "equip_cat_id", "", ERKeyRole::Primary, false);
er->AddTypedAttribute("equipcat", "category_id", "", ERKeyRole::Foreign, false);   // FK1
er->AddTypedAttribute("equipcat", "equipment_id", "", ERKeyRole::Foreign, false);  // FK2

er->AutoSizeAll();
er->RunLayout();
```

Foreign keys are numbered in order of appearance. To pin the numbering — when
two columns belong to the same constraint, say — set `foreignKeyIndex` on the
attribute:

```cpp
ERDiagramAttribute column("order_no", ERKeyRole::Foreign);
column.foreignKeyIndex = 1;       // Both halves of a composite FK read FK1
er->AddAttribute("line", column);
```

The compartment divider is drawn only when there are columns on both sides of
it, and the hoisting is display-only — `GetEntity()->attributes` keeps the order
you supplied.
