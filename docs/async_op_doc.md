# AsyncOp Library v2.4 - Documentation

> **Lightweight Promise/Future pattern for embedded Linux with GLib or Qt**
> 
> Modern async programming with chainable operations, powerful error recovery, and comprehensive collection utilities. Designed for embedded systems with moderate memory constraints (64MB+ RAM).

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Backend](https://img.shields.io/badge/Backend-GLib%20%7C%20Qt-green.svg)](https://github.com/)

---

## Table of Contents

1. [Quick Start](#quick-start)
2. [Core Concepts](#core-concepts)
3. [Promise/Future Semantics](#promisefuture-semantics)
4. [Basic Usage](#basic-usage)
5. [Error Handling & Recovery](#error-handling--recovery)
6. [Collection Operations](#collection-operations)
7. [Core API Reference](#core-api-reference)
8. [Utility Functions](#utility-functions)
9. [Thread Safety](#thread-safety)
10. [Best Practices](#best-practices)
11. [Troubleshooting](#troubleshooting)
12. [Performance Tips](#performance-tips)

---

## Quick Start

### Installation

```cpp
#include "async_op.hpp"

// For Qt backend: please define ASYNC_USE_QT in CMakeLists.txt or compiler option.
```

### Your First Async Operation

```cpp
// Simple delay
ao::delay(std::chrono::seconds(1))
    .then([]() {
        spdlog::info("1 second has passed!");
    });

// Chaining operations
fetchUserAsync(userId)
    .then([](User user) {
        return fetchUserPostsAsync(user.id);
    })
    .then([](std::vector<Post> posts) {
        displayPosts(posts);
    })
    .onError([](ao::ErrorCode err) {
        spdlog::error("Failed: {}", err);  // Uses fmt formatter
    });
```

### With Error Recovery

```cpp
fetchFromPrimaryServer()
    .recover([](ao::ErrorCode err) {
        spdlog::warn("Primary failed ({}), using cache", err);
        return getCachedData();  // Recover to success!
    })
    .then([](Data data) {
        // data from either primary or cache
        processData(data);
    });
```

### Requirements

- **C++17 or later**
- **GLib 2.0+** or **Qt 5.12+** (event loop backend)
- **spdlog** (for logging, includes fmt)
- **Modern compiler** (GCC 7+, Clang 5+)

---

## Core Concepts

### What is AsyncOp?

`AsyncOp<T>` represents an asynchronous operation that will eventually:
- **Resolve** with a value of type `T` (success), or
- **Reject** with an `ErrorCode` (failure)

Think of it as a "container for a future value" that you can chain operations on.

### Promise/Future Model

AsyncOp uses the **Promise/Future pattern**:

- **`AsyncOp<T>`** = **Future** (consumer side)
  - Provides methods to react to results: `.then()`, `.onError()`, `.recover()`
  - Returned from async functions
  - Can be copied and shared safely

- **`Promise<T>`** = **Promise** (producer side)  
  - Type alias for `std::shared_ptr<AsyncOp<T>::State>`
  - Provides methods to produce results: `.resolveWith()`, `.rejectWith()`
  - Captured in async callbacks to settle the operation
  - Obtained via `.promise()` method on AsyncOp

### The Six Core Methods

| Method | Purpose | When Used | Continues Chain? | Overwrite Protection |
|--------|---------|-----------|------------------|----------------------|
| **`.then()`** | Transform success values | Success path | ✅ Yes (can change type) | Prevents overwriting existing success callbacks |
| **`.next()`** | Handle both paths | Dual processing | ✅ Yes (converges types) | Prevents overwriting existing callbacks |
| **`.recover()`** / **`.otherwise()`** | Convert errors to success | Error recovery | ✅ Yes (error→value) | Allows overwrite if previous callback was for propagation |
| **`.onSuccess()`** | Final success handling | Success path (terminal) | ❌ No | Prevents overwriting existing success callbacks |
| **`.onError()`** | Final error handling | Error path (terminal) | ❌ No | Prevents overwriting existing error callbacks |
| **`.tap()`** | Side effects | Debugging/logging | ✅ Yes (value unchanged) | Does not prevent subsequent overwrites |

### Flow Visualization

```
Linear Success Chain:
    operation → .then() → .then() → .then() → success!
                   ↓         ↓         ↓
                   └─────────┴─────────→ .onError() (if any fails)

Terminal Success Chain:
    operation → .then() → .then() → .onSuccess() (terminates chain)
                   ↓         ↓
                   └─────────┴─────→ .onError() (if any fails)

Error Recovery (Single Chain):
    operation → .recover() → .then() → success!
       ↓             ↓
     error      recovers to value

Dual-Path Handling (Convergent):
    operation
       ├─ success → success_handler ─┐
       │                              ├─→ .next() → single continuation
       └─ error   → error_handler   ─┘

Independent Branching (Mutually Exclusive):
    operation ──────→ .then(A) → .then(B) → ...     (Success branch)
       │
       └───────────→ .otherwise(C) → .then(D) → ... (Error branch)

    Only ONE branch executes
```

### Key Insights

**Propagation Rules:**
- **`.then()`**: Executes on success, **auto-propagates errors unchanged**
- **`.recover()`** / **`.otherwise()`**: Execute on error, **auto-propagate successes unchanged**
- **`.next()`**: Executes appropriate handler for **both** success and error, **always continues**
- **`.onSuccess()`**: Executes on success, **terminates the chain** (no continuation)
- **`.onError()`**: Executes on error, **terminates the chain** (no continuation)

**Type Transformation:**
- **`.then(T → U)`**: Can change type from T to U (value transformation)
- **`.recover(error → T)`**: Must return same type T as the AsyncOp (error recovery)
- **`.next((T → U), (error → U))`**: Both handlers must return same type U (convergence)
- **`.otherwise(error → T)`**: Alias for `.recover()`, identical semantics

---

## Promise/Future Semantics

### Understanding Promise vs Future

AsyncOp follows standard Promise/Future separation:

```cpp
// Creating an async operation
ao::AsyncOp<int> fetchData() {
    // Create the Future (return value)
    ao::AsyncOp<int> future;
    
    // Get the Promise (to settle it later)
    ao::Promise<int> promise = future.promise();
    
    // Schedule async work - capture promise, return future
    ao::add_timeout(100ms, [promise]() {
        promise->resolveWith(42);  // Promise settles the future
        return false;  // G_SOURCE_REMOVE
    });
    
    return future;  // Consumer gets the future
}

// Using the async operation
fetchData()  // Returns AsyncOp<int> (Future)
    .then([](int value) {  // React to the future value
        spdlog::info("Got value: {}", value);
    });
```

### Promise API

```cpp
template<typename T>
using Promise = std::shared_ptr<typename AsyncOp<T>::State>;

// Promise methods (producer side):
promise->resolveWith(T value);                       // Settle with success
promise->rejectWith(ErrorCode error);                // Settle with error
promise->isPending();                                // Query state
promise->isResolved();
promise->isRejected();
promise->isSettled();
promise->canOverwriteSuccessCallback();              // Check if success callback can be overwritten
promise->canOverwriteErrorCallback();                // Check if error callback can be overwritten
```

**Key characteristics:**
- **Idempotent**: Calling `resolveWith()` or `rejectWith()` multiple times is safe (ignored after first)
- **Shared ownership**: `Promise<T>` is a `shared_ptr`, safe to capture in multiple callbacks
- **Thread-safe creation**: Safe to create on any thread, but settlement must be on main thread

### Future API

```cpp
// AsyncOp methods (consumer side):
AsyncOp<T> future;

// Getting the promise
Promise<T> promise = future.promise();

// Chaining (returns new AsyncOp with transformed type)
future.then([](T value) -> U { ... });           // T → U transformation
future.recover([](ErrorCode) -> T { ... });      // Error → T recovery
future.otherwise([](ErrorCode) -> T { ... });    // Alias for recover()
future.next(success_fn, error_fn);               // Dual-path convergence

// Terminal handlers (no continuation)
future.onError([](ErrorCode) { ... });

// Utilities
future.timeout(duration);                        // Add timeout
future.tap([](const T&) { ... });                // Side effects
future.finally([]() { ... });                    // Cleanup
future.orElse(fallback_value);                   // Simple fallback

// Static factories
AsyncOp<T>::resolved(value);                     // Already resolved
AsyncOp<T>::rejected(error);                     // Already rejected

// State queries
future.isPending();
future.isResolved();
future.isRejected();
future.isSettled();
future.getError();  // Returns ErrorCode if rejected
```

### Important: Single Callback Limitation

⚠️ **Unlike standard Promise/Future libraries, AsyncOp supports only ONE callback per operation type:**

```cpp
// ❌ WRONG: Second then() cannot overwrite first
AsyncOp<int> op = fetchData();
op.then([](int x) { spdlog::info("First: {}", x); });   // may work.
op.then([](int x) { spdlog::info("Second: {}", x); });  // This will never run!

// ❌ WRONG: Second onSuccess() cannot overwrite first
AsyncOp<int> op = fetchData();
op.onSuccess([](int x) { spdlog::info("First: {}", x); });   // may work.
op.onSuccess([](int x) { spdlog::info("Second: {}", x); });  // This will never run!

// ✅ CORRECT: Use .tap() for side effects without overwriting
fetchData()
    .tap([](int x) { spdlog::info("Logging: {}", x); })  // Side effect
    .then([](int x) { return processData(x); })          // Main processing
    .then([](Data d) { return saveData(d); });           // Continue chain

// ✅ CORRECT: Create independent branches by capturing promise separately
AsyncOp<int> op = fetchData();
Promise<int> promise = op.promise();

op.then([](int x) { processBranchA(x); });               // Success branch
op.otherwise([](ErrorCode e) { processBranchB(e); });    // Error branch
// Only ONE branch executes (mutually exclusive)

// ✅ CORRECT: Use onSuccess() as terminal success handler
fetchData()
    .then([](int x) { return processResult(x); })
    .onSuccess([](ProcessedData data) {                  // Terminal success handler
        displayResult(data);
    })
    .onError([](ErrorCode err) {                         // Terminal error handler
        handleError(err);
    });
// Only one of onSuccess() or onError() will execute
```

This design simplifies memory management for embedded systems. The library now includes callback overwrite protection that prevents accidental overwrites unless the previous callback was a propagation callback. The library will log an error if you attempt to overwrite a callback that is not safe to overwrite.

---

## Basic Usage

### Creating AsyncOp Operations

#### From Your Async Functions

```cpp
ao::AsyncOp<Data> fetchDataAsync() {
    ao::AsyncOp<Data> future;
    ao::Promise<Data> promise = future.promise();
    
    // Schedule async work - always capture promise, never the future!
    ao::add_timeout(100ms, [promise]() {
        promise->resolveWith(Data{"hello"});
        return false;  // G_SOURCE_REMOVE (single-shot timer)
    });
    
    return future;  // Consumer gets the future
}
```

**⚠️ CRITICAL:** Always capture `promise` (via `future.promise()`), never the `AsyncOp` itself!

```cpp
// ✅ CORRECT
ao::AsyncOp<int> future;
ao::Promise<int> promise = future.promise();
add_timeout(ms, [promise]() { promise->resolveWith(42); });

// ❌ WRONG - Don't capture the future
add_timeout(ms, [future]() { /* How do we settle it? */ });
```

#### Alternative: Direct member access (for brevity)

For convenience, you can access `m_promise` directly:

```cpp
ao::AsyncOp<Data> fetchDataAsync() {
    ao::AsyncOp<Data> future;
    
    // Direct access to promise member
    ao::add_timeout(100ms, [promise = future.m_promise]() {
        promise->resolveWith(Data{"hello"});
        return false;
    });
    
    return future;
}
```

Both styles are equivalent - use whichever is clearer in your codebase.

#### Factory Methods

```cpp
// Create already-resolved future
auto success = ao::AsyncOp<int>::resolved(42);

// Create already-rejected future
auto failure = ao::AsyncOp<int>::rejected(ao::ErrorCode::NetworkError);
```

Perfect for:
- Testing async code with synchronous values
- Returning cached/immediate results in async API
- Short-circuiting async chains

### Chaining with then()

```cpp
// Simple transformation
fetchValue()
    .then([](int x) {
        spdlog::info("Got: {}", x);
        return x * 2;  // Transform the value
    })
    .then([](int doubled) {
        spdlog::info("Doubled: {}", doubled);
    });

// Type transformation (int → string)
fetchValue()
    .then([](int x) -> std::string {
        return std::to_string(x);
    })
    .then([](std::string s) {
        spdlog::info("String: {}", s);
    });

// Chain async operations (returns AsyncOp, auto-unwraps)
fetchUserId()
    .then([](int id) -> ao::AsyncOp<User> {
        return fetchUserAsync(id);  // Returns another AsyncOp
    })
    .then([](User user) -> ao::AsyncOp<Posts> {
        return fetchUserPostsAsync(user.id);
    })
    .then([](Posts posts) {
        displayPosts(posts);
    });
```

### Basic Error Handling

```cpp
operation()
    .then([](Result r) {
        // Success path
        return processResult(r);
    })
    .onError([](ao::ErrorCode err) {
        // Error path (terminal - no continuation)
        spdlog::error("Operation failed: {}", err);
    });

// Using onSuccess() for terminal success handling
operation()
    .then([](Result r) {
        // Success path - transform or continue chain
        return processResult(r);
    })
    .onSuccess([](ProcessedResult result) {
        // Terminal success handler (no continuation)
        displayResult(result);
    })
    .onError([](ao::ErrorCode err) {
        // Terminal error handler (no continuation)
        spdlog::error("Operation failed: {}", err);
    });
// Only one of onSuccess() or onError() will execute
```

### Essential Utilities

#### timeout()

Add timeout to any operation:

```cpp
fetchFromServer()
    .timeout(std::chrono::seconds(5))
    .then([](Data d) {
        // Got data within 5 seconds
        processData(d);
    })
    .onError([](ao::ErrorCode err) {
        if (err == ao::ErrorCode::Timeout) {
            spdlog::error("Server didn't respond in time");
        }
    });
```

**⚠️ IMPORTANT: Timer starts immediately when `timeout()` is called**, not when the previous operation completes.

```cpp
// Timeout applies to BOTH op1 AND op2 combined (timer starts immediately when timeout() is called)
// Total time from op1 start through op2 completion must be < 3000ms
op1().then([](){ return op2(); }).timeout(3000ms);

// Timeout applies ONLY to op2 (timer starts when op2's chain is set up)
// op1 can take any amount of time; op2 must complete within 3000ms of starting
op1().then([](){ return op2().timeout(3000ms); });
```

**Timeline visualization:**

```
// Case 1: .then().timeout() - timer covers entire chain
op1().then([](){ return op2(); }).timeout(3000ms)

Timeline:
|-- op1 starts
|   timeout() called, timer starts NOW ⏱️
|--- op1 completes (500ms elapsed)
|       .then() callback runs, op2 starts
|------- op2 completes (800ms total) ✓ Success (under 3000ms)

If op1 takes 2500ms and op2 takes 1000ms:
|-- op1 starts
|   timeout() called, timer starts ⏱️
|----- op1 completes (2500ms elapsed)
|         .then() callback runs, op2 starts
|----------- TIMEOUT fires at 3000ms! ✗ op2 cancelled


// Case 2: .then(op2().timeout()) - timer covers only inner operation
op1().then([](){ return op2().timeout(3000ms); })

Timeline:
|-- op1 starts
|--- op1 completes (could be 10 seconds, no timeout yet)
|       op2().timeout() called, timer starts NOW ⏱️
|--------- op2 completes (500ms) ✓ Success
```

#### tap()

Execute side effects without modifying the value:

```cpp
operation()
    .tap([](const Result& r) {
        // Side effects - value passes through unchanged
        spdlog::info("Got result, size: {}", r.size);
        recordMetric("operation_success");
    })
    .then([](Result r) {
        // r is unchanged from before tap()
        processResult(r);
    });
```

**Use cases:**
- Logging
- Metrics collection
- Debug breakpoints
- Validation (throw to convert to error)

#### finally()

Always execute cleanup code:

```cpp
auto lock = acquireLock();

operation()
    .then([](Result r) {
        return processResult(r);
    })
    .finally([lock]() {
        // Always executes, regardless of success or failure
        releaseLock(lock);
    })
    .onError([](ao::ErrorCode err) {
        // Lock already released by finally()
        spdlog::error("Failed: {}", err);
    });
```

#### cancel()

Cancel a pending operation with a configurable error code:

```cpp
// Cancel with default error (ErrorCode::Cancelled)
auto op = fetchData();
cancelButton.clicked.connect([&op]() {
    op.cancel();  // Rejects with ErrorCode::Cancelled
});

// Cancel with custom error code
op.cancel(ao::ErrorCode::NetworkError);

// Chain after cancel
op.cancel()
    .onError([](ao::ErrorCode err) {
        spdlog::warn("Operation cancelled: {}", err);
    });
```

**Important notes:**
- `cancel()` only rejects the AsyncOp state, not underlying operations
- For timer-based operations, you must clean up resources manually:

```cpp
// Typical timer pattern with cleanup
auto timer_op = ao::AsyncOp<Data>();
auto timer_id = add_timeout(5000ms, [timer_op]() {
    timer_op.m_promise->resolveWith(data);
    return false;
});

// Cancel button handler
cancelButton.clicked.connect([&timer_op, timer_id]() {
    ao::remove_timeout(timer_id);  // Manual cleanup
    timer_op.cancel();             // Reject the AsyncOp
});
```

---

## Advanced Filtering with filter()

The `filter()` method provides unified handling for both success and error paths with a single call.

### Dual-Path Filtering

```cpp
operation()
    .filter(
        [](Result& r) -> Result {
            // Success filter: validate and pass through
            if (!r.isValid()) {
                throw ao::ErrorCode::InvalidResponse;  // Reject
            }
            return std::move(r);  // Pass through (use std::move to avoid copy)
        },
        [](ao::ErrorCode err) -> Result {
            // Error filter: recover from specific errors
            if (err == ao::ErrorCode::Timeout) {
                return getCachedResult();  // Recover with cached data
            }
            throw err;  // Propagate other errors
        }
    )
    .then([](Result r) {
        // Receives result from either success path or error recovery
        processResult(r);
    });
```

### Success Filter Only

Pass `nullptr` for error filter to propagate errors unchanged:

```cpp
fetchData()
    .filter(
        [](Data& d) -> Data {
            if (d.empty()) throw ao::ErrorCode::InvalidResponse;
            return std::move(d);
        },
        nullptr  // Errors propagate unchanged
    )
    .onError([](ao::ErrorCode err) {
        // Catches both fetch errors and validation errors
        handleAllErrors(err);
    });
```

### Error Filter Only

Pass `nullptr` for success filter to pass values unchanged:

```cpp
fetchData()
    .filter(
        nullptr,  // Success propagates unchanged
        [](ao::ErrorCode err) -> Data {
            // Recovery logic - equivalent to recoverFrom() but more flexible
            if (err == ao::ErrorCode::NetworkError) {
                return getFallbackData();
            }
            throw err;  // Propagate other errors
        }
    )
    .then([](Data d) {
        processData(d);
    });
```

### Migration from recoverFrom()

`filterError()` provides a cleaner API for error-only handling:

```cpp
// Old: recoverFrom (deprecated)
op.recoverFrom(ao::ErrorCode::Timeout, [](ErrorCode err) {
    return defaultValue;
});

// New: filterError (recommended)
op.filterError([](ao::ErrorCode err) -> T {
    if (err == ao::ErrorCode::Timeout) return defaultValue;
    throw err;  // propagate other errors
});
```

### filterSuccess() and filterError()

Convenience wrappers for single-path filtering:

```cpp
// Validate success result, errors propagate unchanged
op.filterSuccess([](Data& d) -> Data {
    if (!isValid(d)) throw ao::ErrorCode::InvalidResponse;
    return std::move(d);
});

// Handle specific errors, success propagates unchanged
op.filterError([](ao::ErrorCode err) -> Data {
    if (err == ao::ErrorCode::Timeout) return getCachedData();
    throw err;  // propagate other errors
});
```

### filter() Callback Semantics

| Filter Path | Return Value | Throw ErrorCode |
|-------------|--------------|-----------------|
| Success     | Pass value through | Reject with error |
| Error       | Recover with value | Propagate error |

**Move semantics:** For expensive-to-copy types, use `std::move()` on return:

```cpp
.filterSuccess([](LargeObject& obj) -> LargeObject {
    if (!isValid(obj)) throw ao::ErrorCode::InvalidResponse;
    return std::move(obj);  // Avoid copy
})
```

---

## Error Handling & Recovery

AsyncOp provides multiple powerful patterns for handling errors and recovering from failures.

### Single-Path Recovery: recover() / otherwise()

Convert errors back to success values and continue the chain.

#### Basic Error Recovery

```cpp
fetchFromServer()
    .recover([](ao::ErrorCode err) -> Data {
        spdlog::warn("Server failed ({}), using cache", err);
        return getCachedData();  // Convert error → success
    })
    .then([](Data d) {
        // d is from either server (success) or cache (recovered)
        processData(d);
    });
```

#### Cascading Fallbacks

```cpp
fetchFromPrimary()
    .recover([](ao::ErrorCode err) -> ao::AsyncOp<Data> {
        spdlog::warn("Primary failed ({}), trying secondary", err);
        return fetchFromSecondary();  // Return AsyncOp - auto-unwrapped
    })
    .recover([](ao::ErrorCode err) -> Data {
        spdlog::warn("Secondary failed ({}), using cache", err);
        return getCachedData();  // Final fallback
    })
    .then([](Data d) {
        // d from primary, secondary, or cache
        processData(d);
    });
```

#### Conditional Recovery (Specific Errors)

```cpp
operation()
    .recoverFrom(ao::ErrorCode::NetworkError, [](ao::ErrorCode err) {
        return retryOperation();  // Only recover from network errors
    })
    // Other errors propagate unchanged
    .onError([](ao::ErrorCode err) {
        // Handle non-network errors
        spdlog::error("Unrecoverable error: {}", err);
    });
```

#### otherwise() - Semantic Alias

`otherwise()` is identical to `recover()` but expresses branching intent:

```cpp
// recover() - expresses error recovery
parseConfig()
    .recover([](ao::ErrorCode) {
        return getDefaultConfig();  // Recover from parse failure
    });

// otherwise() - expresses alternative path
fetchFromCache(key)
    .otherwise([](ao::ErrorCode) {
        return fetchFromDatabase(key);  // If not in cache, try DB
    });
```

Use whichever name better expresses your code's intent.

### Dual-Path Convergence: next()

Handle success and error differently, then converge to same type:

```cpp
fetchUserData(userId)
    .next(
        // Success handler: process real data
        [](UserData data) -> DisplayData {
            return DisplayData{
                .name = data.name,
                .status = "Online",
                .data = processUserData(data)
            };
        },
        // Error handler: create placeholder
        [](ao::ErrorCode err) -> DisplayData {
            spdlog::warn("User data unavailable: {}", err);
            return DisplayData{
                .name = "Unknown User",
                .status = "Offline",
                .data = {}
            };
        }
    )
    .then([](DisplayData display) {
        // Single path - display either real or placeholder data
        showUserDisplay(display);
    });
```

**When to use `.next()`:**
- Different processing for success/error, but same continuation point
- UI display with real data or placeholders
- Analytics with actual metrics or estimated values
- Both paths produce valid (different) results

### Independent Branching: Success vs Error Paths

Create mutually exclusive branches from a single operation:

```cpp
auto op = fetchData();

// Success branch
op.then([](Data d) {
    return validateData(d);
})
.then([](ValidData vd) {
    return saveToDatabase(vd);
})
.then([](SaveResult r) {
    spdlog::info("Data saved successfully");
});

// Error branch (completely independent)
op.otherwise([](ao::ErrorCode err) {
    return logErrorToMonitoring(err);
})
.then([](LogResult lr) {
    return sendErrorNotification(lr);
})
.then([]() {
    spdlog::info("Error logged and notified");
});

// Only ONE branch executes (success XOR error)
```

### Simple Fallback: orElse()

Provide a default value if operation fails:

```cpp
fetchUserPreferences(userId)
    .orElse(
        getDefaultPreferences(),  // Fallback value
        "User preferences unavailable"  // Optional log message
    )
    .then([](Preferences prefs) {
        // prefs is either user's or default
        applyPreferences(prefs);
    });
```

### Error Code Reference

```cpp
enum class ErrorCode {
    None,                // No error (should not occur in error handlers)
    Timeout,            // Operation timed out
    NetworkError,       // Network communication failed
    InvalidResponse,    // Response validation failed
    Cancelled,          // Operation was cancelled
    Exception,          // Exception thrown in callback
    MaxRetriesExceeded, // Retry limit reached
    Unknown             // Unspecified error
};
```

**fmt formatter provided:** Error codes automatically format to names:
```cpp
spdlog::error("Failed: {}", err);  // "Failed: NetworkError"
// No need for static_cast<int>(err)
```

---

## Collection Operations

AsyncOp provides powerful utilities for working with collections of async operations.

### Sequential Processing

Process items one at a time (useful for rate limiting or ordered execution).

#### forEach() - Fail Fast

```cpp
std::vector<UserId> users = {1, 2, 3, 4, 5};

ao::forEach(users, [](UserId id) {
    return processUserAsync(id);
})
.then([]() {
    spdlog::info("All users processed successfully");
})
.onError([](ao::ErrorCode err) {
    spdlog::error("Failed on one user: {}", err);
    // Remaining users NOT processed
});
```

- **Behavior**: Stops on first error
- **Use when**: All items must succeed, order matters

#### forEachSettled() - Fault Tolerant

```cpp
std::vector<Item> items = loadItems();

ao::forEachSettled(items, [](const Item& item) {
    return processItemAsync(item);
})
.then([](std::vector<Item> failed) {
    if (failed.empty()) {
        spdlog::info("All items processed successfully");
    } else {
        spdlog::warn("{} items failed, retrying", failed.size());
        return retryItems(failed);
    }
});
```

- **Behavior**: Processes ALL items, collects failures
- **Returns**: `AsyncOp<std::vector<T>>` containing failed items
- **Use when**: Want to retry failed items, best-effort processing

#### map() - Transform Sequential

```cpp
std::vector<int> ids = {1, 2, 3};

ao::map(ids, [](int id) -> ao::AsyncOp<User> {
    return fetchUserAsync(id);
})
.then([](std::vector<User> users) {
    spdlog::info("Loaded {} users", users.size());
    displayUsers(users);
})
.onError([](ao::ErrorCode err) {
    spdlog::error("Failed to load users: {}", err);
});
```

- **Behavior**: Transform each item sequentially, stops on first error
- **Returns**: `AsyncOp<std::vector<U>>` containing transformed items
- **Use when**: Order matters, dependent transformations

#### mapSettled() - Transform with Full Details

```cpp
ao::mapSettled(items, [](const Item& item) {
    return transformItemAsync(item);
})
.then([](std::vector<ao::SettledResult<TransformedItem>> results) {
    for (size_t i = 0; i < results.size(); ++i) {
        if (results[i].isFulfilled()) {
            processSuccess(results[i].value);
        } else {
            spdlog::error("Item {} failed: {}", i, results[i].error);
        }
    }
});
```

- **Behavior**: Transforms ALL items, provides detailed results
- **Returns**: `AsyncOp<std::vector<SettledResult<U>>>`
- **Use when**: Need visibility into which items succeeded/failed

### Parallel Processing

Process multiple items simultaneously (much faster for independent operations).

#### all() - Wait for All, Fail Fast

```cpp
std::vector<ao::AsyncOp<User>> ops = {
    fetchUserAsync(1),
    fetchUserAsync(2),
    fetchUserAsync(3)
};

ao::all(ops)
    .then([](std::vector<User> users) {
        // All operations succeeded
        spdlog::info("Loaded {} users", users.size());
        displayUsers(users);
    })
    .onError([](ao::ErrorCode err) {
        // At least one operation failed
        spdlog::error("Failed to load all users: {}", err);
    });
```

- **Behavior**: All start immediately, resolves when all succeed
- **Fails**: On first error
- **Use when**: All results needed, fail if any fails

#### allSettled() - Wait for All, Get Details

```cpp
std::vector<ao::AsyncOp<Data>> ops = createOperations();

ao::allSettled(ops)
    .then([](std::vector<ao::SettledResult<Data>> results) {
        int succeeded = 0, failed = 0;
        for (const auto& r : results) {
            if (r.isFulfilled()) {
                processData(r.value);
                succeeded++;
            } else {
                spdlog::error("Operation failed: {}", r.error);
                failed++;
            }
        }
        spdlog::info("Results: {} succeeded, {} failed", succeeded, failed);
    });
```

- **Behavior**: All start immediately, waits for all to settle
- **Never fails**: Always resolves with all results
- **Use when**: Want full visibility, best-effort batch processing

#### mapParallel() - Transform in Parallel

```cpp
std::vector<int> ids = {1, 2, 3, 4, 5};

ao::mapParallel(ids, [](int id) {
    return fetchUserAsync(id);
})
.then([](std::vector<User> users) {
    spdlog::info("Loaded {} users in parallel", users.size());
    displayUsers(users);
});
```

- **Behavior**: Transforms all items simultaneously
- **Returns**: `AsyncOp<std::vector<U>>` (results in input order)
- **Use when**: Independent transformations, speed matters

### Race Operations

#### race() - First to Settle Wins

```cpp
std::vector<ao::AsyncOp<Data>> ops = {
    fetchFromServerA(),
    fetchFromServerB(),
    fetchFromServerC()
};

ao::race(ops)
    .then([](Data d) {
        // First operation that completed (success or failure)
        spdlog::info("Got data from fastest server");
        processData(d);
    })
    .onError([](ao::ErrorCode err) {
        // First operation completed with error
        spdlog::warn("Fastest server failed: {}", err);
    });
```

- **Behavior**: First to complete (success OR error) wins
- **Use when**: Timeout scenarios, redundant requests

#### any() - First Success Wins

```cpp
std::vector<ao::AsyncOp<Data>> ops = {
    fetchFromPrimary(),
    fetchFromSecondary(),
    fetchFromCache()
};

ao::any(ops)
    .then([](Data d) {
        // First successful operation
        spdlog::info("Got data from one source");
        processData(d);
    })
    .onError([](ao::ErrorCode err) {
        // ALL operations failed
        spdlog::error("All sources failed, last error: {}", err);
    });
```

- **Behavior**: First success wins, fails only if all fail
- **Use when**: Fallback sources, redundancy patterns

### Choosing the Right Collection Operation

| Need | Sequential | Parallel |
|------|-----------|----------|
| **All succeed or fail** | `forEach()` | `all()` |
| **Process all, collect failures** | `forEachSettled()` | `allSettled()` |
| **Transform items** | `map()` | `mapParallel()` |
| **Transform with details** | `mapSettled()` | _(use mapParallel + allSettled)_ |
| **First to finish** | _(not applicable)_ | `race()` |
| **First success** | _(not applicable)_ | `any()` |

---

## Core API Reference

### AsyncOp<T> Class

```cpp
template<typename T>
class AsyncOp {
public:
    // Type alias for promise
    using Promise = std::shared_ptr<State>;

    // Construction
    AsyncOp();                                    // Create pending operation
    AsyncOp(const AsyncOp&) = default;            // Copy (shares state)
    AsyncOp(AsyncOp&&) = default;                 // Move

    // Static factories
    static AsyncOp<T> resolved(T value);          // Create resolved
    static AsyncOp<T> rejected(ErrorCode err);    // Create rejected

    // State queries
    bool isPending() const;
    bool isResolved() const;
    bool isRejected() const;
    bool isSettled() const;
    ErrorCode getError() const;
    id_type id() const;                           // For logging/debugging

    // Get promise for async callbacks
    Promise<T> promise() const;
    // Or direct member access:
    Promise<T> m_promise;  // Public member
    
    // Chaining (returns new AsyncOp)
    template<typename F>
    auto then(F&& f) -> AsyncOp<U>;               // Success handler
    // NOTE: Callback overwrite protection prevents overwriting existing success callbacks
    // unless the previous callback was a propagation callback.
    
    template<typename F>
    auto recover(F&& f) -> AsyncOp<T>;            // Error → value
    // NOTE: Allows overwriting error callbacks when previous callback was for propagation.
    
    template<typename F>
    auto otherwise(F&& f) -> AsyncOp<T>;          // Alias for recover
    // NOTE: Allows overwriting error callbacks when previous callback was for propagation.
    
    template<typename SuccessF, typename ErrorF>
    auto next(SuccessF&& s, ErrorF&& e) -> AsyncOp<U>;  // Dual-path
    // NOTE: Callback overwrite protection applies to both success and error handlers.
    
    // Terminal handlers (no continuation)
    AsyncOp<T>& onSuccess(std::function<void(T)> handler);
    // NOTE: Callback overwrite protection prevents overwriting existing success callbacks
    // unless the previous callback was a propagation callback.
    // WARNING: If not the last handler in chain, the next handler must be recover() or otherwise().

    AsyncOp<T>& onError(std::function<void(ErrorCode)> handler);
    // NOTE: Callback overwrite protection prevents overwriting existing error callbacks
    // unless the previous callback was a propagation callback.
    // WARNING: If not the last handler in chain, the next handler must be then().

    // Cancellation
    AsyncOp<T>& cancel(ErrorCode code = ErrorCode::Cancelled);
    // Rejects pending operation with specified error code
    // Returns *this for chaining; no-op if already settled
    // Note: Only cancels AsyncOp state, not underlying operations (timers, etc.)

    // Utilities
    AsyncOp<T> timeout(std::chrono::milliseconds duration);

    template<typename F>
    AsyncOp<T> tap(F&& side_effect_fn);           // Side effects
    // NOTE: Does not prevent subsequent callback overwrites.

    template<typename F>
    AsyncOp<T> finally(F&& cleanup_fn);           // Cleanup
    // NOTE: Callback overwrite protection allows overwriting propagation callbacks
    // set by methods like recover() and finally().

    template<typename SuccessF, typename ErrorF>
    AsyncOp<T> filter(SuccessF&& successFilter, ErrorF&& errorFilter);
    // Dual-path filtering for success and error handling
    // Success filter: return T to pass, throw ErrorCode to reject
    // Error filter: return T to recover, throw ErrorCode to propagate
    // Pass nullptr for unused filter path (propagates unchanged)

    // Deprecated methods
    [[deprecated]] AsyncOp<T> orElse(T fallback_value, const std::string& log_msg = "");
    // Use otherwise() with explicit fallback logic instead

    template<typename F>
    [[deprecated]] AsyncOp<T> recoverFrom(ErrorCode err, F&& handler);
    // Use filter() with error filter only instead
};

### Chaining Rules and Constraints

When using terminal handlers like `onSuccess()` and `onError()`, be aware of the following constraints:

1. **`.onSuccess()` as terminal handler**: When using `onSuccess()` as a terminal success handler,
   if it's not the last handler in the chain, the next handler must be `recover()` or `otherwise()`.

2. **`.onError()` as terminal handler**: When using `onError()` as a terminal error handler,
   if it's not the last handler in the chain, the next handler must be `then()`.

3. **Callback overwrite protection**: Both `onSuccess()` and `onError()` follow the same 
   overwrite protection rules as other handlers - they can only overwrite previous handlers
   if no handler exists or if the previous handler was a propagation callback.

```cpp
// ✅ CORRECT: onSuccess followed by recover (valid combination)
operation()
    .then([](T val) { return process(val); })
    .onSuccess([](ProcessedVal val) { display(val); })  // Terminal success handler
    .recover([](ErrorCode err) { return getDefault(); }); // Next must be recover/otherwise

// ✅ CORRECT: onError followed by then (valid combination)  
operation()
    .then([](T val) { return process(val); })
    .onError([](ErrorCode err) { logError(err); })      // Terminal error handler
    .then([](T val) { return transform(val); });         // Next must be then

// ❌ INCORRECT: onSuccess followed by then (invalid combination)
operation()
    .then([](T val) { return process(val); })
    .onSuccess([](ProcessedVal val) { display(val); })  // Terminal success handler
    .then([](T val) { return transform(val); });         // ERROR: Should be recover/otherwise
```

### Promise<T> (Type Alias)

```cpp
template<typename T>
using Promise = std::shared_ptr<typename AsyncOp<T>::State>;

// State methods (via promise->)
void resolveWith(T value);          // Settle with success (idempotent)
void rejectWith(ErrorCode err);     // Settle with error (idempotent)

bool isPending() const;
bool isResolved() const;
bool isRejected() const;
bool isSettled() const;
```

### ErrorCode Enum

```cpp
enum class ErrorCode {
    None,                // No error
    Timeout,            // Operation timed out
    NetworkError,       // Network communication failed
    InvalidResponse,    // Response validation failed
    Cancelled,          // Operation was cancelled
    Exception,          // Exception thrown in callback
    MaxRetriesExceeded, // Retry limit reached
    Unknown             // Unspecified error
};

// Automatic formatting via fmt
spdlog::error("Error: {}", err);  // Prints "Error: NetworkError"
```

### SettledResult<T> Struct

```cpp
template<typename T>
struct SettledResult {
    enum Status { Fulfilled, Rejected };
    
    Status status;
    T value;              // Valid if Fulfilled
    ErrorCode error;      // Valid if Rejected
    
    bool isFulfilled() const;
    bool isRejected() const;
};
```

---

## Utility Functions

All utility functions are in the `ao::` namespace.

### Basic Utilities

```cpp
// Delay execution
ao::AsyncOp<void> delay(std::chrono::milliseconds duration);

// Defer synchronous function to async
template<typename F>
auto defer(F&& f) -> AsyncOp<ReturnType>;

// Example:
ao::defer([]() {
    return computeHeavyResult();  // Runs on next event loop iteration
}).then([](Result r) {
    processResult(r);
});
```

### Retry with Backoff

```cpp
template<typename T, typename F>
AsyncOp<T> retryWithBackoff(
    F&& operation,                       // Function returning AsyncOp<T>
    int max_attempts,                    // Max tries (including first)
    std::chrono::milliseconds initial_delay
);

// Example:
ao::retryWithBackoff(
    []() { return fetchFromUnreliableServer(); },
    3,                                   // Try up to 3 times
    std::chrono::seconds(1)              // Start with 1s, then 2s, 4s...
)
.then([](Data d) {
    spdlog::info("Succeeded after retry");
    processData(d);
})
.onError([](ao::ErrorCode err) {
    // err == MaxRetriesExceeded if all failed
    spdlog::error("Failed after all retries: {}", err);
});
```

### Poll Until Condition

```cpp
template<typename T, typename F, typename Pred>
AsyncOp<T> pollUntil(
    F&& operation,                       // Function returning AsyncOp<T>
    Pred&& condition,                    // Predicate: bool(const T&)
    int max_attempts,
    std::chrono::milliseconds interval
);

// Example: Wait for job completion
ao::pollUntil(
    []() { return checkJobStatus(); },   // Returns AsyncOp<JobStatus>
    [](const JobStatus& status) {        // Check condition
        return status.isComplete();
    },
    20,                                  // Poll up to 20 times
    std::chrono::seconds(5)              // Every 5 seconds
)
.then([](JobStatus status) {
    spdlog::info("Job completed: {}", status.result);
})
.onError([](ao::ErrorCode err) {
    spdlog::error("Job didn't complete in time");
});
```

### Event Loop Functions

Located in `ao_event_loop.hpp`:

```cpp
// Schedule callback after timeout
timer_type add_timeout(
    std::chrono::milliseconds timeout,
    std::function<bool()> cb              // Return false to cancel, true to repeat
);

// Schedule callback on next event loop iteration
timer_type add_idle(std::function<bool()> cb);

// Cancel timer
void remove_timeout(timer_type id);

// Thread utilities
bool is_main_thread();                    // Check if on main thread
void invoke_main(std::function<void()> cb);  // Execute on main thread

// Backend info (for debugging)
const char* get_backend_name();           // "GLib" or "Qt"
std::string get_backend_version();        // Version string
```

---

## Thread Safety

### Safe Patterns

#### Worker Thread → Main Thread

```cpp
ao::AsyncOp<Result> computeAsync() {
    ao::AsyncOp<Result> future;
    ao::Promise<Result> promise = future.promise();
    
    // Spawn worker thread
    std::thread worker([promise]() {
        // Heavy computation on worker thread
        Result result = computeExpensiveResult();
        
        // Marshal result to main thread
        ao::invoke_main([promise, result]() {
            promise->resolveWith(result);
        });
    });
    
    worker.detach();  // Or join later
    return future;
}
```

#### Using add_idle for Marshaling

```cpp
std::thread worker([promise]() {
    auto result = compute();
    
    // Schedule on main thread's event loop
    ao::add_idle([promise, result]() {
        promise->resolveWith(result);
        return false;  // G_SOURCE_REMOVE
    });
});
```

### Thread Safety Rules

✅ **Safe from any thread:**
- Creating `AsyncOp` / `Promise`
- Copying `AsyncOp` / `Promise`
- Calling `add_timeout()`, `add_idle()`, `remove_timeout()`
- Calling `invoke_main()`
- Calling `is_main_thread()`

⚠️ **Must be on main thread:**
- Calling `promise->resolveWith()` / `rejectWith()`
- Calling `.then()`, `.onError()`, `.recover()`, etc.
- Any callback execution (happens on main thread automatically)

❌ **Never do from worker thread:**
- Call `promise->resolveWith()` directly (use `invoke_main()` or `add_idle()`)
- Access AsyncOp state queries (`isPending()`, etc.)
- Rely on `.then()` callbacks firing synchronously

### invoke_main() Behavior

**⚠️ IMPORTANT**: `invoke_main()` has different behavior based on calling thread:

```cpp
// From main thread → Executes SYNCHRONOUSLY (immediately)
ao::invoke_main([]() {
    spdlog::info("Runs immediately");
});
spdlog::info("After invoke_main");
// Output: "Runs immediately" then "After invoke_main"

// From worker thread → Executes ASYNCHRONOUSLY (via event loop)
std::thread([]() {
    ao::invoke_main([]() {
        spdlog::info("Runs on main thread later");
    });
    spdlog::info("After invoke_main");
}).join();
// Output: "After invoke_main" then "Runs on main thread later"
```

For **consistent async behavior**, use `add_idle()` directly:

```cpp
ao::add_idle([]() {
    spdlog::info("Always async, always on main thread");
    return false;
});
```

---

## Best Practices

### 1. Always Capture Promise, Never Future

```cpp
// ✅ CORRECT
ao::AsyncOp<int> fetchData() {
    ao::AsyncOp<int> future;
    ao::Promise<int> promise = future.promise();  // Get promise
    
    ao::add_timeout(100ms, [promise]() {          // Capture promise
        promise->resolveWith(42);
        return false;
    });
    
    return future;  // Return future
}

// ❌ WRONG
ao::AsyncOp<int> fetchData() {
    ao::AsyncOp<int> future;
    
    ao::add_timeout(100ms, [future]() {           // ❌ Capturing future!
        // How do we settle it?
        future.???  // No resolveWith() method on future
        return false;
    });
    
    return future;
}
```

### 2. Use Descriptive Error Codes

```cpp
// ✅ GOOD
if (response.status != 200) {
    promise->rejectWith(ao::ErrorCode::InvalidResponse);
} else if (timeout_occurred) {
    promise->rejectWith(ao::ErrorCode::Timeout);
}

// ❌ BAD
promise->rejectWith(ao::ErrorCode::Unknown);  // Not helpful!
```

### 3. Add Timeouts to Network Operations

```cpp
// ✅ GOOD
fetchFromServer()
    .timeout(std::chrono::seconds(30))
    .then([](Data d) { processData(d); })
    .onError([](ao::ErrorCode err) {
        if (err == ao::ErrorCode::Timeout) {
            spdlog::error("Server timeout");
        }
    });

// ❌ RISKY - No timeout, might hang forever
fetchFromServer()
    .then([](Data d) { processData(d); });
```

### 4. Use finally() for Resource Cleanup

```cpp
// ✅ GOOD
auto file = openFile("data.txt");
readFileAsync(file)
    .finally([file]() {
        closeFile(file);  // Always closes
    })
    .then([](Data d) { processData(d); })
    .onError([](ao::ErrorCode err) {
        // File already closed
    });

// ❌ RISKY - File might not be closed on error
readFileAsync(file)
    .then([file](Data d) {
        closeFile(file);  // Only closes on success!
        processData(d);
    });
```

### 5. Move Large Objects

```cpp
// ✅ GOOD - Efficient
promise->resolveWith(std::move(large_data));

.then([](Data d) {  // d moved in
    return processData(std::move(d));  // Move again
})

// ❌ BAD - Unnecessary copies
promise->resolveWith(large_data);  // Copy
.then([](Data d) {  // Another copy
    return processData(d);  // Yet another copy
})
```

### 6. Use tap() for Side Effects

```cpp
// ✅ GOOD
fetchData()
    .tap([](const Data& d) {
        spdlog::info("Got data: {}", d.size);  // Log
        recordMetric("fetch_success");          // Metric
    })
    .then([](Data d) { return processData(d); });

// ❌ WASTEFUL - Extra then() in chain
fetchData()
    .then([](Data d) {
        spdlog::info("Got data: {}", d.size);
        return d;  // Have to return it
    })
    .then([](Data d) { return processData(d); });
```

### 7. Choose Right Collection Operation

```cpp
// Independent operations? → Parallel
ao::mapParallel(users, fetchUserData);     // ✅ Fast

// Dependent or rate-limited? → Sequential  
ao::map(users, fetchUserData);             // ✅ Controlled

// Want to retry failures? → Settled
ao::forEachSettled(items, processItem)     // ✅ Resilient
    .then([](auto failed) {
        if (!failed.empty()) retryItems(failed);
    });
```

### 8. Use Semantic Method Names

```cpp
// For error recovery
parseConfig()
    .recover([](ao::ErrorCode) {
        return getDefaultConfig();  // ✅ "recover" = fixing error
    });

// For alternative paths
fetchFromCache()
    .otherwise([](ao::ErrorCode) {
        return fetchFromDB();       // ✅ "otherwise" = alternative
    });

// Both are identical, use whichever reads better!
```

---

## Troubleshooting

### Problem: Callback Never Fires

**Symptoms:**
- `.then()` or `.onError()` never executes
- Operation seems to hang

**Common Causes:**

1. **Event loop not running**
```cpp
// ✅ Ensure event loop is running
g_main_loop_run(loop);  // GLib
app.exec();             // Qt
```

2. **AsyncOp destroyed too early**
```cpp
// ❌ BAD - op destroyed before callback fires
void myFunc() {
    auto op = fetchData();
    op.then([](Data d) { /* never called */ });
}  // op destroyed here!

// ✅ GOOD - Keep op alive
class MyClass {
    ao::AsyncOp<Data> m_operation;
    
    void myFunc() {
        m_operation = fetchData();
        m_operation.then([](Data d) { /* will be called */ });
    }
};
```

3. **Forgot to settle the promise**
```cpp
// ❌ BAD - Promise never resolved
ao::AsyncOp<int> fetchData() {
    ao::AsyncOp<int> future;
    ao::Promise<int> promise = future.promise();
    
    ao::add_timeout(100ms, [promise]() {
        // Forgot to resolve!
        return false;
    });
    
    return future;  // Will never settle
}

// ✅ GOOD - Always resolve or reject
ao::add_timeout(100ms, [promise]() {
    promise->resolveWith(42);  // or rejectWith()
    return false;
});
```

### Problem: Segmentation Fault

**Common Causes:**

1. **Capturing wrong thing in callback**
```cpp
// ❌ BAD - Capturing future instead of promise
ao::AsyncOp<int> op;
ao::add_timeout(100ms, [op]() {  // ❌ Wrong!
    // op has no resolveWith() method!
});

// ✅ GOOD - Capture promise
ao::add_timeout(100ms, [promise = op.promise()]() {
    promise->resolveWith(42);
});
```

2. **Accessing state from worker thread**
```cpp
// ❌ BAD - Direct access from worker
std::thread([promise]() {
    auto result = compute();
    promise->resolveWith(result);  // ❌ Crash!
}).detach();

// ✅ GOOD - Marshal to main thread
std::thread([promise]() {
    auto result = compute();
    ao::invoke_main([promise, result]() {
        promise->resolveWith(result);  // ✅ Safe
    });
}).detach();
```

3. **Dangling references**
```cpp
// ❌ BAD - Reference to local variable
void processData() {
    Data local_data = loadData();
    
    ao::delay(1s).then([&local_data]() {  // ❌ Dangling reference!
        useData(local_data);
    });
}  // local_data destroyed here

// ✅ GOOD - Capture by value or shared_ptr
void processData() {
    auto data = std::make_shared<Data>(loadData());
    
    ao::delay(1s).then([data]() {  // ✅ Safe
        useData(*data);
    });
}
```

### Problem: Callback Overwrite Prevention

**Symptom:**
- Only the first `.then()` executes (subsequent ones are prevented)
- Error in logs: "cannot overwrite then() callback", "cannot overwrite success handler", or "cannot overwrite error handler"

**Cause:**
AsyncOp now includes callback overwrite protection to prevent accidental overwrites. A callback can only be overwritten if:
- No previous callback exists, OR
- The previous callback was a propagation callback (set by methods like `recover`, `finally`, etc.)

```cpp
// ❌ WRONG - Second then() is prevented from overwriting first
auto op = fetchData();
op.then([](Data d) { processA(d); });  // Prevents overwrites!
op.then([](Data d) { processB(d); });  // This will be prevented and logged

// ✅ CORRECT - Use tap() for side effects
auto op = fetchData();
op.then([](Data d) { processMain(d); })  // Main processing
 .tap([](Data d) { logData(d); });      // Side effect without preventing overwrites
```

**Solutions:**

1. **Chain instead of branching**
```cpp
// ✅ Linear chain
fetchData()
    .tap([](Data d) { processA(d); })   // Side effect
    .then([](Data d) { return processB(d); });  // Main processing
```

2. **Use separate branches for success/error**
```cpp
// ✅ One success branch, one error branch
auto op = fetchData();
op.then([](Data d) { processSuccess(d); });
op.otherwise([](ao::ErrorCode e) { processError(e); });
```

### Problem: Race Condition

**Symptom:**
- Crashes in multi-threaded code
- Inconsistent behavior

**Cause:**
Accessing AsyncOp state from wrong thread.

```cpp
// ❌ BAD - Worker thread accessing state
std::thread([promise]() {
    auto result = compute();
    
    if (some_condition) {
        promise->resolveWith(result);  // ❌ Race!
    } else {
        promise->rejectWith(ao::ErrorCode::InvalidResponse);  // ❌ Race!
    }
}).detach();

// ✅ GOOD - Always marshal to main thread
std::thread([promise]() {
    auto result = compute();
    
    ao::add_idle([promise, result]() {
        if (some_condition) {
            promise->resolveWith(result);  // ✅ Safe
        } else {
            promise->rejectWith(ao::ErrorCode::InvalidResponse);  // ✅ Safe
        }
        return false;
    });
}).detach();
```

### Problem: Memory Leak

**Cause:**
Circular references or improper resource management.

```cpp
// ❌ POTENTIAL LEAK - Timer not cancelled
auto timer = ao::add_timeout(1s, []() {
    return true;  // Repeating timer
});
// Timer runs forever if not cancelled!

// ✅ GOOD - Use single-shot or cancel
auto timer = ao::add_timeout(1s, [promise]() {
    promise->resolveWith(data);
    return false;  // Single-shot
});

// Or cancel explicitly
ao::remove_timeout(timer);
```

---

## Performance Tips

### 1. Parallel vs Sequential

```cpp
// ✅ FAST - Independent operations in parallel
ao::mapParallel(users, [](User u) {
    return fetchUserData(u);  // All fetch simultaneously
});

// ⏱️ SLOW - Sequential (but necessary if rate-limited)
ao::map(users, [](User u) {
    return fetchUserData(u);  // One at a time
});
```

**Speedup example**: Fetching 100 users
- Sequential: 100 requests × 100ms = 10 seconds
- Parallel: 100ms (all at once)

### 2. Use Timeouts

```cpp
// ✅ GOOD - Prevents hung operations
fetchData()
    .timeout(5s)
    .then([](Data d) { processData(d); });

// ❌ RISKY - Might wait forever
fetchData()
    .then([](Data d) { processData(d); });
```

### 3. Move Large Objects

```cpp
struct LargeData {
    std::vector<uint8_t> buffer;  // 10MB
    // ...
};

// ✅ EFFICIENT - No copies
promise->resolveWith(std::move(large_data));

.then([](LargeData d) {  // Move construction
    return processData(std::move(d));  // Move again
})

// ❌ WASTEFUL - 3 copies of 10MB!
promise->resolveWith(large_data);  // Copy 1

.then([](LargeData d) {  // Copy 2
    return processData(d);  // Copy 3
})
```

### 4. Use tap() Not Extra then()

```cpp
// ✅ EFFICIENT - One allocation
fetchData()
    .tap([](const Data& d) {
        spdlog::info("Size: {}", d.size);
    })
    .then([](Data d) { return processData(d); });

// ❌ WASTEFUL - Two allocations
fetchData()
    .then([](Data d) {
        spdlog::info("Size: {}", d.size);
        return d;
    })
    .then([](Data d) { return processData(d); });
```

### 5. Retry with Backoff

```cpp
// ✅ GOOD - Exponential backoff
ao::retryWithBackoff(
    []() { return unreliableOperation(); },
    3,     // Max 3 attempts
    1s     // 1s, 2s, 4s delays
)

// ❌ BAD - Hammering server with immediate retries
for (int i = 0; i < 3; ++i) {
    unreliableOperation();  // No delay!
}
```

### 6. Batch with Settled Operations

```cpp
// ✅ RESILIENT - Process all, handle failures
ao::forEachSettled(items, [](Item item) {
    return processItem(item);
})
.then([](std::vector<Item> failed) {
    if (!failed.empty()) {
        spdlog::warn("{} items failed, requeueing", failed.size());
        requeue(failed);
    }
});

// ❌ FRAGILE - Stops on first error
ao::forEach(items, [](Item item) {
    return processItem(item);  // One error = whole batch fails
});
```

### 7. Choose Right Race Semantic

```cpp
// ✅ For timeout implementation
ao::race({
    fetchData(),
    ao::delay(5s).then([]() { throw ao::ErrorCode::Timeout; })
});

// ✅ For fallback sources
ao::any({
    fetchFromPrimary(),
    fetchFromSecondary(),
    fetchFromCache()
});  // First success wins, fail only if all fail
```

### 8. Worker Threads for Heavy Computation

```cpp
// ✅ GOOD - Keep main thread responsive
ao::AsyncOp<Result> computeAsync() {
    ao::AsyncOp<Result> future;
    ao::Promise<Result> promise = future.promise();
    
    std::thread([promise]() {
        auto result = heavyComputation();  // 5 seconds
        ao::invoke_main([promise, result]() {
            promise->resolveWith(result);
        });
    }).detach();
    
    return future;
}

// ❌ BAD - Blocks main thread
ao::AsyncOp<Result> computeAsync() {
    ao::AsyncOp<Result> future;
    auto result = heavyComputation();  // UI freezes for 5 seconds!
    return ao::AsyncOp<Result>::resolved(result);
}
```

### Performance Comparison Table

| Operation | Sequential | Parallel | Speedup |
|-----------|-----------|----------|---------|
| 10 independent 100ms tasks | 1.0s | 0.1s | **10×** |
| 100 independent 100ms tasks | 10.0s | 0.1s | **100×** |
| 10 dependent tasks | 1.0s | N/A | — |

**Rule of thumb:** Use parallel operations unless:
- Tasks are dependent on each other
- Rate limiting required
- Server can't handle parallel load

---

## What's New in v2.3

### Major Changes

**✨ New onSuccess() Function**
- Added `onSuccess()` terminal handler for success cases (similar to `onError()` but for success)
- Provides symmetry for terminal success handling without continuing the chain
- Follows same callback overwrite protection as other handlers
- Includes proper chaining constraints documentation

**🔒 Enhanced Callback Protection**
- Improved callback overwrite protection mechanism to prevent accidental overwrites
- More informative error messages when callback overwrites are prevented
- Consistent overwrite protection across all handler types (then, onSuccess, onError, etc.)

**📝 Documentation Improvements**
- Comprehensive documentation for the new `onSuccess()` function
- Added chaining rules and constraints section
- Updated examples and API references to include the new functionality
- Improved project structure and build configuration documentation

**⚙️ Build System Updates**
- Enhanced CMake configuration to better support both Qt5 and GLib backends
- Better handling of optional Qt5 dependencies

---

## Project Structure and Build Configuration


Understanding the project structure and build configuration will help you navigate and extend the AsyncOp library.

### 1. Repository Structure

```
asyncop/
├── README.md                    # Overview, quick start, badges
├── LICENSE                      # MIT license
├── CMakeLists.txt              # Build configuration
├── .gitignore                  # Ignore build/, *.o, etc.
├── .clang-format               # Code formatting rules
├── CHANGELOG.md                # Version history
├── CONTRIBUTING.md             # Contribution guidelines
│
├── include/
│   ├── async_op.hpp            # Main header for AsyncOp<T>
│   └── async_op_void.hpp       # Specialization for AsyncOp<void>
│
├── docs/
│   └── async_op_doc.md         # Comprehensive documentation
│
├── tests/
│   └── ...                     # Unit tests
│
└── examples/
    └── CMakeLists.txt          # Example build configuration
```

### 2. Build Configuration

The library uses CMake for build configuration with the following features:

#### CMakeLists.txt
```cmake
cmake_minimum_required(VERSION 3.10)
project(AsyncOp VERSION 2.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(BUILD_TESTS "Build tests" ON)

# Library (header-only)
add_library(asyncop INTERFACE)
target_include_directories(asyncop INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Find dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
find_package(spdlog REQUIRED)

# Try to find Qt5 (optional)
find_package(Qt5 COMPONENTS Core Widgets Network QUIET)
if(Qt5_FOUND)
    set(HAVE_QT5 TRUE)
    message(STATUS "Qt5 found - building Qt support")
else()
    set(HAVE_QT5 FALSE)
    message(WARNING "Qt5 not found - building without Qt support")
endif()

# Link dependencies
if(HAVE_QT5)
    target_link_libraries(asyncop INTERFACE ${GLIB_LIBRARIES} Qt5::Core Qt5::Widgets Qt5::Network spdlog::spdlog)
else()
    target_link_libraries(asyncop INTERFACE ${GLIB_LIBRARIES} spdlog::spdlog)
endif()
target_include_directories(asyncop SYSTEM INTERFACE ${GLIB_INCLUDE_DIRS})
target_compile_options(asyncop INTERFACE ${GLIB_CFLAGS_OTHER})

# Options
option(BUILD_EXAMPLES "Build examples" ON)

# Add tests if enabled
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Add examples if enabled
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
```

This configuration supports both GLib and Qt backends, with spdlog for logging and pkg-config for dependency management.

---

## Summary

**AsyncOp v2.3** provides production-ready Promise/Future semantics for embedded Linux:

✅ **Clear Promise/Future model** - `AsyncOp<T>` = Future, `Promise<T>` = Promise
✅ **Powerful chaining** - `.then()`, `.recover()`, `.next()`, `.otherwise()`
✅ **Error handling** - Multiple patterns for resilient async workflows
✅ **Collection operations** - Sequential and parallel processing
✅ **Thread-safe** - Worker thread integration via `invoke_main()`
✅ **Type-safe** - Compile-time checking, no runtime surprises
✅ **Embedded-friendly** - Minimal overhead, designed for 64MB+ systems
✅ **Dual backend** - GLib 2.0+ or Qt 5.12+