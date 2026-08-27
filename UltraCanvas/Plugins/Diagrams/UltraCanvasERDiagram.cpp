// Plugins/Diagrams/UltraCanvasERDiagram.cpp
// Entity-Relationship diagram component implementation
// Version: 1.0.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework
//
// See the header for the design note on why relationships are nodes.

#include "Plugins/Diagrams/UltraCanvasERDiagram.h"
#include "DataFormats/UltraCanvasJSON.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace UltraCanvas {

// =============================================================================
// CONSTANTS & LOCAL HELPERS
// =============================================================================

static constexpr double ER_PI = 3.14159265358979323846;
static constexpr double ER_CONNECTOR_HIT_TOLERANCE = 7.0;
static constexpr double ER_MIN_ENTITY_WIDTH = 90.0;
static constexpr double ER_MIN_ENTITY_HEIGHT = 46.0;
static constexpr double ER_ROW_HEIGHT = 20.0;
static constexpr double ER_HEADER_HEIGHT = 28.0;
static constexpr double ER_KEY_DIVIDER_GAP = 7.0;

namespace {

double Deg2Rad(double degrees) { return degrees * ER_PI / 180.0; }

Color WithAlpha(const Color& color, double alpha) {
    if (alpha >= 0.999) return color;
    int a = static_cast<int>(std::lround(static_cast<double>(color.a) * alpha));
    return Color(color.r, color.g, color.b, std::clamp(a, 0, 255));
}

Color Lighten(const Color& color, int amount) {
    return Color(std::min(255, color.r + amount),
                 std::min(255, color.g + amount),
                 std::min(255, color.b + amount),
                 color.a);
}

double Distance(const Point2Dd& a, const Point2Dd& b) {
    double dx = b.x - a.x;
    double dy = b.y - a.y;
    return std::sqrt(dx * dx + dy * dy);
}

// Shortest distance from p to the segment ab.
double DistanceToSegment(const Point2Dd& p, const Point2Dd& a, const Point2Dd& b) {
    double dx = b.x - a.x;
    double dy = b.y - a.y;
    double lenSq = dx * dx + dy * dy;
    if (lenSq < 1e-9) return Distance(p, a);
    double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / lenSq;
    t = std::clamp(t, 0.0, 1.0);
    return Distance(p, Point2Dd(a.x + t * dx, a.y + t * dy));
}

// Where the ray from the rect centre towards 'towards' crosses the rect border.
Point2Dd RectBorderPoint(const Rect2Dd& rect, const Point2Dd& towards) {
    double cx = rect.x + rect.width / 2.0;
    double cy = rect.y + rect.height / 2.0;
    double dx = towards.x - cx;
    double dy = towards.y - cy;
    if (std::abs(dx) < 1e-9 && std::abs(dy) < 1e-9) return Point2Dd(cx, cy);

    double halfW = rect.width / 2.0;
    double halfH = rect.height / 2.0;
    double scaleX = (std::abs(dx) > 1e-9) ? halfW / std::abs(dx) : 1e9;
    double scaleY = (std::abs(dy) > 1e-9) ? halfH / std::abs(dy) : 1e9;
    double scale = std::min(scaleX, scaleY);
    return Point2Dd(cx + dx * scale, cy + dy * scale);
}

// Same for a diamond inscribed in the rect (|x|/a + |y|/b = 1).
Point2Dd DiamondBorderPoint(const Rect2Dd& rect, const Point2Dd& towards) {
    double cx = rect.x + rect.width / 2.0;
    double cy = rect.y + rect.height / 2.0;
    double dx = towards.x - cx;
    double dy = towards.y - cy;
    if (std::abs(dx) < 1e-9 && std::abs(dy) < 1e-9) return Point2Dd(cx, cy);

    double a = rect.width / 2.0;
    double b = rect.height / 2.0;
    double denom = std::abs(dx) / a + std::abs(dy) / b;
    if (denom < 1e-9) return Point2Dd(cx, cy);
    double scale = 1.0 / denom;
    return Point2Dd(cx + dx * scale, cy + dy * scale);
}

// Same for an ellipse inscribed in the rect.
Point2Dd EllipseBorderPoint(const Rect2Dd& rect, const Point2Dd& towards) {
    double cx = rect.x + rect.width / 2.0;
    double cy = rect.y + rect.height / 2.0;
    double dx = towards.x - cx;
    double dy = towards.y - cy;
    double a = rect.width / 2.0;
    double b = rect.height / 2.0;
    if (a < 1e-9 || b < 1e-9) return Point2Dd(cx, cy);
    double denom = std::sqrt((dx * dx) / (a * a) + (dy * dy) / (b * b));
    if (denom < 1e-9) return Point2Dd(cx, cy);
    return Point2Dd(cx + dx / denom, cy + dy / denom);
}

// Conservative average-glyph-width approximation; matches the contract of
// UltraCanvasNodeDiagram::MeasureLabel (no render context needed).
void EstimateTextSize(const std::string& text, double fontSize, bool bold,
                      int& outWidth, int& outHeight) {
    double factor = bold ? 0.70 : 0.60;
    outWidth = static_cast<int>(std::lround(static_cast<double>(text.size()) *
                                            fontSize * factor));
    outHeight = static_cast<int>(std::lround(fontSize * 1.35));
}

const char* EntityKindToString(ERDiagramEntityKind kind) {
    switch (kind) {
        case ERDiagramEntityKind::Weak:        return "weak";
        case ERDiagramEntityKind::Associative: return "associative";
        case ERDiagramEntityKind::Strong:      break;
    }
    return "strong";
}

ERDiagramEntityKind EntityKindFromString(const std::string& text) {
    if (text == "weak") return ERDiagramEntityKind::Weak;
    if (text == "associative") return ERDiagramEntityKind::Associative;
    return ERDiagramEntityKind::Strong;
}

const char* AttributeKindToString(ERAttributeKind kind) {
    switch (kind) {
        case ERAttributeKind::Composite:   return "composite";
        case ERAttributeKind::MultiValued: return "multivalued";
        case ERAttributeKind::Derived:     return "derived";
        case ERAttributeKind::Simple:      break;
    }
    return "simple";
}

ERAttributeKind AttributeKindFromString(const std::string& text) {
    if (text == "composite")   return ERAttributeKind::Composite;
    if (text == "multivalued") return ERAttributeKind::MultiValued;
    if (text == "derived")     return ERAttributeKind::Derived;
    return ERAttributeKind::Simple;
}

const char* KeyRoleToString(ERKeyRole role) {
    switch (role) {
        case ERKeyRole::Primary: return "primary";
        case ERKeyRole::Partial: return "partial";
        case ERKeyRole::Foreign: return "foreign";
        case ERKeyRole::Unique:  return "unique";
        case ERKeyRole::NoKey:    break;
    }
    return "none";
}

ERKeyRole KeyRoleFromString(const std::string& text) {
    if (text == "primary") return ERKeyRole::Primary;
    if (text == "partial") return ERKeyRole::Partial;
    if (text == "foreign") return ERKeyRole::Foreign;
    if (text == "unique")  return ERKeyRole::Unique;
    return ERKeyRole::NoKey;
}

const char* ParticipationToString(ERParticipation participation) {
    switch (participation) {
        case ERParticipation::Total:    return "total";
        case ERParticipation::Optional: return "optional";
        case ERParticipation::Partial:  break;
    }
    return "partial";
}

ERParticipation ParticipationFromString(const std::string& text) {
    if (text == "total")    return ERParticipation::Total;
    if (text == "optional") return ERParticipation::Optional;
    return ERParticipation::Partial;
}

const char* NotationToString(ERNotation notation) {
    switch (notation) {
        case ERNotation::ChenMinMax: return "chenminmax";
        case ERNotation::CrowsFoot:  return "crowsfoot";
        case ERNotation::Chen:       break;
    }
    return "chen";
}

ERNotation NotationFromString(const std::string& text) {
    if (text == "chenminmax") return ERNotation::ChenMinMax;
    if (text == "crowsfoot")  return ERNotation::CrowsFoot;
    return ERNotation::Chen;
}

JSONValue AttributeToJson(const ERDiagramAttribute& attribute);

JSONValue AttributeListToJson(const std::vector<ERDiagramAttribute>& attributes) {
    JSONValue array = JSONValue::MakeArray();
    for (const auto& attribute : attributes) {
        array.Append(AttributeToJson(attribute));
    }
    return array;
}

JSONValue AttributeToJson(const ERDiagramAttribute& attribute) {
    JSONValue object = JSONValue::MakeObject();
    object.Set("id", attribute.id);
    object.Set("name", attribute.name);
    if (!attribute.dataType.empty()) object.Set("dataType", attribute.dataType);
    if (!attribute.comment.empty())  object.Set("comment", attribute.comment);
    object.Set("kind", std::string(AttributeKindToString(attribute.kind)));
    object.Set("keyRole", std::string(KeyRoleToString(attribute.keyRole)));
    object.Set("nullable", attribute.nullable);
    if (!attribute.defaultValue.empty()) object.Set("default", attribute.defaultValue);
    if (attribute.highlighted) object.Set("highlighted", true);
    if (attribute.foreignKeyIndex > 0) {
        object.Set("foreignKeyIndex", static_cast<int64_t>(attribute.foreignKeyIndex));
    }
    if (attribute.hasPlacement) {
        object.Set("x", attribute.x);
        object.Set("y", attribute.y);
        object.Set("width", attribute.width);
        object.Set("height", attribute.height);
    }
    if (attribute.useCustomColors) {
        object.Set("fillColor", JSON::FromColor(attribute.fillColor));
        object.Set("borderColor", JSON::FromColor(attribute.borderColor));
        object.Set("textColor", JSON::FromColor(attribute.textColor));
    }
    if (!attribute.children.empty()) {
        object.Set("children", AttributeListToJson(attribute.children));
    }
    return object;
}

ERDiagramAttribute AttributeFromJson(const JSONValue& object);

std::vector<ERDiagramAttribute> AttributeListFromJson(const JSONValue& array) {
    std::vector<ERDiagramAttribute> attributes;
    if (!array.IsArray()) return attributes;
    for (const auto& element : array.GetElements()) {
        attributes.push_back(AttributeFromJson(element));
    }
    return attributes;
}

ERDiagramAttribute AttributeFromJson(const JSONValue& object) {
    ERDiagramAttribute attribute;
    attribute.id = object.Get("id").GetString();
    attribute.name = object.Get("name").GetString();
    if (attribute.id.empty()) attribute.id = attribute.name;
    attribute.dataType = object.Get("dataType").GetString();
    attribute.comment = object.Get("comment").GetString();
    attribute.kind = AttributeKindFromString(object.Get("kind").GetString());
    attribute.keyRole = KeyRoleFromString(object.Get("keyRole").GetString());
    attribute.nullable = object.Get("nullable").GetBoolean(true);
    attribute.defaultValue = object.Get("default").GetString();
    attribute.highlighted = object.Get("highlighted").GetBoolean(false);
    attribute.foreignKeyIndex =
            static_cast<int>(object.Get("foreignKeyIndex").GetInteger(0));
    if (object.Contains("x") && object.Contains("y")) {
        attribute.x = object.Get("x").GetNumber();
        attribute.y = object.Get("y").GetNumber();
        attribute.width = object.Get("width").GetNumber(96.0);
        attribute.height = object.Get("height").GetNumber(34.0);
        attribute.hasPlacement = true;
    }
    if (object.Contains("fillColor")) {
        attribute.useCustomColors = true;
        JSON::ToColor(object.Get("fillColor"), attribute.fillColor);
        JSON::ToColor(object.Get("borderColor"), attribute.borderColor);
        JSON::ToColor(object.Get("textColor"), attribute.textColor);
    }
    if (object.Contains("children")) {
        attribute.children = AttributeListFromJson(object.Get("children"));
    }
    return attribute;
}

} // namespace

// =============================================================================
// CONSTRUCTOR
// =============================================================================

UltraCanvasERDiagram::UltraCanvasERDiagram(const std::string& id,
                                           int x, int y, int width, int height)
    : UltraCanvasUIElement(id, x, y, width, height) {
    ApplyThemeColors();
}

// =============================================================================
// ENTITIES
// =============================================================================

void UltraCanvasERDiagram::AddEntity(const std::string& id, const std::string& name,
                                      ERDiagramEntityKind kind) {
    AddEntity(id, name, 0.0, 0.0, kind);
}

void UltraCanvasERDiagram::AddEntity(const std::string& id, const std::string& name,
                                      double x, double y, ERDiagramEntityKind kind) {
    ERDiagramEntity entity(id, name, kind);
    entity.x = x;
    entity.y = y;
    SuggestEntitySizeForName(name, entity.width, entity.height);
    AddEntity(entity);
}

void UltraCanvasERDiagram::AddEntity(const ERDiagramEntity& entity) {
    if (entity.id.empty() || entities.find(entity.id) != entities.end()) return;
    entities[entity.id] = entity;
    entityOrder.push_back(entity.id);
    NotifyModelChanged();
    RequestRedraw();
}

void UltraCanvasERDiagram::RemoveEntity(const std::string& id) {
    auto it = entities.find(id);
    if (it == entities.end()) return;

    // Drop every relationship that still references the entity.
    std::vector<std::string> doomed;
    for (const auto& entry : relationships) {
        for (const auto& leg : entry.second.legs) {
            if (leg.entityId == id) { doomed.push_back(entry.first); break; }
        }
    }
    for (const auto& relId : doomed) RemoveRelationship(relId);

    entities.erase(it);
    entityOrder.erase(std::remove(entityOrder.begin(), entityOrder.end(), id),
                      entityOrder.end());
    NotifyModelChanged();
    RequestRedraw();
}

void UltraCanvasERDiagram::SetEntityPosition(const std::string& id, double x, double y) {
    auto* entity = GetEntity(id);
    if (!entity) return;
    MoveEntityBy(*entity, x - entity->x, y - entity->y);
    RequestRedraw();
}

void UltraCanvasERDiagram::SetEntitySize(const std::string& id, double width, double height) {
    auto* entity = GetEntity(id);
    if (!entity) return;
    entity->width = std::max(width, ER_MIN_ENTITY_WIDTH);
    entity->height = std::max(height, ER_MIN_ENTITY_HEIGHT);
    RequestRedraw();
}

void UltraCanvasERDiagram::SetEntityName(const std::string& id, const std::string& name) {
    auto* entity = GetEntity(id);
    if (!entity) return;
    entity->name = name;
    NotifyModelChanged();
    RequestRedraw();
}

void UltraCanvasERDiagram::SetEntityKind(const std::string& id, ERDiagramEntityKind kind) {
    auto* entity = GetEntity(id);
    if (!entity) return;
    entity->kind = kind;
    NotifyModelChanged();
    RequestRedraw();
}

void UltraCanvasERDiagram::SetEntityColors(const std::string& id, const Color& fill,
                                            const Color& border) {
    auto* entity = GetEntity(id);
    if (!entity) return;
    entity->useCustomColors = true;
    entity->fillColor = fill;
    entity->borderColor = border;
    RequestRedraw();
}

void UltraCanvasERDiagram::SetEntityCollapsed(const std::string& id, bool collapsed) {
    auto* entity = GetEntity(id);
    if (!entity) return;
    entity->collapsed = collapsed;
    RequestRedraw();
}

void UltraCanvasERDiagram::PinEntity(const std::string& id, bool pinned) {
    auto* entity = GetEntity(id);
    if (entity) entity->isPinned = pinned;
}

ERDiagramEntity* UltraCanvasERDiagram::GetEntity(const std::string& id) {
    auto it = entities.find(id);
    return (it == entities.end()) ? nullptr : &it->second;
}

const ERDiagramEntity* UltraCanvasERDiagram::GetEntity(const std::string& id) const {
    auto it = entities.find(id);
    return (it == entities.end()) ? nullptr : &it->second;
}

std::vector<std::string> UltraCanvasERDiagram::GetAllEntityIds() const {
    return entityOrder;
}

// =============================================================================
// ATTRIBUTES
// =============================================================================

void UltraCanvasERDiagram::AddAttribute(const std::string& entityId, const std::string& name,
                                         ERKeyRole keyRole, ERAttributeKind kind) {
    AddAttribute(entityId, ERDiagramAttribute(name, keyRole, kind));
}

void UltraCanvasERDiagram::AddAttribute(const std::string& entityId,
                                         const ERDiagramAttribute& attribute) {
    auto* entity = GetEntity(entityId);
    if (!entity) return;
    ERDiagramAttribute copy = attribute;
    if (copy.id.empty()) copy.id = copy.name;
    for (const auto& existing : entity->attributes) {
        if (existing.id == copy.id) return;
    }
    entity->attributes.push_back(copy);
    NotifyModelChanged();
    RequestRedraw();
}

void UltraCanvasERDiagram::AddTypedAttribute(const std::string& entityId,
                                              const std::string& name,
                                              const std::string& dataType,
                                              ERKeyRole keyRole, bool nullable) {
    ERDiagramAttribute attribute(name, keyRole);
    attribute.dataType = dataType;
    attribute.nullable = nullable && keyRole != ERKeyRole::Primary;
    AddAttribute(entityId, attribute);
}

void UltraCanvasERDiagram::AddChildAttribute(const std::string& entityId,
                                              const std::string& parentAttributeId,
                                              const ERDiagramAttribute& child) {
    auto* parent = GetAttribute(entityId, parentAttributeId);
    if (!parent) return;
    ERDiagramAttribute copy = child;
    if (copy.id.empty()) copy.id = copy.name;
    parent->kind = ERAttributeKind::Composite;
    parent->children.push_back(copy);
    NotifyModelChanged();
    RequestRedraw();
}

void UltraCanvasERDiagram::AddRelationshipAttribute(const std::string& relationshipId,
                                                     const ERDiagramAttribute& attribute) {
    auto* rel = GetRelationship(relationshipId);
    if (!rel) return;
    ERDiagramAttribute copy = attribute;
    if (copy.id.empty()) copy.id = copy.name;
    rel->attributes.push_back(copy);
    NotifyModelChanged();
    RequestRedraw();
}

void UltraCanvasERDiagram::RemoveAttribute(const std::string& entityId,
                                            const std::string& attributeId) {
    auto* entity = GetEntity(entityId);
    if (!entity) return;
    auto& list = entity->attributes;
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&](const ERDiagramAttribute& a) { return a.id == attributeId; }),
               list.end());
    NotifyModelChanged();
    RequestRedraw();
}

ERDiagramAttribute* UltraCanvasERDiagram::GetAttribute(const std::string& entityId,
                                                        const std::string& attributeId) {
    auto* entity = GetEntity(entityId);
    if (!entity) return nullptr;
    for (auto& attribute : entity->attributes) {
        if (attribute.id == attributeId) return &attribute;
    }
    return nullptr;
}

void UltraCanvasERDiagram::SetAttributePosition(const std::string& entityId,
                                                 const std::string& attributeId,
                                                 double x, double y) {
    auto* attribute = GetAttribute(entityId, attributeId);
    if (!attribute) return;
    attribute->x = x;
    attribute->y = y;
    attribute->hasPlacement = true;
    RequestRedraw();
}

// =============================================================================
// RELATIONSHIPS
// =============================================================================

void UltraCanvasERDiagram::AddRelationship(const std::string& id, const std::string& name,
                                            const std::string& sourceEntityId,
                                            const std::string& targetEntityId,
                                            ERCardinality sourceCardinality,
                                            ERCardinality targetCardinality) {
    ERDiagramRelationship rel(id, name);

    ERDiagramLeg source;
    source.entityId = sourceEntityId;
    ERCardinalityToMinMax(sourceCardinality, source.minCard, source.maxCard);
    source.participation = (source.minCard > 0) ? ERParticipation::Total
                                                : ERParticipation::Partial;

    ERDiagramLeg target;
    target.entityId = targetEntityId;
    ERCardinalityToMinMax(targetCardinality, target.minCard, target.maxCard);
    target.participation = (target.minCard > 0) ? ERParticipation::Total
                                                : ERParticipation::Partial;

    rel.legs = { source, target };
    AddRelationship(rel);
}

void UltraCanvasERDiagram::AddRelationship(const ERDiagramRelationship& relationship) {
    if (relationship.id.empty() ||
        relationships.find(relationship.id) != relationships.end()) {
        return;
    }
    ERDiagramRelationship copy = relationship;
    if (copy.width <= 0.0 || copy.height <= 0.0) {
        copy.width = 110.0;
        copy.height = 62.0;
    }
    relationships[copy.id] = copy;
    relationshipOrder.push_back(copy.id);
    NotifyModelChanged();
    RequestRedraw();
}

void UltraCanvasERDiagram::AddIdentifyingRelationship(const std::string& id,
                                                       const std::string& name,
                                                       const std::string& ownerEntityId,
                                                       const std::string& weakEntityId) {
    ERDiagramRelationship rel(id, name);
    rel.kind = ERRelationshipKind::Identifying;

    // The owner side is 1; the weak side participates totally by definition.
    rel.legs = {
        ERDiagramLeg(ownerEntityId, std::string(), 1, 1, ERParticipation::Partial),
        ERDiagramLeg(weakEntityId, std::string(), 1, ERCardinalityN, ERParticipation::Total)
    };
    AddRelationship(rel);

    auto* weak = GetEntity(weakEntityId);
    if (weak) weak->kind = ERDiagramEntityKind::Weak;
}

