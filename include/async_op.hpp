/**
 * @file async_op.hpp
 * @brief Lightweight asynchronous operation chaining for embedded Linux
 * @author pansz
 * @date 2026.2.8
 *
 * AsyncOp 2.4
 *
 * Promise/Future-like pattern for chaining async I/O operations. Eliminates callback hell
 * and improves code readability. Designed for embedded systems with moderate memory constraints.
 *
 * Backend Support:
 * - GLib 2.0+ (default)
 * - Qt 5.12+ (define ASYNC_USE_QT)
 * - Extensible to other event loops via ao_event_loop.hpp
 *
 * @note See async_op_doc.md for comprehensive usage guide
 * @note Requires ao_event_loop.hpp, spdlog
 * @note C++17 or later
 *
 * v2.4 Changes:
 * - Added: cancel() - Reject pending operations with configurable error code
 * - Added: filter() - Dual-path success/error filtering with throw/return semantics
 * - Deprecated: orElse() - Use otherwise() with explicit logic instead
 * - Deprecated: recoverFrom() - Use filter() with error filter instead
 */

#ifndef ASYNC_OP_HPP
#define ASYNC_OP_HPP

#include <functional>
#include <memory>
#include <type_traits>
#include <chrono>
#include <vector>
#include <utility>
#include <cassert>

// Event loop abstraction layer - Provides unified interface for scheduling timers,
// idle callbacks, and main thread marshaling across different backends (GLib/Qt).
// Key functions: add_timeout(), add_idle(), remove_timeout(), is_main_thread(), invoke_main().
// See ao_event_loop.hpp for backend details and extension guide.
#include "ao_event_loop.hpp"

namespace ao {
enum class ErrorCode {
    None = 0,
    Timeout,
    NetworkError,
    InvalidResponse,
    Cancelled,
    Exception,
    MaxRetriesExceeded,
    Unknown
};

// Static assertion to ensure ErrorCode enum fits in the 4-bit bitfield
static_assert(static_cast<int>(ErrorCode::Unknown) < 16,
              "ErrorCode enum exceeds 4-bit bitfield capacity");

/**
 * @brief Convert ErrorCode to string name
 * @param ec Error code
 * @return String representation (e.g., "NetworkError")
 */
inline const char* error_code_name(ErrorCode ec) {
    switch (ec) {
        case ErrorCode::None:              return "None";
        case ErrorCode::Timeout:           return "Timeout";
        case ErrorCode::NetworkError:      return "NetworkError";
        case ErrorCode::InvalidResponse:   return "InvalidResponse";
        case ErrorCode::Cancelled:         return "Cancelled";
        case ErrorCode::Exception:         return "Exception";
        case ErrorCode::MaxRetriesExceeded: return "MaxRetriesExceeded";
        case ErrorCode::Unknown:           return "Unknown";
        default:                           return "Unknown";
    }
}
} // namespace ao

// fmt/spdlog formatter for ErrorCode - enables automatic formatting
// Usage: spdlog::info("Error: {}", error_code);
template <>
struct fmt::formatter<ao::ErrorCode> {
    // Parse format specification (we don't use any custom format specs)
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
        return ctx.begin();
    }
    
    // Format the ErrorCode
    template <typename FormatContext>
    auto format(const ao::ErrorCode& ec, FormatContext& ctx) const -> decltype(ctx.out()) {
        return fmt::format_to(ctx.out(), "{}", ao::error_code_name(ec));
    }
};

namespace ao { 
// Forward declarations
template<typename T>
class AsyncOp;

// Type traits for AsyncOp detection and unwrapping
template<typename T>
struct is_async_op : std::false_type {};

template<typename T>
struct is_async_op<AsyncOp<T>> : std::true_type {};

template<typename T>
inline constexpr bool is_async_op_v = is_async_op<T>::value;

template<typename T>
struct unwrap_async_op {
    using type = T;
};

template<typename T>
struct unwrap_async_op<AsyncOp<T>> {
    using type = T;
};

template<typename T>
using unwrap_async_op_t = typename unwrap_async_op<T>::type;

// Helper template for dependent static_assert in if constexpr branches
template<typename>
inline constexpr bool dependent_false_v = false;

template<typename T>
using Promise = std::shared_ptr<typename AsyncOp<T>::State>;

/**
 * @brief Asynchronous operation with Promise/Future semantics
 * 
 * Represents a pending async operation that resolves with value T or rejects with ErrorCode.
 * Operations chain via .then() and handle errors via .onError().
 * 
 * asyncOp behaves as future, while promise is the shared_ptr of state.
 * 
 * @tparam T Result value type
 * 
 * @note CRITICAL: Always capture promise (not the AsyncOp itself) in callbacks:
 * @code
 * AsyncOp<int> myFunc1() {
 *     AsyncOp<int> future;
 *     add_timeout(100ms, [promise = future.promise()]() {
 *         promise->resolveWith(42);
 *         return false;
 *     });
 *     return future;
 * }
 * AsyncOp<int> myFunc2() {
 *     auto promise = makePromise<int>();
 *     add_timeout(100ms, [promise]() {
 *         promise->resolveWith(42);
 *         return false;
 *     });
 *     return AsyncOp(promise);
 * }
 * @endcode
 */
template<typename T>
class AsyncOp {
public:
    /**
     * @brief Shared state managed by shared_ptr for safe copy/move
     * 
     * Multiple AsyncOp instances can reference same State.
     * Provides idempotent helper methods: isPending(), resolveWith(), rejectWith()
     */
    struct State {
        enum Status { Pending, Resolved, Rejected };

        // Combined status and flags to reduce memory footprint
        struct StatusFlags {
            unsigned int status : 2;                    // 2 bits: Pending/Resolved/Rejected
            unsigned int error_code : 4;                // 4 bits: ErrorCode enum (static_assert enforces < 16 values)
            unsigned int success_cb_is_propagating : 1; // 1 bit: flag for success callback
            unsigned int error_cb_is_propagating : 1;   // 1 bit: flag for error callback
            unsigned int reserved : 24;                 // 24 bits: reserved for future use
        };

