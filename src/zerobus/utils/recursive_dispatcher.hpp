#pragma once

#include "dispatch_queue.hpp"

namespace zerobus {









namespace utils {

/**
 * @class ThreadRecursiveDispatcher
 * @brief Thread-local function dispatcher with recursive dispatching support.
 *
 * This class provides a thread-local singleton dispatcher that manages a queue of functions
 * to be executed in a thread-safe manner. It supports recursive dispatching, ensuring that
 * functions enqueued during dispatch are handled correctly without causing stack overflows
 * or reentrancy issues.
 */
class ThreadRecursiveDispatcher {
public:

    /// Retrieves the thread-local singleton instance of the dispatcher.
    /**
     * @return Reference to the thread-local ThreadRecursiveDispatcher instance.
     */
    static ThreadRecursiveDispatcher &get_instance() {
        static thread_local ThreadRecursiveDispatcher disp;
        return disp;
    }

    /// Increments the recursion level counter.
    /**
     * @return A unique_ptr that, upon destruction, decrements the recursion level.
     *         This ensures proper tracking of nested dispatch calls.
     */

    auto inc_level() {
        ++_level;
        return std::unique_ptr<unsigned int,
          decltype([](unsigned int *x){--(*x);})>(&_level);
    }

    /// Enqueues a function to be executed by the dispatcher.
    /**
     * @tparam Fn Callable type with no arguments and no return value.
     * @param fn The function to enqueue.
     */
    template<std::invocable<> Fn>
    void enqueue(Fn &&fn) {
        _queue.push(std::move(fn));
    }

    /// Finish or continue execution of current task
    /**
     * If a task is currently in progress, it will be invoked recursively.
     * The task itself must be designed to handle potential reentrant execution.
     * For coroutine tasks, execution will resume from the current suspension point.
     * For non-coroutine tasks, the function must be robust against reentrancy.
     * When the call completes at the current recursion level, the task is considered finished,
     * and all recursion levels created during this cycle will be exited.
     * If dispatch() or finish() is called again recursively, the task remains marked as in progress.
     */
    void finish() {
        auto _=inc_level();

        if (_in_progress) {
            _queue.front();
            if (_in_progress) {
                _in_progress = false;
                _queue.pop_discard();
            }
        }
    }

    ///Finish current task and continue dispatching tasks in the queue
    void dispatch() noexcept{
        finish();
        while (!_queue.empty()) {
            //mark new task in progress
            _in_progress = true;
            //finish it
            finish();
        }
    }

    /**
     * @brief Gets the current recursion level.
     * @return The current recursion level as an unsigned integer.
     */

    auto get_level() const {
        return _level;
    }

    /**
     * @brief Checks if a dispatch is currently in progress.
     * @return True if dispatch is in progress, false otherwise.
     */
    auto get_in_progress() const {
        return _in_progress;
    }

protected:
    DispatchQueue<void()> _queue;
    unsigned int _level = 0;
    bool _in_progress = false;

    ThreadRecursiveDispatcher() = default;
};


}
}
