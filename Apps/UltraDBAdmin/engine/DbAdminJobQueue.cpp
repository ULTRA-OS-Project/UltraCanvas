// Apps/UltraDBAdmin/engine/DbAdminJobQueue.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminJobQueue.h"

#include <utility>

namespace UltraDBAdmin {

JobQueue::JobQueue() : worker_([this] { Run(); }) {}

JobQueue::~JobQueue() { Stop(); }

void JobQueue::Post(Job job) {
    if (!job) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        jobs_.push_back(std::move(job));
    }
    wake_.notify_one();
}

size_t JobQueue::Outstanding() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return jobs_.size() + running_;
}

void JobQueue::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        jobs_.clear();
    }
    wake_.notify_all();
    if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) worker_.join();
}

void JobQueue::Run() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (stopping_) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
            running_ = 1;
        }
        job();
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = 0;
    }
}

} // namespace UltraDBAdmin
