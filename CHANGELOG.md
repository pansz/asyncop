# Changelog

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