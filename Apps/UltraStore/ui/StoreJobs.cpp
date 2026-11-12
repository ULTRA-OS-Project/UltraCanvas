// Apps/UltraStore/ui/StoreJobs.cpp
// The window's jobs: one worker thread at a time runs the core's operation
// and posts progress and the outcome to the UI thread; the Downloads panel
// shows the queue. The engine and the client are touched by the worker only
// while a job runs and by the UI thread only while none does.
// Version: 0.4.0 - the refresh syncs the licences (renewals, licences taken back)
// Version: 0.3.1 - every text through Tr; an update to buy is told by the licence, not the message
// Version: 0.3.0 - the open order kept and resumed, the licence file
// Version: 0.2.0 - the Buy job
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "StoreWindow.h"

#include "StoreTheme.h"

#include "UltraCanvasUtils.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <chrono>
#include <thread>

using namespace UltraCanvas;

namespace UltraStore::Ui {

namespace {

std::string Percent(double fraction) {
    return TrF("{0} %", static_cast<int>(fraction * 100.0 + 0.5));
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
        entry.status = Tr("Waiting");
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
        e->status = Tr("Starting");
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
        progress(step.alreadyInstalled ? TrF("Have {0} {1}", nameOf(step.component->id), step.release->version) : TrF("Fetching {0} {1}", nameOf(step.component->id), step.release->version), -1.0);
    };
    callbacks.onDownload = [&](const InstallStep& step, int64_t done, int64_t total) {
        const double fraction = total > 0 ? double(done) / double(total) : -1.0;
        progress(total > 0 ? TrF("Downloading {0}: {1} of {2} ({3})", nameOf(step.component->id), FormatBytes(uint64_t(done)), FormatBytes(uint64_t(total)), Percent(fraction))
                           : TrF("Downloading {0}: {1}", nameOf(step.component->id), FormatBytes(uint64_t(done))), fraction);
        return !cancelRunning_;
    };
    callbacks.onUnpack = [&](const InstallStep& step, uint64_t done, uint64_t total, const std::string& file) {
        const double fraction = total > 0 ? double(done) / double(total) : -1.0;
        progress(TrF("Unpacking {0}", nameOf(step.component->id)) + (file.empty() ? "" : ": " + file), fraction);
        return !cancelRunning_;
    };

