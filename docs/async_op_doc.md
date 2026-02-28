# AsyncOp Library v2.4.1 - Complete Documentation

> **Modern Promise/Future pattern for embedded Linux**
> 
> Chainable asynchronous operations with comprehensive error handling, designed for embedded systems with GLib or Qt event loops.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Backend](https://img.shields.io/badge/Backend-GLib%20%7C%20Qt-green.svg)](https://github.com/)

---

## Table of Contents

1. [Quick Start](#quick-start)
2. [Core Concepts](#core-concepts)
3. [Promise/Future Pattern](#promisefuture-pattern)
4. [API Reference](#api-reference)
5. [Chaining Operations](#chaining-operations)
6. [Error Handling](#error-handling)
7. [Collection Operations](#collection-operations)
8. [Advanced Patterns](#advanced-patterns)
9. [Thread Safety](#thread-safety)
10. [Best Practices](#best-practices)
11. [Performance Considerations](#performance-considerations)
12. [Integration Guide](#integration-guide)

---

## Quick Start

### Installation

```cpp
#include "async_op.hpp"

// For Qt backend, define ASYNC_USE_QT in compiler options
// Default is GLib backend
```

**Requirements:**
- C++17 or later
- GLib 2.0+ (default) or Qt 5.12+ (with ASYNC_USE_QT defined)
- spdlog (includes fmt for formatting)

### Hello AsyncOp

```cpp
// Simple delay
ao::delay(1000ms)
    .then([]() {
        spdlog::info("1 second has passed!");
    });

// Fetch user and posts
fetchUserAsync(userId)
    .then([](User user) {
        return fetchPostsAsync(user.id);
    })
    .then([](std::vector<Post> posts) {
        displayPosts(posts);
    })
    .onError([](ao::ErrorCode err) {
        spdlog::error("Failed: {}", err);
    });
```

### Error Recovery

```cpp
fetchFromCache()
    .recover([](ao::ErrorCode err) {
        spdlog::warn("Cache miss, fetching from DB");
        return fetchFromDatabase();
    })
    .then([](Data data) {
        processData(data);
    });
```

---

## Core Concepts

### What is AsyncOp?

`AsyncOp<T>` represents an asynchronous operation that will eventually:
- **Resolve** with a value of type `T` (success)
- **Reject** with an `ErrorCode` (failure)

It's a "container for a future value" that you can chain operations on.

### The Promise/Future Model

AsyncOp uses the classic Promise/Future separation:

```
┌─────────────────┐                    ┌──────────────────┐
│  AsyncOp<T>     │                    │   Promise<T>     │
│  (Future)       │                    │   (Promise)      │
├─────────────────┤                    ├──────────────────┤
│ Read-only       │                    │ Write-side       │
│ Consumer side   │◄───────shares──────│ Producer side    │
│                 │      State         │                  │
│ .then()         │                    │ .resolveWith()   │
│ .recover()      │                    │ .rejectWith()    │
│ .onError()      │                    │ .isPending()     │
└─────────────────┘                    └──────────────────┘
```

**Key Points:**
- **AsyncOp**: Read-only "Future" handle returned from async functions
- **Promise**: `std::shared_ptr<State>` used to complete the operation
- **Shared State**: Both reference the same underlying state object
- **Terminology**: `AsyncOp` acts as the Future, `Promise` is the write-side (shared_ptr<State>)

### ErrorCode Enum

```cpp
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
}
```

ErrorCode has built-in fmt/spdlog formatting:
```cpp
spdlog::error("Operation failed: {}", error_code);  // Prints "NetworkError", not "2"
```

---

## Promise/Future Pattern

### Creating Async Operations

**Method 1: Get promise from future**
```cpp
ao::AsyncOp<int> fetchDataAsync() {
    ao::AsyncOp<int> future;
    
    // Get the promise for settling
    ao::Promise<int> promise = future.promise();
    
    // Schedule async work - capture promise
    ao::add_timeout(100ms, [promise]() {
        promise->resolveWith(42);
        return false;  // Single-shot timer
    });
    
    return future;  // Return the future to caller
}
```

**Method 2: Create promise first**
```cpp
ao::AsyncOp<int> fetchDataAsync() {
    auto promise = ao::makePromise<int>();
    
    // Schedule async work
    ao::add_timeout(100ms, [promise]() {
        promise->resolveWith(42);
        return false;
    });
    
    return ao::AsyncOp<int>(promise);  // Construct from promise
}
```

Both methods are equivalent - use whichever is clearer.

### ⚠️ Critical Rule: Capture Promise, Not Future

```cpp
// ✅ CORRECT - Capture promise
ao::AsyncOp<int> future;
ao::add_timeout(100ms, [promise = future.promise()]() {
    promise->resolveWith(42);
    return false;
});

// ❌ WRONG - Don't capture future
ao::AsyncOp<int> future;
ao::add_timeout(100ms, [future]() {  // ❌ Can't settle it!
    // future is read-only, no way to complete the operation
    return false;
});
```

**Why?** The `AsyncOp` (future) is read-only. Only the `Promise` has methods to settle the operation.

### Promise API

```cpp
// State management
promise->resolveWith(T value);           // Complete successfully
promise->rejectWith(ErrorCode error);    // Complete with error

// State queries
bool isPending();                        // Not yet settled
bool isResolved();                       // Completed successfully
bool isRejected();                       // Completed with error
bool isSettled();                        // Either resolved or rejected

// Callback management (advanced)
bool canOverwriteSuccessCallback();      // Check if safe to set success handler
bool canOverwriteErrorCallback();        // Check if safe to set error handler
```

**Properties:**
- **Idempotent**: Calling `resolveWith/rejectWith` multiple times is safe (first call wins)
- **Thread-safe creation**: Can create on any thread
- **Settlement**: Must settle on main event loop thread (via `invoke_main` if needed)

### Factory Methods

```cpp
// Already resolved
auto success = ao::AsyncOp<int>::resolved(42);

// Already rejected
auto failure = ao::AsyncOp<int>::rejected(ao::ErrorCode::NetworkError);
```

**Use cases:**
- Testing async code with synchronous values
- Returning cached results from async APIs
- Short-circuiting chains based on conditions

---

## API Reference

### Chaining Methods

#### `.then(f)` - Transform Success Values

Transforms successful results to new values. Errors auto-propagate unchanged.

```cpp
template<typename F>
auto then(F&& f) -> AsyncOp<U>;  // Where F: (T -> U) or (T -> AsyncOp<U>)
```

**Returns:** New `AsyncOp<U>` with transformed type
**Propagates:** Errors unchanged (auto-propagation)
**Example:**
```cpp
fetchValue()
    .then([](int x) { return x * 2; })           // int -> int
    .then([](int x) { return std::to_string(x); })  // int -> string
    .then([](std::string s) { 
        spdlog::info("Result: {}", s); 
    });
```

**Async chaining** (auto-unwraps nested AsyncOp):
```cpp
fetchUserId()
    .then([](int id) -> ao::AsyncOp<User> {
        return fetchUserAsync(id);  // Returns AsyncOp<User>
    })
    .then([](User user) {  // Automatically unwrapped!
        displayUser(user);
    });
```

#### `.recover(f)` - Convert Error to Success

Catches errors and provides fallback values. Success values auto-propagate unchanged.

```cpp
template<typename F>
auto recover(F&& f) -> AsyncOp<T>;  // Where F: (ErrorCode -> T) or (ErrorCode -> AsyncOp<T>)
```

**Returns:** New `AsyncOp<T>` (same type)
**Propagates:** Success values unchanged (auto-propagation)
**Handler returns:**
- `T` or `AsyncOp<T>`: Recovery succeeds, becomes success
- `throw ErrorCode`: Propagate error (same or different)

**Example:**
```cpp
fetchFromCache()
    .recover([](ErrorCode err) {
        if (err == ErrorCode::Timeout) {
            return getCachedData();  // Recover to success
        }
        throw err;  // Propagate other errors
    })
    .then([](Data d) {  // Runs for both cache hit and recovery
        processData(d);
    });
```

#### `.otherwise(f)` - Branching Semantics

Semantic alias for `.recover()` - identical implementation, different intent.

```cpp
template<typename F>
auto otherwise(F&& f) -> AsyncOp<T>;  // Alias for recover()
```

**Use `.recover()`** when: Fixing an error to continue on same path
**Use `.otherwise()`** when: Taking alternative path on error

**Branching pattern:**
```cpp
auto op = fetchFromCache(key);

// Success branch
op.then([](Data data) { return processData(data); })
  .then([](Result r) { displayResult(r); });

// Error branch (alternative path)
op.otherwise([](ErrorCode err) { return fetchFromDatabase(key); })
  .then([](Data data) { return validateData(data); })
  .then([](Data data) { cacheData(data); });

// Only ONE branch executes
```

#### `.next(success_fn, error_fn)` - Dual-Path Convergence

Handles both success and error with different logic, converging to same result type.

```cpp
template<typename SuccessF, typename ErrorF>
auto next(SuccessF&& success_fn, ErrorF&& error_fn) -> AsyncOp<U>;
// Where: (T -> U) and (ErrorCode -> U) must return same type U
```

**Key concept:** The "next step" regardless of previous result

**Returns:** New `AsyncOp<U>` 
**Requirement:** Both handlers must return same type `U`

**Example:**
```cpp
fetchUser(id)
    .next(
        // Success path
        [](User user) {
            spdlog::info("Found user: {}", user.name);
            return user;
        },
        // Error path
        [](ErrorCode err) {
            spdlog::warn("User not found: {}, using default", err);
            return User::defaultUser();
        }
    )
    .then([](User user) {
        // Always executes with a User (from either path)
        displayUser(user);
    });
```

**When to use:**
- Need guaranteed result regardless of success/failure
- Different processing, same output type
- Cleaner than `.then().recover()` when types align

### Terminal Handlers

#### `.onSuccess(f)` - Terminal Success Handler

Sets final success handler without creating new AsyncOp. **Terminates the chain.**

```cpp
AsyncOp<T>& onSuccess(std::function<void(T)> handler);
```

**Returns:** Reference to `*this` (for chaining with `.onError()`)
**Memory optimization:** No unused AsyncOp allocation

**Example:**
```cpp
fetchData()
    .then(processData)
    .onSuccess([](Result r) {
        // Terminal handler - efficient, no unused AsyncOp
        displayResult(r);
    })
    .onError([](ErrorCode err) {
        handleError(err);
    });
```

**Why use onSuccess() over then()?**
```cpp
// Less efficient: creates unused AsyncOp
fetchData().then(process).then([](Result r) { display(r); });
//                              ^^^ Returns AsyncOp that's never used

// More efficient: no unused allocation
fetchData().then(process).onSuccess([](Result r) { display(r); });
//                                   ^^^ No AsyncOp created
```

#### `.onError(f)` - Terminal Error Handler

Sets final error handler without creating new AsyncOp. **Terminates the chain.**

```cpp
AsyncOp<T>& onError(std::function<void(ErrorCode)> handler);
```

**Returns:** Reference to `*this`
**Can be used:**
1. As end-of-chain: `.then().onError()`
2. Before then(): `.onError().then()` (sets error handler, then continues)

**Example:**
```cpp
// Pattern 1: End of chain
fetchData()
    .then(process)
    .onSuccess([](Result r) { display(r); })
    .onError([](ErrorCode e) { logError(e); });

// Pattern 2: Set error handler mid-chain
fetchData()
    .onError([](ErrorCode e) { logError(e); })
    .then([](Data d) { return process(d); });  // Success path continues
```

### Filtering Methods

#### `.filter(success_fn, error_fn)` - Dual-Path Filtering

Validates/transforms both success and error paths using throw/return semantics.

```cpp
template<typename SuccessF, typename ErrorF>
auto filter(SuccessF&& success_fn, ErrorF&& error_fn) -> AsyncOp<T>;
```

**Throw/Return semantics:**
- **Success filter:** Return `T` to pass, throw `ErrorCode` to reject
- **Error filter:** Return `T` to recover, throw `ErrorCode` to propagate

**Example:**
```cpp
fetchUser(id).filter(
    // Success filter: validate
    [](User user) -> User {
        if (!user.isValid()) {
            throw ErrorCode::InvalidResponse;
        }
        return user;  // Validated
    },
    // Error filter: recover from timeout
    [](ErrorCode err) -> User {
        if (err == ErrorCode::Timeout) {
            return getCachedUser();  // Convert to success
        }
        throw err;  // Propagate other errors
    }
);
```

#### `.filterSuccess(f)` - Success-Only Filtering

Convenience wrapper when only success needs filtering. Errors propagate unchanged.

```cpp
template<typename SuccessF>
auto filterSuccess(SuccessF&& success_fn) -> AsyncOp<T>;
```

**Example:**
```cpp
fetchData()
    .filterSuccess([](Data data) -> Data {
        if (data.size() == 0) {
            throw ErrorCode::InvalidResponse;
        }
        return data.normalize();  // Transform and pass through
    });
```

#### `.filterError(f)` - Error-Only Filtering

Convenience wrapper when only errors need handling. Success values propagate unchanged.

```cpp
template<typename ErrorF>
auto filterError(ErrorF&& error_fn) -> AsyncOp<T>;
```

**Example:**
```cpp
operation()
    .filterError([](ErrorCode err) -> Data {
        if (err == ErrorCode::Timeout) {
            return cachedData;  // Recover
        }
        spdlog::error("Error: {}", err);
        throw err;  // Propagate after logging
    });
```

### Side Effect Methods

#### `.tap(f)` - Success Side Effect

Execute side effect without modifying the value. Value passes through unchanged.

```cpp
template<typename F>
AsyncOp<T> tap(F&& side_effect_fn);  // Where F: (T -> void) or (const T& -> void)
```

**Properties:**
- Value passes through unchanged
- Exceptions caught and logged (don't break chain)
- Use for logging, metrics, debugging

**Example:**
```cpp
fetchUser(id)
    .tap([](const User& u) {
        spdlog::debug("Fetched user: {}, age: {}", u.name, u.age);
        metrics.increment("users_fetched");
    })
    .then([](User u) { return processUser(u); });
```

#### `.tapError(f)` - Error Side Effect

Execute side effect on error without modifying it. Error passes through unchanged.

```cpp
template<typename F>
AsyncOp<T> tapError(F&& side_effect_fn);  // Where F: (ErrorCode -> void)
```

**Properties:**
- Error passes through unchanged
- Exceptions caught and logged
- Use for error logging, metrics

**Example:**
```cpp
operation()
    .tapError([](ErrorCode err) {
        spdlog::error("Operation failed: {}", err);
        metrics.increment("operation_errors");
    })
    .recover([](ErrorCode err) {
        return getDefaultValue();
    });
```

### Cleanup & Utility Methods

#### `.finally(f)` - Cleanup Regardless of Outcome

Executes cleanup function whether operation succeeds or fails. Result preserved.

```cpp
template<typename F>
AsyncOp<T> finally(F&& cleanup_fn);  // Where F: (void -> void)
```

**Properties:**
- Executes on both success and error
- Original result/error preserved and propagated
- Cleanup executes exactly once
- Exceptions in cleanup caught and logged

**Example:**
```cpp
openFile(path)
    .then([](File f) { return readData(f); })
    .finally([file]() {
        file.close();
        spdlog::debug("File closed");
    })
    .then([](Data d) { processData(d); });
```

#### `.timeout(duration)` - Add Timeout

Creates new AsyncOp that rejects with `ErrorCode::Timeout` if duration elapses.

```cpp
AsyncOp<T> timeout(std::chrono::milliseconds duration);
```

**⚠️ CRITICAL:** Timer starts **immediately** when `.timeout()` is called

**Example:**
```cpp
// Timeout applies to BOTH op1 and op2 (timer starts immediately)
op1().then([](){ return op2(); }).timeout(3000ms);

// Timeout applies only to op2 (timer starts when op2 begins)
op1().then([](){ return op2().timeout(3000ms); });
```

**Timeline:**
```
// Case 1: .timeout() after chain
op1().then(op2).timeout(3000ms)
|-- op1 starts
|   timeout() called, timer starts ⏱️
|--- op1 completes (500ms)
|       op2 starts
|------- op2 completes (800ms total) ✓

// Case 2: .timeout() on inner op
op1().then([]{ return op2().timeout(3000ms); })
|-- op1 starts (no timer)
|--- op1 completes (could be 10 seconds)
|       op2().timeout() called, timer starts ⏱️
|--------- op2 completes (500ms) ✓
```

#### `.cancel(code)` - Cancel Operation

Rejects pending operation with specified error code. No-op if already settled.

```cpp
AsyncOp<T>& cancel(ErrorCode code = ErrorCode::Cancelled);
```

**⚠️ Important:** Only cancels AsyncOp state machine, not underlying operation.

**Example:**
```cpp
auto op = fetchLargeFile(url);

onCancelButtonClicked([op]() {
    op.cancel();  // Rejects with ErrorCode::Cancelled
});

op.then([](File f) { processFile(f); })
  .onError([](ErrorCode err) {
      if (err == ErrorCode::Cancelled) {
          spdlog::info("User cancelled download");
      }
  });
```

### State Query Methods

```cpp
bool isPending() const;      // Not yet settled
bool isResolved() const;     // Completed successfully
bool isRejected() const;     // Completed with error
bool isSettled() const;      // Either resolved or rejected
ErrorCode errorCode() const; // Get error (if rejected)
id_type id() const;          // Get unique operation ID (for logging)
```

---

## Chaining Operations

### Sequential Chaining

```cpp
fetchUserId()
    .then([](int id) { return fetchUser(id); })
    .then([](User u) { return fetchPosts(u.id); })
    .then([](Posts p) { displayPosts(p); })
    .onError([](ErrorCode e) { handleError(e); });

// Type flow: AsyncOp<int> -> AsyncOp<User> -> AsyncOp<Posts> -> void
```

### Branching Pattern

Create independent success and error branches from same operation:

```cpp
auto op = fetchFromCache(key);

// Success branch
op.then([](Data data) { return processForUI(data); })
  .then([](UIData ui) { displayUI(ui); });

// Error branch (alternative path)
op.otherwise([](ErrorCode err) { return fetchFromDB(key); })
  .then([](Data data) { return processForCache(data); })
  .then([](Data data) { updateCache(data); });

// Only ONE branch executes (mutually exclusive)
```

### Mixed Chaining

```cpp
operation()
    .tap([](Result r) { log Debug("Got: {}", r); })      // Side effect
    .then([](Result r) { return process(r); })         // Transform
    .filterSuccess([](Data d) -> Data {                // Validate
        if (!d.isValid()) throw ErrorCode::InvalidResponse;
        return d;
    })
    .timeout(5000ms)                                   // Add timeout
    .recover([](ErrorCode e) { return getDefault(); }) // Fallback
    .finally([]() { cleanup(); })                      // Cleanup
    .then([](Data d) { display(d); });                // Final action
```

---

## Error Handling

### Basic Error Handling

```cpp
operation()
    .then([](Result r) { return process(r); })
    .onError([](ErrorCode err) {
        spdlog::error("Failed: {}", err);
    });
```

### Error Recovery

```cpp
fetchFromServer()
    .recover([](ErrorCode err) {
        if (err == ErrorCode::Timeout) {
            return getCachedData();
        }
        throw err;  // Propagate other errors
    })
    .then([](Data d) { processData(d); });
```

### Conditional Error Handling

```cpp
operation()
    .filterError([](ErrorCode err) -> Data {
        switch (err) {
            case ErrorCode::Timeout:
                return cachedData;
            case ErrorCode::NetworkError:
                spdlog::warn("Network error, retrying...");
                throw err;  // Will be caught by next handler
            default:
                throw err;
        }
    })
    .recover([](ErrorCode err) {
        return Data::defaultValue();
    });
```

### Error Transformation

```cpp
operation()
    .recover([](ErrorCode err) -> ao::AsyncOp<Data> {
        // Transform error into new async operation
        if (err == ErrorCode::NotFound) {
            return createNewData();  // Returns AsyncOp<Data>
        }
        throw err;
    });
```

### Error Propagation

Errors automatically propagate through `.then()` chains:

```cpp
step1()
    .then([](A a) { return step2(a); })  // Error bypasses
    .then([](B b) { return step3(b); })  // Error bypasses
    .then([](C c) { return step4(c); })  // Error bypasses
    .onError([](ErrorCode err) {
        // Catches errors from any step
        spdlog::error("Chain failed: {}", err);
    });
```

---

## Collection Operations

### `all()` - Wait for All Success

Resolves when **all** operations succeed, rejects on **first** error.

```cpp
template<typename T>
AsyncOp<std::vector<T>> all(std::vector<AsyncOp<T>> operations);
```

**Example:**
```cpp
std::vector<ao::AsyncOp<Data>> ops = {
    fetchData(1),
    fetchData(2),
    fetchData(3)
};

ao::all(std::move(ops))
    .then([](std::vector<Data> results) {
        // All succeeded - process all results
        for (const auto& data : results) {
            processData(data);
        }
    })
    .onError([](ErrorCode err) {
        // At least one failed
        spdlog::error("Batch failed: {}", err);
    });
```

### `any()` - Race for First Success

Resolves with **first success**, rejects only if **all** fail.

```cpp
template<typename T>
AsyncOp<T> any(std::vector<AsyncOp<T>> operations);
```

**Example:**
```cpp
ao::any({
    fetchFromServer1(),
    fetchFromServer2(),
    fetchFromServer3()
})
    .then([](Data data) {
        // Got data from fastest server
        processData(data);
    })
    .onError([](ErrorCode err) {
        // All servers failed
        spdlog::error("All servers failed");
    });
```

### `race()` - First to Settle (Success or Error)

Completes as soon as **any** operation settles (success OR failure).

```cpp
template<typename T>
AsyncOp<T> race(std::vector<AsyncOp<T>> operations);
```

**Difference from `any()`:**
- `race()`: First to finish (success or error) wins
- `any()`: First success wins, only fails if all fail

**Example:**
```cpp
ao::race({
    fetchWithTimeout(url, 1000ms),
    ao::delay(5000ms).then([]() -> Data {
        throw ErrorCode::Timeout;
    })
})
    .then([](Data data) { processData(data); })
    .onError([](ErrorCode err) { handleTimeout(); });
```

### `allSettled()` - Wait for All (Success and Failures)

Waits for **all** operations to complete, returns results for both successes and failures.

```cpp
template<typename T>
struct SettledResult {
    enum Status { Fulfilled, Rejected };
    T value;           // Valid if Fulfilled
    ErrorCode error;   // Valid if Rejected
    Status status;
};

template<typename T>
AsyncOp<std::vector<SettledResult<T>>> allSettled(std::vector<AsyncOp<T>> operations);
```

**Example:**
```cpp
ao::allSettled({
    fetchData(1),
    fetchData(2),
    fetchData(3)
})
    .then([](std::vector<ao::SettledResult<Data>> results) {
        for (size_t i = 0; i < results.size(); ++i) {
            if (results[i].isFulfilled()) {
                spdlog::info("Item {}: success", i);
                processData(results[i].value);
            } else {
                spdlog::warn("Item {}: failed with {}", 
                             i, results[i].error);
            }
        }
    });
```

### `map()` - Sequential Transformation

Processes items **one at a time**, collects results. Fails on first error.

```cpp
template<typename T, typename F>
auto map(const std::vector<T>& items, F&& transform) 
    -> AsyncOp<std::vector<U>>;  // Where F: (T -> AsyncOp<U>)
```

**Example:**
```cpp
std::vector<int> ids = {1, 2, 3, 4, 5};

ao::map(ids, [](int id) {
    return fetchUser(id);  // Returns AsyncOp<User>
})
    .then([](std::vector<User> users) {
        // All users fetched sequentially
        displayUsers(users);
    });
```

### `mapParallel()` - Parallel Transformation

Processes items **simultaneously**, collects results. Fails on first error.

```cpp
template<typename T, typename F>
auto mapParallel(const std::vector<T>& items, F&& transform)
    -> AsyncOp<std::vector<U>>;
```

**Example:**
```cpp
std::vector<int> ids = {1, 2, 3, 4, 5};

ao::mapParallel(ids, [](int id) {
    return fetchUser(id);
})
    .then([](std::vector<User> users) {
        // All users fetched in parallel
        displayUsers(users);
    });
```

### `forEach()` - Sequential Execution

Executes operation for each item **sequentially**. Fails immediately on first error.

```cpp
template<typename T, typename F>
AsyncOp<void> forEach(const std::vector<T>& items, F&& process);
```

**Example:**
```cpp
std::vector<File> files = getFiles();

ao::forEach(files, [](File f) {
    return processFile(f);  // Returns AsyncOp<void>
})
    .then([]() {
        spdlog::info("All files processed");
    });
```

### `forEachSettled()` - Sequential with Failed Items

Processes **all** items sequentially, returns list of failed items.

```cpp
template<typename Item, typename F>
AsyncOp<std::vector<Item>> forEachSettled(const std::vector<Item>& items, F&& process);
```

**Example:**
```cpp
ao::forEachSettled(files, [](File f) {
    return processFile(f);
})
    .then([](std::vector<File> failed_files) {
        if (failed_files.empty()) {
            spdlog::info("All files processed successfully");
        } else {
            spdlog::warn("{} files failed", failed_files.size());
            retryFiles(failed_files);
        }
    });
```

### `mapSettled()` - Sequential with All Results

Processes items **sequentially**, returns `SettledResult` for each (both successes and failures).

```cpp
template<typename Item, typename F>
auto mapSettled(const std::vector<Item>& items, F&& transform)
    -> AsyncOp<std::vector<SettledResult<U>>>;
```

**Example:**
```cpp
ao::mapSettled(urls, [](std::string url) {
    return fetchUrl(url);
})
    .then([](std::vector<ao::SettledResult<Data>> results) {
        for (const auto& result : results) {
            if (result.isFulfilled()) {
                processData(result.value);
            } else {
                spdlog::error("URL failed: {}", result.error);
            }
        }
    });
```

---

## Advanced Patterns

### Retry Pattern

```cpp
template<typename T, typename F>
AsyncOp<T> retry(F&& operation, int max_attempts);

template<typename T, typename F>
AsyncOp<T> retryWithBackoff(F&& operation, int max_attempts, 
                           std::chrono::milliseconds initial_delay);
```

**Immediate retry:**
```cpp
ao::retry<Data>([]() {
    return fetchFromServer();
}, 3)
    .then([](Data d) { processData(d); })
    .onError([](ErrorCode err) {
        if (err == ErrorCode::MaxRetriesExceeded) {
            spdlog::error("All retries failed");
        }
    });
```

**Exponential backoff:**
```cpp
ao::retryWithBackoff<Data>([]() {
    return fetchFromAPI();
}, 3, 1000ms)  // Retry after 1s, 2s, 4s
    .then([](Data d) { processData(d); });
```

### Delay Pattern

```cpp
AsyncOp<void> delay(std::chrono::milliseconds duration);
```

**Example:**
```cpp
ao::delay(1000ms)
    .then([]() {
        spdlog::info("1 second later");
    });

// Delayed retry
operation()
    .recover([](ErrorCode err) -> ao::AsyncOp<Data> {
        return ao::delay(2000ms).then([]() {
            return retryOperation();
        });
    });
```

### Defer Pattern

Execute synchronous function asynchronously on next event loop iteration.

```cpp
template<typename F>
auto defer(F&& f) -> AsyncOp<ReturnType>;
```

**Example:**
```cpp
ao::defer([]() {
    // Heavy computation
    return expensiveCalculation();
})
    .then([](Result r) {
        displayResult(r);
    });
```

### Conditional Execution

```cpp
AsyncOp<Data> fetchData(bool use_cache) {
    if (use_cache) {
        return AsyncOp<Data>::resolved(getCached());
    } else {
        return fetchFromServer();
    }
}
```

### Combining Multiple Sources

```cpp
// Fetch from primary, fallback to secondary, finally use cache
fetchFromPrimary()
    .recover([](ErrorCode) { return fetchFromSecondary(); })
    .recover([](ErrorCode) { return getCachedData(); })
    .then([](Data d) { processData(d); });
```

### Parallel Execution with Dependencies

```cpp
auto user_op = fetchUser(id);
auto settings_op = fetchSettings(id);

ao::all({user_op, settings_op})
    .then([](std::vector<std::any> results) {
        // Both completed - now do something that needs both
        User user = /* extract from results[0] */;
        Settings settings = /* extract from results[1] */;
        return initialize(user, settings);
    });
```

---

## Thread Safety

### Event Loop Threading Model

AsyncOp is designed for **single-threaded event loop** environments (GLib/Qt main loop).

**Thread-safe operations:**
- Creating AsyncOp/Promise on any thread ✅
- Querying state (`isPending()`, etc.) ✅
- `add_timeout()`, `add_idle()`, `invoke_main()` ✅

**Main thread required:**
- Settling promises (`resolveWith()`, `rejectWith()`) ⚠️
- Executing callbacks (`.then()`, `.onError()`, etc.) ⚠️

### Cross-Thread Example

```cpp
ao::AsyncOp<Data> fetchDataFromWorker() {
    auto promise = ao::makePromise<Data>();
    
    std::thread worker([promise]() {
        Data data = expensiveComputation();
        
        // Marshal back to main thread before settling
        ao::invoke_main([promise, data]() {
            promise->resolveWith(data);
        });
    });
    
    worker.detach();
    return ao::AsyncOp<Data>(promise);
}
```

### invoke_main() Behavior

⚠️ **Important:** Behavior differs based on calling thread:

```cpp
void invoke_main(std::function<void()> cb);
```

- **Main thread:** Executes **synchronously** (immediately)
- **Worker thread:** Executes **asynchronously** (via event loop)

For **consistent async behavior**, use `add_idle()` directly:

```cpp
// Always async (even from main thread)
ao::add_idle([promise, data]() {
    promise->resolveWith(data);
    return false;  // Single-shot
});
```

---

## Best Practices

### 1. Always Capture Promise, Not Future

```cpp
// ✅ CORRECT
ao::AsyncOp<int> op;
ao::add_timeout(100ms, [promise = op.promise()]() {
    promise->resolveWith(42);
    return false;
});

// ❌ WRONG
ao::add_timeout(100ms, [op]() {  // Can't settle!
    return false;
});
```

### 2. Use onSuccess()/onError() for Terminal Handlers

```cpp
// ✅ Efficient - no unused AsyncOp
fetchData()
    .then(process)
    .onSuccess([](Result r) { display(r); });

// ❌ Less efficient - creates unused AsyncOp
fetchData()
    .then(process)
    .then([](Result r) { display(r); });  // Returns unused AsyncOp
```

### 3. Chain Operations, Don't Set Multiple Handlers

```cpp
// ✅ CORRECT - Chain operations
fetchData()
    .then(step1)
    .then(step2)
    .then(step3);

// ❌ WRONG - Multiple handlers on same op (only first runs)
auto op = fetchData();
op.then(step1);  // Runs
op.then(step2);  // NEVER RUNS - triggers assertion
```

### 4. Use tap() for Side Effects

```cpp
// ✅ CORRECT - Use tap() for logging
fetchData()
    .tap([](Data d) { spdlog::debug("Got: {}", d); })
    .then([](Data d) { return process(d); });

// ❌ WRONG - Don't log in then()
fetchData()
    .then([](Data d) {
        spdlog::debug("Got: {}", d);
        return d;  // Unnecessary return
    })
    .then([](Data d) { return process(d); });
```

### 5. Use recover() for Error Handling, otherwise() for Branching

```cpp
// ✅ CORRECT - Use recover() for fallback
operation()
    .recover([](ErrorCode e) { return getDefault(); })
    .then([](Data d) { process(d); });

// ✅ CORRECT - Use otherwise() for branching
auto op = fetchData();
op.then(processSuccess);
op.otherwise(processError);  // Alternative path
```

### 6. Add Timeouts to Network Operations

```cpp
// ✅ CORRECT
fetchFromServer()
    .timeout(5000ms)
    .recover([](ErrorCode e) {
        if (e == ErrorCode::Timeout) {
            return getCached();
        }
        throw e;
    });
```

### 7. Use finally() for Cleanup

```cpp
// ✅ CORRECT - Guaranteed cleanup
openResource()
    .then([](Resource r) { return useResource(r); })
    .finally([resource]() {
        resource.close();  // Always runs
    });
```

### 8. Prefer filterSuccess/filterError() Over filter()

```cpp
// ✅ CLEAR - Only validating success
op.filterSuccess([](Data d) -> Data {
    if (!d.isValid()) throw ErrorCode::InvalidResponse;
    return d;
});

// ❌ LESS CLEAR - Using full filter() for one path
op.filter(
    [](Data d) -> Data {
        if (!d.isValid()) throw ErrorCode::InvalidResponse;
        return d;
    },
    nullptr  // Confusing
);
```

### 9. Use mapParallel() for Independent Operations

```cpp
// ✅ CORRECT - Independent ops can run in parallel
ao::mapParallel(ids, [](int id) {
    return fetchUser(id);
});

// ❌ SUBOPTIMAL - Sequential when could be parallel
ao::map(ids, [](int id) {
    return fetchUser(id);
});
```

### 10. Handle Errors Explicitly

```cpp
// ✅ CORRECT - Always handle errors
operation()
    .then(process)
    .onError([](ErrorCode e) {
        spdlog::error("Failed: {}", e);
    });

// ❌ WRONG - Unhandled errors silently disappear
operation()
    .then(process);
// No error handler - errors are lost!
```

---

## Performance Considerations

### Memory Usage

**Per AsyncOp instance:**
- State object: ~120-200 bytes (depending on `T`)
- Shared via `shared_ptr`: Multiple AsyncOps can share same state

**Optimization tips:**
- Use `onSuccess()`/`onError()` for terminal handlers (saves one allocation)
- Use `std::move()` for large values
- Avoid capturing large objects in lambdas

### Callback Overhead

**Single callback per operation:**
- Unlike JavaScript Promises (multiple `.then()`), AsyncOp supports ONE handler per type
- Prevents handler explosion in memory-constrained environments
- Use `.tap()` for multiple side effects

### Collection Operations

**Performance characteristics:**
- `all()`: O(n) operations, parallel execution
- `any()`: Best case O(1) (first success), worst case O(n)
- `race()`: O(1) (first to finish)
- `map()`: O(n) sequential, one-at-a-time
- `mapParallel()`: O(n) parallel, all-at-once

**Choose wisely:**
```cpp
// Fast - parallel execution
ao::mapParallel(urls, fetchUrl);

// Slow - sequential execution (useful for rate limiting)
ao::map(urls, fetchUrl);
```

### Async Operation Overhead

**Timer creation cost:**
- GLib: `g_timeout_add_full()` ~1-2 μs
- Qt: `new QTimer()` ~2-5 μs

**For high-frequency operations (>1000/sec):**
- Consider batching
- Use object pools for reusable operations
- Profile before optimizing

---

## Integration Guide

### Project Setup

**CMakeLists.txt:**
```cmake
# Required
find_package(PkgConfig REQUIRED)
find_package(spdlog REQUIRED)

# Backend selection
# Option 1: GLib (default)
pkg_check_modules(GLIB REQUIRED glib-2.0)
target_link_libraries(your_app ${GLIB_LIBRARIES})
target_include_directories(your_app PUBLIC ${GLIB_INCLUDE_DIRS})

# Option 2: Qt
# add_definitions(-DASYNC_USE_QT)
# find_package(Qt5 COMPONENTS Core REQUIRED)
# target_link_libraries(your_app Qt5::Core)

# AsyncOp
target_include_directories(your_app PUBLIC ${CMAKE_SOURCE_DIR}/include)
```

### Source Files

Required files:
- `async_op.hpp` - Main AsyncOp implementation
- `async_op_void.hpp` - Specialization for `AsyncOp<void>`
- `ao_event_loop.hpp` - Event loop abstraction

Optional:
- `msg_registry.hpp` - Message-based async operations (for networking)

### Logging Configuration

AsyncOp uses spdlog extensively:

```cpp
#include <spdlog/spdlog.h>

int main() {
    // Set log level (default: info)
    spdlog::set_level(spdlog::level::debug);
    
    // Or disable AsyncOp logging
    spdlog::set_level(spdlog::level::warn);
    
    // Your code...
}
```

### Backend Selection

**GLib (default):**
```cpp
#include "async_op.hpp"  // Uses GLib by default

int main() {
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    
    // Your async operations
    
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
}
```

**Qt:**
```cpp
// In CMakeLists.txt or compiler flags:
// -DASYNC_USE_QT

#include <QApplication>
#include "async_op.hpp"  // Uses Qt backend

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    
    // Your async operations
    
    return app.exec();
}
```

### Custom Event Loop Backend

To add a new backend (e.g., libuv):

1. **Edit `ao_event_loop.hpp`:**

```cpp
#ifdef ASYNC_USE_LIBUV
# include <uv.h>
using timer_type = uv_timer_t*;
#define FORMAT_TIMER(timer) fmt::ptr(timer)
#endif
```

2. **Implement required functions:**
```cpp
timer_type add_timeout(std::chrono::milliseconds timeout, std::function<bool()> cb);
timer_type add_idle(std::function<bool()> cb);
void remove_timeout(timer_type id);
bool is_main_thread();
void invoke_main(std::function<void()> cb);
```

3. **Update format macro and utility functions.**

---

## Additional Utilities

### MessageRegistry (msg_registry.hpp)

For network/IPC operations that need request-response correlation:

```cpp
#include "msg_registry.hpp"

ao::MessageRegistry<ResponseData> registry;

// Send request
ao::AsyncOp<ResponseData> sendRequest(const Request& req) {
    ao::AsyncOp<ResponseData> result;
    int64_t msg_id = registry.registerMessage(
        result.promise(), 
        std::chrono::seconds(5)  // Timeout
    );
    
    sendToNetwork(msg_id, req);
    return result;
}

// Handle response
void onNetworkMessage(int64_t id, const ResponseData& data) {
    registry.handleResponse(id, data);
}
```

**Features:**
- Timestamp-based unique IDs (no collisions across restarts)
- Built-in timeout support
- Thread-safe registration/lookup
- O(1) operations

---

## Troubleshooting

### Common Issues

**1. Callback Never Executes**

Problem:
```cpp
auto op = fetchData();
op.then(handler1);
op.then(handler2);  // Never runs!
```

Solution: Chain operations, don't set multiple handlers
```cpp
fetchData()
    .then(handler1)
    .then(handler2);
```

**2. Memory Leak / Callback Not Cleaned Up**

Problem: Capturing AsyncOp instead of Promise
```cpp
ao::AsyncOp<int> op;
add_timeout(100ms, [op]() {  // ❌ Captured future
    return false;
});
```

Solution: Always capture promise
```cpp
ao::AsyncOp<int> op;
add_timeout(100ms, [promise = op.promise()]() {
    promise->resolveWith(42);
    return false;
});
```

**3. Operation Never Completes**

Problem: Promise never settled
```cpp
ao::AsyncOp<int> fetchData() {
    ao::AsyncOp<int> op;
    // Forgot to settle the promise!
    return op;
}
```

Solution: Always settle the promise
```cpp
ao::AsyncOp<int> fetchData() {
    ao::AsyncOp<int> op;
    add_timeout(100ms, [promise = op.promise()]() {
        promise->resolveWith(42);  // ✅ Settled
        return false;
    });
    return op;
}
```

**4. Timeout Doesn't Work as Expected**

Problem: Timer starts immediately when `.timeout()` is called
```cpp
// Timeout covers BOTH operations
op1().then(op2).timeout(1000ms);
```

Solution: Apply timeout to specific operation
```cpp
// Timeout covers only op2
op1().then([]() { return op2().timeout(1000ms); });
```

**5. Error Silently Disappears**

Problem: No error handler
```cpp
operation()
    .then(process);  // ❌ No error handler
```

Solution: Always add `.onError()`
```cpp
operation()
    .then(process)
    .onError([](ErrorCode e) { 
        spdlog::error("Error: {}", e); 
    });
```

### Debug Tips

**Enable verbose logging:**
```cpp
spdlog::set_level(spdlog::level::trace);
```

**Check operation state:**
```cpp
auto op = fetchData();
spdlog::debug("Op[{}] pending: {}", op.id(), op.isPending());

op.then([](Data d) {
    // ...
});
```

**Log in callbacks:**
```cpp
fetchData()
    .tap([](Data d) { 
        spdlog::debug("Got data: {}", d.size()); 
    })
    .tapError([](ErrorCode e) { 
        spdlog::error("Error occurred: {}", e); 
    });
```

---

## API Summary

### Core Methods

| Method | Purpose | Returns New AsyncOp? | Terminal? |
|--------|---------|---------------------|-----------|
| `.then(f)` | Transform success | ✅ Yes | ❌ No |
| `.recover(f)` | Convert error to success | ✅ Yes | ❌ No |
| `.otherwise(f)` | Branching (alias for recover) | ✅ Yes | ❌ No |
| `.next(s, e)` | Handle both paths | ✅ Yes | ❌ No |
| `.onSuccess(f)` | Terminal success handler | ❌ No | ✅ Yes |
| `.onError(f)` | Terminal error handler | ❌ No | ✅ Yes |
| `.filter(s, e)` | Dual-path filtering | ✅ Yes | ❌ No |
| `.filterSuccess(f)` | Success-only filtering | ✅ Yes | ❌ No |
| `.filterError(f)` | Error-only filtering | ✅ Yes | ❌ No |
| `.tap(f)` | Success side effect | ✅ Yes | ❌ No |
| `.tapError(f)` | Error side effect | ✅ Yes | ❌ No |
| `.finally(f)` | Cleanup (both paths) | ✅ Yes | ❌ No |
| `.timeout(d)` | Add timeout | ✅ Yes | ❌ No |
| `.cancel(e)` | Cancel operation | ❌ No | ❌ No |

### Collection Functions

| Function | Purpose | Completion |
|----------|---------|------------|
| `all()` | Wait for all success | First error |
| `any()` | Race for first success | All fail |
| `race()` | First to settle | First result |
| `allSettled()` | Wait for all (success + error) | All complete |
| `map()` | Sequential transformation | First error |
| `mapParallel()` | Parallel transformation | First error |
| `mapSettled()` | Sequential with all results | All complete |
| `forEach()` | Sequential execution | First error |
| `forEachSettled()` | Sequential with failed items | All complete |

### Utility Functions

| Function | Purpose |
|----------|---------|
| `delay(duration)` | Wait for duration |
| `defer(f)` | Execute sync function async |
| `retry(op, n)` | Retry immediately |
| `retryWithBackoff(op, n, d)` | Retry with exponential backoff |

---

## License

MIT License - See LICENSE file for details

---

## Support & Contributing

- **Issues:** Report bugs or request features on GitHub
- **Documentation:** This guide covers v2.4.1
- **Examples:** See `examples/` directory for more patterns

**Version:** 2.4.1  
**Last Updated:** 2026-02-28  
**Author:** pansz
