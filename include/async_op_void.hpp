#ifndef ASYNC_OP_VOID_HPP
#define ASYNC_OP_VOID_HPP

#include <functional>
#include <memory>
#include <type_traits>
#include <chrono>
#include <vector>
#include <utility>
#ifdef ASYNC_USE_QT
# include <QTimer>
# include <QApplication>
# include <QThread>
#else
# include <glib.h>
#endif
#include <spdlog/spdlog.h>
#include <atomic>

namespace ao {

/**
 * @brief AsyncOp<void> specialization for operations without return values
 * 
 * Identical API to AsyncOp<T> but for void operations (signals, notifications, etc).
 * 
 * @note CRITICAL: Always capture asyncOp.promise() (not AsyncOp itself) in callbacks
 */
template<>
class AsyncOp<void> {
public:
    /**
     * @brief Shared state for AsyncOp<void>
     * 
     * Multiple AsyncOp instances can reference same State.
     * Provides idempotent helper methods: isPending(), resolveWith(), rejectWith()
     */
    struct State {
        enum Status { Pending, Resolved, Rejected };

        // Combined status and flags to reduce memory footprint
        struct StatusFlags {
            unsigned int status : 2;                    // 2 bits: Pending/Resolved/Rejected
            unsigned int error_code : 4;                // 4 bits: enough for ErrorCode enum
            unsigned int success_cb_is_propagating : 1; // 1 bit: flag for success callback
            unsigned int error_cb_is_propagating : 1;   // 1 bit: flag for error callback
            unsigned int reserved : 24;                 // 24 bits: reserved for future use
        };

        // Optimized member ordering for memory layout (largest to smallest)
        std::function<void()> success_cb;        // std::function is typically quite large
        std::function<void(ErrorCode)> error_cb; // std::function is typically quite large

        id_type op_id;                           // Usually size_t or similar
        StatusFlags status_flags;                // Combined status, error code and flags in 1 byte
        
        State() : op_id(detail::get_next_async_op_id()) {
            status_flags.status = Pending;
            status_flags.error_code = static_cast<unsigned int>(ErrorCode::None);
            status_flags.success_cb_is_propagating = 0;
            status_flags.error_cb_is_propagating = 0;
            status_flags.reserved = 0;
            spdlog::trace("AsyncOp[{}] state created", op_id);
        }

        ~State() {
            spdlog::trace("AsyncOp[{}] state destroyed", op_id);
        }

        bool isPending() const { return status_flags.status == Pending; }
        bool isResolved() const { return status_flags.status == Resolved; }
        bool isRejected() const { return status_flags.status == Rejected; }
        bool isSettled() const { return status_flags.status != Pending; }
        
        ErrorCode getErrorCode() const { 
            return static_cast<ErrorCode>(status_flags.error_code); 
        }
        
        void setErrorCode(ErrorCode code) {
            status_flags.error_code = static_cast<unsigned int>(code);
        }
        
        void setStatus(Status new_status) {
            status_flags.status = new_status;
        }

        bool canOverwriteSuccessCallback() const {
            return !success_cb || status_flags.success_cb_is_propagating;
        }

        bool canOverwriteErrorCallback() const {
            return !error_cb || status_flags.error_cb_is_propagating;
        }

        // Idempotent: does nothing if already settled
        void resolveWith() {
            if (!isPending()) return;

            setStatus(Resolved);
            if (success_cb) {
                success_cb();
            }
        }

        // Idempotent: does nothing if already settled
        void rejectWith(ErrorCode err) {
            if (!isPending()) return;
            setErrorCode(err);
            setStatus(Rejected);
            if (error_cb) {
                error_cb(err);
            }
        }

    };
    
    Promise<void> m_promise;

public:
    AsyncOp() : m_promise(std::make_shared<State>()) {}
    
