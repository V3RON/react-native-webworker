#include "TaskQueue.h"

namespace webworker {

void TaskQueue::enqueue(Task task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        task.runAt = std::chrono::steady_clock::now();
        immediateTasks_.push(std::move(task));
    }
    cv_.notify_one();
}

void TaskQueue::enqueueDelayed(Task task, std::chrono::milliseconds delay) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        task.runAt = std::chrono::steady_clock::now() + delay;
        delayedTasks_.push(std::move(task));
    }
    cv_.notify_one();
}

bool TaskQueue::cancel(uint64_t taskId) {
    std::lock_guard<std::mutex> lock(mutex_);
    cancelledIds_.insert(taskId);
    return true;
}

std::optional<Task> TaskQueue::dequeue(std::chrono::milliseconds maxWait) {
    std::unique_lock<std::mutex> lock(mutex_);

    auto deadline = std::chrono::steady_clock::now() + maxWait;

    while (true) {
        if (shuttingDown_) {
            return std::nullopt;
        }

        auto now = std::chrono::steady_clock::now();

        // Check immediate tasks first (higher priority)
        while (!immediateTasks_.empty()) {
            Task task = std::move(immediateTasks_.front());
            immediateTasks_.pop();

            // Skip cancelled tasks
            if (cancelledIds_.count(task.id) > 0) {
                cancelledIds_.erase(task.id);
                continue;
            }

            return task;
        }

        // Check delayed tasks that are ready to run
        while (!delayedTasks_.empty()) {
            const Task& top = delayedTasks_.top();

            // Skip cancelled tasks
            if (cancelledIds_.count(top.id) > 0) {
                cancelledIds_.erase(top.id);
                // Need to pop and continue (can't modify priority_queue in place)
                Task discarded = std::move(const_cast<Task&>(delayedTasks_.top()));
                delayedTasks_.pop();
                continue;
            }

            // Check if it's time to run this task
            if (top.runAt <= now) {
                Task task = std::move(const_cast<Task&>(delayedTasks_.top()));
                delayedTasks_.pop();
                return task;
            }

            // Task not ready yet
            break;
        }

        // Calculate how long to wait
        auto waitUntil = deadline;

        if (!delayedTasks_.empty()) {
            const auto& nextTask = delayedTasks_.top();
            if (nextTask.runAt < waitUntil) {
                waitUntil = nextTask.runAt;
            }
        }

        // Check if we've exceeded our deadline
        if (now >= deadline) {
            return std::nullopt;
        }

        // Wait for new tasks or until next delayed task is ready
        cv_.wait_until(lock, waitUntil);
    }
}

std::chrono::milliseconds TaskQueue::timeUntilNext() const {
    std::lock_guard<std::mutex> lock(mutex_);

    // If there are immediate tasks, return 0
    if (!immediateTasks_.empty()) {
        return std::chrono::milliseconds(0);
    }

    // If there are delayed tasks, calculate time until the next one
    if (!delayedTasks_.empty()) {
        auto now = std::chrono::steady_clock::now();
        const auto& nextTask = delayedTasks_.top();

        if (nextTask.runAt <= now) {
            return std::chrono::milliseconds(0);
        }

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            nextTask.runAt - now
        );
        return duration;
    }

    // No tasks, return a large value
    return std::chrono::milliseconds::max();
}

bool TaskQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return immediateTasks_.empty() && delayedTasks_.empty();
}

void TaskQueue::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shuttingDown_ = true;
    }
    cv_.notify_all();
}

} // namespace webworker
