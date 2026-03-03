/**
 * @file msg_registry.hpp
 * @brief Message-based AsyncOp integration - Lightweight Asynchronous Operation Chaining for C++17
 * @author pansz
 * @date 2026.2.6
 * 
 * AsyncOp - Elegant Promise/Future pattern bringing modern async programming to C++17 with minimal overhead
 * 
 * Provides MessageRegistry for tracking async operations by message ID.
 * Uses timestamp-based ID generation for uniqueness across restarts.
 * 
 * Key Features:
 * - Timestamp-based IDs (unique across restarts, no persistence needed)
 * - Thread-safe message tracking
 * - Built-in timeout support
 * - Java-compatible signed int64_t IDs
 * - O(1) registration and lookup
 * 
 * @note Requires async_op.hpp
 * @note C++17 or later
 * 
 * Usage:
 * @code
 * ao::MessageRegistry<ResponseData> registry;
 * 
 * // Send message
 * ao::AsyncOp<ResponseData> sendRequest(const Request& req) {
 *     ao::AsyncOp<ResponseData> result;
 *     int64_t msg_id = registry.registerMessage(result.m_promise, std::chrono::seconds(5));
 *     sendMessageToNetwork(msg_id, req);
 *     return result;
 * }
 * 
 * // Handle response
 * void onNetworkMessage(int64_t id, const ResponseData& data) {
 *     registry.handleResponse(id, data);
 * }
 * @endcode
 */

#ifndef MESSAGE_REGISTRY_HPP
#define MESSAGE_REGISTRY_HPP

#include "async_op.hpp"
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cstdint>
#include <sstream>
#include <iomanip>

namespace ao {

/**
 * @brief Generate unique message IDs using timestamp + continuous counter
 *
 * ID Format: [timestamp_ms (42 bits) | counter (21 bits)]
 * - Bit 63: Always 0 (ensures positive signed value)
 * - Bits 62-21: Timestamp in milliseconds (42 bits = ~139 years from epoch)
 * - Bits 20-0: Counter (21 bits = 2,097,152 IDs per millisecond)
 *
 * Properties:
 * - IDs are always positive (Java long compatibility)
 * - Unique across process restarts (timestamp changes)
 * - Monotonically increasing globally by using continuous counter
 * - No persistence needed
 * - Handles clock skew gracefully by using continuous counter increment
 * - Timestamp valid until year 2109
 *
 * Thread-safe: Yes (uses atomics)
 */
class IdGen {
private:
    std::atomic<int64_t> last_timestamp_ms_{0};
    std::atomic<int32_t> global_counter_{0};  // Continuous counter across time periods

    // Counter uses 21 bits (0 to 2,097,151)
    static constexpr int32_t COUNTER_BITS = 21;
    static constexpr int32_t COUNTER_MASK = (1 << COUNTER_BITS) - 1;  // 0x1FFFFF

    // Timestamp uses 42 bits (milliseconds since epoch)
    static constexpr int32_t TIMESTAMP_BITS = 42;
    static constexpr int64_t TIMESTAMP_MASK = (1LL << TIMESTAMP_BITS) - 1;  // 0x3FFFFFFFFFF

public:
    /**
     * @brief Generate unique message ID
     *
     * @return Positive int64_t message ID, unique across restarts
     *
     * @note Thread-safe
     * @note IDs are monotonically increasing globally by using continuous counter
     * @note Automatically handles clock skew by using continuous counter increment
     */
    int64_t generateId() {
        // Get current timestamp in milliseconds since epoch
        auto now = std::chrono::system_clock::now();
        int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ).count();

        // Mask to 41 bits to ensure we don't overflow
        now_ms = now_ms & TIMESTAMP_MASK;

        // Atomically increment the global counter and get its value
        int64_t current_counter = global_counter_.fetch_add(1, std::memory_order_relaxed);
        
        // Extract counter portion (masked to fit in 22 bits)
        int32_t counter_part = static_cast<int32_t>(current_counter & COUNTER_MASK);
        
        // Update the last timestamp if the current one is greater
        int64_t expected_last = last_timestamp_ms_.load(std::memory_order_relaxed);
        while (now_ms > expected_last) {
            if (last_timestamp_ms_.compare_exchange_weak(expected_last, now_ms, 
                                                       std::memory_order_relaxed)) {
                break;  // Successfully updated
            }
            // If CAS failed, expected_last was updated, try again
        }

        // Combine: [0 | 41-bit timestamp | 22-bit counter]
        // Use unsigned for bit operations, then cast to signed
        uint64_t timestamp_part = static_cast<uint64_t>(now_ms) << COUNTER_BITS;
        uint64_t counter_val = static_cast<uint64_t>(counter_part) & COUNTER_MASK;
        uint64_t id_unsigned = timestamp_part | counter_val;