    AsyncOp(const AsyncOp&) = default;
    AsyncOp& operator=(const AsyncOp&) = default;
    AsyncOp(AsyncOp&&) = default;
    AsyncOp& operator=(AsyncOp&&) = default;
    explicit AsyncOp(std::shared_ptr<State> state) 
    : m_promise(state ? std::move(state) : std::make_shared<State>()) {}

    bool isPending() const { return m_promise->isPending(); }
    bool isResolved() const { return m_promise->isResolved(); }
    bool isRejected() const { return m_promise->isRejected(); }
    bool isSettled() const { return m_promise->isSettled(); }
    ErrorCode errorCode() const { return m_promise->getErrorCode(); }
    id_type id() const { return m_promise->op_id; }

    /// @brief Get the underlying promise (shared state) for capturing in callbacks
    Promise<void> promise() const { return m_promise; }

    static AsyncOp<void> resolved() {
        AsyncOp<void> op;
        op.m_promise->resolveWith();
        spdlog::debug("AsyncOp[{}] created as resolved", op.id());
        return op;
    }
    
    static AsyncOp<void> rejected(ErrorCode err) {
        AsyncOp<void> op;
        op.m_promise->rejectWith(err);
        spdlog::debug("AsyncOp[{}] created as rejected with error {}", 
                     op.id(), err);
        return op;
    }
    
    /**
     * @brief Chain operation to execute on success
     * 
     * @tparam F Function type (T -> U or T -> AsyncOp<U>)
     * @param f Function to execute on success
     * @return New AsyncOp for chained operation
     * 
     * @warning Calling then() multiple times will be ignored. 
     * Only the FIRST then() executes. Use tap() for side effects or chain properly.
     */
    template<typename F>
    auto then(F&& f) -> AsyncOp<unwrap_async_op_t<typename std::invoke_result<F>::type>> {
        using InvokeResult = typename std::invoke_result<F>::type;
        using RetType = unwrap_async_op_t<InvokeResult>;

        spdlog::trace("AsyncOp[{}] chaining with then()", id());

        // Declare next_op early so we can return it if needed
        AsyncOp<RetType> next_op;
        auto next_state = next_op.m_promise;

        if (m_promise->canOverwriteSuccessCallback()) {
            // Set the new success callback (overwriting existing if present)
            m_promise->status_flags.success_cb_is_propagating = 0; // New callback is not propagation
            m_promise->success_cb = [f = std::forward<F>(f), next_state,
                                     op_id = m_promise->op_id]() mutable {
                spdlog::debug("AsyncOp[{}] executing then() callback", op_id);
                try {
                    if constexpr (is_async_op_v<InvokeResult>) {
                        // f returns AsyncOp - chain it
                        auto future_result = f();
                        future_result
                            .then([next_state](auto v) mutable {
                                if constexpr (std::is_void_v<RetType>) {
                                    next_state->resolveWith();
                                } else {
                                    next_state->resolveWith(std::move(v));
                                }
                            })
                            .onError([next_state](ErrorCode e) mutable { next_state->rejectWith(e); });
                    } else {
                        // f returns plain value or void
                        if constexpr (std::is_void_v<RetType>) {
                            f();
                            next_state->resolveWith();
                        } else {
                            auto result = f();
                            next_state->resolveWith(std::move(result));
                        }
                    }
                } catch (const std::exception &e) {
                    spdlog::error("AsyncOp[{}] exception in then(): {}", op_id, e.what());
                    next_state->rejectWith(ErrorCode::Exception);
                } catch (...) {
                    spdlog::error("AsyncOp[{}] unknown exception in then()", op_id);
                    next_state->rejectWith(ErrorCode::Exception);
                }
            };
        } else {
            spdlog::error("AsyncOp[{}] then() called but we cannot overwrite the success handler", id());
        }

        // Auto-propagate errors if no error handler
        if (m_promise->canOverwriteErrorCallback()) {
            m_promise->status_flags.error_cb_is_propagating = 1; // Set propagation flag to true
            m_promise->error_cb = [next_state, op_id = m_promise->op_id](ErrorCode err) mutable {
                spdlog::debug("AsyncOp[{}] propagating error {} to next op",
                             op_id, err);
                next_state->rejectWith(err);
            };
        }
        
        // If already settled, execute via micro-async (add_idle)
        if (!isPending()) {
            add_idle([state = m_promise]() {
                if (state->isResolved() && state->success_cb) {
                    state->success_cb();
                } else if (state->isRejected() && state->error_cb) {
                    state->error_cb(state->getErrorCode());
                }
                return false;
            });
        }

        return next_op;
    }
    
