#pragma once
#include <queue>


namespace zerobus {

namespace utils {

/**
 * @class RecursiveDispatcher
 *
 * This class provides a thread-local singleton dispatcher that manages a queue of functions
 * to be executed in a thread-safe manner. It supports correct dispatching order
 * even if dispatch() is called recursively.
 *
 * When dispatch() is called recursively, current task function is called again, which
 * allows it to continue in its operation. The next function is dispatched once the
 * current function is exited by its return (doesn't necessery need to exit all
 * recursive levels).
 *
 * The reentrant function must handle correctly its state. Once the function exits
 * by return, its closure is destroyed. If it still need to manage state while
 * returning from its recursion, it needs to move its state to the stack and maintain
 * link to this state for every futher recursion.
 *
 *
 */
template<typename Subj>
class RecursiveDispatcher {
public:

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
            _queue.front()();
            if (_in_progress) {
                _in_progress = false;
                _queue.pop();
            }
        }
    }

    ///Finish current task and continue dispatching tasks in the queue
    void dispatch() noexcept{
        _dispatching = true;
        finish();
        while (!_queue.empty()) {
            //mark new task in progress
            _in_progress = true;
            //finish it
            finish();
        }
        if (_level == 0) {
            _dispatching = false;
        }
    }


    void dispatch_if_needed() noexcept {
        if (!_dispatching) dispatch();
    }

    /**
     * @brief Gets the current recursion level.
     * @return The current recursion level as an unsigned integer.
     */

    auto get_level() const {
        return _level;
    }

    /**
     * @brief Checks whether top most function is still considered as running
     * @retval true current function is running
     * @retval false current function has exited. If is_dispatching is true, we
     * still waiting to leave some recursion to continue in dispatching
     */
    auto is_in_progress() const {
        return _in_progress;
    }

    ///Returns true, if dispatcher is dispatching
    /**
     * @retval true dispatching
     * @retval false idle
     */
    auto is_dispatching() const {
        return _dispatching;
    }

    RecursiveDispatcher() = default;


protected:
    std::queue<Subj> _queue;
    unsigned int _level = 0;
    bool _in_progress = false;
    bool _dispatching = false;

};


}
}
