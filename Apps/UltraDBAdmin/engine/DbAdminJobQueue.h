// Apps/UltraDBAdmin/engine/DbAdminJobQueue.h
// One worker thread that runs jobs in the order they were posted. The window
// hands it every database call it makes for the tree, Browse and Structure -
// a schema list, a page, a row count - so a slow server or a connect timeout
// never freezes a repaint, and the calls on one session are made one after
// another, as the drivers require. A job reports back by posting to the UI
// thread itself; the queue knows nothing of windows.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace UltraDBAdmin {

class JobQueue {
public:
    using Job = std::function<void()>;

    JobQueue();
    ~JobQueue();                        // Stop()

    JobQueue(const JobQueue&) = delete;
    JobQueue& operator=(const JobQueue&) = delete;

    // Run `job` after every job posted before it. Ignored once stopped.
    void Post(Job job);

    // Jobs posted and not yet finished, the running one included.
    size_t Outstanding() const;

    // Drop the jobs still waiting, wait for the running one, end the thread.
    // Idempotent.
    void Stop();

private:
    void Run();

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> jobs_;
    size_t running_ = 0;
    bool stopping_ = false;
    std::thread worker_;
};

} // namespace UltraDBAdmin
