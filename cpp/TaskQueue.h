#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <unordered_set>
#include <vector>
#include <cstdint>

namespace webworker {

/**
 * Task types following the HTML specification's event loop model.
 */
enum class TaskType {
    Message,        // postMessage from host
    Timer,          // setTimeout/setInterval fired
    Immediate,      // setImmediate (non-standard but useful)
    Close,          // self.close() requested
};

/**
 * Represents a single task in the event loop.
 */
struct Task {
    TaskType type;
    uint64_t id;
    std::function<void()> execute;
    std::chrono::steady_clock::time_point runAt;
    bool cancelled{false};
};

/**
 * Comparator for delayed tasks priority queue.
 * Earlier runAt = higher priority (min-heap).
 */
struct CompareByRunTime {
    bool operator()(const Task& a, const Task& b) const {
        return a.runAt > b.runAt;
    }
};

/**
 * Thread-safe task queue for the event loop.
 *
 * Manages both immediate tasks (FIFO) and delayed tasks (priority queue by time).
 * Following web semantics:
 * - Immediate tasks (messages, setTimeout(fn, 0)) have runAt = now
 * - Delayed tasks are ordered by their runAt time
 */
class TaskQueue {
public:
    TaskQueue() = default;
    ~TaskQueue() = default;

    // Non-copyable
    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;

    /**
     * Add a task to run immediately.
     * @param task The task to enqueue
     */
    void enqueue(Task task);

    /**
     * Add a task to run after a delay.
     * @param task The task to enqueue
     * @param delay Time to wait before running
     */
    void enqueueDelayed(Task task, std::chrono::milliseconds delay);

    /**
     * Cancel a pending task by ID.
     * @param taskId The ID of the task to cancel
     * @return true if task was found and cancelled
     */
    bool cancel(uint64_t taskId);

    /**
     * Get the next task to execute.
     * Blocks until a task is available or timeout expires.
     * @param maxWait Maximum time to wait
     * @return The next task, or nullopt if timeout expired
     */
    std::optional<Task> dequeue(std::chrono::milliseconds maxWait);

    /**
     * Get time until the next scheduled task.
     * @return Time until next task, or max duration if no tasks
     */
    std::chrono::milliseconds timeUntilNext() const;

    /**
     * Check if both queues are empty.
     * @return true if no pending tasks
     */
    bool empty() const;

    /**
     * Wake up any waiting dequeue() call.
     * Used during shutdown.
     */
    void shutdown();

private:
    // Immediate tasks (FIFO)
    std::queue<Task> immediateTasks_;

    // Delayed tasks (priority queue by runAt time)
    std::priority_queue<Task, std::vector<Task>, CompareByRunTime> delayedTasks_;

    // Set of cancelled task IDs for quick lookup
    std::unordered_set<uint64_t> cancelledIds_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool shuttingDown_{false};
};

} // namespace webworker
