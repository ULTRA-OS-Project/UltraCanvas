// Apps/UltraStore/ui/StoreJobs.cpp
// The window's jobs: one worker thread at a time runs the core's operation
// and posts progress and the outcome to the UI thread; the Downloads panel
// shows the queue. The engine and the client are touched by the worker only
// while a job runs and by the UI thread only while none does.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "StoreWindow.h"

#include "StoreTheme.h"

#include <algorithm>
#include <chrono>

using namespace UltraCanvas;

namespace UltraStore::Ui {

namespace {

std::string Percent(double fraction) {
    return std::to_string(static_cast<int>(fraction * 100.0 + 0.5)) + " %";
}

} // namespace

bool StoreWindow::HasJobFor(const std::string& appId) const {
    if (running_ && running_->appId == appId) return true;
    for (const Job& j : pending_) if (j.appId == appId) return true;
    return false;
}

StoreWindow::QueueEntry* StoreWindow::FindEntry(uint64_t jobId) {
    for (QueueEntry& e : queue_) if (e.jobId == jobId) return &e;
    return nullptr;
}

void StoreWindow::Enqueue(Job job) {
    job.id = nextJobId_++;
    if (job.showInQueue) {
        QueueEntry entry;
        entry.jobId = job.id;
        entry.title = job.title;
        entry.status = "Waiting";
        queue_.push_back(entry);
        if (downloads_ && !downloads_->IsExpanded()) {
            // Opened for the job, not remembered: the user's own fold is the setting.
            autoExpanding_ = true;
            downloads_->SetExpanded(true);
            autoExpanding_ = false;
        }
    }
    pending_.push_back(std::move(job));
    RebuildQueuePanel();
    RunNextJob();
}

void StoreWindow::RunNextJob() {
    if (running_ || pending_.empty()) return;
    running_ = pending_.front();
    pending_.pop_front();
    if (QueueEntry* e = FindEntry(running_->id)) {
        e->state = QueueState::Running;
        e->status = "Starting";
    }
    cancelRunning_ = false;
    if (jobThread_.joinable()) jobThread_.join();   // the previous one has posted its result; it is done
    RebuildQueuePanel();
    RebuildAllPages();   // the buttons of the app in work go grey

    const Job job = *running_;
    const std::shared_ptr<const Catalog> catalog = catalog_;
    const std::string channel = channel_;
    std::weak_ptr<int> token = alive_;
    jobThread_ = std::thread([this, job, catalog, channel, token]() {
        using Clock = std::chrono::steady_clock;
        Clock::time_point lastPost = Clock::time_point::min();
        std::string lastStatus;
        auto progress = [this, token, &lastPost, &lastStatus, id = job.id](const std::string& status, double fraction) {
            const Clock::time_point now = Clock::now();
            if (status == lastStatus && now - lastPost < std::chrono::milliseconds(80)) return;
            lastPost = now;
            lastStatus = status;
            app_.PostToUIThread([this, token, id, status, fraction]() {
                if (!token.lock()) return;
                JobProgress(id, status, fraction);
            });
        };
        JobResult result = RunJob(job, catalog, channel, progress);
        app_.PostToUIThread([this, token, id = job.id, result]() {
            if (!token.lock()) return;
            JobFinished(id, result);
        });
    });
}

StoreWindow::JobResult StoreWindow::RunJob(const Job& job, const std::shared_ptr<const Catalog>& catalog,
                                           const std::string& channel,
                                           const std::function<void(const std::string&, double)>& progress) {
    JobResult result;
    auto fail = [&result](const Error& e) {
        result.ok = false;
        result.message = e.message + (e.detail.empty() ? "" : " (" + e.detail + ")");
        return result;
    };
    auto nameOf = [&catalog](const std::string& id) {
        const Component* c = catalog ? catalog->FindAny(id) : nullptr;
        return c && !c->name.empty() ? c->name : id;
    };
    auto takeInstalled = [this, &result]() {
        if (const auto all = engine_->Installed()) {
            result.installed = all.Value();
            result.haveInstalled = true;
        }
    };
    EngineCallbacks callbacks;
    callbacks.onStep = [&](const InstallStep& step) {
        progress((step.alreadyInstalled ? "Have " : "Fetching ") + nameOf(step.component->id) + " " + step.release->version, -1.0);
    };
    callbacks.onDownload = [&](const InstallStep& step, int64_t done, int64_t total) {
        const double fraction = total > 0 ? double(done) / double(total) : -1.0;
        progress("Downloading " + nameOf(step.component->id) + ": " + FormatBytes(uint64_t(done)) +
                 (total > 0 ? " of " + FormatBytes(uint64_t(total)) + " (" + Percent(fraction) + ")" : ""), fraction);
        return !cancelRunning_;
    };
    callbacks.onUnpack = [&](const InstallStep& step, uint64_t done, uint64_t total, const std::string& file) {
        const double fraction = total > 0 ? double(done) / double(total) : -1.0;
        progress("Unpacking " + nameOf(step.component->id) + (file.empty() ? "" : ": " + file), fraction);
        return !cancelRunning_;
    };

    switch (job.kind) {
        case JobKind::Refresh: {
            if (!client_) return fail(Error{ErrorCode::Internal, "No catalog client"});
            progress("Fetching the catalog", -1.0);
            const Result<Catalog> fetched = client_->Refresh();
            if (!fetched) return fail(fetched.error);
            result.catalog = std::make_shared<const Catalog>(fetched.Value());
            result.state = client_->State();
            result.message = "Catalog updated: " + std::to_string(fetched.Value().apps.size()) + " apps, sequence " +
                             std::to_string(fetched.Value().sequence) + ", from " + result.state.fetchedFrom;
            return result;
        }
        case JobKind::Reload:
            takeInstalled();
            result.message = "Installed apps read";
            return result;
        case JobKind::Install: {
            if (!catalog) return fail(Error{ErrorCode::NotFound, "No catalog"});
            const Result<InstallOutcome> outcome = engine_->Install(*catalog, job.appId, platform_, channel, callbacks);
            takeInstalled();
            if (!outcome) return fail(cancelRunning_ ? Error{ErrorCode::NetworkError, "Cancelled"} : outcome.error);
            const InstallOutcome& o = outcome.Value();
            if (o.installed.empty()) result.message = nameOf(job.appId) + " was already installed";
            else result.message = "Installed " + nameOf(job.appId) +
                                  (o.plan.steps.empty() ? "" : " " + o.plan.steps.back().release->version) +
                                  " (" + FormatBytes(o.bytesDownloaded) + " downloaded)";
            return result;
        }
        case JobKind::Update: {
            if (!catalog) return fail(Error{ErrorCode::NotFound, "No catalog"});
            const Result<InstallOutcome> outcome = engine_->Update(*catalog, job.appId, platform_, {}, callbacks);
            takeInstalled();
            if (!outcome) return fail(cancelRunning_ ? Error{ErrorCode::NetworkError, "Cancelled"} : outcome.error);
            const InstallOutcome& o = outcome.Value();
            if (o.installed.empty()) result.message = nameOf(job.appId) + " is up to date";
            else result.message = "Updated " + nameOf(job.appId) +
                                  (o.plan.steps.empty() ? "" : " to " + o.plan.steps.back().release->version);
            return result;
        }
        case JobKind::UpdateAll: {
            if (!catalog) return fail(Error{ErrorCode::NotFound, "No catalog"});
            const auto all = engine_->UpdateAll(*catalog, platform_, callbacks);
            takeInstalled();
            if (!all) return fail(all.error);
            size_t updated = 0, failed = 0;
            for (const auto& [id, outcome] : all.Value()) {
                if (!outcome) ++failed;
                else if (!outcome.Value().installed.empty()) ++updated;
            }
            result.ok = failed == 0;
            result.message = std::to_string(updated) + " updated" + (failed ? ", " + std::to_string(failed) + " failed" : "");
            return result;
        }
        case JobKind::Remove: {
            progress("Removing " + nameOf(job.appId), -1.0);
            const auto removed = engine_->Remove(job.appId);
            takeInstalled();
            if (!removed) return fail(removed.error);
            size_t components = 0;
            for (const InstalledRecord& r : removed.Value()) if (r.kind != ComponentKind::App) ++components;
            result.message = "Removed " + nameOf(job.appId) +
                             (components ? " and " + std::to_string(components) + (components == 1 ? " component" : " components") : "");
            return result;
        }
        case JobKind::Collect: {
            progress("Looking for unused components", -1.0);
            const auto removed = engine_->CollectGarbage();
            takeInstalled();
            if (!removed) return fail(removed.error);
            result.message = removed.Value().empty() ? "Nothing to remove"
                : "Removed " + std::to_string(removed.Value().size()) + (removed.Value().size() == 1 ? " unused component" : " unused components");
            return result;
        }
    }
    return result;
}

void StoreWindow::JobProgress(uint64_t jobId, const std::string& status, double fraction) {
    QueueEntry* e = FindEntry(jobId);
    if (e) {
        e->status = status;
        e->fraction = fraction;
        if (auto gauge = e->gauge.lock()) {
            gauge->SetIndeterminate(fraction < 0.0);
            if (fraction >= 0.0) gauge->SetValue(fraction * 100.0);
            gauge->RequestRedraw();
        }
        if (auto label = e->statusLabel.lock()) label->SetText(status);
    }
    UpdateQueueSummary();
    SetStatus((running_ ? running_->title + ": " : std::string()) + status);
}

void StoreWindow::JobFinished(uint64_t jobId, JobResult result) {
    if (jobThread_.joinable()) jobThread_.join();
    const std::optional<Job> job = running_;
    running_.reset();
    if (QueueEntry* e = FindEntry(jobId)) {
        e->state = !result.ok ? (cancelRunning_ ? QueueState::Cancelled : QueueState::Failed) : QueueState::Done;
        e->status = result.message;
        e->fraction = result.ok ? 1.0 : e->fraction;
    }
    if (result.catalog) {
        catalogState_ = result.state;
        SetCatalog(result.catalog, "Catalog sequence " + std::to_string(result.catalog->sequence) +
                   ", fetched " + result.state.fetchedAt + " from " + result.state.fetchedFrom);
    }
    if (result.haveInstalled) SetInstalled(std::move(result.installed));
    SetStatus(result.message, !result.ok);
    if (!result.ok && job && job->kind != JobKind::Reload && !cancelRunning_) Alert(job->title + ": " + result.message);
    cancelRunning_ = false;
    RebuildQueuePanel();
    RebuildAllPages();
    RunNextJob();
}

void StoreWindow::CancelJob(uint64_t jobId) {
    if (running_ && running_->id == jobId) {
        cancelRunning_ = true;
        if (engine_) engine_->Downloads().Cancel();
        if (QueueEntry* e = FindEntry(jobId)) e->status = "Cancelling…";
        RebuildQueuePanel();
        return;
    }
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [jobId](const Job& j) { return j.id == jobId; }), pending_.end());
    if (QueueEntry* e = FindEntry(jobId)) {
        e->state = QueueState::Cancelled;
        e->status = "Cancelled before it started";
    }
    RebuildQueuePanel();
    RebuildAllPages();
}