    /**
     * @brief Set success handler
     * @warning Calling onSuccess() multiple times will be ignored.
     * @warning If not the last handler in chain, the next handler must be recover() or otherwise().
     */
    AsyncOp<void>& onSuccess(std::function<void()> handler) {
        spdlog::trace("AsyncOp[{}] setting success handler", id());

        if (m_promise->canOverwriteSuccessCallback()) {
            // Set the flag to indicate this is not a propagation callback
            m_promise->status_flags.success_cb_is_propagating = 0;
            m_promise->success_cb = std::move(handler);
        } else {
            spdlog::error("AsyncOp[{}] cannot overwrite success handler", id());
        }
        if (isResolved()) {
            add_idle([state = m_promise]() {
                state->success_cb();
                return false;
            });
        }
        return *this;
    }

    /**
     * @brief Set error handler
     * @warning Calling onError() multiple times will be ignored.
     * @warning If not the last handler in chain, the next handler must be then().
     */
    AsyncOp<void>& onError(std::function<void(ErrorCode)> handler) {
        spdlog::trace("AsyncOp[{}] setting error handler", id());

        if (m_promise->canOverwriteErrorCallback()) {
            // Set the flag to indicate this is not a propagation callback
            m_promise->status_flags.error_cb_is_propagating = 0;
            m_promise->error_cb = std::move(handler);
        } else {
            spdlog::error("AsyncOp[{}] cannot overwrite error handler", id());
        }
        if (isRejected()) {
            add_idle([state = m_promise]() {
                state->error_cb(state->getErrorCode());
                return false;
            });
        }
        return *this;
    }