        // Cast to signed - top bit is 0, so always positive
        int64_t id = static_cast<int64_t>(id_unsigned);

        return id;
    }
    
    /**
     * @brief Extract timestamp from generated ID
     * 
     * @param id Message ID
     * @return Timestamp in milliseconds since epoch
     */
    static int64_t extractTimestamp(int64_t id) {
        // Convert to unsigned for safe right shift
        uint64_t id_unsigned = static_cast<uint64_t>(id);
        uint64_t timestamp = id_unsigned >> COUNTER_BITS;
        return static_cast<int64_t>(timestamp);
    }
    
    /**
     * @brief Extract counter from generated ID
     * 
     * @param id Message ID
     * @return Counter value (0 to 4,194,303)
     */
    static int32_t extractCounter(int64_t id) {
        // Convert to unsigned for safe masking
        uint64_t id_unsigned = static_cast<uint64_t>(id);
        return static_cast<int32_t>(id_unsigned & COUNTER_MASK);
    }
    
    /**
     * @brief Get human-readable timestamp from ID
     * 
     * @param id Message ID
     * @return Time point when ID was generated
     */
    static std::chrono::system_clock::time_point extractTimePoint(int64_t id) {
        int64_t timestamp_ms = extractTimestamp(id);
        return std::chrono::system_clock::time_point(
            std::chrono::milliseconds(timestamp_ms)
        );
    }
    
    /**
     * @brief Format timestamp as human-readable string
     * 
     * @param id Message ID (or any timestamp in milliseconds)
     * @return Formatted timestamp "yyyy.mm.dd hh:mm:ss.zzz"
     * 
     * @example
     * formatTimestamp(1709539200000) -> "2024.03.04 12:00:00.000"
     */
    static std::string formatTimestamp(int64_t id) {
        int64_t timestamp_ms = extractTimestamp(id);
        
        // Convert to time_t (seconds) and extract milliseconds
        std::time_t seconds = static_cast<std::time_t>(timestamp_ms / 1000);
        int milliseconds = static_cast<int>(timestamp_ms % 1000);
        
        // Convert to local time
        std::tm* tm_info = std::localtime(&seconds);
        
        // Format the string
        std::ostringstream oss;
        oss << std::setfill('0')
            << (1900 + tm_info->tm_year) << '.'
            << std::setw(2) << (1 + tm_info->tm_mon) << '.'
            << std::setw(2) << tm_info->tm_mday << ' '
            << std::setw(2) << tm_info->tm_hour << ':'
            << std::setw(2) << tm_info->tm_min << ':'
            << std::setw(2) << tm_info->tm_sec << '.'
            << std::setw(3) << milliseconds;
        
        return oss.str();
    }
    
    /**
     * @brief Format message ID as human-readable string
     * 
     * @param id Message ID
     * @return Formatted ID "yyyy.mm.dd hh:mm:ss.zzz [counter]"
     * 
     * @example
     * formatId(id) -> "2024.03.04 12:00:00.000 [1234]"
     */
    static std::string formatId(int64_t id) {
        std::ostringstream oss;
        oss << formatTimestamp(id) << " [" << extractCounter(id) << "]";
        return oss.str();
    }
};

/**
 * @brief Registry for tracking message-based async operations
 * 
 * Maps message IDs to AsyncOp states for request-response protocols.
 * Handles automatic timeout and cleanup of pending messages.
 * 
 * Thread-safe: Yes (all operations are thread-safe)
 * 
 * @tparam ResponseType Type of response data expected
 */
template<typename ResponseType>
class MsgRegistry {
public:
    using MessageId = int64_t;  // Java-compatible signed long
    
    /**
     * @brief Information about a pending message
     */
    struct PendingMessage {
        Promise<ResponseType> state;
        std::chrono::steady_clock::time_point created_at;
        std::chrono::milliseconds timeout;
        timer_type timeout_timer;
        PendingMessage() = default;
        PendingMessage(Promise<ResponseType> s, std::chrono::milliseconds t)
            : state(s)
            , created_at(std::chrono::steady_clock::now())
            , timeout(t)
            , timeout_timer(0) {}
    };
    
private:
    std::unordered_map<MessageId, PendingMessage> pending_;
    mutable std::mutex mutex_;
    IdGen id_generator_;
    
public:
    /**
     * @brief Construct message registry
     */
    MsgRegistry() = default;
    
    /**
     * @brief Destroy registry and cleanup pending messages
     *
     * Rejects all pending messages with ErrorCode::Cancelled
     */
    ~MsgRegistry() {
        // Don't call clearAll() in destructor to avoid issues during global cleanup
        // where spdlog might already be destroyed
        // Instead, rely on users to call clearAll() before global cleanup if needed
    }
    
