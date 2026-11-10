// Apps/UltraAIApp/UltraAIAgentDialog.cpp
// Version: 0.1.0

#include "UltraAIAgentDialog.h"

#include "UltraAIAgentTools.h"
#include "UltraAILocalModelDialog.h"
#include "UltraAISettingsDialog.h"
#include "UltraAISettingsWidgets.h"

#include "UltraAI.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTextUtils.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <chrono>
#include <condition_variable>
#include <optional>
#include <thread>
#include <utility>

namespace UltraAIApp {

using namespace UltraCanvas;
using namespace UltraAI;

namespace {

// A tool's arguments or result can be long (a whole text to count); the
// transcript shows the start of it.
std::string Shorten(const std::string& text, size_t limit = 300) {
    if (text.size() <= limit) return text;
    size_t cut = limit;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;   // whole code points
    return text.substr(0, cut) + "…";
}

// Inline code in the markdown transcript: backticks inside would end it.
std::string Code(const std::string& text) {
    std::string clean;
    for (char c : text) clean += (c == '`') ? '\'' : (c == '\n' ? ' ' : c);
    return "`" + clean + "`";
}

std::string ToolNames(const std::vector<ToolCall>& calls) {
    std::string out;
    for (const auto& c : calls) {
        if (!out.empty()) out += ", ";
        out += c.name;
    }
    return out;
}

std::string GreetingText() {
    return "**" + Tr("UltraAI Agents") + "**\n\n" +
           Tr("Give the model a task. It can use the tools ticked above and decides "
              "itself when to call them; each call and its result appear here. "
              "\"Ask before a tool runs\" lets you allow or refuse every call.") +
           "\n\n---\n\n";
}

} // namespace

UltraAIAgentDialog::UltraAIAgentDialog() = default;

UltraAIAgentDialog::~UltraAIAgentDialog() {
    uiAlive_->store(false);
    OnStop();
}

void UltraAIAgentDialog::CreateAgentDialog() {
    using UltraCanvas::CSSLayout::Dimension;
    using UltraCanvas::CSSLayout::AlignItems;

    DialogConfig cfg;
    cfg.title      = Tr("UltraAI — Agents");
    cfg.width      = kW;
    cfg.height     = kH;
    cfg.dialogType = DialogType::Custom;
    cfg.buttons    = DialogButtons::NoButtons;
    cfg.position   = DialogPosition::CenterParent;
    cfg.resizable  = true;
    CreateDialog(cfg);

    layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(AlignItems::Stretch);
    SetPadding(16);

    auto makeRow = [](const std::string& id, int height) {
        auto row = std::make_shared<UltraCanvasContainer>(id);
        row->layout.SetFlexRow().SetFlexGap(10).SetFlexAlignItems(AlignItems::Center);
        row->size.height = Dimension::Px(height);
        row->layoutItem.SetFlexShrink(0);
        return row;
    };
    auto makeButton = [](const std::string& id, const std::string& text, int width,
                         std::function<void()> onClick) {
        auto b = std::make_shared<UltraCanvasButton>(id, 0, 0, width, 28);
        b->SetText(text);
        b->size.width = Dimension::Px(width);
        b->layoutItem.SetFlexShrink(0);
        b->onClick = std::move(onClick);
        return b;
    };

    // ===== Endpoint row =====
    auto topRow = makeRow("agent-toprow", 30);
    endpointPicker_ = std::make_shared<UltraCanvasDropdown>("agent-endpoint", 0, 0, 0, 28);
    endpointPicker_->size.height = Dimension::Px(28);
    endpointPicker_->layoutItem.SetFlexGrow(1);
    topRow->AddChild(endpointPicker_);
    topRow->AddChild(makeButton("agent-settings", Tr("Settings"), 110, [this]() { OnOpenSettings(); }));
    topRow->AddChild(makeButton("agent-new", Tr("New conversation"), 150, [this]() { OnNewConversation(); }));
    AddChild(topRow);

    // ===== Instructions (the system prompt) =====
    auto instructionsLabel = std::make_shared<UltraCanvasLabel>(
        "agent-instructions-label", 0, 0, 0, 18, Tr("Instructions for the agent"));
    instructionsLabel->size.height = Dimension::Px(18);
    instructionsLabel->layoutItem.SetFlexShrink(0);
    AddChild(instructionsLabel);

    instructions_ = std::make_shared<UltraCanvasTextArea>("agent-instructions", 0, 0, 0, 56);
    instructions_->SetEditingMode(TextAreaEditingMode::PlainText);
    instructions_->SetWordWrap(true);
    instructions_->SetText(Tr("You are a careful assistant. Use the tools for facts and "
                              "arithmetic instead of guessing, then answer briefly."));
    instructions_->size.height = Dimension::Px(56);
    instructions_->SetBorders(1.0f, Color(200, 200, 200, 255));
    instructions_->layoutItem.SetFlexShrink(0);
    AddChild(instructions_);

    // ===== Tools =====
    auto toolsRow = makeRow("agent-tools", 26);
    toolsRow->AddChild(SettingsWidgets::MakeLabel("agent-tools-label", Tr("Tools:")));
    int index = 0;
    for (const auto& demo : DemoTools()) {
        auto box = SettingsWidgets::MakeCheckbox("agent-tool-" + std::to_string(index++),
                                                 demo.label, 150, true, nullptr);
        toolBoxes_.push_back(box);
        toolsRow->AddChild(box);
    }
    AddChild(toolsRow);

    auto optionsRow = makeRow("agent-options", 26);
    askBox_ = SettingsWidgets::MakeCheckbox("agent-ask", Tr("Ask before a tool runs"), 210, false, nullptr);
    optionsRow->AddChild(askBox_);
    streamBox_ = SettingsWidgets::MakeCheckbox("agent-stream", Tr("Show the text as it arrives"), 230, true, nullptr);
    optionsRow->AddChild(streamBox_);
    optionsRow->AddChild(SettingsWidgets::MakeLabel("agent-turns-label", Tr("Most model calls per task:")));
    maxTurnsInput_ = std::make_shared<UltraCanvasTextInput>("agent-turns", 0, 0, 60, 24);
    maxTurnsInput_->SetText("10");
    maxTurnsInput_->size.width = Dimension::Px(60);
    maxTurnsInput_->layoutItem.SetFlexShrink(0);
    optionsRow->AddChild(maxTurnsInput_);
    AddChild(optionsRow);

    // ===== Transcript =====
    transcript_ = std::make_shared<UltraCanvasTextArea>("agent-transcript", 0, 0, 0, 0);
    transcript_->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
    transcript_->SetReadOnly(true);
    transcript_->SetCaretVisible(false);
    transcript_->SetWordWrap(true);
    transcript_->SetText(GreetingText());
    transcript_->SetBorders(1.0f, Color(200, 200, 200, 255));
    transcript_->layoutItem.SetFlexGrow(1);
    AddChild(transcript_);

    // ===== Task input =====
    input_ = std::make_shared<UltraCanvasTextArea>("agent-input", 0, 0, 0, 70);
    input_->SetEditingMode(TextAreaEditingMode::PlainText);
    input_->SetWordWrap(true);
    input_->SetPlaceholder(Tr("Describe the task. Enter starts it, Shift+Enter starts a new line."));
    input_->size.height = Dimension::Px(70);
    input_->SetBorders(1.0f, Color(200, 200, 200, 255));
    input_->layoutItem.SetFlexShrink(0);
    input_->onBeforeKeyDown = [this](const UCEvent& e) {
        const bool enter = e.virtualKey == UCKeys::Return || e.virtualKey == UCKeys::NumPadEnter;
        if (!enter || e.shift) return false;
        if (!running_) OnRun();
        return true;
    };
    AddChild(input_);

    // ===== Actions =====
    auto actionRow = makeRow("agent-actions", 34);
    statusLabel_ = std::make_shared<UltraCanvasLabel>("agent-status", 0, 0, 0, 20, "");
    statusLabel_->layoutItem.SetFlexGrow(1);
    actionRow->AddChild(statusLabel_);
    runButton_ = makeButton("agent-run", Tr("Run"), 120, [this]() {
        if (running_) OnStop(); else OnRun();
    });
    actionRow->AddChild(runButton_);
    actionRow->AddChild(makeButton("agent-close", Tr("Close"), 90,
                                   [this]() { CloseDialog(DialogResult::Close); }));
    AddChild(actionRow);

    RebuildEndpointPicker();
}

void UltraAIAgentDialog::RebuildEndpointPicker(const std::string& selectId) {
    chatEndpoints_ = EndpointStore::Instance().ForCapability(AICapability::Chat);
    if (!endpointPicker_) return;
    endpointPicker_->ClearItems();
    if (chatEndpoints_.empty()) {
        endpointPicker_->AddItem(Tr("(no chat endpoint — open Settings)"));
        endpointPicker_->SetSelectedIndex(0, false);
        SetStatus(Tr("No chat endpoint is set up. Choose Settings to add one."));
        return;
    }
    int selectIndex = 0;
    for (size_t i = 0; i < chatEndpoints_.size(); ++i) {
        const auto& e = chatEndpoints_[i];
        endpointPicker_->AddItem((e.name.empty() ? e.id : e.name) + "   (" + e.providerId + ")");
        if (!selectId.empty() && e.id == selectId) selectIndex = static_cast<int>(i);
    }
    endpointPicker_->SetSelectedIndex(selectIndex, false);
    SetStatus("");
}

AgentOptions UltraAIAgentDialog::BuildOptions() const {
    AgentOptions opts;
    opts.systemPrompt = instructions_ ? instructions_->GetText() : std::string{};
    opts.stream = streamBox_ && streamBox_->IsChecked();
    opts.maxTurns = 10;
    if (maxTurnsInput_) {
        double turns = 0;
        if (TryParseFloat(maxTurnsInput_->GetText(), turns) && turns >= 1 && turns <= 100) {
            opts.maxTurns = static_cast<int>(turns);
        }
    }
    return opts;
}

void UltraAIAgentDialog::OnRun() {
    if (running_) return;
    if (chatEndpoints_.empty()) {
        SetStatus(Tr("No chat endpoint is set up. Choose Settings to add one."));
        return;
    }
    int idx = endpointPicker_ ? endpointPicker_->GetSelectedIndex() : 0;
    if (idx < 0 || idx >= static_cast<int>(chatEndpoints_.size())) idx = 0;
    const Endpoint endpoint = chatEndpoints_[static_cast<size_t>(idx)];

    const std::string task = input_ ? input_->GetText() : std::string{};
    if (task.empty()) {
        SetStatus(Tr("Describe a task first."));
        return;
    }
    AppendTranscript("**" + Tr("You") + "**\n\n" + task + "\n\n");
    if (input_) input_->SetText("");
    StartRun(endpoint, task);
}

void UltraAIAgentDialog::StartRun(const Endpoint& endpoint, const std::string& task) {
    AgentOptions opts = BuildOptions();
    const bool ask = askBox_ && askBox_->IsChecked();

    std::vector<AgentTool> tools;
    const auto demos = DemoTools();
    for (size_t i = 0; i < demos.size() && i < toolBoxes_.size(); ++i) {
        if (toolBoxes_[i]->IsChecked()) tools.push_back(demos[i].tool);
    }

    auto alive = uiAlive_;
    auto post = [alive](std::function<void()> fn) {
        auto guarded = [alive, fn = std::move(fn)]() { if (alive->load()) fn(); };
        auto* app = UltraCanvasApplicationBase::GetCurrent();
        if (app) app->PostToUIThread(std::move(guarded));
        else guarded();
    };

    auto state = std::make_shared<RunState>();
    run_ = state;

    if (ask) {
        // The question is asked on the UI thread; the run waits for the
        // answer, and gives up when the dialog closes or Stop is chosen.
        opts.canUseTool = [this, post, alive, state](const ToolCall& call) {
            struct Answer {
                std::mutex mutex;
                std::condition_variable cv;
                std::optional<bool> allowed;
            };
            auto answer = std::make_shared<Answer>();
            post([this, call, answer]() {
                AskPermission(call, [answer](bool allowed) {
                    std::lock_guard<std::mutex> lock(answer->mutex);
                    answer->allowed = allowed;
                    answer->cv.notify_all();
                });
            });
            std::unique_lock<std::mutex> lock(answer->mutex);
            while (!answer->allowed) {
                if (!alive->load() || state->stop.load()) return ToolPermission::Stop();
                answer->cv.wait_for(lock, std::chrono::milliseconds(100));
            }
            return *answer->allowed ? ToolPermission::Allow()
                                    : ToolPermission::Deny("The user refused this tool call.");
        };
    }

    SetRunning(true);
    streamedThisTurn_ = false;
    SetStatus(TrF("Contacting {0}…", endpoint.name.empty() ? endpoint.id : endpoint.name));

    const TextLLMConfig cfg = ToTextLLMConfig(endpoint);
    opts.model = endpoint.defaultModel;
    std::vector<Message> history = history_;

    std::thread worker([this, post, state, cfg, opts, tools, history, endpoint, task]() {
        RunSummary summary;
        Error createError;
        std::unique_ptr<Agent> made = CreateAgent(cfg, opts, &createError);
        if (!made) {
            summary.stopReason = AgentStopReason::Error;
            summary.error = createError;
            if (summary.error.message.empty()) {
                summary.error.message = "Could not create the provider.";
            }
            post([this, endpoint, task, summary]() { OnRunFinished(endpoint, task, summary); });
            return;
        }
        std::shared_ptr<Agent> agent(std::move(made));
        for (const auto& tool : tools) agent->AddTool(tool);
        agent->SetHistory(history);
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->agent = agent;
        }
        if (state->stop.load()) agent->Cancel();

        AgentResult result = agent->Run(task, [this, post](const AgentEvent& ev) {
            if (ev.kind == AgentEventKind::Finished) return;   // OnRunFinished reports it
            AgentEvent copy = ev;
            copy.result = nullptr;
            post([this, copy]() { OnAgentEvent(copy); });
        });

        summary.stopReason = result.stopReason;
        summary.turns = result.turns;
        summary.toolCalls = result.toolCalls;
        summary.usage = result.usage;
        summary.error = result.error;
        summary.history = agent->History();
        summary.historyValid = true;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->agent.reset();
        }
        post([this, endpoint, task, summary]() { OnRunFinished(endpoint, task, summary); });
    });
    worker.detach();
}