void UltraCanvasERDiagram::AddRecursiveRelationship(const std::string& id,
                                                     const std::string& name,
                                                     const std::string& entityId,
                                                     const std::string& roleA,
                                                     const std::string& roleB,
                                                     ERCardinality cardA,
                                                     ERCardinality cardB) {
    ERDiagramRelationship rel(id, name);
    ERDiagramLeg legA(entityId, roleA, 0, 1);
    ERDiagramLeg legB(entityId, roleB, 0, ERCardinalityN);
    ERCardinalityToMinMax(cardA, legA.minCard, legA.maxCard);
    ERCardinalityToMinMax(cardB, legB.minCard, legB.maxCard);
    rel.legs = { legA, legB };
    AddRelationship(rel);
}

void UltraCanvasERDiagram::AddNaryRelationship(const std::string& id, const std::string& name,
                                                const std::vector<ERDiagramLeg>& legs) {
    ERDiagramRelationship rel(id, name);
    rel.legs = legs;
    AddRelationship(rel);
}

void UltraCanvasERDiagram::RemoveRelationship(const std::string& id) {
    auto it = relationships.find(id);
    if (it == relationships.end()) return;
    relationships.erase(it);
    relationshipOrder.erase(std::remove(relationshipOrder.begin(), relationshipOrder.end(), id),
                            relationshipOrder.end());
    NotifyModelChanged();
    RequestRedraw();
}

void UltraCanvasERDiagram::SetRelationshipPosition(const std::string& id, double x, double y) {
    auto* rel = GetRelationship(id);
    if (!rel) return;
    rel->x = x;
    rel->y = y;
    rel->hasPlacement = true;
    RequestRedraw();
}

void UltraCanvasERDiagram::SetRelationshipName(const std::string& id, const std::string& name) {
    auto* rel = GetRelationship(id);
    if (!rel) return;
    rel->name = name;
    NotifyModelChanged();
    RequestRedraw();
}

void UltraCanvasERDiagram::SetRelationshipKind(const std::string& id, ERRelationshipKind kind) {
    auto* rel = GetRelationship(id);
    if (!rel) return;
    rel->kind = kind;
    RequestRedraw();
}

void UltraCanvasERDiagram::SetRelationshipColors(const std::string& id, const Color& fill,
                                                  const Color& border) {
    auto* rel = GetRelationship(id);
    if (!rel) return;
    rel->useCustomColors = true;
    rel->fillColor = fill;
    rel->borderColor = border;
    RequestRedraw();
}

void UltraCanvasERDiagram::SetRelationshipConnectorStyle(const std::string& id,
                                                          ERConnectorStyle newStyle) {
    auto* rel = GetRelationship(id);
    if (!rel) return;
    rel->connectorStyle = newStyle;
    rel->connectorStyleOverride = true;
    RequestRedraw();
}

void UltraCanvasERDiagram::SetLegCardinality(const std::string& relationshipId, size_t legIndex,
                                              int minCard, int maxCard) {
    auto* rel = GetRelationship(relationshipId);
    if (!rel || legIndex >= rel->legs.size()) return;
    rel->legs[legIndex].minCard = minCard;
    rel->legs[legIndex].maxCard = maxCard;
    RequestRedraw();
}

void UltraCanvasERDiagram::SetLegParticipation(const std::string& relationshipId, size_t legIndex,
                                                ERParticipation participation) {
    auto* rel = GetRelationship(relationshipId);
    if (!rel || legIndex >= rel->legs.size()) return;
    rel->legs[legIndex].participation = participation;
    RequestRedraw();
}

void UltraCanvasERDiagram::SetLegRoleName(const std::string& relationshipId, size_t legIndex,
                                           const std::string& roleName) {
    auto* rel = GetRelationship(relationshipId);
    if (!rel || legIndex >= rel->legs.size()) return;
    rel->legs[legIndex].roleName = roleName;
    RequestRedraw();
}

ERDiagramRelationship* UltraCanvasERDiagram::GetRelationship(const std::string& id) {
    auto it = relationships.find(id);
    return (it == relationships.end()) ? nullptr : &it->second;
}

const ERDiagramRelationship* UltraCanvasERDiagram::GetRelationship(const std::string& id) const {
    auto it = relationships.find(id);
    return (it == relationships.end()) ? nullptr : &it->second;
}

std::vector<std::string> UltraCanvasERDiagram::GetAllRelationshipIds() const {
    return relationshipOrder;
}

// =============================================================================
// ANNOTATIONS
// =============================================================================

void UltraCanvasERDiagram::AddAnnotation(const std::string& id, const std::string& text,
                                          const std::string& anchorRef, ERAnnotationSide side) {
    AddAnnotation(ERDiagramAnnotation(id, text, anchorRef, side));
}

void UltraCanvasERDiagram::AddAnnotation(const ERDiagramAnnotation& annotation) {
    if (annotation.id.empty() || annotations.find(annotation.id) != annotations.end()) return;
    annotations[annotation.id] = annotation;
    annotationOrder.push_back(annotation.id);
    RequestRedraw();
}

void UltraCanvasERDiagram::RemoveAnnotation(const std::string& id) {
    auto it = annotations.find(id);
    if (it == annotations.end()) return;
    annotations.erase(it);
    annotationOrder.erase(std::remove(annotationOrder.begin(), annotationOrder.end(), id),
                          annotationOrder.end());
    RequestRedraw();
}

ERDiagramAnnotation* UltraCanvasERDiagram::GetAnnotation(const std::string& id) {
    auto it = annotations.find(id);
    return (it == annotations.end()) ? nullptr : &it->second;
}

// =============================================================================
// NOTATION
// =============================================================================

void UltraCanvasERDiagram::SetNotation(ERNotation newNotation) {
    if (notation == newNotation) return;
    notation = newNotation;

    // Crow's foot puts attributes inside the box; the entity has to grow to
    // hold the rows. Chen shrinks it back to a name plate.
    for (auto& entry : entities) {
        auto& entity = entry.second;
        if (notation == ERNotation::CrowsFoot) {
            entity.height = MeasuredEntityHeight(entity);
        } else {
            double w = entity.width;
            double h = entity.height;
            SuggestEntitySizeForName(entity.name, w, h);
            entity.width = std::max(entity.width, w);
            entity.height = h;
        }
    }
    // No label style is forced here. An earlier version cleared MinMax under
    // CrowsFoot on the theory that the pair and the glyph say the same thing
    // twice - but the relational/MERISE style is exactly a table notation
    // carrying (min,max) labels and no crow's feet, and the clear was also
    // order-dependent (it only fired when the notation actually changed).
    RequestRedraw();
}

void UltraCanvasERDiagram::SetLineEndStyle(ERLineEndStyle newStyle) {
    lineEndStyle = newStyle;
    RequestRedraw();
}

void UltraCanvasERDiagram::SetCardinalityLabelStyle(ERCardinalityLabels newStyle) {
    cardinalityLabels = newStyle;
    RequestRedraw();
}

void UltraCanvasERDiagram::SetShowRoleNames(bool show) {
    showRoleNames = show;
    RequestRedraw();
}

void UltraCanvasERDiagram::SetShowRelationshipNames(bool show) {
    showRelationshipNames = show;
    RequestRedraw();
}

void UltraCanvasERDiagram::SetShowDataTypes(bool show) {
    showDataTypes = show;
    RequestRedraw();
}

// =============================================================================
// LAYOUT
// =============================================================================

void UltraCanvasERDiagram::SetLayout(ERDiagramLayout layout) {
    layoutMode = layout;
}

void UltraCanvasERDiagram::RunLayout() {
    switch (layoutMode) {
        case ERDiagramLayout::Manual:
            PlaceUnplacedRelationships();
            ApplyAttributeSatelliteLayout();
            break;
        case ERDiagramLayout::AttributeSatellite:
            PlaceUnplacedRelationships();
            ApplyAttributeSatelliteLayout();
            break;
        case ERDiagramLayout::Spine:
            ApplySpineLayout(true);
            break;
        case ERDiagramLayout::ForceDirected:
            RunForceDirectedLayout(0);
            break;
        case ERDiagramLayout::Symmetric:
            ApplySymmetricLayout();
            break;
    }
    ResolveAnnotationPlacement();
    if (autoFitOnLayout) FitView();
    RequestRedraw();
}

// Entities and relationship diamonds are bodies; legs are springs. Attributes
// ride along with their entity afterwards.
void UltraCanvasERDiagram::RunForceDirectedLayout(int iterationCount) {
    if (entities.empty()) return;
    int iterations = (iterationCount > 0) ? iterationCount : style.iterations;

    double width = static_cast<double>(finalBounds.width);
    double height = static_cast<double>(finalBounds.height);
    if (width < 50.0 || height < 50.0) { width = 900.0; height = 620.0; }
    double centerX = width / 2.0;
    double centerY = height / 2.0;

    // Deterministic circular seeding - random placement made Reset produce a
    // different (sometimes bad) result every time. Same lesson as NodeDiagram
    // 2.0.4.
    struct Body {
        std::string id;
        bool isRelationship = false;
        double x = 0.0, y = 0.0, vx = 0.0, vy = 0.0;
        double radius = 60.0;
        bool pinned = false;
    };

    std::vector<Body> bodies;
    std::map<std::string, size_t> indexOf;

    for (const auto& id : entityOrder) {
        auto& entity = entities[id];
        Body body;
        body.id = id;
        body.radius = std::max(entity.width, entity.height) / 2.0 + 30.0;
        body.pinned = entity.isPinned;
        bodies.push_back(body);
        indexOf[id] = bodies.size() - 1;
    }
    for (const auto& id : relationshipOrder) {
        auto& rel = relationships[id];
        Body body;
        body.id = id;
        body.isRelationship = true;
        body.radius = std::max(rel.width, rel.height) / 2.0 + 20.0;
        bodies.push_back(body);
        indexOf["rel:" + id] = bodies.size() - 1;
    }
    if (bodies.empty()) return;

    // Seed on an ellipse matching the viewport, not a circle: a round result
    // in a wide panel wastes most of the width once FitView scales it down.
    double seedRadiusX = width * 0.36;
    double seedRadiusY = height * 0.36;
    for (size_t i = 0; i < bodies.size(); ++i) {
        double angle = (2.0 * ER_PI * static_cast<double>(i)) /
                       static_cast<double>(bodies.size());
        bodies[i].x = centerX + seedRadiusX * std::cos(angle);
        bodies[i].y = centerY + seedRadiusY * std::sin(angle);
    }

    // Anisotropic centring keeps the cloud in the viewport's aspect ratio.
    double aspect = std::clamp(width / std::max(height, 1.0), 0.25, 4.0);

    // Springs: one per leg (entity <-> relationship body).
    struct Spring { size_t a; size_t b; };
    std::vector<Spring> springs;
    for (const auto& relId : relationshipOrder) {
        auto relIt = indexOf.find("rel:" + relId);
        if (relIt == indexOf.end()) continue;
        for (const auto& leg : relationships[relId].legs) {
            auto entityIt = indexOf.find(leg.entityId);
            if (entityIt == indexOf.end()) continue;
            springs.push_back({ entityIt->second, relIt->second });
        }
    }

    const double damping = 0.86;
    for (int step = 0; step < iterations; ++step) {
        for (auto& body : bodies) { body.vx *= damping; body.vy *= damping; }

        // Repulsion
        for (size_t i = 0; i < bodies.size(); ++i) {
            for (size_t j = i + 1; j < bodies.size(); ++j) {
                double dx = bodies[j].x - bodies[i].x;
                double dy = bodies[j].y - bodies[i].y;
                double distSq = dx * dx + dy * dy;
                if (distSq < 1.0) { dx = 1.0; dy = 1.0; distSq = 2.0; }
                double dist = std::sqrt(distSq);
                double force = style.chargeStrength / distSq;
                double fx = (dx / dist) * force;
                double fy = (dy / dist) * force;
                bodies[i].vx -= fx; bodies[i].vy -= fy;
                bodies[j].vx += fx; bodies[j].vy += fy;
            }
        }

        // Springs
        for (const auto& spring : springs) {
            Body& a = bodies[spring.a];
            Body& b = bodies[spring.b];
            double dx = b.x - a.x;
            double dy = b.y - a.y;
            double dist = std::sqrt(dx * dx + dy * dy);
            if (dist < 0.01) continue;
            double desired = style.linkDistance * 0.6;
            double force = (dist - desired) * style.linkStrength;
            double fx = (dx / dist) * force;
            double fy = (dy / dist) * force;
            a.vx += fx; a.vy += fy;
            b.vx -= fx; b.vy -= fy;
        }

        // Weak pull to the centre so nothing drifts off-canvas, scaled by the
        // viewport aspect so a wide panel gets a wide graph.
        for (auto& body : bodies) {
            body.vx += (centerX - body.x) * 0.004 / aspect;
            body.vy += (centerY - body.y) * 0.004 * aspect;
        }

        for (auto& body : bodies) {
            if (body.pinned) { body.vx = 0.0; body.vy = 0.0; continue; }
            body.x += std::clamp(body.vx, -30.0, 30.0);
            body.y += std::clamp(body.vy, -30.0, 30.0);
        }
    }

    // Discrete timesteps can leave residual overlap; push pairs apart.
    for (int pass = 0; pass < 40; ++pass) {
        bool moved = false;
        for (size_t i = 0; i < bodies.size(); ++i) {
            for (size_t j = i + 1; j < bodies.size(); ++j) {
                double dx = bodies[j].x - bodies[i].x;
                double dy = bodies[j].y - bodies[i].y;
                double dist = std::sqrt(dx * dx + dy * dy);
                double minDist = bodies[i].radius + bodies[j].radius;
                if (dist >= minDist) continue;
                if (dist < 0.01) { dx = 1.0; dy = 0.0; dist = 1.0; }
                double push = (minDist - dist) / 2.0;
                double ux = dx / dist;
                double uy = dy / dist;
                if (!bodies[i].pinned) { bodies[i].x -= ux * push; bodies[i].y -= uy * push; }
                if (!bodies[j].pinned) { bodies[j].x += ux * push; bodies[j].y += uy * push; }
                moved = true;
            }
        }
        if (!moved) break;
    }

    for (const auto& body : bodies) {
        if (body.isRelationship) {
            auto* rel = GetRelationship(body.id);
            if (!rel) continue;
            rel->x = body.x - rel->width / 2.0;
            rel->y = body.y - rel->height / 2.0;
            rel->hasPlacement = true;
        } else {
            auto* entity = GetEntity(body.id);
            if (!entity) continue;
            entity->x = body.x - entity->width / 2.0;
            entity->y = body.y - entity->height / 2.0;
        }
    }

    ApplyAttributeSatelliteLayout();
}

// Fan the ovals around the entity on the arc facing away from its legs.
// Reference image 2 is unusable without this.
void UltraCanvasERDiagram::ApplyAttributeSatelliteLayout() {
    if (notation == ERNotation::CrowsFoot) return;   // Rows live inside the box

    for (const auto& entityId : entityOrder) {
        auto& entity = entities[entityId];
        if (entity.attributes.empty()) continue;

        double baseAngle = PreferredAttributeDirection(entity);
        Point2Dd center = GetEntityCenter(entity);

        size_t count = entity.attributes.size();
        double spread = Deg2Rad(style.attributeSpread);
        // One ring holds ~6 ovals comfortably; overflow steps outwards.
        size_t perRing = std::max<size_t>(3, static_cast<size_t>(style.attributeSpread / 45.0));
        size_t index = 0;

        for (auto& attribute : entity.attributes) {
            if (attribute.hasPlacement) { ++index; continue; }

            size_t ring = index / perRing;
            size_t slot = index % perRing;
            size_t slotsInRing = std::min(perRing, count - ring * perRing);

            double t = (slotsInRing <= 1) ? 0.5
                                          : static_cast<double>(slot) /
                                            static_cast<double>(slotsInRing - 1);
            double angle = baseAngle - spread / 2.0 + t * spread;

            // Measure the orbit from the entity's half-diagonal and add half
            // the oval, so an oval can never land on its own entity whatever
            // the fan angle. attributeOrbit is the clear gap between the two.
            double halfDiag = 0.5 * std::sqrt(entity.width * entity.width +
                                              entity.height * entity.height);
            double radius = halfDiag + attribute.width / 2.0 +
                            style.attributeOrbit * 0.45 +
                            static_cast<double>(ring) * style.attributeRingStep;

            attribute.x = center.x + radius * std::cos(angle) - attribute.width / 2.0;
            attribute.y = center.y + radius * std::sin(angle) - attribute.height / 2.0;

            // Composite children orbit their parent one step further out.
            double childBase = angle;
            double childSpread = Deg2Rad(50.0);
            size_t childIndex = 0;
            for (auto& child : attribute.children) {
                double ct = (attribute.children.size() <= 1)
                            ? 0.5
                            : static_cast<double>(childIndex) /
                              static_cast<double>(attribute.children.size() - 1);
                double childAngle = childBase - childSpread / 2.0 + ct * childSpread;
                double childRadius = radius + style.attributeRingStep + 12.0;
                child.x = center.x + childRadius * std::cos(childAngle) - child.width / 2.0;
                child.y = center.y + childRadius * std::sin(childAngle) - child.height / 2.0;
                ++childIndex;
            }
            ++index;
        }
    }

    // Relationship attributes hang below their diamond.
    for (const auto& relId : relationshipOrder) {
        auto& rel = relationships[relId];
        if (rel.attributes.empty()) continue;
        Point2Dd center = GetRelationshipCenter(rel);
        size_t index = 0;
        for (auto& attribute : rel.attributes) {
            if (attribute.hasPlacement) { ++index; continue; }
            double offset = (static_cast<double>(index) -
                             (static_cast<double>(rel.attributes.size()) - 1.0) / 2.0) *
                            (attribute.width + 12.0);
            attribute.x = center.x + offset - attribute.width / 2.0;
            attribute.y = center.y + rel.height / 2.0 + 46.0;
            ++index;
        }
    }

    ResolveAttributeOverlaps();
}

// entity - relationship - entity chains laid along one axis (image 4).
void UltraCanvasERDiagram::ApplySpineLayout(bool horizontal) {
    if (entityOrder.empty()) return;

    const double gap = 90.0;
    double cursor = 60.0;
    std::set<std::string> placedEntities;
    std::set<std::string> placedRelationships;

    double axis = horizontal ? static_cast<double>(finalBounds.height) / 2.0
                             : static_cast<double>(finalBounds.width) / 2.0;
    if (axis < 60.0) axis = 300.0;

    auto place = [&](double& x, double& y, double w, double h) {
        if (horizontal) {
            x = cursor;
            y = axis - h / 2.0;
            cursor += w + gap;
        } else {
            x = axis - w / 2.0;
            y = cursor;
            cursor += h + gap;
        }
    };

    // Walk the relationships in insertion order, emitting each chain.
    for (const auto& relId : relationshipOrder) {
        auto& rel = relationships[relId];
        if (rel.legs.empty()) continue;

        for (size_t i = 0; i < rel.legs.size(); ++i) {
            const auto& leg = rel.legs[i];
            auto* entity = GetEntity(leg.entityId);
            if (!entity) continue;

            if (i == 0 && !placedEntities.count(leg.entityId)) {
                place(entity->x, entity->y, entity->width, entity->height);
                placedEntities.insert(leg.entityId);
            }
            if (i == 0 && !placedRelationships.count(relId)) {
                place(rel.x, rel.y, rel.width, rel.height);
                rel.hasPlacement = true;
                placedRelationships.insert(relId);
            }
            if (i > 0 && !placedEntities.count(leg.entityId)) {
                place(entity->x, entity->y, entity->width, entity->height);
                placedEntities.insert(leg.entityId);
            }
        }
    }

    // Anything the walk did not reach goes on a second row.
    double strayAxis = axis + 220.0;
    double strayCursor = 60.0;
    for (const auto& entityId : entityOrder) {
        if (placedEntities.count(entityId)) continue;
        auto& entity = entities[entityId];
        if (horizontal) {
            entity.x = strayCursor;
            entity.y = strayAxis;
            strayCursor += entity.width + gap;
        } else {
            entity.x = strayAxis;
            entity.y = strayCursor;
            strayCursor += entity.height + gap;
        }
        placedEntities.insert(entityId);
    }

    PlaceUnplacedRelationships();
    ApplyAttributeSatelliteLayout();
}

