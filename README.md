# AsyncOp - Lightweight Asynchronous Operation Chaining for C++17

> Elegant Promise/Future pattern bringing modern async programming to C++17 with minimal overhead

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Backend](https://img.shields.io/badge/Backend-GLib%20%7C%20Qt-green.svg)](https://github.com/pansz/asyncop)

---

## What is AsyncOp?

AsyncOp is a lightweight C++ library that provides Promise/Future semantics for asynchronous programming. It eliminates callback hell through chainable operations while maintaining minimal memory footprint and CPU overhead.

**Perfect for:**
- Event-driven applications (GLib/Qt)
- Resource-constrained environments
- Network and I/O operations
- Embedded Linux systems
- Any C++17 codebase needing clean async patterns

---

## Features at a Glance

✨ **Modern API** - Clean, chainable syntax inspired by JavaScript Promises  
🔗 **Type-Safe Chaining** - Transform `AsyncOp<T>` → `AsyncOp<U>` seamlessly  
🛡️ **Robust Error Handling** - Multiple recovery strategies with automatic propagation  
🔄 **Collection Operations** - Process multiple async operations (sequential or parallel)  
🧵 **Thread-Safe** - Safe worker thread integration with main event loop marshaling  
⚡ **Zero Dependencies** - Only requires C++17, spdlog, and GLib/Qt (for event loop)  
📦 **Embedded-Friendly** - Optimized memory layout, minimal allocations  
🎯 **Single Responsibility** - One callback per operation (prevents handler explosion)

---

## Quick Example

```cpp
#include "async_op.hpp"

// Fetch user, then posts, with fallback
fetchUserAsync(userId)
    .then([](User user) {
        return fetchPostsAsync(user.id);  // Chain async operations
    })
    .recover([](ao::ErrorCode err) {
        return getCachedPosts();  // Fallback on error
    })
    .then([](std::vector<Post> posts) {
        displayPosts(posts);
    })
    .onError([](ao::ErrorCode err) {
        spdlog::error("Failed: {}", err);
    });
```

**That's it!** No nested callbacks, clean error handling, easy to read and maintain.

---

## Installation

### Requirements
- **C++17** or later
- **Event Loop**: GLib 2.0+ (default) or Qt 5.12+ (define `ASYNC_USE_QT`)
- **spdlog** (includes fmt for formatting)

### Quick Setup

```bash
# Clone repository
git clone https://github.com/pansz/asyncop.git
cd asyncop

# Build
mkdir build && cd build
cmake ..
make

# Run examples
./build/examples/example_basic_glib
```

**CMakeLists.txt Integration:**
```cmake
# Add AsyncOp headers
include_directories(${CMAKE_SOURCE_DIR}/include)

# Link dependencies (GLib example)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
find_package(spdlog REQUIRED)

target_link_libraries(your_app ${GLIB_LIBRARIES} spdlog::spdlog)
```

---

## API Highlights

### Creating Async Operations

```cpp
ao::AsyncOp<Data> fetchDataAsync() {
    auto promise = ao::makePromise<Data>();
    
    // Schedule async work
    ao::add_timeout(100ms, [promise]() {
        promise->resolveWith(Data{42});
        return false;
    });
    
    return ao::AsyncOp<Data>(promise);
}
```

### Chaining Operations

```cpp
fetchValue()
    .then([](int x) { return x * 2; })           // Transform
    .then([](int x) { return std::to_string(x); }) // Change type
    .then([](std::string s) { 
        spdlog::info("Result: {}", s); 
    });
```

### Error Recovery

```cpp
fetchFromServer()
    .recover([](ao::ErrorCode err) {
        return getCachedData();  // Error → Success
    })
    .then([](Data d) { 
        processData(d);  // Handles both server and cache results
    });
```

### Parallel Collection Processing

```cpp
std::vector<int> ids = {1, 2, 3, 4, 5};

ao::mapParallel(ids, [](int id) {
    return fetchUserAsync(id);
})
.then([](std::vector<User> users) {
    displayUsers(users);
});
```

### Side Effects & Debugging

```cpp
operation()
    .tap([](Data d) { 
        spdlog::debug("Got: {} bytes", d.size()); 
    })
    .tapError([](ao::ErrorCode e) { 
        metrics.increment("errors"); 
    })
    .then([](Data d) { return process(d); });
```

---

## Core Methods

| Method | Purpose | Example |
|--------|---------|---------|
| `.then(f)` | Transform success value | `.then([](int x) { return x * 2; })` |
| `.recover(f)` | Convert error to success | `.recover([](ErrorCode e) { return fallback(); })` |
| `.next(s, e)` | Handle both paths | `.next(onSuccess, onError)` |
| `.onError(f)` | Terminal error handler | `.onError([](ErrorCode e) { log(e); })` |
| `.tap(f)` | Side effect (success) | `.tap([](T v) { log(v); })` |
| `.tapError(f)` | Side effect (error) | `.tapError([](ErrorCode e) { metrics++; })` |
| `.timeout(d)` | Add timeout | `.timeout(5000ms)` |
| `.filter(s, e)` | Validate/filter paths | `.filter(validate, recover)` |
| `.finally(f)` | Cleanup handler | `.finally([]() { cleanup(); })` |

### Collection Operations

| Function | Behavior | Use Case |
|----------|----------|----------|
| `all(ops)` | Wait for all success | Batch operations |
| `any(ops)` | First success wins | Redundant servers |
| `race(ops)` | First to settle wins | Timeout pattern |
| `allSettled(ops)` | Wait for all (success + error) | Best-effort batch |
| `map(items, f)` | Sequential transform | Rate-limited processing |
| `mapParallel(items, f)` | Parallel transform | Fast batch loading |

---

## Why AsyncOp?

### Problem: Callback Hell

```cpp
// ❌ Traditional callback style
fetchUser(userId, [](User user) {
    fetchPosts(user.id, [](Posts posts) {
        fetchComments(posts[0].id, [](Comments comments) {
            display(comments);
        }, [](Error e) { handleError(e); });
    }, [](Error e) { handleError(e); });
}, [](Error e) { handleError(e); });
```

### Solution: AsyncOp Chaining

```cpp
// ✅ Clean, readable, maintainable
fetchUser(userId)
    .then([](User u) { return fetchPosts(u.id); })
    .then([](Posts p) { return fetchComments(p[0].id); })
    .then([](Comments c) { display(c); })
    .onError([](ErrorCode e) { handleError(e); });
```

---

## Design Philosophy

AsyncOp is built with these principles:

1. **Minimal Overhead** - Optimized memory layout, efficient allocations
2. **Resource Awareness** - Provides sequential operations for constrained environments
3. **Type Safety** - Compile-time type checking, no runtime type erasure surprises
4. **Single Responsibility** - One callback per operation prevents memory explosion
5. **Error Transparency** - Errors visible and handleable at every stage
6. **Event Loop Agnostic** - Works with GLib, Qt, or custom event loops

**Memory Optimization Example:**
```cpp
// ✅ Efficient - uses onSuccess() (no unused AsyncOp allocation)
op.then(process).onSuccess([](Result r) { display(r); });

// ❌ Less efficient - creates unused AsyncOp
op.then(process).then([](Result r) { display(r); });
```

---

## Documentation

📖 **[Complete Documentation](docs/async_op_doc.md)** - Comprehensive guide with:
- Detailed API reference for all methods
- Advanced patterns (retry, branching, conditional execution)
- Collection operations guide
- Thread safety & integration
- Performance considerations
- Troubleshooting guide

🚀 **[Examples](examples/)** - Working code samples:
- Basic chaining and error handling
- Network request patterns
- Message registry (request-response correlation)
- Worker thread integration
- Collection operations

---

## Thread Safety

AsyncOp is designed for single-threaded event loops but provides safe cross-thread integration:

```cpp
ao::AsyncOp<Result> computeInBackground() {
    auto promise = ao::makePromise<Result>();
    
    std::thread([promise]() {
        Result r = heavyComputation();
        
        // Marshal back to main thread
        ao::invoke_main([promise, r]() {
            promise->resolveWith(r);
        });
    }).detach();
    
    return ao::AsyncOp<Result>(promise);
}
```

**Thread-safe operations:**
- Creating AsyncOp/Promise ✅
- Querying state (`isPending()`, etc.) ✅
- `add_timeout()`, `add_idle()`, `invoke_main()` ✅

**Main thread required:**
- Settling promises (`resolveWith()`, `rejectWith()`) ⚠️
- Executing callbacks ⚠️

---

## Backend Support

### GLib (Default)
```cpp
#include "async_op.hpp"

int main() {
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    
    // Your async operations
    
    g_main_loop_run(loop);
}
```

### Qt
```cpp
// Define ASYNC_USE_QT before including
#include <QCoreApplication>
#include "async_op.hpp"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    
    // Your async operations
    
    return app.exec();
}
```

### Custom Event Loop
Extend `ao_event_loop.hpp` with your backend. See [Integration Guide](docs/async_op_doc.md#integration-guide).

---

## Performance Characteristics

**Memory per AsyncOp:**
- State object: ~120-200 bytes (varies with `T`)
- Shared via `shared_ptr`: Multiple AsyncOps can share state

**Operation overhead:**
- Timer creation: ~1-5 μs (GLib/Qt)
- Callback invocation: ~100 ns

**Collection operations:**
- `all()`, `mapParallel()`: O(n) parallel
- `map()`, `forEach()`: O(n) sequential
- `any()`, `race()`: O(1) best case

**Optimization tips:**
- Use `onSuccess()`/`onError()` for terminal handlers (saves allocation)
- Use `std::move()` for large objects
- Prefer `mapParallel()` for independent operations
- Use `map()` for rate-limited sequential processing

---

## Comparison with Other Libraries

| Feature | AsyncOp | Folly Futures | Boost.Asio | JavaScript Promises |
|---------|---------|---------------|------------|---------------------|
| **C++ Version** | C++17 | C++14 | C++11 | N/A |
| **Dependencies** | Minimal | Heavy | Moderate | N/A |
| **Memory Footprint** | Small | Large | Medium | N/A |
| **Event Loop** | GLib/Qt | Custom | Built-in | Browser/Node |
| **Learning Curve** | Low | Medium | High | Low |
| **Embedded-Friendly** | ✅ Yes | ❌ No | ⚠️ Maybe | N/A |

---

## Building & Testing

### Build Options

```bash
# GLib backend (default)
cmake ..
make

# Qt backend
cmake -DASYNC_USE_QT=ON ..
make

# With examples
cmake -DBUILD_EXAMPLES=ON ..
make

# With tests
cmake -DBUILD_TESTS=ON ..
make test
```

### Running Tests

```bash
# All tests
ctest

# Specific backend (after running `make`)
./build/tests/test_asyncop_glib
./build/tests/test_asyncop_qt
```

---

## Examples

### Basic Chaining
```cpp
fetchUser(id)
    .then([](User u) { return fetchProfile(u.profileId); })
    .then([](Profile p) { displayProfile(p); })
    .onError([](ErrorCode e) { spdlog::error("Error: {}", e); });
```

### Retry Pattern
```cpp
ao::retryWithBackoff<Data>(
    []() { return fetchFromAPI(); },
    3,      // max attempts
    1000ms  // initial delay (exponential backoff)
)
.then([](Data d) { processData(d); });
```

### Timeout Pattern
```cpp
fetchFromServer()
    .timeout(5000ms)
    .recover([](ErrorCode e) {
        if (e == ErrorCode::Timeout) {
            return getCachedData();
        }
        throw e;
    });
```

### Parallel Batch Processing
```cpp
std::vector<URL> urls = getURLs();

ao::mapParallel(urls, [](URL url) {
    return fetchURL(url);
})
.then([](std::vector<Data> results) {
    spdlog::info("Fetched {} URLs", results.size());
});
```

---

## Project Structure

```
asyncop/
├── include/
│   ├── async_op.hpp           # Main AsyncOp implementation
│   ├── async_op_void.hpp      # Specialization for AsyncOp<void>
│   ├── ao_event_loop.hpp      # Event loop abstraction (GLib/Qt)
│   └── msg_registry.hpp       # Message-based async tracker (optional)
├── docs/
│   └── async_op_doc.md        # Complete documentation
├── examples/
│   ├── CMakeLists.txt
│   ├── example_callback_conversion.cpp
│   ├── example_message_registry.cpp
│   └── example_qt_http.cpp
├── tests/
│   ├── CMakeLists.txt
│   ├── test_asyncop.cpp       # Shared test implementation
│   ├── testmain_glib.cpp      # GLib backend test entry point
│   └── testmain_qt.cpp        # Qt backend test entry point
├── CMakeLists.txt             # Root CMake configuration
├── README.md                  # Project overview
├── LICENSE                    # MIT license
├── CHANGELOG.md               # Version history
├── CONTRIBUTING.md            # Contribution guidelines
└── CLAUDE.md                  # Development guidelines
```

---

## Contributing

We welcome contributions! Please:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

**Areas we'd love help with:**
- Additional event loop backends (libuv, asio, etc.)
- Performance optimizations
- More examples and use cases
- Documentation improvements

---

## License

MIT License - See [LICENSE](LICENSE) file for details.

---

## Acknowledgments

AsyncOp was inspired by:
- JavaScript Promises - for elegant chaining API
- Folly Futures - for collection operations
- Boost.Asio - for async I/O patterns
- The C++ community - for ongoing feedback and ideas

Special thanks to all contributors and users who have helped shape this library.

---

## Version

**Current Version:** 2.4.1  
**Release Date:** 2026-02-28  
**Author:** pansz

**Changelog:**
- v2.4.1: Added `filterSuccess()`, `filterError()`, improved documentation
- v2.4.0: Added `filter()`, `cancel()` methods
- v2.3.0: Added collection operations (`all()`, `any()`, `map()`, etc.)
- v2.2.0: Added `timeout()`, `tap()`, `finally()`
- v2.1.0: Initial public release

---

## Support

- 📖 **Documentation**: [docs/async_op_doc.md](docs/async_op_doc.md)
- 🐛 **Issues**: [GitHub Issues](https://github.com/pansz/asyncop/issues)
- 💬 **Discussions**: [GitHub Discussions](https://github.com/pansz/asyncop/discussions)
- ✉️ **Email**: pan.sz@outlook.com

---

<div align="center">

**⭐ Star this repo if AsyncOp helps your project! ⭐**

Made with ❤️ for the C++ community

</div>