void UltraAIAgentDialog::AskPermission(const ToolCall& call, std::function<void(bool)> answer) {
    const std::string message =
        TrF("The agent wants to use the tool \"{0}\" with these arguments:\n\n{1}\n\nAllow it?",
            call.name, Shorten(call.argumentsJson.empty() ? "{}" : call.argumentsJson, 600));
    UltraCanvasDialogManager::ShowConfirmation(message, Tr("Allow the tool?"),
        [answer](bool confirmed) { answer(confirmed); }, this);
}

void UltraAIAgentDialog::OnAgentEvent(const AgentEvent& ev) {
    switch (ev.kind) {
        case AgentEventKind::TurnStarted:
            streamedThisTurn_ = false;
            SetStatus(TrF("Step {0}: the model is thinking…", ev.turn));
            break;
        case AgentEventKind::TextDelta:
            if (!streamedThisTurn_) {
                streamedThisTurn_ = true;
                AppendTranscript("**" + Tr("Agent") + "**\n\n");
            }
            AppendTranscript(ev.textDelta);
            break;
        case AgentEventKind::AssistantMessage:
            if (!streamedThisTurn_ && !ev.message.text.empty()) {
                AppendTranscript("**" + Tr("Agent") + "**\n\n" + ev.message.text);
            }
            if (streamedThisTurn_ || !ev.message.text.empty()) AppendTranscript("\n\n");
            if (!ev.message.toolCalls.empty()) {
                SetStatus(TrF("Step {0}: using {1}", ev.turn, ToolNames(ev.message.toolCalls)));
            }
            break;
        case AgentEventKind::ToolCallStarted:
            AppendTranscript("🔧 **" + ev.toolCall.name + "** " +
                             Code(Shorten(ev.toolCall.argumentsJson.empty() ? "{}" : ev.toolCall.argumentsJson)) +
                             "\n\n");
            break;
        case AgentEventKind::ToolCallFinished:
            AppendTranscript((ev.toolResult.isError ? "⚠️ " : "↳ ") +
                             Code(Shorten(ev.toolResult.content)) + "\n\n");
            break;
        case AgentEventKind::ToolCallDenied:
            AppendTranscript("⛔ **" + ev.toolCall.name + "** — " +
                             Tr("not run") + "\n\n");
            break;
        case AgentEventKind::Finished:
            break;
    }
}