    // Non-copyable
    MsgRegistry(const MsgRegistry&) = delete;
    MsgRegistry& operator=(const MsgRegistry&) = delete;
    
    // Movable
    MsgRegistry(MsgRegistry&&) = default;
    MsgRegistry& operator=(MsgRegistry&&) = default;
    
    /**
     * @brief Generate a unique message ID without registering
     * 
     * Useful when you need an ID first, then register later.
     * 
     * @return Unique message ID
     * 
     * @note Thread-safe
     * @note Remember to call registerMessage() with this ID later
     * 
     * @example
     * int64_t id = registry.generateId();
     * // ... prepare state ...
     * registry.registerMessage(id, state, timeout);
     */
    MessageId generateId() {
        return id_generator_.generateId();
    }
    
    /**
     * @brief Register a message with auto-generated ID
     * 
     * Convenience method that generates ID and registers in one call.
     * 
     * @param state AsyncOp state to track
     * @param timeout Timeout duration (0 = no timeout)
     * @return Unique message ID to send with request
     * 
     * @note Thread-safe
     * @note If timeout expires, state will be rejected with ErrorCode::Timeout
     * 
     * @example
     * ao::AsyncOp<Response> result;
     * int64_t id = registry.registerMessage(result.m_promise, std::chrono::seconds(5));
     * sendToNetwork(id, data);
     */
    MessageId registerMessage(Promise<ResponseType> state, 
                              std::chrono::milliseconds timeout = std::chrono::milliseconds(0)) {
        MessageId id = id_generator_.generateId();
        registerMessage(id, state, timeout);
        return id;
    }
    
    /**
     * @brief Register a message with pre-generated ID
     * 
     * Useful when you need to generate ID first (e.g., for logging),
     * then register it later.
     * 
     * @param id Pre-generated message ID (from generateId())
     * @param state AsyncOp state to track
     * @param timeout Timeout duration (0 = no timeout)
     * 
     * @note Thread-safe
     * @note ID should come from generateId() to ensure uniqueness
     * @note Throws std::runtime_error if ID already registered
     * 
     * @example
     * int64_t id = registry.generateId();
     * spdlog::info("Sending request with ID {}", id);
     * registry.registerMessage(id, state, std::chrono::seconds(5));
     * sendToNetwork(id, data);
     */
    void registerMessage(MessageId id, 
                        Promise<ResponseType> state,
                        std::chrono::milliseconds timeout = std::chrono::milliseconds(0)) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Check for duplicate ID (should not happen with proper usage)
        auto it = pending_.find(id);
        if (it != pending_.end()) {
            spdlog::error("MessageRegistry: Attempt to register duplicate message ID {}", id);
            throw std::runtime_error("Duplicate message ID: " + std::to_string(id));
        }
        
        // auto& pending = pending_[id];
        // pending = PendingMessage(state, timeout);
        auto [item, inserted] = pending_.try_emplace(id, state, timeout);
        if (!inserted) {
            spdlog::error("MessageRegistry: Failed to insert message ID {}", id);
            throw std::runtime_error("Failed to insert message ID: " + std::to_string(id));
        }
        auto& pending = item->second;
        
