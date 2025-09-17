# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Requirements

- Bazel 7.0+ with bzlmod support
- Clang 19+ for C++23 features

## Build and Test Commands

- **Build everything**: `bazel build //...`
- **Run all tests**: `bazel test //...`
- **Build with configuration**: `bazel build //... --config=debug` (or `release`, `fastbuild`)
- **Generate compile_commands.json**: `bazel run @hedron_compile_commands//:refresh_all`
- **Format code**: `find src include test -name "*.cpp" -o -name "*.hpp" | xargs clang-format -i`

## Development Workflow

### Pre-Commit Process

Before adding/committing changes:

1. **Format code**: `find src include test -name "*.cpp" -o -name "*.hpp" | xargs clang-format -i`
2. **Build check**: Ensure `bazel build //...` passes
3. **Test check**: Ensure `bazel test //...` passes

### Branch Naming

Use these prefixes:

- `feature/` - for new features
- `refactor/` - for code restructuring
- `bugfix/` - for bug fixes
- `docs/` - for documentation changes
- `chore/` - for maintenance tasks

### Git Commit Messages

1. **First line**: Short summary (50-72 characters max)
2. **Body**: Use bullet points with `-`, keep concise based on change scale
   - Wrap lines at 72 characters
   - Explain what and why, not how
   - Focus on concrete technical changes and their purpose
3. **No attribution**: Do not include Claude Code attribution in commits

**Example format:**

```
Short summary under 72 chars

- Primary change explained
- Secondary change if needed
```

### Pull Request Guidelines

**Title:**

- Short summary (50-72 chars max)
- Match branch naming style if helpful

**Description:**

```markdown
## Summary

- Brief bullet points of main changes
- Use `-` for consistency

🤖 Generated with [Claude Code](https://claude.ai/code)
```

**Guidelines:**

- Always include Summary section
- For small changes with no test modifications, keep description minimal
- Add additional sections (Breaking Changes, Notes) only when they add value
- Keep concise and relevant to the specific change

## Architecture

This is a SystemVerilog Language Server Protocol (LSP) implementation with a modular design separating generic LSP functionality from language-specific features.

### Core Components

**LSP Core Library (`lsp/`)**

- Language-agnostic LSP protocol implementation built on JSON-RPC
- Base `LspServer` class with virtual handlers for LSP lifecycle and features
- Document management and ASIO coroutine-based async operations

**Slangd Server (`slangd/`)**

- SystemVerilog LSP server extending the generic `LspServer`
- Uses Slang library for SystemVerilog parsing and semantic analysis
- Built on GlobalCatalog + OverlaySession architecture for performance and correctness
- Clean service-oriented design with semantic indexing

### Core Architecture

- **LanguageService**: Main LSP service coordinating catalog and overlay sessions
- **GlobalCatalog**: Long-lived compilation extracting packages and interfaces from disk files
- **OverlaySession**: Per-request compilation (1-5ms) combining current buffer with catalog metadata
- **ProjectLayoutService**: Configuration management and intelligent file discovery
- **Semantic Indexes**: DefinitionIndex, DiagnosticIndex, and SymbolIndex for LSP queries

### Dependencies

- **Slang**: SystemVerilog compiler frontend for parsing and semantics
- **jsonrpc**: Custom JSON-RPC library with ASIO integration
- **ASIO**: Asynchronous I/O with C++20 coroutines
- **spdlog**: Structured logging
- **yaml-cpp**: Configuration file parsing

### Current LSP Features

**Core Functionality (Production Ready):**

- **Diagnostics**: Syntax and semantic errors with cross-file context
- **Go-to-Definition**: Symbol navigation across packages and interfaces
- **Document Symbols**: Hierarchical outline of SystemVerilog modules, packages, and interfaces
- **Cross-File Support**: Package imports and interface references work correctly

**Architecture Benefits:**

- **Performance**: 1-5ms response times with bounded memory usage
- **Reliability**: Always-correct single-file features with robust cross-file support
- **Maintainability**: Clean service architecture ready for future enhancements

The modular architecture enables future language servers (e.g. VHDL) to reuse the generic `lsp` core.

## Coding Standards

Follow Google C++ Style Guide with these specifics:

- Use C++23 features, avoid macros
- ASIO coroutines with strands for synchronization (no mutexes/futures)
- `std::expected` for error handling over exceptions
- Trailing return types (`auto Foo() -> Type`) for clarity
- Smart pointers and RAII for memory management

## Configuration Notes

- Use `CLAUDE.local.md` for local development notes and debugging details

## Documentation Guidelines

- Use standard markdown formatting with regular bullets (-) and numbered lists (1.)
- Avoid special Unicode characters that may not render correctly in all markdown engines