        // Optimized member ordering for memory layout (largest to smallest)
        std::function<void(T)> success_cb;       // std::function is typically quite large
        std::function<void(ErrorCode)> error_cb; // std::function is typically quite large
        T result_value;                          // Largest: could be std::string, nlohmann::json, etc.
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
        void resolveWith(T value) {
            if (!isPending()) return;
            result_value = std::move(value);
            setStatus(Resolved);
            if (success_cb) {
                success_cb(std::move(result_value));
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
    
    Promise<T> m_promise;

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
    Promise<T> promise() const { return m_promise; }

    static AsyncOp<T> resolved(T value) {
        AsyncOp<T> op;
        op.m_promise->resolveWith(std::move(value));
        spdlog::debug("AsyncOp[{}] created as resolved", op.id());
        return op;
    }
    
    static AsyncOp<T> rejected(ErrorCode err) {
        AsyncOp<T> op;
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
     * @warning Calling then() after onSuccess() is a logic error.
     */
    template<typename F>
    auto then(F&& f) -> AsyncOp<unwrap_async_op_t<typename std::invoke_result<F, T>::type>> {
        using InvokeResult = typename std::invoke_result<F, T>::type;
        using RetType = unwrap_async_op_t<InvokeResult>;

        spdlog::trace("AsyncOp[{}] chaining with then()", id());

        // Declare next_op early so we can return it if needed
        AsyncOp<RetType> next_op;
        auto next_state = next_op.m_promise;

        if (!m_promise->canOverwriteSuccessCallback()) {
            spdlog::error("AsyncOp[{}] then() called after terminal handler - callback ignored", id());
            assert(false && "then() cannot overwrite existing non-propagating callback");
        } else {
            // Set the new success callback (overwriting existing if present)
            m_promise->status_flags.success_cb_is_propagating = 0; // New callback is not propagation
            m_promise->success_cb = [f = std::forward<F>(f), next_state,
                                     op_id = m_promise->op_id](T val) mutable {
                spdlog::debug("AsyncOp[{}] executing then() callback", op_id);
                try {
                    if constexpr (is_async_op_v<InvokeResult>) {
                        // f returns AsyncOp - chain it
                        auto future_result = f(std::move(val));
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
                            f(std::move(val));
                            next_state->resolveWith();
                        } else {
                            auto result = f(std::move(val));
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
                    state->success_cb(std::move(state->result_value));
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
    AsyncOp<T>& onSuccess(std::function<void(T)> handler) {
        spdlog::trace("AsyncOp[{}] setting success handler", id());

        if (!m_promise->canOverwriteSuccessCallback()) {
            spdlog::error("AsyncOp[{}] cannot overwrite success handler - onSuccess() called after terminal handler", id());
            assert(false && "onSuccess() cannot overwrite existing non-propagating callback");
        } else {
            // Set the flag to indicate this is not a propagation callback
            m_promise->status_flags.success_cb_is_propagating = 0;
            m_promise->success_cb = std::move(handler);
        }

        if (isResolved()) {
            add_idle([state = m_promise]() {
                state->success_cb(std::move(state->result_value));
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
    AsyncOp<T>& onError(std::function<void(ErrorCode)> handler) {
        spdlog::trace("AsyncOp[{}] setting error handler", id());

        if (!m_promise->canOverwriteErrorCallback()) {
            spdlog::error("AsyncOp[{}] cannot overwrite error handler - onError() called after terminal handler", id());
            assert(false && "onError() cannot overwrite existing non-propagating callback");
        } else {
            // Set the flag to indicate this is not a propagation callback
            m_promise->status_flags.error_cb_is_propagating = 0;
            m_promise->error_cb = std::move(handler);
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
     * Handler can return T (recovery succeeds) or AsyncOp<T>, or re-throw to propagate error.
     * Success values automatically propagate through (no handler needed).
     * 
     * @tparam F Function type (ErrorCode -> T or ErrorCode -> AsyncOp<T>)
     * @param f Recovery function that converts error to success value
     * @return New AsyncOp that resolves with recovered value or propagates error
     * 
     * @note For creating separate success/error branches without conversion, use otherwise()
     */
    template<typename F>
    auto recover(F&& f) -> AsyncOp<T> {
        using InvokeResult = typename std::invoke_result<F, ErrorCode>::type;
        using RetType = unwrap_async_op_t<InvokeResult>;
        
        static_assert(std::is_same_v<RetType, T>, 
                    "recover() handler must return same type T as AsyncOp<T>");
        
        spdlog::trace("AsyncOp[{}] adding recover() handler", id());
        
        AsyncOp<T> next_op;
        auto next_state = next_op.m_promise;
        auto op_id = m_promise->op_id;

        if (!m_promise->canOverwriteErrorCallback()) {
            spdlog::error("AsyncOp[{}] recover() called after terminal error handler", id());
            assert(false && "recover() cannot overwrite existing non-propagating error callback");
        } else {
            // Set the error callback propagation flag to false (not a pure propagation, it's recovery)
            m_promise->status_flags.error_cb_is_propagating = 0;

            m_promise->error_cb = [f = std::forward<F>(f), next_state, op_id](ErrorCode err) mutable {
                spdlog::debug("AsyncOp[{}] executing recover() recovery", op_id);
                try {
                    if constexpr (is_async_op_v<InvokeResult>) {
                        // Handler returns AsyncOp<T> - chain recovery operation
                        auto recovery_op = f(err);
                        recovery_op
                            .then([next_state](T value) mutable { next_state->resolveWith(std::move(value)); })
                            .onError([next_state](ErrorCode e) mutable { next_state->rejectWith(e); });
                    } else {
                        // Handler returns T - recovery succeeded
                        T recovery_value = f(err);
                        next_state->resolveWith(std::move(recovery_value));
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
        }

        // Auto-propagate success - this is a pure propagation
        // Note: recover() only handles errors; success passes through to next chained operation.
        // This enables branching: op.then(...); op.recover(...).then(...)
        if (m_promise->canOverwriteSuccessCallback()) {
            m_promise->status_flags.success_cb_is_propagating = 1;
            m_promise->success_cb = [next_state, op_id](T val) mutable {
                spdlog::debug("AsyncOp[{}] propagating success through recover()", op_id);
                next_state->resolveWith(std::move(val));
            };
        }
        // If canOverwriteSuccessCallback() returns false, it's not a problem since this
        // is just the default propagating callback and an existing terminal handler is already set.
        
        // If already settled, execute immediately
        if (!isPending()) {
            add_idle([state = m_promise]() {
                if (state->isResolved() && state->success_cb) {
                    state->success_cb(std::move(state->result_value));
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
     * @tparam F Function type (ErrorCode -> T or ErrorCode -> AsyncOp<T>)
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
     * op.then([](auto data) { return processData(data); })
     *   .then([](auto data1) { return processData1(data); });
     * op.otherwise([](ErrorCode err) { return fetchFromDatabase(key); })
     *   .then([](auto data2) { return processData2(data); });
     * // Using recover() for error recovery semantics
     * parseJson(text)
     *     .recover([](ErrorCode err) { return getDefaultConfig(); })
     *     .then([](auto data) { return processData(data); });
     */
    template<typename F>
    auto otherwise(F&& f) -> AsyncOp<T> {
        return recover(std::forward<F>(f));
    }

    /**
     * @brief Handle both success and error paths, converging to same type
     * 
     * The next() method allows you to provide both a success handler and an error handler,
     * both of which must return the same type U. This enables different processing for
     * success and error cases while continuing the same chain afterward.
     * 
     * @tparam SuccessF Success handler function type (T -> U or T -> AsyncOp<U>)
     * @tparam ErrorF Error handler function type (ErrorCode -> U or ErrorCode -> AsyncOp<U>)
     * @param success_handler Function to handle success values
     * @param error_handler Function to handle errors
     * @return New AsyncOp<U> that continues from either path
     * 
     * **Behavior:**
     * - If operation succeeds: success_handler executes, result goes to next operation
     * - If operation fails: error_handler executes, result goes to next operation
     * - Both handlers must return the same type U (or AsyncOp<U>)
     * 
     * Benefit: Different processing, same continuation point
     */
    template<typename SuccessF, typename ErrorF>
    auto next(SuccessF&& success_handler, ErrorF&& error_handler) {
        using SuccessInvokeResult = typename std::invoke_result<SuccessF, T>::type;
        using ErrorInvokeResult = typename std::invoke_result<ErrorF, ErrorCode>::type;
        using SuccessRetType = unwrap_async_op_t<SuccessInvokeResult>;
        using ErrorRetType = unwrap_async_op_t<ErrorInvokeResult>;

        if constexpr (std::is_null_pointer_v<ErrorF>) {
            if constexpr (std::is_null_pointer_v<SuccessF>) {
                static_assert(dependent_false_v<SuccessF>, "next() does not accept nullptr, use filter() instead.");
            } else {
                using RetType = SuccessRetType;
                AsyncOp<RetType> next_op;
                static_assert(dependent_false_v<SuccessF>, "next() does not accept nullptr, use then() instead.");
                return next_op;
            }
        } else if constexpr (std::is_null_pointer_v<SuccessF>) {
            static_assert(dependent_false_v<ErrorF>, "next() does not accept nullptr, use recover() instead.");
        } else {
            // Both handlers provided - must return same type
            static_assert(std::is_same_v<SuccessRetType, ErrorRetType>,
                          "next() success and error handlers must return the same type");
        }
        using RetType = SuccessRetType;
        
        spdlog::trace("AsyncOp[{}] adding next() with dual handlers", id());

        AsyncOp<RetType> next_op;
        auto next_state = next_op.m_promise;
        auto op_id = m_promise->op_id;

        if (!m_promise->canOverwriteSuccessCallback()) {
            spdlog::error("AsyncOp[{}] next() called after terminal success handler", id());
            assert(false && "next() cannot overwrite existing non-propagating success callback");
        } else {
            m_promise->status_flags.success_cb_is_propagating = 0;
            // Set success handler
            m_promise->success_cb = [success_handler = std::forward<SuccessF>(success_handler), next_state,
                                     op_id](T val) mutable {
                spdlog::debug("AsyncOp[{}] executing next() success handler", op_id);
                try {
                    if constexpr (is_async_op_v<SuccessInvokeResult>) {
                        // Handler returns AsyncOp<RetType>
                        auto result_op = success_handler(std::move(val));
                        result_op
                            .then([next_state](RetType value) mutable {
                                next_state->resolveWith(std::move(value));
                            })
                            .onError([next_state](ErrorCode e) mutable { next_state->rejectWith(e); });
                    } else {
                        // Handler returns RetType
                        RetType result = success_handler(std::move(val));
                        next_state->resolveWith(std::move(result));
                    }
                } catch (const std::exception &e) {
                    spdlog::error("AsyncOp[{}] exception in next() success handler: {}", op_id, e.what());
                    next_state->rejectWith(ErrorCode::Exception);
                } catch (...) {
                    spdlog::error("AsyncOp[{}] unknown exception in next() success handler", op_id);
                    next_state->rejectWith(ErrorCode::Exception);
                }
            };
        }

        if (!m_promise->canOverwriteErrorCallback()) {
            spdlog::error("AsyncOp[{}] next() called after terminal error handler", id());
            assert(false && "next() cannot overwrite existing non-propagating error callback");
        } else {
            m_promise->status_flags.error_cb_is_propagating = 0;
            // Set error handler
            m_promise->error_cb = [error_handler = std::forward<ErrorF>(error_handler), next_state,
                                op_id](ErrorCode err) mutable {
                spdlog::debug("AsyncOp[{}] executing next() error handler", op_id);
                try {
                    if constexpr (is_async_op_v<ErrorInvokeResult>) {
                        // Handler returns AsyncOp<RetType>
                        auto result_op = error_handler(err);
                        result_op
                            .then([next_state](RetType value) mutable {
                                next_state->resolveWith(std::move(value)); })
                            .onError([next_state](ErrorCode e) mutable { next_state->rejectWith(e); });
                    } else {
                        // Handler returns RetType
                        RetType result = error_handler(err);
                        next_state->resolveWith(std::move(result));
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
        }

        // If already settled, execute immediately
        if (!isPending()) {
            add_idle([state = m_promise]() {
                if (state->isResolved() && state->success_cb) {
                    state->success_cb(std::move(state->result_value));
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
     * 
     * **IMPORTANT: Timer starts immediately when timeout() is called**, not when
     * the previous operation completes. The timeout covers all operations in the
     * chain from the point where timeout() is called.
     * 
     * @code
     * // Timeout applies to both op1 AND op2 combined (timer starts immediately)
     * op1().then([](){ return op2(); }).timeout(3000ms);
     * 
     * // Timeout applies only to op2 (timer starts when op2's chain is set up)
     * op1().then([](){ return op2().timeout(3000ms); });
     * @endcode
     */
    AsyncOp<T> timeout(std::chrono::milliseconds duration) {
        spdlog::debug("AsyncOp[{}] setting timeout of {}ms", id(), duration.count());
        
        AsyncOp<T> timed_op;
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
        this->then([timed_state, expired, timer](T val) {
            if (!*expired) {
                *expired = true;
                spdlog::debug("AsyncOp completed before timeout, removing timer {}", FORMAT_TIMER(timer));
                remove_timeout(timer);
                timed_state->resolveWith(std::move(val));
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
    AsyncOp<T> tap(F&& side_effect_fn) {
        spdlog::trace("AsyncOp[{}] adding tap", id());
        
        return this->then([f = std::forward<F>(side_effect_fn)](T value) mutable {
            try {
                f(value);
            } catch (const std::exception& e) {
                spdlog::warn("Exception in tap(): {}", e.what());
            } catch (...) {
                spdlog::warn("Unknown exception in tap()");
            }
            return value;
        });
    }

    /**
     * @brief Execute cleanup function regardless of success/failure
     * 
     * Similar to try-finally. Cleanup executes whether operation succeeds or fails.
     * Original result/error is preserved and propagated.
     */
    template<typename F>
    AsyncOp<T> finally(F&& cleanup_fn) {
        spdlog::trace("AsyncOp[{}] adding finally", id());
        
        AsyncOp<T> result;
        auto result_state = result.m_promise;
        auto cleanup = std::make_shared<F>(std::forward<F>(cleanup_fn));
        auto cleanup_done = std::make_shared<bool>(false);

        if (!m_promise->canOverwriteSuccessCallback()) {
            spdlog::error("AsyncOp[{}] finally() called after terminal success handler", id());
            assert(false && "finally() cannot overwrite existing non-propagating success callback");
        } else {
            m_promise->status_flags.success_cb_is_propagating = 0;
            // Success path: cleanup then propagate value
            m_promise->success_cb = [cleanup, cleanup_done, result_state,
                                    op_id = m_promise->op_id](T val) mutable {
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

                result_state->resolveWith(std::move(val));
            };
        }

        if (!m_promise->canOverwriteErrorCallback()) {
            spdlog::error("AsyncOp[{}] finally() called after terminal error handler", id());
            assert(false && "finally() cannot overwrite existing non-propagating error callback");
        } else {
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
        }

        // Execute immediately if already settled
        if (isResolved() && m_promise->success_cb) {
            ao::add_idle([promise = m_promise]() {
                promise->success_cb(std::move(promise->result_value));
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
     * @param fallback_value Value to use if operation fails
     * @param log_message Optional warning message to log on error
     *
     * @deprecated Use `otherwise()` with explicit fallback logic instead:
     * @code
     * op.otherwise([](ErrorCode err) {
     *     spdlog::warn("Error: {}", err);
     *     return fallback_value;
     * });
     * @endcode
     */
    [[deprecated("Use otherwise() with explicit fallback logic instead")]]
    AsyncOp<T> orElse(T fallback_value, const std::string& log_message = "") {
        return this->otherwise([fallback_value = std::move(fallback_value), 
                            log_message, 
                            op_id = m_promise->op_id](ErrorCode err) mutable -> T {
            if (!log_message.empty()) {
                spdlog::warn("AsyncOp[{}] {}: error {}", op_id, log_message, err);
            }
            return std::move(fallback_value);
        });
    }

    /**
     * @brief Recover from specific error type only
     * @param error_to_handle Specific ErrorCode to handle
     * @param handler Recovery function for this error type
     * @note Re-throws other error types
     *
     * @deprecated Use `filterError()` for more flexible error handling:
     * @code
     * // Old: recoverFrom
     * op.recoverFrom(ErrorCode::Timeout, [](ErrorCode err) {
     *     return defaultValue;
     * });
     *
     * // New: filterError
     * op.filterError([](ErrorCode err) -> T {
     *     if (err == ErrorCode::Timeout) return defaultValue;
     *     throw err;  // propagate other errors
     * });
     * @endcode
     */
    template<typename F>
    [[deprecated("Use filterError() for more flexible handling")]]
    AsyncOp<T> recoverFrom(ErrorCode error_to_handle, F&& handler) {
        return this->recover([error_to_handle,
                            handler = std::forward<F>(handler)](ErrorCode err) mutable -> T {
            if (err == error_to_handle) {
                return handler(err);
            }
            throw err;  // Re-throw non-matching errors
        });
    }

    /**
     * @brief Cancel the operation with specified error code
     *
     * Rejects the operation with ErrorCode::Cancelled (or custom code) if pending.
     * Does nothing if already settled.
     *
     * @param code Error code to use (default: ErrorCode::Cancelled)
     * @return *this for chaining
     *
     * @note This only cancels the AsyncOp state, not any underlying operation.
     * Similar to timeout(), the caller is responsible for cleaning up any
     * underlying resources (timers, network requests, etc.).
     *
     * @code
     * // Cancel with default error code
     * asyncOp.cancel();
     *
     * // Cancel with custom error code
     * asyncOp.cancel(ErrorCode::NetworkError);
     *
     * // Typical usage: cancel on external event
     * auto op = fetchData();
     * onCancelButtonClicked.connect([op]() { op.cancel(); });
     * @endcode
     */
    AsyncOp<T>& cancel(ErrorCode code = ErrorCode::Cancelled) {
        if (m_promise->isPending()) {
            m_promise->rejectWith(code);
            spdlog::debug("AsyncOp[{}] cancelled with error {}", id(), code);
        } else {
            spdlog::trace("AsyncOp[{}] already settled, ignoring cancel()", id());
        }
        return *this;
    }

    /**
     * @brief Filter with dual-path handling for success and error
     *
     * Provides unified filtering for both success and error paths:
     * - Success filter: Return T to pass through, throw ErrorCode to reject
     * - Error filter: Return T to recover, throw ErrorCode to propagate
     *
     * @tparam SuccessF Success filter function type (T -> T)
     * @tparam ErrorF Error filter function type (ErrorCode -> T)
     * @param successFilter Filter for success path. Return value to pass, throw ErrorCode to reject.
     * @param errorFilter Filter for error path. Return value to recover, throw to propagate.
     * @return New AsyncOp<T> with filtering applied
     *
     * Move semantics: Value is moved into callback. User controls return copy/move:
     * @code
     * // Move on return (avoid copy)
     * op.filter([](User& u) -> User {
     *     if (!u.isValid()) throw ErrorCode::InvalidResponse;
     *     return std::move(u);
     * }, nullptr);
     *
     * // Copy on return (input was const ref)
     * op.filter([](const User& u) -> User {
     *     if (!u.isValid()) throw ErrorCode::InvalidResponse;
     *     return u;
     * }, nullptr);
     * @endcode
     *
     * @note For single-path filtering, use filterSuccess() or filterError() for clearer intent.
     *
     * @code
     * // Success filter only (errors propagate unchanged)
     * op.filterSuccess([](User& u) -> User {
     *     if (!u.isValid()) throw ErrorCode::InvalidResponse;
     *     return std::move(u);
     * });
     *
     * // Error filter only (success propagates unchanged)
     * op.filterError([](ErrorCode err) -> User {
     *     if (err == ErrorCode::Timeout) return defaultUser;
     *     throw err;  // propagate other errors
     * });
     * @endcode
     */
    template<typename SuccessF, typename ErrorF>
    auto filter(SuccessF&& successFilter, ErrorF&& errorFilter) -> AsyncOp<T> {
        spdlog::trace("AsyncOp[{}] adding filter with dual handlers", id());

        AsyncOp<T> result;
        auto result_state = result.m_promise;
        auto op_id = m_promise->op_id;

        // Set up success filter
        if constexpr (!std::is_null_pointer_v<SuccessF>) {
            if (!m_promise->canOverwriteSuccessCallback()) {
                spdlog::error("AsyncOp[{}] filter() called after terminal success handler", id());
                assert(false && "filter() cannot overwrite existing non-propagating success callback");
            } else {
                m_promise->status_flags.success_cb_is_propagating = 0;
                m_promise->success_cb = [f = std::decay_t<SuccessF>(successFilter), result_state, op_id](T val) mutable {
                    spdlog::debug("AsyncOp[{}] executing filter success handler", op_id);
                    try {
                        T filtered = f(std::move(val));
                        result_state->resolveWith(std::move(filtered));
                    } catch (ErrorCode err) {
                        spdlog::debug("AsyncOp[{}] filter success handler rejected with {}", op_id, err);
                        result_state->rejectWith(err);
                    } catch (const std::exception& e) {
                        spdlog::error("AsyncOp[{}] exception in filter success handler: {}", op_id, e.what());
                        result_state->rejectWith(ErrorCode::Exception);
                    } catch (...) {
                        spdlog::error("AsyncOp[{}] unknown exception in filter success handler", op_id);
                        result_state->rejectWith(ErrorCode::Exception);
                    }
                };
            }
        } else {
            // Success propagates unchanged
            if (m_promise->canOverwriteSuccessCallback()) {
                m_promise->status_flags.success_cb_is_propagating = 1;
                m_promise->success_cb = [result_state, op_id](T val) mutable {
                    spdlog::debug("AsyncOp[{}] filter propagating success", op_id);
                    result_state->resolveWith(std::move(val));
                };
            }
        }

        // Set up error filter
        if constexpr (!std::is_null_pointer_v<ErrorF>) {
            if (!m_promise->canOverwriteErrorCallback()) {
                spdlog::error("AsyncOp[{}] filter() called after terminal error handler", id());
                assert(false && "filter() cannot overwrite existing non-propagating error callback");
            } else {
                m_promise->status_flags.error_cb_is_propagating = 0;
                m_promise->error_cb = [f = std::decay_t<ErrorF>(errorFilter), result_state, op_id](ErrorCode err) mutable {
                    spdlog::debug("AsyncOp[{}] executing filter error handler", op_id);
                    try {
                        T recovered = f(err);
                        result_state->resolveWith(std::move(recovered));
                    } catch (ErrorCode thrown) {
                        spdlog::debug("AsyncOp[{}] filter error handler propagating {}", op_id, thrown);
                        result_state->rejectWith(thrown);
                    } catch (const std::exception& e) {
                        spdlog::error("AsyncOp[{}] exception in filter error handler: {}", op_id, e.what());
                        result_state->rejectWith(ErrorCode::Exception);
                    } catch (...) {
                        spdlog::error("AsyncOp[{}] unknown exception in filter error handler", op_id);
                        result_state->rejectWith(ErrorCode::Exception);
                    }
                };
            }
        } else {
            // Error propagates unchanged
            if (m_promise->canOverwriteErrorCallback()) {
                m_promise->status_flags.error_cb_is_propagating = 1;
                m_promise->error_cb = [result_state, op_id](ErrorCode err) mutable {
                    spdlog::debug("AsyncOp[{}] filter propagating error {}", op_id, err);
                    result_state->rejectWith(err);
                };
            }
        }

        // If already settled, execute immediately
        if (!isPending()) {
            add_idle([state = m_promise]() {
                if (state->isResolved() && state->success_cb) {
                    state->success_cb(std::move(state->result_value));
                } else if (state->isRejected() && state->error_cb) {
                    state->error_cb(state->getErrorCode());
                }
                return false;
            });
        }

        return result;
    }

    /**
     * @brief Filter success path only, errors propagate unchanged
     *
     * Convenience wrapper for filter() when you only need to validate/transform
     * the success value. Errors pass through without modification.
     *
     * @tparam SuccessF Filter function type (T -> T)
     * @param successFilter Filter for success path. Return value to pass, throw ErrorCode to reject.
     * @return New AsyncOp<T> with success filtering applied
     *
     * @code
     * // Validate response before passing through
     * op.filterSuccess([](Response& r) -> Response {
     *     if (!r.isValid()) throw ErrorCode::InvalidResponse;
     *     return std::move(r);
     * });
     *
     * // Transform value
     * op.filterSuccess([](int value) -> int {
     *     if (value < 0) throw ErrorCode::InvalidResponse;
     *     return value * 2;
     * });
     * @endcode
     */
    template<typename SuccessF>
    auto filterSuccess(SuccessF&& successFilter) -> AsyncOp<T> {
        return filter(std::forward<SuccessF>(successFilter), nullptr);
    }

    /**
     * @brief Filter error path only, success propagates unchanged
     *
     * Convenience wrapper for filter() when you only need to handle errors.
     * Success values pass through without modification.
     *
     * @tparam ErrorF Filter function type (ErrorCode -> T)
     * @param errorFilter Filter for error path. Return value to recover, throw ErrorCode to propagate.
     * @return New AsyncOp<T> with error filtering applied
     *
     * @code
     * // Recover from specific error
     * op.filterError([](ErrorCode err) -> Data {
     *     if (err == ErrorCode::Timeout) return cachedData;
     *     throw err;  // propagate other errors
     * });
     *
     * // Log all errors but propagate
     * op.filterError([](ErrorCode err) -> Data {
     *     spdlog::warn("Operation failed: {}", err);
     *     throw err;  // re-throw after logging
     * });
     * @endcode
     */
    template<typename ErrorF>
    auto filterError(ErrorF&& errorFilter) -> AsyncOp<T> {
        return filter(nullptr, std::forward<ErrorF>(errorFilter));
    }

    // Idempotent resolve/reject - these check isPending() internally
    void resolve(T value) {
        if (!isPending()) {
            spdlog::warn("AsyncOp[{}] already completed, ignoring resolve", m_promise->op_id);
            return;
        }
        
        spdlog::debug("AsyncOp[{}] resolved", m_promise->op_id);
        m_promise->result_value = std::move(value);
        m_promise->setStatus(State::Resolved);
        
        if (m_promise->success_cb) {
            m_promise->success_cb(std::move(m_promise->result_value));
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
    
    using value_type = T;
};

} // namespace ao

// Include AsyncOp<void> specialization
#include "async_op_void.hpp"

// Workflow helper functions
namespace ao {

/**
 * @brief Make a promise
 * 
 * Promise is a shared state object that can be resolved or rejected.
 * Shared state is a shared_ptr of State
 * 
 * @return Promise shared state object
 *
 * @tparam T 
 * @return std::shared_ptr<typename AsyncOp<T>::State> 
 */
template<typename T>
Promise<T> makePromise() {
    return std::make_shared<typename AsyncOp<T>::State>();
}

/**
 * @brief Make a future from promise
 * 
 * future can be chained.
 * you can get the promise later by future.promise().
 * 
 */
template<typename T>
AsyncOp<T> makeFuture(Promise<T> a_promise) {
    return AsyncOp<T>(a_promise);
}

/**
 * @brief Retry operation with exponential backoff
 * 
 * Timeline with initial_delay=1s, max_attempts=3:
 * - Attempt 1: immediate
 * - Attempt 2: after 1s
 * - Attempt 3: after 2s more (total 3s)
 * - If all fail: rejects with MaxRetriesExceeded
 * 
 * @param operation Function returning AsyncOp<T>
 * @param max_attempts Max attempts including initial
 * @param initial_delay Delay before first retry (not before first attempt)
 */
template<typename T, typename F>
AsyncOp<T> retryWithBackoff(F&& operation, int max_attempts, 
                            std::chrono::milliseconds initial_delay) {
    spdlog::debug("Retry setup: max_attempts={}, initial_delay={}ms", 
                 max_attempts, initial_delay.count());
    
    AsyncOp<T> result;
    auto result_state = result.m_promise;
    auto attempt = std::make_shared<int>(0);
    auto delay = std::make_shared<std::chrono::milliseconds>(initial_delay);
    
    auto tryOnce = std::make_shared<std::function<void()>>();
    *tryOnce = [=, operation = std::forward<F>(operation)]() mutable {
        (*attempt)++;
        spdlog::debug("Retry attempt {}/{}", *attempt, max_attempts);
        
        operation()
            .then([=](T value) {
                spdlog::info("Retry succeeded on attempt {}/{}", *attempt, max_attempts);
                result_state->resolveWith(std::move(value));
            })
            .onError([=](ErrorCode err) {
                if (*attempt < max_attempts) {
                    spdlog::warn("Attempt {}/{} failed, retrying in {}ms", 
                                *attempt, max_attempts, delay->count());
                    
                    add_timeout(*delay, [tryOnce]() {
                        (*tryOnce)();
                        return false;
                    });
                    
                    *delay *= 2;  // Exponential backoff
                } else {
                    spdlog::error("Retry failed after {} attempts", max_attempts);
                    result_state->rejectWith(ErrorCode::MaxRetriesExceeded);
                }
            });
    };
    
    (*tryOnce)();
    return result;
}

/**
 * @brief Execute async operation for each item SEQUENTIALLY
 * 
 * Processes one item at a time. Fails immediately on first error (remaining items NOT processed).
 * For parallel processing, use all() or mapParallel().
 */
template<typename T, typename F>
AsyncOp<void> forEach(const std::vector<T>& items, F&& process) {
    spdlog::debug("forEach starting with {} items", items.size());
    
    auto index = std::make_shared<size_t>(0);
    AsyncOp<void> result;
    auto result_state = result.m_promise;
    
    auto processNext = std::make_shared<std::function<void()>>();
    *processNext = [=, &items, process = std::forward<F>(process)]() mutable {
        if (*index >= items.size()) {
            spdlog::debug("forEach completed all {} items", items.size());
            result_state->resolveWith();
            return;
        }
        
        spdlog::trace("forEach processing item {}/{}", *index + 1, items.size());
        
        process(items[*index])
            .then([=](auto) {
                (*index)++;
                (*processNext)();
            })
            .onError([=](ErrorCode err) {
                spdlog::error("forEach failed at item {}/{} with error {}", 
                             *index + 1, items.size(), err);
                result_state->rejectWith(err);
            });
    };
    
    (*processNext)();
    return result;
}

/**
 * @brief Execute async operation for each item SEQUENTIALLY, collecting failed items
 * 
 * Unlike forEach() which fails fast on first error, forEachSettled() processes ALL items
 * and returns a list of items that failed. Useful for batch operations where you want to
 * retry failed items later.
 * 
 * @return AsyncOp<std::vector<T>> containing only the items that failed processing
 */
template<typename Item, typename F>
AsyncOp<std::vector<Item>> forEachSettled(const std::vector<Item>& items, F&& process) {
    spdlog::debug("forEachSettled() starting with {} items", items.size());
    
    auto index = std::make_shared<size_t>(0);
    auto failed_items = std::make_shared<std::vector<Item>>();
    AsyncOp<std::vector<Item>> result;
    auto result_state = result.m_promise;
    
    if (items.empty()) {
        spdlog::debug("forEachSettled() called with 0 items");
        return AsyncOp<std::vector<Item>>::resolved(std::vector<Item>{});
    }
    
    auto processNext = std::make_shared<std::function<void()>>();
    *processNext = [=, &items, process = std::forward<F>(process)]() mutable {
        if (*index >= items.size()) {
            spdlog::debug("forEachSettled() completed all {} items, {} failed", 
                         items.size(), failed_items->size());
            result_state->resolveWith(std::move(*failed_items));
            return;
        }
        
        size_t current = *index;
        spdlog::trace("forEachSettled() processing item {}/{}", current + 1, items.size());
        
        process(items[current])
            .then([=](auto) {
                (*index)++;
                (*processNext)();
            })
            .onError([=, &items](ErrorCode err) {
                spdlog::debug("forEachSettled() item {}/{} failed with error {}", 
                             current + 1, items.size(), err);
                failed_items->push_back(items[current]);
                (*index)++;
                (*processNext)();
            });
    };
    
    (*processNext)();
    return result;
}

/**
 * @brief Result container for allSettled() and mapSettled()
 */
template<typename T>
struct SettledResult {
    enum Status { Fulfilled, Rejected };

    T value;              // Valid only if Fulfilled
    ErrorCode error;      // Valid only if Rejected
    Status status;

    bool isFulfilled() const { return status == Fulfilled; }
    bool isRejected() const { return status == Rejected; }
};


/**
 * @brief Transform each item SEQUENTIALLY, collecting all settled results
 * 
 * Unlike map() which fails fast on first error, mapSettled() processes ALL items
 * and returns results for both successes and failures. Useful for batch transformations
 * where you want full visibility into what succeeded and what failed.
 * 
 * @return AsyncOp<std::vector<SettledResult<T>>> where T is the unwrapped result type of transform()
 */
template<typename Item, typename F>
auto mapSettled(const std::vector<Item>& items, F&& transform)
    -> AsyncOp<std::vector<SettledResult<unwrap_async_op_t<typename std::invoke_result<F, Item>::type>>>> {
    
    using InvokeResult = typename std::invoke_result<F, Item>::type;
    using RetType = unwrap_async_op_t<InvokeResult>;
    
    spdlog::debug("mapSettled() starting with {} items", items.size());
    
    auto index = std::make_shared<size_t>(0);
    auto results = std::make_shared<std::vector<SettledResult<RetType>>>();
    AsyncOp<std::vector<SettledResult<RetType>>> result;
    auto result_state = result.m_promise;
    
    if (items.empty()) {
        spdlog::debug("mapSettled() called with 0 items");
        return AsyncOp<std::vector<SettledResult<RetType>>>::resolved(std::vector<SettledResult<RetType>>{});
    }
    
    results->resize(items.size());
    
    auto processNext = std::make_shared<std::function<void()>>();
    *processNext = [=, &items, transform = std::forward<F>(transform)]() mutable {
        if (*index >= items.size()) {
            spdlog::debug("mapSettled() completed all {} items", items.size());
            result_state->resolveWith(std::move(*results));
            return;
        }
        
        size_t current = *index;
        spdlog::trace("mapSettled() processing item {}/{}", current + 1, items.size());
        
        transform(items[current])
            .then([=](RetType value) {
                (*results)[current].status = SettledResult<RetType>::Fulfilled;
                (*results)[current].value = std::move(value);
                (*index)++;
                (*processNext)();
            })
            .onError([=](ErrorCode err) {
                spdlog::debug("mapSettled() item {}/{} failed with error {}", 
                             current + 1, items.size(), err);
                (*results)[current].status = SettledResult<RetType>::Rejected;
                (*results)[current].error = err;
                (*index)++;
                (*processNext)();
            });
    };
    
    (*processNext)();
    return result;
}

/**
 * @brief Poll operation until condition met or max attempts reached
 * 
 * Executes operation repeatedly at intervals until condition(result) returns true.
 * Rejects with MaxRetriesExceeded if max_attempts reached.
 */
template<typename T, typename F, typename Pred>
AsyncOp<T> pollUntil(F&& operation, Pred&& condition, int max_attempts,
                     std::chrono::milliseconds interval) {
    spdlog::debug("pollUntil: max_attempts={}, interval={}ms", 
                 max_attempts, interval.count());
    
    AsyncOp<T> result;
    auto result_state = result.m_promise;
    auto attempt = std::make_shared<int>(0);
    
    auto poll = std::make_shared<std::function<void()>>();
    *poll = [=, operation = std::forward<F>(operation), condition = std::forward<Pred>(condition)]() mutable {
        (*attempt)++;
        spdlog::trace("pollUntil attempt {}/{}", *attempt, max_attempts);
        
        operation()
            .then([=](T value) {
                if (condition(value)) {
                    spdlog::info("pollUntil condition met on attempt {}/{}", 
                                *attempt, max_attempts);
                    result_state->resolveWith(std::move(value));
                } else if (*attempt < max_attempts) {
                    spdlog::debug("pollUntil condition not met, next poll in {}ms", 
                                 interval.count());
                    
                    add_timeout(interval, [poll]() {
                        (*poll)();
                        return false;
                    });
                } else {
                    spdlog::error("pollUntil max attempts {} reached", max_attempts);
                    result_state->rejectWith(ErrorCode::MaxRetriesExceeded);
                }
            })
            .onError([=](ErrorCode err) {
                spdlog::error("pollUntil failed on attempt {}/{} with error {}", 
                             *attempt, max_attempts, err);
                result_state->rejectWith(err);
            });
    };
    
    (*poll)();
    return result;
}

/**
 * @brief Execute operations in parallel, wait for all to complete
 * 
 * All operations start immediately. Resolves when ALL succeed (results in input order).
 * Rejects immediately on FIRST failure.
 */
template<typename T>
AsyncOp<std::vector<T>> all(std::vector<AsyncOp<T>> operations) {
    spdlog::debug("all() waiting for {} operations", operations.size());

    AsyncOp<std::vector<T>> result;
    auto result_state = result.m_promise;
    auto results = std::make_shared<std::vector<T>>();
    auto completed = std::make_shared<std::atomic<size_t>>(0);
    auto failed = std::make_shared<std::atomic<bool>>(false);
    auto total = operations.size();

    if (total == 0) {
        spdlog::debug("all() called with 0 operations");
        return AsyncOp<std::vector<T>>::resolved(std::vector<T>{});
    }

    results->resize(total);

    for (size_t i = 0; i < total; ++i) {
        operations[i].then([=, index = i](T value) {
            if (failed->load(std::memory_order_relaxed)) return;

            (*results)[index] = std::move(value);
            size_t count = completed->fetch_add(1, std::memory_order_acq_rel) + 1;

            spdlog::trace("all() operation {}/{} completed", count, total);

            if (count == total) {
                spdlog::info("all() completed successfully", total);
                result_state->resolveWith(std::move(*results));
            }
        }).onError([=](ErrorCode err) {
            bool expected = false;
            if (failed->compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                spdlog::error("all() operation failed with err {}, failing batch", err);
                result_state->rejectWith(err);
            }
        });
    }

    return result;
}

/**
 * @brief Race operations, resolve with first to complete (success or failure)
 * 
 * All operations start immediately. Resolves/rejects as soon as ANY completes,
 * regardless of whether it succeeds or fails. This is true "race" semantics.
 * 
 * For "succeed on first success, fail only if all fail" behavior, use any().
 */
template<typename T>
AsyncOp<T> race(std::vector<AsyncOp<T>> operations) {
    spdlog::debug("race() racing {} operations (first to settle wins)", operations.size());

    AsyncOp<T> result;
    auto result_state = result.m_promise;
    auto completed = std::make_shared<std::atomic<bool>>(false);
    auto total = operations.size();

    if (total == 0) {
        spdlog::warn("race() called with 0 operations");
        return AsyncOp<T>::rejected(ErrorCode::InvalidResponse);
    }

    for (size_t i = 0; i < total; ++i) {
        operations[i].then([=, index = i](T value) {
            bool expected = false;
            if (completed->compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                spdlog::info("race() operation {} won (success)", index);
                result_state->resolveWith(std::move(value));
            }
        }).onError([=, index = i](ErrorCode err) {
            bool expected = false;
            if (completed->compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                spdlog::info("race() operation {} won (failure)", index);
                result_state->rejectWith(err);
            }
        });
    }

    return result;
}

/**
 * @brief Resolve with first success, reject only if all fail
 * 
 * All operations start immediately. Resolves as soon as ANY succeeds.
 * Rejects only if ALL fail (with last error).
 * Useful for fallback scenarios or timeout alternatives.
 * 
 * This is equivalent to JavaScript's Promise.any().
 */
template<typename T>
AsyncOp<T> any(std::vector<AsyncOp<T>> operations) {
    spdlog::debug("any() racing {} operations (first success wins)", operations.size());

    AsyncOp<T> result;
    auto result_state = result.m_promise;
    auto completed = std::make_shared<std::atomic<bool>>(false);
    auto failed_count = std::make_shared<std::atomic<size_t>>(0);
    auto total = operations.size();

    if (total == 0) {
        spdlog::warn("any() called with 0 operations");
        return AsyncOp<T>::rejected(ErrorCode::InvalidResponse);
    }

    for (size_t i = 0; i < total; ++i) {
        operations[i].then([=, index = i](T value) {
            bool expected = false;
            if (completed->compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                spdlog::info("any() operation {} won", index);
                result_state->resolveWith(std::move(value));
            }
        }).onError([=, index = i](ErrorCode err) {
            size_t count = failed_count->fetch_add(1, std::memory_order_acq_rel) + 1;
            spdlog::debug("any() operation {} failed, {}/{} failed",
                         index, count, total);

            if (count == total) {
                bool already_completed = completed->load(std::memory_order_acquire);
                if (!already_completed) {
                    spdlog::error("any() all operations failed");
                    result_state->rejectWith(err);
                }
            }
        });
    }

    return result;
}

/**
 * @brief Delay execution for specified duration
 */
inline AsyncOp<void> delay(std::chrono::milliseconds duration) {
    spdlog::debug("delay() scheduling for {}ms", duration.count());
    
    AsyncOp<void> result;
    auto result_state = result.m_promise;
    
    add_timeout(duration, [result_state]() {
        spdlog::debug("delay() timer fired");
        result_state->resolveWith();
        return false;
    });
    
    return result;
}

/**
 * @brief Wrap synchronous function to execute asynchronously on main loop
 * 
 * Schedules function to run on next main loop iteration (via add_idle).
 * Useful for deferring execution or converting sync code to async pattern.
 * Rejects with ErrorCode::Exception if function throws.
 */
template<typename F>
auto defer(F&& f) -> AsyncOp<typename std::invoke_result<F>::type> {
    using RetType = typename std::invoke_result<F>::type;
    
    spdlog::trace("defer() scheduling function");
    
    AsyncOp<RetType> result;
    auto result_state = result.m_promise;
    auto func = std::function<RetType()>(std::forward<F>(f));
    
    add_idle([result_state, func = std::move(func)]() {
        try {
            if constexpr (std::is_void_v<RetType>) {
                func();
                result_state->resolveWith();
            } else {
                auto value = func();
                result_state->resolveWith(std::move(value));
            }
        } catch (const std::exception& e) {
            spdlog::error("defer() function threw: {}", e.what());
            result_state->rejectWith(ErrorCode::Exception);
        } catch (...) {
            spdlog::error("defer() function threw unknown exception");
            result_state->rejectWith(ErrorCode::Exception);
        }
        
        return false;
    });
    
    return result;
}

/**
 * @brief Wait for all operations to complete (both successes and failures)
 * 
 * Unlike all() which fails on first error, allSettled() waits for ALL operations
 * and returns results for both successes and failures.
 * Useful for "best effort" batch operations.
 */
template<typename T>
AsyncOp<std::vector<SettledResult<T>>> allSettled(std::vector<AsyncOp<T>> operations) {
    spdlog::debug("allSettled() waiting for {} operations", operations.size());

    AsyncOp<std::vector<SettledResult<T>>> result;
    auto result_state = result.m_promise;
    auto results = std::make_shared<std::vector<SettledResult<T>>>();
    auto completed = std::make_shared<std::atomic<size_t>>(0);
    auto total = operations.size();

    if (total == 0) {
        spdlog::debug("allSettled() called with 0 operations");
        return AsyncOp<std::vector<SettledResult<T>>>::resolved(std::vector<SettledResult<T>>{});
    }

    results->resize(total);

    for (size_t i = 0; i < total; ++i) {
        operations[i].then([=, index = i](T value) {
            (*results)[index].status = SettledResult<T>::Fulfilled;
            (*results)[index].value = std::move(value);
            size_t count = completed->fetch_add(1, std::memory_order_acq_rel) + 1;

            spdlog::trace("allSettled() operation {}/{} fulfilled", count, total);

            if (count == total) {
                spdlog::info("allSettled() all {} operations settled", total);
                result_state->resolveWith(std::move(*results));
            }
        }).onError([=, index = i](ErrorCode err) {
            (*results)[index].status = SettledResult<T>::Rejected;
            (*results)[index].error = err;
            size_t count = completed->fetch_add(1, std::memory_order_acq_rel) + 1;

            spdlog::trace("allSettled() operation {}/{} rejected", count, total);

            if (count == total) {
                spdlog::info("allSettled() all {} operations settled", total);
                result_state->resolveWith(std::move(*results));
            }
        });
    }

    return result;
}

/**
 * @brief Transform each item sequentially, collecting results
 * 
 * Processes items one at a time. Fails on first error (remaining items NOT processed).
 * For parallel processing, use mapParallel().
 */
template<typename T, typename F>
auto map(const std::vector<T>& items, F&& transform) 
    -> AsyncOp<std::vector<unwrap_async_op_t<typename std::invoke_result<F, T>::type>>> {
    
    using InvokeResult = typename std::invoke_result<F, T>::type;
    using RetType = unwrap_async_op_t<InvokeResult>;
    
    spdlog::debug("map() starting with {} items", items.size());
    
    auto index = std::make_shared<size_t>(0);
    auto results = std::make_shared<std::vector<RetType>>();
    AsyncOp<std::vector<RetType>> result;
    auto result_state = result.m_promise;
    
    auto processNext = std::make_shared<std::function<void()>>();
    *processNext = [=, &items, transform = std::forward<F>(transform)]() mutable {
        if (*index >= items.size()) {
            spdlog::debug("map() completed all {} items", items.size());
            result_state->resolveWith(std::move(*results));
            return;
        }
        
        spdlog::trace("map() processing item {}/{}", *index + 1, items.size());
        
        transform(items[*index])
            .then([=](RetType value) {
                results->push_back(std::move(value));
                (*index)++;
                (*processNext)();
            })
            .onError([=](ErrorCode err) {
                spdlog::error("map() failed at item {}/{}", *index + 1, items.size());
                result_state->rejectWith(err);
            });
    };
    
    (*processNext)();
    return result;
}

/**
 * @brief Transform each item in parallel, collecting results
 * 
 * Processes all items simultaneously. Fails on first error.
 * For sequential processing, use map().
 */
template<typename T, typename F>
auto mapParallel(const std::vector<T>& items, F&& transform) 
    -> AsyncOp<std::vector<unwrap_async_op_t<typename std::invoke_result<F, T>::type>>> {
    
    using InvokeResult = typename std::invoke_result<F, T>::type;
    using RetType = unwrap_async_op_t<InvokeResult>;
    
    spdlog::debug("mapParallel() starting with {} items", items.size());
    
    std::vector<AsyncOp<RetType>> operations;
    operations.reserve(items.size());
    
    for (const auto& item : items) {
        operations.push_back(transform(item));
    }
    
    return all(std::move(operations));
}

} // namespace ao

#endif // ASYNC_OP_HPP