// Two entities left and right, the diamond centred, attributes fanned out
// (image 3).
void UltraCanvasERDiagram::ApplySymmetricLayout() {
    if (entityOrder.size() < 2) {
        ApplySpineLayout(true);
        return;
    }

    double width = static_cast<double>(finalBounds.width);
    double height = static_cast<double>(finalBounds.height);
    if (width < 100.0) width = 900.0;
    if (height < 100.0) height = 620.0;

    double midY = height / 2.0;
    double span = std::min(width * 0.62, 620.0);
    double leftX = width / 2.0 - span / 2.0;
    double rightX = width / 2.0 + span / 2.0;

    auto& first = entities[entityOrder[0]];
    auto& second = entities[entityOrder[1]];
    first.x = leftX - first.width / 2.0;
    first.y = midY - first.height / 2.0;
    second.x = rightX - second.width / 2.0;
    second.y = midY - second.height / 2.0;

    // Remaining entities stack below the axis.
    double stackY = midY + 220.0;
    for (size_t i = 2; i < entityOrder.size(); ++i) {
        auto& entity = entities[entityOrder[i]];
        entity.x = leftX + static_cast<double>(i - 2) * (entity.width + 90.0);
        entity.y = stackY;
    }

    for (const auto& relId : relationshipOrder) {
        auto& rel = relationships[relId];
        rel.x = width / 2.0 - rel.width / 2.0;
        rel.y = midY - rel.height / 2.0;
        rel.hasPlacement = true;
    }

    // Attributes fan straight up from the left entity and down from the right
    // so the two haloes cannot collide.
    style.attributeSpread = 150.0;
    ApplyAttributeSatelliteLayout();
}

void UltraCanvasERDiagram::PlaceUnplacedRelationships() {
    for (const auto& relId : relationshipOrder) {
        auto& rel = relationships[relId];
        if (rel.hasPlacement) continue;

        double sumX = 0.0, sumY = 0.0;
        int count = 0;
        for (const auto& leg : rel.legs) {
            const auto* entity = GetEntity(leg.entityId);
            if (!entity) continue;
            Point2Dd center = GetEntityCenter(*entity);
            sumX += center.x;
            sumY += center.y;
            ++count;
        }
        if (count == 0) continue;

        double cx = sumX / count;
        double cy = sumY / count;

        // A recursive relationship has both legs on one entity, so the
        // centroid sits on top of it - park the diamond beside it instead.
        if (rel.IsRecursive()) {
            const auto* entity = GetEntity(rel.legs[0].entityId);
            if (entity) cx = entity->x + entity->width + rel.width;
        }
        rel.x = cx - rel.width / 2.0;
        rel.y = cy - rel.height / 2.0;
    }
}

// The satellite fan guarantees an oval never lands on its own entity, but two
// entities standing close together can still interleave their haloes. Push the
// offenders apart, and out of any box or diamond they landed on.
void UltraCanvasERDiagram::ResolveAttributeOverlaps() {
    struct Oval {
        ERDiagramAttribute* attribute;
        Point2Dd ownerCenter;
    };

    std::vector<Oval> ovals;
    for (const auto& entityId : entityOrder) {
        auto& entity = entities[entityId];
        Point2Dd center = GetEntityCenter(entity);
        for (auto& attribute : entity.attributes) {
            if (attribute.hasPlacement || !attribute.visible) continue;
            ovals.push_back({ &attribute, center });
            for (auto& child : attribute.children) {
                ovals.push_back({ &child, center });
            }
        }
    }
    for (const auto& relId : relationshipOrder) {
        auto& rel = relationships[relId];
        Point2Dd center = GetRelationshipCenter(rel);
        for (auto& attribute : rel.attributes) {
            if (attribute.hasPlacement) continue;
            ovals.push_back({ &attribute, center });
        }
    }
    if (ovals.size() < 2) return;

    const double margin = 6.0;
    auto separate = [&](Oval& oval, const Rect2Dd& blocker) {
        Rect2Dd rect(oval.attribute->x, oval.attribute->y,
                     oval.attribute->width, oval.attribute->height);
        double overlapX = std::min(rect.x + rect.width, blocker.x + blocker.width) -
                          std::max(rect.x, blocker.x);
        double overlapY = std::min(rect.y + rect.height, blocker.y + blocker.height) -
                          std::max(rect.y, blocker.y);
        if (overlapX <= 0.0 || overlapY <= 0.0) return false;

        // Push radially away from the owning entity so the spoke stays sane.
        double dx = (rect.x + rect.width / 2.0) - oval.ownerCenter.x;
        double dy = (rect.y + rect.height / 2.0) - oval.ownerCenter.y;
        double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-6) { dx = 0.0; dy = -1.0; len = 1.0; }
        double push = std::min(overlapX, overlapY) + margin;
        oval.attribute->x += (dx / len) * push;
        oval.attribute->y += (dy / len) * push;
        return true;
    };

    for (int pass = 0; pass < 24; ++pass) {
        bool moved = false;

        for (size_t i = 0; i < ovals.size(); ++i) {
            for (size_t j = i + 1; j < ovals.size(); ++j) {
                Rect2Dd other(ovals[j].attribute->x, ovals[j].attribute->y,
                              ovals[j].attribute->width, ovals[j].attribute->height);
                if (separate(ovals[i], other)) moved = true;
            }
        }
        for (auto& oval : ovals) {
            for (const auto& entityId : entityOrder) {
                if (separate(oval, GetEntityRect(entities[entityId]))) moved = true;
            }
            for (const auto& relId : relationshipOrder) {
                if (separate(oval, GetRelationshipRect(relationships[relId]))) moved = true;
            }
        }
        if (!moved) break;
    }
}

void UltraCanvasERDiagram::ResolveEntityOverlaps() {
    for (int pass = 0; pass < 30; ++pass) {
        bool moved = false;
        for (size_t i = 0; i < entityOrder.size(); ++i) {
            for (size_t j = i + 1; j < entityOrder.size(); ++j) {
                auto& a = entities[entityOrder[i]];
                auto& b = entities[entityOrder[j]];
                Rect2Dd ra = GetEntityRect(a);
                Rect2Dd rb = GetEntityRect(b);
                double overlapX = std::min(ra.x + ra.width, rb.x + rb.width) -
                                  std::max(ra.x, rb.x);
                double overlapY = std::min(ra.y + ra.height, rb.y + rb.height) -
                                  std::max(ra.y, rb.y);
                if (overlapX <= 0.0 || overlapY <= 0.0) continue;

                if (overlapX < overlapY) {
                    double shift = (overlapX / 2.0) + 4.0;
                    if (ra.x < rb.x) { a.x -= shift; b.x += shift; }
                    else             { a.x += shift; b.x -= shift; }
                } else {
                    double shift = (overlapY / 2.0) + 4.0;
                    if (ra.y < rb.y) { a.y -= shift; b.y += shift; }
                    else             { a.y += shift; b.y -= shift; }
                }
                moved = true;
            }
        }
        if (!moved) break;
    }
}

void UltraCanvasERDiagram::ResolveAnnotationPlacement() {
    for (const auto& annId : annotationOrder) {
        auto& annotation = annotations[annId];

        Point2Dd anchor(0.0, 0.0);
        double anchorHalfW = 0.0;
        double anchorHalfH = 0.0;
        bool found = false;

        size_t dot = annotation.anchorRef.find('.');
        if (dot != std::string::npos) {
            std::string entityId = annotation.anchorRef.substr(0, dot);
            std::string attributeId = annotation.anchorRef.substr(dot + 1);
            auto* attribute = GetAttribute(entityId, attributeId);
            if (attribute) {
                anchor = Point2Dd(attribute->x + attribute->width / 2.0,
                                  attribute->y + attribute->height / 2.0);
                anchorHalfW = attribute->width / 2.0;
                anchorHalfH = attribute->height / 2.0;
                found = true;
            }
        }
        if (!found) {
            if (const auto* entity = GetEntity(annotation.anchorRef)) {
                anchor = GetEntityCenter(*entity);
                anchorHalfW = entity->width / 2.0;
                anchorHalfH = entity->height / 2.0;
                found = true;
            } else if (const auto* rel = GetRelationship(annotation.anchorRef)) {
                anchor = GetRelationshipCenter(*rel);
                anchorHalfW = rel->width / 2.0;
                anchorHalfH = rel->height / 2.0;
                found = true;
            }
        }
        if (!found) continue;

        int textWidth = 0, textHeight = 0;
        EstimateTextSize(annotation.text, annotation.fontSize, false, textWidth, textHeight);
        double w = (annotation.width > 0.0) ? annotation.width
                                            : static_cast<double>(textWidth) + 8.0;
        double h = (annotation.height > 0.0) ? annotation.height
                                             : static_cast<double>(textHeight) + 4.0;

        switch (annotation.side) {
            case ERAnnotationSide::Left:
                annotation.x = anchor.x - anchorHalfW - annotation.distance - w;
                annotation.y = anchor.y - h / 2.0;
                break;
            case ERAnnotationSide::Right:
                annotation.x = anchor.x + anchorHalfW + annotation.distance;
                annotation.y = anchor.y - h / 2.0;
                break;
            case ERAnnotationSide::Top:
                annotation.x = anchor.x - w / 2.0;
                annotation.y = anchor.y - anchorHalfH - annotation.distance - h;
                break;
            case ERAnnotationSide::Bottom:
                annotation.x = anchor.x - w / 2.0;
                annotation.y = anchor.y + anchorHalfH + annotation.distance;
                break;
        }
        annotation.width = w;
        annotation.height = h;

        // The callout is placed relative to its anchor, which says nothing
        // about what else is sitting there - an attribute halo very often is.
        // Slide it further out along its own side until it is clear.
        double stepX = 0.0, stepY = 0.0;
        switch (annotation.side) {
            case ERAnnotationSide::Left:   stepX = -16.0; break;
            case ERAnnotationSide::Right:  stepX = 16.0;  break;
            case ERAnnotationSide::Top:    stepY = -16.0; break;
            case ERAnnotationSide::Bottom: stepY = 16.0;  break;
        }

        auto overlapsSomething = [&]() {
            Rect2Dd box(annotation.x - 4.0, annotation.y - 3.0, w + 8.0, h + 6.0);
            auto hits = [&](const Rect2Dd& other) {
                return box.x < other.x + other.width && other.x < box.x + box.width &&
                       box.y < other.y + other.height && other.y < box.y + box.height;
            };
            for (const auto& entityId : entityOrder) {
                const auto& entity = entities[entityId];
                if (hits(GetEntityRect(entity))) return true;
                if (notation == ERNotation::CrowsFoot) continue;
                for (const auto& attribute : entity.attributes) {
                    if (!attribute.visible) continue;
                    if (hits(Rect2Dd(attribute.x, attribute.y,
                                     attribute.width, attribute.height))) return true;
                }
            }
            if (notation != ERNotation::CrowsFoot) {
                for (const auto& relId : relationshipOrder) {
                    if (hits(GetRelationshipRect(relationships[relId]))) return true;
                }
            }
            for (const auto& otherId : annotationOrder) {
                if (otherId == annId) continue;
                const auto& other = annotations[otherId];
                if (other.width <= 0.0) continue;
                if (hits(Rect2Dd(other.x, other.y, other.width, other.height))) return true;
            }
            return false;
        };

        for (int attempt = 0; attempt < 12 && overlapsSomething(); ++attempt) {
            annotation.x += stepX;
            annotation.y += stepY;
        }
    }
}

std::vector<std::string> UltraCanvasERDiagram::GetRelationshipIdsForEntity(
        const std::string& entityId) const {
    std::vector<std::string> result;
    for (const auto& relId : relationshipOrder) {
        auto it = relationships.find(relId);
        if (it == relationships.end()) continue;
        for (const auto& leg : it->second.legs) {
            if (leg.entityId == entityId) { result.push_back(relId); break; }
        }
    }
    return result;
}

// The direction opposite the average bearing of the entity's relationships -
// so ovals never sit on top of the connectors.
double UltraCanvasERDiagram::PreferredAttributeDirection(const ERDiagramEntity& entity) const {
    Point2Dd center = GetEntityCenter(entity);
    double sumX = 0.0, sumY = 0.0;
    int count = 0;

    for (const auto& relId : GetRelationshipIdsForEntity(entity.id)) {
        auto it = relationships.find(relId);
        if (it == relationships.end()) continue;
        Point2Dd relCenter = GetRelationshipCenter(it->second);
        double dx = relCenter.x - center.x;
        double dy = relCenter.y - center.y;
        double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-6) continue;
        sumX += dx / len;
        sumY += dy / len;
        ++count;
    }
    if (count == 0 || (std::abs(sumX) < 1e-6 && std::abs(sumY) < 1e-6)) {
        return -ER_PI / 2.0;   // Straight up by default
    }
    return std::atan2(-sumY, -sumX);
}

void UltraCanvasERDiagram::SetSnapToGrid(bool enabled) { snapEnabled = enabled; }

void UltraCanvasERDiagram::SetSnapGrid(double gridX, double gridY) {
    snapX = std::max(1.0, gridX);
    snapY = std::max(1.0, gridY);
}

// =============================================================================
// STYLING & THEME
// =============================================================================

void UltraCanvasERDiagram::SetTheme(ERDiagramTheme theme) {
    currentTheme = theme;
    ApplyThemeColors();
    RequestRedraw();
}

void UltraCanvasERDiagram::ApplyThemeColors() {
    ERDiagramRolePalette palette;

    switch (currentTheme) {
        case ERDiagramTheme::Default:
            break;   // Struct defaults: teal entities, navy diamonds, crimson ovals

        case ERDiagramTheme::Professional:
            palette.entityFill        = Color(70, 95, 130, 255);
            palette.entityBorder      = Color(45, 65, 95, 255);
            palette.weakEntityFill    = Color(88, 115, 150, 255);
            palette.weakEntityBorder  = Color(45, 65, 95, 255);
            palette.relationshipFill  = Color(120, 135, 160, 255);
            palette.relationshipBorder = Color(80, 95, 120, 255);
            palette.attributeFill     = Color(158, 175, 200, 255);
            palette.attributeBorder   = Color(105, 125, 155, 255);
            palette.attributeText     = Color(25, 35, 50, 255);
            palette.keyAttributeFill  = Color(120, 140, 175, 255);
            palette.derivedAttributeFill = Color(196, 208, 224, 255);
            palette.multiValuedAttributeFill = Color(140, 160, 190, 255);
            palette.connectorColor    = Color(95, 105, 120, 255);
            style.backgroundColor     = Color(250, 251, 252, 255);
            palette.rowFill = Color(246, 248, 251, 255);
            palette.rowStripeFill = Color(233, 238, 245, 255);
            palette.rowHighlightFill = Color(150, 178, 214, 255);
            palette.rowDivider = Color(120, 136, 160, 255);
            palette.rowText = Color(42, 52, 68, 255);
            palette.rowKeyText = Color(24, 34, 50, 255);
            break;

        case ERDiagramTheme::Colorful:
            palette.entityFill        = Color(232, 130, 40, 255);
            palette.entityBorder      = Color(180, 95, 20, 255);
            palette.weakEntityFill    = Color(240, 160, 70, 255);
            palette.weakEntityBorder  = Color(180, 95, 20, 255);
            palette.relationshipFill  = Color(35, 175, 175, 255);
            palette.relationshipBorder = Color(20, 125, 125, 255);
            palette.attributeFill     = Color(240, 240, 245, 255);
            palette.attributeBorder   = Color(150, 155, 165, 255);
            palette.attributeText     = Color(35, 40, 50, 255);
            palette.keyAttributeFill  = Color(255, 235, 190, 255);
            palette.derivedAttributeFill = Color(248, 248, 250, 255);
            palette.multiValuedAttributeFill = Color(235, 242, 250, 255);
            palette.connectorColor    = Color(90, 95, 105, 255);
            style.backgroundColor     = Color(253, 253, 254, 255);
            palette.rowFill = Color(255, 252, 246, 255);
            palette.rowStripeFill = Color(252, 240, 226, 255);
            palette.rowHighlightFill = Color(245, 196, 130, 255);
            palette.rowDivider = Color(196, 150, 100, 255);
            palette.rowText = Color(60, 45, 30, 255);
            palette.rowKeyText = Color(120, 62, 12, 255);
            break;

        case ERDiagramTheme::Pastel:
            palette.entityFill        = Color(126, 148, 194, 255);
            palette.entityBorder      = Color(96, 118, 164, 255);
            palette.weakEntityFill    = Color(150, 170, 210, 255);
            palette.weakEntityBorder  = Color(96, 118, 164, 255);
            palette.relationshipFill  = Color(226, 132, 148, 255);
            palette.relationshipBorder = Color(190, 100, 118, 255);
            palette.attributeFill     = Color(244, 196, 206, 255);
            palette.attributeBorder   = Color(214, 158, 172, 255);
            palette.attributeText     = Color(70, 50, 58, 255);
            palette.keyAttributeFill  = Color(238, 172, 188, 255);
            palette.derivedAttributeFill = Color(250, 224, 231, 255);
            palette.multiValuedAttributeFill = Color(241, 186, 199, 255);
            palette.connectorColor    = Color(140, 145, 158, 255);
            style.backgroundColor     = Color(255, 255, 255, 255);
            style.roundedEntities     = true;
            palette.rowFill = Color(253, 250, 251, 255);
            palette.rowStripeFill = Color(248, 238, 242, 255);
            palette.rowHighlightFill = Color(238, 186, 200, 255);
            palette.rowDivider = Color(206, 172, 184, 255);
            palette.rowText = Color(66, 52, 58, 255);
            palette.rowKeyText = Color(140, 70, 88, 255);
            break;

        case ERDiagramTheme::Dark:
            palette.entityFill        = Color(52, 62, 82, 255);
            palette.entityBorder      = Color(96, 116, 150, 255);
            palette.entityText        = Color(232, 236, 244, 255);
            palette.weakEntityFill    = Color(62, 74, 96, 255);
            palette.weakEntityBorder  = Color(96, 116, 150, 255);
            palette.weakEntityText    = Color(232, 236, 244, 255);
            palette.relationshipFill  = Color(70, 92, 128, 255);
            palette.relationshipBorder = Color(120, 148, 190, 255);
            palette.relationshipText  = Color(235, 240, 248, 255);
            palette.attributeFill     = Color(58, 68, 88, 255);
            palette.attributeBorder   = Color(104, 122, 154, 255);
            palette.attributeText     = Color(224, 230, 240, 255);
            palette.keyAttributeFill  = Color(78, 92, 120, 255);
            palette.derivedAttributeFill = Color(50, 58, 76, 255);
            palette.multiValuedAttributeFill = Color(66, 78, 100, 255);
            palette.connectorColor    = Color(140, 152, 175, 255);
            palette.cardinalityText   = Color(198, 208, 224, 255);
            palette.roleText          = Color(150, 162, 182, 255);
            style.backgroundColor     = Color(28, 32, 42, 255);
            style.gridColor           = Color(42, 48, 60, 255);
            palette.rowFill = Color(38, 44, 58, 255);
            palette.rowStripeFill = Color(45, 52, 68, 255);
            palette.rowHighlightFill = Color(74, 104, 148, 255);
            palette.rowDivider = Color(104, 122, 154, 255);
            palette.rowText = Color(214, 222, 236, 255);
            palette.rowKeyText = Color(244, 248, 255, 255);
            break;

        case ERDiagramTheme::Blueprint:
            palette.entityFill        = Color(18, 52, 96, 255);
            palette.entityBorder      = Color(150, 200, 255, 255);
            palette.entityText        = Color(228, 240, 255, 255);
            palette.weakEntityFill    = Color(24, 64, 112, 255);
            palette.weakEntityBorder  = Color(150, 200, 255, 255);
            palette.weakEntityText    = Color(228, 240, 255, 255);
            palette.relationshipFill  = Color(18, 52, 96, 255);
            palette.relationshipBorder = Color(150, 200, 255, 255);
            palette.relationshipText  = Color(228, 240, 255, 255);
            palette.attributeFill     = Color(18, 52, 96, 255);
            palette.attributeBorder   = Color(120, 175, 235, 255);
            palette.attributeText     = Color(214, 232, 252, 255);
            palette.keyAttributeFill  = Color(24, 68, 118, 255);
            palette.derivedAttributeFill = Color(16, 46, 86, 255);
            palette.multiValuedAttributeFill = Color(20, 58, 104, 255);
            palette.connectorColor    = Color(150, 200, 255, 255);
            palette.cardinalityText   = Color(200, 226, 252, 255);
            palette.roleText          = Color(160, 195, 235, 255);
            style.backgroundColor     = Color(12, 38, 72, 255);
            style.gridColor           = Color(24, 60, 104, 255);
            style.showGrid            = true;
            palette.rowFill = Color(14, 42, 80, 255);
            palette.rowStripeFill = Color(18, 50, 92, 255);
            palette.rowHighlightFill = Color(38, 92, 150, 255);
            palette.rowDivider = Color(120, 175, 235, 255);
            palette.rowText = Color(206, 228, 250, 255);
            palette.rowKeyText = Color(238, 246, 255, 255);
            break;

        case ERDiagramTheme::Minimal:
            palette.entityFill        = Color(255, 255, 255, 255);
            palette.entityBorder      = Color(60, 62, 68, 255);
            palette.entityText        = Color(30, 32, 38, 255);
            palette.weakEntityFill    = Color(255, 255, 255, 255);
            palette.weakEntityBorder  = Color(60, 62, 68, 255);
            palette.weakEntityText    = Color(30, 32, 38, 255);
            palette.relationshipFill  = Color(255, 255, 255, 255);
            palette.relationshipBorder = Color(60, 62, 68, 255);
            palette.relationshipText  = Color(30, 32, 38, 255);
            palette.attributeFill     = Color(255, 255, 255, 255);
            palette.attributeBorder   = Color(120, 122, 128, 255);
            palette.attributeText     = Color(30, 32, 38, 255);
            palette.keyAttributeFill  = Color(248, 248, 250, 255);
            palette.derivedAttributeFill = Color(255, 255, 255, 255);
            palette.multiValuedAttributeFill = Color(255, 255, 255, 255);
            palette.connectorColor    = Color(60, 62, 68, 255);
            style.backgroundColor     = Color(255, 255, 255, 255);
            palette.rowFill = Color(255, 255, 255, 255);
            palette.rowStripeFill = Color(244, 244, 246, 255);
            palette.rowHighlightFill = Color(224, 226, 232, 255);
            palette.rowDivider = Color(60, 62, 68, 255);
            palette.rowText = Color(40, 42, 48, 255);
            palette.rowKeyText = Color(20, 22, 28, 255);
            break;

        case ERDiagramTheme::Print:
            palette.entityFill        = Color(255, 255, 255, 255);
            palette.entityBorder      = Color(0, 0, 0, 255);
            palette.entityText        = Color(0, 0, 0, 255);
            palette.weakEntityFill    = Color(255, 255, 255, 255);
            palette.weakEntityBorder  = Color(0, 0, 0, 255);
            palette.weakEntityText    = Color(0, 0, 0, 255);
            palette.relationshipFill  = Color(255, 255, 255, 255);
            palette.relationshipBorder = Color(0, 0, 0, 255);
            palette.relationshipText  = Color(0, 0, 0, 255);
            palette.attributeFill     = Color(255, 255, 255, 255);
            palette.attributeBorder   = Color(0, 0, 0, 255);
            palette.attributeText     = Color(0, 0, 0, 255);
            palette.keyAttributeFill  = Color(240, 240, 240, 255);
            palette.derivedAttributeFill = Color(255, 255, 255, 255);
            palette.multiValuedAttributeFill = Color(255, 255, 255, 255);
            palette.connectorColor    = Color(0, 0, 0, 255);
            palette.cardinalityText   = Color(0, 0, 0, 255);
            palette.roleText          = Color(0, 0, 0, 255);
            style.backgroundColor     = Color(255, 255, 255, 255);
            style.showGrid            = false;
            palette.rowFill = Color(255, 255, 255, 255);
            palette.rowStripeFill = Color(240, 240, 240, 255);
            palette.rowHighlightFill = Color(216, 216, 216, 255);
            palette.rowDivider = Color(0, 0, 0, 255);
            palette.rowText = Color(0, 0, 0, 255);
            palette.rowKeyText = Color(0, 0, 0, 255);
            break;
    }
    style.palette = palette;

    // The title band is part of the canvas, so it follows the theme unless the
    // caller has taken it over through SetTitleConfig.
    if (!titleColorsCustomized) {
        bool darkCanvas = (style.backgroundColor.r + style.backgroundColor.g +
                           style.backgroundColor.b) < 300;
        titleConfig.backgroundColor = darkCanvas
                ? Color(style.backgroundColor.r, style.backgroundColor.g,
                        style.backgroundColor.b, 235)
                : Color(255, 255, 255, 235);
        titleConfig.textColor = darkCanvas ? Color(233, 238, 248, 255)
                                           : Color(35, 40, 50, 255);
        titleConfig.subtitleColor = darkCanvas ? Color(154, 166, 186, 255)
                                               : Color(110, 115, 125, 255);
    }
}