void UltraAIAgentDialog::OnRunFinished(const Endpoint& endpoint, const std::string& task,
                                       const RunSummary& summary) {
    run_.reset();
    SetRunning(false);
    if (summary.historyValid) history_ = summary.history;

    if (summary.error.providerCode == "needs-install") {
        OnNeedsLocalModel(endpoint, task, summary.error.message);
        return;
    }

    std::string note;
    switch (summary.stopReason) {
        case AgentStopReason::Completed:       break;
        case AgentStopReason::MaxTurns:        note = Tr("The agent stopped: it reached the most model calls per task."); break;
        case AgentStopReason::MaxOutputTokens: note = Tr("The reply was cut off at the model's length limit."); break;
        case AgentStopReason::ContentFiltered: note = Tr("The provider refused to answer."); break;
        case AgentStopReason::Stopped:         note = Tr("Stopped."); break;
        case AgentStopReason::Cancelled:       note = Tr("Stopped."); break;
        case AgentStopReason::Error:
            note = TrF("Error: {0}", summary.error.message.empty() ? Tr("the model call failed") : summary.error.message);
            break;
    }
    if (!note.empty()) AppendTranscript("_" + note + "_\n\n");
    AppendTranscript("---\n\n");

    std::string status = TrF("{0} model calls, {1} tool calls, {2} tokens in, {3} out",
                             summary.turns, summary.toolCalls,
                             summary.usage.inputTokens, summary.usage.outputTokens);
    if (!note.empty()) status = note + "  ·  " + status;
    SetStatus(status);
}

