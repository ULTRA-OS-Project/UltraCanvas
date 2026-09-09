// Plugins/Diagrams/UltraCanvasSequenceDiagram.cpp
// UML sequence diagram element
// Version: 1.0.0
// Last Modified: 2026-08-07
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasSequenceDiagram.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace UltraCanvas {

namespace {

const UCDashPattern& LifelineDash() {
    static const UCDashPattern pattern({6.0, 5.0}, 0.0);
    return pattern;
}

const UCDashPattern& ReturnDash() {
    static const UCDashPattern pattern({5.0, 4.0}, 0.0);
    return pattern;
}

const UCDashPattern& SeparatorDash() {
    static const UCDashPattern pattern({4.0, 4.0}, 0.0);
    return pattern;
}

Color UnpackColor(unsigned int packed) {
    return Color(static_cast<unsigned char>((packed >> 24) & 0xFF),
                 static_cast<unsigned char>((packed >> 16) & 0xFF),
                 static_cast<unsigned char>((packed >> 8) & 0xFF),
                 static_cast<unsigned char>(packed & 0xFF));
}

unsigned int PackColor(const Color& color) {
    return (static_cast<unsigned int>(color.r) << 24) |
           (static_cast<unsigned int>(color.g) << 16) |
           (static_cast<unsigned int>(color.b) << 8) |
           static_cast<unsigned int>(color.a);
}

// Vertical reach of a self-message's loop-back.
constexpr double kSelfLoopHeight = 18.0;
// Horizontal clearance between a head box edge and its neighbour.
constexpr double kHeadClearance = 18.0;

} // namespace

// =============================================================================
// CONSTRUCTION
// =============================================================================

UltraCanvasSequenceDiagram::UltraCanvasSequenceDiagram(const std::string& id,
                                                       int x, int y, int width, int height)
    : UltraCanvasUIElement(id, static_cast<float>(x), static_cast<float>(y),
                           static_cast<float>(width), static_cast<float>(height)) {
    style = SequenceDiagramStyles::CreateForTheme(SequenceDiagramTheme::Default);
}

std::shared_ptr<UltraCanvasSequenceDiagram> CreateSequenceDiagramElement(
        const std::string& id, int x, int y, int width, int height) {
    return std::make_shared<UltraCanvasSequenceDiagram>(id, x, y, width, height);
}

std::shared_ptr<UltraCanvasSequenceDiagram> CreateSequenceDiagramWithModel(
        const std::string& id, int x, int y, int width, int height,
        const UltraCanvasSequenceModel& model) {
    auto element = std::make_shared<UltraCanvasSequenceDiagram>(id, x, y, width, height);
    element->SetModel(model);
    return element;
}

// =============================================================================
// MODEL
// =============================================================================

void UltraCanvasSequenceDiagram::SetModel(const UltraCanvasSequenceModel& value) {
    model = value;
    MarkModelChanged();
}

void UltraCanvasSequenceDiagram::Clear() {
    model.Clear();
    DeselectAll();
    MarkModelChanged();
}

std::string UltraCanvasSequenceDiagram::AddLifeline(const std::string& name,
                                                    SequenceLifelineKind kind) {
    std::string id = model.AddLifeline(name, kind);
    MarkModelChanged();
    return id;
}

std::string UltraCanvasSequenceDiagram::AddActor(const std::string& name) {
    std::string id = model.AddActor(name);
    MarkModelChanged();
    return id;
}

std::string UltraCanvasSequenceDiagram::AddDatabase(const std::string& name) {
    std::string id = model.AddDatabase(name);
    MarkModelChanged();
    return id;
}

size_t UltraCanvasSequenceDiagram::AddMessage(const std::string& from, const std::string& to,
                                              const std::string& label,
                                              SequenceMessageKind kind) {
    size_t index = model.AddMessage(from, to, label, kind);
    MarkModelChanged();
    return index;
}

size_t UltraCanvasSequenceDiagram::AddReturnMessage(const std::string& from,
                                                    const std::string& to,
                                                    const std::string& label) {
    size_t index = model.AddReturnMessage(from, to, label);
    MarkModelChanged();
    return index;
}

size_t UltraCanvasSequenceDiagram::AddSelfMessage(const std::string& lifeline,
                                                  const std::string& label) {
    size_t index = model.AddSelfMessage(lifeline, label);
    MarkModelChanged();
    return index;
}

std::string UltraCanvasSequenceDiagram::AddLoop(size_t firstMessage, size_t lastMessage,
                                                const std::string& guard) {
    std::string id = model.AddLoop(firstMessage, lastMessage, guard);
    MarkModelChanged();
    return id;
}

std::string UltraCanvasSequenceDiagram::AddAlt(size_t firstMessage, size_t lastMessage,
                                               const std::string& firstGuard) {
    std::string id = model.AddAlt(firstMessage, lastMessage, firstGuard);
    MarkModelChanged();
    return id;
}

void UltraCanvasSequenceDiagram::MarkModelChanged() {
    layoutDirty = true;
    RequestRedraw();
}

// =============================================================================
// PRESENTATION
// =============================================================================

void UltraCanvasSequenceDiagram::SetTheme(SequenceDiagramTheme value) {
    theme = value;
    style = SequenceDiagramStyles::CreateForTheme(value);
    MarkModelChanged();
}

void UltraCanvasSequenceDiagram::SetStyle(const SequenceDiagramStyle& value) {
    style = value;
    MarkModelChanged();
}

void UltraCanvasSequenceDiagram::SetTitle(const std::string& value) {
    model.SetTitle(value);
    MarkModelChanged();
}

void UltraCanvasSequenceDiagram::SetShowFrame(bool value) {
    showFrame = value;
    MarkModelChanged();
}

void UltraCanvasSequenceDiagram::SetShowFootBoxes(bool value) {
    style.showFootBoxes = value;
    MarkModelChanged();
}

void UltraCanvasSequenceDiagram::SetNumbering(SequenceNumbering value) {
    numbering = value;
    MarkModelChanged();
}

void UltraCanvasSequenceDiagram::ApplyPaletteByLifeline() {
    const std::vector<Color>& palette = SequenceDiagramStyles::GetLifelinePalette();
    size_t position = 0;
    for (const auto& lifeline : model.Lifelines()) {
        model.SetLifelineColor(lifeline.id, PackColor(palette[position % palette.size()]));
        ++position;
    }
    MarkModelChanged();
}

// =============================================================================
// VIEW
// =============================================================================

void UltraCanvasSequenceDiagram::SetZoomLevel(double zoom) {
    zoomLevel = std::clamp(zoom, 0.1, 10.0);
    RequestRedraw();
}

void UltraCanvasSequenceDiagram::SetPanOffset(double x, double y) {
    panOffset = Point2Dd(x, y);
    RequestRedraw();
}

void UltraCanvasSequenceDiagram::FitView() {
    fitPending = true;
    RequestRedraw();
}

void UltraCanvasSequenceDiagram::ResetView() {
    zoomLevel = 1.0;
    panOffset = Point2Dd(0, 0);
    fitPending = false;
    RequestRedraw();
}

void UltraCanvasSequenceDiagram::SetInteractive(bool value) {
    interactive = value;
}

void UltraCanvasSequenceDiagram::ApplyFit() {
    Rect2Df bounds = GetLocalBounds();
    if (contentBounds.width <= 0.0 || contentBounds.height <= 0.0 ||
        bounds.width <= 0 || bounds.height <= 0) {
        return;
    }
    double scaleX = static_cast<double>(bounds.width) / contentBounds.width;
    double scaleY = static_cast<double>(bounds.height) / contentBounds.height;
    zoomLevel = std::clamp(std::min(scaleX, scaleY) * 0.95, 0.1, 1.5);

    double contentCentreX = contentBounds.x + contentBounds.width * 0.5;
    double contentCentreY = contentBounds.y + contentBounds.height * 0.5;
    panOffset.x = bounds.width * 0.5 - contentCentreX * zoomLevel;
    panOffset.y = bounds.height * 0.5 - contentCentreY * zoomLevel;
}

Point2Dd UltraCanvasSequenceDiagram::ScreenToWorld(const Point2Di& screenPos) const {
    return Point2Dd((screenPos.x - panOffset.x) / zoomLevel,
                    (screenPos.y - panOffset.y) / zoomLevel);
}

// =============================================================================
// SELECTION
// =============================================================================

void UltraCanvasSequenceDiagram::SelectMessage(int index) {
    selectedMessage = (index >= 0 && index < static_cast<int>(model.MessageCount()))
                          ? index : -1;
    selectedLifelineId.clear();
    RequestRedraw();
}

void UltraCanvasSequenceDiagram::SelectLifeline(const std::string& idOrName) {
    const SequenceLifeline* lifeline = model.GetLifeline(idOrName);
    selectedLifelineId = lifeline ? lifeline->id : std::string();
    selectedMessage = -1;
    RequestRedraw();
}

void UltraCanvasSequenceDiagram::DeselectAll() {
    selectedMessage = -1;
    selectedLifelineId.clear();
    RequestRedraw();
}

// =============================================================================
// LAYOUT
// =============================================================================

int UltraCanvasSequenceDiagram::LifelinePosition(const std::string& lifelineId) const {
    const auto& lifelines = model.Lifelines();
    for (size_t i = 0; i < lifelines.size(); ++i) {
        if (lifelines[i].id == lifelineId) return static_cast<int>(i);
    }
    return -1;
}

Color UltraCanvasSequenceDiagram::HeadColorFor(const SequenceLifeline& lifeline,
                                               size_t position) const {
    if (lifeline.hasHeadColor) return UnpackColor(lifeline.headColor);
    if (theme == SequenceDiagramTheme::Colorful) {
        const std::vector<Color>& palette = SequenceDiagramStyles::GetLifelinePalette();
        return palette[position % palette.size()];
    }
    return style.headColor;
}

void UltraCanvasSequenceDiagram::SolveHorizontal(IRenderContext* ctx,
                                                 std::vector<double>& centers,
                                                 std::vector<Rect2Dd>& headSizes) {
    const auto& lifelines = model.Lifelines();
    centers.assign(lifelines.size(), 0.0);
    headSizes.assign(lifelines.size(), Rect2Dd(0, 0, 0, 0));
    if (lifelines.empty()) return;

    ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
    ctx->SetFontSize(style.headFontSize);
    double lineHeight = static_cast<double>(ctx->GetTextLineDimensions("Mg").height);

    for (size_t i = 0; i < lifelines.size(); ++i) {
        const SequenceLifeline& lifeline = lifelines[i];
        double nameWidth = static_cast<double>(
            ctx->GetTextLineDimensions(lifeline.name).width);
        double stereotypeWidth = 0.0;
        double stereotypeHeight = 0.0;
        if (!lifeline.stereotype.empty()) {
            std::string decorated = "\xC2\xAB" + lifeline.stereotype + "\xC2\xBB";
            stereotypeWidth = static_cast<double>(
                ctx->GetTextLineDimensions(decorated).width);
            stereotypeHeight = lineHeight;
        }
        double textWidth = std::max(nameWidth, stereotypeWidth);

        double width = 0.0;
        double height = 0.0;
        switch (lifeline.kind) {
            case SequenceLifelineKind::Actor:
                width = std::max(46.0, textWidth + 6.0);
                height = 38.0 + stereotypeHeight + lineHeight + 4.0;
                break;
            case SequenceLifelineKind::Database:
                width = std::max({56.0, textWidth + 2.0 * style.headPaddingX});
                height = 16.0 + stereotypeHeight + lineHeight + 2.0 * style.headPaddingY;
                break;
            case SequenceLifelineKind::Boundary:
            case SequenceLifelineKind::Control:
            case SequenceLifelineKind::Entity:
                width = std::max(44.0, textWidth + 6.0);
                height = 32.0 + stereotypeHeight + lineHeight + 4.0;
                break;
            case SequenceLifelineKind::Object:
                width = std::max(style.minHeadWidth, textWidth + 2.0 * style.headPaddingX);
                height = stereotypeHeight + lineHeight + 2.0 * style.headPaddingY;
                break;
        }
        headSizes[i] = Rect2Dd(0, 0, width, height);
    }

    // Extra room on the left when a note hangs off the leftmost lifeline.
    double leftMargin = style.sideMargin;
    for (const auto& note : model.Notes()) {
        if (note.placement == SequenceNotePlacement::LeftOf &&
            LifelinePosition(note.lifelineId) == 0) {
            leftMargin = std::max(leftMargin, style.noteMaxWidth + style.sideMargin);
        }
    }
    // And room for fragment frames around everything.
    size_t fragmentLevels = 0;
    for (const auto& fragment : model.Fragments()) {
        size_t depth = 1;
        for (const auto& other : model.Fragments()) {
            if (&other == &fragment) continue;
            if (other.firstMessage <= fragment.firstMessage &&
                fragment.lastMessage <= other.lastMessage) {
                ++depth;
            }
        }
        fragmentLevels = std::max(fragmentLevels, depth);
    }
    leftMargin += static_cast<double>(fragmentLevels) * style.fragmentPadding;

    // Initial spacing from head widths, then message-label constraints widen
    // the gaps. Gaps only ever grow, so earlier constraints stay satisfied.
    std::vector<double> gaps(lifelines.size() > 0 ? lifelines.size() - 1 : 0, 0.0);
    for (size_t i = 0; i + 1 < lifelines.size(); ++i) {
        double byHeads = headSizes[i].width * 0.5 + headSizes[i + 1].width * 0.5 +
                         kHeadClearance;
        gaps[i] = std::max(style.minLifelineGap, byHeads);
    }

    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(style.messageFontSize);

    std::vector<std::string> numbers = model.ComputeMessageNumbers(
        numbering == SequenceNumbering::Hierarchical);

    struct Constraint {
        size_t from;
        size_t to;
        double required;
    };
    std::vector<Constraint> constraints;

    const auto& messages = model.Messages();
    for (size_t k = 0; k < messages.size(); ++k) {
        const SequenceMessage& message = messages[k];
        std::string label = message.label;
        if (numbering != SequenceNumbering::Hidden) {
            label = numbers[k] + (label.empty() ? "" : ": " + label);
        }
        double labelWidth = label.empty()
                                ? 0.0
                                : static_cast<double>(ctx->GetTextLineDimensions(label).width);

        int fromPos = LifelinePosition(message.fromId);
        int toPos = LifelinePosition(message.toId);

        if (message.IsSelfMessage() && fromPos >= 0) {
            // The loop-back and its label live in the gap to the right.
            if (static_cast<size_t>(fromPos) + 1 < lifelines.size()) {
                constraints.push_back({static_cast<size_t>(fromPos),
                                       static_cast<size_t>(fromPos) + 1,
                                       style.selfMessageWidth + labelWidth +
                                           2.0 * style.labelPadding});
            }
            continue;
        }
        if (fromPos < 0 || toPos < 0 || fromPos == toPos) continue;

        size_t left = static_cast<size_t>(std::min(fromPos, toPos));
        size_t right = static_cast<size_t>(std::max(fromPos, toPos));
        double required = labelWidth + 2.0 * style.labelPadding;
        if (message.kind == SequenceMessageKind::Create) {
            required += headSizes[static_cast<size_t>(toPos)].width * 0.5;
        }
        constraints.push_back({left, right, required});
    }

    std::stable_sort(constraints.begin(), constraints.end(),
                     [](const Constraint& a, const Constraint& b) {
                         return (a.to - a.from) < (b.to - b.from);
                     });
    for (const Constraint& constraint : constraints) {
        double current = 0.0;
        for (size_t i = constraint.from; i < constraint.to; ++i) current += gaps[i];
        if (current < constraint.required) {
            double addPerGap = (constraint.required - current) /
                               static_cast<double>(constraint.to - constraint.from);
            for (size_t i = constraint.from; i < constraint.to; ++i) gaps[i] += addPerGap;
        }
    }

    centers[0] = leftMargin + headSizes[0].width * 0.5;
    for (size_t i = 1; i < lifelines.size(); ++i) {
        centers[i] = centers[i - 1] + gaps[i - 1];
    }
}

void UltraCanvasSequenceDiagram::WalkVertical(IRenderContext* ctx,
                                              const std::vector<double>& centers,
                                              std::vector<double>& messageYs,
                                              std::vector<double>& selfBottoms,
                                              double& bottomY) {
    (void)centers;
    const auto& messages = model.Messages();
    const auto& fragments = model.Fragments();
    messageYs.assign(messages.size(), 0.0);
    selfBottoms.assign(messages.size(), 0.0);

    // Vertical events per message index, mirroring the text-export plan.
    std::map<size_t, std::vector<size_t>> opensAt;     // fragment indices, outer first
    std::map<size_t, std::vector<size_t>> closesAt;    // inner first
    struct SeparatorEvent { size_t fragmentIndex; const SequenceFragmentOperand* operand; };
    std::map<size_t, std::vector<SeparatorEvent>> separatorsAt;

    auto span = [&](size_t index) {
        return fragments[index].lastMessage - fragments[index].firstMessage;
    };
    for (size_t f = 0; f < fragments.size(); ++f) {
        if (messages.empty() || fragments[f].lastMessage >= messages.size()) continue;
        opensAt[fragments[f].firstMessage].push_back(f);
        closesAt[fragments[f].lastMessage].push_back(f);
        for (size_t o = 1; o < fragments[f].operands.size(); ++o) {
            separatorsAt[fragments[f].operands[o].firstMessage].push_back(
                {f, &fragments[f].operands[o]});
        }
    }
    for (auto& [index, list] : opensAt) {
        std::stable_sort(list.begin(), list.end(),
                         [&](size_t a, size_t b) { return span(a) > span(b); });
    }
    for (auto& [index, list] : closesAt) {
        std::stable_sort(list.begin(), list.end(),
                         [&](size_t a, size_t b) { return span(a) < span(b); });
    }

    fragmentGeometry.assign(fragments.size(), FragmentGeometry());
    for (size_t f = 0; f < fragments.size(); ++f) {
        fragmentGeometry[f].fragmentId = fragments[f].id;
        fragmentGeometry[f].headerText = fragments[f].Keyword();
        if (!fragments[f].label.empty()) {
            fragmentGeometry[f].headerText += " " + fragments[f].label;
        }
        if (!fragments[f].operands.front().guard.empty()) {
            fragmentGeometry[f].guardText = "[" + fragments[f].operands.front().guard + "]";
        }
    }

    // Note anchors: -1 renders above the first message.
    std::map<int, std::vector<size_t>> notesAt;
    const auto& notes = model.Notes();
    noteGeometry.clear();
    for (size_t n = 0; n < notes.size(); ++n) {
        if (notes[n].afterMessage >= static_cast<int>(messages.size())) continue;
        notesAt[notes[n].afterMessage].push_back(n);
    }

    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(style.messageFontSize);
    double labelHeight = static_cast<double>(ctx->GetTextLineDimensions("Mg").height);

    std::vector<std::string> numbers = model.ComputeMessageNumbers(
        numbering == SequenceNumbering::Hierarchical);

    double headBottom = 0.0;
    for (const auto& geometry : lifelineGeometry) {
        headBottom = std::max(headBottom, geometry.headRect.y + geometry.headRect.height);
    }
    double y = headBottom + style.headGap;

    auto placeNotes = [&](int anchor) {
        auto notesHere = notesAt.find(anchor);
        if (notesHere == notesAt.end()) return;
        ctx->SetFontSize(style.noteFontSize);
        double noteLineHeight = static_cast<double>(ctx->GetTextLineDimensions("Mg").height);
        for (size_t noteIndex : notesHere->second) {
            const SequenceNote& note = notes[noteIndex];
            NoteGeometry geometry;
            geometry.noteIndex = noteIndex;

            double maxTextWidth = style.noteMaxWidth - 2.0 * style.notePadding;
            geometry.lines = WrapNoteText(ctx, note.text, maxTextWidth);
            double textWidth = 0.0;
            for (const auto& line : geometry.lines) {
                textWidth = std::max(textWidth, static_cast<double>(
                    ctx->GetTextLineDimensions(line).width));
            }
            double height = static_cast<double>(geometry.lines.size()) * noteLineHeight +
                            2.0 * style.notePadding;
            double width = textWidth + 2.0 * style.notePadding + 10.0;   // dog-ear room

            int position = LifelinePosition(note.lifelineId);
            double anchorX = position >= 0 ? centers[static_cast<size_t>(position)] : 0.0;
            double x = anchorX + style.activationWidth + 10.0;
            if (note.placement == SequenceNotePlacement::LeftOf) {
                x = anchorX - style.activationWidth - 10.0 - width;
            } else if (note.placement == SequenceNotePlacement::Over) {
                double otherX = anchorX;
                int otherPosition = LifelinePosition(note.secondLifelineId);
                if (otherPosition >= 0) otherX = centers[static_cast<size_t>(otherPosition)];
                double spanLeft = std::min(anchorX, otherX);
                double spanRight = std::max(anchorX, otherX);
                width = std::max(width, spanRight - spanLeft + 40.0);
                x = (spanLeft + spanRight - width) * 0.5;
            }

            geometry.rect = Rect2Dd(x, y, width, height);
            noteGeometry.push_back(std::move(geometry));
            y += height + 10.0;
        }
        ctx->SetFontSize(style.messageFontSize);
    };

    placeNotes(-1);

    for (size_t k = 0; k < messages.size(); ++k) {
        auto opens = opensAt.find(k);
        if (opens != opensAt.end()) {
            for (size_t f : opens->second) {
                fragmentGeometry[f].rect.y = y;
                y += style.fragmentHeaderHeight + 4.0;
            }
        }
        auto separators = separatorsAt.find(k);
        if (separators != separatorsAt.end()) {
            for (const SeparatorEvent& event : separators->second) {
                y += style.operandGap * 0.5;
                fragmentGeometry[event.fragmentIndex].separatorYs.push_back(y);
                fragmentGeometry[event.fragmentIndex].separatorGuards.push_back(
                    event.operand->guard.empty() ? std::string("[else]")
                                                 : "[" + event.operand->guard + "]");
                y += style.operandGap * 0.5 + labelHeight;
            }
        }

        const SequenceMessage& message = messages[k];
        double arrowY = y + labelHeight + 4.0;
        messageYs[k] = arrowY;

        if (message.IsSelfMessage()) {
            selfBottoms[k] = arrowY + kSelfLoopHeight;
            y = selfBottoms[k] + style.rowGap;
        } else if (message.kind == SequenceMessageKind::Create) {
            int toPos = LifelinePosition(message.toId);
            double headHalf = 0.0;
            if (toPos >= 0) {
                headHalf = lifelineGeometry[static_cast<size_t>(toPos)].headRect.height * 0.5;
            }
            y = arrowY + std::max(style.rowGap, headHalf + 8.0);
        } else {
            y = arrowY + style.rowGap;
        }

        placeNotes(static_cast<int>(k));

        auto closes = closesAt.find(k);
        if (closes != closesAt.end()) {
            for (size_t f : closes->second) {
                y += 10.0;
                fragmentGeometry[f].rect.height = y - fragmentGeometry[f].rect.y;
            }
        }
    }

    bottomY = y + style.bottomMargin;
    (void)numbers;
}

void UltraCanvasSequenceDiagram::PlaceActivations(const std::vector<double>& messageYs,
                                                  double bottomY, double headBottom) {
    activationGeometry.clear();
    for (const SequenceActivation& activation : model.ComputeActivations()) {
        int position = LifelinePosition(activation.lifelineId);
        if (position < 0) continue;
        const LifelineGeometry& lifeline = lifelineGeometry[static_cast<size_t>(position)];

        double top = activation.startMessage < 0
                         ? headBottom
                         : messageYs[static_cast<size_t>(activation.startMessage)];
        double bottom;
        if (activation.endMessage < 0) {
            bottom = lifeline.destroyed ? lifeline.bottomY : bottomY - style.bottomMargin;
        } else if (activation.endMessage == activation.startMessage) {
            bottom = top + kSelfLoopHeight;   // unmatched self-call stub
        } else {
            bottom = messageYs[static_cast<size_t>(activation.endMessage)];
        }
        if (bottom <= top) continue;

        ActivationGeometry geometry;
        geometry.lifelineId = activation.lifelineId;
        geometry.rect = Rect2Dd(
            lifeline.centerX - style.activationWidth * 0.5 +
                static_cast<double>(activation.depth) * style.activationNestingOffset,
            top, style.activationWidth, bottom - top);
        activationGeometry.push_back(std::move(geometry));
    }
}

double UltraCanvasSequenceDiagram::AttachX(const std::string& lifelineId, double y,
                                           bool towardsRight) const {
    int position = LifelinePosition(lifelineId);
    if (position < 0) return 0.0;
    double centerX = lifelineGeometry[static_cast<size_t>(position)].centerX;

    const ActivationGeometry* deepest = nullptr;
    for (const auto& activation : activationGeometry) {
        if (activation.lifelineId != lifelineId) continue;
        if (y < activation.rect.y - 0.5 ||
            y > activation.rect.y + activation.rect.height + 0.5) {
            continue;
        }
        if (!deepest || activation.rect.x > deepest->rect.x) deepest = &activation;
    }
    if (!deepest) return centerX;
    return towardsRight ? deepest->rect.x + deepest->rect.width : deepest->rect.x;
}

void UltraCanvasSequenceDiagram::PlaceFragmentHorizontals() {
    const auto& fragments = model.Fragments();
    const auto& messages = model.Messages();

    // Outer frames get more side padding than the frames they contain.
    std::vector<size_t> depth(fragments.size(), 0);
    size_t maxDepth = 0;
    for (size_t f = 0; f < fragments.size(); ++f) {
        for (size_t g = 0; g < fragments.size(); ++g) {
            if (f == g) continue;
            if (fragments[g].firstMessage <= fragments[f].firstMessage &&
                fragments[f].lastMessage <= fragments[g].lastMessage &&
                !(fragments[g].firstMessage == fragments[f].firstMessage &&
                  fragments[g].lastMessage == fragments[f].lastMessage && g > f)) {
                ++depth[f];
            }
        }
        maxDepth = std::max(maxDepth, depth[f]);
    }

    for (size_t f = 0; f < fragments.size(); ++f) {
        const SequenceFragment& fragment = fragments[f];
        if (messages.empty() || fragment.lastMessage >= messages.size()) continue;

        double minX = 1e18;
        double maxX = -1e18;
        for (size_t k = fragment.firstMessage; k <= fragment.lastMessage; ++k) {
            const MessageGeometry& geometry = messageGeometry[k];
            minX = std::min({minX, geometry.fromX, geometry.toX, geometry.labelRect.x});
            maxX = std::max({maxX, geometry.fromX, geometry.toX,
                             geometry.labelRect.x + geometry.labelRect.width});
            if (messages[k].IsSelfMessage()) {
                maxX = std::max(maxX, geometry.fromX + style.selfMessageWidth);
            }
        }
        if (minX > maxX) continue;

        double pad = style.fragmentPadding *
                     static_cast<double>(1 + maxDepth - depth[f]);
        fragmentGeometry[f].rect.x = minX - pad;
        fragmentGeometry[f].rect.width = (maxX + pad) - fragmentGeometry[f].rect.x;
    }
}

void UltraCanvasSequenceDiagram::RebuildLayout(IRenderContext* ctx) {
    lifelineGeometry.clear();
    messageGeometry.clear();
    activationGeometry.clear();
    fragmentGeometry.clear();
    noteGeometry.clear();
    contentBounds = Rect2Dd(0, 0, 0, 0);

    const auto& lifelines = model.Lifelines();
    const auto& messages = model.Messages();
    if (lifelines.empty()) {
        layoutDirty = false;
        return;
    }

    std::vector<double> centers;
    std::vector<Rect2Dd> headSizes;
    SolveHorizontal(ctx, centers, headSizes);

    // Created / destroyed lifelines.
    std::map<std::string, size_t> createdAt;
    std::map<std::string, size_t> destroyedAt;
    for (size_t k = 0; k < messages.size(); ++k) {
        if (messages[k].kind == SequenceMessageKind::Create &&
            !createdAt.count(messages[k].toId)) {
            createdAt[messages[k].toId] = k;
        }
        if (messages[k].kind == SequenceMessageKind::Destroy) {
            destroyedAt[messages[k].toId] = k;
        }
    }

    double headTop = style.topMargin + (showFrame ? 34.0 : 0.0);
    lifelineGeometry.assign(lifelines.size(), LifelineGeometry());
    for (size_t i = 0; i < lifelines.size(); ++i) {
        LifelineGeometry& geometry = lifelineGeometry[i];
        geometry.id = lifelines[i].id;
        geometry.centerX = centers[i];
        geometry.headRect = Rect2Dd(centers[i] - headSizes[i].width * 0.5, headTop,
                                    headSizes[i].width, headSizes[i].height);
        geometry.headColor = HeadColorFor(lifelines[i], i);
        geometry.destroyed = destroyedAt.count(lifelines[i].id) > 0;
    }

    // First vertical walk: created heads still sit at the top, which is fine —
    // only their own height feeds the row spacing, not their y.
    std::vector<double> messageYs;
    std::vector<double> selfBottoms;
    double bottomY = 0.0;
    WalkVertical(ctx, centers, messageYs, selfBottoms, bottomY);

    double headBottom = 0.0;
    for (size_t i = 0; i < lifelines.size(); ++i) {
        if (createdAt.count(lifelines[i].id)) continue;
        headBottom = std::max(headBottom,
                              lifelineGeometry[i].headRect.y +
                                  lifelineGeometry[i].headRect.height);
    }
    if (headBottom <= 0.0) headBottom = headTop;

    // Drop created heads onto their create-message row; set line extents.
    for (size_t i = 0; i < lifelines.size(); ++i) {
        LifelineGeometry& geometry = lifelineGeometry[i];
        auto created = createdAt.find(lifelines[i].id);
        if (created != createdAt.end()) {
            double arrowY = messageYs[created->second];
            geometry.headRect.y = arrowY - geometry.headRect.height * 0.5;
        }
        geometry.topY = geometry.headRect.y + geometry.headRect.height;

        auto destroyed = destroyedAt.find(lifelines[i].id);
        geometry.bottomY = destroyed != destroyedAt.end()
                               ? messageYs[destroyed->second]
                               : bottomY - style.bottomMargin;
        if (style.showFootBoxes && !geometry.destroyed) {
            geometry.footRect = Rect2Dd(geometry.headRect.x, geometry.bottomY,
                                        geometry.headRect.width,
                                        geometry.headRect.height);
            bottomY = std::max(bottomY,
                               geometry.footRect.y + geometry.footRect.height +
                                   style.bottomMargin * 0.5);
        }
    }

    PlaceActivations(messageYs, bottomY, headBottom);

    // Message geometry: endpoints attach to the deepest activation bar edge.
    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(style.messageFontSize);
    double labelHeight = static_cast<double>(ctx->GetTextLineDimensions("Mg").height);
    std::vector<std::string> numbers = model.ComputeMessageNumbers(
        numbering == SequenceNumbering::Hierarchical);

    messageGeometry.assign(messages.size(), MessageGeometry());
    for (size_t k = 0; k < messages.size(); ++k) {
        const SequenceMessage& message = messages[k];
        MessageGeometry& geometry = messageGeometry[k];
        geometry.index = k;
        geometry.y = messageYs[k];
        geometry.selfBottomY = selfBottoms[k];

        geometry.displayLabel = message.label;
        if (numbering != SequenceNumbering::Hidden) {
            geometry.displayLabel = numbers[k] +
                (message.label.empty() ? "" : ": " + message.label);
        }
        double labelWidth = geometry.displayLabel.empty()
                                ? 0.0
                                : static_cast<double>(
                                      ctx->GetTextLineDimensions(geometry.displayLabel).width);

        int fromPos = LifelinePosition(message.fromId);
        int toPos = LifelinePosition(message.toId);

        if (message.IsSelfMessage()) {
            geometry.fromX = AttachX(message.fromId, geometry.y, true);
            geometry.toX = AttachX(message.toId, geometry.selfBottomY, true);
            geometry.labelRect = Rect2Dd(
                geometry.fromX + style.selfMessageWidth + 8.0,
                (geometry.y + geometry.selfBottomY) * 0.5 - labelHeight * 0.5,
                labelWidth, labelHeight);
            continue;
        }

        switch (message.kind) {
            case SequenceMessageKind::Found: {
                double attach = AttachX(message.toId, geometry.y, false);
                geometry.toX = attach;
                geometry.fromX = attach - style.gateStubLength;
                break;
            }
            case SequenceMessageKind::Lost: {
                double attach = AttachX(message.fromId, geometry.y, true);
                geometry.fromX = attach;
                geometry.toX = attach + style.gateStubLength;
                break;
            }
            case SequenceMessageKind::Create: {
                bool rightwards = toPos > fromPos;
                geometry.fromX = AttachX(message.fromId, geometry.y, rightwards);
                const Rect2Dd& head = lifelineGeometry[static_cast<size_t>(toPos)].headRect;
                geometry.toX = rightwards ? head.x : head.x + head.width;
                break;
            }
            case SequenceMessageKind::Destroy: {
                bool rightwards = toPos > fromPos;
                geometry.fromX = AttachX(message.fromId, geometry.y, rightwards);
                double centerX = lifelineGeometry[static_cast<size_t>(toPos)].centerX;
                geometry.toX = rightwards ? centerX - 9.0 : centerX + 9.0;
                break;
            }
            default: {
                bool rightwards = toPos > fromPos;
                geometry.fromX = AttachX(message.fromId, geometry.y, rightwards);
                geometry.toX = AttachX(message.toId, geometry.y, !rightwards);
                break;
            }
        }

        double midX = (geometry.fromX + geometry.toX) * 0.5;
        geometry.labelRect = Rect2Dd(midX - labelWidth * 0.5,
                                     geometry.y - labelHeight - 3.0,
                                     labelWidth, labelHeight);
    }

    PlaceFragmentHorizontals();

    // Content bounds for FitView and the "sd" frame.
    double minX = 1e18, minY = 1e18, maxX = -1e18, maxY = -1e18;
    auto include = [&](const Rect2Dd& rect) {
        if (rect.width <= 0.0 && rect.height <= 0.0) return;
        minX = std::min(minX, rect.x);
        minY = std::min(minY, rect.y);
        maxX = std::max(maxX, rect.x + rect.width);
        maxY = std::max(maxY, rect.y + rect.height);
    };
    for (const auto& geometry : lifelineGeometry) {
        include(geometry.headRect);
        include(Rect2Dd(geometry.centerX, geometry.topY, 0.001,
                        std::max(0.0, geometry.bottomY - geometry.topY)));
        if (style.showFootBoxes) include(geometry.footRect);
    }
    for (const auto& geometry : messageGeometry) {
        include(geometry.labelRect);
        include(Rect2Dd(std::min(geometry.fromX, geometry.toX), geometry.y - 1.0,
                        std::fabs(geometry.toX - geometry.fromX), 2.0));
    }
    for (const auto& geometry : fragmentGeometry) include(geometry.rect);
    for (const auto& geometry : noteGeometry) include(geometry.rect);
    if (minX < maxX) {
        contentBounds = Rect2Dd(minX - 8.0, std::min(minY, style.topMargin) - 8.0,
                                (maxX - minX) + 16.0, (maxY - minY) + 24.0);
    }

    layoutDirty = false;
}

// =============================================================================
// RENDERING
// =============================================================================

void UltraCanvasSequenceDiagram::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    (void)dirtyRect;
    if (!ctx || !IsVisible()) return;

    Rect2Df bounds = GetLocalBounds();
    ctx->SetFillPaint(style.backgroundColor);
    ctx->FillRectangle(Rect2Dd(0, 0, bounds.width, bounds.height));

    if (layoutDirty) RebuildLayout(ctx);
    if (fitPending && bounds.width > 0 && bounds.height > 0) {
        ApplyFit();
        fitPending = false;
    }

    ctx->PushState();
    ctx->Translate(panOffset.x, panOffset.y);
    ctx->Scale(zoomLevel, zoomLevel);

    RenderLifelines(ctx);
    RenderActivations(ctx);
    RenderFragments(ctx);
    RenderMessages(ctx);
    RenderNotes(ctx);
    if (showFrame) RenderFrame(ctx);

    ctx->PopState();
}