void UltraCanvasERDiagram::SetStyle(const ERDiagramStyle& newStyle) {
    style = newStyle;
    RequestRedraw();
}

void UltraCanvasERDiagram::SetRolePalette(const ERDiagramRolePalette& palette) {
    style.palette = palette;
    RequestRedraw();
}

void UltraCanvasERDiagram::SetBackgroundColor(const Color& color) {
    style.backgroundColor = color;
    RequestRedraw();
}

void UltraCanvasERDiagram::SetGridVisible(bool visible, double spacing) {
    style.showGrid = visible;
    if (spacing > 0.0) style.gridSpacing = spacing;
    RequestRedraw();
}

void UltraCanvasERDiagram::SetFontFamily(const std::string& fontFamily) {
    style.fontFamily = fontFamily;
    RequestRedraw();
}

void UltraCanvasERDiagram::SetFontSize(double size) {
    style.baseFontSize = size;
    RequestRedraw();
}

void UltraCanvasERDiagram::SetConnectorStyle(ERConnectorStyle newStyle) {
    connectorStyle = newStyle;
    RequestRedraw();
}

void UltraCanvasERDiagram::SetRowMarkerSide(ERRowMarkerSide side) {
    style.rowMarkerSide = side;
    RequestRedraw();
}

void UltraCanvasERDiagram::SetStripeRows(bool stripe) {
    style.stripeRows = stripe;
    RequestRedraw();
}

void UltraCanvasERDiagram::SetKeyCompartment(bool enabled) {
    style.keyCompartment = enabled;
    RequestRedraw();
}

void UltraCanvasERDiagram::SetUnderlineKeyRows(bool underline) {
    style.underlineKeyRows = underline;
    RequestRedraw();
}

void UltraCanvasERDiagram::SetNumberForeignKeys(bool numbered) {
    style.numberForeignKeys = numbered;
    RequestRedraw();
}

void UltraCanvasERDiagram::SetAttributeHighlighted(const std::string& entityId,
                                                    const std::string& attributeId,
                                                    bool highlighted) {
    auto* attribute = GetAttribute(entityId, attributeId);
    if (!attribute) return;
    attribute->highlighted = highlighted;
    RequestRedraw();
}

Color UltraCanvasERDiagram::GetRoleFill(ERNodeRole role) const {
    const auto& p = style.palette;
    switch (role) {
        case ERNodeRole::Entity:                  return p.entityFill;
        case ERNodeRole::WeakEntity:              return p.weakEntityFill;
        case ERNodeRole::AssociativeEntity:       return p.entityFill;
        case ERNodeRole::Relationship:            return p.relationshipFill;
        case ERNodeRole::IdentifyingRelationship: return p.relationshipFill;
        case ERNodeRole::Attribute:               return p.attributeFill;
        case ERNodeRole::KeyAttribute:            return p.keyAttributeFill;
        case ERNodeRole::DerivedAttribute:        return p.derivedAttributeFill;
        case ERNodeRole::MultiValuedAttribute:    return p.multiValuedAttributeFill;
        case ERNodeRole::AttributeRow:            return p.rowFill;
    }
    return p.entityFill;
}

Color UltraCanvasERDiagram::GetRoleBorder(ERNodeRole role) const {
    const auto& p = style.palette;
    switch (role) {
        case ERNodeRole::Entity:
        case ERNodeRole::AssociativeEntity:       return p.entityBorder;
        case ERNodeRole::WeakEntity:              return p.weakEntityBorder;
        case ERNodeRole::Relationship:
        case ERNodeRole::IdentifyingRelationship: return p.relationshipBorder;
        case ERNodeRole::Attribute:
        case ERNodeRole::KeyAttribute:
        case ERNodeRole::DerivedAttribute:
        case ERNodeRole::MultiValuedAttribute:
        case ERNodeRole::AttributeRow:            return p.attributeBorder;
    }
    return p.entityBorder;
}

Color UltraCanvasERDiagram::GetRoleText(ERNodeRole role) const {
    const auto& p = style.palette;
    switch (role) {
        case ERNodeRole::Entity:
        case ERNodeRole::AssociativeEntity:       return p.entityText;
        case ERNodeRole::WeakEntity:              return p.weakEntityText;
        case ERNodeRole::Relationship:
        case ERNodeRole::IdentifyingRelationship: return p.relationshipText;
        case ERNodeRole::Attribute:
        case ERNodeRole::KeyAttribute:
        case ERNodeRole::DerivedAttribute:
        case ERNodeRole::MultiValuedAttribute:    return p.attributeText;
        case ERNodeRole::AttributeRow:            return p.rowText;
    }
    return p.entityText;
}

// =============================================================================
// CANVAS CHROME
// =============================================================================

void UltraCanvasERDiagram::SetTitle(const std::string& text, const std::string& subtitle) {
    titleConfig.text = text;
    titleConfig.subtitle = subtitle;
    titleConfig.visible = !text.empty();
    RequestRedraw();
}

void UltraCanvasERDiagram::SetTitleConfig(const ERDiagramTitleConfig& config) {
    titleConfig = config;
    titleColorsCustomized = true;
    RequestRedraw();
}

void UltraCanvasERDiagram::SetLegendVisible(bool visible, ERPanelPosition position) {
    legendConfig.visible = visible;
    legendConfig.position = position;
    RequestRedraw();
}

void UltraCanvasERDiagram::SetLegendConfig(const ERDiagramLegendConfig& config) {
    legendConfig = config;
    RequestRedraw();
}

void UltraCanvasERDiagram::SetControlsVisible(bool visible) {
    controlsConfig.visible = visible;
    RequestRedraw();
}

void UltraCanvasERDiagram::SetControlsPosition(ERPanelPosition position) {
    controlsConfig.position = position;
    RequestRedraw();
}

void UltraCanvasERDiagram::SetControlsConfig(const ERDiagramControlsConfig& config) {
    controlsConfig = config;
    RequestRedraw();
}

// =============================================================================
// VIEWPORT
// =============================================================================

Point2Dd UltraCanvasERDiagram::ScreenToWorld(const Point2Di& screenPos) const {
    return Point2Dd((static_cast<double>(screenPos.x) - panOffset.x) / zoomLevel,
                    (static_cast<double>(screenPos.y) - panOffset.y) / zoomLevel);
}

Point2Di UltraCanvasERDiagram::WorldToScreen(const Point2Dd& worldPos) const {
    return Point2Di(static_cast<int>(worldPos.x * zoomLevel + panOffset.x),
                    static_cast<int>(worldPos.y * zoomLevel + panOffset.y));
}

Point2Dd UltraCanvasERDiagram::SnapPoint(const Point2Dd& p) const {
    if (!snapEnabled) return p;
    return Point2Dd(std::round(p.x / snapX) * snapX,
                    std::round(p.y / snapY) * snapY);
}

void UltraCanvasERDiagram::ClampZoom() {
    zoomLevel = std::clamp(zoomLevel, minZoom, maxZoom);
}

void UltraCanvasERDiagram::SetZoomLevel(double zoom) {
    zoomLevel = zoom;
    ClampZoom();
    NotifyViewportChange();
    RequestRedraw();
}

void UltraCanvasERDiagram::SetPanOffset(double x, double y) {
    panOffset.x = x;
    panOffset.y = y;
    NotifyViewportChange();
    RequestRedraw();
}

void UltraCanvasERDiagram::ZoomIn(double factor) { SetZoomLevel(zoomLevel * factor); }
void UltraCanvasERDiagram::ZoomOut(double factor) { SetZoomLevel(zoomLevel / factor); }

Rect2Dd UltraCanvasERDiagram::GetContentBounds() const {
    bool any = false;
    double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;

    auto include = [&](double x, double y, double w, double h) {
        if (!any) { minX = x; minY = y; maxX = x + w; maxY = y + h; any = true; return; }
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x + w);
        maxY = std::max(maxY, y + h);
    };

    for (const auto& entry : entities) {
        const auto& entity = entry.second;
        Rect2Dd rect = GetEntityRect(entity);
        include(rect.x, rect.y, rect.width, rect.height);
        if (notation != ERNotation::CrowsFoot) {
            for (const auto& attribute : entity.attributes) {
                if (!attribute.visible) continue;
                include(attribute.x, attribute.y, attribute.width, attribute.height);
                for (const auto& child : attribute.children) {
                    include(child.x, child.y, child.width, child.height);
                }
            }
        }
    }
    for (const auto& entry : relationships) {
        const auto& rel = entry.second;
        if (notation == ERNotation::CrowsFoot) continue;
        Rect2Dd rect = GetRelationshipRect(rel);
        include(rect.x, rect.y, rect.width, rect.height);
        for (const auto& attribute : rel.attributes) {
            include(attribute.x, attribute.y, attribute.width, attribute.height);
        }
    }
    for (const auto& entry : annotations) {
        const auto& annotation = entry.second;
        include(annotation.x, annotation.y,
                std::max(annotation.width, 10.0), std::max(annotation.height, 10.0));
    }
    if (!any) return Rect2Dd(0.0, 0.0, 0.0, 0.0);
    return Rect2Dd(minX, minY, maxX - minX, maxY - minY);
}

void UltraCanvasERDiagram::FitView(double padding) {
    Rect2Dd content = GetContentBounds();
    if (content.width <= 0.0 || content.height <= 0.0) return;

    double viewW = static_cast<double>(finalBounds.width);
    double viewH = static_cast<double>(finalBounds.height);
    if (viewW < 10.0 || viewH < 10.0) return;

    double topInset = titleConfig.visible ? titleConfig.height : 0.0;
    viewH -= topInset;
    if (viewH < 10.0) return;

    double scaleX = (viewW - 2.0 * padding) / content.width;
    double scaleY = (viewH - 2.0 * padding) / content.height;
    zoomLevel = std::min(scaleX, scaleY);
    ClampZoom();

    double contentCenterX = content.x + content.width / 2.0;
    double contentCenterY = content.y + content.height / 2.0;
    panOffset.x = viewW / 2.0 - contentCenterX * zoomLevel;
    panOffset.y = topInset + viewH / 2.0 - contentCenterY * zoomLevel;

    NotifyViewportChange();
    RequestRedraw();
}

void UltraCanvasERDiagram::CenterOn(double worldX, double worldY) {
    panOffset.x = static_cast<double>(finalBounds.width) / 2.0 - worldX * zoomLevel;
    panOffset.y = static_cast<double>(finalBounds.height) / 2.0 - worldY * zoomLevel;
    NotifyViewportChange();
    RequestRedraw();
}

// =============================================================================
// SELECTION
// =============================================================================

void UltraCanvasERDiagram::SelectEntity(const std::string& id, bool addToSelection) {
    if (!addToSelection) DeselectAll();
    auto* entity = GetEntity(id);
    if (entity && entity->selectable) entity->isSelected = true;
    NotifySelectionChange();
    RequestRedraw();
}

void UltraCanvasERDiagram::SelectRelationship(const std::string& id, bool addToSelection) {
    if (!addToSelection) DeselectAll();
    auto* rel = GetRelationship(id);
    if (rel && rel->selectable) rel->isSelected = true;
    NotifySelectionChange();
    RequestRedraw();
}

void UltraCanvasERDiagram::SelectAll() {
    for (auto& entry : entities) {
        if (entry.second.selectable) entry.second.isSelected = true;
    }
    for (auto& entry : relationships) {
        if (entry.second.selectable) entry.second.isSelected = true;
    }
    NotifySelectionChange();
    RequestRedraw();
}

void UltraCanvasERDiagram::DeselectAll() {
    for (auto& entry : entities) {
        entry.second.isSelected = false;
        for (auto& attribute : entry.second.attributes) attribute.isSelected = false;
    }
    for (auto& entry : relationships) entry.second.isSelected = false;
    NotifySelectionChange();
    RequestRedraw();
}

void UltraCanvasERDiagram::Clear() {
    entities.clear();
    entityOrder.clear();
    relationships.clear();
    relationshipOrder.clear();
    annotations.clear();
    annotationOrder.clear();
    hoveredNode = ERHitResult();
    draggedNode = ERHitResult();
    NotifyModelChanged();
    RequestRedraw();
}

std::vector<std::string> UltraCanvasERDiagram::GetSelectedEntityIds() const {
    std::vector<std::string> result;
    for (const auto& id : entityOrder) {
        auto it = entities.find(id);
        if (it != entities.end() && it->second.isSelected) result.push_back(id);
    }
    return result;
}

std::vector<std::string> UltraCanvasERDiagram::GetSelectedRelationshipIds() const {
    std::vector<std::string> result;
    for (const auto& id : relationshipOrder) {
        auto it = relationships.find(id);
        if (it != relationships.end() && it->second.isSelected) result.push_back(id);
    }
    return result;
}

bool UltraCanvasERDiagram::IsEntitySelected(const std::string& id) const {
    const auto* entity = GetEntity(id);
    return entity && entity->isSelected;
}

// =============================================================================
// GEOMETRY
// =============================================================================

Rect2Dd UltraCanvasERDiagram::GetEntityRect(const ERDiagramEntity& entity) const {
    double height = (notation == ERNotation::CrowsFoot)
                    ? MeasuredEntityHeight(entity)
                    : entity.height;
    return Rect2Dd(entity.x, entity.y, entity.width, height);
}

Rect2Dd UltraCanvasERDiagram::GetRelationshipRect(const ERDiagramRelationship& rel) const {
    return Rect2Dd(rel.x, rel.y, rel.width, rel.height);
}

Point2Dd UltraCanvasERDiagram::GetEntityCenter(const ERDiagramEntity& entity) const {
    Rect2Dd rect = GetEntityRect(entity);
    return Point2Dd(rect.x + rect.width / 2.0, rect.y + rect.height / 2.0);
}

Point2Dd UltraCanvasERDiagram::GetRelationshipCenter(const ERDiagramRelationship& rel) const {
    return Point2Dd(rel.x + rel.width / 2.0, rel.y + rel.height / 2.0);
}

Point2Dd UltraCanvasERDiagram::GetEntityBorderPoint(const ERDiagramEntity& entity,
                                                     const Point2Dd& towards) const {
    return RectBorderPoint(GetEntityRect(entity), towards);
}

Point2Dd UltraCanvasERDiagram::GetDiamondBorderPoint(const ERDiagramRelationship& rel,
                                                      const Point2Dd& towards) const {
    return DiamondBorderPoint(GetRelationshipRect(rel), towards);
}

double UltraCanvasERDiagram::MeasuredEntityHeight(const ERDiagramEntity& entity) const {
    if (entity.collapsed) return ER_HEADER_HEIGHT + 4.0;
    size_t rows = entity.attributes.size();
    double height = ER_HEADER_HEIGHT + static_cast<double>(rows) * ER_ROW_HEIGHT + 8.0;

    // The key compartment costs one rule plus its breathing room, but only when
    // there is something on both sides of it to separate.
    if (style.keyCompartment) {
        size_t keyCount = 0;
        for (const auto& attribute : entity.attributes) {
            if (attribute.keyRole == ERKeyRole::Primary ||
                attribute.keyRole == ERKeyRole::Partial) {
                ++keyCount;
            }
        }
        if (keyCount > 0 && keyCount < rows) height += ER_KEY_DIVIDER_GAP;
    }
    return height;
}

// Display order for the crow's-foot projection. With keyCompartment on, key
// columns are hoisted above the divider (the IDEF1X convention); the model
// itself keeps whatever order the caller supplied.
std::vector<const ERDiagramAttribute*> UltraCanvasERDiagram::RowOrder(
        const ERDiagramEntity& entity, size_t& outKeyCount) const {
    std::vector<const ERDiagramAttribute*> order;
    order.reserve(entity.attributes.size());
    outKeyCount = 0;

    if (!style.keyCompartment) {
        for (const auto& attribute : entity.attributes) order.push_back(&attribute);
        return order;
    }
    for (const auto& attribute : entity.attributes) {
        if (attribute.keyRole == ERKeyRole::Primary ||
            attribute.keyRole == ERKeyRole::Partial) {
            order.push_back(&attribute);
            ++outKeyCount;
        }
    }
    for (const auto& attribute : entity.attributes) {
        if (attribute.keyRole != ERKeyRole::Primary &&
            attribute.keyRole != ERKeyRole::Partial) {
            order.push_back(&attribute);
        }
    }
    return order;
}

// PK / FK / UK, optionally numbered so a reader can tell which foreign key a
// column belongs to. An explicit foreignKeyIndex wins; otherwise foreign keys
// are numbered in order of appearance within the entity.
std::string UltraCanvasERDiagram::RowMarkerText(const ERDiagramEntity& entity,
                                                 const ERDiagramAttribute& attribute) const {
    switch (attribute.keyRole) {
        case ERKeyRole::Primary:
        case ERKeyRole::Partial:
            return "PK";
        case ERKeyRole::Unique:
            return "UK";
        case ERKeyRole::NoKey:
            return std::string();
        case ERKeyRole::Foreign:
            break;
    }
    if (!style.numberForeignKeys) return "FK";

    int index = attribute.foreignKeyIndex;
    if (index <= 0) {
        int seen = 0;
        for (const auto& candidate : entity.attributes) {
            if (candidate.keyRole != ERKeyRole::Foreign) continue;
            ++seen;
            if (candidate.id == attribute.id) { index = seen; break; }
        }
    }
    return (index > 0) ? ("FK" + std::to_string(index)) : "FK";
}

// =============================================================================
// ROUTING
// =============================================================================

