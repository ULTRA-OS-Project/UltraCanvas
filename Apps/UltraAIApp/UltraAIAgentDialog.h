// Apps/UltraAIApp/UltraAIAgentDialog.h
// The dashboard's Agents tile: give a model a task and a set of tools, and
// watch it work. The dialog runs an UltraAI::Agent (UltraAIAgent.h) on the
// chosen chat endpoint and shows each step in the transcript: the model's
// text as it streams, every tool call with its arguments, and each result.
// The tools are the safe demo set of UltraAIAgentTools.h. "Ask before a
// tool runs" puts each call to the user first. The conversation carries
// over between tasks until "New conversation".
// Version: 0.1.0 - first version
// Last Modified: 2026-11-09
// Author: UltraAI Module
#pragma once

#include "UltraCanvasModalDialog.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTextInput.h"

#include "UltraAIAgent.h"
#include "UltraAIEndpoints.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace UltraAIApp {

class UltraAIAgentDialog : public UltraCanvas::UltraCanvasModalDialog {
public:
    UltraAIAgentDialog();
    ~UltraAIAgentDialog() override;

    void CreateAgentDialog();

    // What a run hands back to the UI thread when it ends.
    struct RunSummary {
        UltraAI::AgentStopReason stopReason = UltraAI::AgentStopReason::Completed;
        int turns = 0;
        int toolCalls = 0;
        UltraAI::TokenUsage usage;
        UltraAI::Error error;
        std::vector<UltraAI::Message> history;
        bool historyValid = false;   // false: the agent could not be made
    };

    // Shared between the dialog and the run's thread: the agent to cancel,
    // and the stop flag a waiting permission question watches.
    struct RunState {
        std::mutex mutex;
        std::shared_ptr<UltraAI::Agent> agent;
        std::atomic<bool> stop{false};
    };

private:
    void RebuildEndpointPicker(const std::string& selectId = "");
    void OnRun();
    void StartRun(const Endpoint& endpoint, const std::string& task);
    void OnStop();
    void OnNewConversation();
    void OnOpenSettings();
    void OnNeedsLocalModel(const Endpoint& endpoint, const std::string& task,
                           const std::string& message);

    // On the UI thread, posted from the run.
    void OnAgentEvent(const UltraAI::AgentEvent& event);
    void OnRunFinished(const Endpoint& endpoint, const std::string& task,
                       const RunSummary& summary);
    // A tool call waiting for the user's yes or no.
    void AskPermission(const UltraAI::ToolCall& call, std::function<void(bool)> answer);

    UltraAI::AgentOptions BuildOptions() const;
    void AppendTranscript(const std::string& markdown);
    void SetStatus(const std::string& text);
    void SetRunning(bool running);

    // Widgets.
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown>  endpointPicker_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea>  instructions_;
    std::vector<std::shared_ptr<UltraCanvas::UltraCanvasCheckbox>> toolBoxes_;
    std::shared_ptr<UltraCanvas::UltraCanvasCheckbox>  askBox_;
    std::shared_ptr<UltraCanvas::UltraCanvasCheckbox>  streamBox_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> maxTurnsInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea>  transcript_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea>  input_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    runButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     statusLabel_;

    std::vector<Endpoint> chatEndpoints_;

    // The conversation between runs (each run's agent starts from it).
    std::vector<UltraAI::Message> history_;
    bool running_ = false;
    bool streamedThisTurn_ = false;
    std::shared_ptr<RunState> run_;

    std::shared_ptr<std::atomic<bool>> uiAlive_ =
        std::make_shared<std::atomic<bool>>(true);

    static constexpr long kW = 900;
    static constexpr long kH = 720;
};

} // namespace UltraAIApp