void UltraCanvasSequenceDiagram::RenderFrame(IRenderContext* ctx) {
    if (contentBounds.width <= 0.0) return;

    ctx->SetStrokePaint(style.frameBorderColor);
    ctx->SetStrokeWidth(style.frameBorderWidth);
    ctx->DrawRectangle(contentBounds);

    std::string label = "sd " + (model.Title().empty() ? std::string("Interaction")
                                                       : model.Title());
    ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
    ctx->SetFontSize(style.titleFontSize);
    Size2Di size = ctx->GetTextLineDimensions(label);
    double width = static_cast<double>(size.width) + 22.0;
    double height = static_cast<double>(size.height) + 10.0;

    std::vector<Point2Dd> pentagon = {
        Point2Dd(contentBounds.x, contentBounds.y),
        Point2Dd(contentBounds.x + width, contentBounds.y),
        Point2Dd(contentBounds.x + width, contentBounds.y + height - 8.0),
        Point2Dd(contentBounds.x + width - 8.0, contentBounds.y + height),
        Point2Dd(contentBounds.x, contentBounds.y + height)
    };
    ctx->SetFillPaint(style.frameLabelColor);
    ctx->FillLinePath(pentagon);
    ctx->SetStrokePaint(style.frameBorderColor);
    ctx->DrawLinePath(pentagon, true);

    ctx->SetTextPaint(style.frameLabelTextColor);
    ctx->DrawText(label, Point2Dd(contentBounds.x + 8.0, contentBounds.y + 5.0));
}