void UltraAIAgentDialog::OnNeedsLocalModel(const Endpoint& endpoint, const std::string& task,
                                           const std::string& message) {
    SetStatus(message);
    UltraAILocalModelDialog::Show(this, Tr("Agents"), "textllm", endpoint.defaultModel,
        [this, alive = uiAlive_, endpoint, task](const std::string& modelId) {
            if (!alive->load()) return;
            Endpoint resolved = endpoint;
            if (resolved.defaultModel.empty()) resolved.defaultModel = modelId;
            SetStatus(Tr("Model installed; starting the task…"));
            StartRun(resolved, task);
        });
}

void UltraAIAgentDialog::OnStop() {
    auto state = run_;
    if (!state) return;
    state->stop.store(true);
    std::shared_ptr<Agent> agent;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        agent = state->agent;
    }
    if (agent) agent->Cancel();
    if (uiAlive_->load()) SetStatus(Tr("Stopping…"));
}

void UltraAIAgentDialog::OnNewConversation() {
    if (running_) { SetStatus(Tr("Stop the running task first.")); return; }
    history_.clear();
    if (transcript_) transcript_->SetText(GreetingText());
    SetStatus(Tr("New conversation."));
}

void UltraAIAgentDialog::OnOpenSettings() {
    std::string selectId;
    if (endpointPicker_) {
        const int idx = endpointPicker_->GetSelectedIndex();
        if (idx >= 0 && idx < static_cast<int>(chatEndpoints_.size())) {
            selectId = chatEndpoints_[static_cast<size_t>(idx)].id;
        }
    }
    auto dlg = std::make_shared<UltraAISettingsDialog>();
    dlg->CreateSettingsDialog(selectId);
    dlg->ShowModal(this);
    RebuildEndpointPicker(selectId);
}

void UltraAIAgentDialog::AppendTranscript(const std::string& markdown) {
    if (transcript_) transcript_->AppendText(markdown);
}

void UltraAIAgentDialog::SetStatus(const std::string& text) {
    if (statusLabel_) statusLabel_->SetText(text);
}

void UltraAIAgentDialog::SetRunning(bool running) {
    running_ = running;
    if (runButton_) runButton_->SetText(running ? Tr("Stop") : Tr("Run"));
}

} // namespace UltraAIApp