    /**
     * @brief Error recovery handler - attempts to recover from errors and convert to success
     * 
     * This is the error-handling counterpart to then(). Use recover() when you want to:
     * - Catch an error and provide a fallback value (error → success conversion)
     * - Retry the operation on failure
     * - Transform the error into a valid result
     * 
     * Handler can return void (recovery succeeds) or AsyncOp<void>, or re-throw to propagate error.
     * Success values automatically propagate through (no handler needed).
     * 
     * @tparam F Function type (ErrorCode -> T or ErrorCode -> AsyncOp<void>)
     * @param f Recovery function that converts error to success value
     * @return New AsyncOp that resolves with recovered value or propagates error
     * 
     * @note For creating separate success/error branches without conversion, use otherwise()
     */
    template<typename F>
    auto recover(F&& f) -> AsyncOp<void> {
        using InvokeResult = typename std::invoke_result<F, ErrorCode>::type;
        using RetType = unwrap_async_op_t<InvokeResult>;
        
        static_assert(std::is_void_v<RetType>, 
                    "recover() handler must return void or AsyncOp<void>");
        
        spdlog::trace("AsyncOp[{}] adding recover() handler", id());
        
        AsyncOp<void> next_op;
        auto next_state = next_op.m_promise;
        auto op_id = m_promise->op_id;

        if (m_promise->canOverwriteErrorCallback()) {

            // Set the error callback propagation flag to false (not a pure propagation, it's recovery)
            m_promise->status_flags.error_cb_is_propagating = 0;

            m_promise->error_cb = [f = std::forward<F>(f), next_state, op_id](ErrorCode err) mutable {
                spdlog::debug("AsyncOp[{}] executing recover() recovery", op_id);
                try {
                    if constexpr (is_async_op_v<InvokeResult>) {
                        // Handler returns AsyncOp<void> - chain recovery operation
                        auto recovery_op = f(err);
                        recovery_op
                            .then([next_state]() mutable { next_state->resolveWith(); })
                            .onError([next_state](ErrorCode e) mutable { next_state->rejectWith(e); });
                    } else {
                        // Handler returns void - recovery succeeded
                        f(err);
                        next_state->resolveWith();
                    }
                } catch (const ErrorCode &e) {
                    spdlog::debug("AsyncOp[{}] recover() re-threw error {}", op_id, e);
                    next_state->rejectWith(e);
                } catch (const std::exception &e) {
                    spdlog::error("AsyncOp[{}] exception in recover(): {}", op_id, e.what());
                    next_state->rejectWith(ErrorCode::Exception);
                } catch (...) {
                    spdlog::error("AsyncOp[{}] unknown exception in recover()", op_id);
                    next_state->rejectWith(ErrorCode::Exception);
                }
            };
        } else {
            spdlog::error("AsyncOp[{}] cannot overwrite error handler", id());
        }

        // Auto-propagate success - this is a pure propagation
        if (m_promise->canOverwriteSuccessCallback()) {
            m_promise->status_flags.success_cb_is_propagating = 1; // Set propagation flag to true
            m_promise->success_cb = [next_state, op_id]() mutable {
                spdlog::debug("AsyncOp[{}] propagating success through recover()", op_id);
                next_state->resolveWith();
            };
        }
        
        // If already settled, execute immediately
        if (!isPending()) {
            add_idle([state = m_promise]() {
                if (state->isResolved() && state->success_cb) {
                    state->success_cb();
                } else if (state->isRejected() && state->error_cb) {
                    state->error_cb(state->getErrorCode());
                }
                return false;
            });
        }
        
        return next_op;
    }

    /**
     * @brief Error branch handler - semantic alias for creating error handling branches
     * 
     * This is an alias for recover() but with clearer semantics for branching logic.
     * Use otherwise() when you want to express "if error, do this alternative path" rather than
     * "recover from error". Both functions are identical in implementation.
     * 
     * Common use cases:
     * - Creating if-then-else style branches: op.then(...).then(); op.otherwise(...).then();
     * - Expressing alternative execution paths on failure
     * - Making code intention clearer when not doing true error "recovery"
     * 
     * @tparam F Function type (ErrorCode -> void or ErrorCode -> AsyncOp<void>)
     * @param f Alternative path handler for error case
     * @return New AsyncOp with error branch attached
     * 
     * @note Functionally identical to recover(), use whichever name better expresses your intent:
     *       - recover() → "try to fix the error and continue"
     *       - otherwise() → "if error occurs, take this alternative path"
     * 
     * @example
     * // Using otherwise() for branching semantics
     * op = fetchFromCache(key);
     * op.then([]() { return processData(); })
     *   .then([]() { return processData1(); });
     * op.otherwise([](ErrorCode err) { return fetchFromDatabase(key); })
     *   .then([]() { return processData2(); });
     * // Using recover() for error recovery semantics
     * parseJson(text)
     *     .recover([](ErrorCode err) { return getDefaultConfig(); })
     *     .then([]() { return processData(); });
     */
    template<typename F>
    auto otherwise(F&& f) -> AsyncOp<void> {
        return recover(std::forward<F>(f));
    }