void UltraCanvasSequenceDiagram::RenderLifelines(IRenderContext* ctx) {
    const auto& lifelines = model.Lifelines();
    for (size_t i = 0; i < lifelines.size(); ++i) {
        const LifelineGeometry& geometry = lifelineGeometry[i];

        // Dashed spine first, so heads and bars cover it.
        ctx->SetStrokePaint(style.lifelineColor);
        ctx->SetStrokeWidth(style.lifelineWidth);
        ctx->SetLineDash(LifelineDash());
        ctx->DrawLine(Point2Dd(geometry.centerX, geometry.topY),
                      Point2Dd(geometry.centerX, geometry.bottomY));
        ctx->SetLineDash(UCDashPattern::EMPTY);

        if (geometry.destroyed) {
            ctx->SetStrokePaint(style.messageColor);
            ctx->SetStrokeWidth(style.messageLineWidth * 1.4);
            ctx->DrawLine(Point2Dd(geometry.centerX - 8.0, geometry.bottomY - 8.0),
                          Point2Dd(geometry.centerX + 8.0, geometry.bottomY + 8.0));
            ctx->DrawLine(Point2Dd(geometry.centerX - 8.0, geometry.bottomY + 8.0),
                          Point2Dd(geometry.centerX + 8.0, geometry.bottomY - 8.0));
        }

        RenderHead(ctx, geometry, lifelines[i], geometry.headRect);
        if (style.showFootBoxes && !geometry.destroyed) {
            RenderHead(ctx, geometry, lifelines[i], geometry.footRect);
        }
    }
}