    switch (job.kind) {
        case JobKind::Refresh: {
            if (!client_) return fail(Error{ErrorCode::Internal, Tr("No catalog client")});
            progress(Tr("Fetching the catalog"), -1.0);
            const Result<Catalog> fetched = client_->Refresh();
            if (!fetched) return fail(fetched.error);
            result.catalog = std::make_shared<const Catalog>(fetched.Value());
            result.state = client_->State();
            result.message = TrF("Catalog updated: {0}, sequence {1}, from {2}", TrN("{n} app", "{n} apps", static_cast<long long>(fetched.Value().apps.size())), fetched.Value().sequence, result.state.fetchedFrom);
            // The licences follow: a subscription renewed gets its new token,
            // a licence refunded or charged back goes.
            if (const auto bought = engine_->Registry().Licences(); bought && !bought.Value().empty() && !fetched.Value().paymentUrl.empty()) {
                progress(Tr("Checking the licences with the payment service"), -1.0);
                const auto sync = engine_->SyncLicences(fetched.Value().paymentUrl, fetched.Value().licenceKey, UltraNetHttpCall());
                if (sync && sync.Value().Changed()) {
                    if (const auto all = engine_->Registry().Licences()) { result.licences = all.Value(); result.haveLicences = true; }
                    if (!sync.Value().renewed.empty()) result.message += " " + TrN("{n} subscription renewed.", "{n} subscriptions renewed.", static_cast<long long>(sync.Value().renewed.size()));
                    if (!sync.Value().revoked.empty()) result.message += " " + TrN("{n} licence taken back.", "{n} licences taken back.", static_cast<long long>(sync.Value().revoked.size()));
                }
            }
            return result;
        }
        case JobKind::Reload:
            takeInstalled();
            result.message = Tr("Installed apps read");
            return result;
        case JobKind::Install: {
            if (!catalog) return fail(Error{ErrorCode::NotFound, Tr("No catalog")});
            const Result<InstallOutcome> outcome = engine_->Install(*catalog, job.appId, platform_, channel, callbacks);
            takeInstalled();
            if (!outcome) return fail(cancelRunning_ ? Error{ErrorCode::NetworkError, Tr("Cancelled")} : outcome.error);
            const InstallOutcome& o = outcome.Value();
            if (o.installed.empty()) result.message = TrF("{0} was already installed", nameOf(job.appId));
            else result.message = TrF("Installed {0} ({1} downloaded)", nameOf(job.appId) + (o.plan.steps.empty() ? "" : " " + o.plan.steps.back().release->version), FormatBytes(o.bytesDownloaded));
            return result;
        }
        case JobKind::Update: {
            if (!catalog) return fail(Error{ErrorCode::NotFound, Tr("No catalog")});
            const Result<InstallOutcome> outcome = engine_->Update(*catalog, job.appId, platform_, {}, callbacks);
            takeInstalled();
            if (!outcome) return fail(cancelRunning_ ? Error{ErrorCode::NetworkError, Tr("Cancelled")} : outcome.error);
            const InstallOutcome& o = outcome.Value();
            if (o.installed.empty()) result.message = TrF("{0} is up to date", nameOf(job.appId));
            else if (o.plan.steps.empty()) result.message = TrF("Updated {0}", nameOf(job.appId));
            else result.message = TrF("Updated {0} to {1}", nameOf(job.appId), o.plan.steps.back().release->version);
            return result;
        }
        case JobKind::UpdateAll: {
            if (!catalog) return fail(Error{ErrorCode::NotFound, Tr("No catalog")});
            const auto all = engine_->UpdateAll(*catalog, platform_, callbacks);
            takeInstalled();
            if (!all) return fail(all.error);
            // An update the purchase does not cover is one to buy, not a
            // failure: asked of the licence, since the message is in the
            // person's language.
            auto outsidePurchase = [&](const std::string& id) {
                const Component* app = catalog->FindApp(id);
                const auto current = engine_->Registry().Current(id);
                if (!app || !current) return false;
                const Resolution res = PackageResolver::Resolve(*app, platform_, current.Value().channel);
                return res.release && !engine_->PurchaseCovers(id, *res.release);
            };
            size_t updated = 0, failed = 0, toBuy = 0;
            for (const auto& [id, outcome] : all.Value()) {
                if (!outcome && outcome.error.code == ErrorCode::NotAvailable && outsidePurchase(id)) ++toBuy;
                else if (!outcome) ++failed;
                else if (!outcome.Value().installed.empty()) ++updated;
            }
            result.ok = failed == 0;
            result.message = TrN("{n} app updated", "{n} apps updated", static_cast<long long>(updated)) +
                             (failed ? ", " + TrN("{n} failed", "{n} failed", static_cast<long long>(failed)) : std::string()) +
                             (toBuy ? ", " + TrN("{n} to buy anew", "{n} to buy anew", static_cast<long long>(toBuy)) : std::string());
            return result;
        }
        case JobKind::Remove: {
            progress(TrF("Removing {0}", nameOf(job.appId)), -1.0);
            const auto removed = engine_->Remove(job.appId);
            takeInstalled();
            if (!removed) return fail(removed.error);
            size_t components = 0;
            for (const InstalledRecord& r : removed.Value()) if (r.kind != ComponentKind::App) ++components;
            result.message = TrF("Removed {0}", nameOf(job.appId)) +
                             (components ? " " + TrN("and {n} component", "and {n} components", static_cast<long long>(components)) : std::string());
            return result;
        }
        case JobKind::Buy: {
            if (!catalog) return fail(Error{ErrorCode::NotFound, Tr("No catalog")});
            const Component* app = catalog->FindApp(job.appId);
            if (!app) return fail(Error{ErrorCode::NotFound, TrF("The catalog does not list {0}", job.appId)});
            const HttpCall http = UltraNetHttpCall();
            Order order;
            // The settings remember the order until it is over, so a store
            // closed during the payment resumes it at the next start.
            auto remember = [this](const StoreSettings::OpenOrder& open) {
                app_.PostToUIThread([this, open]() { settings_.openOrder = open; settings_.Save(); });
            };
            if (job.resume) {
                const StoreSettings::OpenOrder open = settings_.OrderToResume(Today());
                if (open.Empty() || open.app != job.appId) return fail(Error{ErrorCode::NotFound, Tr("No open order to resume")});
                progress(TrF("Asking the payment service about the order {0}", open.orderId), -1.0);
                const auto now = OrderStatus(catalog->paymentUrl, open.orderId, open.secret, http);
                if (!now) return fail(now.error);
                order = now.Value();
                if (!order.Over()) {
                    // Still waiting for the payment: the browser again, as Buy does.
                    const std::string checkoutUrl = order.checkoutUrl.empty() ? open.checkoutUrl : order.checkoutUrl;
                    app_.PostToUIThread([checkoutUrl]() { OpenURL(checkoutUrl); });
                }
            } else {
                progress(Tr("Asking the payment service"), -1.0);
                const auto started = StartCheckout(catalog->paymentUrl, job.appId, settings_.CodeInForce(Today()), http);
                if (!started) return fail(started.error);
                order = started.Value();
                remember({order.orderId, order.secret, job.appId, order.amount, order.currency, order.checkoutUrl, Today()});
                // The browser takes the buyer to Skrill; this job waits for the
                // order to be paid, cancelled or given up on.
                const std::string checkoutUrl = order.checkoutUrl;
                app_.PostToUIThread([checkoutUrl]() { OpenURL(checkoutUrl); });
            }
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(30);
            while (!order.Over()) {
                if (cancelRunning_) return fail(Error{ErrorCode::NetworkError, Tr("Cancelled; nothing was charged unless Skrill says so")});
                if (std::chrono::steady_clock::now() > deadline) return fail(Error{ErrorCode::NetworkError, TrF("No payment arrived within 30 minutes; the order {0} stays open", order.orderId)});
                progress(TrF("Waiting for the payment of {0} {1} in the browser (order {2})", order.amount, order.currency, order.orderId), -1.0);
                for (int i = 0; i < 50 && !cancelRunning_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
                const auto now = OrderStatus(catalog->paymentUrl, order.orderId, order.secret, http);
                if (now) order = now.Value();
            }
            remember({});   // over, one way or the other
            if (!order.Paid()) return fail(Error{ErrorCode::NetworkError, TrF("The purchase was {0}; nothing to install", order.status)});
            Licence licence;
            if (catalog->licenceKey.empty()) {
                licence.app = job.appId;
                licence.licenceId = order.licenceId;
                licence.orderId = order.orderId;
                licence.issued = order.paidAt.substr(0, 10);
            } else {
                const auto verified = VerifyLicence(order.licence, catalog->licenceKey);
                if (!verified) return fail(verified.error);
                if (verified.Value().app != job.appId) return fail(Error{ErrorCode::SignatureInvalid, Tr("The licence names another app"), verified.Value().app});
                licence = verified.Value();
            }
            if (const auto kept = engine_->Registry().PutLicence(LicenceRecordOf(order, licence)); !kept) return fail(kept.error);
            engine_->WriteLicenceFile(job.appId);   // for an app already installed (its trial); a fresh install writes it too
            if (const auto all = engine_->Registry().Licences()) { result.licences = all.Value(); result.haveLicences = true; }
            result.message = TrF("Bought {0} for {1} {2}: receipt no. {3}", nameOf(job.appId), order.amount, order.currency, order.receiptNo) +
                             (catalog->licenceKey.empty() ? " " + Tr("(the licence is kept unverified: the catalog names no licence key)") : std::string());
            return result;
        }
        case JobKind::Collect: {
            progress(Tr("Looking for unused components"), -1.0);
            const auto removed = engine_->CollectGarbage();
            takeInstalled();
            if (!removed) return fail(removed.error);
            result.message = removed.Value().empty() ? Tr("Nothing to remove")
                : TrN("Removed {n} unused component", "Removed {n} unused components", static_cast<long long>(removed.Value().size()));
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
        SetCatalog(result.catalog, TrF("Catalog sequence {0}, fetched {1} from {2}", result.catalog->sequence, result.state.fetchedAt, result.state.fetchedFrom));
    }
    if (result.haveInstalled) SetInstalled(std::move(result.installed));
    if (result.haveLicences) licences_ = std::move(result.licences);
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
        if (QueueEntry* e = FindEntry(jobId)) e->status = Tr("Cancelling…");
        RebuildQueuePanel();
        return;
    }
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [jobId](const Job& j) { return j.id == jobId; }), pending_.end());
    if (QueueEntry* e = FindEntry(jobId)) {
        e->state = QueueState::Cancelled;
        e->status = Tr("Cancelled before it started");
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
    if (auto button = e->pauseButton.lock()) button->SetText(e->paused ? Tr("Resume") : Tr("Pause"));
    if (auto label = e->statusLabel.lock()) label->SetText(e->paused ? TrF("Paused - {0}", e->status) : e->status);
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
        queueList_->AddChild(Theme::Caption("usQ.empty", Tr("Nothing queued. Installs, updates and removals line up here.")));
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
            auto pause = Theme::Button(id + ".pause", e.paused ? Tr("Resume") : Tr("Pause"), 72);
            pause->onClick = [this, jobId]() { TogglePause(jobId); };
            card->AddAction(pause);
            e.pauseButton = pause;
        }
        if (e.state == QueueState::Running || e.state == QueueState::Waiting) {
            auto cancel = Theme::Button(id + ".cancel", Tr("Cancel"), 72, Theme::ButtonKind::Danger);
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
        if (running->paused) detail += " " + Tr("(paused)");
        else if (running->fraction >= 0.0) detail += " " + Percent(running->fraction);
    }
    if (waiting) detail += (detail.empty() ? "" : " · ") + TrN("{n} waiting", "{n} waiting", static_cast<long long>(waiting));
    if (!running && !waiting) detail = finished ? TrN("{n} finished", "{n} finished", static_cast<long long>(finished)) : Tr("Nothing queued");
    downloads_->SetDetail(detail);
}

void StoreWindow::StopJobThread() {
    if (jobThread_.joinable()) jobThread_.join();
}

} // namespace UltraStore::Ui