    /**
     * @brief Handle both success and error paths, converging to void
     * 
     * The next() method for AsyncOp<void> allows you to provide both a success handler
     * and an error handler, both of which return void or AsyncOp<void>. This enables
     * different side effects for success and error cases while continuing the same chain.
     * 
     * @tparam SuccessF Success handler function type (() -> void or () -> AsyncOp<void>)
     * @tparam ErrorF Error handler function type (ErrorCode -> void or ErrorCode -> AsyncOp<void>)
     * @param success_handler Function to handle success
     * @param error_handler Function to handle errors
     * @return New AsyncOp<void> that continues from either path
     * 
     * **Behavior:**
     * - If operation succeeds: success_handler executes, then next operation continues
     * - If operation fails: error_handler executes, then next operation continues
     * - Both handlers must return void (or AsyncOp<void>)
     * 
     * Benefit: Different processing, same continuation point
     */
    template<typename SuccessF, typename ErrorF>
    auto next(SuccessF&& success_handler, ErrorF&& error_handler) -> AsyncOp<void> {
        using SuccessInvokeResult = typename std::invoke_result<SuccessF>::type;
        using ErrorInvokeResult = typename std::invoke_result<ErrorF, ErrorCode>::type;
        
        // For void specialization, we need to check if handlers return void or AsyncOp<void>
        static_assert(
            std::is_void_v<SuccessInvokeResult> || is_async_op_v<SuccessInvokeResult>,
            "next() success handler must return void or AsyncOp<void>"
        );
        static_assert(
            std::is_void_v<ErrorInvokeResult> || is_async_op_v<ErrorInvokeResult>,
            "next() error handler must return void or AsyncOp<void>"
        );
        
        spdlog::trace("AsyncOp<void>[{}] adding next() with dual handlers", id());
        
        AsyncOp<void> next_op;

        auto next_state = next_op.m_promise;
        auto op_id = m_promise->op_id;

        if (m_promise->canOverwriteSuccessCallback()) {
            m_promise->status_flags.success_cb_is_propagating = 0;
            // Set success handler
            m_promise->success_cb = [success_handler = std::forward<SuccessF>(success_handler), next_state,
                                     op_id]() mutable {
                spdlog::debug("AsyncOp<void>[{}] executing next() success handler", op_id);
                try {
                    if constexpr (is_async_op_v<SuccessInvokeResult>) {
                        // Handler returns AsyncOp<void>
                        auto result_op = success_handler();
                        result_op
                            .then([next_state]() mutable { next_state->resolveWith(); })
                            .onError([next_state](ErrorCode e) mutable { next_state->rejectWith(e); });
                    } else {
                        // Handler returns void
                        success_handler();
                        next_state->resolveWith();
                    }
                } catch (const std::exception &e) {
                    spdlog::error("AsyncOp[{}] exception in next() success handler: {}", op_id, e.what());
                    next_state->rejectWith(ErrorCode::Exception);
                } catch (...) {
                    spdlog::error("AsyncOp[{}] unknown exception in next() success handler", op_id);
                    next_state->rejectWith(ErrorCode::Exception);
                }
            };
        } else {
            spdlog::error("AsyncOp[{}] cannot overwrite success handler", id());
        }

        if (m_promise->canOverwriteErrorCallback()) {
            m_promise->status_flags.error_cb_is_propagating = 0;
            // Set error handler
            m_promise->error_cb = [error_handler = std::forward<ErrorF>(error_handler), next_state,
                                   op_id](ErrorCode err) mutable {
                spdlog::debug("AsyncOp<void>[{}] executing next() error handler", op_id);
                try {
                    if constexpr (is_async_op_v<ErrorInvokeResult>) {
                        // Handler returns AsyncOp<void>
                        auto result_op = error_handler(err);
                        result_op
                            .then([next_state]() mutable { next_state->resolveWith(); })
                            .onError([next_state](ErrorCode e) mutable { next_state->rejectWith(e); });
                    } else {
                        // Handler returns void
                        error_handler(err);
                        next_state->resolveWith();
                    }
                } catch (const ErrorCode &e) {
                    // Re-thrown ErrorCode
                    spdlog::debug("AsyncOp[{}] next() error handler re-threw error {}", op_id, e);
                    next_state->rejectWith(e);
                } catch (const std::exception &e) {
                    spdlog::error("AsyncOp[{}] exception in next() error handler: {}", op_id, e.what());
                    next_state->rejectWith(ErrorCode::Exception);
                } catch (...) {
                    spdlog::error("AsyncOp[{}] unknown exception in next() error handler", op_id);
                    next_state->rejectWith(ErrorCode::Exception);
                }
            };
        } else {
            spdlog::error("AsyncOp[{}] cannot overwrite error handler", id());
        }

        // If already settled, execute immediately
        if (!isPending()) {
            add_idle([state = m_promise]() {
                if (state->isResolved() && state->success_cb) {
                    state->success_cb();
                } else if (state->isRejected() && state->error_cb) {
                    state->error_cb(state->getErrorCode());
                }
                return false; // G_SOURCE_REMOVE
            });
        }
        
        return next_op;
    }
    