void UltraCanvasSequenceDiagram::RenderHead(IRenderContext* ctx,
                                            const LifelineGeometry& geometry,
                                            const SequenceLifeline& lifeline,
                                            const Rect2Dd& rect) {
    bool selected = lifeline.id == selectedLifelineId;
    bool hovered = lifeline.id == hoveredLifelineId;
    Color border = selected ? style.selectionColor : style.headBorderColor;

    ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
    ctx->SetFontSize(style.headFontSize);
    double lineHeight = static_cast<double>(ctx->GetTextLineDimensions("Mg").height);

    double textTop = rect.y + style.headPaddingY;
    switch (lifeline.kind) {
        case SequenceLifelineKind::Actor: {
            Rect2Dd icon(rect.x, rect.y, rect.width, 36.0);
            RenderActorIcon(ctx, icon, border);
            textTop = rect.y + 38.0;
            break;
        }
        case SequenceLifelineKind::Database: {
            RenderDatabaseIcon(ctx, rect, geometry.headColor, border);
            textTop = rect.y + 16.0 + style.headPaddingY;
            break;
        }
        case SequenceLifelineKind::Boundary:
        case SequenceLifelineKind::Control:
        case SequenceLifelineKind::Entity: {
            Rect2Dd icon(rect.x, rect.y, rect.width, 30.0);
            RenderRobustnessIcon(ctx, lifeline.kind, icon, geometry.headColor, border);
            textTop = rect.y + 34.0;
            break;
        }
        case SequenceLifelineKind::Object: {
            ctx->SetFillPaint(geometry.headColor);
            ctx->FillRoundedRectangle(rect, style.headCornerRadius);
            ctx->SetStrokePaint(border);
            ctx->SetStrokeWidth(selected ? style.selectionWidth : style.headBorderWidth);
            ctx->DrawRoundedRectangle(rect, style.headCornerRadius);
            break;
        }
    }
    if (hovered && !selected) {
        ctx->SetStrokePaint(style.hoverColor);
        ctx->SetStrokeWidth(style.selectionWidth);
        ctx->DrawRoundedRectangle(rect, style.headCornerRadius);
    }

    ctx->SetTextPaint(style.headTextColor);
    if (!lifeline.stereotype.empty()) {
        std::string decorated = "\xC2\xAB" + lifeline.stereotype + "\xC2\xBB";
        ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Italic);
        Size2Di size = ctx->GetTextLineDimensions(decorated);
        ctx->DrawText(decorated,
                      Point2Dd(rect.x + (rect.width - size.width) * 0.5, textTop));
        textTop += lineHeight;
        ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
    }
    Size2Di size = ctx->GetTextLineDimensions(lifeline.name);
    ctx->DrawText(lifeline.name,
                  Point2Dd(rect.x + (rect.width - size.width) * 0.5, textTop));
}