void UltraCanvasERDiagram::BuildConnectorPath(const Point2Dd& from, const Point2Dd& to,
                                               ERConnectorStyle routeStyle,
                                               std::vector<Point2Dd>& outPath) const {
    outPath.clear();

    switch (routeStyle) {
        case ERConnectorStyle::Straight:
            outPath.push_back(from);
            outPath.push_back(to);
            break;

        case ERConnectorStyle::Orthogonal: {
            // Elbow through the dominant axis first, so trunks stack cleanly.
            double dx = to.x - from.x;
            double dy = to.y - from.y;
            outPath.push_back(from);
            if (std::abs(dx) > std::abs(dy)) {
                double midX = from.x + dx / 2.0;
                outPath.push_back(Point2Dd(midX, from.y));
                outPath.push_back(Point2Dd(midX, to.y));
            } else {
                double midY = from.y + dy / 2.0;
                outPath.push_back(Point2Dd(from.x, midY));
                outPath.push_back(Point2Dd(to.x, midY));
            }
            outPath.push_back(to);
            break;
        }

        case ERConnectorStyle::Bezier: {
            // Flattened cubic - the render context draws polylines, and a
            // flattened curve keeps hit-testing and marker maths uniform.
            double dx = to.x - from.x;
            double dy = to.y - from.y;
            Point2Dd c1(from.x + dx * 0.4, from.y + dy * 0.1);
            Point2Dd c2(from.x + dx * 0.6, from.y + dy * 0.9);
            const int segments = 18;
            for (int i = 0; i <= segments; ++i) {
                double t = static_cast<double>(i) / segments;
                double u = 1.0 - t;
                double x = u * u * u * from.x + 3.0 * u * u * t * c1.x +
                           3.0 * u * t * t * c2.x + t * t * t * to.x;
                double y = u * u * u * from.y + 3.0 * u * u * t * c1.y +
                           3.0 * u * t * t * c2.y + t * t * t * to.y;
                outPath.emplace_back(x, y);
            }
            break;
        }
    }
}

// Both legs of a recursive relationship touch one entity, so the connector has
// to arc out and back (image 5's Tutor_Tutored).
void UltraCanvasERDiagram::BuildSelfLoopPath(const ERDiagramEntity& entity,
                                              const ERDiagramRelationship& rel,
                                              int legIndex,
                                              std::vector<Point2Dd>& outPath) const {
    outPath.clear();
    Rect2Dd rect = GetEntityRect(entity);
    Point2Dd relCenter = GetRelationshipCenter(rel);

    // Leave from the top or the bottom edge depending on which leg this is, so
    // the two legs are visually distinguishable.
    double edgeY = (legIndex == 0) ? rect.y : rect.y + rect.height;
    double edgeX = rect.x + rect.width * ((legIndex == 0) ? 0.68 : 0.85);
    Point2Dd start(edgeX, edgeY);

    double bulge = (legIndex == 0) ? -46.0 : 46.0;
    outPath.push_back(start);
    outPath.push_back(Point2Dd(edgeX, edgeY + bulge));
    outPath.push_back(Point2Dd(relCenter.x, edgeY + bulge));
    outPath.push_back(DiamondBorderPoint(GetRelationshipRect(rel),
                                         Point2Dd(relCenter.x, edgeY + bulge)));
}

// =============================================================================
// MEASUREMENT
// =============================================================================

void UltraCanvasERDiagram::MeasureLabel(const std::string& label, double fontSize,
                                         int& outWidth, int& outHeight) const {
    EstimateTextSize(label, fontSize, true, outWidth, outHeight);
}

void UltraCanvasERDiagram::SuggestEntitySizeForName(const std::string& name,
                                                     double& outWidth,
                                                     double& outHeight) const {
    int textWidth = 0, textHeight = 0;
    EstimateTextSize(name, style.baseFontSize, true, textWidth, textHeight);
    outWidth = std::max(ER_MIN_ENTITY_WIDTH, static_cast<double>(textWidth) + 34.0);
    outHeight = std::max(ER_MIN_ENTITY_HEIGHT, static_cast<double>(textHeight) + 24.0);
}

void UltraCanvasERDiagram::AutoSizeAll() {
    for (const auto& entityId : entityOrder) {
        auto& entity = entities[entityId];
        double w = 0.0, h = 0.0;
        SuggestEntitySizeForName(entity.name, w, h);

        if (notation == ERNotation::CrowsFoot) {
            // Widest row decides the box width. Rows start at x+34 (after the
            // PK/FK marker column) and may carry a trailing " *" NOT NULL mark,
            // so the reserve has to cover both plus a right inset.
            for (const auto& attribute : entity.attributes) {
                std::string row = attribute.name;
                if (showDataTypes && !attribute.dataType.empty()) {
                    row += " : " + attribute.dataType;
                }
                if (!attribute.nullable) row += " *";
                int rw = 0, rh = 0;
                EstimateTextSize(row, style.rowFontSize, true, rw, rh);
                // Name cell + badge column + an inset on each side.
                w = std::max(w, static_cast<double>(rw) +
                                EffectiveMarkerColumnWidth() + 22.0);
            }
            entity.width = w;
            entity.height = MeasuredEntityHeight(entity);
        } else {
            entity.width = w;
            entity.height = h;
        }

        for (auto& attribute : entity.attributes) {
            int aw = 0, ah = 0;
            EstimateTextSize(attribute.name, style.attributeFontSize, false, aw, ah);
            attribute.width = std::max(72.0, static_cast<double>(aw) + 26.0);
            attribute.height = std::max(30.0, static_cast<double>(ah) + 14.0);
            for (auto& child : attribute.children) {
                int cw = 0, ch = 0;
                EstimateTextSize(child.name, style.attributeFontSize, false, cw, ch);
                child.width = std::max(66.0, static_cast<double>(cw) + 24.0);
                child.height = std::max(28.0, static_cast<double>(ch) + 12.0);
            }
        }
    }

    for (const auto& relId : relationshipOrder) {
        auto& rel = relationships[relId];
        int rw = 0, rh = 0;
        EstimateTextSize(rel.name, style.baseFontSize, true, rw, rh);
        // A diamond only offers about half its bounding box to horizontal text.
        rel.width = std::max(96.0, static_cast<double>(rw) * 1.9 + 20.0);
        rel.height = std::max(56.0, static_cast<double>(rh) * 2.6);
    }
    RequestRedraw();
}

// =============================================================================
// CARDINALITY TEXT
// =============================================================================

std::string UltraCanvasERDiagram::CardinalityText(const ERDiagramRelationship& rel,
                                                   size_t legIndex) const {
    if (legIndex >= rel.legs.size()) return std::string();
    const auto& leg = rel.legs[legIndex];

    switch (cardinalityLabels) {
        case ERCardinalityLabels::NoLabels:
            return std::string();

        case ERCardinalityLabels::Letters: {
            if (leg.maxCard == 1) return "1";
            // N for the first many-leg, then M, P, Q - the usual convention
            // for M:N and n-ary relationships.
            static const char* letters[] = { "N", "M", "P", "Q" };
            int manyIndex = 0;
            for (size_t i = 0; i < legIndex; ++i) {
                if (rel.legs[i].maxCard != 1) ++manyIndex;
            }
            if (manyIndex > 3) manyIndex = 3;
            return letters[manyIndex];
        }

        case ERCardinalityLabels::MinMax: {
            std::string maxText = (leg.maxCard == ERCardinalityN)
                                  ? "N" : std::to_string(leg.maxCard);
            return "(" + std::to_string(leg.minCard) + "," + maxText + ")";
        }

        case ERCardinalityLabels::UML: {
            std::string maxText = (leg.maxCard == ERCardinalityN)
                                  ? "*" : std::to_string(leg.maxCard);
            if (leg.minCard == leg.maxCard) return maxText;
            return std::to_string(leg.minCard) + ".." + maxText;
        }
    }
    return std::string();
}

// The explicit per-leg override wins; otherwise derive the glyph from (min,max)
// when a crow's-foot family is active, and fall back to the diagram default.
ERLineEndStyle UltraCanvasERDiagram::ResolveLineEnd(const ERDiagramLeg& leg) const {
    if (leg.lineEnd != ERLineEndStyle::NoMarker) return leg.lineEnd;

    // A marker set the caller chose explicitly is used verbatim, whatever the
    // notation - the whole point of SetLineEndStyle being separate from
    // SetNotation is that a table notation can carry plain arrowheads. Only the
    // crow's-foot family (and the default, unset case under CrowsFoot) derives
    // its glyph from (min,max).
    bool crowFamily = (lineEndStyle == ERLineEndStyle::CrowsFoot ||
                       lineEndStyle == ERLineEndStyle::CircleCrowsFoot ||
                       lineEndStyle == ERLineEndStyle::Tick ||
                       lineEndStyle == ERLineEndStyle::DoubleTick ||
                       lineEndStyle == ERLineEndStyle::Circle);
    if (notation == ERNotation::CrowsFoot &&
        lineEndStyle == ERLineEndStyle::NoMarker) {
        crowFamily = true;
    }
    if (!crowFamily) return lineEndStyle;

    bool many = (leg.maxCard != 1);
    bool optional = (leg.minCard == 0);
    if (many)  return optional ? ERLineEndStyle::CircleCrowsFoot : ERLineEndStyle::CrowsFoot;
    return optional ? ERLineEndStyle::Circle : ERLineEndStyle::DoubleTick;
}

// =============================================================================
// SERIALIZATION
// =============================================================================

std::string UltraCanvasERDiagram::ToJson() const {
    JSONValue root = JSONValue::MakeObject();

    JSONValue viewport = JSONValue::MakeObject();
    viewport.Set("zoom", zoomLevel);
    viewport.Set("panX", panOffset.x);
    viewport.Set("panY", panOffset.y);
    root.Set("viewport", viewport);

    root.Set("notation", std::string(NotationToString(notation)));

    JSONValue entityArray = JSONValue::MakeArray();
    for (const auto& id : entityOrder) {
        auto it = entities.find(id);
        if (it == entities.end()) continue;
        const auto& entity = it->second;

        JSONValue object = JSONValue::MakeObject();
        object.Set("id", entity.id);
        object.Set("name", entity.name);
        if (!entity.alias.empty())   object.Set("alias", entity.alias);
        if (!entity.comment.empty()) object.Set("comment", entity.comment);
        object.Set("kind", std::string(EntityKindToString(entity.kind)));
        object.Set("x", entity.x);
        object.Set("y", entity.y);
        object.Set("width", entity.width);
        object.Set("height", entity.height);
        object.Set("collapsed", entity.collapsed);
        object.Set("pinned", entity.isPinned);
        if (entity.useCustomColors) {
            object.Set("fillColor", JSON::FromColor(entity.fillColor));
            object.Set("borderColor", JSON::FromColor(entity.borderColor));
            object.Set("textColor", JSON::FromColor(entity.textColor));
        }
        object.Set("attributes", AttributeListToJson(entity.attributes));
        entityArray.Append(object);
    }
    root.Set("entities", entityArray);

    JSONValue relationshipArray = JSONValue::MakeArray();
    for (const auto& id : relationshipOrder) {
        auto it = relationships.find(id);
        if (it == relationships.end()) continue;
        const auto& rel = it->second;

        JSONValue object = JSONValue::MakeObject();
        object.Set("id", rel.id);
        object.Set("name", rel.name);
        if (!rel.inverseName.empty()) object.Set("inverseName", rel.inverseName);
        object.Set("kind", std::string(rel.kind == ERRelationshipKind::Identifying
                                       ? "identifying" : "regular"));
        object.Set("x", rel.x);
        object.Set("y", rel.y);
        object.Set("width", rel.width);
        object.Set("height", rel.height);
        object.Set("hasPlacement", rel.hasPlacement);
        if (rel.useCustomColors) {
            object.Set("fillColor", JSON::FromColor(rel.fillColor));
            object.Set("borderColor", JSON::FromColor(rel.borderColor));
            object.Set("textColor", JSON::FromColor(rel.textColor));
        }

        JSONValue legArray = JSONValue::MakeArray();
        for (const auto& leg : rel.legs) {
            JSONValue legObject = JSONValue::MakeObject();
            legObject.Set("entityId", leg.entityId);
            if (!leg.roleName.empty()) legObject.Set("roleName", leg.roleName);
            legObject.Set("minCard", static_cast<int64_t>(leg.minCard));
            legObject.Set("maxCard", static_cast<int64_t>(leg.maxCard));
            legObject.Set("participation",
                          std::string(ParticipationToString(leg.participation)));
            legArray.Append(legObject);
        }
        object.Set("legs", legArray);
        if (!rel.attributes.empty()) {
            object.Set("attributes", AttributeListToJson(rel.attributes));
        }
        relationshipArray.Append(object);
    }
    root.Set("relationships", relationshipArray);

    if (!annotationOrder.empty()) {
        JSONValue annotationArray = JSONValue::MakeArray();
        for (const auto& id : annotationOrder) {
            auto it = annotations.find(id);
            if (it == annotations.end()) continue;
            const auto& annotation = it->second;
            JSONValue object = JSONValue::MakeObject();
            object.Set("id", annotation.id);
            object.Set("text", annotation.text);
            object.Set("anchorRef", annotation.anchorRef);
            object.Set("side", static_cast<int64_t>(annotation.side));
            object.Set("distance", annotation.distance);
            annotationArray.Append(object);
        }
        root.Set("annotations", annotationArray);
    }

    JSONSerializeOptions options;
    options.pretty = true;
    options.indentWidth = 2;
    return JSON::Serialize(root, options);
}

bool UltraCanvasERDiagram::FromJson(const std::string& json) {
    if (json.empty()) return false;

    JSONParseResult parseResult;
    JSONValue root = JSON::Parse(json, &parseResult);
    if (!parseResult.success || !root.IsObject()) return false;

    Clear();

    if (root.Contains("notation")) {
        notation = NotationFromString(root.Get("notation").GetString());
    }

    const JSONValue& entityArray = root.Get("entities");
    if (entityArray.IsArray()) {
        for (const auto& object : entityArray.GetElements()) {
            ERDiagramEntity entity;
            entity.id = object.Get("id").GetString();
            if (entity.id.empty()) continue;
            entity.name = object.Get("name").GetString();
            entity.alias = object.Get("alias").GetString();
            entity.comment = object.Get("comment").GetString();
            entity.kind = EntityKindFromString(object.Get("kind").GetString());
            entity.x = object.Get("x").GetNumber();
            entity.y = object.Get("y").GetNumber();
            entity.width = object.Get("width").GetNumber(140.0);
            entity.height = object.Get("height").GetNumber(56.0);
            entity.collapsed = object.Get("collapsed").GetBoolean(false);
            entity.isPinned = object.Get("pinned").GetBoolean(false);
            if (object.Contains("fillColor")) {
                entity.useCustomColors = true;
                JSON::ToColor(object.Get("fillColor"), entity.fillColor);
                JSON::ToColor(object.Get("borderColor"), entity.borderColor);
                JSON::ToColor(object.Get("textColor"), entity.textColor);
            }
            entity.attributes = AttributeListFromJson(object.Get("attributes"));
            entities[entity.id] = entity;
            entityOrder.push_back(entity.id);
        }
    }

    const JSONValue& relationshipArray = root.Get("relationships");
    if (relationshipArray.IsArray()) {
        for (const auto& object : relationshipArray.GetElements()) {
            ERDiagramRelationship rel;
            rel.id = object.Get("id").GetString();
            if (rel.id.empty()) continue;
            rel.name = object.Get("name").GetString();
            rel.inverseName = object.Get("inverseName").GetString();
            rel.kind = (object.Get("kind").GetString() == "identifying")
                       ? ERRelationshipKind::Identifying : ERRelationshipKind::Regular;
            rel.x = object.Get("x").GetNumber();
            rel.y = object.Get("y").GetNumber();
            rel.width = object.Get("width").GetNumber(110.0);
            rel.height = object.Get("height").GetNumber(62.0);
            rel.hasPlacement = object.Get("hasPlacement").GetBoolean(false);
            if (object.Contains("fillColor")) {
                rel.useCustomColors = true;
                JSON::ToColor(object.Get("fillColor"), rel.fillColor);
                JSON::ToColor(object.Get("borderColor"), rel.borderColor);
                JSON::ToColor(object.Get("textColor"), rel.textColor);
            }

            const JSONValue& legArray = object.Get("legs");
            if (legArray.IsArray()) {
                for (const auto& legObject : legArray.GetElements()) {
                    ERDiagramLeg leg;
                    leg.entityId = legObject.Get("entityId").GetString();
                    leg.roleName = legObject.Get("roleName").GetString();
                    leg.minCard = static_cast<int>(legObject.Get("minCard").GetInteger(1));
                    leg.maxCard = static_cast<int>(
                            legObject.Get("maxCard").GetInteger(ERCardinalityN));
                    leg.participation =
                            ParticipationFromString(legObject.Get("participation").GetString());
                    rel.legs.push_back(leg);
                }
            }
            if (object.Contains("attributes")) {
                rel.attributes = AttributeListFromJson(object.Get("attributes"));
            }
            relationships[rel.id] = rel;
            relationshipOrder.push_back(rel.id);
        }
    }

    const JSONValue& annotationArray = root.Get("annotations");
    if (annotationArray.IsArray()) {
        for (const auto& object : annotationArray.GetElements()) {
            ERDiagramAnnotation annotation;
            annotation.id = object.Get("id").GetString();
            if (annotation.id.empty()) continue;
            annotation.text = object.Get("text").GetString();
            annotation.anchorRef = object.Get("anchorRef").GetString();
            int side = static_cast<int>(object.Get("side").GetInteger(0));
            annotation.side = static_cast<ERAnnotationSide>(std::clamp(side, 0, 3));
            annotation.distance = object.Get("distance").GetNumber(90.0);
            annotations[annotation.id] = annotation;
            annotationOrder.push_back(annotation.id);
        }
    }

    const JSONValue& viewport = root.Get("viewport");
    if (viewport.IsObject()) {
        zoomLevel = viewport.Get("zoom").GetNumber(1.0);
        panOffset.x = viewport.Get("panX").GetNumber(0.0);
        panOffset.y = viewport.Get("panY").GetNumber(0.0);
        ClampZoom();
    }

    PlaceUnplacedRelationships();
    ApplyAttributeSatelliteLayout();
    ResolveAnnotationPlacement();
    NotifyModelChanged();
    RequestRedraw();
    return true;
}

// =============================================================================
// RENDERING
// =============================================================================

void UltraCanvasERDiagram::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    if (!ctx || !IsVisible()) return;

    ctx->SetFillPaint(style.backgroundColor);
    ctx->FillRectangle(GetLocalBounds());

    // World space: every draw call below takes diagram coordinates.
    ctx->PushState();
    ctx->Translate(panOffset.x, panOffset.y);
    ctx->Scale(zoomLevel, zoomLevel);

    if (style.showGrid) RenderGrid(ctx);

    RenderConnectors(ctx);
    if (notation != ERNotation::CrowsFoot) {
        RenderAttributes(ctx);
        RenderRelationships(ctx);
    }
    RenderEntities(ctx);
    RenderAnnotations(ctx);

    if (isSelectingBox) RenderSelectionBox(ctx);

    ctx->PopState();

    // Screen-space chrome
    if (titleConfig.visible)   RenderTitle(ctx);
    if (legendConfig.visible)  RenderLegend(ctx);
    if (controlsConfig.visible) RenderControls(ctx);
}

void UltraCanvasERDiagram::RenderGrid(IRenderContext* ctx) {
    if (style.gridSpacing < 1.0) return;
    ctx->SetStrokePaint(style.gridColor);
    ctx->SetStrokeWidth(1.0 / zoomLevel);

    Point2Dd topLeft = ScreenToWorld(Point2Di(0, 0));
    Point2Dd bottomRight = ScreenToWorld(Point2Di(finalBounds.width, finalBounds.height));

    double startX = std::floor(topLeft.x / style.gridSpacing) * style.gridSpacing;
    double startY = std::floor(topLeft.y / style.gridSpacing) * style.gridSpacing;

    for (double x = startX; x <= bottomRight.x; x += style.gridSpacing) {
        ctx->DrawLine(Point2Dd(x, topLeft.y), Point2Dd(x, bottomRight.y));
    }
    for (double y = startY; y <= bottomRight.y; y += style.gridSpacing) {
        ctx->DrawLine(Point2Dd(topLeft.x, y), Point2Dd(bottomRight.x, y));
    }
}

bool UltraCanvasERDiagram::IsRelatedToHover(const std::string& entityId) const {
    if (hoveredNode.kind != ERHitKind::Entity) return true;
    if (hoveredNode.ownerId == entityId) return true;
    for (const auto& relId : GetRelationshipIdsForEntity(hoveredNode.ownerId)) {
        auto it = relationships.find(relId);
        if (it == relationships.end()) continue;
        for (const auto& leg : it->second.legs) {
            if (leg.entityId == entityId) return true;
        }
    }
    return false;
}

double UltraCanvasERDiagram::AlphaForEntity(const std::string& entityId) const {
    if (!highlightRelatedOnHover || hoveredNode.kind != ERHitKind::Entity) return 1.0;
    return IsRelatedToHover(entityId) ? 1.0 : 0.28;
}

double UltraCanvasERDiagram::AlphaForRelationship(const std::string& relationshipId) const {
    if (!highlightRelatedOnHover || hoveredNode.kind != ERHitKind::Entity) return 1.0;
    auto it = relationships.find(relationshipId);
    if (it == relationships.end()) return 1.0;
    for (const auto& leg : it->second.legs) {
        if (leg.entityId == hoveredNode.ownerId) return 1.0;
    }
    return 0.28;
}

void UltraCanvasERDiagram::RenderConnectors(IRenderContext* ctx) {
    for (const auto& relId : relationshipOrder) {
        auto it = relationships.find(relId);
        if (it == relationships.end()) continue;
        RenderRelationshipConnectors(ctx, it->second);
    }
    if (notation != ERNotation::CrowsFoot) {
        for (const auto& entityId : entityOrder) {
            RenderAttributeSpokes(ctx, entities[entityId]);
        }
    }
}