    /**
     * @brief Add timeout to operation
     * 
     * Creates new AsyncOp that:
     * - Resolves with same value if operation completes within duration
     * - Rejects with ErrorCode::Timeout if duration elapses first
     * 
     * @param duration Timeout duration
     * @return New AsyncOp with timeout applied
     */
    AsyncOp<void> timeout(std::chrono::milliseconds duration) {
        spdlog::debug("AsyncOp[{}] setting timeout of {}ms", id(), duration.count());
        
        AsyncOp<void> timed_op;
        auto timed_state = timed_op.m_promise;
        auto expired = std::make_shared<bool>(false);
        
        auto timer = add_timeout(duration, [timed_state, expired]() {
            if (!*expired) {
                *expired = true;
                spdlog::warn("AsyncOp[{}] timed out", timed_state->op_id);
                timed_state->rejectWith(ErrorCode::Timeout);
            }
            return false;
        });
        spdlog::trace("AsyncOp[{}] created timer {} for timeout", id(), FORMAT_TIMER(timer));
        
        // Cancel timer on success
        this->then([timed_state, expired, timer]() {
            if (!*expired) {
                *expired = true;
                spdlog::debug("AsyncOp completed before timeout, removing timer {}", FORMAT_TIMER(timer));
                remove_timeout(timer);
                timed_state->resolveWith();
            }
        }).onError([timed_state, expired, timer](ErrorCode err) {
            if (!*expired) {
                *expired = true;
                spdlog::debug("AsyncOp failed before timeout, removing timer {}", FORMAT_TIMER(timer));
                remove_timeout(timer);
                timed_state->rejectWith(err);
            }
        });
        
        return timed_op;
    }

    /**
     * @brief Execute side effect without modifying value
     * 
     * Useful for logging, metrics, debugging. Value passes through unchanged.
     * Exceptions in side effect are caught and logged but don't affect the chain.
     */
    template<typename F>
    AsyncOp<void> tap(F&& side_effect_fn) {
        spdlog::trace("AsyncOp[{}] adding tap", id());
        
        return this->then([f = std::forward<F>(side_effect_fn)]() mutable {
            try {
                f();
            } catch (const std::exception& e) {
                spdlog::warn("Exception in tap(): {}", e.what());
            } catch (...) {
                spdlog::warn("Unknown exception in tap()");
            }
            return;
        });
    }

