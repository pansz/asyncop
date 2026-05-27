# Changelog

All notable changes to AsyncOp will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

**Latest:** [2.5.2](#252---2026-05-27) - See [README.md](README.md#version) for brief summary.

---

## [2.5.2] - 2026-05-27

### Fixed
- `AsyncOp<T>` now supports non-default-constructible types - `State::result_value` uses `std::optional<T>` internally, removing the requirement for `T` to have a default constructor. `all()` also uses `std::vector<std::optional<T>>` internally before converting to `std::vector<T>`.
- `all()` log message now correctly includes the operation count (was missing `{}` format specifier).
- `then()` now preserves thrown `ErrorCode` — previously, throwing an `ErrorCode` inside a `.then()` handler was caught by `catch (...)` and incorrectly converted to `ErrorCode::Exception`. It now uses `executeProtectedWithErrorCode`, matching the behavior of `recover()`, `filter()`, and `next()`.

### Changed
- Documentation and header comments for `.onError()` now explicitly state that it is a **terminal handler** and must not be followed by `.then()`, `.recover()`, `.timeout()`, etc. Removed the broken `.onError().then()` "Pattern 2" example that would cause downstream chains to hang forever on error.
- Added `REVIEW_CHECKLIST.md` to `.gitignore`.
- Documentation and header comments for `.timeout()` now explicitly warn that it must be called **before** terminal handlers (`.onSuccess()`, `.onError()`). It internally uses `.then()` / `.onError()` to intercept results, so the callback slots must still be available.

### Known Limitations
- `allSettled()` and `mapSettled()` still require `T` to be default-constructible due to `SettledResult<T>` containing a public `T value` member. Changing this would be a breaking API change.

## [2.5.1] - 2026-05-27

### Fixed
- `then()` compilation failure when handler returns `AsyncOp<void>` - Both `AsyncOp<T>::then()` and `AsyncOp<void>::then()` now use compile-time branching to handle `void` return types correctly. Previously, the generic lambda `[](auto v){...}` would fail to compile because `AsyncOp<void>::then()` passes no arguments to the handler.

## [2.5.0] - 2026-05-22

### Fixed
- `next()` void return support - Success and error handlers can now return `void` or `AsyncOp<void>` without compilation errors
- `map()`/`forEach()` dangling reference - Collection functions now take vectors by value to prevent use-after-free when caller's vector goes out of scope
- `MessageRegistry` destructor - Pending timeout timers are now cancelled during destruction to prevent dangling callbacks

### Changed
- **API Cleanup:** Removed `resolve()` and `reject()` from `AsyncOp` public interface. Use `promise()->resolveWith()` and `promise()->rejectWith()` instead
- **Encapsulation:** `m_promise` is now private in both `AsyncOp<T>` and `AsyncOp<void>`. Use the public `promise()` accessor
- Collection functions (`map()`, `forEach()`, `forEachSettled()`, `mapSettled()`, `mapParallel()`) parameter changed from `const std::vector<T>&` to `std::vector<T>` (value semantics)

## [2.4.2] - 2026-05-22

### Fixed
- `next()` static_assert order - Moved `using` declarations into `else` branch and added `safe_invoke_result` helper to prevent hard template errors before `static_assert` fires when `nullptr` is passed
- `IdGen` dead code - Removed unused `last_timestamp_ms_` member and its CAS loop
- `IdGen` counter type - Changed `global_counter_` from `int32_t` to `uint32_t` to avoid implementation-defined behavior on negative bit operations

### Changed
- Documentation: Clarified that `onError()` is a terminal handler and does not propagate errors to subsequent chained operations
- Documentation: Fixed `all()` example to use same-type operations and added note about homogeneous type requirement
- Removed version history comments from `async_op.hpp` header (maintained in CHANGELOG.md only)
- Removed version numbers from `examples/CMakeLists.txt` and `tests/CMakeLists.txt`

## [2.4.1] - 2026-02-27

### Added
- `tapError()` - Execute side effects on error without modifying the error
  - Useful for error logging, metrics collection, and debugging
  - Equivalent to `filterError([](ErrorCode err) { side_effect_fn(err); throw err; })`
  - Exceptions in side effect are caught and logged but don't affect the chain
- `filterSuccess()` - Convenience wrapper for success-only filtering
- `filterError()` - Convenience wrapper for error-only filtering
- Both wrappers provide clearer intent for single-path filtering

### Changed
- Updated `filter()` documentation to feature `filterSuccess()` and `filterError()`
- Updated `recoverFrom()` deprecation notice to reference `filterError()`
- Updated examples to use `filterError()` for error-only filtering
- Suppressed deprecation warnings in tests for deprecated API coverage

### Fixed
- `next()` nullptr handling - Changed `static_assert(false, ...)` to `static_assert(dependent_false_v<T>, ...)` to allow proper `if constexpr` branch compilation

## [2.4.0] - 2026-02-27

### Added
- `cancel()` function to reject pending operations with configurable error code
  - Returns `*this` for chaining
  - No-op if operation already settled
  - Similar semantics to `timeout()` - caller manages underlying resources

- `filter()` function for dual-path success/error filtering
  - Success filter: return value to pass through, throw `ErrorCode` to reject
  - Error filter: return value to recover, throw `ErrorCode` to propagate
  - Use `filterSuccess()` or `filterError()` for single-path filtering
  - Replaces need for separate `recoverFrom()` with more flexible API

### Deprecated
- `orElse()` - Use `otherwise()` with explicit fallback logic instead
- `recoverFrom()` - Use `filterError()` for more flexible error handling

### Changed
- Updated documentation with new API methods and examples

## [2.3.2] - 2026-02-25

### Changed
- Callback overwrite violations now trigger `assert()` in debug builds instead of only logging errors
- Applies to `then()`, `onSuccess()`, `onError()`, `recover()`, `next()`, and `finally()` methods
- Helps catch API misuse early during development (e.g., calling `then()` after `onSuccess()`)
- Release builds with `NDEBUG` continue to log errors without asserting

## [2.3.1] - 2026-02-25

### Fixed
- **Critical race conditions in parallel batch operations** - Fixed thread safety issues in `all()`, `race()`, `any()`, and `allSettled()` functions
  - Changed shared counters from `std::shared_ptr<size_t>` to `std::shared_ptr<std::atomic<size_t>>`
  - Changed shared flags from `std::shared_ptr<bool>` to `std::shared_ptr<std::atomic<bool>>`
  - Used proper atomic operations (`fetch_add`, `compare_exchange_strong`) with appropriate memory ordering
  - Prevents undefined behavior when multiple callbacks complete concurrently

## [2.3.0] - 2026-02-25

### Added
- `onSuccess()` function for terminal success handling (similar to `onError()` but for success cases)
- Comprehensive documentation for the new `onSuccess()` function
- Chaining rules and constraints section in documentation

### Changed
- Updated callback overwrite protection mechanism to prevent accidental overwrites
- Modified error messages to be more informative when callback overwrites are prevented
- Renamed `getError()` to `errorCode()` for consistency
- Improved documentation with more examples and clearer explanations
- Enhanced build configuration to support both Qt5 and GLib backends properly
- Updated project structure documentation to match actual directory layout
- Changed warning levels from `debug`/`warn` to `error` for overwrite protection violations
- Bumped version from 2.2 to 2.3

### Fixed
- Corrected member variable ordering in State struct for better memory layout
- Fixed issue where settled operations wouldn't execute callbacks immediately
- Improved error handling in documentation examples
- Removed meta-instructions from documentation that were meant for internal use only

## [2.2.0] - 2026-02-13

### Added
- Initial release of AsyncOp library
- Promise/Future pattern for embedded Linux systems
- Support for both GLib and Qt event loops
- Message registry for request/response patterns
- Comprehensive test suite
- Examples for callback conversion