void UltraCanvasERDiagram::RenderRelationshipConnectors(IRenderContext* ctx,
                                                         const ERDiagramRelationship& rel) {
    double alpha = AlphaForRelationship(rel.id);
    Color lineColor = WithAlpha(style.palette.connectorColor, alpha);
    if (rel.isSelected) lineColor = style.selectionColor;

    ERConnectorStyle routeStyle = rel.connectorStyleOverride ? rel.connectorStyle
                                                             : connectorStyle;

    // Crow's foot: the relationship IS the edge - one line straight between
    // the two entities, no diamond.
    if (notation == ERNotation::CrowsFoot && rel.legs.size() == 2) {
        const auto* a = GetEntity(rel.legs[0].entityId);
        const auto* b = GetEntity(rel.legs[1].entityId);
        if (!a || !b) return;

        std::vector<Point2Dd> path;
        if (a->id == b->id) {
            BuildSelfLoopPath(*a, rel, 0, path);
        } else {
            Point2Dd from = GetEntityBorderPoint(*a, GetEntityCenter(*b));
            Point2Dd to = GetEntityBorderPoint(*b, GetEntityCenter(*a));
            BuildConnectorPath(from, to, routeStyle, path);
        }
        if (path.size() < 2) return;

        ctx->SetStrokePaint(lineColor);
        ctx->SetStrokeWidth(style.connectorWidth);
        bool identifying = (rel.kind == ERRelationshipKind::Identifying);
        if (!identifying) ctx->SetLineDash(UCDashPattern({ 6.0, 4.0 }));
        ctx->DrawLinePath(path, false);
        if (!identifying) ctx->SetLineDash(UCDashPattern());

        // Markers point inward from each entity's border.
        RenderLineEndMarker(ctx, path.front(), path[1], rel.legs[0], lineColor);
        RenderLineEndMarker(ctx, path.back(), path[path.size() - 2], rel.legs[1], lineColor);

        if (showRelationshipNames && !rel.name.empty()) {
            Point2Dd mid = path[path.size() / 2];
            int textWidth = 0, textHeight = 0;
            EstimateTextSize(rel.name, style.cardinalityFontSize, false, textWidth, textHeight);
            Rect2Dd box(mid.x - textWidth / 2.0 - 4.0, mid.y - textHeight / 2.0 - 2.0,
                        textWidth + 8.0, textHeight + 4.0);
            ctx->SetFillPaint(style.backgroundColor);
            ctx->FillRectangle(box);
            RenderCenteredText(ctx, rel.name, box,
                               WithAlpha(style.palette.cardinalityText, alpha),
                               style.cardinalityFontSize, FontWeight::Normal);
        }
        RenderCardinalityLabel(ctx, path.front(), path[1], rel, 0);
        RenderCardinalityLabel(ctx, path.back(), path[path.size() - 2], rel, 1);
        return;
    }

    // Chen: one connector per leg, entity border -> diamond border.
    Point2Dd relCenter = GetRelationshipCenter(rel);
    for (size_t i = 0; i < rel.legs.size(); ++i) {
        const auto& leg = rel.legs[i];
        const auto* entity = GetEntity(leg.entityId);
        if (!entity) continue;

        std::vector<Point2Dd> path;
        if (rel.IsRecursive()) {
            BuildSelfLoopPath(*entity, rel, static_cast<int>(i), path);
        } else {
            Point2Dd from = GetEntityBorderPoint(*entity, relCenter);
            Point2Dd to = GetDiamondBorderPoint(rel, GetEntityCenter(*entity));
            BuildConnectorPath(from, to, routeStyle, path);
        }
        if (path.size() < 2) continue;

        ctx->SetStrokePaint(lineColor);
        ctx->SetStrokeWidth(style.connectorWidth);

        if (leg.participation == ERParticipation::Total) {
            // Total participation is the classic double line.
            RenderDoubleLinePath(ctx, path, style.doubleLineGap);
        } else {
            if (leg.participation == ERParticipation::Optional) {
                ctx->SetLineDash(UCDashPattern({ 6.0, 4.0 }));
            }
            ctx->DrawLinePath(path, false);
            if (leg.participation == ERParticipation::Optional) {
                ctx->SetLineDash(UCDashPattern());
            }
        }

        RenderLineEndMarker(ctx, path.front(), path[1], leg, lineColor);
        RenderCardinalityLabel(ctx, path.front(), path[1], rel, i);

        if (showRoleNames && !leg.roleName.empty()) {
            Point2Dd anchor = path[path.size() / 2];
            int textWidth = 0, textHeight = 0;
            EstimateTextSize(leg.roleName, style.cardinalityFontSize - 1.0, false,
                             textWidth, textHeight);
            Rect2Dd box(anchor.x - textWidth / 2.0, anchor.y - textHeight - 4.0,
                        textWidth, textHeight);
            RenderCenteredText(ctx, leg.roleName, box,
                               WithAlpha(style.palette.roleText, alpha),
                               style.cardinalityFontSize - 1.0, FontWeight::Normal);
        }
    }
}

void UltraCanvasERDiagram::RenderAttributeSpokes(IRenderContext* ctx,
                                                  const ERDiagramEntity& entity) {
    double alpha = AlphaForEntity(entity.id);
    ctx->SetStrokePaint(WithAlpha(style.palette.connectorColor, alpha * 0.85));
    ctx->SetStrokeWidth(std::max(1.0, style.connectorWidth - 0.4));

    Rect2Dd entityRect = GetEntityRect(entity);
    for (const auto& attribute : entity.attributes) {
        if (!attribute.visible) continue;
        Rect2Dd ovalRect(attribute.x, attribute.y, attribute.width, attribute.height);
        Point2Dd ovalCenter(ovalRect.x + ovalRect.width / 2.0,
                            ovalRect.y + ovalRect.height / 2.0);
        Point2Dd from = RectBorderPoint(entityRect, ovalCenter);
        Point2Dd to = EllipseBorderPoint(ovalRect, Point2Dd(entityRect.x + entityRect.width / 2.0,
                                                            entityRect.y + entityRect.height / 2.0));
        ctx->DrawLine(from, to);

        for (const auto& child : attribute.children) {
            Rect2Dd childRect(child.x, child.y, child.width, child.height);
            Point2Dd childCenter(childRect.x + childRect.width / 2.0,
                                 childRect.y + childRect.height / 2.0);
            ctx->DrawLine(EllipseBorderPoint(ovalRect, childCenter),
                          EllipseBorderPoint(childRect, ovalCenter));
        }
    }

    // Relationship attributes
    for (const auto& relId : relationshipOrder) {
        const auto& rel = relationships.at(relId);
        Rect2Dd relRect = GetRelationshipRect(rel);
        Point2Dd relCenter = GetRelationshipCenter(rel);
        for (const auto& attribute : rel.attributes) {
            Rect2Dd ovalRect(attribute.x, attribute.y, attribute.width, attribute.height);
            Point2Dd ovalCenter(ovalRect.x + ovalRect.width / 2.0,
                                ovalRect.y + ovalRect.height / 2.0);
            ctx->DrawLine(DiamondBorderPoint(relRect, ovalCenter),
                          EllipseBorderPoint(ovalRect, relCenter));
        }
    }
}

void UltraCanvasERDiagram::RenderDoubleLinePath(IRenderContext* ctx,
                                                 const std::vector<Point2Dd>& path,
                                                 double gap) {
    if (path.size() < 2) return;
    // Offset each segment along its own normal; good enough for the shallow
    // angles ER connectors actually use.
    for (int side = -1; side <= 1; side += 2) {
        std::vector<Point2Dd> offsetPath;
        offsetPath.reserve(path.size());
        for (size_t i = 0; i < path.size(); ++i) {
            size_t a = (i == 0) ? 0 : i - 1;
            size_t b = (i == 0) ? 1 : i;
            double dx = path[b].x - path[a].x;
            double dy = path[b].y - path[a].y;
            double len = std::sqrt(dx * dx + dy * dy);
            if (len < 1e-6) { offsetPath.push_back(path[i]); continue; }
            double nx = -dy / len;
            double ny = dx / len;
            offsetPath.emplace_back(path[i].x + nx * gap / 2.0 * side,
                                    path[i].y + ny * gap / 2.0 * side);
        }
        ctx->DrawLinePath(offsetPath, false);
    }
}

void UltraCanvasERDiagram::RenderLineEndMarker(IRenderContext* ctx, const Point2Dd& tip,
                                                const Point2Dd& inward,
                                                const ERDiagramLeg& leg,
                                                const Color& color) {
    ERLineEndStyle marker = ResolveLineEnd(leg);
    if (marker == ERLineEndStyle::NoMarker) return;

    double dx = inward.x - tip.x;
    double dy = inward.y - tip.y;
    double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6) return;
    double ux = dx / len;
    double uy = dy / len;
    double nx = -uy;
    double ny = ux;

    const double size = 12.0;
    ctx->SetStrokePaint(color);
    ctx->SetFillPaint(color);
    ctx->SetStrokeWidth(style.connectorWidth);

    auto drawTick = [&](double along) {
        Point2Dd center(tip.x + ux * along, tip.y + uy * along);
        ctx->DrawLine(Point2Dd(center.x + nx * size * 0.45, center.y + ny * size * 0.45),
                      Point2Dd(center.x - nx * size * 0.45, center.y - ny * size * 0.45));
    };
    auto drawCrowsFoot = [&](double along) {
        Point2Dd base(tip.x + ux * (along + size), tip.y + uy * (along + size));
        Point2Dd apex(tip.x + ux * along, tip.y + uy * along);
        ctx->DrawLine(apex, base);
        ctx->DrawLine(apex, Point2Dd(base.x + nx * size * 0.55, base.y + ny * size * 0.55));
        ctx->DrawLine(apex, Point2Dd(base.x - nx * size * 0.55, base.y - ny * size * 0.55));
    };
    auto drawCircle = [&](double along) {
        Point2Dd center(tip.x + ux * along, tip.y + uy * along);
        ctx->SetFillPaint(style.backgroundColor);
        ctx->FillCircle(center, size * 0.32);
        ctx->SetStrokePaint(color);
        ctx->DrawCircle(center, size * 0.32);
    };

    switch (marker) {
        case ERLineEndStyle::NoMarker:
            break;
        case ERLineEndStyle::Tick:
            drawTick(size * 0.9);
            break;
        case ERLineEndStyle::DoubleTick:
            drawTick(size * 0.75);
            drawTick(size * 1.25);
            break;
        case ERLineEndStyle::CrowsFoot:
            drawCrowsFoot(0.0);
            break;
        case ERLineEndStyle::CircleCrowsFoot:
            drawCrowsFoot(0.0);
            drawCircle(size * 1.65);
            break;
        case ERLineEndStyle::Circle:
            drawCircle(size * 0.55);
            break;
        case ERLineEndStyle::Arrow: {
            std::vector<Point2Dd> head = {
                tip,
                Point2Dd(tip.x + ux * size + nx * size * 0.42,
                         tip.y + uy * size + ny * size * 0.42),
                Point2Dd(tip.x + ux * size - nx * size * 0.42,
                         tip.y + uy * size - ny * size * 0.42)
            };
            ctx->SetFillPaint(color);
            ctx->FillLinePath(head);
            break;
        }
        case ERLineEndStyle::FilledDot:
            ctx->SetFillPaint(color);
            ctx->FillCircle(Point2Dd(tip.x + ux * size * 0.4, tip.y + uy * size * 0.4),
                            size * 0.3);
            break;
    }
}

void UltraCanvasERDiagram::RenderCardinalityLabel(IRenderContext* ctx, const Point2Dd& at,
                                                   const Point2Dd& inward,
                                                   const ERDiagramRelationship& rel,
                                                   size_t legIndex) {
    std::string text = CardinalityText(rel, legIndex);
    if (text.empty()) return;

    double dx = inward.x - at.x;
    double dy = inward.y - at.y;
    double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6) { dx = 0.0; dy = -1.0; len = 1.0; }

    // Sit a little way along the leg, offset perpendicular so the label never
    // lands on the line itself. Clear the line-end marker, which occupies the
    // first ~26px of the leg.
    double alongX = at.x + (dx / len) * 34.0;
    double alongY = at.y + (dy / len) * 34.0;
    double offX = -(dy / len) * 12.0;
    double offY = (dx / len) * 12.0;

    int textWidth = 0, textHeight = 0;
    EstimateTextSize(text, style.cardinalityFontSize, true, textWidth, textHeight);
    Rect2Dd box(alongX + offX - textWidth / 2.0, alongY + offY - textHeight / 2.0,
                static_cast<double>(textWidth), static_cast<double>(textHeight));

    RenderCenteredText(ctx, text, box, style.palette.cardinalityText,
                       style.cardinalityFontSize, FontWeight::Bold);
}

void UltraCanvasERDiagram::RenderCenteredText(IRenderContext* ctx, const std::string& text,
                                               const Rect2Dd& box, const Color& color,
                                               double fontSize, FontWeight weight) {
    if (text.empty()) return;
    ctx->SetTextPaint(color);
    ctx->SetFontFace(style.fontFamily, weight, FontSlant::Normal);
    ctx->SetFontSize(fontSize);

    Size2Di dimensions = ctx->GetTextLineDimensions(text);
    // Cairo+Pango treats the DrawText Y as the TOP of the bounding box, not the
    // baseline - same correction as NodeDiagram 2.0.6.
    double textX = box.x + (box.width - static_cast<double>(dimensions.width)) / 2.0;
    double textY = box.y + (box.height - static_cast<double>(dimensions.height)) / 2.0;
    ctx->DrawText(text, Point2Dd(textX, textY));
}

void UltraCanvasERDiagram::RenderUnderline(IRenderContext* ctx, const Point2Dd& textOrigin,
                                            int textWidth, int textHeight, bool dashed,
                                            const Color& color) {
    ctx->SetStrokePaint(color);
    ctx->SetStrokeWidth(1.2);
    if (dashed) ctx->SetLineDash(UCDashPattern({ 3.0, 2.0 }));
    double y = textOrigin.y + static_cast<double>(textHeight) + 1.0;
    ctx->DrawLine(Point2Dd(textOrigin.x, y),
                  Point2Dd(textOrigin.x + static_cast<double>(textWidth), y));
    if (dashed) ctx->SetLineDash(UCDashPattern());
}

void UltraCanvasERDiagram::RenderEntities(IRenderContext* ctx) {
    for (const auto& entityId : entityOrder) {
        const auto& entity = entities[entityId];
        if (notation == ERNotation::CrowsFoot) {
            RenderEntityCrowsFoot(ctx, entity);
        } else {
            RenderEntityChen(ctx, entity);
        }
    }
}

void UltraCanvasERDiagram::RenderEntityChen(IRenderContext* ctx, const ERDiagramEntity& entity) {
    double alpha = AlphaForEntity(entity.id);
    ERNodeRole role = (entity.kind == ERDiagramEntityKind::Weak) ? ERNodeRole::WeakEntity
                                                                 : ERNodeRole::Entity;

    Color fill = entity.useCustomColors ? entity.fillColor : GetRoleFill(role);
    Color border = entity.useCustomColors ? entity.borderColor : GetRoleBorder(role);
    Color textColor = entity.useCustomColors ? entity.textColor : GetRoleText(role);
    double borderWidth = entity.borderWidth;

    if (hoveredNode.kind == ERHitKind::Entity && hoveredNode.ownerId == entity.id &&
        !entity.isSelected) {
        fill = Lighten(fill, 18);
        borderWidth += 1.0;
    }
    if (entity.isSelected) {
        border = style.selectionColor;
        borderWidth = style.selectionWidth;
    }

    Rect2Dd rect = GetEntityRect(entity);
    ctx->SetFillPaint(WithAlpha(fill, alpha));
    ctx->SetStrokePaint(WithAlpha(border, alpha));
    ctx->SetStrokeWidth(borderWidth);

    if (style.roundedEntities) {
        ctx->FillRoundedRectangle(rect, style.cornerRadius);
        ctx->DrawRoundedRectangle(rect, style.cornerRadius);
    } else {
        ctx->FillRectangle(rect);
        ctx->DrawRectangle(rect);
    }

    // Weak entity: the second, inset rectangle.
    if (entity.kind == ERDiagramEntityKind::Weak) {
        double inset = style.doubleLineGap;
        Rect2Dd inner(rect.x + inset, rect.y + inset,
                      rect.width - 2.0 * inset, rect.height - 2.0 * inset);
        if (inner.width > 0.0 && inner.height > 0.0) {
            ctx->SetStrokeWidth(std::max(1.0, borderWidth - 0.5));
            if (style.roundedEntities) {
                ctx->DrawRoundedRectangle(inner, std::max(1.0, style.cornerRadius - 2.0));
            } else {
                ctx->DrawRectangle(inner);
            }
        }
    }

    // Associative entity: a diamond inscribed in the rectangle.
    if (entity.kind == ERDiagramEntityKind::Associative) {
        double cx = rect.x + rect.width / 2.0;
        double cy = rect.y + rect.height / 2.0;
        std::vector<Point2Dd> diamond = {
            Point2Dd(cx, rect.y + 3.0),
            Point2Dd(rect.x + rect.width - 3.0, cy),
            Point2Dd(cx, rect.y + rect.height - 3.0),
            Point2Dd(rect.x + 3.0, cy)
        };
        ctx->SetStrokeWidth(std::max(1.0, borderWidth - 0.5));
        ctx->DrawLinePath(diamond, true);
    }

    RenderCenteredText(ctx, entity.name, rect, WithAlpha(textColor, alpha),
                       style.baseFontSize, FontWeight::Bold);
}

void UltraCanvasERDiagram::RenderEntityCrowsFoot(IRenderContext* ctx,
                                                  const ERDiagramEntity& entity) {
    double alpha = AlphaForEntity(entity.id);
    ERNodeRole role = (entity.kind == ERDiagramEntityKind::Weak) ? ERNodeRole::WeakEntity
                                                                 : ERNodeRole::Entity;
    Color headerFill = entity.useCustomColors ? entity.fillColor : GetRoleFill(role);
    Color border = entity.useCustomColors ? entity.borderColor : GetRoleBorder(role);
    Color headerText = entity.useCustomColors ? entity.textColor : GetRoleText(role);
    double borderWidth = entity.borderWidth;

    if (entity.isSelected) {
        border = style.selectionColor;
        borderWidth = style.selectionWidth;
    }

    Rect2Dd rect = GetEntityRect(entity);
    Rect2Dd header(rect.x, rect.y, rect.width, ER_HEADER_HEIGHT);

    // Body
    ctx->SetFillPaint(WithAlpha(style.palette.rowFill, alpha));
    ctx->SetStrokePaint(WithAlpha(border, alpha));
    ctx->SetStrokeWidth(borderWidth);
    ctx->FillRoundedRectangle(rect, style.cornerRadius);
    ctx->DrawRoundedRectangle(rect, style.cornerRadius);

    // Header band
    ctx->SetFillPaint(WithAlpha(headerFill, alpha));
    ctx->FillRectangle(header);
    ctx->DrawLine(Point2Dd(rect.x, rect.y + ER_HEADER_HEIGHT),
                  Point2Dd(rect.x + rect.width, rect.y + ER_HEADER_HEIGHT));

    RenderCenteredText(ctx, entity.name, header, WithAlpha(headerText, alpha),
                       style.baseFontSize, FontWeight::Bold);
    if (entity.collapsed) return;

    // Attribute rows: a badge cell (PK / FK1 / UK) and a name cell, in whichever
    // order the house style puts them.
    size_t keyCount = 0;
    std::vector<const ERDiagramAttribute*> order = RowOrder(entity, keyCount);
    bool drawDivider = style.keyCompartment && keyCount > 0 && keyCount < order.size();

    const double markerWidth = EffectiveMarkerColumnWidth();
    const double inset = 8.0;
    bool markerLeft = (style.rowMarkerSide == ERRowMarkerSide::Left);

    double markerX = markerLeft ? rect.x + inset
                                : rect.x + rect.width - inset - markerWidth;
    double nameX   = markerLeft ? rect.x + inset + markerWidth : rect.x + inset;
    double nameWidth = rect.width - 2.0 * inset - markerWidth;

    double rowY = rect.y + ER_HEADER_HEIGHT + 4.0;
    size_t index = 0;

    for (const auto* attributePtr : order) {
        const auto& attribute = *attributePtr;

        if (drawDivider && index == keyCount) {
            double lineY = rowY + ER_KEY_DIVIDER_GAP / 2.0;
            ctx->SetStrokePaint(WithAlpha(style.palette.rowDivider, alpha));
            ctx->SetStrokeWidth(1.0);
            ctx->DrawLine(Point2Dd(rect.x, lineY), Point2Dd(rect.x + rect.width, lineY));
            rowY += ER_KEY_DIVIDER_GAP;
        }

        if (style.stripeRows && (index % 2) == 1) {
            ctx->SetFillPaint(WithAlpha(style.palette.rowStripeFill, alpha));
            ctx->FillRectangle(Rect2Dd(rect.x + 1.0, rowY - 2.0,
                                       rect.width - 2.0, ER_ROW_HEIGHT));
        }

        std::string marker = RowMarkerText(entity, attribute);

        if (attribute.highlighted) {
            ctx->SetFillPaint(WithAlpha(style.palette.rowHighlightFill, alpha));
            ctx->FillRectangle(Rect2Dd(markerX - 4.0, rowY - 2.0,
                                       markerWidth + 8.0, ER_ROW_HEIGHT));
        }

        bool isKey = (attribute.keyRole != ERKeyRole::NoKey);
        Color rowColor = WithAlpha(isKey ? style.palette.rowKeyText
                                         : style.palette.rowText, alpha);
        ctx->SetTextPaint(rowColor);
        ctx->SetFontFace(style.fontFamily,
                         isKey ? FontWeight::Bold : FontWeight::Normal,
                         attribute.kind == ERAttributeKind::Derived ? FontSlant::Italic
                                                                    : FontSlant::Normal);
        ctx->SetFontSize(style.rowFontSize);

        if (!marker.empty()) {
            ctx->DrawText(marker, Point2Dd(markerX, rowY));
        }

        std::string label = attribute.name;
        if (showDataTypes && !attribute.dataType.empty()) {
            label += " : " + attribute.dataType;
        }
        if (!attribute.nullable) label += " *";

        Point2Dd labelOrigin(nameX, rowY);
        ctx->DrawText(label, labelOrigin);

        // IDEF1X underlines the key column names inside the compartment.
        if (style.underlineKeyRows &&
            (attribute.keyRole == ERKeyRole::Primary ||
             attribute.keyRole == ERKeyRole::Partial)) {
            Size2Di dimensions = ctx->GetTextLineDimensions(label);
            RenderUnderline(ctx, labelOrigin,
                            std::min(dimensions.width, static_cast<int>(nameWidth)),
                            dimensions.height,
                            attribute.keyRole == ERKeyRole::Partial, rowColor);
        }

        rowY += ER_ROW_HEIGHT;
        ++index;
    }
}

