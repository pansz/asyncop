/**
 * @file ao_event_loop.hpp
 * @brief Event loop abstraction layer - Lightweight Asynchronous Operation Chaining for C++17
 * @author pansz
 * @date 2026.2.8
 * 
 * AsyncOp - Elegant Promise/Future pattern bringing modern async programming to C++17 with minimal overhead
 * 
 * Provides unified interface for different event loop backends (GLib, Qt, etc.)
 * Allows AsyncOp to be backend-agnostic and easily extensible to new event loops.
 * 
 * Currently supported backends:
 * - GLib 2.0+ (default)
 * - Qt 5.12+ (define ASYNC_USE_QT)
 * 
 * To add a new backend:
 * 1. Define backend detection macro (e.g., ASYNC_USE_LIBUV)
 * 2. Implement timer_type, add_timeout, add_idle, remove_timeout
 * 3. Implement is_main_thread and invoke_main
 * 4. Update FORMAT_TIMER for logging
 * 
 * @note Requires spdlog for logging
 * @note C++17 or later
 */

#ifndef AO_EVENT_LOOP_HPP
#define AO_EVENT_LOOP_HPP

#include <functional>
#include <chrono>
#include <atomic>

// Backend selection
#ifdef ASYNC_USE_QT
# include <QTimer>
# include <QApplication>
# include <QThread>
# include <QMetaObject>
#else
# include <glib.h>
#endif

#include <spdlog/spdlog.h>

namespace ao {

// ============================================================================
// Backend-specific type definitions
// ============================================================================

#ifdef ASYNC_USE_QT
using id_type = quint32;
using timer_type = QTimer*;
#define FORMAT_TIMER(timer) fmt::ptr(timer)
#else
using id_type = guint;
using timer_type = guint;
#define FORMAT_TIMER(timer) (timer)
#endif

// ============================================================================
// Internal implementation details
// ============================================================================

namespace detail {

#ifdef ASYNC_USE_QT
    // Qt uses QTimer directly, no trampoline needed
#else
    /**
     * @brief Holder for GLib callbacks with exception safety
     */
    struct CallbackHolder {
        std::function<bool()> cb;
    };

    /**
     * @brief Trampoline function for GLib timeout/idle callbacks
     * 
     * Wraps C++ lambda in C-compatible function pointer.
     * Catches and logs exceptions to prevent crashes.
     */
    inline gboolean trampoline(gpointer user_data) {
        auto* holder = static_cast<CallbackHolder*>(user_data);
        try {
            return holder->cb() ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
        } catch (const std::exception& e) {
            spdlog::error("Exception in GLib callback: {}", e.what());
            return G_SOURCE_REMOVE;
        } catch (...) {
            spdlog::error("Unknown exception in GLib callback");
            return G_SOURCE_REMOVE;
        }
    }

    /**
     * @brief Cleanup function for GLib callbacks
     * 
     * Called by GLib when timer/idle source is removed.
     */
    inline void destroy_notify(gpointer user_data) {
        delete static_cast<CallbackHolder*>(user_data);
    }
#endif