void UltraCanvasSequenceDiagram::RenderActorIcon(IRenderContext* ctx, const Rect2Dd& rect,
                                                 const Color& color) {
    double centerX = rect.x + rect.width * 0.5;
    double top = rect.y;
    ctx->SetStrokePaint(color);
    ctx->SetStrokeWidth(1.8);
    ctx->DrawCircle(Point2Dd(centerX, top + 6.0), 5.0);                       // head
    ctx->DrawLine(Point2Dd(centerX, top + 11.0), Point2Dd(centerX, top + 24.0));  // body
    ctx->DrawLine(Point2Dd(centerX - 9.0, top + 15.0), Point2Dd(centerX + 9.0, top + 15.0));
    ctx->DrawLine(Point2Dd(centerX, top + 24.0), Point2Dd(centerX - 8.0, top + 34.0));
    ctx->DrawLine(Point2Dd(centerX, top + 24.0), Point2Dd(centerX + 8.0, top + 34.0));
}

void UltraCanvasSequenceDiagram::RenderDatabaseIcon(IRenderContext* ctx, const Rect2Dd& rect,
                                                    const Color& fill, const Color& border) {
    const double capHeight = 14.0;
    Rect2Dd body(rect.x, rect.y + capHeight * 0.5, rect.width,
                 rect.height - capHeight * 0.5);
    ctx->SetFillPaint(fill);
    ctx->FillRectangle(body);
    ctx->FillEllipse(Rect2Dd(rect.x, rect.y, rect.width, capHeight));
    ctx->FillEllipse(Rect2Dd(rect.x, rect.y + rect.height - capHeight, rect.width,
                             capHeight));
    ctx->SetStrokePaint(border);
    ctx->SetStrokeWidth(style.headBorderWidth);
    ctx->DrawEllipse(Rect2Dd(rect.x, rect.y, rect.width, capHeight));
    ctx->DrawLine(Point2Dd(rect.x, rect.y + capHeight * 0.5),
                  Point2Dd(rect.x, rect.y + rect.height - capHeight * 0.5));
    ctx->DrawLine(Point2Dd(rect.x + rect.width, rect.y + capHeight * 0.5),
                  Point2Dd(rect.x + rect.width, rect.y + rect.height - capHeight * 0.5));
    ctx->DrawArc(rect.x + rect.width * 0.5, rect.y + rect.height - capHeight * 0.5,
                 rect.width * 0.5, 0.0, 3.14159265358979);
}

void UltraCanvasSequenceDiagram::RenderRobustnessIcon(IRenderContext* ctx,
                                                      SequenceLifelineKind kind,
                                                      const Rect2Dd& rect,
                                                      const Color& fill,
                                                      const Color& border) {
    double centerX = rect.x + rect.width * 0.5;
    double centerY = rect.y + 15.0;
    double radius = 11.0;

    ctx->SetFillPaint(fill);
    ctx->FillCircle(Point2Dd(centerX, centerY), radius);
    ctx->SetStrokePaint(border);
    ctx->SetStrokeWidth(1.8);
    ctx->DrawCircle(Point2Dd(centerX, centerY), radius);

    switch (kind) {
        case SequenceLifelineKind::Boundary:
            ctx->DrawLine(Point2Dd(centerX - radius - 8.0, centerY - radius),
                          Point2Dd(centerX - radius - 8.0, centerY + radius));
            ctx->DrawLine(Point2Dd(centerX - radius - 8.0, centerY),
                          Point2Dd(centerX - radius, centerY));
            break;
        case SequenceLifelineKind::Control: {
            Point2Dd tip(centerX - 3.0, centerY - radius - 1.0);
            ctx->DrawLine(Point2Dd(tip.x + 6.0, tip.y - 4.0), tip);
            ctx->DrawLine(Point2Dd(tip.x + 6.0, tip.y + 4.0), tip);
            break;
        }
        case SequenceLifelineKind::Entity:
            ctx->DrawLine(Point2Dd(centerX - radius, centerY + radius + 3.0),
                          Point2Dd(centerX + radius, centerY + radius + 3.0));
            break;
        default:
            break;
    }
}

void UltraCanvasSequenceDiagram::RenderActivations(IRenderContext* ctx) {
    for (const auto& activation : activationGeometry) {
        ctx->SetFillPaint(style.activationColor);
        ctx->FillRectangle(activation.rect);
        ctx->SetStrokePaint(style.activationBorderColor);
        ctx->SetStrokeWidth(1.0);
        ctx->DrawRectangle(activation.rect);
    }
}

void UltraCanvasSequenceDiagram::RenderFragments(IRenderContext* ctx) {
    ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
    ctx->SetFontSize(style.fragmentFontSize);
    double lineHeight = static_cast<double>(ctx->GetTextLineDimensions("Mg").height);

    for (const auto& geometry : fragmentGeometry) {
        if (geometry.rect.width <= 0.0 || geometry.rect.height <= 0.0) continue;

        if (style.fragmentFillColor.a > 0) {
            ctx->SetFillPaint(style.fragmentFillColor);
            ctx->FillRectangle(geometry.rect);
        }
        ctx->SetStrokePaint(style.fragmentBorderColor);
        ctx->SetStrokeWidth(style.fragmentBorderWidth);
        ctx->DrawRectangle(geometry.rect);

        // Pentagon operator tab.
        ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
        Size2Di size = ctx->GetTextLineDimensions(geometry.headerText);
        double tabWidth = static_cast<double>(size.width) + 18.0;
        double tabHeight = style.fragmentHeaderHeight - 2.0;
        std::vector<Point2Dd> pentagon = {
            Point2Dd(geometry.rect.x, geometry.rect.y),
            Point2Dd(geometry.rect.x + tabWidth, geometry.rect.y),
            Point2Dd(geometry.rect.x + tabWidth, geometry.rect.y + tabHeight - 7.0),
            Point2Dd(geometry.rect.x + tabWidth - 7.0, geometry.rect.y + tabHeight),
            Point2Dd(geometry.rect.x, geometry.rect.y + tabHeight)
        };
        ctx->SetFillPaint(style.fragmentLabelColor);
        ctx->FillLinePath(pentagon);
        ctx->SetStrokePaint(style.fragmentBorderColor);
        ctx->DrawLinePath(pentagon, true);
        ctx->SetTextPaint(style.fragmentLabelTextColor);
        ctx->DrawText(geometry.headerText,
                      Point2Dd(geometry.rect.x + 6.0,
                               geometry.rect.y + (tabHeight - size.height) * 0.5));

        // First operand's guard, right of the tab.
        if (!geometry.guardText.empty()) {
            ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Italic);
            ctx->SetTextPaint(style.fragmentGuardTextColor);
            ctx->DrawText(geometry.guardText,
                          Point2Dd(geometry.rect.x + tabWidth + 8.0,
                                   geometry.rect.y + (tabHeight - lineHeight) * 0.5));
        }

        // Operand separators with their guards.
        for (size_t i = 0; i < geometry.separatorYs.size(); ++i) {
            double y = geometry.separatorYs[i];
            ctx->SetStrokePaint(style.fragmentBorderColor);
            ctx->SetStrokeWidth(style.fragmentBorderWidth);
            ctx->SetLineDash(SeparatorDash());
            ctx->DrawLine(Point2Dd(geometry.rect.x, y),
                          Point2Dd(geometry.rect.x + geometry.rect.width, y));
            ctx->SetLineDash(UCDashPattern::EMPTY);
            if (i < geometry.separatorGuards.size()) {
                ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Italic);
                ctx->SetTextPaint(style.fragmentGuardTextColor);
                ctx->DrawText(geometry.separatorGuards[i],
                              Point2Dd(geometry.rect.x + 10.0, y + 3.0));
            }
        }
    }
}