    /**
     * @brief Execute cleanup function regardless of success/failure
     * 
     * Similar to try-finally. Cleanup executes whether operation succeeds or fails.
     * Original result/error is preserved and propagated.
     */
    template<typename F>
    AsyncOp<void> finally(F&& cleanup_fn) {
        spdlog::trace("AsyncOp[{}] adding finally", id());
        
        AsyncOp<void> result;
        auto result_state = result.m_promise;
        auto cleanup = std::make_shared<F>(std::forward<F>(cleanup_fn));
        auto cleanup_done = std::make_shared<bool>(false);

        if (m_promise->canOverwriteSuccessCallback()) {
            m_promise->status_flags.success_cb_is_propagating = 0;
            // Success path: cleanup then propagate
            m_promise->success_cb = [cleanup, cleanup_done, result_state,
                                     op_id = m_promise->op_id]() mutable {
                spdlog::debug("AsyncOp[{}] executing finally (success)", op_id);

                if (!*cleanup_done) {
                    *cleanup_done = true;
                    try {
                        (*cleanup)();
                    } catch (const std::exception &e) {
                        spdlog::error("Exception in finally(): {}", e.what());
                    } catch (...) {
                        spdlog::error("Unknown exception in finally()");
                    }
                }

                result_state->resolveWith();
            };
        } else {
            spdlog::error("AsyncOp[{}] cannot overwrite success handler", id());
        }

        if (m_promise->canOverwriteErrorCallback()) {
            m_promise->status_flags.error_cb_is_propagating = 0;
            // Error path: cleanup then propagate error
            m_promise->error_cb = [cleanup, cleanup_done, result_state,
                                   op_id = m_promise->op_id](ErrorCode err) mutable {
                spdlog::debug("AsyncOp[{}] executing finally (error)", op_id);

                if (!*cleanup_done) {
                    *cleanup_done = true;
                    try {
                        (*cleanup)();
                    } catch (const std::exception &e) {
                        spdlog::error("Exception in finally(): {}", e.what());
                    } catch (...) {
                        spdlog::error("Unknown exception in finally()");
                    }
                }

                result_state->rejectWith(err);
            };
        } else {
            spdlog::error("AsyncOp[{}] cannot overwrite error handler", id());
        }

        // Execute immediately if already settled
        if (isResolved() && m_promise->success_cb) {
            ao::add_idle([promise = m_promise]() {
                promise->success_cb();
                return false;
            });
        } else if (isRejected() && m_promise->error_cb) {
            ao::add_idle([promise = m_promise]() {
                promise->error_cb(promise->getErrorCode());
                return false;
            });
        }
        
        return result;
    }

    /**
     * @brief Provide fallback value on any error

     * @param log_message Optional warning message to log on error
     */
    AsyncOp<void> orElse(const std::string& log_message = "") {
        return this->otherwise([
                            log_message,
                            op_id = m_promise->op_id](ErrorCode err) mutable -> void {
            if (!log_message.empty()) {
                spdlog::warn("AsyncOp[{}] {}: error {}", op_id, log_message, err);
            }
            return;
        });
    }

    /**
     * @brief Recover from specific error type only
     * @param error_to_handle Specific ErrorCode to handle
     * @param handler Recovery function for this error type
     * @note Re-throws other error types
     */
    template<typename F>
    AsyncOp<void> recoverFrom(ErrorCode error_to_handle, F&& handler) {
        return this->recover([error_to_handle, 
                            handler = std::forward<F>(handler)](ErrorCode err) mutable -> void {
            if (err == error_to_handle) {
                return handler(err);
            }
            throw err;  // Re-throw non-matching errors
        });
    }
    
    // Idempotent resolve/reject
    void resolve() {
        if (!isPending()) {
            spdlog::warn("AsyncOp[{}] already completed, ignoring resolve", m_promise->op_id);
            return;
        }
        
        spdlog::debug("AsyncOp[{}] resolved", m_promise->op_id);

        m_promise->setStatus(State::Resolved);
        
        if (m_promise->success_cb) {
            m_promise->success_cb();
        }
    }

    void reject(ErrorCode err) {
        if (!isPending()) {
            spdlog::warn("AsyncOp[{}] already completed, ignoring reject", m_promise->op_id);
            return;
        }

        spdlog::debug("AsyncOp[{}] rejected with error {}", m_promise->op_id, err);
        m_promise->setErrorCode(err);
        m_promise->setStatus(State::Rejected);

        if (m_promise->error_cb) {
            m_promise->error_cb(err);
        }
    }
    
    using value_type = void;
};

} // namespace ao

#endif // ASYNC_OP_VOID_HPP