    /**
     * @brief Generate unique AsyncOp IDs
     * 
     * Thread-safe counter for AsyncOp instance tracking.
     * Used for debugging and logging.
     */
    inline id_type get_next_async_op_id() {
        static std::atomic<id_type> next_id{1};
        return next_id.fetch_add(1, std::memory_order_relaxed);
    }

} // namespace detail

// ============================================================================
// Unified event loop interface
// ============================================================================

/**
 * @brief Schedule callback to run after timeout
 * 
 * @param timeout Delay before executing callback
 * @param cb Callback function returning bool:
 *           - true: reschedule (G_SOURCE_CONTINUE)
 *           - false: remove timer (G_SOURCE_REMOVE)
 * @return Timer handle (for cancellation via remove_timeout)
 * 
 * @note Thread-safe (can be called from any thread)
 * @note For single-shot timers, return false from callback
 * @note For repeating timers, return true from callback
 */
inline timer_type add_timeout(std::chrono::milliseconds timeout, std::function<bool()> cb)
{
#ifdef ASYNC_USE_QT
    auto* timer = new QTimer(qApp);
    timer->setSingleShot(false);  // Allow repeating
    auto interval = static_cast<int>(timeout.count());
    QObject::connect(timer, &QTimer::timeout, qApp, [cb = std::move(cb), timer]() mutable {
        bool should_continue = cb();
        if (!should_continue) {
            timer->stop();
            timer->deleteLater();
        }
    });
    timer->start(interval);
    return timer;
#else
    auto* holder = new detail::CallbackHolder{std::move(cb)};
    return g_timeout_add_full(
        G_PRIORITY_DEFAULT,
        static_cast<guint>(timeout.count()),
        &detail::trampoline,
        holder,
        &detail::destroy_notify
    );
#endif
}

/**
 * @brief Schedule callback to run on next event loop iteration
 * 
 * @param cb Callback function returning bool:
 *           - true: reschedule (G_SOURCE_CONTINUE)
 *           - false: remove (G_SOURCE_REMOVE)
 * @return Timer handle (for cancellation via remove_timeout)
 * 
 * @note Thread-safe (can be called from any thread)
 * @note Callback runs with idle priority (after I/O events)
 * @note For single-shot, return false from callback
 */
inline timer_type add_idle(std::function<bool()> cb)
{
#ifdef ASYNC_USE_QT
    auto* timer = new QTimer(qApp);
    timer->setSingleShot(false);  // Allow repeating
    QObject::connect(timer, &QTimer::timeout, qApp, [cb = std::move(cb), timer]() mutable {
        bool should_continue = cb();
        if (!should_continue) {
            timer->stop();
            timer->deleteLater();
        }
    });
    timer->start(0);  // 0ms = next event loop iteration
    return timer;
#else
    auto* holder = new detail::CallbackHolder{std::move(cb)};
    return g_idle_add_full(
        G_PRIORITY_DEFAULT_IDLE,
        &detail::trampoline,
        holder,
        &detail::destroy_notify
    );
#endif
}

/**
 * @brief Cancel a scheduled timeout or idle callback
 * 
 * @param id Timer handle returned from add_timeout or add_idle
 * 
 * @note Thread-safe (can be called from any thread)
 * @note Safe to call with already-removed timer (no-op)
 * @note For Qt: calls deleteLater() (safe in event loop)
 * @note For GLib: calls g_source_remove()
 */
inline void remove_timeout(timer_type id)
{
#ifdef ASYNC_USE_QT
    if (id) {
        id->stop();
        id->deleteLater();
    }
#else
    if (id != 0) {
        g_source_remove(id);
    }
#endif
}

/**
 * @brief Check if current thread is the main event loop thread
 * 
 * @return true if caller is on main thread, false otherwise
 * 
 * @note Thread-safe
 * @note For Qt: compares with QApplication::instance()->thread()
 * @note For GLib: checks if owns default main context
 */
inline bool is_main_thread()
{
#ifdef ASYNC_USE_QT
    return QThread::currentThread() == QCoreApplication::instance()->thread();
#else
    return g_main_context_is_owner(g_main_context_default());
#endif
}

/**
 * @brief Execute callback on main event loop thread
 * 
 * ⚠️ IMPORTANT: Behavior differs based on calling thread:
 * - Main thread: Executes synchronously (immediately, before return)
 * - Worker thread: Executes asynchronously (via event loop)
 * 
 * For consistent async behavior, use add_idle() directly.
 * 
 * @param cb Callback to execute (void function)
 * 
 * @note Thread-safe
 * @note Exceptions are caught and logged
 * @note For Qt: uses QMetaObject::invokeMethod
 * @note For GLib: checks thread and dispatches accordingly
 * 
 * @example
 * // From worker thread - marshals to main thread
 * std::thread worker([state, result]() {
 *     auto data = compute();
 *     invoke_main([state, data]() {
 *         state->resolveWith(data);
 *     });
 * });
 */
inline void invoke_main(std::function<void()> cb)
{
#ifdef ASYNC_USE_QT
    QMetaObject::invokeMethod(qApp, [cb = std::move(cb)]() {
        try {
            cb();
        } catch (const std::exception& e) {
            spdlog::error("Exception in invoke_main (Qt): {}", e.what());
        } catch (...) {
            spdlog::error("Unknown exception in invoke_main (Qt)");
        }
    });
#else
    if (is_main_thread()) {
        // Already on main thread - execute immediately
        try {
            cb();
        } catch (const std::exception& e) {
            spdlog::error("Exception in invoke_main (sync path): {}", e.what());
        } catch (...) {
            spdlog::error("Unknown exception in invoke_main (sync path)");
        }
    } else {
        // Worker thread - marshal to main thread
        add_idle([cb = std::move(cb)]() {
            try {
                cb();
            } catch (const std::exception& e) {
                spdlog::error("Exception in invoke_main (async path): {}", e.what());
            } catch (...) {
                spdlog::error("Unknown exception in invoke_main (async path)");
            }
            return false;  // G_SOURCE_REMOVE
        });
    }
#endif
}

// ============================================================================
// Backend information (for debugging/logging)
// ============================================================================

/**
 * @brief Get name of current event loop backend
 * 
 * @return Backend name string ("GLib", "Qt", etc.)
 */
inline const char* get_backend_name()
{
#ifdef ASYNC_USE_QT
    return "Qt";
#else
    return "GLib";
#endif
}

/**
 * @brief Get version info of event loop backend
 * 
 * @return Version string (e.g., "GLib 2.76.0" or "Qt 5.15.2")
 */
inline std::string get_backend_version()
{
#ifdef ASYNC_USE_QT
    return std::string("Qt ") + qVersion();
#else
    return std::string("GLib ") + 
           std::to_string(glib_major_version) + "." +
           std::to_string(glib_minor_version) + "." +
           std::to_string(glib_micro_version);
#endif
}

} // namespace ao

#endif // AO_EVENT_LOOP_HPP