void UltraCanvasSequenceDiagram::RenderArrowhead(IRenderContext* ctx, const Point2Dd& tip,
                                                 bool pointsRight, bool filled) {
    double direction = pointsRight ? -1.0 : 1.0;
    Point2Dd upper(tip.x + direction * style.arrowheadLength,
                   tip.y - style.arrowheadWidth * 0.5);
    Point2Dd lower(tip.x + direction * style.arrowheadLength,
                   tip.y + style.arrowheadWidth * 0.5);
    if (filled) {
        ctx->SetFillPaint(style.messageColor);
        ctx->FillLinePath({tip, upper, lower});
    } else {
        ctx->DrawLine(upper, tip);
        ctx->DrawLine(lower, tip);
    }
}

void UltraCanvasSequenceDiagram::RenderMessage(IRenderContext* ctx,
                                               const MessageGeometry& geometry,
                                               const SequenceMessage& message,
                                               bool highlighted) {
    Color lineColor = highlighted ? style.selectionColor : style.messageColor;
    double lineWidth = highlighted ? style.messageLineWidth + 0.8 : style.messageLineWidth;
    bool dashed = message.kind == SequenceMessageKind::Return ||
                  message.kind == SequenceMessageKind::Create;

    ctx->SetStrokePaint(lineColor);
    ctx->SetStrokeWidth(lineWidth);
    if (dashed) ctx->SetLineDash(ReturnDash());

    if (message.IsSelfMessage()) {
        double rightX = geometry.fromX + style.selfMessageWidth;
        ctx->DrawLine(Point2Dd(geometry.fromX, geometry.y), Point2Dd(rightX, geometry.y));
        ctx->DrawLine(Point2Dd(rightX, geometry.y), Point2Dd(rightX, geometry.selfBottomY));
        ctx->DrawLine(Point2Dd(rightX, geometry.selfBottomY),
                      Point2Dd(geometry.toX, geometry.selfBottomY));
        if (dashed) ctx->SetLineDash(UCDashPattern::EMPTY);
        ctx->SetStrokePaint(lineColor);
        RenderArrowhead(ctx, Point2Dd(geometry.toX, geometry.selfBottomY), false,
                        message.kind == SequenceMessageKind::Synchronous ||
                            message.kind == SequenceMessageKind::Destroy);
    } else {
        ctx->DrawLine(Point2Dd(geometry.fromX, geometry.y),
                      Point2Dd(geometry.toX, geometry.y));
        if (dashed) ctx->SetLineDash(UCDashPattern::EMPTY);

        bool pointsRight = geometry.toX >= geometry.fromX;
        bool filled = message.kind == SequenceMessageKind::Synchronous ||
                      message.kind == SequenceMessageKind::Destroy ||
                      message.kind == SequenceMessageKind::Found;
        ctx->SetStrokePaint(lineColor);
        ctx->SetStrokeWidth(lineWidth);
        RenderArrowhead(ctx, Point2Dd(geometry.toX, geometry.y), pointsRight, filled);

        if (message.kind == SequenceMessageKind::Found) {
            ctx->SetFillPaint(lineColor);
            ctx->FillCircle(Point2Dd(geometry.fromX, geometry.y), style.gateCircleRadius);
        } else if (message.kind == SequenceMessageKind::Lost) {
            ctx->SetFillPaint(lineColor);
            ctx->FillCircle(Point2Dd(geometry.toX, geometry.y), style.gateCircleRadius);
        }
    }

    if (!geometry.displayLabel.empty()) {
        ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(style.messageFontSize);
        ctx->SetTextPaint(highlighted ? style.selectionColor : style.messageTextColor);
        ctx->DrawText(geometry.displayLabel,
                      Point2Dd(geometry.labelRect.x, geometry.labelRect.y));
    }
}

void UltraCanvasSequenceDiagram::RenderMessages(IRenderContext* ctx) {
    const auto& messages = model.Messages();
    for (size_t k = 0; k < messages.size(); ++k) {
        bool highlighted = static_cast<int>(k) == selectedMessage ||
                           static_cast<int>(k) == hoveredMessage;
        RenderMessage(ctx, messageGeometry[k], messages[k], highlighted);
    }
}

void UltraCanvasSequenceDiagram::RenderNotes(IRenderContext* ctx) {
    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(style.noteFontSize);
    double lineHeight = static_cast<double>(ctx->GetTextLineDimensions("Mg").height);
    const double earSize = 9.0;

    for (const auto& geometry : noteGeometry) {
        const Rect2Dd& rect = geometry.rect;
        std::vector<Point2Dd> outline = {
            Point2Dd(rect.x, rect.y),
            Point2Dd(rect.x + rect.width - earSize, rect.y),
            Point2Dd(rect.x + rect.width, rect.y + earSize),
            Point2Dd(rect.x + rect.width, rect.y + rect.height),
            Point2Dd(rect.x, rect.y + rect.height)
        };
        ctx->SetFillPaint(style.noteColor);
        ctx->FillLinePath(outline);
        ctx->SetStrokePaint(style.noteBorderColor);
        ctx->SetStrokeWidth(1.0);
        ctx->DrawLinePath(outline, true);
        // Folded corner.
        ctx->DrawLine(Point2Dd(rect.x + rect.width - earSize, rect.y),
                      Point2Dd(rect.x + rect.width - earSize, rect.y + earSize));
        ctx->DrawLine(Point2Dd(rect.x + rect.width - earSize, rect.y + earSize),
                      Point2Dd(rect.x + rect.width, rect.y + earSize));

        ctx->SetTextPaint(style.noteTextColor);
        double y = rect.y + style.notePadding;
        for (const auto& line : geometry.lines) {
            ctx->DrawText(line, Point2Dd(rect.x + style.notePadding, y));
            y += lineHeight;
        }
    }
}

// =============================================================================
// TEXT HELPERS
// =============================================================================

std::vector<std::string> UltraCanvasSequenceDiagram::WrapNoteText(IRenderContext* ctx,
                                                                  const std::string& text,
                                                                  double maxWidth) const {
    std::vector<std::string> lines;
    std::string current;
    std::string word;

    auto flushWord = [&]() {
        if (word.empty()) return;
        std::string candidate = current.empty() ? word : current + " " + word;
        double width = static_cast<double>(ctx->GetTextLineDimensions(candidate).width);
        if (width <= maxWidth || current.empty()) {
            current = candidate;
        } else {
            lines.push_back(current);
            current = word;
        }
        word.clear();
    };

    for (char character : text) {
        if (character == '\n') {
            flushWord();
            lines.push_back(current);
            current.clear();
        } else if (character == ' ') {
            flushWord();
        } else {
            word += character;
        }
    }
    flushWord();
    if (!current.empty()) lines.push_back(current);
    if (lines.empty()) lines.push_back(std::string());
    return lines;
}

// =============================================================================
// HIT TESTING
// =============================================================================

int UltraCanvasSequenceDiagram::FindMessageAt(const Point2Dd& worldPos) const {
    const double slack = 5.0;
    for (size_t k = 0; k < messageGeometry.size(); ++k) {
        const MessageGeometry& geometry = messageGeometry[k];
        if (geometry.labelRect.Contains(worldPos)) return static_cast<int>(k);

        double left = std::min(geometry.fromX, geometry.toX);
        double right = std::max(geometry.fromX, geometry.toX);
        if (geometry.selfBottomY > 0.0) {
            right = std::max(right, geometry.fromX + style.selfMessageWidth);
            if (worldPos.x >= left - slack && worldPos.x <= right + slack &&
                worldPos.y >= geometry.y - slack &&
                worldPos.y <= geometry.selfBottomY + slack) {
                return static_cast<int>(k);
            }
        } else if (worldPos.x >= left - slack && worldPos.x <= right + slack &&
                   std::fabs(worldPos.y - geometry.y) <= slack) {
            return static_cast<int>(k);
        }
    }
    return -1;
}

const UltraCanvasSequenceDiagram::LifelineGeometry*
UltraCanvasSequenceDiagram::FindLifelineAt(const Point2Dd& worldPos) const {
    for (const auto& geometry : lifelineGeometry) {
        if (geometry.headRect.Contains(worldPos)) return &geometry;
        if (style.showFootBoxes && geometry.footRect.Contains(worldPos)) return &geometry;
    }
    return nullptr;
}

const UltraCanvasSequenceDiagram::FragmentGeometry*
UltraCanvasSequenceDiagram::FindFragmentLabelAt(const Point2Dd& worldPos) const {
    for (const auto& geometry : fragmentGeometry) {
        Rect2Dd tab(geometry.rect.x, geometry.rect.y, 90.0, style.fragmentHeaderHeight);
        if (tab.Contains(worldPos)) return &geometry;
    }
    return nullptr;
}

