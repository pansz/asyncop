# AsyncOp - Lightweight Promise/Future for Embedded Systems

> Modern C++ async programming with chainable operations, powerful error recovery, and comprehensive collection utilities.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Backend](https://img.shields.io/badge/Backend-GLib%20%7C%20Qt-green.svg)](https://github.com/)

## Overview

AsyncOp is a lightweight C++ library that brings Promise/Future pattern to embedded Linux systems. Designed for applications with moderate memory constraints (64MB+ RAM), it provides a clean, chainable API for managing asynchronous operations without the complexity of callback hell.

### Features

- **Promise/Future semantics** - Clear separation between producer (Promise) and consumer (AsyncOp/Future)
- **Chainable operations** - Elegant `.then()`, `.recover()`, `.next()` patterns
- **Robust error handling** - Multiple recovery strategies and error propagation
- **Collection operations** - Sequential and parallel processing of async operations
- **Thread-safe** - Safe worker thread integration with main event loop marshaling
- **Embedded-friendly** - Minimal overhead, designed for constrained environments
- **Dual backend support** - Works with both GLib and Qt event loops

## Installation

### Requirements
- C++17 or later
- GLib 2.0+ or Qt 5.12+ (event loop backend)
- spdlog (for logging)
- Modern compiler (GCC 7+, Clang 5+)

### Basic Setup

```cpp
#include "async_op.hpp"

// For Qt backend: define ASYNC_USE_QT in CMakeLists.txt or compiler option
```

## Quick Start

### Basic Usage

```cpp
#include "async_op.hpp"
#include <spdlog/spdlog.h>

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

### Error Recovery

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

### Collection Operations

```cpp
// Parallel processing
std::vector<int> userIds = {1, 2, 3, 4, 5};

ao::mapParallel(userIds, [](int id) {
    return fetchUserAsync(id);
})
.then([](std::vector<User> users) {
    spdlog::info("Loaded {} users in parallel", users.size());
    displayUsers(users);
});
```

## Core Concepts

### Promise/Future Model

- **`AsyncOp<T>`** = **Future** (consumer side)
  - Provides methods to react to results: `.then()`, `.onError()`, `.recover()`
  - Returned from async functions
  - Can be copied and shared safely

- **`Promise<T>`** = **Promise** (producer side)
  - Type alias for `std::shared_ptr<AsyncOp<T>::State>`
  - Provides methods to produce results: `.resolveWith()`, `.rejectWith()`
  - Captured in async callbacks to settle the operation
  - Obtained via `.promise()` method on AsyncOp

### The Five Core Methods

| Method | Purpose | When Used | Continues Chain? |
|--------|---------|-----------|------------------|
| **`.then()`** | Transform success values | Success path | ✅ Yes (can change type) |
| **`.next()`** | Handle both paths | Dual processing | ✅ Yes (converges types) |
| **`.recover()`** / **`.otherwise()`** | Convert errors to success | Error recovery | ✅ Yes (error→value) |
| **`.onError()`** | Final error handling | Error path (terminal) | ❌ No |
| **`.tap()`** | Side effects | Debugging/logging | ✅ Yes (value unchanged) |

## Thread Safety

AsyncOp provides safe integration between worker threads and the main event loop:

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

    worker.detach();
    return future;
}
```

## Documentation

For complete documentation, see [docs/async_op_doc.md](docs/async_op_doc.md) which includes:

- Detailed API reference
- Advanced usage patterns
- Collection operations guide
- Thread safety guidelines
- Performance tips
- Troubleshooting

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests if applicable
5. Submit a pull request

## Building and Running

### Prerequisites
- C++17 compatible compiler (GCC 7+, Clang 5+)
- CMake 3.10+
- GLib 2.0+ or Qt 5.12+ development libraries
- spdlog library

### Build Instructions
```bash
mkdir build
cd build
cmake ..
make
```

### Running Tests
```bash
# Qt backend tests
./tests/test_asyncop_qt

# GLib backend tests  
./tests/test_asyncop_glib
```

### Running Examples
```bash
# Qt backend examples
./examples/example_callback_conversion_qt
./examples/example_message_registry_qt

# GLib backend examples
./examples/example_callback_conversion_glib
./examples/example_message_registry_glib
```

## License

MIT License - see [LICENSE](LICENSE) for details.

## Acknowledgments

AsyncOp was designed to bring modern async programming patterns to embedded systems while maintaining efficiency and reliability. Special thanks to the C++ community for inspiration on Promise/Future implementations.