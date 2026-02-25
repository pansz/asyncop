# Changelog

## [Unreleased]

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

### Fixed
- Corrected member variable ordering in State struct for better memory layout
- Fixed issue where settled operations wouldn't execute callbacks immediately
- Improved error handling in documentation examples
- Removed meta-instructions from documentation that were meant for internal use only