void UltraCanvasERDiagram::RenderRelationships(IRenderContext* ctx) {
    for (const auto& relId : relationshipOrder) {
        const auto& rel = relationships[relId];
        double alpha = AlphaForRelationship(relId);
        ERNodeRole role = (rel.kind == ERRelationshipKind::Identifying)
                          ? ERNodeRole::IdentifyingRelationship : ERNodeRole::Relationship;

        Color fill = rel.useCustomColors ? rel.fillColor : GetRoleFill(role);
        Color border = rel.useCustomColors ? rel.borderColor : GetRoleBorder(role);
        Color textColor = rel.useCustomColors ? rel.textColor : GetRoleText(role);
        double borderWidth = 2.0;

        if (hoveredNode.kind == ERHitKind::Relationship && hoveredNode.ownerId == relId &&
            !rel.isSelected) {
            fill = Lighten(fill, 18);
        }
        if (rel.isSelected) {
            border = style.selectionColor;
            borderWidth = style.selectionWidth;
        }

        Rect2Dd rect = GetRelationshipRect(rel);
        double cx = rect.x + rect.width / 2.0;
        double cy = rect.y + rect.height / 2.0;
        std::vector<Point2Dd> diamond = {
            Point2Dd(cx, rect.y),
            Point2Dd(rect.x + rect.width, cy),
            Point2Dd(cx, rect.y + rect.height),
            Point2Dd(rect.x, cy)
        };

        ctx->SetFillPaint(WithAlpha(fill, alpha));
        ctx->SetStrokePaint(WithAlpha(border, alpha));
        ctx->SetStrokeWidth(borderWidth);
        ctx->FillLinePath(diamond);
        ctx->DrawLinePath(diamond, true);

        // Identifying relationship: the inner second diamond.
        if (rel.kind == ERRelationshipKind::Identifying) {
            double gap = style.doubleLineGap;
            std::vector<Point2Dd> inner = {
                Point2Dd(cx, rect.y + gap * 1.8),
                Point2Dd(rect.x + rect.width - gap * 1.8, cy),
                Point2Dd(cx, rect.y + rect.height - gap * 1.8),
                Point2Dd(rect.x + gap * 1.8, cy)
            };
            ctx->SetStrokeWidth(std::max(1.0, borderWidth - 0.5));
            ctx->DrawLinePath(inner, true);
        }

        if (showRelationshipNames) {
            RenderCenteredText(ctx, rel.name, rect, WithAlpha(textColor, alpha),
                               style.attributeFontSize, FontWeight::Bold);
        }
    }
}

void UltraCanvasERDiagram::RenderAttributes(IRenderContext* ctx) {
    for (const auto& entityId : entityOrder) {
        const auto& entity = entities[entityId];
        bool dimmed = AlphaForEntity(entityId) < 0.99;
        for (const auto& attribute : entity.attributes) {
            if (!attribute.visible) continue;
            RenderAttributeOval(ctx, attribute, !dimmed);
            for (const auto& child : attribute.children) {
                RenderAttributeOval(ctx, child, !dimmed);
            }
        }
    }
    for (const auto& relId : relationshipOrder) {
        const auto& rel = relationships[relId];
        bool dimmed = AlphaForRelationship(relId) < 0.99;
        for (const auto& attribute : rel.attributes) {
            RenderAttributeOval(ctx, attribute, !dimmed);
        }
    }
}

void UltraCanvasERDiagram::RenderAttributeOval(IRenderContext* ctx,
                                                const ERDiagramAttribute& attribute,
                                                bool fullyOpaque) {
    double alpha = fullyOpaque ? 1.0 : 0.28;

    ERNodeRole role = ERNodeRole::Attribute;
    if (attribute.keyRole == ERKeyRole::Primary || attribute.keyRole == ERKeyRole::Partial) {
        role = ERNodeRole::KeyAttribute;
    } else if (attribute.kind == ERAttributeKind::Derived) {
        role = ERNodeRole::DerivedAttribute;
    } else if (attribute.kind == ERAttributeKind::MultiValued) {
        role = ERNodeRole::MultiValuedAttribute;
    }

    Color fill = attribute.useCustomColors ? attribute.fillColor : GetRoleFill(role);
    Color border = attribute.useCustomColors ? attribute.borderColor : GetRoleBorder(role);
    Color textColor = attribute.useCustomColors ? attribute.textColor : GetRoleText(role);

    Rect2Dd rect(attribute.x, attribute.y, attribute.width, attribute.height);
    ctx->SetFillPaint(WithAlpha(fill, alpha));
    ctx->SetStrokePaint(WithAlpha(border, alpha));
    ctx->SetStrokeWidth(1.5);

    // Derived attributes are drawn with a dashed outline.
    if (attribute.kind == ERAttributeKind::Derived) {
        ctx->FillEllipse(rect);
        ctx->SetLineDash(UCDashPattern({ 5.0, 3.0 }));
        ctx->DrawEllipse(rect);
        ctx->SetLineDash(UCDashPattern());
    } else {
        ctx->FillEllipse(rect);
        ctx->DrawEllipse(rect);
        // Multivalued attributes get the second, inner ellipse.
        if (attribute.kind == ERAttributeKind::MultiValued) {
            double inset = 3.5;
            Rect2Dd inner(rect.x + inset, rect.y + inset,
                          rect.width - 2.0 * inset, rect.height - 2.0 * inset);
            if (inner.width > 0.0 && inner.height > 0.0) ctx->DrawEllipse(inner);
        }
    }

    // Text, then the key underline directly beneath it.
    ctx->SetTextPaint(WithAlpha(textColor, alpha));
    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(style.attributeFontSize);
    Size2Di dimensions = ctx->GetTextLineDimensions(attribute.name);
    double textX = rect.x + (rect.width - static_cast<double>(dimensions.width)) / 2.0;
    double textY = rect.y + (rect.height - static_cast<double>(dimensions.height)) / 2.0;
    ctx->DrawText(attribute.name, Point2Dd(textX, textY));

    if (attribute.keyRole == ERKeyRole::Primary || attribute.keyRole == ERKeyRole::Partial) {
        RenderUnderline(ctx, Point2Dd(textX, textY), dimensions.width, dimensions.height,
                        attribute.keyRole == ERKeyRole::Partial,
                        WithAlpha(textColor, alpha));
    }
}