// =============================================================================
// EVENTS
// =============================================================================

bool UltraCanvasSequenceDiagram::OnEvent(const UCEvent& event) {
    switch (event.type) {
        case UCEventType::MouseDown:  return HandleMouseDown(event);
        case UCEventType::MouseUp:    return HandleMouseUp(event);
        case UCEventType::MouseMove:  return HandleMouseMove(event);
        case UCEventType::MouseWheel: return HandleMouseWheel(event);
        case UCEventType::MouseLeave:
            if (hoveredMessage != -1 || !hoveredLifelineId.empty()) {
                hoveredMessage = -1;
                hoveredLifelineId.clear();
                RequestRedraw();
            }
            isPanning = false;
            return false;
        default: break;
    }
    return UltraCanvasUIElement::OnEvent(event);
}

bool UltraCanvasSequenceDiagram::HandleMouseDown(const UCEvent& event) {
    if (event.button == UCMouseButton::Middle ||
        event.button == UCMouseButton::Right) {
        isPanning = true;
        panStartScreen = event.pointer;
        panStartOffset = panOffset;
        return true;
    }
    if (event.button != UCMouseButton::Left) return false;

    Point2Dd worldPos = ScreenToWorld(event.pointer);

    if (interactive) {
        int messageIndex = FindMessageAt(worldPos);
        if (messageIndex >= 0) {
            selectedMessage = messageIndex;
            selectedLifelineId.clear();
            if (onMessageClick) {
                onMessageClick(static_cast<size_t>(messageIndex),
                               *model.GetMessage(static_cast<size_t>(messageIndex)));
            }
            if (onSelectionChanged) onSelectionChanged();
            RequestRedraw();
            return true;
        }
        const LifelineGeometry* lifeline = FindLifelineAt(worldPos);
        if (lifeline) {
            selectedLifelineId = lifeline->id;
            selectedMessage = -1;
            if (onLifelineClick) onLifelineClick(lifeline->id);
            if (onSelectionChanged) onSelectionChanged();
            RequestRedraw();
            return true;
        }
        const FragmentGeometry* fragment = FindFragmentLabelAt(worldPos);
        if (fragment) {
            if (onFragmentClick) onFragmentClick(fragment->fragmentId);
            return true;
        }
        if (selectedMessage != -1 || !selectedLifelineId.empty()) {
            DeselectAll();
            if (onSelectionChanged) onSelectionChanged();
        }
    }

    // Left-drag on empty canvas pans, like the class diagram.
    isPanning = true;
    panStartScreen = event.pointer;
    panStartOffset = panOffset;
    return true;
}

bool UltraCanvasSequenceDiagram::HandleMouseUp(const UCEvent& event) {
    (void)event;
    if (isPanning) {
        isPanning = false;
        return true;
    }
    return false;
}

bool UltraCanvasSequenceDiagram::HandleMouseMove(const UCEvent& event) {
    if (isPanning) {
        panOffset.x = panStartOffset.x + (event.pointer.x - panStartScreen.x);
        panOffset.y = panStartOffset.y + (event.pointer.y - panStartScreen.y);
        RequestRedraw();
        return true;
    }

    Point2Dd worldPos = ScreenToWorld(event.pointer);
    int messageIndex = FindMessageAt(worldPos);
    const LifelineGeometry* lifeline =
        messageIndex < 0 ? FindLifelineAt(worldPos) : nullptr;
    std::string lifelineId = lifeline ? lifeline->id : std::string();

    if (messageIndex != hoveredMessage || lifelineId != hoveredLifelineId) {
        hoveredMessage = messageIndex;
        hoveredLifelineId = lifelineId;
        RequestRedraw();
    }
    return messageIndex >= 0 || lifeline != nullptr;
}

// One zoom step about the cursor: the world point under it stays put. A wheel
// notch is eased in as a run of these (UltraCanvasSmoothZoom), and applying them
// in a row about the same cursor is exactly applying their product once.
void UltraCanvasSequenceDiagram::ApplyZoomFactorAtCursor(double factor,
                                                         const Point2Di& cursor) {
    double newZoom = std::clamp(zoomLevel * factor, 0.1, 10.0);
    if (newZoom == zoomLevel) return;

    Point2Dd worldBefore = ScreenToWorld(cursor);
    zoomLevel = newZoom;
    panOffset.x = cursor.x - worldBefore.x * zoomLevel;
    panOffset.y = cursor.y - worldBefore.y * zoomLevel;
}

bool UltraCanvasSequenceDiagram::HandleMouseWheel(const UCEvent& event) {
    if (!zoomAnim.IsBound()) {
        zoomAnim.Bind([this](double f) { ApplyZoomFactorAtCursor(f, zoomCursor); },
                      [this] { RequestRedraw(); });
    }
    zoomCursor = event.pointer;
    zoomAnim.ZoomBy(event.wheelDelta > 0 ? 1.1 : 1.0 / 1.1, zoomLevel, 0.1, 10.0);
    return true;
}

// =============================================================================
// THEME PRESETS
// =============================================================================

namespace SequenceDiagramStyles {

SequenceDiagramStyle CreateForTheme(SequenceDiagramTheme theme) {
    SequenceDiagramStyle s;
    switch (theme) {
        case SequenceDiagramTheme::Default:
            break;

        case SequenceDiagramTheme::Classic:
            // Reference image 1: golden heads, hairline black arrows.
            s.backgroundColor = Color(255, 255, 255, 255);
            s.headColor = Color(255, 214, 89, 255);
            s.headBorderColor = Color(120, 90, 20, 255);
            s.headTextColor = Color(50, 40, 10, 255);
            s.activationColor = Color(255, 244, 214, 255);
            s.activationBorderColor = Color(120, 90, 20, 255);
            s.messageColor = Color(40, 40, 40, 255);
            s.messageTextColor = Color(40, 40, 40, 255);
            s.fragmentLabelColor = Color(245, 238, 214, 255);
            s.frameLabelColor = Color(245, 238, 214, 255);
            s.headCornerRadius = 0.0;
            break;

        case SequenceDiagramTheme::Professional:
            // Reference image 2: teal heads and activation bars.
            s.backgroundColor = Color(252, 253, 253, 255);
            s.headColor = Color(128, 203, 196, 255);
            s.headBorderColor = Color(38, 121, 113, 255);
            s.headTextColor = Color(20, 55, 52, 255);
            s.activationColor = Color(178, 223, 219, 255);
            s.activationBorderColor = Color(38, 121, 113, 255);
            s.messageColor = Color(55, 71, 79, 255);
            s.messageTextColor = Color(45, 60, 68, 255);
            s.lifelineColor = Color(96, 125, 139, 255);
            s.fragmentBorderColor = Color(69, 90, 100, 255);
            s.fragmentLabelColor = Color(207, 232, 229, 255);
            s.frameLabelColor = Color(207, 232, 229, 255);
            s.headCornerRadius = 4.0;
            break;

        case SequenceDiagramTheme::Colorful:
            // Reference image 3: per-lifeline accents (heads coloured by
            // position via HeadColorFor / ApplyPaletteByLifeline).
            s.backgroundColor = Color(255, 255, 255, 255);
            s.headTextColor = Color(255, 255, 255, 255);
            s.headBorderColor = Color(70, 70, 70, 120);
            s.activationColor = Color(236, 239, 241, 255);
            s.activationBorderColor = Color(120, 130, 140, 255);
            s.headCornerRadius = 4.0;
            break;

        case SequenceDiagramTheme::Minimal:
            s.backgroundColor = Color(255, 255, 255, 255);
            s.headColor = Color(255, 255, 255, 255);
            s.headBorderColor = Color(60, 60, 60, 255);
            s.headTextColor = Color(40, 40, 40, 255);
            s.headBorderWidth = 1.0;
            s.headCornerRadius = 0.0;
            s.activationColor = Color(250, 250, 250, 255);
            s.activationBorderColor = Color(60, 60, 60, 255);
            s.messageColor = Color(50, 50, 50, 255);
            s.messageTextColor = Color(50, 50, 50, 255);
            s.messageLineWidth = 1.1;
            s.lifelineColor = Color(150, 150, 150, 255);
            s.fragmentBorderColor = Color(90, 90, 90, 255);
            s.fragmentLabelColor = Color(245, 245, 245, 255);
            s.frameLabelColor = Color(245, 245, 245, 255);
            s.noteColor = Color(252, 252, 252, 255);
            s.noteBorderColor = Color(140, 140, 140, 255);
            s.noteTextColor = Color(60, 60, 60, 255);
            break;

        case SequenceDiagramTheme::Dark:
            s.backgroundColor = Color(30, 33, 39, 255);
            s.headColor = Color(55, 62, 72, 255);
            s.headBorderColor = Color(130, 148, 170, 255);
            s.headTextColor = Color(225, 230, 238, 255);
            s.activationColor = Color(62, 70, 82, 255);
            s.activationBorderColor = Color(130, 148, 170, 255);
            s.messageColor = Color(190, 200, 214, 255);
            s.messageTextColor = Color(205, 214, 226, 255);
            s.lifelineColor = Color(105, 115, 130, 255);
            s.fragmentBorderColor = Color(130, 148, 170, 255);
            s.fragmentLabelColor = Color(55, 62, 72, 255);
            s.fragmentLabelTextColor = Color(225, 230, 238, 255);
            s.fragmentGuardTextColor = Color(180, 190, 205, 255);
            s.frameBorderColor = Color(130, 148, 170, 255);
            s.frameLabelColor = Color(55, 62, 72, 255);
            s.frameLabelTextColor = Color(225, 230, 238, 255);
            s.noteColor = Color(74, 70, 48, 255);
            s.noteBorderColor = Color(150, 140, 90, 255);
            s.noteTextColor = Color(230, 224, 190, 255);
            s.selectionColor = Color(97, 175, 254, 255);
            s.hoverColor = Color(97, 175, 254, 90);
            break;
    }
    return s;
}

std::vector<Color> GetLifelinePalette() {
    return {
        Color(96, 125, 199, 255),    // indigo
        Color(190, 74, 80, 255),     // red
        Color(84, 130, 154, 255),    // steel blue
        Color(222, 146, 54, 255),    // orange
        Color(94, 152, 90, 255),     // green
        Color(151, 101, 170, 255),   // purple
        Color(64, 145, 152, 255),    // teal
        Color(166, 128, 70, 255)     // brown
    };
}

} // namespace SequenceDiagramStyles

