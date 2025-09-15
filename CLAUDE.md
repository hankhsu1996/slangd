# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build and Test Commands

- **Build everything**: `bazel build //...`
- **Run all tests**: `bazel test //...`
- **Build with configuration**: `bazel build //... --config=debug` (or `release`, `fastbuild`)
- **Run specific test**: `bazel test //test/slangd:document_manager_test`
- **Generate compile_commands.json**: `bazel run @hedron_compile_commands//:refresh_all`
- **Format code**: `find src include test -name "*.cpp" -o -name "*.hpp" | xargs clang-format -i`

## CRITICAL: Always Format Before Commit

**MANDATORY**: Run formatter before EVERY commit to avoid CI failures:

```bash
find src include test -name "*.cpp" -o -name "*.hpp" | xargs clang-format -i
```

## Requirements

- Bazel 7.0+ with bzlmod support
- Clang 19+ for C++23 features

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
- Core managers handle configuration, documents, and workspace operations
- Feature providers implement specific LSP capabilities (diagnostics, symbols, definitions)

### Key Managers

- **ConfigManager**: Handles slangd configuration files
- **DocumentManager**: Manages open documents and their Slang compilation units
- **WorkspaceManager**: Workspace-level file operations and indexing
- **SymbolIndex**: Repository-wide symbol indexing for cross-file navigation

### Dependencies

- **Slang**: SystemVerilog compiler frontend for parsing and semantics
- **jsonrpc**: Custom JSON-RPC library with ASIO integration
- **ASIO**: Asynchronous I/O with C++20 coroutines
- **spdlog**: Structured logging
- **yaml-cpp**: Configuration file parsing

### Coding Standards

Follow Google C++ Style Guide with these specifics:

- Use C++23 features, avoid macros
- ASIO coroutines with strands for synchronization (no mutexes/futures)
- `std::expected` for error handling over exceptions
- Trailing return types (`auto Foo() -> Type`) for clarity
- Smart pointers and RAII for memory management

### Current LSP Features

- Diagnostics for syntax/semantic errors (single file scope)
- Document symbols for SystemVerilog modules and packages
- Basic go-to-definition support
- Repository-wide symbol indexing (infrastructure complete, not yet exposed via LSP)

The architecture enables future language servers (e.g. VHDL) to reuse the generic `lsp` core.

## Branch Naming Conventions

Use these prefixes for branch names:

- `feature/` - for new features
- `refactor/` - for code restructuring
- `bugfix/` - for bug fixes
- `docs/` - for documentation changes
- `chore/` - for maintenance tasks

## Pre-Commit Requirements

**IMPORTANT**: Before committing any code changes:

1. **Format code**: Run `clang-format -i <modified-files>` on all changed files
2. **Build check**: Ensure `bazel build //...` passes
3. **Test check**: Ensure `bazel test //...` passes

## Git Commit Message Rules

1. **First line**: Short summary (50-72 characters max)
2. **Body**: Use bullet points with `-` for multiple changes/reasons
   - Wrap lines at 72 characters
   - Explain what and why, not how

**Example format:**

```
Short summary under 72 chars

- First change or reason explained
- Second change or reason explained
```

**Note**: Claude Code attribution goes in PR descriptions, not individual commits

## Pull Request Rules

**Title:**

- Short summary (50-72 chars max)
- Match branch naming style if helpful

**Description Structure:**

```markdown
## Summary

- Brief bullet points of main changes
- Use `-` for consistency

## Additional sections as needed:

- Changes (for complex PRs)
- Breaking Changes (if applicable)
- Notes (context, decisions, etc.)

🤖 Generated with [Claude Code](https://claude.ai/code)
```

**Guidelines:**

- Always include Summary
- Add other sections only when they add value
- Keep it concise and relevant to the specific change

## Current Development Focus

**Next Task: SourceExplorer Refactor**

The `SourceExplorer` is a foundational service that provides file discovery and configuration reading for the new architecture. It's orthogonal to compilation and will be shared by both Global Index Service and Overlay Session Service.

**Key Responsibilities:**

- Config file reading (`.slangd` files, defines, include paths)
- File discovery with repository scanning and ignore rules
- Supply file sets to compilation services
- Handle configuration changes and file system events

**Architecture Position:**

```
SourceExplorer → supplies file lists and config → GlobalIndexService
SourceExplorer → supplies file lists and config → OverlaySessionService
```

This refactor prepares the foundation for Phase 1 (Global Index Service) and Phase 2 (Overlay Session Service).

## Configuration Notes

- Use `CLAUDE.local.md` for local development notes and debugging details