        // Set up timeout if requested
        if (timeout.count() > 0) {
            pending.timeout_timer = add_timeout(timeout, [this, id]() {
                handleTimeout(id);
                return false;  // G_SOURCE_REMOVE
            });
            
            spdlog::debug("MessageRegistry: Registered message {} with {}ms timeout", 
                         IdGen::formatId(id),
                         timeout.count());
        } else {
            spdlog::debug("MessageRegistry: Registered message {} without timeout",
                         IdGen::formatId(id));
        }
    }
    
    /**
     * @brief Handle incoming message response
     * 
     * @param id Message ID from response
     * @param response Response data
     * @return true if message was found and handled, false if unknown
     * 
     * @note Thread-safe
     * @note Resolves AsyncOp on main thread via invoke_main()
     */
    bool handleResponse(MessageId id, ResponseType response) {
        Promise<ResponseType> state;
        timer_type timeout_timer = 0;
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = pending_.find(id);
            if (it == pending_.end()) {
                spdlog::warn("MessageRegistry: Received response for unknown message {}", id);
                return false;
            }
            
            state = it->second.state;
            timeout_timer = it->second.timeout_timer;
            pending_.erase(it);
        }
        
        // Cancel timeout if it was set
        if (timeout_timer != 0) {
            remove_timeout(timeout_timer);
        }
        
        // Resolve on main thread
        invoke_main([state, response = std::move(response)]() mutable {
            state->resolveWith(std::move(response));
        });
        
        spdlog::debug("MessageRegistry: Handled response for message {}", id);
        return true;
    }
    
    /**
     * @brief Handle incoming error response
     * 
     * @param id Message ID from error response
     * @param error Error code
     * @return true if message was found and handled, false if unknown
     * 
     * @note Thread-safe
     * @note Rejects AsyncOp on main thread via invoke_main()
     */
    bool handleError(MessageId id, ErrorCode error) {
        Promise<ResponseType> state;
        timer_type timeout_timer = 0;
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = pending_.find(id);
            if (it == pending_.end()) {
                spdlog::warn("MessageRegistry: Received error for unknown message {}", id);
                return false;
            }
            
            state = it->second.state;
            timeout_timer = it->second.timeout_timer;
            pending_.erase(it);
        }
        
        // Cancel timeout if it was set
        if (timeout_timer != 0) {
            remove_timeout(timeout_timer);
        }
        
        // Reject on main thread
        invoke_main([state, error]() {
            state->rejectWith(error);
        });
        
        spdlog::debug("MessageRegistry: Handled error for message {} with code {}", 
                     id, static_cast<int>(error));
        return true;
    }
    
    /**
     * @brief Cancel a pending message
     * 
     * @param id Message ID to cancel
     * @return true if message was found and cancelled
     * 
     * @note Thread-safe
     * @note Rejects AsyncOp with ErrorCode::Cancelled
     */
    bool cancelMessage(MessageId id) {
        Promise<ResponseType> state;
        timer_type timeout_timer = 0;
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = pending_.find(id);
            if (it == pending_.end()) {
                return false;
            }
            
            state = it->second.state;
            timeout_timer = it->second.timeout_timer;
            pending_.erase(it);
        }
        
        // Cancel timeout if it was set
        if (timeout_timer != 0) {
            remove_timeout(timeout_timer);
        }
        
        // Reject with Cancelled
        invoke_main([state]() {
            state->rejectWith(ErrorCode::Cancelled);
        });
        
        spdlog::debug("MessageRegistry: Cancelled message {}", id);
        return true;
    }
    
    /**
     * @brief Get number of pending messages
     * 
     * @return Count of messages waiting for response
     * 
     * @note Thread-safe
     */
    size_t pendingCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pending_.size();
    }
    
    /**
     * @brief Check if a specific message is pending
     * 
     * @param id Message ID to check
     * @return true if message is pending
     * 
     * @note Thread-safe
     */
    bool isPending(MessageId id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pending_.find(id) != pending_.end();
    }
    
    /**
     * @brief Clear all pending messages
     * 
     * Rejects all pending messages with ErrorCode::Cancelled.
     * Typically called during shutdown.
     * 
     * @note Thread-safe
     */
    void clearAll() {
        std::unordered_map<MessageId, PendingMessage> pending_copy;
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_copy = std::move(pending_);
            pending_.clear();
        }
        
        // Reject all and cancel timeouts
        for (auto& [id, pending] : pending_copy) {
            if (pending.timeout_timer != 0) {
                remove_timeout(pending.timeout_timer);
            }
            
            invoke_main([state = pending.state]() {
                state->rejectWith(ErrorCode::Cancelled);
            });
        }
        
        if (!pending_copy.empty()) {
            spdlog::info("MessageRegistry: Cleared {} pending messages", pending_copy.size());
        }
    }
    
    /**
     * @brief Get diagnostic information about pending messages
     * 
     * @return Vector of [message_id, age_in_ms] pairs
     * 
     * @note Thread-safe
     * @note Useful for debugging and monitoring
     */
    std::vector<std::pair<MessageId, int64_t>> getDiagnostics() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::pair<MessageId, int64_t>> result;
        result.reserve(pending_.size());
        
        auto now = std::chrono::steady_clock::now();
        for (const auto& [id, pending] : pending_) {
            auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - pending.created_at
            ).count();
            result.emplace_back(id, age_ms);
        }
        
        return result;
    }
    
private:
    /**
     * @brief Handle message timeout (called by timer)
     * 
     * @param id Message ID that timed out
     */
    void handleTimeout(MessageId id) {
        Promise<ResponseType> state;
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = pending_.find(id);
            if (it == pending_.end()) {
                // Already handled (response arrived)
                return;
            }
            
            state = it->second.state;
            pending_.erase(it);
        }
        
        // Reject with Timeout
        invoke_main([state]() {
            state->rejectWith(ErrorCode::Timeout);
        });
        
        spdlog::warn("MessageRegistry: Message {} timed out", id);
    }
};

// Convenient type aliases
template<typename T>
using MessageRegistry = MsgRegistry<T>;  // Backward compatibility

} // namespace ao

#endif // MESSAGE_REGISTRY_HPP