# Changelog

All notable changes to the slangd language server will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0-beta.1] - 2025-12-21

First beta release with stability improvements and bug fixes.

### Changed

- Update SystemVerilog syntax highlighting with improved grammar
- Update jsonrpc-cpp-lib to v2.1.2

### Fixed

- Fix use-after-free in BroadcastEvent when destroyed with pending work
- Fix file watcher registration conflicts in multi-workspace setup
- Fix enum value lookup in parameterized covergroup bins
- Fix slow diagnostics on file save by removing premature overlay rebuild

## [0.1.0-alpha.1] - 2025-11-02

Initial alpha release.

### Added

- Diagnostics: Real-time syntax and semantic error detection
- Go-to-definition: Navigate to symbol definitions across files
- Document symbols: Outline view for modules, classes, packages, and other constructs
- Workspace indexing: Cross-file symbol resolution
- `.slangd` YAML configuration file support
- Auto-discovery of SystemVerilog files with selective directory scanning
- Explicit file lists via `Files` and `FileLists` options
- Path filtering with regex-based include/exclude patterns
- Include directory and preprocessor define specification

[0.1.0-beta.1]: https://github.com/hankhsu1996/slangd/releases/tag/v0.1.0-beta.1
[0.1.0-alpha.1]: https://github.com/hankhsu1996/slangd/releases/tag/v0.1.0-alpha.1