void UltraCanvasERDiagram::RenderAnnotations(IRenderContext* ctx) {
    for (const auto& annId : annotationOrder) {
        const auto& annotation = annotations[annId];
        if (annotation.text.empty()) continue;

        // Leader line back to the anchor
        Point2Dd anchor(0.0, 0.0);
        bool found = false;
        size_t dot = annotation.anchorRef.find('.');
        if (dot != std::string::npos) {
            auto entityIt = entities.find(annotation.anchorRef.substr(0, dot));
            if (entityIt != entities.end()) {
                std::string attributeId = annotation.anchorRef.substr(dot + 1);
                for (const auto& attribute : entityIt->second.attributes) {
                    if (attribute.id != attributeId) continue;
                    anchor = Point2Dd(attribute.x + attribute.width / 2.0,
                                      attribute.y + attribute.height / 2.0);
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            auto entityIt = entities.find(annotation.anchorRef);
            if (entityIt != entities.end()) {
                anchor = GetEntityCenter(entityIt->second);
                found = true;
            } else {
                auto relIt = relationships.find(annotation.anchorRef);
                if (relIt != relationships.end()) {
                    anchor = GetRelationshipCenter(relIt->second);
                    found = true;
                }
            }
        }

        Rect2Dd box(annotation.x, annotation.y,
                    std::max(annotation.width, 10.0), std::max(annotation.height, 10.0));
        if (found) {
            Point2Dd boxCenter(box.x + box.width / 2.0, box.y + box.height / 2.0);
            Point2Dd from = RectBorderPoint(box, anchor);
            ctx->SetStrokePaint(annotation.useCustomColors ? annotation.leaderColor
                                                           : style.palette.roleText);
            ctx->SetStrokeWidth(1.0);
            ctx->SetLineDash(UCDashPattern({ 4.0, 3.0 }));
            ctx->DrawLine(from, anchor);
            ctx->SetLineDash(UCDashPattern());
            (void)boxCenter;
        }

        RenderCenteredText(ctx, annotation.text, box,
                           annotation.useCustomColors ? annotation.textColor
                                                      : style.palette.cardinalityText,
                           annotation.fontSize, FontWeight::Normal);
    }
}

void UltraCanvasERDiagram::RenderSelectionBox(IRenderContext* ctx) {
    Rect2Dd box(std::min(selectionBoxStart.x, selectionBoxEnd.x),
                std::min(selectionBoxStart.y, selectionBoxEnd.y),
                std::abs(selectionBoxEnd.x - selectionBoxStart.x),
                std::abs(selectionBoxEnd.y - selectionBoxStart.y));
    ctx->SetFillPaint(style.selectionBoxFill);
    ctx->FillRectangle(box);
    ctx->SetStrokePaint(style.selectionBoxStroke);
    ctx->SetStrokeWidth(1.0 / zoomLevel);
    ctx->DrawRectangle(box);
}

// =============================================================================
// SCREEN-SPACE CHROME
// =============================================================================

Rect2Dd UltraCanvasERDiagram::GetPanelRect(ERPanelPosition position, double width,
                                            double height, double padding) const {
    double viewW = static_cast<double>(finalBounds.width);
    double viewH = static_cast<double>(finalBounds.height);
    double topInset = titleConfig.visible ? titleConfig.height : 0.0;

    switch (position) {
        case ERPanelPosition::TopLeft:
            return Rect2Dd(padding, topInset + padding, width, height);
        case ERPanelPosition::TopRight:
            return Rect2Dd(viewW - width - padding, topInset + padding, width, height);
        case ERPanelPosition::BottomLeft:
            return Rect2Dd(padding, viewH - height - padding, width, height);
        case ERPanelPosition::BottomRight:
            return Rect2Dd(viewW - width - padding, viewH - height - padding, width, height);
    }
    return Rect2Dd(padding, padding, width, height);
}

void UltraCanvasERDiagram::RenderTitle(IRenderContext* ctx) {
    Rect2Dd band(0.0, 0.0, static_cast<double>(finalBounds.width), titleConfig.height);
    ctx->SetFillPaint(titleConfig.backgroundColor);
    ctx->FillRectangle(band);

    if (titleConfig.showSeparator) {
        ctx->SetStrokePaint(style.gridColor);
        ctx->SetStrokeWidth(1.0);
        ctx->DrawLine(Point2Dd(0.0, titleConfig.height),
                      Point2Dd(band.width, titleConfig.height));
    }

    bool hasSubtitle = !titleConfig.subtitle.empty();
    double titleHeight = hasSubtitle ? titleConfig.height * 0.62 : titleConfig.height;
    RenderCenteredText(ctx, titleConfig.text, Rect2Dd(0.0, 0.0, band.width, titleHeight),
                       titleConfig.textColor, titleConfig.fontSize, FontWeight::Bold);
    if (hasSubtitle) {
        RenderCenteredText(ctx, titleConfig.subtitle,
                           Rect2Dd(0.0, titleHeight, band.width,
                                   titleConfig.height - titleHeight),
                           titleConfig.subtitleColor, titleConfig.fontSize * 0.72,
                           FontWeight::Normal);
    }
}

// The legend is generated from the active notation, so it can never drift out
// of sync with what is actually being drawn (reference image 1).
void UltraCanvasERDiagram::RenderLegend(IRenderContext* ctx) {
    struct LegendRow {
        std::string label;
        bool isMarker = false;
        ERNodeRole role = ERNodeRole::Entity;
        ERLineEndStyle marker = ERLineEndStyle::NoMarker;
    };

    std::vector<LegendRow> rows;
    if (legendConfig.showShapes) {
        if (notation == ERNotation::CrowsFoot) {
            rows.push_back({ "Entity", false, ERNodeRole::Entity, ERLineEndStyle::NoMarker });
            rows.push_back({ "Attribute row", false, ERNodeRole::AttributeRow,
                             ERLineEndStyle::NoMarker });
        } else {
            rows.push_back({ "Entity", false, ERNodeRole::Entity, ERLineEndStyle::NoMarker });
            rows.push_back({ "Weak entity", false, ERNodeRole::WeakEntity,
                             ERLineEndStyle::NoMarker });
            rows.push_back({ "Relationship", false, ERNodeRole::Relationship,
                             ERLineEndStyle::NoMarker });
            rows.push_back({ "Attribute", false, ERNodeRole::Attribute,
                             ERLineEndStyle::NoMarker });
            rows.push_back({ "Key attribute", false, ERNodeRole::KeyAttribute,
                             ERLineEndStyle::NoMarker });
        }
    }
    if (legendConfig.showMarkers && lineEndStyle != ERLineEndStyle::NoMarker) {
        rows.push_back({ "One", true, ERNodeRole::Entity, ERLineEndStyle::DoubleTick });
        rows.push_back({ "Zero or many, optional", true, ERNodeRole::Entity,
                         ERLineEndStyle::CircleCrowsFoot });
        rows.push_back({ "Many", true, ERNodeRole::Entity, ERLineEndStyle::CrowsFoot });
    }
    if (rows.empty()) return;

    bool hasTitle = !legendConfig.title.empty();
    double width = 0.0;
    for (const auto& row : rows) {
        int textWidth = 0, textHeight = 0;
        EstimateTextSize(row.label, legendConfig.fontSize, false, textWidth, textHeight);
        width = std::max(width, static_cast<double>(textWidth));
    }
    width += legendConfig.swatchWidth + legendConfig.padding * 3.0;
    double height = legendConfig.padding * 2.0 +
                    static_cast<double>(rows.size()) * legendConfig.rowHeight +
                    (hasTitle ? legendConfig.rowHeight : 0.0);

    Rect2Dd panel = GetPanelRect(legendConfig.position, width, height, legendConfig.padding);
    ctx->SetFillPaint(legendConfig.backgroundColor);
    ctx->SetStrokePaint(legendConfig.borderColor);
    ctx->SetStrokeWidth(1.0);
    ctx->FillRoundedRectangle(panel, 4.0);
    ctx->DrawRoundedRectangle(panel, 4.0);

    double rowY = panel.y + legendConfig.padding;
    if (hasTitle) {
        RenderCenteredText(ctx, legendConfig.title,
                           Rect2Dd(panel.x, rowY, panel.width, legendConfig.rowHeight),
                           legendConfig.textColor, legendConfig.fontSize, FontWeight::Bold);
        rowY += legendConfig.rowHeight;
    }

    for (const auto& row : rows) {
        double swatchX = panel.x + legendConfig.padding;
        double centerY = rowY + legendConfig.rowHeight / 2.0;

        if (row.isMarker) {
            ctx->SetStrokePaint(style.palette.connectorColor);
            ctx->SetStrokeWidth(style.connectorWidth);
            Point2Dd tip(swatchX, centerY);
            Point2Dd inward(swatchX + legendConfig.swatchWidth, centerY);
            ctx->DrawLine(tip, inward);
            ERDiagramLeg dummy;
            dummy.lineEnd = row.marker;
            RenderLineEndMarker(ctx, tip, inward, dummy, style.palette.connectorColor);
        } else {
            Rect2Dd swatch(swatchX, centerY - 7.0, legendConfig.swatchWidth, 14.0);
            ctx->SetFillPaint(GetRoleFill(row.role));
            ctx->SetStrokePaint(GetRoleBorder(row.role));
            ctx->SetStrokeWidth(1.2);
            if (row.role == ERNodeRole::Relationship) {
                double cx = swatch.x + swatch.width / 2.0;
                double cy = swatch.y + swatch.height / 2.0;
                std::vector<Point2Dd> diamond = {
                    Point2Dd(cx, swatch.y),
                    Point2Dd(swatch.x + swatch.width, cy),
                    Point2Dd(cx, swatch.y + swatch.height),
                    Point2Dd(swatch.x, cy)
                };
                ctx->FillLinePath(diamond);
                ctx->DrawLinePath(diamond, true);
            } else if (row.role == ERNodeRole::Attribute ||
                       row.role == ERNodeRole::KeyAttribute) {  // ovals
                ctx->FillEllipse(swatch);
                ctx->DrawEllipse(swatch);
            } else {
                ctx->FillRectangle(swatch);
                ctx->DrawRectangle(swatch);
                if (row.role == ERNodeRole::WeakEntity) {
                    Rect2Dd inner(swatch.x + 2.5, swatch.y + 2.5,
                                  swatch.width - 5.0, swatch.height - 5.0);
                    ctx->DrawRectangle(inner);
                }
            }
        }

        ctx->SetTextPaint(legendConfig.textColor);
        ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(legendConfig.fontSize);
        Size2Di dimensions = ctx->GetTextLineDimensions(row.label);
        ctx->DrawText(row.label,
                      Point2Dd(swatchX + legendConfig.swatchWidth + legendConfig.padding,
                               centerY - static_cast<double>(dimensions.height) / 2.0));
        rowY += legendConfig.rowHeight;
    }
}

void UltraCanvasERDiagram::RenderControls(IRenderContext* ctx) {
    std::vector<int> buttons;   // 0 zoom in, 1 zoom out, 2 fit, 3 lock
    if (controlsConfig.showZoom) { buttons.push_back(0); buttons.push_back(1); }
    if (controlsConfig.showFit)  buttons.push_back(2);
    if (controlsConfig.showLock) buttons.push_back(3);
    if (buttons.empty()) return;

    double count = static_cast<double>(buttons.size());
    double width = controlsConfig.buttonSize + controlsConfig.gap * 2.0;
    double height = count * controlsConfig.buttonSize +
                    (count + 1.0) * controlsConfig.gap;
    Rect2Dd panel = GetPanelRect(controlsConfig.position, width, height,
                                 controlsConfig.padding);

    ctx->SetFillPaint(controlsConfig.backgroundColor);
    ctx->SetStrokePaint(controlsConfig.borderColor);
    ctx->SetStrokeWidth(1.0);
    ctx->FillRoundedRectangle(panel, 4.0);
    ctx->DrawRoundedRectangle(panel, 4.0);

    double buttonY = panel.y + controlsConfig.gap;
    for (size_t i = 0; i < buttons.size(); ++i) {
        Rect2Dd button(panel.x + controlsConfig.gap, buttonY,
                       controlsConfig.buttonSize, controlsConfig.buttonSize);
        if (hoveredControlButton == static_cast<int>(i)) {
            ctx->SetFillPaint(controlsConfig.hoverColor);
            ctx->FillRoundedRectangle(button, 3.0);
        }

        // Vector glyphs - text glyphs render too small and off-centre at this
        // button size (lesson from NodeDiagram 2.0.1).
        double cx = button.x + button.width / 2.0;
        double cy = button.y + button.height / 2.0;
        double arm = button.width * 0.26;
        ctx->SetStrokePaint(controlsConfig.iconColor);
        ctx->SetStrokeWidth(1.8);

        switch (buttons[i]) {
            case 0:   // Zoom in: plus
                ctx->DrawLine(Point2Dd(cx - arm, cy), Point2Dd(cx + arm, cy));
                ctx->DrawLine(Point2Dd(cx, cy - arm), Point2Dd(cx, cy + arm));
                break;
            case 1:   // Zoom out: minus
                ctx->DrawLine(Point2Dd(cx - arm, cy), Point2Dd(cx + arm, cy));
                break;
            case 2: {  // Fit: corner brackets
                double h = arm;
                ctx->DrawLine(Point2Dd(cx - h, cy - h), Point2Dd(cx - h / 2.0, cy - h));
                ctx->DrawLine(Point2Dd(cx - h, cy - h), Point2Dd(cx - h, cy - h / 2.0));
                ctx->DrawLine(Point2Dd(cx + h, cy + h), Point2Dd(cx + h / 2.0, cy + h));
                ctx->DrawLine(Point2Dd(cx + h, cy + h), Point2Dd(cx + h, cy + h / 2.0));
                break;
            }
            case 3: {  // Lock: shackle + body
                Rect2Dd body(cx - arm, cy - arm * 0.1, arm * 2.0, arm * 1.3);
                ctx->SetFillPaint(controlsConfig.iconColor);
                if (isInteractive) {
                    ctx->DrawRectangle(body);
                } else {
                    ctx->FillRectangle(body);
                }
                ctx->DrawArc(cx, cy - arm * 0.1, arm * 0.62, ER_PI, 2.0 * ER_PI);
                break;
            }
            default:
                break;
        }
        buttonY += controlsConfig.buttonSize + controlsConfig.gap;
    }
}

// =============================================================================
// HIT TESTING
// =============================================================================

bool UltraCanvasERDiagram::PointInEntity(const ERDiagramEntity& entity,
                                          const Point2Dd& worldPos) const {
    Rect2Dd rect = GetEntityRect(entity);
    return worldPos.x >= rect.x && worldPos.x <= rect.x + rect.width &&
           worldPos.y >= rect.y && worldPos.y <= rect.y + rect.height;
}

bool UltraCanvasERDiagram::PointInDiamond(const ERDiagramRelationship& rel,
                                           const Point2Dd& worldPos) const {
    Rect2Dd rect = GetRelationshipRect(rel);
    double cx = rect.x + rect.width / 2.0;
    double cy = rect.y + rect.height / 2.0;
    double a = rect.width / 2.0;
    double b = rect.height / 2.0;
    if (a < 1e-6 || b < 1e-6) return false;
    return (std::abs(worldPos.x - cx) / a + std::abs(worldPos.y - cy) / b) <= 1.0;
}

bool UltraCanvasERDiagram::PointInEllipse(const Rect2Dd& rect, const Point2Dd& worldPos) const {
    double cx = rect.x + rect.width / 2.0;
    double cy = rect.y + rect.height / 2.0;
    double a = rect.width / 2.0;
    double b = rect.height / 2.0;
    if (a < 1e-6 || b < 1e-6) return false;
    double dx = (worldPos.x - cx) / a;
    double dy = (worldPos.y - cy) / b;
    return (dx * dx + dy * dy) <= 1.0;
}

// Topmost first: attributes, relationships, entities, annotations.
ERHitResult UltraCanvasERDiagram::HitTest(const Point2Di& screenPos) const {
    Point2Dd worldPos = ScreenToWorld(screenPos);
    ERHitResult result;

    if (notation != ERNotation::CrowsFoot) {
        for (auto it = entityOrder.rbegin(); it != entityOrder.rend(); ++it) {
            auto entityIt = entities.find(*it);
            if (entityIt == entities.end()) continue;
            for (const auto& attribute : entityIt->second.attributes) {
                if (!attribute.visible) continue;
                if (PointInEllipse(Rect2Dd(attribute.x, attribute.y,
                                           attribute.width, attribute.height), worldPos)) {
                    result.kind = ERHitKind::Attribute;
                    result.ownerId = *it;
                    result.elementId = attribute.id;
                    return result;
                }
                for (const auto& child : attribute.children) {
                    if (PointInEllipse(Rect2Dd(child.x, child.y, child.width, child.height),
                                       worldPos)) {
                        result.kind = ERHitKind::Attribute;
                        result.ownerId = *it;
                        result.elementId = child.id;
                        return result;
                    }
                }
            }
        }
        for (auto it = relationshipOrder.rbegin(); it != relationshipOrder.rend(); ++it) {
            auto relIt = relationships.find(*it);
            if (relIt == relationships.end()) continue;
            if (PointInDiamond(relIt->second, worldPos)) {
                result.kind = ERHitKind::Relationship;
                result.ownerId = *it;
                result.elementId = *it;
                return result;
            }
        }
    }

    for (auto it = entityOrder.rbegin(); it != entityOrder.rend(); ++it) {
        auto entityIt = entities.find(*it);
        if (entityIt == entities.end()) continue;
        if (PointInEntity(entityIt->second, worldPos)) {
            result.kind = ERHitKind::Entity;
            result.ownerId = *it;
            result.elementId = *it;
            return result;
        }
    }

    // Crow's foot has no diamond, so relationships are hit through their edge.
    if (notation == ERNotation::CrowsFoot) {
        double tolerance = ER_CONNECTOR_HIT_TOLERANCE / std::max(zoomLevel, 0.01);
        for (auto it = relationshipOrder.rbegin(); it != relationshipOrder.rend(); ++it) {
            auto relIt = relationships.find(*it);
            if (relIt == relationships.end() || relIt->second.legs.size() != 2) continue;
            const auto* a = GetEntity(relIt->second.legs[0].entityId);
            const auto* b = GetEntity(relIt->second.legs[1].entityId);
            if (!a || !b || a->id == b->id) continue;
            Point2Dd from = GetEntityBorderPoint(*a, GetEntityCenter(*b));
            Point2Dd to = GetEntityBorderPoint(*b, GetEntityCenter(*a));
            std::vector<Point2Dd> path;
            BuildConnectorPath(from, to, connectorStyle, path);
            for (size_t i = 0; i + 1 < path.size(); ++i) {
                if (DistanceToSegment(worldPos, path[i], path[i + 1]) <= tolerance) {
                    result.kind = ERHitKind::Relationship;
                    result.ownerId = *it;
                    result.elementId = *it;
                    return result;
                }
            }
        }
    }

    for (auto it = annotationOrder.rbegin(); it != annotationOrder.rend(); ++it) {
        auto annIt = annotations.find(*it);
        if (annIt == annotations.end()) continue;
        const auto& annotation = annIt->second;
        if (worldPos.x >= annotation.x && worldPos.x <= annotation.x + annotation.width &&
            worldPos.y >= annotation.y && worldPos.y <= annotation.y + annotation.height) {
            result.kind = ERHitKind::Annotation;
            result.ownerId = *it;
            result.elementId = *it;
            return result;
        }
    }
    return result;
}

int UltraCanvasERDiagram::FindControlButtonAt(const Point2Di& screenPos) const {
    if (!controlsConfig.visible) return -1;

    std::vector<int> buttons;
    if (controlsConfig.showZoom) { buttons.push_back(0); buttons.push_back(1); }
    if (controlsConfig.showFit)  buttons.push_back(2);
    if (controlsConfig.showLock) buttons.push_back(3);
    if (buttons.empty()) return -1;

    double count = static_cast<double>(buttons.size());
    double width = controlsConfig.buttonSize + controlsConfig.gap * 2.0;
    double height = count * controlsConfig.buttonSize + (count + 1.0) * controlsConfig.gap;
    Rect2Dd panel = GetPanelRect(controlsConfig.position, width, height,
                                 controlsConfig.padding);

    double buttonY = panel.y + controlsConfig.gap;
    for (size_t i = 0; i < buttons.size(); ++i) {
        Rect2Dd button(panel.x + controlsConfig.gap, buttonY,
                       controlsConfig.buttonSize, controlsConfig.buttonSize);
        if (screenPos.x >= button.x && screenPos.x <= button.x + button.width &&
            screenPos.y >= button.y && screenPos.y <= button.y + button.height) {
            return static_cast<int>(i);
        }
        buttonY += controlsConfig.buttonSize + controlsConfig.gap;
    }
    return -1;
}

// =============================================================================
// EVENTS
// =============================================================================

bool UltraCanvasERDiagram::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;

    isMultiSelectKeyHeld = event.shift;

    switch (event.type) {
        case UCEventType::MouseDown:  return HandleMouseDown(event);
        case UCEventType::MouseUp:    return HandleMouseUp(event);
        case UCEventType::MouseMove:  return HandleMouseMove(event);
        case UCEventType::MouseWheel: return HandleMouseWheel(event);
        case UCEventType::MouseDoubleClick: {
            Point2Di pos(event.pointer.x, event.pointer.y);
            if (!Contains(Point2Df(static_cast<float>(pos.x),
                                   static_cast<float>(pos.y)))) {
                return false;
            }
            ERHitResult hit = HitTest(pos);
            if (hit.kind == ERHitKind::Entity && onEntityDoubleClick) {
                onEntityDoubleClick(hit.ownerId);
                return true;
            }
            return false;
        }
        case UCEventType::KeyDown:    return HandleKeyDown(event);
        default: break;
    }
    return UltraCanvasUIElement::OnEvent(event);
}

bool UltraCanvasERDiagram::HandleMouseDown(const UCEvent& event) {
    Point2Di mousePos(event.pointer.x, event.pointer.y);
    if (!Contains(Point2Df(static_cast<float>(mousePos.x),
                           static_cast<float>(mousePos.y)))) {
        return false;
    }

    lastMousePos = mousePos;
    dragStartPos = mousePos;

    // Controls overlay first - it sits above the canvas.
    int button = FindControlButtonAt(mousePos);
    if (button >= 0) {
        std::vector<int> buttons;
        if (controlsConfig.showZoom) { buttons.push_back(0); buttons.push_back(1); }
        if (controlsConfig.showFit)  buttons.push_back(2);
        if (controlsConfig.showLock) buttons.push_back(3);
        if (button < static_cast<int>(buttons.size())) {
            switch (buttons[button]) {
                case 0: ZoomIn(); break;
                case 1: ZoomOut(); break;
                case 2: FitView(); break;
                case 3: isInteractive = !isInteractive; RequestRedraw(); break;
                default: break;
            }
        }
        return true;
    }

    if (event.button == UCMouseButton::Right) {
        if (onCanvasRightClick) {
            Point2Dd worldPos = ScreenToWorld(mousePos);
            onCanvasRightClick(worldPos.x, worldPos.y);
        }
        return true;
    }

    if (!isInteractive && event.button != UCMouseButton::Middle) return false;

    if (event.button == UCMouseButton::Middle) {
        isPanning = true;
        return true;
    }

    ERHitResult hit = HitTest(mousePos);
    Point2Dd worldPos = ScreenToWorld(mousePos);

    switch (hit.kind) {
        case ERHitKind::Entity: {
            auto* entity = GetEntity(hit.ownerId);
            if (entity && entity->selectable) {
                if (!isMultiSelectKeyHeld && !entity->isSelected) DeselectAll();
                entity->isSelected = isMultiSelectKeyHeld ? !entity->isSelected : true;
                NotifySelectionChange();
            }
            if (entity && entity->draggable) {
                draggedNode = hit;
                isDraggingNode = true;
                dragNodeOffset = Point2Dd(worldPos.x - entity->x, worldPos.y - entity->y);
            }
            if (onEntityClick) onEntityClick(hit.ownerId);
            RequestRedraw();
            return true;
        }
        case ERHitKind::Relationship: {
            auto* rel = GetRelationship(hit.ownerId);
            if (rel && rel->selectable) {
                if (!isMultiSelectKeyHeld && !rel->isSelected) DeselectAll();
                rel->isSelected = isMultiSelectKeyHeld ? !rel->isSelected : true;
                NotifySelectionChange();
            }
            if (rel && notation != ERNotation::CrowsFoot) {
                draggedNode = hit;
                isDraggingNode = true;
                dragNodeOffset = Point2Dd(worldPos.x - rel->x, worldPos.y - rel->y);
            }
            if (onRelationshipClick) onRelationshipClick(hit.ownerId);
            RequestRedraw();
            return true;
        }
        case ERHitKind::Attribute: {
            auto* attribute = GetAttribute(hit.ownerId, hit.elementId);
            if (attribute) {
                draggedNode = hit;
                isDraggingNode = true;
                dragNodeOffset = Point2Dd(worldPos.x - attribute->x,
                                          worldPos.y - attribute->y);
            }
            if (onAttributeClick) onAttributeClick(hit.ownerId, hit.elementId);
            return true;
        }
        case ERHitKind::Annotation:
            draggedNode = hit;
            isDraggingNode = true;
            return true;
        case ERHitKind::NoHit:
            break;
    }

    // Empty canvas: rubber-band select, or pan.
    if (isMultiSelectKeyHeld || !panOnDrag) {
        isSelectingBox = true;
        selectionBoxStart = worldPos;
        selectionBoxEnd = worldPos;
    } else {
        isPanning = true;
        DeselectAll();
    }
    return true;
}

bool UltraCanvasERDiagram::HandleMouseMove(const UCEvent& event) {
    Point2Di mousePos(event.pointer.x, event.pointer.y);

    int hoverButton = FindControlButtonAt(mousePos);
    if (hoverButton != hoveredControlButton) {
        hoveredControlButton = hoverButton;
        RequestRedraw();
    }

    if (isPanning) {
        panOffset.x += mousePos.x - lastMousePos.x;
        panOffset.y += mousePos.y - lastMousePos.y;
        lastMousePos = mousePos;
        NotifyViewportChange();
        RequestRedraw();
        return true;
    }

    if (isSelectingBox) {
        selectionBoxEnd = ScreenToWorld(mousePos);
        lastMousePos = mousePos;
        RequestRedraw();
        return true;
    }

    if (isDraggingNode && draggedNode.IsHit()) {
        Point2Dd worldPos = ScreenToWorld(mousePos);
        Point2Dd target = SnapPoint(Point2Dd(worldPos.x - dragNodeOffset.x,
                                             worldPos.y - dragNodeOffset.y));
        switch (draggedNode.kind) {
            case ERHitKind::Entity: {
                auto* entity = GetEntity(draggedNode.ownerId);
                if (entity) {
                    MoveEntityBy(*entity, target.x - entity->x, target.y - entity->y);
                    if (onEntityDrag) onEntityDrag(entity->id, entity->x, entity->y);
                }
                break;
            }
            case ERHitKind::Relationship: {
                auto* rel = GetRelationship(draggedNode.ownerId);
                if (rel) {
                    rel->x = target.x;
                    rel->y = target.y;
                    rel->hasPlacement = true;
                }
                break;
            }
            case ERHitKind::Attribute: {
                auto* attribute = GetAttribute(draggedNode.ownerId, draggedNode.elementId);
                if (attribute) {
                    attribute->x = target.x;
                    attribute->y = target.y;
                    attribute->hasPlacement = true;
                }
                break;
            }
            case ERHitKind::Annotation: {
                auto* annotation = GetAnnotation(draggedNode.ownerId);
                if (annotation) {
                    annotation->x = target.x;
                    annotation->y = target.y;
                }
                break;
            }
            case ERHitKind::NoHit:
                break;
        }
        lastMousePos = mousePos;
        RequestRedraw();
        return true;
    }

    // Hover tracking
    if (!Contains(Point2Df(static_cast<float>(mousePos.x),
                           static_cast<float>(mousePos.y)))) {
        if (hoveredNode.IsHit()) {
            hoveredNode = ERHitResult();
            RequestRedraw();
        }
        return false;
    }

    ERHitResult hit = HitTest(mousePos);
    if (hit.kind != hoveredNode.kind || hit.ownerId != hoveredNode.ownerId ||
        hit.elementId != hoveredNode.elementId) {
        hoveredNode = hit;
        SetMouseCursor(hit.IsHit() ? UCMouseCursor::SizeAll : UCMouseCursor::Default);
        RequestRedraw();
    }
    lastMousePos = mousePos;
    return hit.IsHit();
}

bool UltraCanvasERDiagram::HandleMouseUp(const UCEvent& event) {
    if (isSelectingBox) {
        Rect2Dd box(std::min(selectionBoxStart.x, selectionBoxEnd.x),
                    std::min(selectionBoxStart.y, selectionBoxEnd.y),
                    std::abs(selectionBoxEnd.x - selectionBoxStart.x),
                    std::abs(selectionBoxEnd.y - selectionBoxStart.y));
        if (!isMultiSelectKeyHeld) DeselectAll();

        for (auto& entry : entities) {
            Rect2Dd rect = GetEntityRect(entry.second);
            if (rect.x >= box.x && rect.y >= box.y &&
                rect.x + rect.width <= box.x + box.width &&
                rect.y + rect.height <= box.y + box.height) {
                entry.second.isSelected = true;
            }
        }
        for (auto& entry : relationships) {
            Rect2Dd rect = GetRelationshipRect(entry.second);
            if (rect.x >= box.x && rect.y >= box.y &&
                rect.x + rect.width <= box.x + box.width &&
                rect.y + rect.height <= box.y + box.height) {
                entry.second.isSelected = true;
            }
        }
        isSelectingBox = false;
        NotifySelectionChange();
        RequestRedraw();
        return true;
    }

    bool consumed = isPanning || isDraggingNode;
    isPanning = false;
    isDraggingNode = false;
    draggedNode = ERHitResult();
    return consumed;
}

bool UltraCanvasERDiagram::HandleMouseWheel(const UCEvent& event) {
    if (!isInteractive || !zoomOnScroll) return false;
    Point2Di cursor(event.pointer.x, event.pointer.y);
    if (!Contains(Point2Df(static_cast<float>(cursor.x),
                           static_cast<float>(cursor.y)))) {
        return false;
    }

    if (!zoomAnim.IsBound()) {
        zoomAnim.Bind([this](double f) { ApplyZoomFactorAtCursor(f, zoomCursor); },
                      [this] { NotifyViewportChange(); RequestRedraw(); });
    }
    zoomCursor = cursor;
    zoomAnim.ZoomBy((event.wheelDelta > 0) ? 1.1 : (1.0 / 1.1),
                    zoomLevel, minZoom, maxZoom);
    return true;
}

// One zoom step about the cursor: the world point under it stays put. A wheel
// notch is eased in as a run of these (UltraCanvasSmoothZoom), and applying them
// in a row about the same cursor is exactly applying their product once.
void UltraCanvasERDiagram::ApplyZoomFactorAtCursor(double factor,
                                                   const Point2Di& cursor) {
    double newZoom = std::clamp(zoomLevel * factor, minZoom, maxZoom);
    if (std::abs(newZoom - zoomLevel) < 1e-9) return;

    Point2Dd worldUnderCursor = ScreenToWorld(cursor);
    zoomLevel = newZoom;
    panOffset.x = cursor.x - worldUnderCursor.x * zoomLevel;
    panOffset.y = cursor.y - worldUnderCursor.y * zoomLevel;
}

bool UltraCanvasERDiagram::HandleKeyDown(const UCEvent& event) {
    if (!isInteractive) return false;

    if ((event.ctrl || event.meta) && event.virtualKey == UCKeys::A) {
        SelectAll();
        return true;
    }
    if (event.virtualKey == UCKeys::Escape) {
        DeselectAll();
        isSelectingBox = false;
        isDraggingNode = false;
        return true;
    }
    if (event.virtualKey == UCKeys::Delete || event.virtualKey == UCKeys::Backspace) {
        for (const auto& id : GetSelectedRelationshipIds()) RemoveRelationship(id);
        for (const auto& id : GetSelectedEntityIds()) RemoveEntity(id);
        NotifySelectionChange();
        return true;
    }

    // Arrow keys nudge the selection.
    double step = event.shift ? 10.0 : 1.0;
    double dx = 0.0, dy = 0.0;
    if (event.virtualKey == UCKeys::Left)  dx = -step;
    if (event.virtualKey == UCKeys::Right) dx = step;
    if (event.virtualKey == UCKeys::Up)    dy = -step;
    if (event.virtualKey == UCKeys::Down)  dy = step;
    if (dx == 0.0 && dy == 0.0) return false;

    for (const auto& id : GetSelectedEntityIds()) {
        auto* entity = GetEntity(id);
        if (entity) MoveEntityBy(*entity, dx, dy);
    }
    for (const auto& id : GetSelectedRelationshipIds()) {
        auto* rel = GetRelationship(id);
        if (rel) { rel->x += dx; rel->y += dy; rel->hasPlacement = true; }
    }
    RequestRedraw();
    return true;
}

// =============================================================================
// MISC
// =============================================================================

// Attributes are positioned in world space, so they have to travel with the
// entity they belong to.
void UltraCanvasERDiagram::MoveEntityBy(ERDiagramEntity& entity, double dx, double dy) {
    if (std::abs(dx) < 1e-9 && std::abs(dy) < 1e-9) return;
    entity.x += dx;
    entity.y += dy;
    for (auto& attribute : entity.attributes) {
        attribute.x += dx;
        attribute.y += dy;
        for (auto& child : attribute.children) {
            child.x += dx;
            child.y += dy;
        }
    }
}

void UltraCanvasERDiagram::NotifySelectionChange() {
    if (onSelectionChange) {
        onSelectionChange(GetSelectedEntityIds(), GetSelectedRelationshipIds());
    }
}

void UltraCanvasERDiagram::NotifyViewportChange() {
    if (onViewportChange) onViewportChange(zoomLevel, panOffset.x, panOffset.y);
}

void UltraCanvasERDiagram::NotifyModelChanged() {
    if (onModelChanged) onModelChanged();
}

} // namespace UltraCanvas