void StoreWindow::TogglePause(uint64_t jobId) {
    if (!running_ || running_->id != jobId || !engine_) return;
    QueueEntry* e = FindEntry(jobId);
    if (!e) return;
    e->paused = !e->paused;
    if (e->paused) engine_->Downloads().Pause();
    else engine_->Downloads().Resume();
    if (auto button = e->pauseButton.lock()) button->SetText(e->paused ? "Resume" : "Pause");
    if (auto label = e->statusLabel.lock()) label->SetText(e->paused ? "Paused - " + e->status : e->status);
    UpdateQueueSummary();
}

void StoreWindow::DismissFinished() {
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [](const QueueEntry& e) {
        return e.state == QueueState::Done || e.state == QueueState::Failed || e.state == QueueState::Cancelled;
    }), queue_.end());
    RebuildQueuePanel();
}

void StoreWindow::RebuildQueuePanel() {
    if (!queueList_) return;
    queueList_->ClearChildren();
    if (queue_.empty()) {
        queueList_->AddChild(Theme::Caption("usQ.empty", "Nothing queued. Installs, updates and removals line up here."));
    }
    int n = 0;
    for (QueueEntry& e : queue_) {
        const std::string id = "usQ" + std::to_string(n++);
        auto card = Theme::Pin(CreateCard(id, e.title, e.status));
        CardStyle style = card->GetStyle();
        style.padding = 8.0f;
        style.cornerRadius = 6.0f;
        card->SetStyle(style);
        e.statusLabel = card->GetSubtitleLabel();

        auto gauge = Theme::Pin(CreateGaugeDiagramElement(id + ".bar", 0, 0, 0, 10));
        gauge->SetMode(GaugeMode::LinearBar);
        gauge->SetMinValue(0.0);
        gauge->SetMaxValue(100.0);
        gauge->SetShowLabels(false);
        gauge->SetGaugeColor(e.state == QueueState::Failed || e.state == QueueState::Cancelled ? Theme::kDanger
                             : e.state == QueueState::Done ? Theme::kSuccess : Theme::kAccent);
        gauge->SetTrackColor(Theme::kTrack);
        gauge->SetBackgroundColor(Theme::kSurface);
        gauge->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(10));
        if (e.state == QueueState::Running && e.fraction < 0.0) gauge->SetIndeterminate(true);
        else gauge->SetValue(e.state == QueueState::Done ? 100.0 : std::max(0.0, e.fraction) * 100.0);
        card->AddContent(gauge);
        e.gauge = gauge;

        const uint64_t jobId = e.jobId;
        if (e.state == QueueState::Running) {
            auto pause = Theme::Button(id + ".pause", e.paused ? "Resume" : "Pause", 72);
            pause->onClick = [this, jobId]() { TogglePause(jobId); };
            card->AddAction(pause);
            e.pauseButton = pause;
        }
        if (e.state == QueueState::Running || e.state == QueueState::Waiting) {
            auto cancel = Theme::Button(id + ".cancel", "Cancel", 72, Theme::ButtonKind::Danger);
            cancel->onClick = [this, jobId]() { CancelJob(jobId); };
            card->AddAction(cancel);
        }
        queueList_->AddChild(card);
    }
    queueList_->InvalidateLayout();
    queueList_->RequestRedraw();
    UpdateQueueSummary();
}

void StoreWindow::UpdateQueueSummary() {
    if (!downloads_) return;
    size_t waiting = 0, finished = 0;
    const QueueEntry* running = nullptr;
    for (const QueueEntry& e : queue_) {
        if (e.state == QueueState::Waiting) ++waiting;
        else if (e.state == QueueState::Running) running = &e;
        else ++finished;
    }
    std::string detail;
    if (running) {
        detail = running->title;
        if (running->paused) detail += " (paused)";
        else if (running->fraction >= 0.0) detail += " " + Percent(running->fraction);
    }
    if (waiting) detail += (detail.empty() ? "" : " · ") + std::to_string(waiting) + " waiting";
    if (!running && !waiting) detail = finished ? std::to_string(finished) + " finished" : "Nothing queued";
    downloads_->SetDetail(detail);
}

void StoreWindow::StopJobThread() {
    if (jobThread_.joinable()) jobThread_.join();
}

} // namespace UltraStore::Ui