// =============================================================================
// SAMPLE DATA
// =============================================================================

namespace SequenceDiagramSamples {

UltraCanvasSequenceModel CardGame() {
    UltraCanvasSequenceModel model;
    model.SetTitle("Card Game Round");
    model.AddLifeline("Game");
    model.AddLifeline("Deck+Cards");
    model.AddLifeline("Player1");
    model.AddLifeline("Player2");
    model.AddLifeline("Rules");
    model.SetActivateOnStart("Game", true);

    model.AddSynchronousMessage("Game", "Deck+Cards", "Shuffle");
    model.AddReturnMessage("Deck+Cards", "Game");
    model.AddSynchronousMessage("Game", "Deck+Cards", "GiveCards");
    size_t take1 = model.AddSynchronousMessage("Deck+Cards", "Player1", "TakeCards");
    model.AddReturnMessage("Player1", "Deck+Cards");
    size_t take2 = model.AddSynchronousMessage("Deck+Cards", "Player2", "TakeCards");
    model.AddReturnMessage("Player2", "Deck+Cards");
    model.AddReturnMessage("Deck+Cards", "Game");
    model.AddLoop(take1, take2 + 1, "until all cards dealt");

    model.AddSynchronousMessage("Game", "Player1", "Bet");
    model.AddSynchronousMessage("Player1", "Rules", "Use");
    model.AddReturnMessage("Rules", "Player1");
    model.AddReturnMessage("Player1", "Game");
    model.AddSynchronousMessage("Game", "Player2", "Bet");
    model.AddSynchronousMessage("Player2", "Rules", "Use");
    model.AddReturnMessage("Rules", "Player2");
    model.AddReturnMessage("Player2", "Game");

    model.AddSelfMessage("Game", "AssessWinner");
    model.AddSynchronousMessage("Game", "Player1", "getHand");
    model.AddReturnMessage("Player1", "Game");
    model.AddSynchronousMessage("Game", "Player2", "getHand");
    model.AddReturnMessage("Player2", "Game");
    model.AddSynchronousMessage("Game", "Rules", "isBetter");
    model.AddReturnMessage("Rules", "Game");
    model.AddSynchronousMessage("Game", "Player1", "takePot");
    model.AddReturnMessage("Player1", "Game");
    return model;
}

UltraCanvasSequenceModel UserLogin() {
    UltraCanvasSequenceModel model;
    model.SetTitle("User Login");
    model.AddActor("Student");
    model.AddLifeline("Login Screen");
    model.AddLifeline("Validate User");
    model.AddDatabase("Database");
    model.SetActivateOnStart("Student", true);

    model.AddSynchronousMessage("Student", "Login Screen", "click on login button");
    model.AddSynchronousMessage("Login Screen", "Validate User",
                                "ValidateUser(userid, password)");
    size_t check = model.AddSynchronousMessage("Validate User", "Database", "checkUser()");
    model.AddReturnMessage("Database", "Validate User", "userID found,\npassword matched");
    model.AddNote("credentials are\nnever stored in\nplain text",
                  SequenceNotePlacement::RightOf, "Database",
                  static_cast<int>(check));
    model.AddSynchronousMessage("Validate User", "Database", "class(upgrades)");
    model.AddReturnMessage("Database", "Validate User");
    model.AddReturnMessage("Validate User", "Login Screen", "user login successful");
    model.AddReturnMessage("Login Screen", "Student", "display class(upgrades)");
    return model;
}

UltraCanvasSequenceModel Authentication() {
    UltraCanvasSequenceModel model;
    model.SetTitle("Facebook Authentication");
    model.AddLifeline("Application");
    model.AddLifeline("Authentication");
    model.AddLifeline("Custom Facebook", SequenceLifelineKind::Control);
    model.AddLifeline("Facebook", SequenceLifelineKind::Boundary);
    model.SetActivateOnStart("Application", true);

    model.AddSynchronousMessage("Application", "Custom Facebook",
                                "Create/facebook context");
    model.AddSynchronousMessage("Application", "Authentication",
                                "Request user authentication");
    size_t ask = model.AddSynchronousMessage("Authentication", "Custom Facebook",
                                             "Asks the permission to the user");
    model.AddReturnMessage("Custom Facebook", "Authentication",
                           "User chooses/logs in to connect");
    model.AddNote("the token is scoped\nand short-lived",
                  SequenceNotePlacement::RightOf, "Facebook", static_cast<int>(ask));
    model.AddSynchronousMessage("Custom Facebook", "Facebook", "Authenticate");
    model.AddReturnMessage("Facebook", "Custom Facebook",
                           "Facebook access token");
    model.AddReturnMessage("Custom Facebook", "Application", "OAuth event: OK");
    model.AddAsynchronousMessage("Application", "Facebook", "Request events");
    model.AddReturnMessage("Facebook", "Application", "Send response");
    return model;
}

UltraCanvasSequenceModel OnlineShop() {
    UltraCanvasSequenceModel model;
    model.SetTitle("Checkout");
    model.AddLifeline("StoreFront");
    model.AddLifeline("Cart");
    model.AddLifeline("Inventory");
    model.SetActivateOnStart("StoreFront", true);

    size_t add = model.AddSynchronousMessage("StoreFront", "Cart", "AddItem");
    model.AddSynchronousMessage("Cart", "Inventory", "ReserveItem");
    model.AddReturnMessage("Inventory", "Cart");
    size_t added = model.AddReturnMessage("Cart", "StoreFront");
    model.AddLoop(add, added, "more items");

    model.AddSynchronousMessage("StoreFront", "Cart", "Checkout");
    model.AddSelfMessage("Cart", "ProcessOrder");
    size_t confirm = model.AddReturnMessage("Cart", "StoreFront", "ConfirmOrder");
    size_t place = model.AddSynchronousMessage("Cart", "Inventory", "PlaceItemInOrder");
    size_t placed = model.AddReturnMessage("Inventory", "Cart");
    std::string alt = model.AddAlt(confirm, placed, "order accepted");
    model.AddFragmentOperand(alt, "payment failed", place);
    return model;
}

UltraCanvasSequenceModel ObjectLifecycle() {
    UltraCanvasSequenceModel model;
    model.SetTitle("Session Lifecycle");
    model.AddActor("User");
    model.AddLifeline("Application");
    model.AddLifeline("Session");
    model.SetActivateOnStart("User", true);

    model.AddSynchronousMessage("User", "Application", "sign in");
    model.AddCreateMessage("Application", "Session");
    model.AddSynchronousMessage("Application", "Session", "store credentials");
    model.AddReturnMessage("Session", "Application");
    model.AddLostMessage("Application", "audit event");
    model.AddFoundMessage("Application", "timeout signal");
    model.AddDestroyMessage("Application", "Session");
    model.AddReturnMessage("Application", "User", "signed out");
    return model;
}

UltraCanvasSequenceModel UltraCanvasEventFlow() {
    // The framework's own event pipeline, verified against
    // core/UltraCanvasApplication.cpp and core/UltraCanvasButton.cpp.
    UltraCanvasSequenceModel model;
    model.SetTitle("UltraCanvas Event Flow");
    model.AddActor("User");
    model.AddLifeline("X Server", SequenceLifelineKind::Boundary);
    model.AddLifeline("Application", SequenceLifelineKind::Control);
    model.AddLifeline("Window");
    model.AddLifeline("Button");
    model.SetActivateOnStart("Application", true);

    model.AddAsynchronousMessage("User", "X Server", "click");
    size_t next = model.AddSynchronousMessage("X Server", "Application",
                                              "XNextEvent");
    model.AddSelfMessage("Application", "ConvertXEventToUCEvent");
    model.AddSelfMessage("Application", "PushEvent(ucEvent)");
    model.AddSelfMessage("Application", "DispatchEvent");
    model.AddSynchronousMessage("Application", "Window",
                                "FindElementAtPoint(pointerWindow)");
    model.AddReturnMessage("Window", "Application", "elementUnderPointer");
    model.AddSynchronousMessage("Application", "Button", "OnEvent(MouseDown)");
    model.AddReturnMessage("Button", "Application", "true");
    model.AddSynchronousMessage("Application", "Button", "OnEvent(MouseUp)");
    model.AddSelfMessage("Button", "Click() → onClick()");
    model.AddSelfMessage("Button", "RequestRedraw()");
    model.AddReturnMessage("Button", "Application", "true");
    size_t render = model.AddSynchronousMessage("Application", "Window",
                                                "UpdateAndRender()");
    model.AddSynchronousMessage("Window", "X Server",
                                "FlushToSurface(nativeSurface)");
    model.AddReturnMessage("X Server", "Window");
    size_t done = model.AddReturnMessage("Window", "Application");
    model.AddLoop(next, done, "while running");
    model.AddNote("one iteration of\nUltraCanvasApplicationBase::Run()",
                  SequenceNotePlacement::RightOf, "Button",
                  static_cast<int>(render));
    return model;
}

} // namespace SequenceDiagramSamples

} // namespace UltraCanvas
